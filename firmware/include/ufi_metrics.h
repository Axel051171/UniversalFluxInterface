#pragma once
#include <stdint.h>
typedef struct {
    uint32_t capture_overruns, capture_dma_errors, write_dma_errors, write_underruns;
    uint32_t native_rx_pauses, lease_releases, capture_peak_words, capture_pending_words;
} ufi_metrics_t;
extern volatile ufi_metrics_t ufi_metrics;
