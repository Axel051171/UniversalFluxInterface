/* Hardware clock adapter; pure arithmetic/reporting lives in ufi_metrics.c. */
#pragma once
#include "ufi_metrics.h"
typedef struct {uint32_t cycles,ms,hz;bool available;} ufi_profile_mark_t;
static inline ufi_profile_mark_t ufi_profile_begin(void){
    ufi_profile_mark_t mark={0};
#if defined(__arm__) || defined(__thumb__) || defined(UFI_PROFILE_TEST_HARDWARE)
    mark.available=(DWT->CTRL&DWT_CTRL_CYCCNTENA_Msk)!=0;
    mark.hz=SystemCoreClock;mark.ms=HAL_GetTick();mark.cycles=DWT->CYCCNT;
#endif
    return mark;
}
static inline void ufi_profile_end(unsigned channel,ufi_profile_mark_t mark){
#if defined(__arm__) || defined(__thumb__) || defined(UFI_PROFILE_TEST_HARDWARE)
    uint32_t last=DWT->CYCCNT,ms=HAL_GetTick();
    bool available=mark.available && (DWT->CTRL&DWT_CTRL_CYCCNTENA_Msk)!=0;
    ufi_cycles_record(channel,mark.cycles,last,ms-mark.ms,
                      mark.hz==SystemCoreClock?mark.hz:0,available);
#else
    (void)channel;(void)mark;
#endif
}
