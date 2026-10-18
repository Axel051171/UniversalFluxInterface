#include "ufi_write_wave.h"
#include <limits.h>

bool wave_plan(const uint32_t *d, uint32_t n, uint32_t *segments, uint64_t *duration)
{
    uint64_t parts = 0, total = 0;
    if (!d || !n || !segments || !duration) return false;
    for (uint32_t i = 0; i < n; i++) {
        if (d[i] < WAVE_MIN_TICKS) return false;
        const uint64_t ticks=(uint64_t)d[i]+(i==0?WAVE_PULSE_TICKS:0u);
        parts += (ticks + WAVE_PERIOD_MAX - 1u) / WAVE_PERIOD_MAX;
        total += d[i];
    }
    if (parts > UINT32_MAX - 2u) return false;
    *segments = (uint32_t)parts;
    *duration = total+WAVE_PULSE_TICKS;
    return true;
}

/* Random stream of a weak write, documented in USB_Protokoll 4.16 so a host can replay it: the seed
 * (0 -> 0x9E3779B9) goes through a multiply-xorshift finaliser, every draw is one xorshift32 step
 * (13, 17, 5).  Successive low bits of xorshift32 are correlated for small seeds, hence the mix and
 * the use of bit 16 (drop) and bits 8.. (jitter). */
static uint32_t weak_seed_mix(uint32_t seed)
{
    uint32_t v = seed ? seed : 0x9E3779B9u;
    v *= 0x9E3779B1u; v ^= v >> 16;
    v *= 0x85EBCA6Bu; v ^= v >> 13;
    return v ? v : 0x9E3779B9u;
}

static uint32_t weak_rng(uint32_t *x)
{
    uint32_t v = *x;
    v ^= v << 13; v ^= v >> 17; v ^= v << 5;
    *x = v;
    return v;
}

uint32_t wave_weak_apply(uint32_t *d, uint32_t n, uint16_t jitter, uint32_t seed, uint32_t *applied)
{
    uint32_t w = 0, rng = weak_seed_mix(seed), done = 0;
    if (!d || !n) { if (applied) *applied = 0; return 0; }
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t marks = d[i] & ~WAVE_WEAK_VALUE;
        uint32_t value = d[i] & WAVE_WEAK_VALUE;
        if (marks && i + 1u < n) {
            uint32_t next = d[i + 1u] & WAVE_WEAK_VALUE;
            if ((marks & WAVE_WEAK_DROP) && ((weak_rng(&rng) >> 16) & 1u)) {
                /* the transition is omitted: this interval joins the next one, markers of the next stay */
                d[i + 1u] = (d[i + 1u] & ~WAVE_WEAK_VALUE) | ((next + value) & WAVE_WEAK_VALUE);
                done++;
                continue;
            }
            if ((marks & WAVE_WEAK_JITTER) && jitter) {
                int32_t r = (int32_t)((weak_rng(&rng) >> 8) % (2u * jitter + 1u)) - (int32_t)jitter;
                const int32_t lo = value > WAVE_MIN_TICKS ? -(int32_t)(value - WAVE_MIN_TICKS) : 0;
                const int32_t hi = next > WAVE_MIN_TICKS ? (int32_t)(next - WAVE_MIN_TICKS) : 0;
                if (r < lo) r = lo;
                if (r > hi) r = hi;
                if (r) {
                    value = (uint32_t)((int32_t)value + r);
                    d[i + 1u] = (d[i + 1u] & ~WAVE_WEAK_VALUE) | ((uint32_t)((int32_t)next - r) & WAVE_WEAK_VALUE);
                    done++;
                }
            }
        }
        d[w++] = value;
    }
    if (applied) *applied = done;
    return w;
}

void wave_init(wave_cursor_t *s, const uint32_t *d, uint32_t n)
{
    *s = (wave_cursor_t){.deltas=d, .count=n};
}

bool wave_next(wave_cursor_t *s, wave_item_t *out)
{
    if(s->lead){uint16_t n=s->lead;s->lead=0;*out=(wave_item_t){(uint16_t)(n-1),0,n};return true;}
    if (!s->remaining) {
        if (s->index == s->count) {
            if(!s->tail)return false;
            uint16_t n=s->tail;s->tail=0;*out=(wave_item_t){(uint16_t)(n-1),0,n};return true;
        }
        s->remaining = (uint64_t)s->deltas[s->index]+(s->index==0?WAVE_PULSE_TICKS:0u);
        s->index++;
    }
    bool pulse = s->remaining <= WAVE_PERIOD_MAX;
    uint32_t period = pulse ? (uint32_t)s->remaining : WAVE_PERIOD_MAX;
    if (!pulse) {
        period = WAVE_PERIOD_MAX;
        if (s->remaining - period < WAVE_MIN_TICKS)
            period = s->remaining - WAVE_MIN_TICKS;
    }
    s->remaining -= period;
    *out = (wave_item_t){(uint16_t)(period - 1u), 0,
                       (uint16_t)(pulse ? period - WAVE_PULSE_TICKS : period)};
    return true;
}
