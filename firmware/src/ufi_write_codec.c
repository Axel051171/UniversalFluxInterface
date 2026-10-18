/**
 * UFI Flux Engine - write path codec (see ufi_write_codec.h)
 *
 * No HAL, no globals: compiled into the firmware and into firmware/tests.
 */

#include "ufi_write_codec.h"
#include <string.h>

/* ============================================================================
 * COMPACT STREAM DECODER
 * ============================================================================ */

void compact_init(compact_dec_t* d, uint32_t* out, uint32_t max)
{
    memset(d, 0, sizeof(*d));
    d->out = out;
    d->max = max;
}

static int fail(compact_dec_t* d)
{
    d->bad = true;
    d->need = 0;
    d->len = 0;
    return WRITE_CODEC_ERR_DATA;
}

static int emit(compact_dec_t* d, uint32_t v)
{
    if (v == 0 || d->decoded >= d->max) {       /* zero interval / more than announced */
        return fail(d);
    }
    d->out[d->decoded++] = v;
    return 0;
}

int compact_feed(compact_dec_t* d, const uint8_t* p, uint32_t n)
{
    if (d->bad) {
        return WRITE_CODEC_ERR_DATA;            /* nothing after an error is interpreted */
    }
    for (uint32_t i = 0; i < n; i++) {
        const uint8_t b = p[i];
        if (d->need == 0) {
            if (b >= 0x01u && b <= 0xEFu) {
                if (emit(d, b)) {
                    return WRITE_CODEC_ERR_DATA;
                }
                continue;
            }
            if (b >= 0xF0u && b <= 0xFCu) {
                d->need = 2;
            } else if (b == 0xFEu) {
                d->need = 5;
            } else {
                return fail(d);                 /* 0x00, 0xFD (index), 0xFF: reserved */
            }
            d->code[0] = b;
            d->len = 1;
            continue;
        }
        if (d->len >= sizeof(d->code)) {        /* cannot happen; keeps the write in bounds */
            return fail(d);
        }
        d->code[d->len++] = b;
        if (d->len < d->need) {
            continue;
        }
        const uint8_t* c = d->code;
        const uint32_t v = (d->need == 2u)
            ? 240u + ((uint32_t)(c[0] - 0xF0u) << 8) + c[1]
            : ((uint32_t)c[1] | ((uint32_t)c[2] << 8) | ((uint32_t)c[3] << 16) | ((uint32_t)c[4] << 24));
        d->need = 0;
        d->len = 0;
        if (emit(d, v)) {
            return WRITE_CODEC_ERR_DATA;
        }
    }
    return 0;
}

bool compact_complete(const compact_dec_t* d)
{
    return !d->bad && d->need == 0 && d->decoded == d->max;
}

/* ============================================================================
 * PACKING
 * ============================================================================ */

#define ED_SHORTEST_TICKS   ((1500UL * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL)   /* 1.5 us */

uint32_t write_precomp_auto_ns(const uint32_t* deltas, uint32_t n)
{
    /* 10th-percentile-ish shortest interval over the first samples, robust to glitches */
    const uint32_t m = (n < 2000u) ? n : 2000u;
    uint32_t below = 0;
    for (uint32_t i = 0; i < m; i++) {
        below += (deltas[i] < ED_SHORTEST_TICKS) ? 1u : 0u;
    }
    return (below * 10u > m) ? PRECOMP_ED_NS : PRECOMP_DD_HD_NS;
}

/* scaled intervals one after the other; the remainder carries so the sum keeps the scale */
typedef struct {
    const uint32_t* d;
    uint32_t n, i;
    uint16_t scale;
    uint32_t rem;
} scaler_t;

static uint32_t next_scaled(scaler_t* s)
{
    if (s->i >= s->n) {
        return 0;
    }
    const uint32_t v = s->d[s->i++];
    if (s->scale == 1000u) {
        return v;
    }
    const uint64_t t = (uint64_t)v * s->scale + s->rem;
    s->rem = (uint32_t)(t % 1000u);
    return (uint32_t)(t / 1000u);
}

static uint32_t precomp(uint32_t cur, uint32_t prev, uint32_t next, uint32_t ticks)
{
    if (prev > cur * 2u) {
        return (cur > ticks) ? cur - ticks : cur;
    }
    if (next > cur * 2u) {
        return cur + ticks;
    }
    return cur;
}

/* one pass over the data: pass 1 validates, pass 2 (arr != NULL) writes */
static int run(const uint32_t* deltas, uint32_t n, const pack_cfg_t* cfg, uint16_t* arr,
               uint32_t* clamped)
{
    const uint32_t pc_ns = cfg->precomp_fixed ? cfg->precomp_ns
                         : (cfg->track < 40u) ? 0u
                         : (cfg->track > 60u) ? cfg->precomp_ns : cfg->precomp_ns / 2u;
    const uint32_t pc_ticks = (pc_ns * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL;
    scaler_t s = {.d = deltas, .n = n, .scale = cfg->scale ? cfg->scale : 1000u};
    uint32_t prev = 0;
    uint32_t cur = next_scaled(&s);
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t next = (i + 1u < n) ? next_scaled(&s) : cur;
        uint32_t d = (pc_ticks && i > 0 && i + 1u < n) ? precomp(cur, prev, next, pc_ticks) : cur;
        if (d < WRITE_MIN_DELTA || d > WRITE_MAX_DELTA) {
            if (!cfg->clamp) {
                return WRITE_CODEC_ERR_RANGE;
            }
            d = (d < WRITE_MIN_DELTA) ? WRITE_MIN_DELTA : WRITE_MAX_DELTA;
            (*clamped)++;
        }
        if (arr) {
            arr[i] = (uint16_t)(d - 1u);        /* ARR = period - 1 */
        }
        prev = cur;
        cur = next;
    }
    return 0;
}

int write_pack(const uint32_t* deltas, uint32_t n, const pack_cfg_t* cfg, uint16_t* arr,
               uint32_t* clamped)
{
    uint32_t cnt = 0;
    const int ret = run(deltas, n, cfg, NULL, &cnt);
    if (ret != 0) {
        *clamped = 0;
        return ret;
    }
    cnt = 0;
    run(deltas, n, cfg, arr, &cnt);
    *clamped = cnt;
    return 0;
}
