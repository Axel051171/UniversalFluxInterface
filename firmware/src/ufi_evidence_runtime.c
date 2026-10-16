#include "ufi_firmware.h"
#include "ufi_evidence.h"
#include "ufi_drive_extra.h"
#include "ufi_metrics.h"
#include "ufi_verify.h"
#include "ufi_composite.h"
#include "ufi_text.h"
#include "ufi_media.h"
#include <string.h>
extern capture_context_t g_capture;
static uint16_t configured,armed;
static bool have_arm,running;
static uint32_t start_ms,last_ms,submitted,last_produced,pending_reasons;
static void put(uint8_t* p,uint32_t v){memcpy(p,&v,4);}
void ufi_evidence_config(uint16_t f){configured=f&EVIDENCE_MASK;}
uint16_t ufi_evidence_configured(void){return configured;}
void ufi_evidence_arm(uint16_t f){armed=f&EVIDENCE_MASK;have_arm=true;}
void ufi_evidence_disarm(void){have_arm=false;}
void ufi_evidence_begin(uint8_t track,uint8_t side,uint8_t revs,uint32_t period){
    uint8_t c[EVIDENCE_CONTEXT_SIZE]={0};uint32_t words;
    (void)ufi_flux_store(&words);
    c[0]=1;c[1]=ufi_board_rev();c[3]=(uint8_t)ufi_drive_get_current();
    put(c+8,FLUX_TIMER_FREQ);start_ms=HAL_GetTick();put(c+12,start_ms);
    uint64_t tick=ufi_flux_now64();memcpy(c+16,&tick,8);
    c[24]=track;c[25]=side;c[26]=revs;c[27]=ufi_flux_store_is_psram();put(c+28,words);put(c+32,period);
    ufi_flux_evidence_context(c+36);
    c[42]=bus_in(&PIN_FDD_DENSITY);c[43]=bus_in(&PIN_FDD_DRATE);
    uint16_t rpm=ufi_drive_get_rpm();memcpy(c+44,&rpm,2);rpm=ufi_index_sim_rpm();memcpy(c+46,&rpm,2);
    drive_timing_t timing=ufi_drive_get_timing();memcpy(c+48,&timing,sizeof(timing));
    drive_extra_t extra=ufi_drive_extra_get();memcpy(c+70,&extra,sizeof(extra));
    put(c+84,ufi_crc32(0,c+24,58));
    ufi_text_append((char*)c+88,24,0,UFI_FW_VERSION);ufi_text_append((char*)c+112,32,0,UFI_GIT_REV);
    put(c+144,ufi_metrics.capture_overruns);put(c+148,ufi_metrics.capture_dma_errors);
    put(c+152,ufi_metrics.native_rx_pauses);put(c+156,ufi_native_tx_timeouts());
    evidence_begin(c,have_arm?armed:configured);have_arm=false;
    uint8_t media_status[40];ufi_drive_media_status(media_status);evidence_set_media(media_status);
    last_ms=start_ms;submitted=last_produced=pending_reasons=0;running=true;
    evidence_trace(0,0,0,0,1);
}
static void sample(uint32_t reason){
    uint32_t n=ufi_flux_written();if(n>last_produced)last_produced=n;
    evidence_trace(HAL_GetTick()-start_ms,last_produced,ufi_metrics.capture_pending_words,submitted,reason|pending_reasons);
    pending_reasons=0;
}
void ufi_evidence_service(void){
    if(!running)return;
    if(g_capture.state==CAPTURE_IDLE || ((g_capture.state==CAPTURE_COMPLETE || g_capture.state==CAPTURE_ERROR) && !ufi_stream_active())){
        ufi_evidence_finish();return;
    }
    if(!(evidence_flags()&EVIDENCE_TRANSFER))return;
    uint32_t now=HAL_GetTick();if(now-last_ms>=20){last_ms=now;sample(2);}
}
void ufi_evidence_usb(uint32_t bytes,bool blocked){
    if(!running)return;
    submitted+=bytes;
    pending_reasons|=blocked?8:4;
    uint32_t now=HAL_GetTick();
    /* Coalesce USB observations so a slow reader cannot flood the diagnostic pool. */
    if(now-last_ms>=20){last_ms=now;sample(blocked?8:4);}
}
void ufi_evidence_finish(void){
    if(!running)return;
    sample(16u|(g_capture.error_code?32u:0u));evidence_end();running=false;
}
