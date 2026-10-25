"""Production capture context/trace runtime and IRQ filtering with hardware doubles."""
from test_regressions import BASE, ERRORS, FW, typedef, function, run


def runtime():
    code=BASE+ERRORS+typedef(FW/'include/ufi_firmware.h','drive_timing_t')
    code+=typedef(FW/'include/ufi_firmware.h','capture_state_t')+r'''
#include "ufi_evidence.h"
#include "ufi_drive_extra.h"
#include "ufi_metrics.h"
#include "ufi_verify.h"
#define FLUX_TIMER_FREQ 275000000u
#define UFI_FW_VERSION "test-version"
#define UFI_GIT_REV "test-build+"
typedef struct {capture_state_t state;uint32_t error_code;} capture_context_t;
capture_context_t g_capture;
volatile ufi_metrics_t ufi_metrics;
static uint32_t tick,produced;
static bool stream;
static const int PIN_FDD_DENSITY=2,PIN_FDD_DRATE=6;
static bool bus_in(const int* p){return *p==2;}
static uint32_t HAL_GetTick(void){return tick;}
static uint8_t ufi_board_rev(void){return 7;}
static uint8_t ufi_drive_get_current(void){return 2;}
static uint64_t ufi_flux_now64(void){return 0x1ffffff00ull;}
static uint32_t* ufi_flux_store(uint32_t* n){*n=2064384;return NULL;}
static bool ufi_flux_store_is_psram(void){return true;}
static void ufi_flux_evidence_context(uint8_t* p){memset(p,0,6);p[4]=10;}
static uint16_t ufi_drive_get_rpm(void){return 300;}
static uint16_t ufi_index_sim_rpm(void){return 0;}
static drive_timing_t ufi_drive_get_timing(void){drive_timing_t t={0};t.spinup_ms=500;return t;}
drive_extra_t ufi_drive_extra_get(void){return (drive_extra_t){20,600,20000,10,20,0};}
static uint32_t ufi_native_tx_timeouts(void){return 3;}
static uint32_t ufi_flux_written(void){ufi_metrics.capture_pending_words=produced/2;return produced;}
static bool ufi_stream_active(void){return stream;}
void ufi_drive_media_status(uint8_t p[40]){memset(p,0,40);p[0]=1;p[4]=42;}
'''
    source=(FW/'src/ufi_evidence_runtime.c').read_text()
    for name in ('ufi_firmware.h','ufi_composite.h'):
        source=source.replace(f'#include "{name}"','')
    code+=source+r'''
static void capture_halt(void){produced=0;}
'''+function('ufi_flux.c','ufi_flux_capture_stop')+r'''
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void){
    uint8_t out[4096],before[160];ufi_evidence_config(24);
    g_capture.state=CAPTURE_RUNNING;tick=0xfffffff0;ufi_evidence_begin(2,1,3,0);
    assert(evidence_pack(0,before,sizeof(before))==160 && before[2]==24 && before[24]==2 && before[40]==10);
    assert(rd(before+8)==275000000 && rd(before+20)==1 && before[42]==1 && !before[43]);
    assert(rd(before+84)==ufi_crc32(0,before+24,58) && !strcmp((char*)before+88,"test-version"));
    ufi_evidence_usb(64,false);ufi_evidence_usb(0,true);produced=100;tick+=20;ufi_evidence_service();
    assert(evidence_pack(2,out,4096)==80 && rd(out+8)==2);
    assert(rd(out+56)==20 && rd(out+60)==100 && rd(out+68)==64 && rd(out+72)==14);
    stream=true;g_capture.state=CAPTURE_COMPLETE;tick+=20;ufi_evidence_service();assert(evidence_flags()==24);
    stream=false;ufi_evidence_service();assert(!evidence_flags());
    assert(evidence_pack(2,out,4096)==128 && !out[3] && (rd(out+120)&16));
    assert(evidence_pack(0,out,4096)==160 && !memcmp(out,before,160));
    assert(evidence_pack(3,out,4096)==40 && out[0]==1 && out[4]==42);
    ufi_evidence_arm(8);g_capture.state=CAPTURE_RUNNING;ufi_evidence_begin(0,0,1,0);assert(evidence_flags()==8);
    ufi_evidence_finish();ufi_evidence_begin(0,0,1,0);assert(evidence_flags()==24);
    ufi_evidence_arm(0);ufi_evidence_disarm();ufi_evidence_finish();ufi_evidence_begin(0,0,1,0);assert(evidence_flags()==24);
    produced=321;tick+=7;ufi_evidence_usb(128,false);
    assert(!ufi_flux_capture_stop() && g_capture.state==CAPTURE_IDLE && !evidence_flags());
    assert(evidence_pack(2,out,4096)==80 && rd(out+60)==321 && rd(out+68)==128 && (rd(out+72)&16));
    memcpy(before,out,80);ufi_evidence_usb(99,true);ufi_evidence_service();ufi_flux_capture_stop();
    assert(evidence_pack(2,out,4096)==80 && !memcmp(out,before,80));
    puts("evidence runtime: frozen configuration, wrap, coalesced USB stalls, finalisation and one-shot flags passed");
}
'''
    run('regression_evidence_runtime',code,('src/ufi_evidence.c','src/ufi_verify.c','src/ufi_mfm.c'))


def index_irq():
    code=BASE+ERRORS+typedef(FW/'include/ufi_firmware.h','capture_state_t')
    code+=typedef(FW/'include/ufi_firmware.h','write_state_t')+r'''
#include "ufi_evidence.h"
static struct {capture_state_t state;unsigned error_code,revolutions_requested;} g_capture;
static uint8_t hard_sectors,hs_n,idx_count;
static uint32_t hs_last,hs_gap,index_mask_ticks,index_previous,period,idx_time[256],idx_pos[256],last_index_ms;
static bool index_seen,index_internal,gw_capture,gw_linger;
static uint64_t gw_limit,gw_end,gw_origin64,gw_linger_ticks;
static uint16_t gw_indexes;
static uint64_t now=0x100000000ull;
static uint32_t rd_de=1;
static struct {uint32_t DIER;} timer;
#define TIM2 (&timer)
#define TIM_DIER_CC2IE 2u
static void capture_rdata_arm(void){timer.DIER|=rd_de;}
#define REVOLUTIONS_STREAM 255u
static uint64_t ufi_flux_now64(void){return now;}
static uint32_t dma_pos(void){return 123;}
static write_state_t ufi_write_get_state(void){return WRITE_IDLE;}
static void ufi_write_index_handler(uint32_t t){(void)t;assert(0);}
static void capture_halt(void){}
static uint32_t HAL_GetTick(void){return 100;}
static void ufi_sched_arm(uint32_t t){(void)t;}
'''
    for name in ('hs_is_index','record_index','ufi_flux_index_event'):
        code+=function('ufi_flux.c',name)
    code+=r'''
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void){
    uint8_t context[160]={1},out[4096];evidence_begin(context,8);
    g_capture.state=CAPTURE_RUNNING;g_capture.revolutions_requested=8;hard_sectors=10;
    /* Warmup, sector hole and the short index gap are all retained. */
    uint32_t times[]={100,1100,2100,2600};
    for(unsigned i=0;i<4;i++){now=0x100000000ull+times[i];ufi_flux_index_event(times[i]);}
    assert(idx_count==1 && hs_n==2 && idx_time[0]==2600 && idx_pos[0]==123 && last_index_ms==100);
    index_mask_ticks=100;now+=10;ufi_flux_index_event(2610);
    assert(evidence_pack(1,out,4096)==112 && rd(out+8)==5);
    assert(rd(out+44)==4 && rd(out+92)==1 && rd(out+108)==2 && rd(out+36)==1);
    assert(rd(out+40)==123); /* raw DMA cursor retained, not claimed exact alignment */
    period=10000;index_internal=true;hard_sectors=0;timer.DIER=TIM_DIER_CC2IE;
    now+=1000;ufi_flux_index_event(3610);assert(timer.DIER&TIM_DIER_CC2IE);
    assert(evidence_pack(1,out,4096)==128 && rd(out+124)==9);
    gw_capture=true;ufi_flux_index_event(4610);assert(evidence_pack(1,out,4096)==128);
    puts("raw index IRQ: masked/sector/accepted/synthetic classification and GW isolation passed");
}
'''
    run('regression_raw_index',code,('src/ufi_evidence.c',))


def timings():
    code=BASE+ERRORS+r'''
enum {DRIVE_NONE,DRIVE_PC,DRIVE_IEC};
static int g_current_drive=DRIVE_PC,rpm_line;
static struct {unsigned tg43;} g_timing;
static bool is_shugart(int d){(void)d;return false;}
static bool is_apple(int d){(void)d;return false;}
#include "ufi_drive_extra.h"
#include "ufi_write_wave.h"
#include "ufi_capabilities.h"
#include "ufi_write_codec.h"
#define PRECOMP_AUTO 65535u
static drive_extra_t extra_timing;
static uint32_t motor_started_ms,tick,delayed;
static bool motor_step_pending;
static uint32_t HAL_GetTick(void){return tick;}
static void HAL_Delay(uint32_t ms){delayed+=ms;}
static const int PIN_FDD_DENSITY=1,PIN_FDD_DRATE=2;
static unsigned pins;
static void bus_out(const int* p,bool v){assert(v);pins=pins*10+*p;}
static void delay_us(uint32_t us){assert(pins==12 && us==25);}
static struct {bool wave_mode,gw_mode,use_precomp,apple,clamp;uint32_t flux_count,clamped;uint16_t scale;uint8_t track;} g_write;
static uint32_t write_buffer[]={220,1100,550};
static uint16_t write_arr[3];
static uint32_t wave_segments,write_timeout_ms,active_pre_ticks,active_post_ticks;
static wave_cursor_t wave_cursor;
static uint32_t ufi_write_precomp_select(uint8_t t){(void)t;return 0;}   /* 1.18: selection lives in ufi_write.c */
static int32_t weak_jitter=-1;static uint32_t weak_seed,weak_applied;   /* 4.16: markers off unless a test sets them */
static void SCB_CleanDCache_by_Addr(uint32_t* p,int32_t n){(void)p;(void)n;}
'''
    code+=function('ufi_drive.c','motor_step_wait')+function('ufi_drive.c','ufi_drive_lines_set')
    code+=function('ufi_write.c','pack_deltas')+r'''
int main(void){
    extra_timing.motor_step_ms=600;motor_started_ms=0xfffffff0u;tick=484;
    motor_step_pending=true;motor_step_wait();assert(delayed==100 && !motor_step_pending);
    motor_step_wait();assert(delayed==100);
    tick=984;motor_step_pending=true;motor_step_wait();assert(delayed==100);
    extra_timing.density_us=25;ufi_drive_lines_set(true,true);
    g_write.wave_mode=true;g_write.flux_count=3;
    active_pre_ticks=55000;active_post_ticks=27500;
    assert(pack_deltas()==0 && wave_segments==5 && active_post_ticks==0 && write_timeout_ms==2000);
    wave_item_t item;uint32_t elapsed=0,edges=0;
    const uint32_t expected[]={55220,56320,56870};
    while(wave_next(&wave_cursor,&item)){
        uint32_t period=(uint32_t)item.arr+1;
        if(item.ccr<period){assert(edges<3 && elapsed+item.ccr==expected[edges]);edges++;}
        elapsed+=period;
    }
    assert(edges==3 && elapsed==56870+110+27500);
    g_write.gw_mode=true;active_post_ticks=27500;
    assert(pack_deltas()==0 && wave_segments==3 && active_post_ticks==27500);
    assert(wave_cursor.lead==0 && wave_cursor.tail==0);
    /* weak-cell markers (4.16): the pass runs before the plan, keeps the track length, and a track
     * that shrinks below three intervals is refused with RANGE; GW writes never see the pass */
    g_write.gw_mode=false;active_pre_ticks=active_post_ticks=0;
    weak_jitter=100;weak_seed=5;write_buffer[0]=220u|WAVE_WEAK_JITTER;write_buffer[1]=1100u|WAVE_WEAK_JITTER;
    assert(pack_deltas()==0 && g_write.flux_count==3 && write_buffer[0]+write_buffer[1]+write_buffer[2]==1870 && write_buffer[0]>=220 && write_buffer[1]>=220 && weak_applied<=2 && !(write_buffer[0]&~WAVE_WEAK_VALUE));
    { unsigned seen=0;for(unsigned seed=1;seed<64;seed++){
          weak_seed=seed;g_write.flux_count=3;write_buffer[0]=220u|WAVE_WEAK_DROP;write_buffer[1]=1100u|WAVE_WEAK_DROP;write_buffer[2]=550;
          const int r=pack_deltas();uint32_t sum=0;for(unsigned i=0;i<g_write.flux_count;i++)sum+=write_buffer[i];
          assert(sum==1870 && r==(g_write.flux_count<3?UFI_ERR_RANGE:0) && weak_applied==3-g_write.flux_count);
          seen|=1u<<(3-g_write.flux_count);
      } assert(seen==7); }                                            /* 0, 1 and 2 drops all occur over the seeds */
    g_write.flux_count=3;write_buffer[0]=220u|WAVE_WEAK_DROP;write_buffer[1]=1100;write_buffer[2]=550;
    g_write.gw_mode=true;weak_jitter=0;
    assert(pack_deltas()==0 && write_buffer[0]==(220u|WAVE_WEAK_DROP) && !weak_applied && wave_segments>16000);   /* GW: the marker is a plain long gap, never applied */
    write_buffer[0]=220;weak_jitter=-1;g_write.gw_mode=false;
    puts("drive timings: remaining motor delay, wrap, line ordering and production gate waveform passed");
}
'''
    run('regression_evidence_timings',code,('src/ufi_write_wave.c','src/ufi_write_codec.c','src/ufi_capabilities.c'))


if __name__=='__main__':
    runtime()
    index_irq()
    timings()
