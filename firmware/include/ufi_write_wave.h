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
bool wave_plan(const uint32_t *deltas, uint32_t count, uint32_t *segments,
               uint64_t *duration);
void wave_init(wave_cursor_t *s, const uint32_t *deltas, uint32_t count);
bool wave_next(wave_cursor_t *s, wave_item_t *out);
