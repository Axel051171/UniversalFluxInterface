"""Execute production cooperative analyzer and job protocol with capture doubles."""
from test_regressions import BASE, ERRORS, FW, run, typedef, function


def main():
    code = BASE + ERRORS + r'''
#include "ufi_quality.h"
#include "ufi_analysis_job.h"
#include "ufi_evidence.h"
#include "ufi_drive_extra.h"
#include "ufi_media.h"
#include "ufi_resources.h"
static uint8_t resource_owner;
uint8_t ufi_resources_owner(void){return resource_owner;}
static uint32_t media_token=1;
static bool soft_lock,jumper_lock;
static uint8_t refusal_stub[6]={1,0x22,9,16,2,0x40};
static uint16_t speed_tol;static uint32_t speed_meas=55500000u,speed_src=55000000u;
uint16_t ufi_analyze_weak_report(uint8_t* out){memset(out,0,68);out[0]=1;out[1]=3;out[2]=40;out[3]=0x91;out[4+17]=6;return 68;}
static uint8_t weak_threshold;
uint16_t ufi_analyze_weak_positions(uint8_t threshold,uint8_t* out){memset(out,0,14);out[0]=1;out[6]=threshold;out[8]=1;out[10]=0x11;out[11]=1;out[12]=3;weak_threshold=threshold;return 14;}
void ufi_write_set_speed_check(uint16_t p){speed_tol=p;}
void ufi_write_speed_state(uint16_t* p,uint32_t* m,uint32_t* s){*p=speed_tol;*m=speed_meas;*s=speed_src;}
static uint8_t pz_n,pz[12];static int pz_ret;
int ufi_write_precomp_profile_set(const uint8_t* z,uint8_t n){if(pz_ret)return pz_ret;pz_n=n;memcpy(pz,z,3u*n);return 0;}
uint16_t ufi_write_precomp_profile(uint8_t* out){memset(out,0,19);out[0]=1;out[1]=pz_n?1:0;out[2]=pz_n;memcpy(out+3,pz,3u*pz_n);return 19;}
void ufi_v2_last_refusal(uint8_t out[6]){memcpy(out,refusal_stub,6);}
void ufi_board_soft_lock(void){soft_lock=true;}
bool ufi_board_soft_locked(void){return soft_lock;}
bool ufi_board_jumper_locked(void){return jumper_lock;}
int ufi_raw_verify_options(const uint8_t* p,uint16_t n,uint8_t* out){(void)p;(void)n;memset(out,0,8);return 0;}
uint16_t ufi_raw_verify_report(uint8_t* out){(void)out;return 0;}
static uint8_t saved_slots[2][128];
static uint16_t saved_lengths[2];
static bool storage_failure;
int ufi_file_read(const char* name,uint32_t offset,uint8_t* out,uint16_t len,uint16_t* got){
    unsigned slot=name[7]=='1';(void)offset;*got=0;
    if(storage_failure)return UFI_ERR_STORAGE;
    if(!saved_lengths[slot])return UFI_ERR_NOT_FOUND;
    *got=saved_lengths[slot]<len?saved_lengths[slot]:len;memcpy(out,saved_slots[slot],*got);return 0;
}
int ufi_file_write(const char* name,uint32_t offset,bool create,const uint8_t* data,uint16_t len,uint16_t* written){
    unsigned slot=name[7]=='1';(void)offset;(void)create;*written=0;
    if(storage_failure)return UFI_ERR_STORAGE;
    assert(len==128);memcpy(saved_slots[slot],data,len);saved_lengths[slot]=len;*written=len;return 0;
}
static uint8_t port_conflicts;
void ufi_drive_port_report(uint8_t p[24]){memset(p,0,24);p[0]=1;p[7]=port_conflicts;}
uint16_t ufi_provenance_block(uint8_t* out){memset(out,0,96);out[0]=1;out[1]=96;out[2]=7;return 96;}
int ufi_drive_operating_profile(const uint8_t* p,uint8_t out[40]){(void)p;memset(out,0,40);out[0]=1;return 0;}
static uint8_t preset_applied,profiles_saved;
int ufi_drive_profiles_save(void){profiles_saved++;return 0;}
int ufi_drive_operating_preset(uint8_t id,uint8_t out[40]){if(id<1||id>3)return UFI_ERR_BAD_ARGS;preset_applied=id;memset(out,0,40);out[0]=1;out[1]=id;return 0;}
uint32_t ufi_drive_media_token(void){return media_token;}
bool ufi_drive_media_matches(uint32_t t){return t==media_token;}
void ufi_drive_media_status(uint8_t p[40]){memset(p,0,40);p[0]=1;memcpy(p+4,&media_token,4);}
int ufi_drive_profile_set(const uint8_t p[8]){return media_drive_valid(p)?0:UFI_ERR_BAD_ARGS;}
int ufi_drive_media_set(const uint8_t p[12]){return media_profile_valid(p)?0:UFI_ERR_BAD_ARGS;}
void ufi_drive_media_notify(void){media_token++;}
void ufi_drive_media_observed(uint8_t f,uint32_t r){(void)f;(void)r;}
static uint16_t diagnostic_flags;
static uint8_t simulated_index;
void ufi_evidence_config(uint16_t f){diagnostic_flags=f;}
uint16_t ufi_evidence_configured(void){return diagnostic_flags;}
void ufi_evidence_arm(uint16_t f){diagnostic_flags=f&EVIDENCE_MASK;}
void ufi_evidence_disarm(void){}
void ufi_evidence_finish(void){evidence_end();}
static drive_extra_t timing;
drive_extra_t ufi_drive_extra_get(void){return timing;}
bool ufi_drive_extra_set(const drive_extra_t* t){timing=*t;return true;}
typedef struct {uint32_t timestamp;} flux_sample_t;
typedef struct {flux_sample_t* samples;uint32_t count,index_time;uint8_t revolution;} flux_revolution_t;
'''
    code += typedef(FW/'include/ufi_firmware.h', 'capture_state_t')
    code += typedef(FW/'include/ufi_firmware.h', 'capture_context_t')
    code += typedef(FW/'include/ufi_firmware.h', 'analyze_result_t')
    code += r'''
capture_context_t g_capture;
static int classify_error;static unsigned classify_calls;
int ufi_analyze_classify(const flux_revolution_t* rev,uint32_t t0,bool sectors,analyze_result_t* r){
    (void)t0;classify_calls++;if(classify_error)return classify_error;
    memset(r,0,sizeof *r);r->encoding=2;r->peak_ns[0]=2000;r->peak_ns[1]=3000;r->peak_ns[2]=4000;
    r->sectors=sectors?9:0;r->index_ticks=rev->index_time;r->transitions=rev->count+g_capture.current_track;return 0;
}
static uint32_t ticks,captures,aborts;
static int start_error;
static bool cancelled,busy,integrity;
static bool change_at_start;
static flux_sample_t samples[100000];
static flux_revolution_t revolution={samples,0,55000000,0};
static uint32_t HAL_GetTick(void){return ticks;}
#define UFI_WATCHDOG_FEED() ((void)0)
#define FLUX_TIMER_FREQ 275000000u
static bool ufi_cancel_requested(void){return cancelled;}
static void ufi_cancel_clear(void){cancelled=false;}
static bool ufi_operation_busy(void){return busy || ufi_analysis_job_active() || ufi_survey_active();}
static bool ufi_job_live(void){return integrity;}
static uint8_t ufi_flux_get_revolution_count(void){return 1;}
static flux_revolution_t* ufi_flux_get_revolution(uint8_t i){return i?NULL:&revolution;}
static bool apple_drive;
bool ufi_drive_is_apple(void){return apple_drive;}
static bool half_track_ok;
bool ufi_drive_quarter_ok(uint8_t q){return !q || (apple_drive?q<=3:(q==2 && half_track_ok));}
static uint32_t reseeks;static uint8_t reseek_track,reseek_quarter;static int reseek_error;
int ufi_drive_seek_q(uint8_t t,uint8_t q){reseeks++;reseek_track=t;reseek_quarter=q;return reseek_error;}
static int ufi_capture_start_q(uint8_t c,uint8_t h,uint8_t r,uint32_t period,uint8_t q){
    (void)r;(void)period;captures++;g_capture.current_track=c;g_capture.current_side=h;g_capture.current_quarter=q?(uint8_t)(0x80u|(q<<4)):0u;
    if(start_error)return start_error; /* seek failure before capture/evidence exists */
    uint8_t context[EVIDENCE_CONTEXT_SIZE]={1};
    context[46]=simulated_index;evidence_begin(context,diagnostic_flags);
    media_state_t ms;media_init(&ms,83,2,true);ms.generation=media_token;
    ms.drive[4]=0x2c;ms.drive[5]=1;
    uint8_t mp[12]={2,80,2,11,2,1,1,0,0x90,0xd0,3,0};memcpy(ms.media,mp,12);
    uint8_t snapshot[40];media_pack(&ms,1,c,h,false,snapshot);evidence_set_media(snapshot);
    g_capture.state=CAPTURE_RUNNING;g_capture.error_code=0;if(change_at_start)media_token++;return (int)start_error;
}
static capture_state_t ufi_capture_get_state(void){return g_capture.state;}
static void ufi_capture_abort(void){aborts++;g_capture.state=CAPTURE_IDLE;}
static flux_revolution_t* ufi_capture_get_data(uint8_t k){return ufi_flux_get_revolution(k);}
typedef enum {DRIVE_NONE,DRIVE_SHUGART} drive_type_t;
static bool drive_present=true;
static drive_type_t ufi_drive_get_current(void){return drive_present?DRIVE_SHUGART:DRIVE_NONE;}
static uint16_t selected;
static uint8_t ufi_selftest_selected(uint8_t* p,uint8_t n,uint16_t mask){(void)p;(void)n;selected=mask;return 0;}
static uint16_t ufi_write_coverage(uint8_t* out){out[0]=1;return 32;}
'''
    for name in ('ufi_quality_runtime.c', 'ufi_analysis_job.c', 'ufi_survey.c'):
        source = (FW/'src'/name).read_text(encoding='utf-8')
        for inc in ('ufi_firmware.h','ufi_cancel.h','ufi_job.h'):
            source = source.replace(f'#include "{inc}"', '')
        code += source
    code += r'''
static uint8_t data[11*512],out[4096],saved[4096],assembled[4096];
static uint32_t flux[100000];
static uint16_t size;
static void pump(void){
    unsigned steps=0;
    while(ufi_analysis_job_active()){
        if(g_capture.state==CAPTURE_RUNNING)g_capture.state=CAPTURE_COMPLETE;
        ufi_analysis_job_service();assert(++steps<100000);
    }
}
static void survey_pump(void){
    unsigned steps=0;
    while(ufi_survey_active()){
        if(g_capture.state==CAPTURE_RUNNING)g_capture.state=CAPTURE_COMPLETE;
        ufi_survey_service();assert(++steps<100000);
    }
}
int main(void){
    uint32_t n=mfm_encode_amiga_track(250000,100000,0,0,11,data,flux,100000),t=0;
    for(unsigned i=0;i<n;i++){t+=flux[i];samples[i].timestamp=t;}revolution.count=n;
    uint8_t p[61]={0};assert(ufi_analysis_command(p,1,out,&size)==0 && size==192 && rd(out+160)==24);
    uint8_t mac_caps=19;assert(!ufi_analysis_command(&mac_caps,1,out,&size) && size==48 && out[1]==6 && rd(out+4)==525 && rd(out+36)==251256);
    uint8_t local_caps=20;assert(!ufi_analysis_command(&local_caps,1,out,&size) && size==24 && out[1]==14 && rd(out+4)==1312 && rd(out+8)==0x84);
    uint8_t recognition_caps=21;assert(!ufi_analysis_command(&recognition_caps,1,out,&size) && size==24 && out[1]==15 && out[2]==16 && rd(out+8)==1023);
    p[0]=1;wr(p+1,42);p[7]=2;wr(p+8,60000);p[26]=1;p[27]=3;p[28]=2;wr(p+29,250000);
    uint8_t config[5]={23};assert(!ufi_analysis_command(config,1,out,&size) && size==24);
    uint32_t config_token=rd(out+4);wr(config+1,config_token);
    assert(!ufi_analysis_command(config,5,out,&size) && !rd(out+12));
    uint8_t guarded[38]={24};wr(guarded+1,config_token);memcpy(guarded+5,p,33);
    ufi_config_changed();unsigned captures_before=captures;
    assert(!ufi_analysis_command(config,5,out,&size) && rd(out+12)==UFI_PROFILE_CHANGED);
    assert(ufi_analysis_command(guarded,38,out,&size)==UFI_ERR_BAD_ARGS && captures==captures_before);
    wr(guarded+1,ufi_config_generation());
    uint8_t media_guarded[42]={26};wr(media_guarded+1,ufi_config_generation());
    wr(media_guarded+5,media_token+1);memcpy(media_guarded+9,p,33);
    assert(ufi_analysis_command(media_guarded,42,out,&size)==UFI_ERR_MEDIA_CHANGED && captures==captures_before);
    wr(media_guarded+5,media_token);
    busy=true;assert(ufi_analysis_command(guarded,38,out,&size)==UFI_ERR_BUSY);busy=false;
    uint8_t matrix=22;assert(!ufi_analysis_command(&matrix,1,out,&size) && size==320 && out[0]==2 && out[1]==9);
    { uint8_t prov[2]={43,0};assert(!ufi_analysis_command(prov,1,out,&size) && size==96 && out[0]==1 && out[1]==96 && out[2]==7);   /* op 43 provenance */
      assert(ufi_analysis_command(prov,2,out,&size)==UFI_ERR_BAD_ARGS); }
    { uint8_t wp[2]={44,0};assert(!ufi_analysis_command(wp,1,out,&size) && size==14 && weak_threshold==2 && out[8]==1);   /* op 44 default threshold 2 */
      wp[1]=5;assert(!ufi_analysis_command(wp,2,out,&size) && weak_threshold==5);
      wp[1]=0;assert(ufi_analysis_command(wp,2,out,&size)==UFI_ERR_BAD_ARGS); }                                           /* threshold 0 refused */
    { uint8_t pp[12]={45};assert(!ufi_analysis_command(pp,1,out,&size) && size==19 && out[0]==1 && !out[2]);           /* op 45 read */
      uint8_t ps[9]={45,1,2, 0,0x7d,0, 40,0xfa,0};assert(!ufi_analysis_command(ps,9,out,&size) && size==19 && pz_n==2 && out[1]==1 && out[2]==2 && out[7]==0xfa);
      assert(ufi_analysis_command(ps,8,out,&size)==UFI_ERR_BAD_ARGS && ufi_analysis_command(ps,10,out,&size)==UFI_ERR_BAD_ARGS);   /* length must match n */
      ps[2]=5;assert(ufi_analysis_command(ps,18,out,&size)==UFI_ERR_BAD_ARGS);ps[2]=2;                                 /* at most four zones */
      pz_ret=UFI_ERR_BAD_ARGS;assert(ufi_analysis_command(ps,9,out,&size)==UFI_ERR_BAD_ARGS);pz_ret=0;                 /* zone check refused */
      pp[1]=0;assert(!ufi_analysis_command(pp,2,out,&size) && !pz_n && !out[2]);                                       /* clear */
      busy=true;assert(ufi_analysis_command(ps,9,out,&size)==UFI_ERR_BUSY && !ufi_analysis_command(pp,1,out,&size));busy=false; }   /* set waits, read never */
    resource_owner=UFI_OWNER_GW;
    assert(!ufi_analysis_command(&matrix,1,out,&size) && (rd(out+8)&UFI_CAP_BUSY) && !out[34]);
    resource_owner=UFI_OWNER_NONE;
    uint8_t write_guard=25;assert(!ufi_analysis_command(&write_guard,1,out,&size) && size==20 && out[0]==2 && out[1]==0x27 && out[2]==0x22 && out[3]==8 && rd(out+4)==3);
    assert(rd(out+8)==ufi_config_generation() && rd(out+12)==media_token && out[16]==0x28 && out[17]==0x20 && out[18]==8 && !out[19]);
    busy=true;assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BUSY);busy=false;
    integrity=true;assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BUSY);integrity=false;
    assert(!ufi_analysis_command(media_guarded,42,out,&size) && size==64 && out[1]==A_CAPTURE);
    assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BUSY);
    g_capture.state=CAPTURE_COMPLETE;ufi_analysis_job_service();assert(job.state==A_ANALYZE);
    uint8_t page[16]={3};wr(page+1,42);page[14]=100;
    assert(ufi_analysis_command(page,16,out,&size)==UFI_ERR_BUSY);
    ufi_analysis_job_service();assert(ufi_analysis_job_active()); /* yields before entire track */
    pump();assert(job.state==A_DONE && job.attempt==1 && result.n==11 && result.profiles==3);
    unsigned length=ufi_quality_report(saved,sizeof(saved));wr(page+5,job.generation);
    for(unsigned offset=0;offset<length;offset+=97){
        wr(page+10,offset);page[14]=97;
        assert(!ufi_analysis_command(page,16,out,&size));assert(rd(out+16)==length);
        assert(rd(out+20)==ufi_crc32(0,out+24,size-24));memcpy(assembled+offset,out+24,size-24);
    }
    assert(!memcmp(saved,assembled,length));
    page[9]=14;wr(page+10,0);assert(!ufi_analysis_command(page,16,out,&size) && rd(out+16)==1312 && out[24]==1);
    assert(rd(out+20)==ufi_crc32(0,out+24,size-24));page[9]=0;
    wr(page+10,length+1);assert(ufi_analysis_command(page,16,out,&size)==UFI_ERR_RANGE);
    wr(page+10,0);wr(page+5,job.generation+1);assert(ufi_analysis_command(page,16,out,&size)==UFI_ERR_BAD_ARGS);
    wr(page+5,job.generation);page[9]=1;assert(!ufi_analysis_command(page,16,out,&size) && size==64);
    /* Exact expectation: retry a missing sector at most max_attempts. */
    p[5]=5;wr64(p+12,0x1ffe);p[22]=2;
    assert(!ufi_analysis_command(p,33,out,&size));pump();
    assert(job.state==A_FAILED && job.attempt==2 && job.status==-UFI_ERR_DMA && job.seen==0xffe && captures==3);
    assert(rd(job.history[0]+8)!=rd(job.history[1]+8));
    wr64(p+12,0xffe);assert(!ufi_analysis_command(p,33,out,&size));pump();assert(job.state==A_DONE && job.attempt==1);
    /* Foreign cancel, explicit cancel, timeout and malformed request. */
    assert(!ufi_analysis_command(p,33,out,&size));uint8_t cancel[5]={4};wr(cancel+1,43);
    assert(ufi_analysis_command(cancel,5,out,&size)==UFI_ERR_BAD_ARGS && ufi_analysis_job_active());
    wr(cancel+1,42);assert(!ufi_analysis_command(cancel,5,out,&size) && out[2]==-UFI_ERR_CANCELLED && aborts);
    assert(ufi_config_generation()==rd(guarded+1)); /* abort never restores stale settings */
    assert(!ufi_analysis_command(p,33,out,&size));ticks+=60000;ufi_analysis_job_service();assert(job.status==-UFI_ERR_TIMEOUT);
    p[5]=32;assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BAD_ARGS);p[5]=5;
    assert(!ufi_analysis_command(p,33,out,&size));g_capture.error_code=3;g_capture.state=CAPTURE_ERROR;
    ufi_analysis_job_service();assert(job.status==-UFI_ERR_NO_INDEX);
    /* New analysis invalidates old report pages, history remains addressable. */
    assert(!ufi_analysis_command(p,33,out,&size));pump();wr(page+5,job.generation);page[9]=0;
    assert(!ufi_quality_analyze(2,250000,false));assert(ufi_analysis_command(page,16,out,&size)==UFI_ERR_BUSY);
    page[9]=1;assert(!ufi_analysis_command(page,16,out,&size));
    /* Scoped bring-up: intrusive operations require their own explicit flag. */
    uint8_t st[5]={5,1,4,0,0};assert(ufi_analysis_command(st,5,out,&size)==UFI_ERR_BAD_ARGS && !selected);
    st[4]=1;assert(!ufi_analysis_command(st,5,out,&size) && selected==4);
    busy=true;assert(ufi_analysis_command(st,5,out,&size)==UFI_ERR_BUSY);busy=false;
    st[2]=8;st[4]=0;assert(!ufi_analysis_command(st,5,out,&size) && selected==8); /* SD info is read-only */
    /* Cooperative autodetection and synchronous path produce the same evidence. */
    quality_options_t options;uint8_t req[4]={0,0,1,3};assert(quality_options(req,4,&options));
    assert(!ufi_quality_analyze_options(&options));length=ufi_quality_report(saved,sizeof(saved));
    assert(!ufi_quality_async_start(&options));int ret;
    unsigned steps=0;do{ret=ufi_quality_async_step();assert(++steps<100000);}while(ret==UFI_ERR_BUSY);
    assert(!ret && result.format==2 && result.n==11);assert(ufi_quality_report(out,sizeof(out))==length);
    memset(saved+36,0,4);memset(out+36,0,4);assert(!memcmp(saved,out,length));
    assert(!ufi_quality_async_start(&options));cancelled=true;assert(ufi_quality_async_step()==UFI_ERR_CANCELLED);cancelled=false;
    /* Diagnostic defaults, timing schema and generation-bound capture pages. */
    uint8_t settings[3]={7,24,0};assert(!ufi_analysis_command(settings,3,out,&size) && out[1]==24);
    settings[1]=32;assert(ufi_analysis_command(settings,3,out,&size)==UFI_ERR_BAD_ARGS);
    uint8_t timing_set[14]={10,1};timing_set[8]=200; /* gate lead at payload offset 8 */
    assert(!ufi_analysis_command(timing_set,14,out,&size) && size==13 && out[7]==200);
    timing_set[8]=201;assert(ufi_analysis_command(timing_set,14,out,&size)==UFI_ERR_BAD_ARGS);
    timing_set[8]=0;timing_set[13]=1;assert(ufi_analysis_command(timing_set,14,out,&size)==UFI_ERR_BAD_ARGS);
    p[5]=24;wr64(p+12,0);assert(!ufi_analysis_command(p,33,out,&size));
    uint32_t capture_id=rd(out+60);assert(capture_id==evidence_id());
    evidence_index(0x100000002ull,12,4);pump();wr(page+5,job.generation);page[9]=5;
    assert(!ufi_analysis_command(page,16,out,&size) && rd(out+16)==48 && rd(out+32)==1);
    uint8_t ep[12]={8};wr(ep+1,capture_id);ep[5]=1;ep[10]=48;
    assert(!ufi_analysis_command(ep,12,out,&size) && size==72 && rd(out+4)==capture_id);
    wr(ep+1,capture_id+1);assert(ufi_analysis_command(ep,12,out,&size)==UFI_ERR_BAD_ARGS);
    uint8_t fresh[160]={1};evidence_begin(fresh,0);evidence_end();
    assert(ufi_analysis_command(page,16,out,&size)==UFI_ERR_BUSY);
    /* A failed retry must not inherit the successful first attempt's report. */
    p[5]=5;wr64(p+12,0x1ffe);assert(!ufi_analysis_command(p,33,out,&size));
    start_error=UFI_ERR_SEEK_FAIL;pump();start_error=0;
    assert(job.state==A_FAILED && job.attempt==2 && job.status==-UFI_ERR_SEEK_FAIL);
    assert(job.generation==0 && job.seen==0 && job.capture_id==0);
    assert(rd(job.history[0]+8)!=0 && job.history[0][2]==11);
    assert(job.history[1][3]==-UFI_ERR_SEEK_FAIL && rd(job.history[1]+8)==0 && rd(job.history[1]+12)==0);
    assert((rd(job.assessment[0]+4)&CAUSE_MISSING) && job.assessment[0][17]==1);
    assert((rd(job.assessment[1]+4)&CAUSE_POSITION) && !(rd(job.assessment[1])&RECOG_FORMAT_OBSERVED));
    uint8_t status_req[5]={2};wr(status_req+1,42);
    assert(!ufi_analysis_command(status_req,5,out,&size));
    assert(rd(out+12)==0 && rd(out+24)==0 && rd(out+32)==0x1ffe && rd(out+60)==0);
    /* The initial start failure is also retained in the attempt history. */
    start_error=UFI_ERR_SEEK_FAIL;
    assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_SEEK_FAIL);
    assert(job.attempt==1 && job.history[0][3]==-UFI_ERR_SEEK_FAIL && !job.capture_id);
    start_error=0;
    /* RESEEK (0x40): no seek on the first attempt, one step outward before every retry, same quarter;
     * a failed re-seek ends the job like a failed capture start. */
    reseeks=0;p[5]=5|64;wr64(p+12,0x1ffe);p[7]=2;assert(!ufi_analysis_command(p,33,out,&size) && !reseeks);
    pump();assert(job.attempt==2 && reseeks==1 && reseek_track==(job.track?job.track-1:1) && !reseek_quarter);   /* track 0: inward */
    reseeks=0;p[5]=5;assert(!ufi_analysis_command(p,33,out,&size));pump();assert(job.attempt==2 && !reseeks);
    reseeks=0;p[5]=5|64;reseek_error=UFI_ERR_SEEK_FAIL;assert(!ufi_analysis_command(p,33,out,&size));pump();
    assert(job.attempt==2 && reseeks==1 && job.state==A_FAILED && job.status==-UFI_ERR_SEEK_FAIL &&
           (rd(job.assessment[1]+4)&CAUSE_POSITION) && job.history[1][3]==-UFI_ERR_SEEK_FAIL);
    reseek_error=0;p[5]=128;assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BAD_ARGS);p[5]=5;
    { uint8_t caps0=0;assert(!ufi_analysis_command(&caps0,1,out,&size) && rd(out+4)==127); }
    /* Recovery policies use independent configuration, never consume job flag bits. */
    p[5]=32;wr64(p+12,0);assert(!ufi_analysis_command(p,33,out,&size));pump();
    wr(page+5,job.generation);page[9]=11;
    assert(!ufi_analysis_command(page,16,out,&size) && rd(out+16)==32 && !rd(out+28));
    assert((rd(job.assessment[0])&(RECOG_PROFILE_DECLARED|RECOG_POSITION_CHECKED))==(RECOG_PROFILE_DECLARED|RECOG_POSITION_CHECKED));
    page[9]=15;assert(!ufi_analysis_command(page,16,out,&size) && rd(out+16)==64 && (rd(out+28)&RECOG_FORMAT_OBSERVED));
    page[9]=16;assert(!ufi_analysis_command(page,16,out,&size) && rd(out+16)==20);
    /* Request cylinder 1 while fixture IDs remain cylinder 0: diagnose, never auto-seek/retry. */
    p[24]=1;unsigned capture_before=captures;assert(!ufi_analysis_command(p,33,out,&size));pump();
    assert(captures==capture_before+1 && job.state==A_DONE && job.attempt==1);
    assert(rd(job.assessment[0])&RECOG_HEADER_POSITION_MISMATCH);
    assert(job.profile_check[4]&MEDIA_CHECK_HEADER_POSITION_MISMATCH);
    assert(rd(job.profile_check+8)&(1u<<20));p[24]=0;page[9]=11;
    simulated_index=1;assert(!ufi_analysis_command(p,33,out,&size));pump();wr(page+5,job.generation);
    assert(!ufi_analysis_command(page,16,out,&size) && rd(out+28)==4 && (rd(out+32)&32));
    simulated_index=0;
    port_conflicts=7;assert(!ufi_analysis_command(p,33,out,&size));pump();wr(page+5,job.generation);
    port_conflicts=0;assert(!ufi_analysis_command(page,16,out,&size));
    assert(rd(out+28)==24 && (rd(out+32)>>16)==7); /* retained at capture start */
    page[9]=12;assert(!ufi_analysis_command(page,16,out,&size) && out[31]==7);
    p[5]=0;assert(!ufi_analysis_command(p,33,out,&size));
    assert(!job.profile_checked); /* no stale profile result in a later attempt */
    media_token++;ufi_analysis_job_service();assert(job.status==-UFI_ERR_MEDIA_CHANGED && job.state==A_FAILED);
    assert(rd(job.assessment[0]+4)&CAUSE_MEDIA);
    uint8_t media_req[1]={13};assert(!ufi_analysis_command(media_req,1,out,&size) && size==40 && rd(out+4)==media_token);
    media_req[0]=16;assert(!ufi_analysis_command(media_req,1,out,&size));
    assert(rd(out+4)==media_token);
    uint8_t recovery[12]={11,1,1,0,1,2,2,0,110,0,0,0};
    assert(!ufi_analysis_command(recovery,12,out,&size) && size==11);
    p[5]=0;wr64(p+12,0);p[7]=1;
    assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BAD_ARGS);
    p[7]=4;assert(!ufi_analysis_command(p,33,out,&size));
    assert(ufi_analysis_command(recovery,12,out,&size)==UFI_ERR_BUSY);
    pump();assert(job.attempt==2 && job.confirmed==2 && job.stop_reason==1 && job.state==A_DONE);
    assert((rd(job.assessment[0]+4)&CAUSE_CONFIRM) && job.assessment[0][17]==1);
    assert((rd(job.assessment[1])&RECOG_CONTENT_CONFIRMED) && job.assessment[1][17]==2);
    wr(page+5,job.generation);page[9]=7;assert(!ufi_analysis_command(page,16,out,&size));
    assert(rd(out+16)==COMPARE_WIRE_SIZE && out[24]==1 && out[25]==128 && out[26]==0); /* one revolution: no comparison */
    page[9]=8;assert(!ufi_analysis_command(page,16,out,&size) && out[27]==1);
    recovery[4]=2;recovery[6]=1;assert(!ufi_analysis_command(recovery,12,out,&size));
    assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BAD_ARGS); /* no retry trigger */
    p[5]=5;wr64(p+12,0x1ffe);assert(!ufi_analysis_command(p,33,out,&size));pump();
    assert(job.attempt==2 && job.stagnant==1 && job.stop_reason==2 && job.status==-UFI_ERR_DMA);
    recovery[2]=2;assert(ufi_analysis_command(recovery,12,out,&size)==UFI_ERR_BAD_ARGS);
    recovery[2]=1;recovery[4]=1;assert(!ufi_analysis_command(recovery,12,out,&size));
    /* Empty decoded tracks cannot meet a confirmation policy. */
    revolution.count=0;p[5]=0;wr64(p+12,0);p[7]=2;
    assert(!ufi_analysis_command(p,33,out,&size));pump();assert(job.state==A_FAILED && job.stop_reason==3 && !job.confirmed);
    assert((rd(job.assessment[1]+4)&(CAUSE_NO_FORMAT|CAUSE_CONFIRM))==(CAUSE_NO_FORMAT|CAUSE_CONFIRM));
    assert(job.assessment[1][17]==4);
    const int start_errors[]={UFI_ERR_DMA,UFI_ERR_BUFFER_FULL,UFI_ERR_NO_INDEX,UFI_ERR_TIMEOUT,UFI_ERR_CANCELLED};
    const uint32_t error_causes[]={CAUSE_TRANSPORT,CAUSE_BUFFER,CAUSE_INDEX,CAUSE_TIMEOUT,CAUSE_CANCEL};
    for(unsigned k=0;k<5;k++){
        start_error=start_errors[k];unsigned before=captures;
        assert(ufi_analysis_command(p,33,out,&size)==start_errors[k]);
        assert(captures==before+1 && job.attempt==1 && (rd(job.assessment[0]+4)&error_causes[k]));
        assert(!(rd(job.assessment[0])&RECOG_FORMAT_OBSERVED) && !job.assessment[0][17]);
    }
    start_error=0;
    change_at_start=true;wr(media_guarded+5,media_token);memcpy(media_guarded+9,p,33);
    assert(ufi_analysis_command(media_guarded,42,out,&size)==UFI_ERR_MEDIA_CHANGED);
    assert(job.state==A_FAILED && (rd(job.assessment[0]+4)&CAUSE_MEDIA) && !ufi_analysis_job_active());
    /* Stop-and-wait multitrack: retained report, ACK cursor and guarded retry. */
    change_at_start=false;cancelled=false;p[5]=0;wr64(p+12,0);p[7]=2;revolution.count=n;
    recovery[4]=0;assert(!ufi_analysis_command(recovery,12,out,&size));
    uint8_t create[49]={27},step[8]={29},poll=28;
    wr(create+1,0xffffffffu);wr(create+5,ufi_config_generation());wr(create+9,media_token);
    create[13]=0;create[14]=1;create[15]=2;memcpy(create+16,p,33);
    assert(!ufi_analysis_command(create,49,out,&size) && size==32 && out[1]==P_READY && out[22]==4);
    assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BUSY);
    wr(step+1,0xffffffffu);
    assert(!ufi_analysis_command(step,8,out,&size) && out[1]==P_RUNNING);
    assert(job.track==0 && job.side==0 && job.id==0xffffffffu);
    assert(ufi_analysis_command(step,8,out,&size)==UFI_ERR_BUSY);
    pump();assert(!ufi_analysis_command(&poll,1,out,&size) && out[1]==P_RESULT && out[20]==0);
    assert(job.state==A_DONE);step[7]=1;
    assert(!ufi_analysis_command(step,8,out,&size) && out[1]==P_READY && out[20]==1);
    assert(ufi_analysis_command(step,8,out,&size)==UFI_ERR_BAD_ARGS); /* stale ACK */
    step[5]=1;step[7]=0;media_token++;
    assert(ufi_analysis_command(step,8,out,&size)==UFI_ERR_MEDIA_CHANGED);media_token--;
    start_error=UFI_ERR_SEEK_FAIL;
    assert(ufi_analysis_command(step,8,out,&size)==UFI_ERR_SEEK_FAIL && out[1]==P_FAILED && out[20]==1);
    start_error=0;assert(!ufi_analysis_command(step,8,out,&size) && job.id==1 && job.side==1);
    ufi_analysis_job_abort();assert(!ufi_analysis_command(&poll,1,out,&size) && out[1]==P_FAILED);
    assert(!ufi_analysis_command(step,8,out,&size));pump();step[7]=1;
    assert(!ufi_analysis_command(step,8,out,&size) && out[20]==2);
    step[5]=2;step[7]=0;assert(!ufi_analysis_command(step,8,out,&size) && job.track==1 && job.side==0 && job.id==2);
    step[7]=2;assert(!ufi_analysis_command(step,8,out,&size) && out[1]==P_DONE && out[2]==-UFI_ERR_CANCELLED);
    /* Durable plan: failed commit never advances RAM cursor, reboot requires explicit identity. */
    assert(!ufi_analysis_command(create,49,out,&size));
    uint8_t persist[28]={30,1,1,0};wr(persist+4,ufi_config_generation());wr(persist+8,media_token);persist[12]=77;
    assert(!ufi_analysis_command(persist,28,out,&size) && plan.persistent && plan.sequence==1);
    step[5]=0;step[7]=0;assert(!ufi_analysis_command(step,8,out,&size));pump();step[7]=1;
    storage_failure=true;assert(ufi_analysis_command(step,8,out,&size)==UFI_ERR_STORAGE && plan.cursor==0);
    storage_failure=false;assert(!ufi_analysis_command(step,8,out,&size) && plan.cursor==1 && plan.sequence==2);
    memset(&plan,0,sizeof(plan));memset(&job,0,sizeof(job));persist[1]=2;
    assert(ufi_analysis_command(persist,28,out,&size)==UFI_ERR_BAD_ARGS); /* missing confirmation flag */
    persist[2]=3;persist[12]=78;
    assert(ufi_analysis_command(persist,28,out,&size)==UFI_ERR_MEDIA_CHANGED && !plan.id);
    persist[12]=77;assert(!ufi_analysis_command(persist,28,out,&size) && plan.cursor==1 && plan.state==P_READY);
    memset(&plan,0,sizeof(plan));saved_slots[0][50]^=1; /* corrupt latest -> older valid checkpoint */
    assert(!ufi_analysis_command(persist,28,out,&size) && plan.cursor==0 && plan.sequence==1);
    memset(&plan,0,sizeof(plan));saved_slots[1][50]^=1;
    assert(ufi_analysis_command(persist,28,out,&size)==UFI_ERR_STORAGE && !plan.id);
    /* Op 18 timing presets (1.18): [18, id] applies, 1/2 valid, others BAD_ARGS, three-byte form rejected. */
    uint8_t preset[3]={18,2,0};
    assert(!ufi_analysis_command(preset,2,out,&size) && size==40 && out[1]==2 && preset_applied==2);
    preset[1]=1;assert(!ufi_analysis_command(preset,2,out,&size) && preset_applied==1);
    preset[1]=3;assert(!ufi_analysis_command(preset,2,out,&size) && preset_applied==3);     /* 8-inch (1.18) */
    preset[1]=4;assert(ufi_analysis_command(preset,2,out,&size)==UFI_ERR_BAD_ARGS && preset_applied==3);
    preset[1]=0;assert(ufi_analysis_command(preset,2,out,&size)==UFI_ERR_BAD_ARGS);
    preset[1]=2;assert(ufi_analysis_command(preset,3,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 33: Apple II GCR options (format 7) - read defaults, reject bad schema/flags/marks, set and read back */
    uint8_t ao[14]={33};assert(!ufi_analysis_command(ao,1,out,&size) && size==13 && out[0]==1 && out[3]==0x96 && out[6]==0xad && !out[11] && !out[12]);
    memcpy(ao+1,out,13);ao[1]=2;assert(ufi_analysis_command(ao,14,out,&size)==UFI_ERR_BAD_ARGS);
    ao[1]=1;ao[12]=0x40;assert(ufi_analysis_command(ao,14,out,&size)==UFI_ERR_BAD_ARGS);
    ao[12]=0x03;ao[2]=0xd4;ao[7]=0xb5;assert(!ufi_analysis_command(ao,14,out,&size) && size==13 && out[1]==0xd4 && out[6]==0xb5 && out[11]==0x03);
    ao[2]=0x54;assert(ufi_analysis_command(ao,14,out,&size)==UFI_ERR_BAD_ARGS);
    assert(!ufi_analysis_command(ao,1,out,&size) && out[1]==0xd4 && out[11]==0x03);
    ao[2]=0xd5;ao[7]=0xad;ao[12]=0;assert(!ufi_analysis_command(ao,14,out,&size) && !out[11]);
    /* Op 34: decoder tolerance flags for formats 1-6 - strict by default, schema and mask checked, busy refused */
    uint8_t to[4]={34};assert(!ufi_analysis_command(to,1,out,&size) && size==3 && out[0]==1 && !out[1] && !out[2]);
    to[1]=2;to[2]=1;assert(ufi_analysis_command(to,4,out,&size)==UFI_ERR_BAD_ARGS);
    to[1]=1;to[2]=0x20;assert(ufi_analysis_command(to,4,out,&size)==UFI_ERR_BAD_ARGS);
    to[2]=0x1f;busy=true;assert(ufi_analysis_command(to,4,out,&size)==UFI_ERR_BUSY);busy=false;
    assert(!ufi_analysis_command(to,4,out,&size) && size==3 && out[1]==0x1f && !out[2] && mfm_get_tolerance()==0x1f);
    assert(ufi_analysis_command(to,3,out,&size)==UFI_ERR_BAD_ARGS);
    assert(!ufi_analysis_command(to,1,out,&size) && out[1]==0x1f);
    to[2]=0;assert(!ufi_analysis_command(to,4,out,&size) && !out[1] && !mfm_get_tolerance());
    /* Op 37: session write lock - query shows lock and jumper, engage-only, idempotent, no disengage, bad payloads refused */
    uint8_t lk[3]={37};assert(!ufi_analysis_command(lk,1,out,&size) && size==2 && out[0]==1 && !out[1]);
    jumper_lock=true;assert(!ufi_analysis_command(lk,1,out,&size) && out[1]==2);jumper_lock=false;
    lk[1]=0;assert(ufi_analysis_command(lk,2,out,&size)==UFI_ERR_BAD_ARGS && !soft_lock);
    lk[1]=2;assert(ufi_analysis_command(lk,2,out,&size)==UFI_ERR_BAD_ARGS && !soft_lock);
    lk[1]=1;assert(ufi_analysis_command(lk,3,out,&size)==UFI_ERR_BAD_ARGS && !soft_lock);
    busy=true;assert(!ufi_analysis_command(lk,2,out,&size) && size==2 && out[1]==1 && soft_lock);busy=false;
    assert(!ufi_analysis_command(lk,2,out,&size) && out[1]==1);
    lk[1]=0;assert(ufi_analysis_command(lk,2,out,&size)==UFI_ERR_BAD_ARGS && soft_lock);
    jumper_lock=true;assert(!ufi_analysis_command(lk,1,out,&size) && out[1]==3);jumper_lock=false;
    soft_lock=false;
    /* Op 42: weak windows of the last ANALYZE - 68 bytes straight from the analyser, read-only */
    uint8_t ww[2]={42};assert(!ufi_analysis_command(ww,1,out,&size) && size==68 && out[0]==1 && out[1]==3 && out[2]==40 && out[3]==0x91 && out[4+17]==6 && !out[4+16]);
    busy=true;assert(!ufi_analysis_command(ww,1,out,&size) && size==68);busy=false;
    ww[1]=0;assert(ufi_analysis_command(ww,2,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 41: speed check before writing - read/set tolerance, range and busy checked, last measurement reported */
    uint8_t sp[4]={41};assert(!ufi_analysis_command(sp,1,out,&size) && size==11 && out[0]==1 && !out[1] && !out[2] && rd(out+3)==55500000u && rd(out+7)==55000000u);
    sp[1]=1;sp[2]=20;assert(!ufi_analysis_command(sp,4,out,&size) && size==11 && out[1]==20 && speed_tol==20);
    sp[2]=0xf5;sp[3]=1;assert(ufi_analysis_command(sp,4,out,&size)==UFI_ERR_BAD_ARGS && speed_tol==20);   /* 501 */
    sp[2]=0xf4;assert(!ufi_analysis_command(sp,4,out,&size) && speed_tol==500);                           /* 500 ok */
    sp[1]=2;sp[2]=1;sp[3]=0;assert(ufi_analysis_command(sp,4,out,&size)==UFI_ERR_BAD_ARGS);
    sp[1]=1;busy=true;assert(ufi_analysis_command(sp,4,out,&size)==UFI_ERR_BUSY && speed_tol==500);
    assert(!ufi_analysis_command(sp,1,out,&size) && out[1]==0xf4 && out[2]==1);busy=false;
    sp[2]=0;assert(!ufi_analysis_command(sp,4,out,&size) && !speed_tol);
    assert(ufi_analysis_command(sp,3,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 40: per-sector verify report - header only until a verified WRITE ran, read-only */
    uint8_t vr2[2]={40};assert(!ufi_analysis_command(vr2,1,out,&size) && size==6 && out[0]==1 && !out[1] && !out[2] && !out[3]);
    ufi_quality_verify_begin(0x4a,1);
    assert(!ufi_analysis_command(vr2,1,out,&size) && size==6 && out[3]==8 && out[4]==0x4a && out[5]==1 && !out[1]);
    busy=true;assert(!ufi_analysis_command(vr2,1,out,&size) && size==6);busy=false;
    vr2[1]=0;assert(ufi_analysis_command(vr2,2,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 39: last refusal - 6 bytes straight from the v2 layer, read-only, no payload allowed */
    uint8_t lr[2]={39};assert(!ufi_analysis_command(lr,1,out,&size) && size==6 && !memcmp(out,refusal_stub,6));
    busy=true;assert(!ufi_analysis_command(lr,1,out,&size) && size==6);busy=false;
    lr[1]=0;assert(ufi_analysis_command(lr,2,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 38: flag masks the parsers accept - fixed 16-byte answer, values are the parser constants */
    uint8_t fm[2]={38};assert(!ufi_analysis_command(fm,1,out,&size) && size==16 && out[0]==1);
    assert(out[1]==UFI_V2_READ_FLAG_MASK && out[2]==0xff && out[3]==UFI_V2_WRITE_FLAGS2_MASK && out[4]==0xff && out[5]==7);
    assert((out[6]|out[7]<<8)==APPLE_GCR_FLAG_MASK && (out[8]|out[9]<<8)==MFM_TOL_MASK);
    assert((out[10]|out[11]<<8)==QUALITY_FEATURE_MASK && (out[12]|out[13]<<8)==EVIDENCE_MASK && out[14]==0xb1 && !out[15]);
    busy=true;assert(!ufi_analysis_command(fm,1,out,&size) && size==16);busy=false;   /* read-only, never BUSY */
    fm[1]=1;assert(ufi_analysis_command(fm,2,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 35: Apple II nibble statistics - 178 bytes, schema 1, no revolutions before a format 7 analysis */
    uint8_t ns[2]={35,0};assert(!ufi_analysis_command(ns,1,out,&size) && size==274 && out[0]==2 && out[1]==0);
    assert(ufi_analysis_command(ns,2,out,&size)==UFI_ERR_BAD_ARGS);
    /* Op 36: loadable 6-and-2 table - read the standard, reject duplicates/low symbols/bad schema, set and restore */
    uint8_t tb[66]={36,1};assert(!ufi_analysis_command(tb,1,out,&size) && size==65 && out[0]==1 && out[1]==0x96 && out[64]==0xff);
    memcpy(tb+2,out+1,64);tb[2]=0x97;assert(ufi_analysis_command(tb,66,out,&size)==UFI_ERR_BAD_ARGS);
    tb[2]=0x16;assert(ufi_analysis_command(tb,66,out,&size)==UFI_ERR_BAD_ARGS);
    tb[2]=0x96;tb[1]=2;assert(ufi_analysis_command(tb,66,out,&size)==UFI_ERR_BAD_ARGS);tb[1]=1;
    tb[2]=0x97;tb[3]=0x96;busy=true;assert(ufi_analysis_command(tb,66,out,&size)==UFI_ERR_BUSY);busy=false;
    assert(!ufi_analysis_command(tb,66,out,&size) && out[1]==0x97 && out[2]==0x96 && mfm_apple_get_table()[0]==0x97);
    tb[2]=0x96;tb[3]=0x97;assert(!ufi_analysis_command(tb,66,out,&size) && out[1]==0x96);
    assert(ufi_analysis_command(tb,10,out,&size)==UFI_ERR_BAD_ARGS);
    /* storing a 40-byte profile also writes UFIPROF.BIN; reading and presets do not */
    uint8_t store[41]={18,1};assert(!ufi_analysis_command(store,41,out,&size) && profiles_saved==1);
    assert(!ufi_analysis_command(preset,1,out,&size) && !ufi_analysis_command(preset,2,out,&size) && profiles_saved==1);
    /* Op 46: track survey - discovery header, argument checks, run with partial reads, entry layout,
     * step errors, timeout, three failures in a row, abort, media change, cancel, plan guard */
    uint8_t svq[1]={46},svs[6]={46,1,5,4,3,0},sva[2]={46,0};
    assert(!ufi_analysis_command(svq,1,out,&size) && size==28 && out[0]==1 && out[1]==28 && !out[2] && !out[3] && out[8]==20 && !(out[10]|out[11]<<8) && !(out[12]|out[13]<<8));
    busy=true;assert(!ufi_analysis_command(svq,1,out,&size) && size==28);busy=false;          /* read-only, never BUSY */
    assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BAD_ARGS);                            /* first > last */
    svs[2]=0;svs[3]=84;assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BAD_ARGS);           /* track 84 */
    svs[3]=2;svs[4]=0;assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BAD_ARGS);            /* no side */
    svs[4]=4;assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BAD_ARGS);                    /* side mask 4 */
    svs[4]=3;svs[5]=2;assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BAD_ARGS);            /* flag bit 1 */
    svs[5]=1;assert(ufi_analysis_command(svs,5,out,&size)==UFI_ERR_BAD_ARGS);                    /* short */
    sva[1]=2;assert(ufi_analysis_command(sva,2,out,&size)==UFI_ERR_BAD_ARGS);sva[1]=0;
    busy=true;assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BUSY);busy=false;
    drive_present=false;assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_NO_DRIVE);drive_present=true;
    uint32_t c0=captures;ticks=1000;
    assert(!ufi_analysis_command(svs,6,out,&size) && size==28 && out[2]==1 && !out[3] && out[4]==0 && out[5]==2 && out[6]==3 && out[7]==1 && out[8]==20);
    assert((out[12]|out[13]<<8)==6 && !(out[10]|out[11]<<8) && rd(out+20)==ufi_config_generation() && rd(out+24)==media_token && captures==c0+1);
    assert(ufi_survey_active() && ufi_operation_busy() && g_capture.current_track==0 && g_capture.current_side==0);
    assert(ufi_analysis_command(p,33,out,&size)==UFI_ERR_BUSY);                                /* job start while the survey runs */
    assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BUSY);
    ticks=1500;assert(!ufi_analysis_command(svq,1,out,&size) && size==28 && out[2]==1 && rd(out+16)==500);   /* readable while running */
    ufi_survey_service();assert(ufi_survey_active() && captures==c0+1);                       /* capture still running */
    g_capture.state=CAPTURE_COMPLETE;ufi_survey_service();
    assert(ufi_survey_active() && captures==c0+2 && g_capture.current_track==0 && g_capture.current_side==1 && classify_calls==1);
    assert(!ufi_analysis_command(svq,1,out,&size) && size==48 && (out[10]|out[11]<<8)==1);
    const uint8_t* se=out+28;
    assert(se[0]==0 && se[1]==0 && se[2]==0 && se[3]==2 && (se[4]|se[5]<<8)==2000 && se[6]==9 && se[7]==3 && rd(se+8)==55000000u && rd(se+12)==revolution.count && (se[16]|se[17]<<8)==3000 && (se[18]|se[19]<<8)==4000);
    g_capture.state=CAPTURE_ERROR;g_capture.error_code=3;ufi_survey_service();g_capture.error_code=0;   /* NO_INDEX on one step: recorded, survey continues */
    assert(ufi_survey_active() && captures==c0+3 && g_capture.current_track==1 && g_capture.current_side==0);
    g_capture.state=CAPTURE_COMPLETE;classify_error=UFI_ERR_TIMEOUT;ufi_survey_service();classify_error=0;   /* classification timeout: step error too */
    ticks=2000;survey_pump();
    assert(!ufi_analysis_command(svq,1,out,&size) && size==28+6*20 && out[2]==2 && !out[3] && (out[10]|out[11]<<8)==6 && rd(out+16)==1000 && classify_calls==5);
    se=out+28;
    assert(se[20]==0 && se[21]==1 && se[22]==4 && !se[23] && !rd(se+28) && !rd(se+32));        /* entry 1: track 0 side 1, NO_INDEX, rest zero */
    assert(se[40]==1 && se[41]==0 && se[42]==5 && !se[43]);                                  /* entry 2: TIMEOUT */
    assert(se[60]==1 && se[61]==1 && !se[62] && rd(se+72)==revolution.count+1u && se[80]==2 && se[81]==0 && se[100]==2 && se[101]==1 && !se[102]);
    assert(!ufi_survey_active() && !ufi_operation_busy() && g_capture.state==CAPTURE_IDLE);
    /* side 1 only, no sector count; a step without a complete revolution within 4 s ends with TIMEOUT and an aborted capture */
    svs[2]=3;svs[3]=3;svs[4]=2;svs[5]=0;ticks=5000;uint32_t a0=aborts;
    assert(!ufi_analysis_command(svs,6,out,&size) && (out[12]|out[13]<<8)==1 && g_capture.current_track==3 && g_capture.current_side==1);
    ticks=9000;ufi_survey_service();assert(ufi_survey_active() && aborts==a0);
    ticks=9001;ufi_survey_service();assert(!ufi_survey_active() && aborts==a0+1);
    assert(!ufi_analysis_command(svq,1,out,&size) && size==48 && out[2]==2 && !out[3] && out[28]==3 && out[29]==1 && out[30]==5);
    svs[4]=1;assert(!ufi_analysis_command(svs,6,out,&size));survey_pump();
    assert(!ufi_analysis_command(svq,1,out,&size) && size==48 && out[29]==0 && !out[30] && out[31]==2 && !out[34]);   /* side 0, sectors not counted */
    /* three start failures in a row end the survey with that status; the entries keep the failed steps */
    start_error=UFI_ERR_SEEK_FAIL;svs[2]=10;svs[3]=20;svs[4]=3;
    assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_SEEK_FAIL && size==28 && out[2]==3 && out[3]==3 && (out[10]|out[11]<<8)==3 && !ufi_survey_active());
    start_error=0;
    assert(!ufi_analysis_command(svq,1,out,&size) && size==28+60 && out[28+2]==3 && out[28+22]==3 && out[28+42]==3 && out[28+40]==11 && out[28+41]==0);
    /* abort by request, idempotent; media change and the general cancel end it with 21 / 20 */
    svs[2]=0;svs[3]=1;a0=aborts;assert(!ufi_analysis_command(svs,6,out,&size) && ufi_survey_active());
    assert(!ufi_analysis_command(sva,2,out,&size) && size==28 && out[2]==3 && out[3]==20 && aborts==a0+1 && !ufi_survey_active() && g_capture.state==CAPTURE_IDLE);
    assert(!ufi_analysis_command(sva,2,out,&size) && out[2]==3 && out[3]==20 && aborts==a0+1);
    assert(!ufi_analysis_command(svs,6,out,&size));media_token++;ufi_survey_service();
    assert(!ufi_analysis_command(svq,1,out,&size) && out[2]==3 && out[3]==21 && !ufi_survey_active());
    assert(!ufi_analysis_command(svs,6,out,&size));cancelled=true;ufi_survey_service();cancelled=false;
    assert(!ufi_analysis_command(svq,1,out,&size) && out[2]==3 && out[3]==20);
    assert(!ufi_analysis_command(svs,6,out,&size));ufi_analysis_job_abort();                 /* general ABORT path */
    assert(!ufi_survey_active() && !ufi_analysis_command(svq,1,out,&size) && out[3]==20);
    assert(!ufi_analysis_command(svs,6,out,&size));g_capture.state=CAPTURE_COMPLETE;classify_error=UFI_ERR_CANCELLED;ufi_survey_service();classify_error=0;
    assert(!ufi_survey_active() && !ufi_analysis_command(svq,1,out,&size) && out[3]==20 && !(out[10]|out[11]<<8));
    /* a live multitrack plan blocks the start, not the read */
    uint8_t create2[49]={27};wr(create2+1,0x77);wr(create2+5,ufi_config_generation());wr(create2+9,media_token);
    create2[13]=0;create2[14]=1;create2[15]=2;memcpy(create2+16,p,33);
    assert(!ufi_analysis_command(create2,49,out,&size) && out[1]==P_READY);
    assert(ufi_analysis_command(svs,6,out,&size)==UFI_ERR_BUSY && !ufi_analysis_command(svq,1,out,&size) && size==28);
    uint8_t end2[8]={29};wr(end2+1,0x77);end2[7]=2;assert(!ufi_analysis_command(end2,8,out,&size) && out[1]==P_DONE);
    assert(!ufi_analysis_command(svs,6,out,&size));survey_pump();assert(!ufi_analysis_command(svq,1,out,&size) && out[2]==2 && (out[10]|out[11]<<8)==4);
    puts("analysis jobs: pages/CRC, retries, durable multitrack ACK/reset/torn checkpoint, media binding, cancel, timing presets and track survey passed");
}
'''
    run('regression_analysis_job',code,('src/ufi_mfm.c','src/ufi_verify.c','src/ufi_compare.c','src/ufi_quality.c','src/ufi_evidence.c','src/ufi_drive_extra.c','src/ufi_media.c','src/ufi_capabilities.c'))


def selftest_selection():
    code=BASE+typedef(FW/'src/ufi_selftest.c','st_item_t')+r'''
static unsigned calls,feeds;
#define UFI_WATCHDOG_FEED() (++feeds)
static void hit(st_item_t* p){calls|=1u<<(p->id-1);p->value=p->id*100;}
'''
    for name in ('timer','usb_sof','psram','sd','board_id','usbc','fdd_lines','iec_loop','index_loop','last_reset','stack'):
        code+=f'static void t_{name}(st_item_t* p){{hit(p);}}\n'
    code+='static void t_rail(st_item_t* p,int r){(void)r;hit(p);}\n'
    code+=function('ufi_selftest.c','ufi_selftest_selected')+r'''
int main(void){
    uint8_t out[13*6+1];memset(out,0xa5,sizeof(out));
    assert(ufi_selftest_selected(out,13,0x1803)==4 && calls==0x1803 && feeds==4);
    assert(out[0]==1 && out[6]==2 && out[12]==12 && out[18]==13 && out[24]==0xa5);
    calls=0;assert(ufi_selftest_selected(out,1,0x1fff)==1 && calls==1);
    calls=0;assert(!ufi_selftest_selected(out,0,0x1fff) && !calls);
}
'''
    run('regression_selftest_selection',code)


if __name__ == '__main__':
    main()
    selftest_selection()
