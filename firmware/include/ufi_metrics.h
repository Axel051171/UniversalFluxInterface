#pragma once
#include <stdint.h>
#include <stdbool.h>
typedef struct {
    uint32_t capture_overruns, capture_dma_errors, write_dma_errors, write_underruns;
    uint32_t native_rx_pauses, lease_releases, capture_peak_words, capture_pending_words;
    uint32_t main_steps,main_max_ms,quality_steps,quality_max_ms,verify_steps;
    uint32_t stream_samples,cache_bytes,format_candidates,format_hint_hits,format_fallbacks;
    uint32_t fingerprint_calls,fingerprint_bytes;
    uint32_t capture_rdata_loss,capture_index_loss; /* episodes, not exact lost edge counts */
} ufi_metrics_t;
extern volatile ufi_metrics_t ufi_metrics;
uint16_t ufi_metrics_pack(uint8_t* out,uint16_t capacity);
enum {UFI_CYCLE_MAIN,UFI_CYCLE_CAPTURE_DMA,UFI_CYCLE_WRITE_DMA,UFI_CYCLE_STREAM,
      UFI_CYCLE_DECODER,UFI_CYCLE_VERIFY,UFI_CYCLE_COUNT};
#define UFI_CYCLE_AVAILABLE 1u
#define UFI_CYCLE_INVALID_SPAN 2u
#define UFI_CYCLE_CLOCK_CHANGED 4u
#define UFI_CYCLE_SATURATED 8u
#define UFI_CYCLE_UNAVAILABLE 16u
typedef struct {uint32_t calls,maximum;uint8_t flags;} ufi_cycle_counter_t;
typedef struct {
    uint32_t hz,maximum_ms;
    uint8_t flags;
    ufi_cycle_counter_t channel[UFI_CYCLE_COUNT];
    uint32_t rdata_loss,index_loss;
} ufi_cycle_snapshot_t;
void ufi_cycles_init(uint32_t hz,bool available);
void ufi_cycles_record(unsigned channel,uint32_t first,uint32_t last,uint32_t elapsed_ms,
                       uint32_t hz,bool available);
void ufi_cycles_snapshot(ufi_cycle_snapshot_t* out);
uint16_t ufi_cycles_pack(const ufi_cycle_snapshot_t* snapshot,uint8_t* out,uint16_t capacity);
static inline void ufi_metric_add(volatile uint32_t* value,uint32_t amount){
    uint32_t old=*value;*value=amount>UINT32_MAX-old?UINT32_MAX:old+amount;
}
static inline void ufi_metric_max(volatile uint32_t* value,uint32_t sample){if(sample>*value)*value=sample;}
