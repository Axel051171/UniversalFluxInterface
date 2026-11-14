/* Firmware simulator for host-in-the-loop tests (sim_v2.py, software/ufi_host/tests/test_sim.py).
 *
 * The production v2 dispatcher (ufi_v2.c), the stream encoder (ufi_stream.c) and the READ
 * completion of ufi_usb.c run natively, with the same real HAL-free modules as the v2 fuzz
 * target; every other driver call goes to a generated stub that returns 0 / false / OK
 * (fuzz_stubs.py with the constant oracle below).  What needs behaviour is modelled here:
 * time, the transport, and a synthetic capture that the "drive" delivers as time passes.
 *
 * The Python host talks to this process over stdin/stdout, so one test runs
 * request -> dispatcher -> stream encoder -> event queue -> frames -> host parser with nothing
 * faked in between.  Commands (little endian), each answered with u32 n + n output bytes:
 *   'W' u16 n, bytes   bytes arrive from the host (any fragmentation), as ufi_v2_rx()
 *   'T' u16 k          k main-loop passes, 1 ms each: capture model, ufi_v2_service(),
 *                      ufi_stream_service(); the answer is what the firmware transmitted
 *   'S' u32 rev_ticks, u32 cell_ticks, u32 lead_ms   capture model for the next READ
 *   'G'                ground truth of the last capture: u8 revs, (revs+1) u32 index times,
 *                      u32 n, n u32 transition times (flux-timer values)
 *   'B' u8 busy        the transmitter reports "not idle" while busy != 0 (slow host)
 *   'L' u8 type, u8 cmd, u8 seq, u16 n, bytes   one frame from the LAN session (ufi_v2_lan_frame)
 *   'C'                the LAN connection is gone (ufi_v2_lan_closed)
 *   'X'                the USB host dropped the session (ufi_v2_clear_comms)
 *   'Z'                state: owner, route, capture state, write pending, u32 write-enable openings
 *   'Q'                quit
 */
#include "ufi_firmware.h"
#include "ufi_cancel.h"
#include "ufi_job.h"
#include "ufi_resources.h"
#include "ufi_lan.h"
#include "fuzz_oracle.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

extern capture_context_t g_capture;     /* defined by the generated stubs (sim_v2.py EXTRA) */

/* ---- constant oracle: every generated stub returns 0 / false / UFI_OK ---------- */
static uint8_t zero_buf[8192];
uint8_t fuzz_byte(void) { return 0; }
bool fuzz_bit(void) { return false; }
uint16_t fuzz_u16(void) { return 0; }
uint32_t fuzz_u32(void) { return 0; }
int fuzz_err(void) { return 0; }
void* fuzz_zero_buffer(void) { return zero_buf; }
const char* fuzz_string(void) { return ""; }

/* ---- time and transport ---------------------------------------------------------- */
static uint32_t sim_ms;
static uint32_t now_ticks = 0xFFF00000u;            /* the flux timer wraps during the test */
static uint8_t out[1u << 20];
static uint32_t out_n;
static bool tx_busy;
uint32_t HAL_GetTick(void) { return sim_ms; }
void HAL_Delay(uint32_t ms) { sim_ms += ms; }
bool ufi_native_tx_room(void) { return !tx_busy; }
bool ufi_native_tx_idle(void) { return !tx_busy; }
static int emit(const uint8_t* d, uint32_t n)
{
    if (tx_busy) return UFI_ERR_BUSY;
    if (out_n + n > sizeof out) { fprintf(stderr, "sim: output overflow\n"); exit(3); }
    memcpy(out + out_n, d, n);
    out_n += n;
    return UFI_OK;
}
int ufi_native_tx_blocking(const uint8_t* d, uint32_t n) { return emit(d, n); }
int ufi_native_tx_start(const uint8_t* d, uint32_t n) { return emit(d, n); }
static bool lan_up;
static uint32_t write_enables;
bool ufi_lan_connected(void) { return lan_up; }
void ufi_board_write_enable(bool on) { if (on) write_enables++; }   /* must never open in a read-only run */
/* providers with an (out, capacity) contract: the generated stub would leave the length unset */
int ufi_file_read(const char* name, uint32_t offset, uint8_t* o, uint16_t len, uint16_t* got)
{
    (void)name; (void)offset; (void)o; (void)len; *got = 0; return UFI_ERR_NOT_FOUND;
}
int ufi_file_dir(const char* path, uint16_t start, uint8_t* o, uint16_t max, uint16_t* len)
{
    (void)path; (void)start; (void)o; (void)max; *len = 0; return UFI_OK;
}

/* ---- synthetic capture ------------------------------------------------------------- */
#define STORE_WORDS (1u << 18)
#define MAX_REVS 16u
static uint32_t store[STORE_WORDS];
static uint32_t total, rev_ticks = 5500000u, cell_ticks = 1100u, lead_ms = 3u;
static uint32_t idx_t[MAX_REVS + 1u];
static uint8_t idx_total, revs_req;
static uint32_t lcg;
static uint32_t rnd(void) { lcg = lcg * 1103515245u + 12345u; return lcg >> 8; }

int ufi_capture_start_q(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks, uint8_t quarter)
{
    (void)period_ticks;
    if (revolutions > MAX_REVS) revolutions = MAX_REVS;
    memset(&g_capture, 0, sizeof g_capture);
    g_capture.current_track = track;
    g_capture.current_side = side;
    g_capture.current_quarter = quarter ? (uint8_t)(0x80u | (quarter << 4)) : 0u;
    g_capture.revolutions_requested = revolutions;
    g_capture.state = CAPTURE_WAITING_INDEX;
    revs_req = revolutions;
    idx_total = (uint8_t)(revolutions + 1u);
    const uint32_t i0 = now_ticks + lead_ms * 275000u;
    for (unsigned k = 0; k < idx_total; k++) idx_t[k] = i0 + k * rev_ticks + k * 777u;
    /* transitions from index 0 to a little past the last index: 2, 3 or 4 cells apart with
     * jitter, some long gaps (three-byte and five-byte codes), one exactly on index 1 */
    lcg = 12345u + track * 131u + side;
    total = 0;
    uint32_t t = i0 + rnd() % cell_ticks;
    const uint32_t end = idx_t[idx_total - 1u] + 4u * cell_ticks;
    bool on_index = false;
    while ((int32_t)(end - t) > 0 && total < STORE_WORDS) {
        if (!on_index && idx_total > 1u && (int32_t)(t - idx_t[1]) >= 0) { t = idx_t[1]; on_index = true; }
        store[total++] = t;
        const uint32_t c = rnd() % 64u;
        t += c == 0 ? 70000u + rnd() % 400000u : c < 4u ? 4000u + rnd() % 60000u
             : cell_ticks * (2u + rnd() % 3u) + rnd() % 41u - 20u;
    }
    return UFI_OK;
}
static uint8_t index_seen(void)
{
    uint8_t n = 0;
    while (n < idx_total && (int32_t)(now_ticks - idx_t[n]) >= 0) n++;
    return n;
}
static void capture_step(void)
{
    if (g_capture.state == CAPTURE_WAITING_INDEX && index_seen() > 0) g_capture.state = CAPTURE_RUNNING;
    if (g_capture.state == CAPTURE_RUNNING && index_seen() == idx_total) g_capture.state = CAPTURE_COMPLETE;
}
uint32_t ufi_flux_written(void)
{
    if (g_capture.state == CAPTURE_IDLE || g_capture.state == CAPTURE_WAITING_INDEX) return 0;
    uint32_t n = 0;
    while (n < total && (int32_t)(now_ticks - store[n]) >= 0) n++;
    return n;
}
uint32_t ufi_flux_now(void) { return now_ticks; }
uint32_t* ufi_flux_store(uint32_t* words) { *words = STORE_WORDS; return store; }
uint8_t ufi_flux_index_count(void) { return (g_capture.state == CAPTURE_IDLE) ? 0u : index_seen(); }
uint32_t ufi_flux_index_time(uint8_t k) { return idx_t[k < idx_total ? k : idx_total - 1u]; }
capture_state_t ufi_flux_poll(void) { return g_capture.state; }
int ufi_flux_capture_stop(void) { g_capture.state = CAPTURE_IDLE; return UFI_OK; }
int ufi_capture_abort(void) { g_capture.state = CAPTURE_IDLE; return UFI_OK; }
bool ufi_operation_busy(void) { return g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING; }

/* ---- command loop ------------------------------------------------------------------- */
static bool get(void* p, size_t n) { return fread(p, 1, n, stdin) == n; }
static void answer(void)
{
    const uint8_t h[4] = { (uint8_t)out_n, (uint8_t)(out_n >> 8), (uint8_t)(out_n >> 16), (uint8_t)(out_n >> 24) };
    fwrite(h, 1, 4, stdout);
    fwrite(out, 1, out_n, stdout);
    fflush(stdout);
    out_n = 0;
}
static void put32(uint32_t v) { for (unsigned i = 0; i < 4; i++) out[out_n++] = (uint8_t)(v >> (8u * i)); }

int main(void)
{
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    static uint8_t in[65536];
    ufi_v2_init();
    ufi_v2_reset();
    ufi_job_reset();
    ufi_resources_reset();
    ufi_cancel_clear();
    for (;;) {
        uint8_t c, b2[2];
        if (!get(&c, 1) || c == 'Q') return 0;
        if (c == 'W') {
            if (!get(b2, 2)) return 2;
            const uint16_t n = (uint16_t)(b2[0] | b2[1] << 8);
            if (!get(in, n)) return 2;
            ufi_v2_rx(in, n);
        } else if (c == 'T') {
            if (!get(b2, 2)) return 2;
            for (uint16_t k = (uint16_t)(b2[0] | b2[1] << 8); k; k--) {
                sim_ms += 1u;
                now_ticks += 275000u;
                capture_step();
                ufi_v2_service();
                ufi_stream_service();
            }
        } else if (c == 'S') {
            uint8_t p[12];
            if (!get(p, 12)) return 2;
            memcpy(&rev_ticks, p, 4); memcpy(&cell_ticks, p + 4, 4); memcpy(&lead_ms, p + 8, 4);
        } else if (c == 'B') {
            uint8_t v;
            if (!get(&v, 1)) return 2;
            tx_busy = v != 0;
        } else if (c == 'L') {
            uint8_t h[5];
            if (!get(h, 5)) return 2;
            const uint16_t n = (uint16_t)(h[3] | h[4] << 8);
            if (n > UFI_V2_MAX_PAYLOAD || !get(in, n)) return 2;
            lan_up = true;
            ufi_v2_lan_frame(h[0], h[1], h[2], in, n);
        } else if (c == 'C') {
            lan_up = false;
            ufi_v2_lan_closed();
        } else if (c == 'X') {
            ufi_v2_clear_comms();
        } else if (c == 'Z') {
            out[out_n++] = ufi_v2_owner();
            out[out_n++] = ufi_v2_route();
            out[out_n++] = (uint8_t)g_capture.state;
            out[out_n++] = ufi_v2_write_pending() ? 1u : 0u;
            put32(write_enables);
        } else if (c == 'G') {
            out[out_n++] = revs_req;
            for (unsigned k = 0; k < idx_total; k++) put32(idx_t[k]);
            put32(total);
            for (uint32_t i = 0; i < total; i++) put32(store[i]);
        } else {
            return 2;
        }
        answer();
    }
}
