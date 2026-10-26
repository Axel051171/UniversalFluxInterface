"""Production capture guards with W0C/register doubles; no physical timing claim."""
from test_regressions import ERRORS, FW, function, run, typedef


def main():
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n#include "ufi_metrics.h"\n' + ERRORS
    code += '#include "ufi_evidence.h"\n'+typedef(FW/'include/ufi_firmware.h','capture_state_t')+r'''
static struct {capture_state_t state;unsigned error_code,revolutions_requested,revolutions_captured;
    unsigned current_track,current_side,current_quarter;void* buffer;} g_capture;
static struct {uint32_t SR,DIER,CCER,CCR1,CCR2,CCR3,CNT;} timer;
static struct {uint32_t CR,NDTR,PAR,M0AR,M1AR;} dma;
static struct {uint32_t LISR,LIFCR;} dma_status;
#define TIM2 (&timer)
#define DMA1_Stream0 (&dma)
#define DMA1 (&dma_status)
enum {TIM_SR_UIF=1,TIM_SR_CC1IF=2,TIM_SR_CC2IF=4,TIM_SR_CC3IF=8,TIM_SR_CC4IF=16,
    TIM_SR_CC1OF=512,TIM_SR_CC2OF=1024,TIM_SR_CC3OF=2048,
    TIM_DIER_CC1DE=512,TIM_DIER_CC3DE=2048,TIM_DIER_CC2IE=4,TIM_DIER_CC4IE=16,
    TIM_CCER_CC1E=1,TIM_CCER_CC3E=256,DMA_SxCR_CT=524288,
    DMA_LISR_TCIF0=32,DMA_LISR_TEIF0=8,DMA_LISR_DMEIF0=4,
    DMA_LIFCR_CTCIF0=32,DMA_LIFCR_CHTIF0=16,DMA_LIFCR_CTEIF0=8,DMA_LIFCR_CDMEIF0=4};
#define REVOLUTIONS_STREAM 200u
#define UFI_WATCHDOG_FEED() ((void)0)
static uint32_t store[20],*flux_store=store,*dma_scratch=store+16;
static uint32_t store_words=16,chunk_words=4,n_chunks=4,chunks_done,next_chunk;
static uint32_t stream_consumed,period,idx_time[256],idx_pos[256],last_index_ms,end_pos;
static unsigned idx_count;
static uint8_t revs[256];
static bool streaming,stream_scratch,index_internal,gw_capture,gw_linger,finalised,index_seen;
static bool loss_ready,loss_hashed;
static uint8_t capture_loss_flags,loss_position[4];
static uint32_t loss_first,loss_count,loss_hash,store_generation,capture_media_token;
static volatile uint32_t loss_clean_time,loss_clean_pos,loss_seen_time;   /* READ info (4.24) */
static uint32_t index_mask_ticks,index_previous;
static uint64_t gw_limit,gw_origin64,gw_end,gw_linger_ticks;
static uint16_t gw_indexes;
static uint8_t hard_sectors;
static uint32_t rd_de=TIM_DIER_CC1DE;
static uint32_t irq_mask,dma_stops;
static uint16_t evidence_config;
static uint32_t __get_PRIMASK(void){return irq_mask;}
static void __disable_irq(void){irq_mask=1;}
static void __set_PRIMASK(uint32_t value){irq_mask=value;}
static void dma_stop(void){dma_stops++;}
static void dma_freeze(void){}
static void dma_arm(void){chunks_done=0;next_chunk=2;dma.NDTR=chunk_words;}
/* Only the hardware acknowledgment primitive is modeled. */
static void capture_clear_status(uint32_t bits){timer.SR&=~bits;}
uint16_t evidence_flags(void){return evidence_config;}
static uint32_t ufi_drive_media_token(void){return 42;}
static uint64_t ufi_flux_now64(void){return 1000;}
static void record_index(uint32_t t,uint8_t reason){(void)t;(void)reason;}
static bool hs_is_index(uint32_t t){(void)t;return true;}
typedef unsigned write_state_t;
static write_state_t ufi_write_get_state(void){return 0;}
#define WRITE_WAITING_INDEX 1u
#define WRITE_ACTIVE 2u
static void ufi_write_index_handler(uint32_t t){(void)t;assert(0);}
static void ufi_write_delayed_start(uint32_t t){(void)t;assert(0);}
static uint32_t HAL_GetTick(void){return 0;}
static void ufi_sched_arm(uint32_t t){(void)t;}
static void SCB_CleanInvalidateDCache(void){}
static void SCB_InvalidateDCache_by_Addr(void* p,int32_t n){assert(p==store && n==64);}
static void capture_halt(void);
static void ufi_flux_invalidate(void);
'''
    for name in ('dma_pos','capture_rdata_arm','capture_loss_status','capture_latch_loss',
                 'capture_halt','capture_check_loss','ufi_flux_capture_start','ufi_flux_index_event',
                 'ufi_flux_tim2_irq','ufi_flux_dma_irq','gw_poll_deadline','ufi_flux_loss_snapshot',
                 'ufi_flux_loss_read','ufi_flux_archive_id','ufi_flux_invalidate'):
        code += function('ufi_flux.c',name)
    code += r'''
static void prepare(bool apple){
    memset(&timer,0,sizeof timer);memset(&dma,0,sizeof dma);memset(&dma_status,0,sizeof dma_status);
    memset(&g_capture,0,sizeof g_capture);memset(store,0,sizeof store);
    rd_de=apple?TIM_DIER_CC3DE:TIM_DIER_CC1DE;
    g_capture.state=CAPTURE_RUNNING;g_capture.revolutions_requested=1;
    g_capture.current_track=7;g_capture.current_side=1;
    timer.DIER=rd_de|TIM_DIER_CC2IE;timer.CCER=TIM_CCER_CC1E|TIM_CCER_CC3E;
    dma.NDTR=1;dma.M0AR=(uint32_t)store;dma.M1AR=(uint32_t)(store+4);
    idx_count=1;idx_time[0]=0;chunks_done=0;streaming=stream_scratch=false;
    period=0;index_internal=gw_capture=gw_linger=false;gw_limit=0;
    capture_loss_flags=0;loss_ready=loss_hashed=false;evidence_config=0;
    irq_mask=0;dma_stops=0;capture_media_token=42;store_generation=7;
    for(unsigned i=0;i<16;i++)store[i]=1000+i;
}
int main(void){
    uint32_t info[4],old_hash,value;uint8_t position[4],out[65];
    prepare(false);timer.CNT=500;timer.SR=TIM_SR_CC3OF;assert(!capture_check_loss() && loss_clean_time==500);
    timer.CNT=900;
    timer.SR=TIM_SR_CC1OF;irq_mask=1;assert(capture_check_loss());
    assert(irq_mask==1 && g_capture.state==CAPTURE_ERROR && g_capture.error_code==2 && dma_stops==1);
    assert(!(timer.DIER&(rd_de|TIM_DIER_CC2IE)) && !(timer.CCER&TIM_CCER_CC1E));
    assert(capture_loss_flags==1 && ufi_metrics.capture_rdata_loss==1 && !ufi_metrics.capture_index_loss);
    assert(loss_clean_time==500 && loss_seen_time==900);   /* READ info (4.24): window from the last clean check to the halt */
    assert(!capture_check_loss() && ufi_metrics.capture_rdata_loss==1);
    assert(!ufi_flux_archive_id() && ufi_flux_loss_snapshot(info,position));
    assert(info[0]==7 && info[1]==3 && !info[3] && position[0]==7 && position[1]==1 && position[3]==1);
    old_hash=info[2];assert(ufi_flux_loss_snapshot(info,position) && old_hash==info[2]);
    memset(out,0xa5,sizeof out);assert(ufi_flux_loss_read(0,3,out));
    memcpy(&value,out+8,4);assert(value==1002 && out[12]==0xa5);
    assert(!ufi_flux_loss_read(UINT32_MAX,1,out) && !ufi_flux_loss_read(2,2,out));
    assert(!ufi_flux_loss_read(0,129,out) && !ufi_flux_loss_read(0,1,NULL));
    assert(ufi_flux_loss_read(3,0,out));
    ufi_flux_invalidate();assert(!ufi_flux_loss_snapshot(info,position) && !ufi_flux_loss_read(0,1,out));
    prepare(true);timer.SR=TIM_SR_CC1OF;assert(!capture_check_loss());
    timer.SR=TIM_SR_CC3OF|TIM_SR_CC2OF;ufi_flux_dma_irq();
    assert(g_capture.state==CAPTURE_ERROR && capture_loss_flags==3 && dma_stops==1);
    assert(ufi_metrics.capture_rdata_loss==2 && ufi_metrics.capture_index_loss==1);
    prepare(false);g_capture.state=CAPTURE_WAITING_INDEX;
    dma.NDTR=chunk_words;timer.DIER&=~rd_de;
    timer.SR=TIM_SR_CC1OF;assert(!capture_check_loss());
    timer.SR=TIM_SR_CC2OF|TIM_SR_CC2IF;ufi_flux_tim2_irq();
    assert(g_capture.state==CAPTURE_ERROR && capture_loss_flags==2 && !loss_ready);
    prepare(false);index_internal=true;timer.SR=TIM_SR_CC2OF;assert(!capture_check_loss());
    prepare(false);period=1000;timer.SR=TIM_SR_CC2OF;assert(!capture_check_loss());
    evidence_config=EVIDENCE_RAW_INDEX;assert(capture_check_loss());
    prepare(false);timer.SR=TIM_SR_CC1OF;ufi_flux_index_event(100);
    assert(g_capture.state==CAPTURE_ERROR && g_capture.error_code==2);
    prepare(false);gw_capture=gw_linger=true;gw_end=999;timer.SR=TIM_SR_CC1OF;irq_mask=1;
    gw_poll_deadline();assert(irq_mask==1 && g_capture.state==CAPTURE_ERROR);
    prepare(false);timer.SR=TIM_SR_UIF|TIM_SR_CC1OF|TIM_SR_CC3OF|TIM_SR_CC2OF;
    capture_rdata_arm();assert(timer.SR==(TIM_SR_UIF|TIM_SR_CC2OF) && (timer.DIER&rd_de));
    assert((timer.CCER&(TIM_CCER_CC1E|TIM_CCER_CC3E))==TIM_CCER_CC1E);
    prepare(true);capture_rdata_arm();assert((timer.CCER&(TIM_CCER_CC1E|TIM_CCER_CC3E))==TIM_CCER_CC3E);
    prepare(false);chunks_done=5;dma.NDTR=2;streaming=true;
    for(unsigned i=6;i<22;i++)store[i%16]=2000+i;
    timer.SR=TIM_SR_CC1OF;assert(capture_check_loss() && ufi_flux_loss_snapshot(info,position));
    assert(info[1]==16 && info[3]==6 && ufi_flux_loss_read(15,1,out));
    memcpy(&value,out,4);assert(value==2021);
    prepare(false);chunks_done=5;dma.NDTR=2;streaming=true;dma.M0AR=(uint32_t)dma_scratch;
    timer.SR=TIM_SR_CC1OF;assert(capture_check_loss() && ufi_flux_loss_snapshot(info,position));
    assert(info[1]==16 && info[3]==4);
    prepare(false);ufi_metrics.capture_rdata_loss=UINT32_MAX;timer.SR=TIM_SR_CC1OF;
    assert(capture_check_loss() && ufi_metrics.capture_rdata_loss==UINT32_MAX);
    prepare(false);g_capture.state=CAPTURE_IDLE;timer.SR=TIM_SR_CC1OF|TIM_SR_CC2OF|TIM_SR_CC3OF;
    assert(!ufi_flux_capture_start(2,1000) && g_capture.state==CAPTURE_RUNNING && !capture_loss_flags);
    assert(!timer.SR && (timer.CCER&TIM_CCER_CC1E) && !loss_ready && !last_index_ms);
    assert(ufi_flux_capture_start(1,0)==UFI_ERR_BUSY);
    prepare(false);chunks_done=UINT32_MAX/chunk_words-1;dma_status.LISR=DMA_LISR_TCIF0;
    dma.NDTR=1;ufi_flux_dma_irq();
    assert(g_capture.state==CAPTURE_ERROR && g_capture.error_code==1 && dma_stops==1);
    assert(end_pos==UINT32_MAX && ufi_metrics.capture_overruns==1);
    puts("capture integrity: CH1/CH3/index loss, arm/reset, stop/deadline, masks, retained ring windows and bounds passed");
    return 0;
}
'''
    run('regression_capture_integrity',code)


if __name__ == '__main__':
    main()
