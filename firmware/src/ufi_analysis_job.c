/* Native analysis jobs: bounded work, retained final report and retry history. */
#include "ufi_firmware.h"
#include "ufi_quality.h"
#include "ufi_analysis_job.h"
#include "ufi_cancel.h"
#include "ufi_job.h"
#include "ufi_evidence.h"
#include "ufi_drive_extra.h"
#include "ufi_compare.h"
#include "ufi_media.h"
#include "ufi_capabilities.h"
#include "ufi_resources.h"
#include "ufi_raw_verify.h"
#include <string.h>
extern capture_context_t g_capture;
enum { A_IDLE,A_CAPTURE,A_ANALYZE,A_DONE,A_FAILED };
/* Separate report selection and retry policy; legacy job flag values stay fixed. */
static uint8_t recovery_config[11]={1,0,0,0,2,2,0,110,0,0,0};
static struct {
    uint32_t id,started,attempt_started,budget,generation,elapsed,capture_id;
    uint8_t state,status,attempt,max_attempts,track,side,revs,c,h,n,position;
    uint16_t flags;
    uint64_t expected,seen;
    quality_options_t options;
    uint8_t recovery[11],confirmed,stagnant,stop_reason,previous_n,best_good;
    uint32_t best_errors;
    verify_sector_t previous[VERIFY_MAX_SECTORS];
    uint8_t media_snapshot[MEDIA_STATUS_SIZE];
    uint32_t media_token;
    uint8_t profile_check[MEDIA_CHECK_SIZE];
    bool profile_checked;
    uint8_t port_snapshot[24];
    uint8_t history[8][40];
    uint8_t assessment[8][20],recognition_profile[12];
} job;
/* Stop-and-wait multitrack plan: one report remains retained until host ACK.
 * Session-local, read/analysis only; no writes are replayed after reconnect. */
enum {P_IDLE,P_READY,P_RUNNING,P_RESULT,P_DONE,P_FAILED};
static struct {
    uint32_t id,config,media,child;
    uint16_t cursor,total;
    uint8_t first,last,sides,state,error,length;
    uint8_t request[61];
    bool starting;
    bool persistent;
    uint8_t media_id[16];
    uint32_t sequence;
} plan;
static bool plan_live(void){return plan.state && plan.state!=P_DONE;}
/* Cause mask for retained attempt assessments; independent of legacy status. */
enum {CAUSE_MISSING=1,CAUSE_HEADER=2,CAUSE_DATA=4,CAUSE_CODING=8,CAUSE_CONTENT=16,
      CAUSE_TRANSPORT=32,CAUSE_BUFFER=64,CAUSE_POSITION=128,CAUSE_INDEX=256,
      CAUSE_MEDIA=512,CAUSE_TIMEOUT=1024,CAUSE_CANCEL=2048,CAUSE_CONFIRM=4096,
      CAUSE_INCOMPLETE=8192,CAUSE_NO_FORMAT=16384};
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static void wr(uint8_t* p,uint32_t v){for(unsigned i=0;i<4;i++)p[i]=(uint8_t)(v>>(8*i));}
static void wr64(uint8_t* p,uint64_t v){wr(p,(uint32_t)v);wr(p+4,(uint32_t)(v>>32));}
bool ufi_analysis_job_active(void){return job.state==A_CAPTURE || job.state==A_ANALYZE;}
static void record(int ret);
static void finish(int ret) {
    ufi_evidence_finish();
    ufi_quality_async_cancel();
    if(job.state==A_CAPTURE)ufi_capture_abort();
    g_capture.state=CAPTURE_IDLE;
    job.elapsed=HAL_GetTick()-job.started;
    job.status=(uint8_t)(ret<0?-ret:ret);job.state=ret?A_FAILED:A_DONE;
}
void ufi_analysis_job_abort(void){if(ufi_analysis_job_active()){record(UFI_ERR_CANCELLED);finish(UFI_ERR_CANCELLED);}}
static int capture(void) {
    job.attempt++;job.attempt_started=HAL_GetTick();job.state=A_CAPTURE;
    job.generation=0;job.seen=0;job.capture_id=0;
    job.profile_checked=false;
    ufi_drive_port_report(job.port_snapshot);
    ufi_quality_clear();
    ufi_evidence_arm(job.flags);
    uint32_t previous=evidence_id();
    int ret=ufi_capture_start_q(job.track,job.side,job.revs,0,(uint8_t)((job.position>>4)&3u));
    if(!ret)g_capture.current_quarter=job.position;
    ufi_evidence_disarm();job.capture_id=evidence_id()==previous?0:evidence_id();
    if(ret){record(ret);finish(ret);}
    return ret;
}
static void record(int ret) {
    const quality_t* q=ufi_quality_result();uint8_t* h=job.history[job.attempt-1];
    uint8_t* a=job.assessment[job.attempt-1];uint32_t causes=0,valid=0,mismatch=0;
    uint8_t media[40];memcpy(media,job.media_snapshot,40);
    if(job.capture_id && evidence_id()==job.capture_id)(void)evidence_pack(3,media,sizeof(media));
    memcpy(job.recognition_profile,media+20,12);
    bool explicit_ids=(job.flags&4)!=0;
    bool check=explicit_ids || ((job.flags&32) && media[26]==1 && q && q->format==media[20] && q->format!=5);
    uint32_t recognition=quality_recognition(q,job.expected,explicit_ids?job.c:job.track,
                         explicit_ids?job.h:job.side,job.n,check,&valid,&mismatch);
    if(media[20])recognition|=RECOG_PROFILE_PRESENT;
    if(media[26]==1)recognition|=RECOG_PROFILE_DECLARED;
    if(ret)recognition=(recognition&~RECOG_SECTOR_SET_CONFIRMED)|RECOG_INCOMPLETE;
    if(mismatch)causes|=CAUSE_POSITION;
    if(ret==UFI_ERR_DMA)causes|=CAUSE_TRANSPORT;
    if(ret==UFI_ERR_BUFFER_FULL)causes|=CAUSE_BUFFER;
    if(ret==UFI_ERR_SEEK_FAIL || ret==UFI_ERR_NO_DRIVE)causes|=CAUSE_POSITION;
    if(ret==UFI_ERR_NO_INDEX)causes|=CAUSE_INDEX;
    if(ret==UFI_ERR_MEDIA_CHANGED)causes|=CAUSE_MEDIA;
    if(ret==UFI_ERR_TIMEOUT)causes|=CAUSE_TIMEOUT;
    if(ret==UFI_ERR_CANCELLED)causes|=CAUSE_CANCEL;
    if(recognition&RECOG_INCOMPLETE)causes|=CAUSE_INCOMPLETE;
    if(q && !(recognition&RECOG_FORMAT_OBSERVED))causes|=CAUSE_NO_FORMAT;
    wr(a,recognition);wr(a+4,causes);wr(a+8,valid);wr(a+12,mismatch);a[16]=job.state;
    h[3]=(uint8_t)(ret<0?-ret:ret);wr(h+4,HAL_GetTick()-job.attempt_started);
    job.seen=0;
    if(!q)return;
    h[0]=q->format;h[1]=q->revs;h[2]=q->n;job.generation=q->generation;wr(h+8,q->generation);
    uint32_t conflicts=0,headers=0,data=0,coding=0;
    for(unsigned i=0;i<q->n;i++) {
        const quality_sector_t* s=&q->sectors[i];
        if(s->flags&1)conflicts++;
        if(s->seen && !(s->flags&21) && s->id.r<64 && s->id.c==job.c && s->id.h==job.h && s->id.n==job.n)job.seen|=1ull<<s->id.r;
    }
    for(unsigned r=0;r<q->revs;r++){headers+=q->attempts[r][0].header_errors;data+=q->attempts[r][0].data_errors;coding+=q->extra.diagnostics[r][0][0];}
    wr64(h+12,job.seen);wr(h+20,conflicts);wr(h+24,headers);wr(h+28,data);wr(h+32,coding);wr(h+36,q->dropped+q->extra.dropped);
    if(job.expected&~job.seen)causes|=CAUSE_MISSING;
    if(headers)causes|=CAUSE_HEADER;
    if(data)causes|=CAUSE_DATA;
    if(coding)causes|=CAUSE_CODING;
    if(conflicts)causes|=CAUSE_CONTENT;
    if(q->dropped || q->extra.dropped)causes|=CAUSE_INCOMPLETE;
    wr(a+4,causes);
}
static bool confirmed_content(const quality_t* q,bool clean){
    bool same=clean && q && q->n && q->n==job.previous_n;
    if(same)for(unsigned i=0;i<q->n;i++){
        const quality_sector_t* s=&q->sectors[i];bool found=false;
        for(unsigned k=0;k<job.previous_n;k++){
            const verify_sector_t* p=&job.previous[k];
            if(p->c==s->id.c && p->h==s->id.h && p->r==s->id.r && p->n==s->id.n && p->crc==s->crc){found=true;break;}
        }
        if(!found){same=false;break;}
    }
    if(!clean || !q || !q->n){job.confirmed=job.previous_n=0;return false;}
    job.confirmed=same?(uint8_t)(job.confirmed+1):1;job.previous_n=q->n;
    for(unsigned i=0;i<q->n;i++){
        const quality_sector_t* s=&q->sectors[i];
        job.previous[i]=(verify_sector_t){s->id.c,s->id.h,s->id.r,s->id.n,s->crc};
    }
    return job.confirmed>=job.recovery[4];
}
void ufi_analysis_job_service(void) {
    if(!ufi_analysis_job_active())return;
    if(!ufi_drive_media_matches(job.media_token)){record(UFI_ERR_MEDIA_CHANGED);finish(UFI_ERR_MEDIA_CHANGED);return;}
    if(ufi_cancel_requested()){record(UFI_ERR_CANCELLED);finish(UFI_ERR_CANCELLED);return;}
    if(HAL_GetTick()-job.started>=job.budget){record(UFI_ERR_TIMEOUT);finish(UFI_ERR_TIMEOUT);return;}
    int ret=UFI_ERR_BUSY;
    if(job.state==A_CAPTURE) {
        capture_state_t state=ufi_capture_get_state();
        if(state==CAPTURE_ERROR){
            int error=g_capture.error_code==4?UFI_ERR_MEDIA_CHANGED:g_capture.error_code==3?UFI_ERR_NO_INDEX:g_capture.error_code==2?UFI_ERR_DMA:UFI_ERR_BUFFER_FULL;
            record(error);finish(error);return;
        }
        if(state!=CAPTURE_COMPLETE) {
            if(HAL_GetTick()-job.attempt_started>4000){record(UFI_ERR_TIMEOUT);finish(UFI_ERR_TIMEOUT);}
            return;
        }
        ufi_evidence_finish();g_capture.state=CAPTURE_IDLE;job.state=A_ANALYZE;
        ret=ufi_quality_async_start(&job.options);
        if(!ret)return;
    } else ret=ufi_quality_async_step();
    if(ret==UFI_ERR_BUSY)return;
    record(ret);
    if(ret){finish(ret);return;}
    const quality_t* observed=ufi_quality_result();
    if(job.flags&32){
        uint8_t context[160]={0},media[40]={0};uint32_t period=0;
        bool context_ok=job.capture_id && evidence_id()==job.capture_id &&
            evidence_pack(0,context,sizeof(context))==160 && evidence_pack(3,media,sizeof(media))==40;
        uint64_t total=0;unsigned count=0;
        if(observed && context_ok && !rd(context+32) && !context[40] && !context[41] &&
           !context[46] && !context[47]){
            for(unsigned i=0;i<observed->revs && i<QUALITY_REVS;i++)
                if(observed->revolution_ticks[i]){total+=observed->revolution_ticks[i];count++;}
            if(count)period=(uint32_t)(total/count);
        }
        if(!context_ok)memcpy(media,job.media_snapshot,40);
        bool decoded=observed && observed->n && !observed->ambiguous;
        media_check(media,context_ok?(context[64] || context[65]?2:1):0,
                    decoded?observed->format:0,decoded?observed->rate:0,
                    period,FLUX_TIMER_FREQ,job.profile_check);
        uint8_t conflicts=job.port_snapshot[7];
        if(conflicts&3)job.profile_check[4]|=MEDIA_CHECK_LINE_CONFLICT;
        if(conflicts&4)job.profile_check[4]|=MEDIA_CHECK_UNSUPPORTED_CONFIGURATION;
        wr(job.profile_check+8,rd(job.profile_check+8)|((uint32_t)conflicts<<16));
        if(observed && observed->format==6){
            uint8_t summary[48];quality_observation_pack(observed,summary,sizeof(summary));
            job.profile_check[4]|=MEDIA_CHECK_ALIGNMENT_UNCERTAIN;
            if(rd(summary+4)&1){job.profile_check[4]|=MEDIA_CHECK_ZONE_MISMATCH;job.profile_check[10]|=8;}
        }
        job.profile_checked=true;
        if(rd(job.assessment[job.attempt-1])&RECOG_HEADER_POSITION_MISMATCH){
            job.profile_check[4]|=MEDIA_CHECK_HEADER_POSITION_MISMATCH;
            wr(job.profile_check+8,rd(job.profile_check+8)|(1u<<20));
        }
    }
    if((rd(job.assessment[job.attempt-1])&RECOG_FORMAT_OBSERVED) && observed && !observed->ambiguous)
        ufi_drive_media_observed(observed->format,observed->rate);
    const uint8_t* h=job.history[job.attempt-1];
    bool missing=(job.seen&job.expected)!=job.expected;
    bool errors=rd(h+20)||rd(h+24)||rd(h+28)||rd(h+32);
    bool retry=((job.flags&1)&&missing)||((job.flags&2)&&errors);
    uint8_t policy=job.recovery[3];
    if(policy==1){
        const quality_t* q=ufi_quality_result();bool clean=!errors && (!(job.flags&4)||!missing);
        if(q && (q->ambiguous || q->dropped || q->status))clean=false;
        if(q)for(unsigned i=0;i<q->n;i++)if(!q->sectors[i].seen || q->sectors[i].bad || (q->sectors[i].flags&21))clean=false;
        retry=!confirmed_content(q,clean);if(!retry)job.stop_reason=1;
        uint8_t* a=job.assessment[job.attempt-1];
        if(retry)wr(a+4,rd(a+4)|CAUSE_CONFIRM);
        else wr(a,rd(a)|RECOG_CONTENT_CONFIRMED);
    } else if(policy==2 && retry){
        const quality_t* q=ufi_quality_result();uint8_t good=0;
        if(q)for(unsigned i=0;i<q->n;i++)if(q->sectors[i].seen && !(q->sectors[i].flags&21))good++;
        uint64_t sum=(uint64_t)rd(h+20)+rd(h+24)+rd(h+28)+rd(h+32);
        uint32_t error_count=sum>UINT32_MAX?UINT32_MAX:(uint32_t)sum;
        if(job.attempt==1 || good>job.best_good || (good==job.best_good && error_count<job.best_errors)){
            job.best_good=good;job.best_errors=error_count;job.stagnant=0;
        } else job.stagnant++;
        if(job.stagnant>=job.recovery[5]){retry=false;job.stop_reason=2;}
    }
    job.assessment[job.attempt-1][17]=job.stop_reason==1?2:job.stop_reason==2?3:0;
    if(retry && job.attempt<job.max_attempts){job.assessment[job.attempt-1][17]=1;(void)capture();return;}
    if(retry)job.assessment[job.attempt-1][17]=4;
    if(retry){job.stop_reason=3;if(policy==1){finish(UFI_ERR_DMA);return;}}
    finish((job.flags&4)&&missing?UFI_ERR_DMA:UFI_OK);
}
static uint16_t status(uint8_t* out) {
    memset(out,0,64);out[0]=1;out[1]=job.state;out[2]=job.status;out[3]=job.attempt;
    wr(out+4,job.id);wr(out+8,ufi_analysis_job_active()?HAL_GetTick()-job.started:job.elapsed);wr(out+12,job.generation);
    wr64(out+16,job.expected);wr64(out+24,job.seen);wr64(out+32,job.expected&~job.seen);
    uint32_t edge=0;ufi_quality_async_progress(out+40,out+41,out+42,&edge);wr(out+44,edge);
    out[48]=job.max_attempts;out[49]=(uint8_t)job.flags;out[50]=job.c;out[51]=job.h;out[52]=job.n;
    out[53]=job.stop_reason;out[54]=job.confirmed;out[55]=job.stagnant;
    wr(out+56,job.budget);wr(out+60,job.capture_id);return 64;
}
static uint16_t capabilities(uint8_t* out) {
    memset(out,0,192);out[0]=1;out[1]=5;out[2]=0xff;out[3]=7; /* five legacy descriptors; Mac via op 19 */
    wr(out+4,63);wr(out+8,60000);wr(out+12,10000);wr(out+16,1024);
    out[20]=8;out[21]=8;out[22]=48;out[23]=4;out[24]=1;out[25]=1;out[26]=1;
    wr(out+28,QUALITY_MAX_WIRE);wr(out+32,FLUX_TIMER_FREQ);
    static const uint32_t rates[]={250000,300000,500000,1000000,125000,150000};
    for(unsigned i=0;i<6;i++)wr(out+40+4*i,rates[i]);
    static const uint16_t rate_masks[]={0,0x0f,0x0f,0x37,0x04,0x1c1};
    for(unsigned f=1;f<=5;f++){
        uint8_t* p=out+64+12*(f-1);p[0]=(uint8_t)f;p[1]=1;p[2]=1;
        wr(p+4,f==2?512:f==4||f==5?256:1024);wr(p+8,rate_masks[f]);
    }
    /* Extension: exact GCR rates, selftest masks and parameter boundaries. */
    wr(out+128,266667);wr(out+132,285714);wr(out+136,307692);
    wr(out+140,0x1fff);wr(out+144,0x0604); /* intrusive IDs 3,10,11; SD info is read-only */
    wr(out+148,1);wr(out+152,499);wr(out+156,32);
    wr(out+160,QUALITY_CANDIDATES);wr(out+164,0xff); /* candidate count, native WRITE flags */
    wr(out+168,100);wr(out+172,100000); /* explicit raw sync cell tick bounds */
    wr(out+176,40);wr(out+180,24); /* history record, page header bytes */
    out[184]=1;out[185]=EVIDENCE_TRACE_MAX;out[186]=EVIDENCE_INDEX_MAX;out[187]=1;
    wr(out+188,EVIDENCE_MASK);
    return 192;
}
static uint16_t plan_status(uint8_t* out){
    if(plan.state==P_RUNNING && !ufi_analysis_job_active()){
        plan.error=job.id==plan.child?job.status:(uint8_t)-UFI_ERR_CANCELLED;
        plan.state=plan.error?P_FAILED:P_RESULT;
    }
    memset(out,0,32);out[0]=1;out[1]=plan.state;out[2]=plan.error;out[3]=29;
    wr(out+4,plan.id);wr(out+8,plan.config);wr(out+12,plan.media);wr(out+16,plan.child);
    out[20]=(uint8_t)plan.cursor;out[21]=(uint8_t)(plan.cursor>>8);
    out[22]=(uint8_t)plan.total;out[23]=(uint8_t)(plan.total>>8);
    out[24]=plan.first;out[25]=plan.last;out[26]=plan.sides;
    if(plan.sides && plan.cursor<plan.total){out[27]=(uint8_t)(plan.first+plan.cursor/plan.sides);out[28]=(uint8_t)(plan.cursor%plan.sides);}
    return 32;
}
/* Two independently closed files: a torn newest record leaves the older slot.
 * FAT metadata failure can still lose both: CRC is detection, not a power guarantee. */
static int checkpoint_read(uint8_t record[128]){
    uint8_t candidate[129];uint32_t newest=0;bool bad=false;
    for(unsigned slot=0;slot<2;slot++){
        uint16_t got=0;int ret=ufi_file_read(slot?"UFIPLAN1.CKP":"UFIPLAN0.CKP",0,candidate,sizeof(candidate),&got);
        if(ret==UFI_ERR_NOT_FOUND)continue;
        if(ret)return ret;
        if(got!=128 || memcmp(candidate,"UFP1",4) || candidate[4]!=1 ||
           !rd(candidate+8) || rd(candidate+124)!=ufi_crc32(0,candidate,124)){bad=true;continue;}
        if(rd(candidate+8)>newest){newest=rd(candidate+8);memcpy(record,candidate,128);}
    }
    return newest?UFI_OK:bad?UFI_ERR_STORAGE:UFI_ERR_NOT_FOUND;
}
static int checkpoint_save(void){
    uint8_t record[128]={0};
    if(plan.sequence==UINT32_MAX)return UFI_ERR_RANGE;
    memcpy(record,"UFP1",4);record[4]=1;wr(record+8,plan.sequence+1);wr(record+12,plan.id);
    record[16]=(uint8_t)plan.cursor;record[17]=(uint8_t)(plan.cursor>>8);
    record[20]=plan.first;record[21]=plan.last;record[22]=plan.sides;record[23]=plan.length;
    memcpy(record+24,plan.media_id,16);memcpy(record+40,plan.request,plan.length);
    memcpy(record+101,recovery_config,11);
    uint8_t profile[40];int profile_ret=ufi_drive_operating_profile(NULL,profile);if(profile_ret)return profile_ret;
    wr(record+112,ufi_crc32(0,profile,39));
    ufi_drive_media_status(profile);wr(record+116,ufi_crc32(0,profile+12,20));record[120]=profile[1];
    wr(record+124,ufi_crc32(0,record,124));
    uint16_t written=0;const char* name=((plan.sequence+1)&1)?"UFIPLAN1.CKP":"UFIPLAN0.CKP";
    int ret=ufi_file_write(name,0,true,record,sizeof(record),&written);
    if(ret || written!=128)return ret?ret:UFI_ERR_STORAGE;
    uint8_t check[129];uint16_t got=0;
    ret=ufi_file_read(name,0,check,sizeof(check),&got);
    if(ret || got!=128 || memcmp(record,check,128))return ret?ret:UFI_ERR_STORAGE;
    plan.sequence++;return UFI_OK;
}
int ufi_analysis_command(const uint8_t* p,uint16_t n,uint8_t* out,uint16_t* size) {
    *size=0;if(!n)return UFI_ERR_BAD_ARGS;
    if(p[0]==31){
        if(n>1 && (plan_live() || ufi_operation_busy() || ufi_job_live()))return UFI_ERR_BUSY;
        int ret=ufi_raw_verify_options(p,n,out);
        if(!ret){*size=8;if(n>1)ufi_config_changed();}
        return ret;
    }
    if(p[0]==32 && n==1){*size=ufi_raw_verify_report(out);return *size?UFI_OK:UFI_ERR_NOT_FOUND;}
    if(p[0]==30){
        if(n==1){memset(out,0,16);out[0]=1;out[1]=3;out[2]=128;out[3]=2;wr(out+4,plan.sequence);out[8]=plan.persistent;*size=16;return UFI_OK;}
        if(n!=28 || p[1]<1 || p[1]>2 || p[2]!=(p[1]==2?3:1) || p[3] ||
           !rd(p+4) || rd(p+4)!=ufi_config_generation())return UFI_ERR_BAD_ARGS;
        if(!ufi_drive_media_matches(rd(p+8)))return UFI_ERR_MEDIA_CHANGED;
        uint8_t any=0;for(unsigned i=12;i<28;i++)any|=p[i];if(!any)return UFI_ERR_BAD_ARGS;
        if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
        uint8_t record[128];int ret=checkpoint_read(record);
        if(p[1]==1){
            if(!plan_live() || (plan.state!=P_READY && plan.state!=P_RESULT && plan.state!=P_FAILED))return UFI_ERR_BUSY;
            if(plan.config!=rd(p+4) || plan.media!=rd(p+8))return UFI_ERR_BAD_ARGS;
            if(ret && ret!=UFI_ERR_NOT_FOUND)return ret;
            plan.sequence=ret?0:rd(record+8);memcpy(plan.media_id,p+12,16);
            ret=checkpoint_save();if(!ret)plan.persistent=true;
        }else{
            if(plan_live())return UFI_ERR_BUSY;
            if(ret)return ret;
            if(memcmp(record+24,p+12,16))return UFI_ERR_MEDIA_CHANGED;
            if(record[23]<28 || record[23]>61 || memcmp(record+101,recovery_config,11))return UFI_ERR_BAD_ARGS;
            uint8_t profile[40];ret=ufi_drive_operating_profile(NULL,profile);if(ret)return ret;
            if(rd(record+112)!=ufi_crc32(0,profile,39))return UFI_ERR_BAD_ARGS;
            ufi_drive_media_status(profile);
            if(rd(record+116)!=ufi_crc32(0,profile+12,20) || record[120]!=profile[1])return UFI_ERR_BAD_ARGS;
            uint16_t cursor=(uint16_t)(record[16]|(uint16_t)record[17]<<8);
            if(record[20]>record[21] || record[21]>83 || !record[22] || record[22]>2 ||
               cursor>(record[21]-record[20]+1u)*record[22])return UFI_ERR_BAD_ARGS;
            uint8_t create[77]={27};wr(create+1,rd(record+12));wr(create+5,rd(p+4));wr(create+9,rd(p+8));
            memcpy(create+13,record+20,3);memcpy(create+16,record+40,record[23]);
            ret=ufi_analysis_command(create,(uint16_t)(16+record[23]),out,size);
            if(!ret){plan.cursor=cursor;plan.state=cursor==plan.total?P_DONE:P_READY;
                plan.sequence=rd(record+8);plan.persistent=true;memcpy(plan.media_id,p+12,16);}
        }
        if(!ret)*size=plan_status(out);
        return ret;
    }
    if(p[0]==28 && n==1){*size=plan_status(out);return UFI_OK;}
    if(p[0]==27){
        if(n<44 || n>77 || p[16]!=1 || !rd(p+1) || !rd(p+5) ||
           rd(p+5)!=ufi_config_generation() || p[13]>p[14] || p[14]>83 ||
           !p[15] || p[15]>2)return UFI_ERR_BAD_ARGS;
        if(!ufi_drive_media_matches(rd(p+9)))return UFI_ERR_MEDIA_CHANGED;
        if(plan_live() || ufi_analysis_job_active() || ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
        /* Same validation as START, without touching drive or retained report. */
        const uint8_t* t=p+16;quality_options_t options;
        uint16_t flags=(uint16_t)(t[5]|(uint16_t)t[6]<<8);
        uint64_t expected=rd(t+12)|(uint64_t)rd(t+16)<<32;
        if(!quality_options(t+24,n-40,&options) || !(options.flags&1) ||
           (flags&~63u) || !t[7] || t[7]>8 || !rd(t+8) || rd(t+8)>60000 || t[23] || t[22]>3 ||
           ((flags&1)&&!(flags&4)) || ((flags&4)?!expected:expected!=0) ||
           (recovery_config[3]==1 && t[7]<recovery_config[4]) ||
           (recovery_config[3]==2 && !(flags&3)))return UFI_ERR_BAD_ARGS;
        memset(&plan,0,sizeof(plan));plan.id=rd(p+1);plan.config=rd(p+5);plan.media=rd(p+9);
        plan.first=p[13];plan.last=p[14];plan.sides=p[15];plan.total=(uint16_t)((p[14]-p[13]+1u)*p[15]);
        plan.length=(uint8_t)(n-16);memcpy(plan.request,t,plan.length);plan.state=P_READY;
        *size=plan_status(out);return UFI_OK;
    }
    if(p[0]==29){
        if(n!=8 || !plan.id || rd(p+1)!=plan.id || (p[5]|(uint16_t)p[6]<<8)!=plan.cursor || p[7]>2)return UFI_ERR_BAD_ARGS;
        (void)plan_status(out);
        if(p[7]==2){if(plan_live()){ufi_analysis_job_abort();plan.state=P_DONE;plan.error=(uint8_t)-UFI_ERR_CANCELLED;}*size=plan_status(out);return UFI_OK;}
        if(p[7]==1){
            if(plan.state!=P_RESULT)return UFI_ERR_BUSY;
            plan.cursor++;
            if(plan.persistent){int ret=checkpoint_save();if(ret){plan.cursor--;return ret;}}
            plan.child=0;plan.state=plan.cursor==plan.total?P_DONE:P_READY;
            *size=plan_status(out);return UFI_OK;
        }
        if(plan.state!=P_READY && plan.state!=P_FAILED)return UFI_ERR_BUSY;
        if(plan.config!=ufi_config_generation())return UFI_ERR_BAD_ARGS;
        if(!ufi_drive_media_matches(plan.media))return UFI_ERR_MEDIA_CHANGED;
        if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
        uint8_t track=(uint8_t)(plan.first+plan.cursor/plan.sides),side=(uint8_t)(plan.cursor%plan.sides);
        plan.child=(uint32_t)(((uint64_t)plan.id-1u+plan.cursor)%UINT32_MAX)+1u;
        wr(plan.request+1,plan.child);plan.request[24]=track;plan.request[25]=side;
        plan.request[20]=track;plan.request[21]=side;
        plan.starting=true;int ret=ufi_analysis_command(plan.request,plan.length,out,size);plan.starting=false;
        if(!ret && !ufi_drive_media_matches(plan.media)){ufi_analysis_job_abort();ret=UFI_ERR_MEDIA_CHANGED;}
        plan.error=(uint8_t)(ret<0?-ret:ret);plan.state=ret?P_FAILED:P_RUNNING;
        *size=plan_status(out);return ret;
    }
    if(plan_live() && !plan.starting && (p[0]==1 || p[0]==24 || p[0]==26 ||
       ((p[0]==7 || p[0]==10 || p[0]==11 || p[0]==14 || p[0]==15 || p[0]==18) && n>1) || p[0]==16))return UFI_ERR_BUSY;
    if(p[0]==0 && n==1){*size=capabilities(out);return UFI_OK;}
    if(p[0]==25 && n==1){
        /* Media-bound starts (4.5), schema 2: write start 0x27 (data/end 0x22), then
         * read start 0x28 (data/end 0x20); both with an 8-byte generation prefix. */
        memset(out,0,20);out[0]=2;out[1]=0x27;out[2]=0x22;out[3]=8;
        wr(out+4,3);wr(out+8,ufi_config_generation());wr(out+12,ufi_drive_media_token());
        out[16]=0x28;out[17]=0x20;out[18]=8;
        *size=20;return UFI_OK;
    }
    if(p[0]==43 && n==1){*size=ufi_provenance_block(out);return UFI_OK;}   /* provenance block (4.12) */
    if(p[0]==22 && n==1){
        uint8_t port[24];ufi_drive_port_report(port);
        ufi_capability_matrix(port,ufi_operation_busy() || ufi_job_live() || ufi_resources_owner()==UFI_OWNER_GW,out);
        *size=UFI_CAP_MATRIX_SIZE;return UFI_OK;
    }
    if(p[0]==23 && (n==1 || n==5)){
        uint32_t generation=ufi_config_generation();
        memset(out,0,24);out[0]=1;out[1]=24;out[2]=1; /* session-local */
        wr(out+4,generation);wr(out+8,n==5?rd(p+1):0);
        wr(out+12,n==5 && rd(p+1)!=generation?UFI_PROFILE_CHANGED:0);
        wr(out+16,ufi_drive_media_token());wr(out+20,UFI_PROFILE_CHANGED);
        *size=24;return UFI_OK;
    }
    if(p[0]==24 || p[0]==26){
        /* Atomic compare-and-start: no poll/yield between token check and start. */
        uint16_t prefix=p[0]==26?9:5;
        if(n<=prefix || p[prefix]!=1 || !rd(p+1) || rd(p+1)!=ufi_config_generation())return UFI_ERR_BAD_ARGS;
        if(p[0]==26 && !ufi_drive_media_matches(rd(p+5)))return UFI_ERR_MEDIA_CHANGED;
        int ret=ufi_analysis_command(p+prefix,(uint16_t)(n-prefix),out,size);
        if(p[0]==26 && !ret && !ufi_drive_media_matches(rd(p+5))){
            record(UFI_ERR_MEDIA_CHANGED);finish(UFI_ERR_MEDIA_CHANGED);
            *size=status(out);return UFI_ERR_MEDIA_CHANGED;
        }
        return ret;
    }
    if(p[0]==13 && n==1){ufi_drive_media_status(out);*size=40;return UFI_OK;}
    if(p[0]==17 && n==1){ufi_drive_port_report(out);*size=24;return UFI_OK;}
    if(p[0]==21 && n==1){
        memset(out,0,24);out[0]=1;out[1]=15;out[2]=16;out[3]=20;
        wr(out+4,64);wr(out+8,1023);wr(out+12,32767);wr(out+16,8);wr(out+20,128);*size=24;return UFI_OK;
    }
    if(p[0]==20 && n==1){
        memset(out,0,24);out[0]=1;out[1]=14;out[2]=QUALITY_LOCAL_WINDOWS;out[3]=40;
        wr(out+4,QUALITY_LOCAL_SIZE);wr(out+8,0x84);wr(out+12,255);
        wr(out+16,64);wr(out+20,30);*size=24;return UFI_OK;
    }
    if(p[0]==33 && (n==1 || n==14)){
        /* Apple II GCR decoder options (format 7): [33] reads, [33, schema 1, 12 bytes] sets */
        if(n==14){
            apple_gcr_options_t o;memcpy(&o,p+2,sizeof o);
            if(p[1]!=1 || !mfm_apple_options_valid(&o))return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            mfm_apple_set_options(&o);
        }
        const apple_gcr_options_t o=mfm_apple_get_options();out[0]=1;memcpy(out+1,&o,sizeof o);*size=13;return UFI_OK;
    }
    if(p[0]==34 && (n==1 || n==4)){
        /* decoder tolerance flags for formats 1-6: [34] reads, [34, schema 1, flags u16] sets */
        if(n==4){
            const uint16_t flags=(uint16_t)(p[2]|(uint16_t)p[3]<<8);
            if(p[1]!=1 || (flags&~MFM_TOL_MASK))return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            mfm_set_tolerance(flags);
        }
        const uint16_t t=mfm_get_tolerance();out[0]=1;out[1]=(uint8_t)t;out[2]=(uint8_t)(t>>8);*size=3;return UFI_OK;
    }
    if(p[0]==36 && (n==1 || n==66)){
        /* Apple II 6-and-2 symbol table: [36] reads, [36, schema 1, 64 symbols] sets (the standard table restores) */
        if(n==66){
            if(p[1]!=1 || !mfm_apple_table_valid(p+2))return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            mfm_apple_set_table(p+2);
        }
        out[0]=1;memcpy(out+1,mfm_apple_get_table(),64);*size=65;return UFI_OK;
    }
    if(p[0]==37 && (n==1 || n==2)){
        /* Session write lock (4.6): [37] reads, [37, 1] engages until MCU reset; there is no
         * disengage over USB.  state bit0 = session lock, bit1 = WRITE-LOCK jumper. */
        if(n==2){
            if(p[1]!=1)return UFI_ERR_BAD_ARGS;
            ufi_board_soft_lock();
        }
        out[0]=1;out[1]=(uint8_t)((ufi_board_soft_locked()?1u:0u)|(ufi_board_jumper_locked()?2u:0u));
        *size=2;return UFI_OK;
    }
    if(p[0]==42 && n==1){
        /* Weak windows of the last ANALYZE 0x25 (4.11): schema, revs, track, side/position, 64 counts */
        *size=ufi_analyze_weak_report(out);return UFI_OK;
    }
    if(p[0]==41 && (n==1 || n==4)){
        /* Speed check before writing (4.10): [41] reads, [41, 1, tolerance_permille u16] sets (0 = off) */
        if(n==4){
            const uint16_t t=(uint16_t)(p[2]|(uint16_t)p[3]<<8);
            if(p[1]!=1 || t>500)return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            ufi_write_set_speed_check(t);
        }
        uint16_t tol;uint32_t measured,source;ufi_write_speed_state(&tol,&measured,&source);
        out[0]=1;out[1]=(uint8_t)tol;out[2]=(uint8_t)(tol>>8);wr(out+3,measured);wr(out+7,source);
        *size=11;return UFI_OK;
    }
    if(p[0]==40 && n==1){
        /* Per-sector outcome of the last verified WRITE (4.9): 4 + 5 * count bytes */
        *size=ufi_quality_verify_report(out);return UFI_OK;
    }
    if(p[0]==39 && n==1){
        /* Last refusal (4.8): schema 1, cmd, seq, status, field, detail; cmd 0 = none since reset */
        ufi_v2_last_refusal(out);*size=6;return UFI_OK;
    }
    if(p[0]==38 && n==1){
        /* Flag masks the request parsers accept (4.7): one answer for every GUI checkbox */
        memset(out,0,16);out[0]=1;
        out[1]=UFI_V2_READ_FLAG_MASK;out[2]=UFI_V2_WRITE_FLAG_MASK;out[3]=UFI_V2_WRITE_FLAGS2_MASK;
        out[4]=UFI_V2_ANALYZE_FLAG_MASK;out[5]=UFI_V2_FORMAT_HINT_MAX;
        out[6]=(uint8_t)APPLE_GCR_FLAG_MASK;out[7]=(uint8_t)(APPLE_GCR_FLAG_MASK>>8);
        out[8]=(uint8_t)MFM_TOL_MASK;out[9]=(uint8_t)(MFM_TOL_MASK>>8);
        out[10]=(uint8_t)QUALITY_FEATURE_MASK;out[11]=(uint8_t)(QUALITY_FEATURE_MASK>>8);
        out[12]=(uint8_t)EVIDENCE_MASK;out[13]=(uint8_t)(EVIDENCE_MASK>>8);
        out[14]=UFI_V2_POSITION_MASK;
        *size=16;return UFI_OK;
    }
    if(p[0]==35 && n==1){
        /* Apple II nibble statistics of the last format 7 analysis: 178 bytes, revs 0 = none yet */
        memcpy(out,mfm_apple_stats(),sizeof(apple_stats_t));*size=sizeof(apple_stats_t);return UFI_OK;
    }
    if(p[0]==19 && n==1){
        static const uint32_t rates[]={500000,380228,346021,312500,280112,251256};
        memset(out,0,48);out[0]=1;out[1]=6;out[2]=18;out[3]=6;
        wr(out+4,525);wr(out+8,48);wr(out+12,13);
        for(unsigned i=0;i<6;i++)wr(out+16+4*i,rates[i]);
        out[40]=80;out[41]=16;out[42]=12;out[43]=8;out[44]=12;*size=48;return UFI_OK;
    }
    if(p[0]==18 && (n==1 || n==2 || n==41)){
        if(n>1 && (ufi_operation_busy() || ufi_job_live()))return UFI_ERR_BUSY;
        /* n == 2: named timing preset (1 factory, 2 conservative) - effective values only */
        int ret=n==2?ufi_drive_operating_preset(p[1],out):ufi_drive_operating_profile(n>1?p+1:NULL,out);
        if(!ret && n==41)ret=ufi_drive_profiles_save();   /* applied and kept in RAM even if the SD write fails */
        if(!ret)*size=40;
        return ret;
    }
    if((p[0]==14 && (n==1 || n==9)) || (p[0]==15 && (n==1 || n==13)) || (p[0]==16 && n==1)){
        if(n>1 || p[0]==16){
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            int ret=UFI_OK;
            if(p[0]==14)ret=ufi_drive_profile_set(p+1);
            else if(p[0]==15)ret=ufi_drive_media_set(p+1);
            else ufi_drive_media_notify();
            if(ret)return ret;
        }
        ufi_drive_media_status(out);*size=40;return UFI_OK;
    }
    if(p[0]==12 && n==1){
        memset(out,0,32);out[0]=1;out[1]=1;out[2]=7;out[3]=8;
        wr(out+4,1);wr(out+8,10000);wr(out+12,COMPARE_WIRE_SIZE);out[16]=COMPARE_WINDOWS;
        *size=32;return UFI_OK;
    }
    if(p[0]==11 && (n==1 || n==12)){
        if(n==12){
            if(p[1]!=1 || (p[2]&~1u) || p[3] || p[4]>2 || p[5]<2 || p[5]>8 ||
               !p[6] || p[6]>7 || p[7] || !rd(p+8) || rd(p+8)>10000)return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            memcpy(recovery_config,p+1,11);
        }
        memcpy(out,recovery_config,11);*size=11;return UFI_OK;
    }
    if(p[0]==7 && (n==1 || n==3)) {
        if(n==3){
            uint16_t f=(uint16_t)(p[1]|(uint16_t)p[2]<<8);
            if(f&~EVIDENCE_MASK)return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            ufi_evidence_config(f);
        }
        memset(out,0,16);out[0]=1;out[1]=(uint8_t)ufi_evidence_configured();wr(out+4,evidence_id());
        out[8]=EVIDENCE_INDEX_MAX;out[9]=EVIDENCE_TRACE_MAX;*size=16;return UFI_OK;
    }
    if(p[0]==10 && (n==1 || n==14)) {
        if(n==14){
            drive_extra_t t;memcpy(&t,p+2,sizeof(t));
            if(p[1]!=1 || !drive_extra_valid(&t))return UFI_ERR_BAD_ARGS;
            if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
            (void)ufi_drive_extra_set(&t);
        }
        drive_extra_t t=ufi_drive_extra_get();out[0]=1;memcpy(out+1,&t,sizeof(t));*size=13;return UFI_OK;
    }
    if(p[0]==8 && n==12){
        if(ufi_operation_busy())return UFI_ERR_BUSY;
        ufi_evidence_finish();
        if(!rd(p+1) || rd(p+1)!=evidence_id() || p[5]>3)return UFI_ERR_BAD_ARGS;
        uint32_t offset=rd(p+6);uint16_t count=(uint16_t)(p[10]|(uint16_t)p[11]<<8);
        if(!count || count>1024)return UFI_ERR_BAD_ARGS;
        uint16_t total=evidence_pack(p[5],out,4096);
        if(!total)return UFI_ERR_BUSY;
        if(offset>total)return UFI_ERR_RANGE;
        if(count>total-offset)count=(uint16_t)(total-offset);
        memmove(out+24,out+offset,count);memset(out,0,24);out[0]=1;out[1]=p[5];
        wr(out+4,rd(p+1));wr(out+8,rd(p+1));wr(out+12,offset);wr(out+16,total);wr(out+20,ufi_crc32(0,out+24,count));
        *size=(uint16_t)(24+count);return UFI_OK;
    }
    if(p[0]==5) {
        if(n!=5 || p[1]!=1 || (p[4]&~1u))return UFI_ERR_BAD_ARGS;
        uint16_t mask=(uint16_t)(p[2]|(uint16_t)p[3]<<8);
        if(!mask || (mask&~0x1fffu) || ((mask&0x0604) && !(p[4]&1)))return UFI_ERR_BAD_ARGS;
        if(ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
        out[0]=1;out[1]=ufi_selftest_selected(out+2,13,mask);*size=(uint16_t)(2+out[1]*6);return UFI_OK;
    }
    if(p[0]==6 && n==1) {
        if(ufi_operation_busy())return UFI_ERR_BUSY;
        *size=ufi_write_coverage(out);return UFI_OK;
    }
    if(p[0]==1) {
        quality_options_t options;
        if(n<28 || !quality_options(p+24,n-24,&options) || !(options.flags&1))return UFI_ERR_BAD_ARGS;
        uint16_t flags=(uint16_t)(p[5]|(uint16_t)p[6]<<8);uint64_t expected=rd(p+12)|(uint64_t)rd(p+16)<<32;
        if(!rd(p+1) || (flags&~63u) || !p[7] || p[7]>8 || !rd(p+8) || rd(p+8)>60000 || p[23] || p[22]>3 ||
           ((flags&1)&&!(flags&4)) || ((flags&4)?!expected:expected!=0))return UFI_ERR_BAD_ARGS;
        if(ufi_analysis_job_active() || ufi_operation_busy() || ufi_job_live())return UFI_ERR_BUSY;
        if((recovery_config[3]==1 && p[7]<recovery_config[4]) ||
           (recovery_config[3]==2 && !(flags&3)))return UFI_ERR_BAD_ARGS;
        memset(&job,0,sizeof(job));job.id=rd(p+1);job.flags=flags;job.expected=expected;
        ufi_drive_media_status(job.media_snapshot);job.media_token=rd(job.media_snapshot+4);
        memcpy(job.recovery,recovery_config,11);
        options.report_mask=recovery_config[1];options.compare_tolerance=rd(recovery_config+7);
        job.c=p[20];job.h=p[21];job.n=p[22];job.max_attempts=p[7];job.budget=rd(p+8);
        if(options.position && !ufi_drive_is_apple())return UFI_ERR_BAD_ARGS;    /* quarter tracks: Apple only */
        job.track=p[24];job.side=options.side;job.position=options.position;job.revs=p[26]?p[26]:3;if(job.revs>8)job.revs=8;
        job.options=options;job.started=HAL_GetTick();ufi_cancel_clear();
        int ret=capture();*size=status(out);return ret;
    }
    if(n<5 || rd(p+1)!=job.id || !job.id)return UFI_ERR_BAD_ARGS;
    if(p[0]==2 && n==5){*size=status(out);return UFI_OK;}
    if(p[0]==4 && n==5){ufi_analysis_job_abort();*size=status(out);return UFI_OK;}
    if(p[0]==3 && n==16) {
        if(ufi_analysis_job_active())return UFI_ERR_BUSY;
        if(rd(p+5)!=job.generation)return UFI_ERR_BAD_ARGS;
        uint8_t section=p[9];uint32_t offset=rd(p+10);uint16_t count=(uint16_t)(p[14]|(uint16_t)p[15]<<8),total=0;
        if(!count || count>1024)return UFI_ERR_BAD_ARGS;
        if(section==0){const quality_t* q=ufi_quality_result();if(!q || q->generation!=job.generation)return UFI_ERR_BUSY;total=ufi_quality_report(out,4096);}
        else if(section==1){total=(uint16_t)(job.attempt*40);memcpy(out,job.history,total);}
        else if(section==2){total=status(out);}
        else if(section==3) {
            const quality_t* q=ufi_quality_result();if(!q || q->generation!=job.generation)return UFI_ERR_BUSY;
            total=2*QUALITY_CANDIDATES;for(unsigned i=0;i<QUALITY_CANDIDATES;i++){out[i*2]=(uint8_t)q->candidates[i];out[i*2+1]=(uint8_t)(q->candidates[i]>>8);}
        }
        else if(section>=4 && section<=6){
            if(!job.capture_id || evidence_id()!=job.capture_id)return UFI_ERR_BUSY;
            total=evidence_pack((uint8_t)(section-4),out,4096);if(!total)return UFI_ERR_BUSY;
        }
        else if(section==7){
            const quality_t* q=ufi_quality_result();if(!q || q->generation!=job.generation)return UFI_ERR_BUSY;
            total=ufi_compare_report(out,4096,job.generation);if(!total)return UFI_ERR_BUSY;
        }
        else if(section==8){memcpy(out,job.recovery,11);total=11;}
        else if(section==9){memcpy(out,job.media_snapshot,40);total=40;}
        else if(section==11){
            if(!job.profile_checked)return UFI_ERR_BUSY;
            memcpy(out,job.profile_check,MEDIA_CHECK_SIZE);total=MEDIA_CHECK_SIZE;
        }
        else if(section==12){memcpy(out,job.port_snapshot,24);total=24;}
        else if(section==15){
            const uint8_t* a=job.assessment[job.attempt-1];
            memset(out,0,64);out[0]=1;out[1]=job.history[job.attempt-1][0];out[2]=job.recovery[3];out[3]=job.confirmed;
            memcpy(out+4,a,16);wr64(out+20,job.expected);wr64(out+28,job.seen);
            memcpy(out+36,job.recognition_profile,12);out[48]=(job.flags&4)?job.c:job.track;
            out[49]=(job.flags&4)?job.h:job.side;out[50]=job.n;
            out[51]=(job.flags&4)!=0;wr(out+52,job.generation);wr(out+56,job.capture_id);
            out[60]=job.attempt;out[61]=a[16];out[62]=a[17];out[63]=job.status;total=64;
        }
        else if(section==16){total=(uint16_t)(job.attempt*20);memcpy(out,job.assessment,total);}
        else if(section==14){
            total=ufi_local_report(out,4096,job.generation);if(!total)return UFI_ERR_BUSY;
        }
        else if(section==13){
            const quality_t* q=ufi_quality_result();if(!q || q->generation!=job.generation)return UFI_ERR_BUSY;
            total=quality_observation_pack(q,out,4096);
        }
        else if(section==10){
            if(!job.capture_id || job.capture_id!=evidence_id())return UFI_ERR_BUSY;
            total=evidence_pack(3,out,4096);
        }
        else return UFI_ERR_BAD_ARGS;
        if(offset>total)return UFI_ERR_RANGE;
        if(count>total-offset)count=(uint16_t)(total-offset);
        memmove(out+24,out+offset,count);memset(out,0,24);out[0]=1;out[1]=section;
        wr(out+4,job.id);wr(out+8,job.generation);wr(out+12,offset);wr(out+16,total);wr(out+20,ufi_crc32(0,out+24,count));
        *size=(uint16_t)(24+count);return UFI_OK;
    }
    return UFI_ERR_BAD_ARGS;
}
