/**
 * UFI Flux Engine - track analysis (firmware 1.14): ANALYZE 0x25
 *
 * Reads `revs` revolutions of a track (blocking, no stream) and reports what the flux looks
 * like: revolution time / rpm, transitions, the interval histogram peaks (-> bit cell and
 * encoding guess), the track length in bit cells, weak-bit windows (transition counts that
 * differ between revolutions) and, for IBM MFM, the sectors found and the time from the
 * index pulse to the first sector ID (track alignment / drive radial offset).
 */

#include <string.h>
#include "ufi_firmware.h"
#include "ufi_mfm.h"

#define HIST_NS         100u                    /* bin width */
#define HIST_BINS       200u                    /* 0 .. 20 us */
#define ANALYZE_MS      4000u
#define WEAK_WINDOWS    64u

extern capture_context_t g_capture;

static mfm_decoder_t dec;
static uint16_t hist[HIST_BINS];

typedef struct { uint32_t t_acc, first_t; uint8_t sectors; } sec_ctx_t;

static void on_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    (void)id; (void)data;
    sec_ctx_t* c = (sec_ctx_t*)p;
    if (crc_ok) {
        if (c->sectors == 0) {
            c->first_t = c->t_acc;
        }
        if (c->sectors < 255u) {
            c->sectors++;
        }
    }
}

int ufi_analyze_track(uint8_t track, uint8_t side, uint8_t revs, analyze_result_t* r)
{
    memset(r, 0, sizeof(*r));
    if (revs == 0) revs = 3;
    if (revs > 8) revs = 8;
    int ret = ufi_capture_start(track, side, revs, 0);
    if (ret != UFI_OK) {
        return ret;
    }
    const uint32_t t0 = HAL_GetTick();
    capture_state_t cs;
    while ((cs = ufi_capture_get_state()) != CAPTURE_COMPLETE && cs != CAPTURE_ERROR) {
        if (HAL_GetTick() - t0 > ANALYZE_MS) {
            ufi_capture_abort();
            g_capture.state = CAPTURE_IDLE;
            return UFI_ERR_TIMEOUT;
        }
    }
    const uint8_t n = ufi_flux_get_revolution_count();
    if (cs == CAPTURE_ERROR || n == 0) {
        g_capture.state = CAPTURE_IDLE;
        return UFI_ERR_NO_INDEX;
    }
    const flux_revolution_t* rev = ufi_capture_get_data(0);
    r->revs = n;
    r->index_ticks = rev->index_time;
    r->transitions = rev->count;
    if (rev->index_time) {
        r->rpm_x100 = (uint16_t)((6000ull * FLUX_TIMER_FREQ + rev->index_time / 2u) / rev->index_time);
    }

    /* histogram of the intervals (100 ns bins), peaks = local maxima above 2 % */
    memset(hist, 0, sizeof(hist));
    for (uint32_t i = 1; i < rev->count; i++) {
        const uint32_t d = rev->samples[i].timestamp - rev->samples[i - 1].timestamp;
        const uint32_t ns = (uint32_t)((uint64_t)d * 1000000000ull / FLUX_TIMER_FREQ);
        const uint32_t b = ns / HIST_NS;
        if (b < HIST_BINS && hist[b] < 0xFFFFu) {
            hist[b]++;
        }
    }
    const uint32_t floor_n = rev->count / 50u + 1u;
    uint8_t np = 0;
    for (uint32_t b = 2; b + 2 < HIST_BINS && np < 4u; b++) {
        const uint32_t v = hist[b] + hist[b - 1] + hist[b + 1];
        if (hist[b] >= floor_n && hist[b] >= hist[b - 1] && hist[b] > hist[b + 1] &&
            hist[b] >= hist[b - 2] && hist[b] > hist[b + 2]) {
            /* weighted centre of the three bins */
            const uint32_t c_ns = (hist[b - 1] * (b - 1) + hist[b] * b + hist[b + 1] * (b + 1)) * HIST_NS / v
                                  + HIST_NS / 2u;
            r->peak_ns[np++] = (uint16_t)c_ns;
            b += 3;                                 /* skip the shoulder */
        }
    }
    if (np >= 2u) {
        const uint32_t p0 = r->peak_ns[0], p1 = r->peak_ns[1];
        const uint32_t ratio10 = p1 * 10u / p0;     /* 15 = MFM / Apple GCR, 20 = FM or C64 GCR */
        if (ratio10 >= 13u && ratio10 <= 17u) {
            r->encoding = ufi_drive_is_apple() ? 3u : 2u;     /* 4:6:8 */
            r->bitcell_ns = (uint16_t)(p0 / 2u);
        } else if (ratio10 >= 18u && ratio10 <= 22u) {
            if (np >= 3u && r->peak_ns[2] * 10u / p0 >= 28u) {
                r->encoding = 3u;                   /* GCR 1:2:3 (C64 style) */
                r->bitcell_ns = (uint16_t)p0;
            } else {
                r->encoding = 1u;                   /* FM: clock + data cells */
                r->bitcell_ns = (uint16_t)(p0 * 2u);
            }
        }
    }
    if (r->bitcell_ns) {
        r->bitcells = (uint32_t)((uint64_t)rev->index_time * 1000000000ull /
                                 ((uint64_t)r->bitcell_ns * FLUX_TIMER_FREQ));
    }

    /* weak bits: transition counts per 1/64 revolution that differ between revolutions */
    if (n >= 2u) {
        for (uint32_t w = 0; w < WEAK_WINDOWS; w++) {
            uint32_t lo = 0xFFFFFFFFu, hi = 0;
            for (uint8_t k = 0; k < n; k++) {
                const flux_revolution_t* rk = ufi_capture_get_data(k);
                const uint32_t a = (uint32_t)((uint64_t)rk->index_time * w / WEAK_WINDOWS);
                const uint32_t b = (uint32_t)((uint64_t)rk->index_time * (w + 1u) / WEAK_WINDOWS);
                uint32_t cnt = 0;
                for (uint32_t i = 0; i < rk->count; i++) {
                    const uint32_t t = rk->samples[i].timestamp;
                    if (t >= a && t < b) cnt++;
                    if (t >= b) break;
                }
                if (cnt < lo) lo = cnt;
                if (cnt > hi) hi = cnt;
            }
            if (hi - lo >= 3u) {
                r->weak_mask |= 1ull << w;
                r->weak_count++;
            }
        }
    }

    /* IBM MFM sectors and the index-to-first-ID time */
    if (r->encoding == 2u && r->bitcell_ns) {
        sec_ctx_t c = {0, 0, 0};
        mfm_dec_init(&dec, 1000000000u / r->bitcell_ns, on_sector, &c);
        for (uint32_t i = 1; i < rev->count; i++) {
            const uint32_t d = rev->samples[i].timestamp - rev->samples[i - 1].timestamp;
            c.t_acc = rev->samples[i].timestamp;
            mfm_dec_interval(&dec, d);
        }
        r->sectors = c.sectors;
        if (c.sectors) {
            r->first_sector_us = (uint32_t)((uint64_t)c.first_t * 1000000ull / FLUX_TIMER_FREQ);
        }
    }
    g_capture.state = CAPTURE_IDLE;
    return UFI_OK;
}
