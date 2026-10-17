"""Exercise native request rejection and compact preparation before side effects."""
from test_regressions import BASE, ERRORS, function, run


def main():
    code = BASE + ERRORS + r'''
#include "ufi_capabilities.h"
#define REVOLUTIONS_STREAM 8u
#define FLUX_TIMER_FREQ 275000000u
enum {JOB_OPEN};
static struct {unsigned kind,state;} ufi_job;
static bool op_read;
static uint8_t read_seq,hard_sectors=9;
static unsigned starts,line_calls;
static int start_error;
static bool ufi_job_live(void){return false;}
static bool ufi_drive_is_apple(void){return false;}
static uint32_t get16(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8;}
static void ufi_flux_hard_sectors(uint8_t n){hard_sectors=n;line_calls++;}
static void ufi_flux_evidence_context(uint8_t p[6]){memset(p,0,6);p[4]=hard_sectors;}
static void ufi_stream_begin_v2(uint8_t seq){(void)seq;}
static int ufi_capture_start_q(uint8_t c,uint8_t h,uint8_t rev,uint32_t period,uint8_t quarter){
    (void)c;(void)h;(void)rev;(void)period;(void)quarter;starts++;return start_error;
}
/* op 39 refusal note (4.8): the parser records field and detail */
enum {REFUSE_LENGTH=1,REFUSE_FLAGS,REFUSE_TRACK,REFUSE_SIDE,REFUSE_REVS,REFUSE_SECTORS,REFUSE_SCALE,
      REFUSE_INDEX_TICKS,REFUSE_EXPECTED,REFUSE_FLAGS2,REFUSE_CONFIG_GEN,REFUSE_MEDIA_GEN,REFUSE_ANALYSIS_OPTIONS};
static uint8_t note_f,note_d;
static void refuse(uint8_t f,uint8_t d){note_f=f;note_d=d;}
/* GUARDED_READ doubles: configuration generation, media token, capture abort */
static uint32_t get32(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
static uint32_t config_gen=5,media_gen=9,media_after_start;
static unsigned aborts;
static uint32_t ufi_config_generation(void){return config_gen;}
static bool ufi_drive_media_matches(uint32_t t){return t==media_gen;}
static int ufi_capture_abort(void){aborts++;return 0;}
enum {CAPTURE_IDLE,CAPTURE_RUNNING};
static struct {int state;} g_capture;
'''
    code += function('ufi_v2.c', 'cmd_read')
    # the guarded start must see the medium "change" right after the capture start
    code += function('ufi_v2.c', 'cmd_guarded_read').replace(
        "const int ret = cmd_read(seq, p + 8, (uint16_t)(len - 8u));",
        "const int ret = cmd_read(seq, p + 8, (uint16_t)(len - 8u)); if(media_after_start)media_gen=media_after_start;")
    code += r'''
static void guarded_checks(void){
    uint8_t q[19]={0};q[8]=0;q[9]=0;q[10]=2;         /* READ payload: track 0, side 0, 2 revs */
    unsigned before=starts;op_read=false;
    assert(cmd_guarded_read(3,q,11)==UFI_ERR_BAD_ARGS && starts==before && note_f==REFUSE_CONFIG_GEN);   /* config 0 */
    q[0]=4;q[4]=9;assert(cmd_guarded_read(3,q,11)==UFI_ERR_BAD_ARGS && starts==before && note_f==REFUSE_CONFIG_GEN);   /* stale config */
    q[0]=5;q[4]=8;assert(cmd_guarded_read(3,q,11)==UFI_ERR_MEDIA_CHANGED && starts==before && note_f==REFUSE_MEDIA_GEN); /* other medium */
    q[4]=0;assert(cmd_guarded_read(3,q,11)==UFI_ERR_MEDIA_CHANGED && starts==before);       /* media 0 */
    q[4]=9;note_f=0;assert(cmd_guarded_read(3,q,10)==UFI_ERR_BAD_ARGS && starts==before && note_f==REFUSE_LENGTH && note_d==11); /* prefix only */
    assert(!cmd_guarded_read(3,q,11) && starts==before+1 && op_read && read_seq==3 && !aborts);
    op_read=false;q[11]=9;assert(cmd_guarded_read(4,q,12)==UFI_ERR_BAD_ARGS && starts==before+1 && note_f==REFUSE_FLAGS && note_d==8); /* READ flags still checked */
    q[11]=0;start_error=UFI_ERR_SEEK_FAIL;assert(cmd_guarded_read(4,q,11)==UFI_ERR_SEEK_FAIL && !op_read);start_error=0;
    media_after_start=10;g_capture.state=CAPTURE_RUNNING;      /* change seen right after the start */
    assert(cmd_guarded_read(5,q,11)==UFI_ERR_MEDIA_CHANGED && aborts==1 && !op_read && g_capture.state==CAPTURE_IDLE);
    media_after_start=0;media_gen=9;starts=0;op_read=false;   /* the plain READ checks count from zero */
}
int main(void){
    guarded_checks();
    uint8_t p[12]={0,0,2,0,0,0,5};
    assert(!cmd_read(7,p,3) && starts==1 && op_read && read_seq==7);
    unsigned before=starts,calls=line_calls;
    p[3]=8;assert(cmd_read(8,p,6)==UFI_ERR_BAD_ARGS && starts==before && line_calls==calls && note_f==REFUSE_FLAGS && note_d==8);
    p[3]=1;p[4]=0xd1;p[5]=7;assert(cmd_read(8,p,6)==UFI_ERR_BAD_ARGS && starts==before && note_f==REFUSE_REVS && note_d==1);
    p[4]=p[5]=0;assert(!cmd_read(8,p,4));
    assert(cmd_read(8,p,5)==UFI_ERR_BAD_ARGS && note_f==REFUSE_LENGTH && note_d==3);        /* length 5 is not a READ shape */
    p[3]=2;assert(!cmd_read(8,p,7) && hard_sectors==5);
    assert(cmd_read(8,p,4)==UFI_ERR_BAD_ARGS && note_f==REFUSE_LENGTH && note_d==7);        /* HARD_SECTORS without the sectors byte */
    p[6]=1;calls=line_calls;assert(cmd_read(8,p,7)==UFI_ERR_BAD_ARGS && line_calls==calls && note_f==REFUSE_SECTORS && note_d==1);
    p[3]=4;p[0]=2;assert(cmd_read(8,p,4)==UFI_ERR_BAD_ARGS && note_f==REFUSE_TRACK && note_d==1);p[0]=0;p[3]=2;
    p[6]=7;start_error=UFI_ERR_SEEK_FAIL;
    assert(cmd_read(8,p,7)==UFI_ERR_SEEK_FAIL && hard_sectors==5);start_error=0;
    for(unsigned flags=0;flags<256;flags++)for(unsigned len=0;len<=sizeof(p);len++){
        p[3]=(uint8_t)flags;calls=line_calls;before=starts;
        int ret=cmd_read(9,p,(uint16_t)len);
        if(ret)assert(calls==line_calls && starts==before);
    }
    puts("native READ: exhaustive flags/lengths, no malformed side effects, failure rollback passed");
}
'''
    run('regression_native_read_requests', code)
    code = BASE + ERRORS + r'''
#include "ufi_write_codec.h"
static uint32_t buffer[8],*write_buffer=buffer;
static struct {bool compact;uint8_t quarter;uint32_t bytes_expected;compact_dec_t dec;} g_write;
static unsigned preparations;
static int ufi_write_prepare(uint8_t c,uint8_t h,uint32_t count,bool verify){
    (void)c;(void)h;(void)count;(void)verify;preparations++;return 0;
}
'''
    code += function('ufi_write.c', 'ufi_write_prepare_compact') + r'''
int main(void){
    assert(ufi_write_prepare_compact(0,0,3,2,true)==UFI_ERR_BUFFER_FULL && !preparations);
    assert(ufi_write_prepare_compact(0,0,3,16,true)==UFI_ERR_BUFFER_FULL && !preparations);
    assert(!ufi_write_prepare_compact(0,0,3,15,true) && preparations==1 && g_write.compact);
    assert(!ufi_write_prepare_compact(0,0,3,3,false) && preparations==2);
    puts("compact WRITE: size rejection precedes preparation/evidence invalidation passed");
}
'''
    run('regression_compact_preparation', code, ('src/ufi_write_codec.c',))

    # Session write lock (op 37, 4.6): the board keeps it until reset, the jumper stays
    # separate, and the v2 dispatcher refuses exactly the disk/drive-changing requests.
    code = BASE + ERRORS + r'''
#define GPIO_PIN_SET 1
#define GPIO_PIN_RESET 0
static const struct {int port,pin;} PIN_WLOCK;
static int jumper;
static int HAL_GPIO_ReadPin(int port,int pin){(void)port;(void)pin;return jumper;}
static bool session_lock;
'''
    for name in ('ufi_board_soft_lock','ufi_board_soft_locked','ufi_board_jumper_locked','ufi_board_write_locked'):
        code += function('ufi_board.c', name)
    code += r'''
enum {UFI_V2_WRITE=0x22,V2_GUARDED_WRITE=0x27,V2_ERASE=0x23,V2_PATTERN=0x24,V2_FORMAT=0x26,
      V2_COPY_START=0x63,V2_WRITE_START=0x64,V2_IEC_BLOCK=0x35,V2_IEC_MEM=0x33,V2_IEC_TARGET=0x36};
'''
    code += function('ufi_v2.c', 'locked_write') + r'''
int main(void){
    assert(!ufi_board_soft_locked() && !ufi_board_write_locked() && !ufi_board_jumper_locked());
    jumper=1;assert(ufi_board_jumper_locked() && ufi_board_write_locked() && !ufi_board_soft_locked());
    jumper=0;ufi_board_soft_lock();ufi_board_soft_lock();
    assert(ufi_board_soft_locked() && ufi_board_write_locked() && !ufi_board_jumper_locked());
    uint8_t p[8]={0};
    for(unsigned cmd=0;cmd<256;cmd++)for(unsigned op=0;op<6;op++){
        p[0]=p[2]=(uint8_t)op;
        const bool always=cmd==0x22||cmd==0x27||cmd==0x23||cmd==0x24||cmd==0x26||cmd==0x63||cmd==0x64;
        const bool iec=(cmd==0x35 && op==1)||((cmd==0x33||cmd==0x36) && (op==0||op==2||op==3));
        assert(locked_write((uint8_t)cmd,p,8)==(always||iec));
        assert(locked_write((uint8_t)cmd,p,0)==always);       /* empty payload: only the plain commands */
    }
    p[2]=0;assert(!locked_write(0x36,p,2) && locked_write(0x36,p,3));  /* TARGET needs the MEM op byte */
    puts("session write lock: board state, jumper separation, v2 refusal set passed");
}
'''
    run('regression_session_write_lock', code)

    # END generation block (4.5): schema 1, configuration generation, media generation the
    # operation was bound to, media generation now, capture id; 17 bytes little endian.
    code = BASE + ERRORS + r'''
#define UFI_V2_GEN_BLOCK 17u
static uint32_t config_gen=0x01020304u,media_now=0x0a0b0c0du;
static uint32_t ufi_config_generation(void){return config_gen;}
static uint32_t ufi_drive_media_token(void){return media_now;}
'''
    # put16 is a one-line helper: the extractor runs on to the next line-start brace and
    # brings put32/get16/get32/st along, so only put16 is named here.
    for name in ('put16','ufi_v2_generations'):
        code += function('ufi_v2.c', name)
    code += r'''
int main(void){
    uint8_t out[24];memset(out,0xee,sizeof out);
    assert(st(-22)==22 && get32(out)==0xeeeeeeeeu);         /* helpers dragged in by put16 */
    assert(ufi_v2_generations(out,0x11223344u,0x55667788u)==17);
    static const uint8_t want[17]={1,4,3,2,1,0x44,0x33,0x22,0x11,0x0d,0x0c,0x0b,0x0a,0x88,0x77,0x66,0x55};
    assert(!memcmp(out,want,17) && out[17]==0xee);
    media_now=0x11223344u;ufi_v2_generations(out,0x11223344u,0);
    assert(!memcmp(out+5,out+9,4) && !out[13] && !out[14] && !out[15] && !out[16]);
    puts("END generation block: layout, bound vs current medium, capture id passed");
}
'''
    run('regression_generation_block', code)

    # Weak windows (op 42): max - min transition count per 1/64 revolution over the
    # captured revolutions; the ANALYZE mask keeps threshold 3, the counts are reported raw.
    code = BASE + ERRORS + r'''
#define WEAK_WINDOWS 64u
typedef struct { uint32_t timestamp; } flux_sample_t;
typedef struct { flux_sample_t* samples; uint32_t count; uint32_t index_time; uint8_t revolution; } flux_revolution_t;
typedef struct { uint8_t revs, encoding; uint16_t bitcell_ns, rpm_x100, peak_ns[4]; uint32_t index_ticks, transitions, bitcells;
                 uint8_t sectors, weak_count; uint32_t first_sector_us; uint64_t weak_mask; } analyze_result_t;
static flux_sample_t samples[3][4096];
static flux_revolution_t revs[3];
static unsigned progress_calls;
static int progress_error;
static flux_revolution_t* ufi_capture_get_data(uint8_t k){return &revs[k];}
static int analysis_progress(uint32_t t0){(void)t0;progress_calls++;return progress_error;}
static uint8_t weak_diff[WEAK_WINDOWS];
static uint8_t weak_revs, weak_track, weak_side;
/* window w of revolution k gets base(w) + extra(k, w) transitions, evenly spaced */
static void build(uint8_t k, unsigned (*extra)(uint8_t, unsigned)){
    unsigned n=0;
    for(unsigned w=0;w<WEAK_WINDOWS;w++){
        const unsigned cnt=10u+(w%3u)+extra(k,w);
        for(unsigned i=0;i<cnt;i++)samples[k][n++].timestamp=(uint32_t)(w*1000u+i*1000u/cnt);
    }
    revs[k].samples=samples[k];revs[k].count=n;revs[k].index_time=64000u;revs[k].revolution=k;
}
static unsigned none(uint8_t k,unsigned w){(void)k;(void)w;return 0;}
static unsigned some(uint8_t k,unsigned w){
    if(w==5)return k;          /* 0,1,2 -> diff 2: below the mask threshold */
    if(w==17)return 3u*k;      /* 0,3,6 -> diff 6: weak */
    if(w==63)return k==2?1:0;  /* diff 1 */
    return 0;
}
'''
    code += function('ufi_analyze.c', 'weak_windows') + function('ufi_analyze.c', 'ufi_analyze_weak_report') + r'''
int main(void){
    analyze_result_t r;uint8_t out[70];
    for(uint8_t k=0;k<3;k++)build(k,none);
    memset(&r,0,sizeof r);assert(weak_windows(3,0,&r)==0 && !r.weak_mask && !r.weak_count && weak_revs==3 && progress_calls);
    for(unsigned w=0;w<WEAK_WINDOWS;w++)assert(!weak_diff[w]);
    for(uint8_t k=0;k<3;k++)build(k,some);
    memset(&r,0,sizeof r);assert(weak_windows(3,0,&r)==0);
    assert(weak_diff[5]==2 && weak_diff[17]==6 && weak_diff[63]==1 && !weak_diff[4] && !weak_diff[18]);
    assert(r.weak_mask==(1ull<<17) && r.weak_count==1);              /* only >= 3 reaches the mask */
    weak_track=40;weak_side=0x91;
    assert(ufi_analyze_weak_report(out)==68 && out[0]==1 && out[1]==3 && out[2]==40 && out[3]==0x91 && out[4+17]==6 && out[4+5]==2);
    memset(&r,0,sizeof r);assert(weak_windows(1,0,&r)==0 && !weak_revs && !weak_diff[17]);   /* one revolution: nothing to compare */
    for(uint8_t k=0;k<3;k++)build(k,some);
    progress_error=UFI_ERR_CANCELLED;assert(weak_windows(3,0,&r)==UFI_ERR_CANCELLED);progress_error=0;
    puts("weak windows: raw differences, fixed mask threshold, header, single revolution and cancel passed");
}
'''
    run('regression_weak_windows', code)


if __name__ == '__main__':
    main()
