/**
 * UFI Flux Engine - write streaming bookkeeping (firmware 1.15, audit F02)
 *
 * A DMA stream moves at most 65535 items per transfer, so long tracks are written from a
 * ring of two halves that the DMA plays in circular mode while the half-transfer /
 * transfer-complete interrupts refill the half just consumed from the packed track.  The
 * timer must stop exactly after the last period: in the final half the update interrupt
 * reads the DMA counter and arms the usual two trailing updates once the last item has
 * been transferred.  This file holds the arithmetic (no HAL) so it can be tested on the
 * host; ufi_write.c does the copying and the registers.
 *
 * Item k of the stream is period k + 2 of the track (the first two are loaded by hand).
 * Event k (k = 1, 2, ...: HT, TC, HT, ...) means the DMA finished half (k - 1) % 2 and
 * started half k % 2; that half must have been filled at event k - 1 (or at init).
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>

#define WSTREAM_HALF        2048u       /* items per ring half (4 KB of u16) */

enum { WSTREAM_GO = 0, WSTREAM_STOP_DMA = 1, WSTREAM_FINISH = 2 };

typedef struct {
    uint32_t n;                 /* items in the stream */
    uint32_t half;              /* items per ring half */
    uint32_t fed;               /* items copied into the ring so far */
    uint32_t events;            /* HT/TC events processed */
    bool tail;                  /* from the half before the last one on: count updates */
    bool armed;                 /* last item transferred: updates_left trailing updates */
    bool underrun;              /* a half started playing before it was refilled */
    uint8_t updates_left;
} wstream_t;

typedef struct {
    uint8_t half;               /* ring half to fill (0/1) */
    uint32_t from;              /* first item */
    uint32_t count;             /* items (0 = nothing to copy) */
} wstream_fill_t;

/* start: fills for both halves (count 0 when the stream is shorter) */
void wstream_init(wstream_t* s, uint32_t n, uint32_t half, wstream_fill_t fill[2]);

/* HT (tc = false) or TC (tc = true) processed in order; returns the refill for the half
 * just consumed.  Sets s->tail when the final half starts, s->underrun on a late refill. */
wstream_fill_t wstream_event(wstream_t* s, bool tc);

/* items the DMA has transferred so far, from the DMA counter (NDTR, counts down from
 * 2 * half) and the events processed; tolerates one unprocessed wrap */
uint32_t wstream_done(const wstream_t* s, uint32_t ndtr);

/* timer update in the tail phase, ndtr read after this update's transfer:
 * WSTREAM_STOP_DMA once the last item is in (disable the DMA request, two trailing
 * updates follow), WSTREAM_FINISH when the last period has ended */
int wstream_update(wstream_t* s, uint32_t ndtr);
