"""Production HFEv3 SD writer and capture publication, using bounded FatFs doubles."""
from test_regressions import BASE,ERRORS,function,run
def main():
    code=BASE+ERRORS+r'''
#include "ufi_hfe_v3.h"
#include "ufi_image.h"
#define FR_OK 0
#define FR_NO_FILE 1
#define FA_WRITE 1
#define FA_CREATE_NEW 2
#define FLUX_TIMER_FREQ 275000000u
#define UFI_WATCHDOG_FEED() ((void)0)
typedef unsigned UINT;
typedef struct {unsigned pos;} FIL;
typedef struct {unsigned unused;} FILINFO;
static FIL file;
static uint8_t stage[32768],disk[70000],cyl,head,q_spt;
static uint32_t table[168],hfe_rate,q_rate=250000;
static uint16_t hfe_next_block,file_no;
static uint8_t hfe_enc,file_open;
static bool q_known=true,q_amiga,cancelled,io_error;
static const char* file_ext=".HFE";
static char cur_ext[5],pending_name[13],completed_name[13],opened[13];
static bool published;
static uint32_t samples[16];
typedef struct {const void* dummy;} unused_t;
typedef struct {uint32_t timestamp;} flux_sample_t;
typedef struct {flux_sample_t* samples;uint32_t count,index_time;} flux_revolution_t;
static flux_revolution_t revolution={(flux_sample_t*)samples,16,17600};
static unsigned ufi_flux_get_revolution_count(void){return 1;}
static const flux_revolution_t* ufi_flux_get_revolution(unsigned n){(void)n;return &revolution;}
static bool ufi_cancel_requested(void){return cancelled;}
static int f_lseek(FIL* f,unsigned position){if(position>sizeof(disk))return 2;f->pos=position;return 0;}
static int f_write(FIL* f,const void* data,unsigned n,unsigned* written){
    *written=0;if(io_error || n>sizeof(disk)-f->pos)return 2;
    memcpy(disk+f->pos,data,n);f->pos+=n;*written=n;return 0;
}
static int f_stat(const char* name,FILINFO* info){(void)info;
    /* An interrupted first capture must never be overwritten. */
    return !strcmp(name,"DUMP0001.PRT")?FR_OK:FR_NO_FILE;
}
static int f_open(FIL* f,const char* name,unsigned flags){(void)f;assert(flags==(FA_WRITE|FA_CREATE_NEW));memcpy(opened,name,13);return 0;}
static int f_rename(const char* old,const char* name){assert(!strcmp(old,"DUMP0002.PRT") && !strcmp(name,"DUMP0002.HFE"));if(io_error)return 2;published=true;return 0;}
'''
    for name in ('open_next_file','publish_capture','write_hfe_v3_track'):
        code+=function('ufi_dump.c',name)
    code+=r'''
static bool read_side(void* context,uint32_t offset,uint8_t* byte){
    unsigned side=*(unsigned*)context;*byte=disk[1024+(offset/256)*512+side*256+offset%256];return true;
}
int main(void){
    (void)q_spt;
    assert(!open_next_file() && file_no==2 && file_open && !strcmp(opened,"DUMP0002.PRT") && !strcmp(cur_ext,".HFE"));
    for(unsigned i=0;i<16;i++)samples[i]=(i+1)*1100;
    assert(!write_hfe_v3_track() && (table[0]&65535)==65534 && hfe_enc==HFE_ENC_ISOIBM_MFM);
    head=1;assert(!write_hfe_v3_track());
    uint32_t flux[32];hfe_v3_report_t report;
    for(unsigned side=0;side<2;side++){
        assert(!hfe_v3_decode(read_side,&side,32767,flux,32,&report));
        assert(report.cells==32 && report.flux_count==16 && report.ticks==17600 && report.index_count==1);
        for(unsigned i=0;i<16;i++)assert(flux[i]==1100);
    }
    cancelled=true;assert(write_hfe_v3_track()==UFI_ERR_CANCELLED);cancelled=false;
    io_error=true;assert(write_hfe_v3_track()==UFI_ERR_STORAGE);
    assert(publish_capture()==UFI_ERR_STORAGE && !published);
    io_error=false;assert(!publish_capture() && published);
    puts("SD HFEv3 export: two sides roundtrip, NOP extent padding, cancellation, write errors and PRT publication passed");
}
'''
    run('regression_sd_export',code,('src/ufi_hfe_v3.c','src/ufi_image.c'))
if __name__=='__main__':main()
