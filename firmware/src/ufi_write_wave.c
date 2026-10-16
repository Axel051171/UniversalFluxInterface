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
