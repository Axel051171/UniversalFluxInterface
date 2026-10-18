#pragma once
#include <stdbool.h>
#include <stdint.h>

/* PWM2/up counting: CCR == period suppresses the pulse for a silent segment.
 * ARR/RCR(reserved on TIM3)/CCR1 form one three-halfword DMA burst. */
#define WAVE_PERIOD_MAX 65535u
#define WAVE_MIN_TICKS 220u
#define WAVE_PULSE_TICKS 110u
#define WAVE_HALF 512u
typedef struct { uint16_t arr, reserved, ccr; } wave_item_t;
typedef struct {
    const uint32_t *deltas;
    uint32_t count, index;
    uint64_t remaining;
    uint16_t lead,tail; /* optional silent gate margins, each <=65535 ticks */
} wave_cursor_t;
/* Weak-cell markers in the u32 intervals of an extended WRITE (flags2 bit3, 4.16): bit31 jitters the
 * transition that ends the interval by a random offset within +-jitter ticks (the next interval is
 * changed by the opposite amount, so later transitions keep their position), bit30 drops it with
 * probability 1/2 (the interval merges into the next one); bits 0..29 hold the interval itself. */
#define WAVE_WEAK_JITTER 0x80000000u
#define WAVE_WEAK_DROP   0x40000000u
#define WAVE_WEAK_VALUE  0x3FFFFFFFu
#define WAVE_WEAK_JITTER_MAX 8192u
/* Apply the markers in place with a deterministic xorshift32 stream from `seed`; returns the new
 * count (drops remove intervals), *applied = transitions displaced by a nonzero offset or dropped.
 * The last interval is never jittered or dropped; its markers are cleared. */
uint32_t wave_weak_apply(uint32_t *deltas, uint32_t count, uint16_t jitter, uint32_t seed, uint32_t *applied);
bool wave_plan(const uint32_t *deltas, uint32_t count, uint32_t *segments,
               uint64_t *duration);
void wave_init(wave_cursor_t *s, const uint32_t *deltas, uint32_t count);
bool wave_next(wave_cursor_t *s, wave_item_t *out);
