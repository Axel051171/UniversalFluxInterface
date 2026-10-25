#include "ufi_firmware.h"
#include "ufi_precision.h"
#include "ufi_compare.h"
#include "ufi_cancel.h"
#include <string.h>
__attribute__((section(".backup"),aligned(8))) static track_compare_t comparison;
static uint32_t source_count,source_period,source_id;
static uint32_t tolerance=110,phase;
static uint8_t flags;
static bool ready,available;
static struct {bool active,comparing;uint8_t rev,revs;uint32_t edge,previous,started;} verify;
bool ufi_precision_enabled(void){return (flags&1)!=0;}
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
void ufi_precision_reset(void){ready=available=false;verify.active=false;}
int ufi_precision_options(const uint8_t* p,uint16_t n,uint8_t* out){
    if(n!=1 && n!=12)return UFI_ERR_BAD_ARGS;
    if(n==12){
        if(p[1]!=1 || (p[2]&~3u) || ((p[2]&2) && !(p[2]&1)) || p[3] || !rd(p+4) || rd(p+4)>1000000u)return UFI_ERR_BAD_ARGS;
        int ret=ufi_flux_reference_reserve((p[2]&1)!=0);if(ret)return ret;
        flags=p[2];tolerance=rd(p+4);phase=rd(p+8);ufi_precision_reset();
    }
    memset(out,0,24);out[0]=1;out[1]=flags;out[2]=54;out[3]=128;
    wr(out+4,tolerance);wr(out+8,phase);uint32_t words;(void)ufi_flux_reference(&words);wr(out+12,words);
    out[16]=ready;out[17]=available;out[18]=1;
    wr(out+20,available?comparison.generation:0);
    return UFI_OK;
}
int ufi_precision_source(const uint32_t* deltas,uint32_t count,uint32_t period,uint32_t offset){
    ufi_precision_reset();if(!(flags&1))return UFI_OK;
    uint32_t words;uint32_t* destination=ufi_flux_reference(&words);
    if(!destination || !count || count>words || !period)return UFI_ERR_NOT_IMPL;
    /* A wrapped splice needs a region model; reject before WGATE. */
    if(offset || phase>=period)return UFI_ERR_BAD_ARGS;
    uint64_t position=phase;
    for(uint32_t i=0;i<count;i++){
        if(!(i&255u)){UFI_WATCHDOG_FEED();if(ufi_cancel_requested())return UFI_ERR_CANCELLED;}
        position+=deltas[i];if(!deltas[i] || position>period)return UFI_ERR_RANGE;
        destination[i]=(uint32_t)position;
    }
    source_count=count;source_period=period;if(!++source_id)source_id=1;ready=true;return UFI_OK;
}
int ufi_precision_verify_begin(void){
    if(verify.active)return UFI_ERR_BUSY;
    available=false;
    if(!(flags&1))return UFI_OK;
    if(!ready)return UFI_ERR_BAD_ARGS;
    uint32_t words;const uint32_t* source=ufi_flux_reference(&words);
    if(!source || source_count>words)return UFI_ERR_NOT_FOUND;
    uint8_t revs=ufi_flux_get_revolution_count();if(!revs || revs>7)return UFI_ERR_DMA;
    track_compare_init(&comparison,source_id,tolerance,source_period);
    memset(&verify,0,sizeof verify);verify.active=true;verify.revs=revs;verify.started=HAL_GetTick();
    return UFI_OK;
}
int ufi_precision_verify_step(void){
    if(!verify.active)return UFI_OK;
    UFI_WATCHDOG_FEED();int ret=ufi_cancel_requested()?UFI_ERR_CANCELLED:
        HAL_GetTick()-verify.started>=10000u?UFI_ERR_TIMEOUT:UFI_OK;
    uint32_t words;const uint32_t* source=ufi_flux_reference(&words);
    const flux_revolution_t* r=ufi_flux_get_revolution(verify.rev);
    if(!ret && (!source || source_count>words))ret=UFI_ERR_NOT_FOUND;
    if(!ret && (!r || !r->index_time))ret=UFI_ERR_DMA;
    if(ret){verify.active=false;return ret;}
    if(!verify.comparing){
        unsigned work=0;
        while(verify.edge<r->count && work++<256){
            uint32_t t=r->samples[verify.edge++].timestamp;
            if(t<=verify.previous || t>r->index_time){verify.active=false;return UFI_ERR_DMA;}
            verify.previous=t;
        }
        if(verify.edge==r->count)verify.comparing=true;
        return UFI_ERR_BUSY;
    }
    if(!track_compare_step(&comparison,source,source_count,(const uint32_t*)r->samples,r->count,
                          r->index_time,(uint8_t)(verify.rev+1),256))return UFI_ERR_BUSY;
    if(++verify.rev<verify.revs){verify.edge=verify.previous=0;verify.comparing=false;return UFI_ERR_BUSY;}
    available=true;verify.active=false;
    if(flags&2)for(unsigned k=0;k<COMPARE_WINDOWS;k++)
        if(comparison.windows[k].missing || comparison.windows[k].extra)return UFI_ERR_DMA;
    return UFI_OK;
}
int ufi_precision_verify(void){
    int ret=ufi_precision_verify_begin();if(ret)return ret;
    do{ret=ufi_precision_verify_step();}while(ret==UFI_ERR_BUSY);
    return ret;
}
void ufi_precision_verify_cancel(void){if(verify.active){verify.active=false;available=false;}}
uint16_t ufi_precision_report(uint8_t first,uint8_t count,uint8_t* out){
    if(!available || first>128 || !count || count>8)return 0;
    if(count>128-first)count=(uint8_t)(128-first);
    uint16_t n=(uint16_t)(32+12*count);memset(out,0,n);
    out[0]=1;out[1]=first;out[2]=count;out[3]=comparison.completed;
    wr(out+4,comparison.generation);wr(out+8,comparison.tolerance);wr(out+12,comparison.reference_period);
    wr(out+16,phase);wr(out+20,source_count);out[24]=1;out[25]=flags;
    for(unsigned k=0;k<count;k++){
        const compare_window_t* w=&comparison.windows[first+k];uint8_t* p=out+32+12*k;
        wr(p,w->missing);wr(p+4,w->extra);p[8]=w->revolutions;
    }
    return n;
}
