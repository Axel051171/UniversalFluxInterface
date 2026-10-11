/**
 * UFI Flux Engine - write streaming bookkeeping (see ufi_write_stream.h)
 */

#include "ufi_write_stream.h"
#include <string.h>

static uint32_t min_u32(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

void wstream_init(wstream_t* s, uint32_t n, uint32_t half, wstream_fill_t fill[2])
{
    memset(s, 0, sizeof(*s));
    s->n = n;
    s->half = half;
    fill[0].half = 0;
    fill[0].from = 0;
    fill[0].count = min_u32(n, half);
    fill[1].half = 1;
    fill[1].from = half;
    fill[1].count = (n > half) ? min_u32(n - half, half) : 0u;
    s->fed = fill[0].count + fill[1].count;
    /* count updates from the half before the one holding the last item: the DMA
     * interrupt that would start the counting may be late by up to a half */
    s->tail = (n <= 2u * half);
}

wstream_fill_t wstream_event(wstream_t* s, bool tc)
{
    (void)tc;                           /* the order HT, TC, HT, ... is implied by events */
    const uint32_t k = ++s->events;
    /* the half that starts now (k % 2) holds items [k*half, (k+1)*half): must be fed */
    if (s->fed < min_u32(s->n, (k + 1u) * s->half)) {
        s->underrun = true;
    }
    wstream_fill_t f;
    f.half = (uint8_t)((k - 1u) % 2u);
    f.from = (k + 1u) * s->half;
    f.count = (f.from < s->n) ? min_u32(s->half, s->n - f.from) : 0u;
    s->fed += f.count;
    if (!s->tail && k + 1u >= (s->n - 1u) / s->half) {
        s->tail = true;                 /* the half before the final one starts: count on */
    }
    return f;
}

uint32_t wstream_done(const wstream_t* s, uint32_t ndtr)
{
    const uint32_t ring = 2u * s->half;
    const uint32_t pos = (ndtr >= ring) ? 0u : ring - ndtr;
    const uint32_t known = s->events * s->half;        /* transferred as of the last event */
    uint32_t done = (known / ring) * ring + pos;
    if (done < known) {
        done += ring;                   /* the counter already wrapped, TC not processed */
    }
    return done;
}

int wstream_update(wstream_t* s, uint32_t ndtr)
{
    if (s->armed) {
        return (--s->updates_left == 0u) ? WSTREAM_FINISH : WSTREAM_GO;
    }
    const uint32_t done = wstream_done(s, ndtr);
    if (done < s->n) {
        return WSTREAM_GO;
    }
    /* last item transferred at this update (or an earlier one if the interrupt was late):
     * the period it defines starts at the next update and ends at the one after */
    const uint32_t extra = done - s->n;
    s->armed = true;
    s->updates_left = (uint8_t)(extra >= 2u ? 0u : 2u - extra);
    return s->updates_left ? WSTREAM_STOP_DMA : WSTREAM_FINISH;
}
