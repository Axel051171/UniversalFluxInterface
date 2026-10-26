/* LibFuzzer harness for the UFI v2 dispatcher (ufi_v2.c compiled as a whole, with the
 * real job/resource/cancel/capability/media/codec/quality modules; every driver call
 * goes to a generated stub, see fuzz_stubs.py).
 *
 * Input layout: byte 0 = mode.  Odd mode: the rest is raw serial traffic.  Even mode:
 * records of [flags][cmd][seq][len lo][len hi][payload], framed with a correct CRC:
 *   flags bit0 DATA frame instead of REQUEST, bit1 corrupt the CRC, bit2 run the main
 *   loop after this record, bit3 report an operation end (bit4: WRITE instead of READ),
 *   bit5 queue an event with code = cmd (bit7: blocking event), bit6 clear comms.
 * Mode bit1: Greaseweazle owns the drive resources for this input.
 * Mode bit2 (L4): records with an odd seq arrive from the LAN session (ufi_v2_lan_frame,
 * already CRC-checked there), the rest over USB; flags bit6 then ends the LAN session
 * (ufi_v2_lan_closed) instead of clearing the USB comms.
 * The tail of the input feeds the stub oracle (fuzz_oracle.h), so driver results are
 * fuzzed too.  Checks: ASan/UBSan, every emitted frame is well formed, and the dispatcher
 * never hangs (libFuzzer -timeout). */
#include "ufi_firmware.h"
#include "ufi_cancel.h"
#include "ufi_job.h"
#include "ufi_resources.h"
#include "ufi_lan.h"
#include "fuzz_oracle.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

/* ---- oracle ---------------------------------------------------------------- */
static const uint8_t* tail;
static size_t tail_n;
static uint32_t prng;
static uint8_t zero_buf[8192];

uint8_t fuzz_byte(void)
{
    if (tail_n) return tail[--tail_n];
    prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5;   /* never a constant */
    return (uint8_t)prng;
}
bool fuzz_bit(void) { return (fuzz_byte() & 1u) != 0; }
uint16_t fuzz_u16(void) { return (uint16_t)fuzz_u32(); }
uint32_t fuzz_u32(void)
{
    const uint8_t b = fuzz_byte();
    switch (b >> 6) {
        case 0: return b & 63u;
        case 1: return 0xFFFFFFFFu - (b & 63u);
        case 2: return (uint32_t)(b & 63u) << 8;
        default: return 1u << (b & 31u);
    }
}
int fuzz_err(void)
{
    const uint8_t b = fuzz_byte();
    return b < 22u ? -(int)b : 0;
}
void* fuzz_zero_buffer(void) { return zero_buf; }
const char* fuzz_string(void) { return ""; }

/* ---- time and transport doubles (not generated: they need behaviour) ---------- */
static uint32_t ticks;
static uint64_t flux_now;
uint32_t HAL_GetTick(void)
{
    ticks += 1u;
    if (fuzz_byte() == 0x7Fu) ticks += 20000u;      /* occasionally cross the 10 s / 30 s limits */
    return ticks;
}
dump_status_t ufi_dump_status(void)
{
    dump_status_t s;
    memset(&s, 0, sizeof s);
    ((uint8_t*)&s)[0] = fuzz_byte() & 15u;          /* changing state -> dump events */
    return s;
}
void HAL_Delay(uint32_t ms) { ticks += ms; }
uint64_t ufi_flux_now64(void) { return flux_now += 1u << 20; }
static uint32_t flux_words[2048];
uint32_t* ufi_flux_store(uint32_t* words)   /* bounded store instead of an unset size */
{
    *words = (uint32_t)(fuzz_u16() % 2049u);
    return flux_words;
}
bool ufi_native_tx_room(void) { return true; }
bool ufi_native_tx_idle(void) { return true; }

/* callees whose contract bounds a returned length by a capacity argument: the generic
 * stub would hand back any number and the overflow would be the stub's, not the firmware's */
uint16_t ufi_quality_report(uint8_t* out, uint16_t capacity)
{
    const uint16_t n = (uint16_t)(fuzz_u16() % (capacity + 1u));
    memset(out, 0xA5, n);
    return n;
}
int ufi_file_read(const char* name, uint32_t offset, uint8_t* out, uint16_t len, uint16_t* got)
{
    (void)name; (void)offset;
    *got = (uint16_t)(fuzz_u16() % (len + 1u));
    memset(out, 0x5A, *got);
    return fuzz_err();
}
int ufi_file_dir(const char* path, uint16_t start, uint8_t* out, uint16_t max, uint16_t* len)
{
    (void)path; (void)start;
    *len = (uint16_t)(fuzz_u16() % (max + 1u));
    memset(out, 0x3C, *len);
    return fuzz_err();
}

static uint16_t crc16(const uint8_t* p, uint32_t n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        uint8_t x = (uint8_t)((crc >> 8) ^ *p++);
        x ^= (uint8_t)(x >> 4);
        crc = (uint16_t)((crc << 8) ^ ((uint16_t)x << 12) ^ ((uint16_t)x << 5) ^ x);
    }
    return crc;
}

static void check_emitted(const uint8_t* f, uint32_t n)
{
    assert(n >= UFI_V2_HDR + 2u && n <= UFI_V2_FRAME_MAX);
    assert(f[0] == 0x55u && f[1] == 0xAAu);
    assert(f[2] >= UFI_V2_REPLY && f[2] <= UFI_V2_END);
    const uint32_t len = f[6] | (uint32_t)f[7] << 8;
    assert(len <= UFI_V2_MAX_PAYLOAD && n == UFI_V2_HDR + len + 2u);
    assert((f[n - 2] | (uint32_t)f[n - 1] << 8) == crc16(&f[2], 6u + len));
}
int ufi_native_tx_blocking(const uint8_t* d, uint32_t n)
{
    check_emitted(d, n);
    return fuzz_byte() == 0xFFu ? UFI_ERR_USB : UFI_OK;
}
int ufi_native_tx_start(const uint8_t* d, uint32_t n)
{
    check_emitted(d, n);
    return fuzz_byte() == 0xFFu ? UFI_ERR_USB : UFI_OK;
}

/* ---- one input -------------------------------------------------------------- */
int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    static uint8_t frame[UFI_V2_FRAME_MAX];
    if (n < 2) return 0;
    tail = d; tail_n = n; prng = 0x9E3779B9u ^ (uint32_t)n; ticks = 0; flux_now = 0;
    memset(zero_buf, 0, sizeof zero_buf);
    ufi_v2_init();
    ufi_v2_reset();
    ufi_job_reset();
    ufi_resources_reset();
    ufi_cancel_clear();
    assert(!ufi_v2_active() && !ufi_v2_write_pending() && !ufi_v2_owner() && ufi_v2_route() == UFI_ROUTE_USB);
    if (d[0] & 2u) ufi_resources_acquire(UFI_OWNER_GW);    /* Greaseweazle holds the drive */

    size_t i = 1;
    if (d[0] & 1u) {
        ufi_v2_rx(d + i, (uint32_t)(n - i));
        for (int k = 0; k < 4; k++) ufi_v2_service();
        return 0;
    }
    while (i + 5 <= n) {
        const uint8_t flags = d[i], cmd = d[i + 1], seq = d[i + 2];
        uint32_t len = (d[i + 3] | (uint32_t)d[i + 4] << 8) & 0x1FFFu;
        i += 5;
        const uint32_t avail = (uint32_t)(n - i);
        const bool lan = (d[0] & 4u) && (seq & 1u);
        if (flags & 0x08u) {
            /* internal API: the firmware's end payloads are a few bytes (verify level, clamp count) */
            uint32_t l = len > avail ? avail : len;
            ufi_v2_end((flags & 0x10u) ? UFI_V2_WRITE : UFI_V2_READ, fuzz_err(), d + i, (uint16_t)(l > 64u ? 64u : l));
        } else if (flags & 0x20u) {
            uint32_t l = len > avail ? avail : len;
            if (flags & 0x80u) ufi_v2_event_now(cmd, d + i, (uint16_t)(l > 8u ? 8u : l));
            else ufi_v2_event(cmd, d + i, (uint16_t)(l > 64u ? 64u : l));
        } else if (flags & 0x40u) {
            if (lan) ufi_v2_lan_closed();               /* TCP connection gone */
            else ufi_v2_clear_comms();                  /* host set 10000 baud: drop the session */
            len = 0;
        } else if (lan) {
            if (len > avail) len = avail;
            if (len > UFI_V2_MAX_PAYLOAD) len = UFI_V2_MAX_PAYLOAD;
            memcpy(frame + UFI_V2_HDR, d + i, len);     /* request() may write the payload */
            ufi_v2_lan_frame((flags & 1u) ? UFI_V2_DATA : UFI_V2_REQUEST, cmd, seq, frame + UFI_V2_HDR, (uint16_t)len);
        } else if (len > UFI_V2_MAX_PAYLOAD) {
            /* oversize length field: header only, the parser must reject and resync */
            uint8_t hdr[UFI_V2_HDR + 2];
            memset(hdr, 0, sizeof hdr);
            hdr[0] = 0x55; hdr[1] = 0xAA; hdr[2] = UFI_V2_REQUEST; hdr[3] = cmd; hdr[4] = seq;
            hdr[6] = (uint8_t)len; hdr[7] = (uint8_t)(len >> 8);
            ufi_v2_rx(hdr, sizeof hdr);
            len = 0;
        } else {
            if (len > avail) len = avail;
            memcpy(frame + UFI_V2_HDR, d + i, len);
            const uint32_t total = ufi_v2_frame(frame, (flags & 1u) ? UFI_V2_DATA : UFI_V2_REQUEST,
                                                cmd, seq, 0, (uint16_t)len);
            if (flags & 2u) frame[total - 1u] ^= 1u;
            ufi_v2_rx(frame, total);
        }
        i += len;
        if (flags & 4u) ufi_v2_service();
    }
    for (int k = 0; k < 4; k++) ufi_v2_service();
    assert(ufi_v2_owner() <= UFI_ROUTE_LAN && ufi_v2_route() != UFI_ROUTE_NONE);
    return 0;
}
