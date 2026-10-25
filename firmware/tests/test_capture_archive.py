"""Production capture finalization and USB reset preserve only stable captures."""
from test_regressions import ERRORS, FW, typedef, function, run


def main():
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <assert.h>\n#include <string.h>\n' + ERRORS
    code += typedef(FW/'include/ufi_firmware.h', 'capture_state_t')
    code += r'''
typedef struct {uint32_t timestamp;} flux_sample_t;
typedef struct {flux_sample_t* samples;uint32_t count,index_time;uint8_t revolution;} flux_revolution_t;
static struct {capture_state_t state;uint8_t revolutions_requested,revolutions_captured,current_track,current_side,current_quarter;uint32_t error_code;} g_capture;
static bool finalised,streaming,gw_capture,gw_linger;
static uint32_t store_generation,archive_fingerprint,capture_media_token=42,period,gw_limit,gw_indexes,last_index_ms,end_pos=6;
static uint8_t archive_info[8],idx_count=3;
static uint32_t idx_time[3]={100,200,300},idx_pos[3]={0,3,6},store[6]={101,150,199,201,250,299};
static uint32_t* flux_store=store;
static uint32_t store_words=6,irq_mask;
static flux_revolution_t revs[8];
#define REVOLUTIONS_BUFFER 8
#define INDEX_TIMEOUT_MS 500
#define UFI_WATCHDOG_FEED() ((void)0)
static void SCB_InvalidateDCache_by_Addr(void* p,int32_t n){(void)p;(void)n;}
static void capture_halt(void){}
static bool loss_ready,loss_hashed;
static bool capture_check_loss(void){return false;}
static void gw_poll_deadline(void){}
static bool ufi_drive_media_matches(uint32_t t){return t==42;}
static uint32_t HAL_GetTick(void){return 0;}
static uint8_t synthetic_seen;
static uint8_t ufi_flux_index_count(void){return synthetic_seen?synthetic_seen:idx_count;}
static uint32_t dma_pos(void){return 0;}
static uint32_t __get_PRIMASK(void){return irq_mask;}
static void __disable_irq(void){irq_mask=1;}
static void __set_PRIMASK(uint32_t m){irq_mask=m;}
static uint32_t evidence_id(void){return 7;}
'''
    for name in ('ufi_flux_invalidate','ufi_flux_archive_id','ufi_flux_archive_media',
                 'ufi_flux_archive_fingerprint','ufi_flux_archive_info','align_boundary','ufi_flux_poll',
                 'ufi_flux_get_revolution','ufi_flux_get_revolution_count'):
        code += function('ufi_flux.c', name)
    code += r'''
static bool on,clear_req,rx_paused,op_read,op_write,iec_reset_pending;
static unsigned have,rx_tail,rx_head,ev_mask,ev_head,ev_tail,position_invalidations;
static void ufi_drive_position_invalidate(void){position_invalidations++;}
static void ufi_analysis_job_abort(void){}
static void ufi_iec_init(void){}
static bool ufi_job_live(void){return false;}
static void ufi_job_finish(unsigned status){(void)status;}
static void ufi_capture_abort(void){g_capture.state=CAPTURE_IDLE;}
static void ufi_write_abort(void){}
'''
    code += function('ufi_v2.c','abort_ops') + function('ufi_v2.c','ufi_v2_reset') + r'''
int main(void){
    g_capture.state=CAPTURE_COMPLETE;g_capture.revolutions_requested=2;g_capture.current_track=2;g_capture.current_side=1;
    ufi_flux_invalidate();assert(!ufi_flux_archive_id() && !loss_ready && !loss_hashed);
    assert(ufi_flux_get_revolution_count()==2 && ufi_flux_archive_id()==1);
    assert(revs[0].count==3 && revs[1].count==3 && revs[1].samples[0].timestamp==1 && revs[1].samples[2].timestamp==99);
    uint32_t hash=ufi_flux_archive_fingerprint();uint8_t info[8];ufi_flux_archive_info(info);
    assert(info[0]==2 && info[1]==1 && info[4]==7 && ufi_flux_archive_media()==42);
    ufi_v2_reset();assert(position_invalidations==1 && ufi_flux_archive_id()==1 && ufi_flux_archive_fingerprint()==hash);
    assert(!on && !clear_req && !rx_paused && !have && !ev_mask && !ev_head && !ev_tail && rx_head==rx_tail);
    assert(ufi_flux_get_revolution(1)->samples[2].timestamp==99);
    g_capture.current_track=70;ufi_flux_archive_info(info);assert(info[0]==2);
    op_read=true;ufi_v2_reset();assert(!op_read && g_capture.state==CAPTURE_IDLE && ufi_flux_archive_id()==1);
    ufi_flux_invalidate();assert(!ufi_flux_archive_id() && !ufi_flux_get_revolution(0));
    uint32_t raw[6]={102,150,199,201,250,299};memcpy(store,raw,sizeof store);
    g_capture.state=CAPTURE_COMPLETE;assert(ufi_flux_get_revolution_count()==2 && ufi_flux_archive_id()==2);
    assert(ufi_flux_archive_fingerprint()!=hash); /* same counter after reboot cannot hide different samples */
    g_capture.error_code=1;assert(!ufi_flux_archive_id());g_capture.error_code=0;
    ufi_flux_invalidate();streaming=true;assert(!ufi_flux_get_revolution_count() && !ufi_flux_archive_id());
    store_generation=UINT32_MAX;ufi_flux_invalidate();assert(store_generation==1);
    streaming=false;idx_count=0;g_capture.state=CAPTURE_COMPLETE;
    assert(ufi_flux_poll()==CAPTURE_ERROR && g_capture.error_code==2 && !ufi_flux_archive_id());
    g_capture.error_code=0;g_capture.state=CAPTURE_COMPLETE;idx_count=3;end_pos=7;
    assert(ufi_flux_poll()==CAPTURE_ERROR && g_capture.error_code==2 && !ufi_flux_archive_id());
    end_pos=0;idx_count=1;idx_time[0]=0;period=100;irq_mask=1;g_capture.error_code=0;
    g_capture.state=CAPTURE_RUNNING;g_capture.revolutions_requested=1;
    /* A synthetic boundary must preserve the caller's interrupt mask. */
    synthetic_seen=2;assert(ufi_flux_poll()==CAPTURE_COMPLETE && irq_mask==1 && idx_count==2);
    return 0;
}
'''
    run('regression_capture_archive', code)


if __name__ == '__main__':
    main()
