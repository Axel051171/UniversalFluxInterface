"""Production seek/recalibration and media profiles using simulated drive signals."""
from test_regressions import BASE, ERRORS, FW, typedef, function, run


def main():
    code=BASE+ERRORS
    for name in ('drive_type_t','drive_status_t','drive_timing_t'):
        code+=typedef(FW/'include/ufi_firmware.h',name)
    code+=r'''
#include "ufi_media.h"
#include "ufi_capabilities.h"
static drive_type_t g_current_drive=DRIVE_SHUGART_A;
static drive_status_t g_drive_status[DRIVE_TYPE_COUNT];
static media_state_t media[DRIVE_TYPE_COUNT];
static drive_timing_t g_timing;
static int g_last_dir,physical=30,direction;
static bool change_line,motor=true,stuck,missing;
static int early;
static unsigned pulses;
static const int PIN_FDD_DIR=1,PIN_FDD_STEP=2,PIN_FDD_SIDE_SEL=3;
#define DIR_SETUP_US 1
#define STEP_PULSE_US 10
#define STEP_RATE_US 3000
#define SETTLE_TIME_US 15000
static bool is_apple(drive_type_t t){return t==DRIVE_APPLE_II || t==DRIVE_APPLE2;}
static bool is_shugart(drive_type_t t){return t>=DRIVE_SHUGART_DS0 && t<=DRIVE_SHUGART_DS3;}
static bool ufi_drive_motor_is_on(void){return motor;}
static bool ufi_drive_disk_changed(void){return change_line;}
static bool ufi_drive_at_track0(void){return !missing && (stuck || physical<=early);}
static void delay_us(unsigned n){(void)n;}
static void motor_step_wait(void){}
static void tg43_apply(unsigned n){(void)n;}
static int apl_seek(uint8_t n){(void)n;return UFI_ERR_NOT_IMPL;}
static int apl_recalibrate(void){return UFI_ERR_NOT_IMPL;}
static int ufi_drive_apple_step(int n){(void)n;return UFI_ERR_NOT_IMPL;}
static void bus_out(const int* pin,bool level){
    if(*pin==1)direction=level?1:-1;
    if(*pin==2 && !level){physical+=direction;if(physical<0)physical=0;pulses++;change_line=false;}
}
int ufi_drive_recalibrate(void);
'''
    for name in ('ufi_drive_media_token','ufi_drive_media_matches','ufi_drive_media_status',
                 'ufi_drive_profile_set','ufi_drive_media_set','ufi_drive_media_notify',
                 'ufi_drive_position_invalidate','ufi_drive_media_observed','ufi_drive_step','ufi_drive_seek',
                 'ufi_drive_recalibrate','ufi_drive_select_side'):
        code+=function('ufi_drive.c',name)
    code+=r'''
static uint32_t rd(const uint8_t* p){return p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;}
int main(void){
    for(unsigned i=0;i<DRIVE_TYPE_COUNT;i++)media_init(&media[i],83,2,i==DRIVE_SHUGART_A);
    assert(!media[1].position_valid);assert(!ufi_drive_seek(12));
    assert(physical==12 && media[1].position_valid && g_drive_status[1].current_track==12);
    unsigned before=pulses;assert(!ufi_drive_seek(12) && pulses==before);
    /* An unexpectedly early TRK0 must never turn a wrong-track seek into success. */
    early=9;assert(ufi_drive_seek(5)==UFI_ERR_SEEK_FAIL && !media[1].position_valid);
    early=0;assert(!ufi_drive_seek(5) && physical==5);
    stuck=true;assert(ufi_drive_recalibrate()==UFI_ERR_SEEK_FAIL && !media[1].position_valid);stuck=false;
    missing=true;before=pulses;assert(ufi_drive_recalibrate()==UFI_ERR_SEEK_FAIL && pulses-before==100);missing=false;
    assert(!ufi_drive_seek(7) && physical==7);
    ufi_drive_position_invalidate();before=pulses;assert(!ufi_drive_seek(7) && pulses>before);
    uint8_t profile[8]={1,39,1,1,0x2c,1,0,0};assert(!ufi_drive_profile_set(profile));
    assert(ufi_drive_seek(40)==UFI_ERR_SEEK_FAIL && ufi_drive_select_side(1)==UFI_ERR_BAD_ARGS);
    assert(!ufi_drive_seek(39));assert(ufi_drive_step(1)==UFI_ERR_SEEK_FAIL);
    uint8_t mp[12]={1,40,1,9,2,1,1,0,0x90,0xd0,3,0}; /* host-declared 250000 bps */
    assert(!ufi_drive_media_set(mp));uint8_t report[40];ufi_drive_media_status(report);assert(report[2]&8);
    ufi_drive_media_observed(2,500000);assert(media[1].media[0]==1); /* host declaration preserved */
    uint32_t token=ufi_drive_media_token();change_line=true;
    assert(!ufi_drive_media_matches(token));uint32_t changed=ufi_drive_media_token();assert(changed!=token);
    ufi_drive_media_status(report);assert(!(report[2]&8) && (report[2]&4) && report[32]==1);
    assert(ufi_drive_media_token()==changed); /* held latch counts exactly once */
    ufi_drive_media_observed(2,250000);ufi_drive_media_status(report);
    assert(report[20]==2 && report[26]==2 && !report[21]); /* format/rate only, geometry unknown */
    assert(!ufi_drive_step(-1));assert(ufi_drive_media_token()==changed); /* STEP cleared latch */
    change_line=true;assert(ufi_drive_media_token()!=changed);
    change_line=false;ufi_drive_media_notify();ufi_drive_media_status(report);assert(report[32]==2);
    g_current_drive=DRIVE_SHUGART_DS0;profile[3]=1;assert(ufi_drive_profile_set(profile)==UFI_ERR_BAD_ARGS);
    profile[3]=0;assert(!ufi_drive_profile_set(profile));change_line=true;token=ufi_drive_media_token();
    assert(ufi_drive_media_matches(token));ufi_drive_media_status(report);assert(!(report[2]&6));
    g_current_drive=DRIVE_SHUGART_A;assert(!ufi_drive_media_matches(token));
    profile[6]=1;assert(ufi_drive_profile_set(profile)==UFI_ERR_BAD_ARGS);
    mp[5]=0;assert(ufi_drive_media_set(mp)==UFI_ERR_BAD_ARGS);
    change_line=false;mp[5]=1;
    const uint32_t rates[]={250000,500000,1000000,250000};
    for(unsigned i=0;i<4;i++){
        uint32_t config=ufi_config_generation();memcpy(mp+8,&rates[i],4);
        assert(!ufi_drive_media_set(mp) && config!=ufi_config_generation());
        ufi_drive_media_status(report);assert(rd(report+28)==rates[i]);
        config=ufi_config_generation();mp[5]=2; /* exceeds 39 physical tracks */
        assert(ufi_drive_media_set(mp)==UFI_ERR_BAD_ARGS && config==ufi_config_generation());
        ufi_drive_media_status(report);assert(rd(report+28)==rates[i] && report[25]==1);
        mp[5]=1;
    }
    g_current_drive=DRIVE_NONE;ufi_drive_media_status(report);assert(!rd(report+4) && !report[2]);
    puts("media: position validity, early/stuck/missing TRK0, limits, side checks, change latch, profile invalidation and drive identity passed");
}
'''
    run('regression_media_runtime',code,('src/ufi_media.c','src/ufi_capabilities.c'))


if __name__=='__main__':
    main()
