/* Firmware simulator for host-in-the-loop tests (sim_v2.py, software/ufi_host/tests/test_sim.py).
 *
 * Production code that runs here natively, unmodified: the v2 dispatcher (ufi_v2.c), the stream
 * encoder (ufi_stream.c), the write path (ufi_write.c with ufi_write_stream.c, ufi_write_wave.c,
 * ufi_write_codec.c, ufi_verify.c, ufi_raw_verify.c, ufi_mfm.c), the same HAL-free modules as the
 * v2 fuzz target, and from units that are not built as a whole (sim_v2.py takes the text from the
 * source): the READ and WRITE completions of ufi_usb.c, the session write lock of ufi_board.c,
 * ufi_capture_abort() of ufi_main.c and the pin table of board_headless.c.  Every other driver
 * call goes to a generated stub that returns 0 / false / OK (fuzz_stubs.py with the constant
 * oracle below).
 *
 * Modelled here, because it is hardware or lives in a unit that is not built:
 *   time        1 ms per main-loop pass = 275000 flux-timer ticks; TIM2->CNT follows.  A wait
 *               loop on HAL_GetTick() or ufi_flux_now64() gets 1 ms for every 4096 polls.
 *               HAL_Delay() advances the millisecond clock only.
 *   transport   what the firmware transmits is collected; 'B' makes the transmitter busy.
 *               Host bytes arrive in packets of at most 512 bytes; after ufi_v2_rx() returned
 *               false the next packet waits for ufi_native_rx_resume() (OUT endpoint not armed).
 *   capture     a synthetic track that the "drive" delivers as time passes (READ).
 *   GPIO        HAL_GPIO_WritePin() sets or clears the pin in the port's ODR; the WGATE level is
 *               read back with the production helper bus_out_state(&PIN_FDD_WGATE).
 *   write enable  ufi_board_write_enable() opens and closes the firmware write enable (WLOCK_FW).
 *               Counted as a violation: WGATE rising while it is closed, and the enable closing
 *               while WGATE is still asserted.
 *   locks, medium  hardware lock open (ufi_board_jumper_locked() is a generated stub), no write
 *               protect, the medium never changes (media token 1).  The session lock is
 *               production code and engage-only: nothing but a new process clears it.
 *               ufi_lan_write_allowed() (lan_write=1 in LAN.CFG and the lock switch open, in
 *               ufi_lan.c) is the flag 'A' sets; off at the start, like the default LAN.CFG.
 *   index       one pulse every rev_ticks ('S'), free running from the start, 'I' stops and
 *               resumes the pulses.  As ufi_flux_tim2_irq() / ufi_flux_index_event() do: a pulse
 *               calls ufi_write_index_handler(t) while TIM2 CC2IE is set and a write waits or
 *               runs; TIM2 compare 4 calls ufi_write_delayed_start() when CNT reaches CCR4 with
 *               CC4IE set, and clears CC4IE.  The capture model keeps its own index times.
 *   TIM3        counts while CR1.CEN is set.  ARR is taken as preloaded (ufi_write_init() asks
 *               for it through HAL_TIM_PWM_Init(), a stub): the running period is shadow ARR + 1
 *               ticks; an update event loads the shadow from ARR (and CCR1), sets SR.UIF and
 *               raises the DMA request if DIER.UDE is set.  The shadow first loads where the
 *               firmware writes TIM_EGR_UG (hook UFI_STUB_TIM_UG_HOOK of the HAL stub header:
 *               plain memory would keep no trace of that write).  CEN set again without a new
 *               update generation restarts the period from the shadow value.
 *   DMA1 stream 1  HAL_DMA_Start_IT() records memory, peripheral register and item count, sets
 *               NDTR and EN | TCIE | TEIE.  One request copies one halfword to the register, or
 *               through TIM3->DMAR a burst of DCR.DBL + 1 halfwords to the registers from
 *               DCR.DBA on; NDTR counts down and reloads at 0 (circular), HTIF1 at half, TCIF1
 *               at the end.  HAL_DMA_Init() is a generated stub (HAL_OK).
 *   interrupts  after an update event: ufi_write_dma_irq() while an enabled HT/TC flag is
 *               pending, then ufi_write_tim3_irq() if UIE and UIF (the order of the NVIC for
 *               equal priority: DMA1_Stream1 is IRQ 12, TIM3 is IRQ 29).  Registers are plain
 *               memory, so the model applies what the handlers wrote: bits written to
 *               DMA1->LIFCR are cleared in LISR after every handler and before the next
 *               interrupt is chosen; TIM3->SR keeps only UIF after the handler (it clears UIF
 *               by writing ~UIF).
 *   disk        every TIM3 period that ended while WGATE was asserted and the write enable
 *               open is recorded ('R'): that is what reached the disk.
 * Not modelled: the read-back of a verifying write (the capture model does not return what was
 * written), write protect, head steps, Apple drives.
 *
 * The Python host talks to this process over stdin/stdout, so one test runs
 * request -> dispatcher -> stream encoder / write path -> event queue -> frames -> host parser
 * with nothing faked in between.  Commands (little endian), each answered with u32 n + n
 * output bytes:
 *   'W' u16 n, bytes   bytes from the host (any fragmentation), delivered through ufi_v2_rx()
 *                      as far as the OUT endpoint takes them, the rest in later 'T' passes
 *   'T' u16 k          k main-loop passes, 1 ms each: hardware model, then ufi_v2_service(),
 *                      ufi_write_process(), ufi_write_service(), ufi_stream_service() in the
 *                      order of ufi_main_loop(); the answer is what the firmware transmitted
 *   'S' u32 rev_ticks, u32 cell_ticks, u32 lead_ms   capture model for the next READ;
 *                      rev_ticks is also the period of the free-running index
 *   'I' u8 on          index pulses of the free-running index on / off (off: no disk)
 *   'A' u8 on          disk writes over LAN allowed (ufi_lan_write_allowed())
 *   'G'                ground truth of the last capture: u8 revs, (revs+1) u32 index times,
 *                      u32 n, n u32 transition times (flux-timer values)
 *   'R'                what reached the disk since WGATE last rose: u32 WGATE rising edges
 *                      since start, u32 time of the last rising edge, u32 time of the last
 *                      falling edge (flux-timer values), u32 periods elapsed, u32 n stored
 *                      (at most 131072), n x (u32 period ticks, u16 CCR1, u8 OC1M mode
 *                      (CCMR1 bits 6..4 at the end of the period: 6 PWM1, 7 PWM2, 4 forced
 *                      inactive = no pulse), u8 0)
 *   'B' u8 busy        the transmitter reports "not idle" while busy != 0 (slow host)
 *   'L' u8 type, u8 cmd, u8 seq, u16 n, bytes   one frame from the LAN session (ufi_v2_lan_frame)
 *   'C'                the LAN connection is gone (ufi_v2_lan_closed)
 *   'X'                the USB host dropped the session (ufi_v2_clear_comms)
 *   'Z'                state: u8 owner, u8 route, u8 capture state, u8 write pending,
 *                      u8 write enable open, u8 WGATE asserted, u8 write state
 *                      (ufi_write_get_state()), u32 write-enable violations (see above)
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
#define TICKS_PER_MS 275000u
#define TIME_BASE 0xFFF00000u                       /* the flux timer wraps during the test */
static uint32_t sim_ms;
static uint64_t sim_ticks;                          /* flux-timer ticks since the start */
static uint32_t now_ticks = TIME_BASE;              /* TIME_BASE + sim_ticks, 32 bit as TIM2->CNT */
static unsigned polls;                              /* time reads since time last advanced */
static bool in_hw;                                  /* inside hw_run(): handlers do not advance time */
static uint8_t out[1u << 21];
static uint32_t out_n;
static bool tx_busy;
static void hw_run(uint32_t ticks);
static void poll_wait(void)
{
    if (in_hw || ++polls < 4096u) return;
    polls = 0;
    sim_ms += 1u;
    hw_run(TICKS_PER_MS);
}
uint32_t HAL_GetTick(void) { poll_wait(); return sim_ms; }
void HAL_Delay(uint32_t ms) { sim_ms += ms; }
uint64_t ufi_flux_now64(void) { poll_wait(); return (uint64_t)TIME_BASE + sim_ticks; }
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
/* OUT endpoint: host bytes wait here and reach ufi_v2_rx() in packets of at most 512 bytes.
 * When ufi_v2_rx() returns false the endpoint is not re-armed and the rest stays with the host
 * until the firmware calls ufi_native_rx_resume(), as on the bus. */
static uint8_t host_q[1u << 20];
static uint32_t host_head, host_tail;
static bool rx_hold;
void ufi_native_rx_resume(void) { rx_hold = false; }
static void usb_out(void)
{
    while (!rx_hold && host_tail < host_head) {
        const uint32_t n = host_head - host_tail < 512u ? host_head - host_tail : 512u;
        if (!ufi_v2_rx(host_q + host_tail, n)) rx_hold = true;
        host_tail += n;
    }
    if (host_tail == host_head) host_tail = host_head = 0;
}
static bool lan_up;
bool ufi_lan_connected(void) { return lan_up; }
static bool lan_write;
bool ufi_lan_write_allowed(void) { return lan_write; }
/* providers with an (out, capacity) contract: the generated stub would leave the length unset */
int ufi_file_read(const char* name, uint32_t offset, uint8_t* o, uint16_t len, uint16_t* got)
{
    (void)name; (void)offset; (void)o; (void)len; *got = 0; return UFI_ERR_NOT_FOUND;
}
int ufi_file_dir(const char* path, uint16_t start, uint8_t* o, uint16_t max, uint16_t* len)
{
    (void)path; (void)start; (void)o; (void)max; *len = 0; return UFI_OK;
}

/* ---- medium: one disk that never changes ------------------------------------------ */
uint32_t ufi_drive_media_token(void) { return 1u; }
bool ufi_drive_media_matches(uint32_t token) { return token == 1u; }

/* ---- GPIO, write enable, WGATE, what reached the disk ------------------------------ */
#define REC_MAX (1u << 17)
static struct { uint32_t period; uint16_t ccr; uint8_t mode; } rec[REC_MAX];
static uint32_t rec_n, rec_total;
/* calls of ufi_write_tim3_irq() since WGATE rose; stale: the update interrupt was enabled during
 * this very update, so the call was caused by a flag that was already pending (TIM3 UIF) */
static uint32_t tim3_irqs, tim3_irqs_stale;
static bool enable_open;
static uint32_t enable_violations, gate_rises, gate_on_time, gate_off_time;
static bool wgate(void) { return bus_out_state(&PIN_FDD_WGATE); }
void HAL_GPIO_WritePin(GPIO_TypeDef* port, uint16_t pin, GPIO_PinState state)
{
    if (!port) return;
    const bool was = wgate();
    if (state == GPIO_PIN_SET) port->ODR |= pin;
    else port->ODR &= ~(uint32_t)pin;
    const bool is = wgate();
    if (is && !was) {
        gate_rises++;
        gate_on_time = now_ticks;
        rec_n = rec_total = 0;
        tim3_irqs = tim3_irqs_stale = 0;
        if (!enable_open) enable_violations++;
    } else if (was && !is) {
        gate_off_time = now_ticks;
    }
}
void ufi_board_write_enable(bool on)
{
    enable_open = on;
    if (!on && wgate()) enable_violations++;
}

/* ---- TIM3 and DMA1 stream 1 (see the header comment) -------------------------------- */
static bool tim3_running;
static uint64_t tim3_next;                          /* sim_ticks of the next update event */
static uint32_t tim3_arr, tim3_ccr;                 /* shadow registers of the running period */
static struct {
    DMA_Stream_TypeDef* stream;
    const uint16_t* mem;
    volatile uint32_t* periph;
    uint32_t total, pos;
} dma;

/* The HAL takes addresses as uint32_t.  On a 64-bit host the upper half is lost in the
 * firmware's cast; it is the same as that of any static object of this program (`ref`). */
static uintptr_t widen(uint32_t low, uintptr_t ref)
{
#if UINTPTR_MAX > 0xFFFFFFFFu
    uintptr_t p = (ref & ~(uintptr_t)0xFFFFFFFFu) | low;
    if (p > ref && p - ref > 0x80000000u) p -= (uintptr_t)1 << 32;
    else if (ref > p && ref - p > 0x80000000u) p += (uintptr_t)1 << 32;
    return p;
#else
    (void)ref;
    return low;
#endif
}

#define DMA_FLAGS1 (DMA_LISR_FEIF1 | DMA_LISR_DMEIF1 | DMA_LISR_TEIF1 | DMA_LISR_HTIF1 | DMA_LISR_TCIF1)
HAL_StatusTypeDef HAL_DMA_Start_IT(DMA_HandleTypeDef* hdma, uint32_t source, uint32_t destination, uint32_t length)
{
    dma.stream = hdma->Instance;
    dma.mem = (const uint16_t*)widen(source, (uintptr_t)zero_buf);
    dma.periph = (volatile uint32_t*)widen(destination, (uintptr_t)TIM3);
    dma.total = length;
    dma.pos = 0;
    DMA1->LISR &= ~DMA_FLAGS1;
    dma.stream->NDTR = length;
    dma.stream->CR |= DMA_SxCR_TCIE | DMA_SxCR_TEIE | DMA_SxCR_EN;
    return HAL_OK;
}

/* called where ufi_write.c writes TIM_EGR_UG to TIM3->EGR (UFI_STUB_TIM_UG_HOOK) */
uint32_t sim_tim_ug(void)
{
    tim3_arr = TIM3->ARR & 0xFFFFu;
    tim3_ccr = TIM3->CCR1 & 0xFFFFu;
    return 0x0001u;
}

static void tim3_sync(void)
{
    const bool cen = (TIM3->CR1 & TIM_CR1_CEN) != 0;
    if (cen && !tim3_running) tim3_next = sim_ticks + tim3_arr + 1u;
    tim3_running = cen;
}

static void dma_request(void)
{
    const bool burst = dma.periph == &TIM3->DMAR;
    const uint32_t base = TIM3->DCR & 0x1Fu;
    const uint32_t n = burst ? ((TIM3->DCR >> 8) & 0x1Fu) + 1u : 1u;
    if (burst && base + n > sizeof(TIM_TypeDef) / sizeof(uint32_t)) return;
    volatile uint32_t* dst = burst ? (volatile uint32_t*)TIM3 + base : dma.periph;
    for (uint32_t i = 0; i < n; i++) {
        dst[burst ? i : 0u] = dma.mem[dma.pos++];
        uint32_t left = dma.stream->NDTR - 1u;
        if (left == dma.total / 2u) DMA1->LISR |= DMA_LISR_HTIF1;
        if (left == 0) {
            DMA1->LISR |= DMA_LISR_TCIF1;
            left = dma.total;
            dma.pos = 0;
        }
        dma.stream->NDTR = left;
    }
}

static void dma_flags(void)
{
    DMA1->LISR &= ~DMA1->LIFCR;
    DMA1->LIFCR = 0;
}

static void tim3_update(void)
{
    const bool uie_before = (TIM3->DIER & TIM_DIER_UIE) != 0;
    if (wgate() && enable_open) {
        if (rec_n < REC_MAX) {
            rec[rec_n].period = tim3_arr + 1u;
            rec[rec_n].ccr = (uint16_t)((TIM3->CCMR1 & TIM_CCMR1_OC1PE) ? tim3_ccr : TIM3->CCR1);
            rec[rec_n].mode = (uint8_t)((TIM3->CCMR1 >> 4) & 7u);
            rec_n++;
        }
        rec_total++;
    }
    tim3_arr = TIM3->ARR & 0xFFFFu;
    tim3_ccr = TIM3->CCR1 & 0xFFFFu;
    tim3_next = sim_ticks + tim3_arr + 1u;
    TIM3->SR |= TIM_SR_UIF;
    dma_flags();
    if ((TIM3->DIER & TIM_DIER_UDE) && dma.stream && (dma.stream->CR & DMA_SxCR_EN) && dma.total) dma_request();
    for (unsigned i = 0; i < 4u && dma.stream; i++) {
        const uint32_t cr = dma.stream->CR, flags = DMA1->LISR;
        if (!(((flags & DMA_LISR_HTIF1) && (cr & DMA_SxCR_HTIE)) || ((flags & DMA_LISR_TCIF1) && (cr & DMA_SxCR_TCIE)))) break;
        ufi_write_dma_irq();
        dma_flags();
    }
    TIM3->SR &= TIM_SR_UIF;                 /* a handler may have written ~UIF (rc_w0) */
    if ((TIM3->DIER & TIM_DIER_UIE) && (TIM3->SR & TIM_SR_UIF)) {
        tim3_irqs++;
        if (!uie_before) tim3_irqs_stale++;
        ufi_write_tim3_irq();
        TIM3->SR &= TIM_SR_UIF;
        dma_flags();
    }
}

/* ---- free-running index, TIM2 compare 4, and the event loop ------------------------- */
static uint32_t rev_ticks = 5500000u;
static uint64_t index_next = 5500000u;
static bool index_on = true;

static void set_time(uint64_t t)
{
    sim_ticks = t;
    now_ticks = TIME_BASE + (uint32_t)t;
    TIM2->CNT = now_ticks;
}

static void hw_run(uint32_t ticks)
{
    const uint64_t end = sim_ticks + ticks;
    in_hw = true;
    polls = 0;
    dma_flags();
    tim3_sync();
    for (;;) {
        uint64_t at = end;
        int what = 0;
        if (index_next <= at) { at = index_next; what = 1; }
        if (TIM2->DIER & TIM_DIER_CC4IE) {
            const uint64_t c = sim_ticks + (uint32_t)(TIM2->CCR4 - now_ticks);
            if (c < at) { at = c; what = 2; }
        }
        if (tim3_running && tim3_next < at) { at = tim3_next; what = 3; }
        set_time(at);
        if (what == 0) break;
        if (what == 1) {
            index_next += rev_ticks ? rev_ticks : 55000000u;
            const write_state_t ws = ufi_write_get_state();
            if (index_on && (TIM2->DIER & TIM_DIER_CC2IE) && (ws == WRITE_WAITING_INDEX || ws == WRITE_ACTIVE))
                ufi_write_index_handler(now_ticks);
        } else if (what == 2) {
            TIM2->DIER &= ~TIM_DIER_CC4IE;
            ufi_write_delayed_start(TIM2->CNT);
        } else {
            tim3_update();
        }
        dma_flags();
        tim3_sync();
    }
    in_hw = false;
}

/* ---- synthetic capture ------------------------------------------------------------- */
#define STORE_WORDS (1u << 18)
#define MAX_REVS 16u
static uint32_t store[STORE_WORDS];
static uint32_t total, cell_ticks = 1100u, lead_ms = 3u;
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
    set_time(0);
    /* what board_gpio_init() leaves behind for the two write gates: released */
    bus_out(&PIN_FDD_WGATE, false);
    HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_SET);
    ufi_write_init();
    ufi_v2_init();
    ufi_v2_reset();
    ufi_job_reset();
    ufi_resources_reset();
    ufi_cancel_clear();
    for (;;) {
        uint8_t c, b2[2];
        if (!get(&c, 1) || c == 'Q') return 0;
        polls = 0;
        if (c == 'W') {
            if (!get(b2, 2)) return 2;
            const uint16_t n = (uint16_t)(b2[0] | b2[1] << 8);
            if (host_head + n > sizeof host_q) { fprintf(stderr, "sim: host queue overflow\n"); exit(3); }
            if (!get(host_q + host_head, n)) return 2;
            host_head += n;
            usb_out();
        } else if (c == 'T') {
            if (!get(b2, 2)) return 2;
            for (uint16_t k = (uint16_t)(b2[0] | b2[1] << 8); k; k--) {
                sim_ms += 1u;
                hw_run(TICKS_PER_MS);
                usb_out();
                capture_step();
                ufi_v2_service();
                ufi_write_process();
                ufi_write_service();
                if (ufi_write_get_state() != WRITE_VERIFYING) ufi_stream_service();
            }
        } else if (c == 'S') {
            uint8_t p[12];
            if (!get(p, 12)) return 2;
            memcpy(&rev_ticks, p, 4); memcpy(&cell_ticks, p + 4, 4); memcpy(&lead_ms, p + 8, 4);
        } else if (c == 'A') {
            uint8_t v;
            if (!get(&v, 1)) return 2;
            lan_write = v != 0;
        } else if (c == 'I') {
            uint8_t v;
            if (!get(&v, 1)) return 2;
            index_on = v != 0;
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
            host_tail = host_head = 0;      /* what the host had not delivered yet is gone with it */
            ufi_v2_clear_comms();
        } else if (c == 'Z') {
            out[out_n++] = ufi_v2_owner();
            out[out_n++] = ufi_v2_route();
            out[out_n++] = (uint8_t)g_capture.state;
            out[out_n++] = ufi_v2_write_pending() ? 1u : 0u;
            out[out_n++] = enable_open ? 1u : 0u;
            out[out_n++] = wgate() ? 1u : 0u;
            out[out_n++] = (uint8_t)ufi_write_get_state();
            put32(enable_violations);
        } else if (c == 'G') {
            out[out_n++] = revs_req;
            for (unsigned k = 0; k < idx_total; k++) put32(idx_t[k]);
            put32(total);
            for (uint32_t i = 0; i < total; i++) put32(store[i]);
        } else if (c == 'R') {
            put32(gate_rises);
            put32(gate_on_time);
            put32(gate_off_time);
            put32(rec_total);
            put32(rec_n);
            for (uint32_t i = 0; i < rec_n; i++) {
                put32(rec[i].period);
                out[out_n++] = (uint8_t)rec[i].ccr;
                out[out_n++] = (uint8_t)(rec[i].ccr >> 8);
                out[out_n++] = rec[i].mode;
                out[out_n++] = 0;
            }
            put32(tim3_irqs);
            put32(tim3_irqs_stale);
        } else {
            return 2;
        }
        answer();
    }
}
