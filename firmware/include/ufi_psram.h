/**
 * UFI Flux Engine - QSPI PSRAM start-up (ufi_psram.c): interface and the names shared by
 * the trial marker, the INFO texts and the status block of SELFTEST 'P'.
 * Free of HAL types so the native tests can include it.
 */
#ifndef UFI_PSRAM_H
#define UFI_PSRAM_H

#include <stdint.h>

int ufi_psram_init(void);                       /* UFI_OK = the window may be the flux store */
void ufi_psram_skip(const char* why);           /* before ufi_flux_init(): leave the PSRAM alone, INFO shows why */
void ufi_psram_note(const char* text);          /* replaces the PSRAM result text of INFO (static text) */
void ufi_psram_unlatch(void);                   /* host retest: lifts a safe-start skip and the "PSRAM off" latch */
void ufi_psram_dma_result(uint32_t result);     /* ufi_flux.c reports the capture-stream write test */
uint8_t ufi_psram_status_pack(uint8_t* out);    /* 32 bytes, schema 1 (SELFTEST 'P') */
const char* ufi_psram_result(void);             /* PSRAM result text of INFO */

/* Start-up phases */
enum {
    UFI_PSRAM_PH_CONTROLLER = 1, UFI_PSRAM_PH_QPI_RESET, UFI_PSRAM_PH_SPI_RESET, UFI_PSRAM_PH_ID,
    UFI_PSRAM_PH_ENTER_QPI, UFI_PSRAM_PH_MAP, UFI_PSRAM_PH_FIRST_READ, UFI_PSRAM_PH_FIRST_WRITE,
    UFI_PSRAM_PH_NARROW, UFI_PSRAM_PH_LINES, UFI_PSRAM_PH_DMA,
};

enum {
    UFI_PSRAM_STATE_NONE = 0,       /* no PSRAM in this build / bypassed */
    UFI_PSRAM_STATE_OK,             /* flux store is the PSRAM */
    UFI_PSRAM_STATE_FAILED,         /* phase names where; internal store */
    UFI_PSRAM_STATE_SAFE_START,     /* button B: not touched */
    UFI_PSRAM_STATE_LATCHED,        /* an earlier start died in latched_phase: off until power cycle or retest */
    UFI_PSRAM_STATE_DMA_NOT_RUN,    /* PSRAM in use, the DMA test got no request */
};

#define UFI_DMA_TEST_NOT_RUN 0xFFFFFFFFu        /* ufi_flux_dma_selftest(): no DMA request was served */
/* ufi_flux.c, of the failing run: guard words changed before, after, data words not increasing,
 * data words zero, value of the first guard word after the run */
const uint32_t* ufi_flux_dma_detail(void);

#endif /* UFI_PSRAM_H */
