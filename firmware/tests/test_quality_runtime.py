"""Real firmware quality runner and WRITE parser with capture/clock doubles."""
from test_regressions import BASE, ERRORS, FW, run, function


def runtime_tests():
    code = BASE + ERRORS + r'''
#include "ufi_quality.h"
#include "../legacy_fixture.h"
#include "../mac_fixture.h"
typedef struct {uint32_t timestamp;} flux_sample_t;
typedef struct {flux_sample_t* samples;uint32_t count,index_time;uint8_t revolution;} flux_revolution_t;
typedef struct {uint8_t current_track,current_side,current_quarter;uint32_t error_code;} capture_context_t;
capture_context_t g_capture;
static flux_sample_t samples[100000];
static flux_revolution_t revolutions[8];
static uint8_t revolution_count=2;
static uint32_t ticks,tick_step=1,watchdogs;
static bool cancelled;
static uint32_t HAL_GetTick(void) {uint32_t t=ticks;ticks+=tick_step;return t;}
#define UFI_WATCHDOG_FEED() (++watchdogs)
static bool ufi_cancel_requested(void) {return cancelled;}
static uint8_t ufi_flux_get_revolution_count(void) {return revolution_count;}
static flux_revolution_t* ufi_flux_get_revolution(uint8_t i) {return i<revolution_count?&revolutions[i]:NULL;}
'''
    # Only platform includes are replaced. All runtime function bodies and state
    # declarations are compiled verbatim together with the real decoder/quality core.
    source=(FW/'src/ufi_quality_runtime.c').read_text(encoding='utf-8')
    source=source.replace('#include "ufi_firmware.h"','').replace('#include "ufi_cancel.h"','')
    code+=source+r'''
static uint32_t get32(const uint8_t* p) {return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint8_t data[11*512], report[QUALITY_WIRE_SIZE];
static uint32_t flux[100000];
static uint32_t capture_track(void) {
    uint32_t n=mfm_encode_amiga_track(250000,100000,0,0,11,data,flux,100000),t=0;
    for(uint32_t i=0;i<n;i++) {t+=flux[i];samples[i].timestamp=t;}
    for(unsigned i=0;i<2;i++) revolutions[i]=(flux_revolution_t){samples,n,55000000,(uint8_t)i};
    return n;
}
int main(void) {
    assert(ufi_quality_report(report,sizeof(report))==0);
    uint32_t n=capture_track();
    assert(ufi_quality_analyze(0,0,false)==0 && watchdogs);
    assert(ufi_quality_report(report,sizeof(report))==QUALITY_WIRE_SIZE);
    assert(report[0]==1 && report[1]==2 && report[2]==2 && report[3]==11 && report[5]==1);
    assert(result.flux_count[0]==n && result.revolution_ticks[0]==55000000 && result.max_gap[0]>0);
    assert(ufi_quality_analyze(2,250000,true)==0 && result.profiles==3);
    assert(result.sectors[0].seen==3 && result.sectors[0].nominal==3);
    verify_ref_t ref;mfm_decoder_t reference_decoder;
    assert(verify_ref_build_u32(&ref,&reference_decoder,flux,n,55000000,NULL));
    uint8_t level=0;
    assert(ufi_quality_verify(&ref,true,&level)==0 && level==VERIFY_CONTENT);
    data[0]=1;capture_track();
    assert(ufi_quality_verify(&ref,true,&level)==UFI_ERR_DMA);
    assert(ufi_quality_report(report,sizeof(report)) && report[4]==6);
    cancelled=true;
    assert(ufi_quality_analyze(2,250000,true)==UFI_ERR_CANCELLED && result.status==20);
    cancelled=false;tick_step=10001;
    assert(ufi_quality_analyze(2,250000,true)==UFI_ERR_TIMEOUT);tick_step=1;
    g_capture.error_code=1;
    assert(ufi_quality_analyze(2,250000,true)==UFI_ERR_BUFFER_FULL);g_capture.error_code=0;
    samples[1].timestamp=samples[0].timestamp;
    assert(ufi_quality_analyze(2,250000,true)==UFI_ERR_DMA);
    revolution_count=9;
    assert(ufi_quality_analyze(2,250000,true)==UFI_ERR_BUFFER_FULL);revolution_count=2;
    for(unsigned i=0;i<3;i++) samples[i].timestamp=(i+1)*100000;
    revolutions[0].count=revolutions[1].count=3;
    assert(ufi_quality_analyze(0,0,true)==0 && result.format==0 && result.n==0 && result.profiles==1);
    uint32_t total=0;for(unsigned i=0;i<128;i++)total+=result.histogram[i];
    assert(total==6);
    /* Extended analysis uses the actual captured positions and immutable source. */
    capture_track();
    uint8_t request[33]={0,0,2,0xff,2,0x90,0xd0,3,0};
    quality_options_t options;
    assert(quality_options(request,9,&options));
    static uint8_t extended_report[(QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE)+1];
    assert(ufi_quality_analyze_options(&options)==0);
    assert(result.extra.completed==3 && result.extra.timing[0].count==revolutions[0].count);
    assert(result.extra.count==48 && result.extra.dropped>0);
    memset(extended_report,0xa5,sizeof(extended_report));
    assert(!ufi_quality_report(extended_report,(QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE)-1) && extended_report[0]==0xa5);
    assert(ufi_quality_report(extended_report,(QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE))==(QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE));
    assert(extended_report[(QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE)]==0xa5 && extended_report[1824]=='Q');
    assert(extended_report[1827]==0xff && extended_report[1834]==3);
    options.flags=0x21;
    assert(ufi_quality_analyze_options(&options)==0 && result.extra.count==22);
    for(unsigned i=0;i<22;i++) {
        const uint8_t* e=result.extra.events[i];
        assert(e[0]==3 && e[1]==i/11 && e[2]==i/11 && e[3]==1);
        assert(get32(e+4)<get32(e+8) && get32(e+8)<=55000000);
    }
    /* Non-MFM evidence: exact interval arithmetic and preserved raw windows. */
    samples[0].timestamp=100;samples[1].timestamp=1100;samples[2].timestamp=10100;
    for(unsigned r=0;r<2;r++){revolutions[r].count=3;revolutions[r].index_time=11000;}
    options.flags=0x9d;options.format=0;options.rate=0;
    assert(ufi_quality_analyze_options(&options)==0 && result.format==0);
    assert(result.extra.timing[0].count==3 && result.extra.timing[0].min==100);
    assert(result.extra.timing[0].sum==10100 && result.extra.timing[0].squares==82010000);
    assert(result.extra.timing[0].short_count==1 && result.extra.timing[0].long_count==1);
    assert(result.extra.events[0][0]==2 && get32(result.extra.events[0]+12)==100);
    assert(result.extra.events[1][0]==5 && result.extra.events[1][2]==1);
    assert(get32(result.extra.events[1]+12)==100);
    assert(result.extra.events[3][0]==5 && result.extra.events[3][2]==3);
    assert(get32(result.extra.events[3]+12)==100 && get32(result.extra.events[3]+20)==9000);
    options.flags=0x41;options.cell=100;options.pattern=1;options.bits=1;
    assert(ufi_quality_analyze_options(&options)==0 && result.extra.count==4);
    options.flags=0x05;options.budget_ms=1;
    assert(ufi_quality_analyze_options(&options)==UFI_ERR_TIMEOUT && !result.extra.completed);
    assert(result.status==5 && ufi_quality_report(extended_report,(QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE)));
    cancelled=true;options.budget_ms=10000;
    assert(ufi_quality_analyze_options(&options)==UFI_ERR_CANCELLED);
    cancelled=false;
    /* Parameter v2 and diagnostic evidence from real firmware on independent FM/MMFM. */
    request[3]=1;request[4]=0;memset(request+5,0,4);
    uint8_t v2[37]={0,0,2,1,0,0,0,0,0,2,48,4,16,220,0,0,0,0x30,0x11,0,0,
                    0,0,0,0,0x89,0x44,0,0,0x10,0x27,0,0,7,0,250,0};
    assert(quality_options(v2,37,&options) && options.features==7);
    static uint8_t diag_report[QUALITY_MAX_WIRE+1];
    for(unsigned hp=0;hp<2;hp++) {
        lf_begin(hp!=0);lf_sector(3,1,0,1,data,false,false,false);
        uint32_t rate=hp?500000:250000, count=lf_flux(flux,100000,rate),t=0;
        for(uint32_t i=0;i<count;i++){t+=flux[i];samples[i].timestamp=t;}
        for(unsigned r=0;r<2;r++)revolutions[r]=(flux_revolution_t){samples,count,t+10000,(uint8_t)r};
        assert(ufi_quality_analyze_options(&options)==0 && result.format==(hp?4:3) && result.n==1);
        assert(result.extra.diagnostics[0][0][0]==0 && result.extra.diagnostics[0][0][1]==2);
        assert(result.extra.diagnostics[0][0][2]==0 && result.extra.events[0][0]==7);
        memset(diag_report,0xa5,sizeof(diag_report));
        assert(!ufi_quality_report(diag_report,QUALITY_MAX_WIRE-1) && diag_report[0]==0xa5);
        assert(ufi_quality_report(diag_report,QUALITY_MAX_WIRE)==QUALITY_MAX_WIRE);
        assert(diag_report[QUALITY_MAX_WIRE]==0xa5 && QUALITY_MAX_WIRE+40<=4096);
        const uint8_t* d=diag_report+QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE;
        assert(d[0]=='Q' && d[1]=='D' && d[2]==1 && d[3]==(hp?4:3) && d[8]==7 && d[9]==7);
        assert(d[12]==3 && !d[13] && get32(d+20)==2);
        /* Modify a clock only; data CRC remains correct. Detection needs a hint for illegal data. */
        lf_cells[lf_data_begin+10]^=1;count=lf_flux(flux,100000,rate);t=0;
        for(uint32_t i=0;i<count;i++){t+=flux[i];samples[i].timestamp=t;}
        for(unsigned r=0;r<2;r++)revolutions[r]=(flux_revolution_t){samples,count,t+10000,(uint8_t)r};
        options.format=hp?4:3;options.rate=rate;
        assert(ufi_quality_analyze_options(&options)==0);
        assert(result.extra.diagnostics[0][0][0]>0 && result.attempts[0][0].data_errors==0);
        assert(result.sectors[0].seen==3 && (result.sectors[0].flags&16));
        options.format=0;options.rate=0;
    }
    /* Existing Amiga decoder exposes coding and digital timing evidence as well. */
    capture_track();options.format=2;options.rate=250000;
    assert(ufi_quality_analyze_options(&options)==0);
    assert(!result.extra.diagnostics[0][0][0] && result.extra.diagnostics[0][0][1]==11);
    options.flags=3;
    assert(ufi_quality_analyze_options(&options)==0 && result.profiles==3);
    assert(ufi_quality_report(diag_report,QUALITY_MAX_WIRE)==QUALITY_MAX_WIRE);
    const uint8_t* masks=diag_report+QUALITY_WIRE_SIZE+QUALITY_EXTRA_SIZE;
    assert(masks[12]==3 && masks[14]==3 && masks[15]==3 && !masks[13]);
    options.flags=1;
    for(uint32_t i=20;i<revolutions[0].count;i+=100)samples[i].timestamp+=220;
    assert(ufi_quality_analyze_options(&options)==0 && result.extra.diagnostics[0][0][2]>0);
    v2[33]=8;assert(!quality_options(v2,37,&options));v2[33]=7;
    v2[35]=0;assert(!quality_options(v2,37,&options));v2[35]=250;
    v2[3]=0;assert(!quality_options(v2,37,&options));v2[3]=1;
    assert(!quality_options(v2,36,&options));v2[9]=1;assert(!quality_options(v2,37,&options));
    /* Production cooperative comparison yields, normalises speed and reports moved edges. */
    tick_step=0;capture_track();options.flags=1;options.report_mask=1;options.compare_tolerance=10;
    static flux_sample_t shifted[100000];
    memcpy(shifted,samples,revolutions[0].count*sizeof(*samples));shifted[50].timestamp+=50;
    revolutions[1].samples=shifted;
    assert(!ufi_quality_async_start(&options));int step;bool phase3=false;unsigned iterations=0;
    do{step=ufi_quality_async_step();phase3|=comparison_active;assert(++iterations<10000);}while(step==UFI_ERR_BUSY);
    assert(!step && phase3 && comparison_available && comparison.completed==2);
    unsigned differences=0;for(unsigned k=0;k<128;k++)differences+=comparison.windows[k].missing+comparison.windows[k].extra;
    assert(differences==2 && ufi_compare_report(diag_report,sizeof(diag_report),result.generation)==COMPARE_WIRE_SIZE);
    assert(!ufi_compare_report(diag_report,sizeof(diag_report),result.generation+1));
    assert(ufi_local_report(diag_report,sizeof(diag_report),result.generation)==QUALITY_LOCAL_SIZE && diag_report[14]==2);
    unsigned local_diff=0;for(unsigned k=0;k<32;k++)local_diff+=get32(diag_report+32+40*k+32)+get32(diag_report+32+40*k+36);
    assert(local_diff==2 && !ufi_local_report(diag_report,sizeof(diag_report),result.generation+1));
    assert(!ufi_quality_async_start(&options));
    while(!comparison_active)assert(ufi_quality_async_step()==UFI_ERR_BUSY);
    cancelled=true;assert(ufi_quality_async_step()==UFI_ERR_CANCELLED && !comparison_available);cancelled=false;
    ufi_quality_clear();assert(ufi_quality_report(report,sizeof(report))==0);
    /* Native Mac timing and all fixed-300-RPM zones, synchronous and cooperative. */
    const uint32_t mac_rates[]={500000,380228,346021,312500,280112,251256};
    uint8_t mac_data[524]={0},summary[48];
    options.format=0;options.rate=0;options.flags=1;options.report_mask=0;
    for(unsigned zone=0;zone<6;zone++){
        unsigned cyl=zone?16*(zone-1):79,sectors=12-cyl/16;
        g_capture.current_track=(uint8_t)cyl;g_capture.current_side=1;
        mac_reset(mac_rates[zone]);for(unsigned s=0;s<sectors;s++)mac_sector(cyl,1,s,mac_data,0);
        uint32_t pos=0;for(unsigned i=0;i<mac_count;i++){pos+=mac_flux[i];samples[i].timestamp=pos;}
        for(unsigned r=0;r<2;r++)revolutions[r]=(flux_revolution_t){samples,mac_count,pos+10000,(uint8_t)r};
        assert(!ufi_quality_analyze_options(&options) && result.format==6 && result.rate==mac_rates[zone] && !result.ambiguous);
        assert(quality_observation_pack(&result,summary,48)==48 && get32(summary+4)==4);
        assert(!ufi_quality_async_start(&options));
        unsigned steps=0;do{step=ufi_quality_async_step();assert(++steps<100000);}while(step==UFI_ERR_BUSY);
        assert(!step && result.format==6 && result.rate==mac_rates[zone] && result.n==sectors && !result.ambiguous);
    }
    return 0;
}
'''
    run('regression_quality_runtime',code,('src/ufi_mfm.c','src/ufi_verify.c','src/ufi_compare.c','src/ufi_quality.c'))


def write_flags_tests():
    code=BASE+ERRORS+r'''
#include "ufi_capabilities.h"
static uint32_t native_activity;
static struct {uint8_t kind,state;} ufi_job;
enum {JOB_OPEN};
static bool op_write,verified,extended;
static uint8_t write_seq;
static uint32_t source_ticks,prepare_calls,line_calls;
static uint8_t hard_sectors;
static int prepare_error;
static unsigned media_checks,write_aborts;
static bool change_during_prepare;
static uint32_t HAL_GetTick(void) {return 0;}
static bool ufi_job_live(void) {return false;}
static bool ufi_board_write_locked(void) {return false;}
static bool ufi_drive_write_protected(void) {return false;}
static bool ufi_drive_is_apple(void) {return false;}
static void ufi_flux_hard_sectors(uint8_t x) {hard_sectors=x;line_calls++;}
static bool ufi_drive_media_matches(uint32_t token){return token==42 && !(change_during_prepare && media_checks++);}
static void ufi_write_abort(void){write_aborts++;}
static uint32_t get16(const uint8_t*p) {return p[0]|(uint32_t)p[1]<<8;}
static uint32_t get32(const uint8_t*p) {return get16(p)|get16(p+2)<<16;}
static int ufi_write_prepare_compact(uint8_t t,uint8_t s,uint32_t n,uint32_t bytes,bool v) {
    (void)t;(void)s;(void)n;(void)bytes;verified=v;prepare_calls++;return prepare_error;
}
static void ufi_write_set_quarter(uint8_t x) {(void)x;}
static void ufi_write_set_scale(uint16_t x) {(void)x;}
static void ufi_write_set_extended(bool x) {extended=x;}
static void ufi_write_set_source_index(uint32_t x) {source_ticks=x;}
static uint64_t requested_set;
static void ufi_write_set_expected(uint64_t x,uint8_t c,uint8_t h,uint8_t n) {requested_set=x;(void)c;(void)h;(void)n;}
static void ufi_write_set_clamp(bool x) {(void)x;}
'''
    code+=function('ufi_v2.c','cmd_write')+function('ufi_v2.c','cmd_guarded_write')+r'''
int main(void) {
    uint8_t p[15]={0,0,0x21,3,0,0,0,3,0,0,0,0xc0,0x3b,0x47,3};
    assert(cmd_write(7,p,11)==0 && verified && extended && op_write && write_seq==7);
    assert(hard_sectors==0);
    assert(native_activity==0);
    p[2]=0x61;assert(cmd_write(8,p,15)==0 && source_ticks==55000000);
    uint32_t before=prepare_calls;
    assert(cmd_write(8,p,14)==UFI_ERR_BAD_ARGS && prepare_calls==before);
    p[2]=0x60;assert(cmd_write(8,p,15)==UFI_ERR_BAD_ARGS);
    p[2]=0x31;assert(cmd_write(8,p,11)==UFI_ERR_NOT_IMPL);
    p[2]=0x80;assert(cmd_write(8,p,11)==UFI_ERR_BAD_ARGS);
    assert(prepare_calls==before);
    uint8_t strict[23]={0,0,0xa1,3,0,0,0,3,0,0,0,0xfe,0x0f,0,0,0,0,0,0,0,0,2,0};
    assert(cmd_write(9,strict,23)==0 && requested_set==4094);
    strict[22]=1;assert(cmd_write(9,strict,23)==UFI_ERR_BAD_ARGS);
    strict[22]=0;strict[21]=4;assert(cmd_write(9,strict,23)==UFI_ERR_BAD_ARGS);
    strict[21]=2;assert(cmd_write(9,strict,22)==UFI_ERR_BAD_ARGS);
    /* Rejected flag/length combinations must not change index configuration. */
    uint8_t fuzz[40]={0};fuzz[3]=fuzz[7]=3;
    for(unsigned flags=0;flags<256;flags++)for(unsigned len=0;len<=sizeof(fuzz);len++){
        fuzz[2]=(uint8_t)flags;unsigned calls=line_calls;
        int ret=cmd_write(9,fuzz,(uint16_t)len);
        if(ret)assert(line_calls==calls);
    }
    uint8_t guard[19]={0};uint32_t generation=ufi_config_generation(),media=42;
    memcpy(guard,&generation,4);memcpy(guard+4,&media,4);guard[11]=guard[15]=3;
    before=prepare_calls;assert(!cmd_guarded_write(10,guard,19) && prepare_calls==before+1);
    ufi_config_changed();before=prepare_calls;
    assert(cmd_guarded_write(10,guard,19)==UFI_ERR_BAD_ARGS && prepare_calls==before);
    generation=ufi_config_generation();memcpy(guard,&generation,4);guard[4]=41;
    assert(cmd_guarded_write(10,guard,19)==UFI_ERR_MEDIA_CHANGED && prepare_calls==before);
    guard[4]=42;prepare_error=UFI_ERR_BUSY;unsigned calls=line_calls;
    assert(cmd_guarded_write(10,guard,19)==UFI_ERR_BUSY && line_calls==calls);
    assert(cmd_guarded_write(10,guard,18)==UFI_ERR_BAD_ARGS);
    prepare_error=0;change_during_prepare=true;media_checks=0;
    assert(cmd_guarded_write(10,guard,19)==UFI_ERR_MEDIA_CHANGED && write_aborts==1 && !op_write);
    return 0;
}
'''
    run('regression_quality_write_flags',code,('src/ufi_capabilities.c',))


if __name__=='__main__':
    runtime_tests()
    write_flags_tests()
