/**
 * UFI Flux Engine - streamed, compact capture transfer (UFI_CMD_READ_TRACK)
 *
 * READ_TRACK_RAW sends 4 bytes per flux transition after the capture.  Here the main loop
 * encodes what DMA has already stored while the disk is still turning, so USB transfer and
 * rotation overlap, and the encoding halves the data (lossless, full 275 MHz resolution).
 *
 * Stream bytes (deltas in TIM2 ticks, time 0 = first index pulse):
 *   0x01..0xEF            flux transition, delta = byte
 *   0xF0..0xFC, b         flux transition, delta = 240 + ((byte - 0xF0) << 8) + b  (240..3567)
 *   0xFD, u32 LE          index pulse, u32 = ticks after the previous flux transition
 *   0xFE, u32 LE          flux transition, delta = u32 (0 or > 3567)
 *   0x00, 0xFF            reserved
 * The bytes travel in {UFI_EVT_FLUX_STREAM, 0, len} messages (message borders carry no
 * meaning).  The stream ends right after index pulse n, followed by
 * {UFI_EVT_READ_DONE, status, 1} + [n] (n = complete revolutions in the stream).
 */

#include "ufi_firmware.h"
#include <string.h>

extern capture_context_t g_capture;

#define STREAM_BUF      8192u       /* payload bytes per message */
#define SEND_AT         4096u       /* hand a buffer to USB once it holds this much */
#define ROOM            32u         /* worst case per loop step: index markers + one delta */
#define TS_MARGIN       2750        /* 10 us: the index IRQ for older samples has run */
#define DELTA_MAX       (1u << 28)  /* ~0.98 s; larger while running = not yet written */
#define STALL_MS        1000u       /* host stopped reading */

static uint8_t buf[2][sizeof(ufi_response_header_t) + STREAM_BUF];
static uint8_t cur;                 /* buffer being filled (the other one may be on USB) */
static uint32_t fill;
static bool active, started, done;
static uint32_t rd;                 /* next sample to encode */
static uint32_t t_last;             /* time of the last emitted transition / index 0 */
static uint8_t next_idx;            /* next index pulse to emit */
static uint8_t n_final;             /* revolutions in this stream, 0 until known */
static uint32_t tx_start_ms;

/* ============================================================================
 * ENCODER
 * ============================================================================ */

static void put_u32(uint8_t* o, uint32_t v)
{
    o[0] = (uint8_t)v;
    o[1] = (uint8_t)(v >> 8);
    o[2] = (uint8_t)(v >> 16);
    o[3] = (uint8_t)(v >> 24);
}

static void put_delta(uint32_t d)
{
    uint8_t* o = &buf[cur][sizeof(ufi_response_header_t) + fill];
    if (d >= 1u && d <= 0xEFu) {
        o[0] = (uint8_t)d;
        fill += 1;
    } else if (d >= 240u && d <= 3567u) {
        const uint32_t v = d - 240u;
        o[0] = (uint8_t)(0xF0u + (v >> 8));
        o[1] = (uint8_t)v;
        fill += 2;
    } else {
        o[0] = 0xFE;
        put_u32(&o[1], d);
        fill += 5;
    }
}

static void put_index(uint32_t offset)
{
    uint8_t* o = &buf[cur][sizeof(ufi_response_header_t) + fill];
    o[0] = 0xFD;
    put_u32(&o[1], offset);
    fill += 5;
}

/* Emit every known index pulse up to time ts (inclusive limit: index <= ts) */
static void put_indexes_before(uint32_t ts, bool all)
{
    const uint8_t known = ufi_flux_index_count();
    while (!done && next_idx < known && (all || (int32_t)(ts - ufi_flux_index_time(next_idx)) >= 0)) {
        put_index(ufi_flux_index_time(next_idx) - t_last);
        next_idx++;
        if (n_final && next_idx > n_final) {
            done = true;                    /* stream ends right after index n */
        }
    }
}

static void encode(bool halted)
{
    uint32_t wr = ufi_flux_written();
    if (!halted && wr > 0) {
        wr--;                               /* the newest word may still be on its way */
    }
    const uint32_t now = ufi_flux_now();
    uint32_t words;
    const volatile uint32_t* s = ufi_flux_store(&words);

    if (wr > rd) {                          /* DMA wrote behind the cache's back */
        const uint32_t from = rd & ~7u;     /* 32-byte lines */
        SCB_InvalidateDCache_by_Addr((void*)&s[from], (int32_t)((wr - from) * 4u + 31u) & ~31);
    }

    while (!done && rd < wr && fill <= STREAM_BUF - ROOM) {
        const uint32_t ts = s[rd];
        const uint32_t d = ts - t_last;
        if (!halted && ((int32_t)(now - ts) < TS_MARGIN || d >= DELTA_MAX)) {
            break;                          /* too fresh, or stale data: retry next call */
        }
        put_indexes_before(ts, false);
        if (done) {
            break;
        }
        put_delta(d);
        t_last = ts;
        rd++;
    }
    /* Capture over and every stored sample used: the last index pulses follow no flux */
    if (halted && !done && rd >= wr && fill <= STREAM_BUF - ROOM) {
        put_indexes_before(0, true);
    }
}

/* ============================================================================
 * SEND / FINISH
 * ============================================================================ */

static void stop(void)
{
    active = false;
    ufi_flux_set_streaming(false);
    g_capture.state = CAPTURE_IDLE;
    led_set(&PIN_LED_FDD, false);
}

static void finish(int result)
{
    if (!ufi_usb_tx_idle()) {
        return;                             /* last data message still on its way */
    }
    const uint8_t revs = (next_idx > 0) ? (uint8_t)(next_idx - 1u) : 0u;
    ufi_usb_send_read_done(result, revs);
    stop();
}

/* Returns false if the host stopped reading (stream aborted) */
static bool pump(void)
{
    if (fill == 0 || (fill < SEND_AT && !done)) {
        return true;
    }
    if (!ufi_usb_tx_idle()) {
        if (HAL_GetTick() - tx_start_ms > STALL_MS) {
            ufi_flux_capture_stop();
            stop();
            return false;
        }
        return true;
    }
    const ufi_response_header_t h = {.command = UFI_EVT_FLUX_STREAM, .status = 0,
                                     .length = (uint16_t)fill};
    memcpy(buf[cur], &h, sizeof(h));
    if (ufi_usb_tx_start(buf[cur], sizeof(h) + fill) != UFI_OK) {
        ufi_flux_capture_stop();
        stop();
        return false;
    }
    tx_start_ms = HAL_GetTick();
    cur ^= 1u;
    fill = 0;
    return true;
}

/* ============================================================================
 * API
 * ============================================================================ */

void ufi_stream_begin(void)
{
    cur = 0;
    fill = 0;
    rd = 0;
    t_last = 0;
    next_idx = 1;
    n_final = 0;
    started = false;
    done = false;
    tx_start_ms = HAL_GetTick();
    ufi_flux_set_streaming(true);
    active = true;
}

void ufi_stream_abort(void)
{
    if (active) {
        active = false;
        ufi_flux_set_streaming(false);
    }
}

bool ufi_stream_active(void)
{
    return active;
}

void ufi_stream_service(void)
{
    if (!active) {
        return;
    }
    const capture_state_t st = *(volatile capture_state_t*)&g_capture.state;

    if (st == CAPTURE_ERROR) {
        /* DMA error, no index or overflow before 2 index pulses: drop the partial data */
        fill = 0;
        const int err = g_capture.error_code == 2 ? UFI_ERR_DMA :
                        g_capture.error_code == 3 ? UFI_ERR_NO_INDEX : UFI_ERR_BUFFER_FULL;
        if (ufi_usb_tx_idle()) {
            ufi_usb_send_read_done(err, 0);
            stop();
            led_set(&PIN_LED_ERR, true);
        }
        return;
    }
    if (st != CAPTURE_RUNNING && st != CAPTURE_COMPLETE) {
        return;                             /* waiting for the first index pulse */
    }
    if (!started) {
        t_last = ufi_flux_index_time(0);
        started = true;
    }
    const bool halted = (st == CAPTURE_COMPLETE);
    if (halted && n_final == 0) {
        uint8_t n = (uint8_t)(ufi_flux_index_count() - 1u);
        if (n > g_capture.revolutions_requested) {
            n = g_capture.revolutions_requested;
        }
        n_final = n;
        if (next_idx > n_final) {
            done = true;
        }
    }

    if (!done) {
        encode(halted);
    }
    if (!pump()) {
        return;
    }
    if (done && fill == 0) {
        finish(g_capture.error_code == 1 ? UFI_ERR_BUFFER_FULL : UFI_OK);
    }
}
