#include "ufi_metrics.h"
static volatile ufi_cycle_snapshot_t cycles;
volatile ufi_metrics_t ufi_metrics;
void ufi_cycles_init(uint32_t hz,bool available){
    ufi_cycle_snapshot_t initial={0};initial.hz=hz;
    uint64_t maximum_ms=hz?((uint64_t)UINT32_MAX*1000u)/hz:0;
    initial.maximum_ms=maximum_ms>UINT32_MAX?UINT32_MAX:(uint32_t)maximum_ms;
    initial.flags=available && initial.maximum_ms>1?UFI_CYCLE_AVAILABLE:UFI_CYCLE_UNAVAILABLE;
    cycles=initial;
}
void ufi_cycles_record(unsigned channel,uint32_t first,uint32_t last,uint32_t elapsed_ms,
                       uint32_t hz,bool available){
    if(channel>=UFI_CYCLE_COUNT)return;
    /* Separate flags avoid RMW races between foreground and nested DMA IRQs. */
    volatile ufi_cycle_counter_t* counter=&cycles.channel[channel];
    if(!available || first==last){counter->flags|=UFI_CYCLE_UNAVAILABLE;return;}
    if(!hz || hz!=cycles.hz){counter->flags|=UFI_CYCLE_CLOCK_CHANGED;return;}
    if(!(cycles.flags&UFI_CYCLE_AVAILABLE))return;
    /* Include one full SysTick interval of timestamp uncertainty. */
    if(elapsed_ms>=cycles.maximum_ms-1u){counter->flags|=UFI_CYCLE_INVALID_SPAN;return;}
    ufi_metric_add(&counter->calls,1);
    if(counter->calls==UINT32_MAX)counter->flags|=UFI_CYCLE_SATURATED;
    ufi_metric_max(&counter->maximum,last-first);
}
void ufi_cycles_snapshot(ufi_cycle_snapshot_t* out){
    if(!out)return;
    *out=cycles;
    for(unsigned i=0;i<UFI_CYCLE_COUNT;i++)out->flags|=out->channel[i].flags;
    if(out->flags&UFI_CYCLE_UNAVAILABLE)out->flags&=(uint8_t)~UFI_CYCLE_AVAILABLE;
    out->rdata_loss=ufi_metrics.capture_rdata_loss;out->index_loss=ufi_metrics.capture_index_loss;
}
uint16_t ufi_cycles_pack(const ufi_cycle_snapshot_t* snapshot,uint8_t* out,uint16_t capacity){
    if(!snapshot || !out || capacity<64)return 0;
    out[0]=1;out[1]=2;out[2]=15;out[3]=snapshot->flags;
    uint32_t values[15];values[0]=snapshot->hz;
    for(unsigned i=0;i<UFI_CYCLE_COUNT;i++){
        values[1+2*i]=snapshot->channel[i].calls;values[2+2*i]=snapshot->channel[i].maximum;
    }
    values[13]=snapshot->rdata_loss;values[14]=snapshot->index_loss;
    for(unsigned i=0;i<15;i++)for(unsigned k=0;k<4;k++)out[4+4*i+k]=(uint8_t)(values[i]>>(8*k));
    return 64;
}
uint16_t ufi_metrics_pack(uint8_t* out,uint16_t capacity){
    if(!out || capacity<64)return 0;
    for(unsigned i=0;i<64;i++)out[i]=0;
    out[0]=1;out[1]=1;out[2]=13;
    const uint32_t values[]={1000u,ufi_metrics.main_steps,ufi_metrics.main_max_ms,
        ufi_metrics.quality_steps,ufi_metrics.quality_max_ms,ufi_metrics.verify_steps,
        ufi_metrics.stream_samples,ufi_metrics.cache_bytes,ufi_metrics.format_candidates,
        ufi_metrics.format_hint_hits,ufi_metrics.format_fallbacks,
        ufi_metrics.fingerprint_calls,ufi_metrics.fingerprint_bytes};
    for(unsigned i=0;i<13;i++)for(unsigned k=0;k<4;k++)out[4+4*i+k]=(uint8_t)(values[i]>>(8*k));
    return 64;
}
