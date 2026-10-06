/**
 * UFI Firmware Fixes
 * Einbinden mit: #include "ufi_fixes.h" (vor anderen Includes)
 */

#ifndef UFI_FIXES_H
#define UFI_FIXES_H

#include "stm32h7xx_hal.h"
#include <stdint.h>

/* Fix #4: __packed Definition */
#ifndef __packed
  #ifdef __GNUC__
    #define __packed __attribute__((packed))
  #elif defined(__ARMCC_VERSION)
    #define __packed __attribute__((packed))
  #else
    #define __packed
    #warning "Unknown compiler - __packed may not work"
  #endif
#endif

/* Fix #10: Error Codes */
typedef enum {
    UFI_OK              =  0,
    UFI_ERR_BUSY        = -1,
    UFI_ERR_NO_DRIVE    = -2,
    UFI_ERR_SEEK_FAIL   = -3,
    UFI_ERR_NO_INDEX    = -4,
    UFI_ERR_TIMEOUT     = -5,
    UFI_ERR_DMA         = -6,
    UFI_ERR_USB         = -7,
    UFI_ERR_IEC_NRFD    = -8,
    UFI_ERR_IEC_NOACK   = -9,
    UFI_ERR_BUFFER_FULL = -10,
    UFI_ERR_NOT_IMPL    = -11,
    UFI_ERR_WRITE_PROT  = -12,
} ufi_error_t;

/* Fix #2: IEC Timeout Helpers */
#define IEC_TIMEOUT_US      10000
#define IEC_TIMEOUT_CYCLES  (IEC_TIMEOUT_US * (SystemCoreClock / 1000000))

/* Fix #6: DWT Init Macro */
#define UFI_DWT_INIT() do { \
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; \
    DWT->CYCCNT = 0; \
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk; \
} while(0)

/* Fix #3: Safe Ring Buffer */
static inline uint32_t ring_buffer_free(uint32_t head, uint32_t tail, uint32_t size) {
    uint32_t used = (head >= tail) ? (head - tail) : (size - tail + head);
    return size - used - 1;
}

/* Fix #9: Watchdog */
#ifdef USE_WATCHDOG
extern IWDG_HandleTypeDef hiwdg;
#define UFI_WATCHDOG_INIT() do { \
    hiwdg.Instance = IWDG1; \
    hiwdg.Init.Prescaler = IWDG_PRESCALER_64; \
    hiwdg.Init.Reload = 4095; \
    hiwdg.Init.Window = 4095; \
    HAL_IWDG_Init(&hiwdg); \
} while(0)
#define UFI_WATCHDOG_FEED() HAL_IWDG_Refresh(&hiwdg)
#else
#define UFI_WATCHDOG_INIT()
#define UFI_WATCHDOG_FEED()
#endif

#endif /* UFI_FIXES_H */
