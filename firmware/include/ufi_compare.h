#pragma once
#include <stdint.h>
#include <stdbool.h>
#define COMPARE_WINDOWS 128u
#define COMPARE_WIRE_SIZE (32u+COMPARE_WINDOWS*12u)
typedef struct {uint32_t missing,extra;uint8_t revolutions,reserved[3];} compare_window_t;
typedef struct {
    uint64_t absolute;
    int64_t signed_sum;
    uint32_t matched,maximum;
    uint8_t revs,reserved[7];
} compare_stats_t;
typedef struct {
    compare_window_t windows[COMPARE_WINDOWS];
    uint32_t generation,tolerance,reference_period,min_period,max_period;
    uint32_t i,j;
    uint8_t completed;
    compare_stats_t* stats;
} track_compare_t;
void track_compare_init(track_compare_t* c,uint32_t generation,uint32_t tolerance,uint32_t period);
/* Arrays contain increasing timestamps relative to their own revolution index.
 * Call only on validated complete captures. Processes at most budget transitions. */
bool track_compare_step(track_compare_t* c,const uint32_t* a,uint32_t na,
                        const uint32_t* b,uint32_t nb,uint32_t period,uint8_t rev,unsigned budget);
uint16_t track_compare_pack(const track_compare_t* c,uint8_t* out,uint16_t capacity);
uint16_t ufi_compare_report(uint8_t* out,uint16_t capacity,uint32_t generation);
void track_compare_stats_attach(track_compare_t* c,compare_stats_t storage[COMPARE_WINDOWS]);
uint16_t track_compare_stats_pack(const track_compare_t* c,uint8_t first,uint8_t count,uint8_t* out,uint16_t capacity);
uint16_t ufi_compare_stats_report(uint8_t* out,uint16_t capacity,uint32_t generation,uint8_t first,uint8_t count);
