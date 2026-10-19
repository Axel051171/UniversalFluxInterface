"""Production SD loading and source-preserving write helper with storage/drive doubles."""
from test_regressions import BASE, ERRORS, function, run

def main():
    code=BASE+ERRORS+r'''
#include "ufi_image.h"
#include "ufi_hfe_v3.h"
#include "ufi_mfm.h"
#define SCP_HDR_LEN 16
#define SCP_TABLE_LEN 672
#define FR_OK 0
#define UFI_WATCHDOG_FEED() ((void)0)
typedef unsigned UINT;
typedef struct {unsigned pos,size;uint8_t data[2048];} FIL;
static FIL file;
static bool cancelled,io_error;
static uint8_t stage[32768],cyl,head;
static uint32_t table[168],job_source_ticks,job_permille,job_rev,job_cells;
static uint32_t store[64],capacity=64;
static image_geo_t geo;
static hfe_header_t hfeh;
static scp_header_t scph;
typedef struct {uint32_t block,length,cached;uint8_t side;} hfe_sd_reader_t;
static bool ufi_cancel_requested(void){return cancelled;}
static uint32_t* ufi_flux_store(uint32_t* words){*words=capacity;return store;}
static unsigned f_size(FIL* f){return f->size;}
static int f_lseek(FIL* f,unsigned offset){if(io_error || offset>f->size)return 1;f->pos=offset;return 0;}
static int f_read(FIL* f,void* out,unsigned n,unsigned* got){
    if(io_error)return 1;
    *got=n<f->size-f->pos?n:f->size-f->pos;memcpy(out,f->data+f->pos,*got);f->pos+=*got;return 0;
}
'''
    for name in ('hfe_sd_byte','load_hfe_v3','job_load_track'):
        code+=function('ufi_dump.c',name)
    code+=r'''
static uint8_t rev(uint8_t x){uint8_t y=0;for(unsigned i=0;i<8;i++){y=(uint8_t)((y<<1)|(x&1));x>>=1;}return y;}
int main(void){
    file.size=2048;geo.kind=IMG_HFE;geo.rate_bps=250000;hfeh.tracks=1;hfeh.v3=true;
    /* Command/argument crosses the 256-byte side block; opposite side is poison. */
    memset(file.data,0xff,sizeof(file.data));memset(file.data+1024,rev(0xf0),256);
    file.data[1279]=rev(0xf2);uint8_t tail[]={72,0x80,0x40,0x20};
    for(unsigned i=0;i<4;i++)file.data[1536+i]=rev(tail[i]);
    table[0]=(2u<<16)|520;uint32_t n;int err;
    assert(job_load_track(&n,&err) && !err && n==3 && job_source_ticks==13200);
    assert(store[0]==550 && store[1]==4950 && store[2]==4950);
    file.data[1537]=rev(0xf4);assert(!job_load_track(&n,&err) && err==UFI_ERR_NOT_IMPL);
    file.data[1537]=rev(0x80);cancelled=true;assert(!job_load_track(&n,&err) && err==UFI_ERR_STORAGE);cancelled=false;
    io_error=true;assert(!job_load_track(&n,&err) && err==UFI_ERR_STORAGE);io_error=false;
    file.size=1538;assert(!job_load_track(&n,&err) && err==UFI_ERR_STORAGE);file.size=2048;
    hfeh.v3=false;table[0]=(2u<<16)|2;file.data[1024]=0xff;capacity=19;
    assert(!job_load_track(&n,&err) && err==UFI_ERR_BUFFER_FULL); /* no truncated v1 success */
    capacity=64;assert(job_load_track(&n,&err) && n==8);
    puts("SD production loader: v3 timing, block crossing, weak rejection, IO/cancel and v1 bounds passed");
}
'''
    run('regression_sd_loader',code,('src/ufi_image.c','src/ufi_hfe_v3.c','src/ufi_mfm.c'))
    code=BASE+ERRORS+r'''
#include "ufi_verify.h"
static uint32_t write_buffer[100000],readback[100000],source_index_ticks,reference_started;
static uint8_t payload[9*512];
static uint32_t read_count;
static verify_ref_t verify_ref;
static mfm_decoder_t verify_dec;
static bool local_reference_ready,apple,cancelled;
static unsigned writes,aborts;
static struct {bool wave_mode;uint8_t verify_level;} g_write;
static uint32_t HAL_GetTick(void){return 0;}
static bool ufi_drive_is_apple(void){return apple;}
static bool ufi_cancel_requested(void){return cancelled;}
static bool reference_poll(void){return !cancelled;}
static void ufi_write_abort(void){aborts++;}
static int ufi_write_prepare(uint8_t t,uint8_t h,uint32_t n,bool verify){
    (void)t;(void)h;(void)n;assert(verify);local_reference_ready=false;g_write.verify_level=0;return 0;
}
static int write_local_run(void){
    assert(local_reference_ready && !source_index_ticks && !reference_started);writes++;
    /* Capture reuses the input store. Only the retained original reference survives. */
    memset(write_buffer,0,sizeof(write_buffer));
    verify_match_t match;verify_match_begin(&match,&verify_ref,&verify_dec);
    for(unsigned i=0;i<read_count;i++)verify_match_interval(&match,readback[i]);
    g_write.verify_level=verify_match_result(&match,true);
    return g_write.verify_level?0:UFI_ERR_DMA;
}
'''+function('ufi_write.c','ufi_write_local_checked')+r'''
int main(void){
    mfm_track_fmt_t fmt={.rate_bps=250000,.track_cells=100000,.spt=9,.gap3=0x50};uint8_t level;
    uint32_t n=mfm_encode_track(&fmt,0,0,payload,write_buffer,100000);assert(n);
    read_count=mfm_encode_track(&fmt,0,0,payload,readback,100000);
    assert(!ufi_write_local_checked(0,0,n,false,0,0x3fe,0,&level) && level==VERIFY_CONTENT && writes==1);
    n=mfm_encode_track(&fmt,0,0,payload,write_buffer,100000);payload[511]^=1;
    read_count=mfm_encode_track(&fmt,0,0,payload,readback,100000);
    assert(ufi_write_local_checked(0,0,n,false,0,0x3fe,0,&level)==UFI_ERR_DMA && !level && writes==2);
    n=mfm_encode_track(&fmt,0,0,payload,write_buffer,100000);
    assert(ufi_write_local_checked(0,0,n,false,0,0x7fe,0,&level)==UFI_ERR_BAD_ARGS && writes==2 && aborts);
    assert(ufi_write_local_checked(0,0,n,false,0,0x3fe,1,&level)==UFI_ERR_BAD_ARGS && writes==2);
    cancelled=true;assert(ufi_write_local_checked(0,0,n,false,0,0,0,&level)==UFI_ERR_CANCELLED && writes==2);
    puts("SD source verify: same geometry/wrong data rejected; missing IDs and cancellation prevent write start");
}
'''
    run('regression_sd_verify',code,('src/ufi_mfm.c','src/ufi_verify.c'))
    code=BASE+ERRORS+r'''
#include "ufi_verify.h"
enum {WRITE_IDLE,WRITE_COMPLETE,WRITE_VERIFYING,WRITE_ERROR,CAPTURE_COMPLETE=8,CAPTURE_ERROR=9};
static struct {unsigned state;bool wave_mode,raw_check;uint32_t flux_count;uint8_t track,side,quarter,verify_level;} g_write;
static struct {unsigned state,error_code;} g_capture;
typedef struct {uint32_t count,index_time;const uint32_t* samples;} flux_revolution_t;
static flux_revolution_t revolution={.count=1000};
#include "ufi_raw_verify.h"
static uint8_t raw_flags,raw_report[RAW_REPORT_SIZE];
static uint16_t raw_tolerance=30;
static bool raw_available;
static raw_profile_t raw_source;
static verify_ref_t verify_ref;
static mfm_decoder_t verify_dec;
static uint16_t write_arr[1000];
static uint32_t write_media_token=42,media=42;
static bool local_reference_ready=true,cancelled,change_on_capture;
static unsigned captures;
#define UFI_WATCHDOG_FEED() ((void)0)
static bool ufi_drive_media_matches(uint32_t token){return token==media;}
static int ufi_capture_start_q(uint8_t t,uint8_t s,uint8_t n,uint32_t p,uint8_t q){
    (void)t;(void)s;(void)n;(void)p;(void)q;captures++;g_capture.state=CAPTURE_COMPLETE;
    if(change_on_capture)media++;
    return 0;
}
static unsigned ufi_capture_get_state(void){return g_capture.state;}
static uint32_t HAL_GetTick(void){return 0;}
static bool ufi_cancel_requested(void){return cancelled;}
static void ufi_capture_abort(void){}
static void ufi_flux_capture_stop(void){}
static uint8_t ufi_flux_get_revolution_count(void){return 2;}
static const flux_revolution_t* ufi_flux_get_revolution(uint8_t n){(void)n;return &revolution;}
static int ufi_quality_verify(const verify_ref_t* ref,bool count_ok,uint8_t* level){
    assert(ref==&verify_ref && count_ok);*level=VERIFY_CONTENT;return 0;
}
'''+function('ufi_write.c','ufi_raw_verify_options')+function('ufi_write.c','ufi_raw_verify_report')+function('ufi_write.c','ufi_write_verify')+r'''
int main(void){
    uint8_t options[5]={31,1,0,30,0},option_reply[8];
    for(unsigned flags=0;flags<256;flags++){
        options[2]=(uint8_t)flags;
        int result=ufi_raw_verify_options(options,5,option_reply);
        assert((result==0)==(flags==0 || flags==1 || flags==3));
    }
    options[2]=0;assert(!ufi_raw_verify_options(options,5,option_reply));
    g_write.state=WRITE_COMPLETE;g_write.flux_count=1000;media++;
    assert(ufi_write_verify()==UFI_ERR_MEDIA_CHANGED && !captures && g_write.state==WRITE_ERROR);
    media--;g_write.state=WRITE_COMPLETE;
    assert(!ufi_write_verify() && g_write.verify_level==VERIFY_READBACK_PLAUSIBLE && captures==1);
    verify_ref.n=1;g_write.state=WRITE_COMPLETE;
    assert(!ufi_write_verify() && g_write.verify_level==VERIFY_CONTENT && captures==2);
    assert(ufi_write_verify()==UFI_ERR_BUSY && captures==2);
    assert(!raw_available);
    change_on_capture=true;g_write.state=WRITE_COMPLETE;
    assert(ufi_write_verify()==UFI_ERR_MEDIA_CHANGED && captures==3 && g_write.state==WRITE_ERROR);
    change_on_capture=false;media--;verify_ref.n=0;options[2]=3;
    assert(!ufi_raw_verify_options(options,5,option_reply));
    uint32_t reference[4096],observed[4096],time=0;
    for(unsigned i=0;i<4096;i++){reference[i]=1000;time+=i&1?1500:500;observed[i]=time;}
    assert(raw_profile_build(&raw_source,reference,4096,4096000,false));
    revolution.samples=observed;revolution.index_time=4096000;revolution.count=g_write.flux_count=4096;g_write.state=WRITE_COMPLETE;
    assert(ufi_write_verify()==UFI_ERR_DMA && raw_available && raw_report[2]==2);
    uint8_t report_copy[RAW_REPORT_SIZE];assert(ufi_raw_verify_report(report_copy)==RAW_REPORT_SIZE);
    assert(!memcmp(report_copy,raw_report,RAW_REPORT_SIZE));
    /* flags2 bit4 (4.17): the fingerprint alone decides - deviation keeps status 0 with level 1 unless
     * op 31 flag 0x02 demands the match; a matching read-back gives level 2; op 31 flag 0x01 is not needed */
    options[2]=1;assert(!ufi_raw_verify_options(options,5,option_reply));
    g_write.raw_check=true;g_write.state=WRITE_COMPLETE;raw_available=false;verify_ref.n=1;
    assert(!ufi_write_verify() && g_write.verify_level==1 && raw_available && raw_report[2]==2 && g_write.state==WRITE_IDLE && captures==5);
    options[2]=3;assert(!ufi_raw_verify_options(options,5,option_reply));g_write.state=WRITE_COMPLETE;
    assert(ufi_write_verify()==UFI_ERR_DMA && raw_report[2]==2 && g_write.state==WRITE_ERROR);
    for(unsigned i=0;i<4096;i++)observed[i]=(i+1u)*1000u;                 /* the read-back equals the source */
    options[2]=0;assert(!ufi_raw_verify_options(options,5,option_reply));g_write.state=WRITE_COMPLETE;
    assert(!ufi_write_verify() && g_write.verify_level==VERIFY_READBACK_PLAUSIBLE && raw_report[2]==1 && captures==7);
    g_write.raw_check=false;verify_ref.n=0;
    puts("write readback: media binding before capture, explicit content/plausibility levels and the raw-only verdict passed");
}
'''
    run('regression_sd_verify_state',code,('src/ufi_verify.c','src/ufi_mfm.c','src/ufi_raw_verify.c'))

if __name__=='__main__':main()
