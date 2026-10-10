/**
 * UFI Flux Engine - fault record and reset cause (firmware 1.14)
 *
 * A Cortex-M fault (HardFault, MemManage, BusFault, UsageFault) used to hang the board
 * until the watchdog fired.  Now the handlers in stm32h7xx_it.c jump here with the
 * exception stack frame: the registers and the fault status registers go into a record
 * in DTCM (which keeps its contents through a system reset), then the MCU resets at
 * once.  At the next start-up ufi_crash_boot() finds the record, writes CRASH.LOG to the
 * SD NAND (the last fault; READ_FILE fetches it) and keeps a one-line summary that INFO
 * reports together with the reset cause (power-on, pin, software, watchdog, brown-out).
 */

#include "ufi_firmware.h"
#include <stdio.h>
#include <string.h>

#define CRASH_MAGIC     0x43524153u     /* "SARC" */

typedef struct {
    uint32_t magic;
    uint32_t kind;                      /* 1 HardFault, 2 MemManage, 3 BusFault, 4 UsageFault */
    uint32_t r[8];                      /* r0-r3, r12, lr, pc, xpsr (exception frame) */
    uint32_t cfsr, hfsr, mmfar, bfar;
    uint32_t uptime_ms;
    uint32_t sum;
} crash_rec_t;

/* DTCM is not touched by the start-up code (.dtcm is NOLOAD) and survives a reset */
__attribute__((section(".dtcm"), aligned(4))) static crash_rec_t rec;

static char last_text[112] = "reset: unknown";
static uint8_t last_kind;

static uint32_t checksum(const crash_rec_t* r)
{
    const uint32_t* w = (const uint32_t*)r;
    uint32_t s = 0x5A5A5A5Au;
    for (uint32_t i = 0; i < (sizeof(*r) - sizeof(r->sum)) / 4u; i++) {
        s = (s << 1 | s >> 31) ^ w[i];
    }
    return s;
}

/* Called from the naked fault handlers with the stacked frame; never returns */
void ufi_crash_fault(const uint32_t* frame, uint32_t kind)
{
    rec.magic = CRASH_MAGIC;
    rec.kind = kind;
    for (int i = 0; i < 8; i++) {
        rec.r[i] = frame[i];
    }
    rec.cfsr = SCB->CFSR;
    rec.hfsr = SCB->HFSR;
    rec.mmfar = SCB->MMFAR;
    rec.bfar = SCB->BFAR;
    rec.uptime_ms = HAL_GetTick();
    rec.sum = checksum(&rec);
    SCB_CleanDCache();
    NVIC_SystemReset();
    for (;;) {
    }
}

static const char* kind_name(uint32_t k)
{
    return k == 1u ? "HardFault" : k == 2u ? "MemManage" : k == 3u ? "BusFault" :
           k == 4u ? "UsageFault" : "fault";
}

void ufi_crash_boot(void)
{
    const char* cause =
        __HAL_RCC_GET_FLAG(RCC_FLAG_PORRST) ? "power-on" :
        __HAL_RCC_GET_FLAG(RCC_FLAG_IWDG1RST) ? "watchdog" :
        __HAL_RCC_GET_FLAG(RCC_FLAG_WWDG1RST) ? "window watchdog" :
        __HAL_RCC_GET_FLAG(RCC_FLAG_BORRST) ? "brown-out" :
        __HAL_RCC_GET_FLAG(RCC_FLAG_SFTRST) ? "software" :
        __HAL_RCC_GET_FLAG(RCC_FLAG_PINRST) ? "pin" : "unknown";
    __HAL_RCC_CLEAR_RESET_FLAGS();

    if (rec.magic != CRASH_MAGIC || rec.sum != checksum(&rec)) {
        snprintf(last_text, sizeof(last_text), "reset: %s", cause);
        return;
    }
    last_kind = (uint8_t)rec.kind;
    snprintf(last_text, sizeof(last_text), "reset: %s pc=%08lX lr=%08lX cfsr=%08lX (CRASH.LOG)",
             kind_name(rec.kind), (unsigned long)rec.r[6], (unsigned long)rec.r[5],
             (unsigned long)rec.cfsr);

    char text[320];
    const int n = snprintf(text, sizeof(text),
        "UFI %s %s after %lu ms\r\n"
        "r0=%08lX r1=%08lX r2=%08lX r3=%08lX\r\n"
        "r12=%08lX lr=%08lX pc=%08lX xpsr=%08lX\r\n"
        "cfsr=%08lX hfsr=%08lX mmfar=%08lX bfar=%08lX\r\n",
        UFI_FW_VERSION, kind_name(rec.kind), (unsigned long)rec.uptime_ms,
        (unsigned long)rec.r[0], (unsigned long)rec.r[1], (unsigned long)rec.r[2],
        (unsigned long)rec.r[3], (unsigned long)rec.r[4], (unsigned long)rec.r[5],
        (unsigned long)rec.r[6], (unsigned long)rec.r[7], (unsigned long)rec.cfsr,
        (unsigned long)rec.hfsr, (unsigned long)rec.mmfar, (unsigned long)rec.bfar);
    uint16_t written = 0;
    if (n > 0) {
        ufi_file_write("CRASH.LOG", 0, true, (const uint8_t*)text, (uint16_t)n, &written);
    }
    rec.magic = 0;                      /* reported once */
    rec.sum = 0;
}

const char* ufi_crash_last(void)
{
    return last_text;
}

uint8_t ufi_crash_kind(void)
{
    return last_kind;
}
