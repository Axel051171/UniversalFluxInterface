/**
 * UFI Flux Engine - head steps on a schedule during a read or write (firmware 1.14)
 *
 * Copy protections such as Spiradisc (Apple II) lay data along a spiral: the head moves
 * a quarter track at fixed points of the revolution.  STEP_SCHEDULE (0x1F) stores up to
 * SCHED_MAX entries (ms after the start of the next READ / WRITE, target position); the
 * start is the index pulse that begins revolution 0 (or the capture start without index),
 * and the main loop performs the seeks when they fall due (ufi_sched_service).  Positions
 * are tracks, or quarter tracks when the schedule says so (Apple).  One-shot: consumed by
 * the next operation, dropped when that operation ends early.
 */

#include "ufi_firmware.h"

#define SCHED_MAX   32u

extern capture_context_t g_capture;

static struct { uint16_t at_ms; uint8_t pos; } items[SCHED_MAX];
static uint8_t count, next;
static bool quarter, armed;
static uint32_t t0;

int ufi_sched_set(const uint8_t* p, uint8_t n, bool quarter_units)
{
    if (n > SCHED_MAX) {
        return UFI_ERR_BAD_ARGS;
    }
    for (uint8_t i = 0; i < n; i++) {
        items[i].at_ms = (uint16_t)(p[3 * i] | (p[3 * i + 1] << 8));
        items[i].pos = p[3 * i + 2];
        if (i && items[i].at_ms < items[i - 1].at_ms) {
            count = 0;
            return UFI_ERR_BAD_ARGS;        /* must be in time order */
        }
    }
    count = n;
    next = 0;
    quarter = quarter_units;
    armed = false;
    return UFI_OK;
}

uint8_t ufi_sched_pending(void)
{
    return count ? (uint8_t)(count - next) : 0u;
}

/* Operation start at flux-timer time t (ISR or thread context) */
void ufi_sched_arm(uint32_t t)
{
    if (count && next < count) {
        t0 = t;
        armed = true;
    }
}

void ufi_sched_clear(void)
{
    count = next = 0;
    armed = false;
}

void ufi_sched_service(void)
{
    if (!armed) {
        return;
    }
    const capture_state_t cs = g_capture.state;
    const write_state_t ws = ufi_write_get_state();
    if (cs != CAPTURE_RUNNING && ws != WRITE_ACTIVE) {
        ufi_sched_clear();                  /* operation over: drop the rest */
        return;
    }
    const uint32_t elapsed_ms = (TIM2->CNT - t0) / (FLUX_TIMER_FREQ / 1000u);
    while (next < count && items[next].at_ms <= elapsed_ms) {
        const uint8_t pos = items[next].pos;
        if (quarter) {
            ufi_drive_seek_q(pos >> 2, pos & 3u);
        } else {
            ufi_drive_seek(pos);
        }
        next++;
    }
    if (next >= count) {
        ufi_sched_clear();
    }
}
