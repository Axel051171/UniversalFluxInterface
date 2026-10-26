"""Execute production line guards and operating profiles with GPIO doubles."""
from test_regressions import BASE, ERRORS, FW, typedef, function, run


def main():
    code=BASE+ERRORS
    for name in ('drive_type_t','drive_status_t','drive_timing_t'):
        code+=typedef(FW/'include/ufi_firmware.h',name)
    code+=r'''
#include "ufi_media.h"
#include "ufi_capabilities.h"
#include "ufi_drive_extra.h"
#include "ufi_write_codec.h"
static drive_type_t g_current_drive=DRIVE_SHUGART_A;
static drive_status_t g_drive_status[DRIVE_TYPE_COUNT];
static media_state_t media[DRIVE_TYPE_COUNT];
static drive_timing_t g_timing={.step_pulse_us=10,.spinup_ms=500};
static drive_extra_t extra_timing;
static uint8_t rpm_line,operating_profiles[DRIVE_TYPE_COUNT][40];
static bool rpm_360_low=true;
static uint16_t rpm_set;
static unsigned writes;
static bool density_level,drate_level;
static const int PIN_FDD_DENSITY=1,PIN_FDD_DRATE=2;
typedef int gpio_pin_t;
static const int PIN_FDD_DRV_SEL_A=3,PIN_FDD_DRV_SEL_B=4,PIN_FDD_MOTOR_A=5,PIN_FDD_MOTOR_B=6;
static const gpio_pin_t* const shugart_sel[4]={&PIN_FDD_MOTOR_A,&PIN_FDD_DRV_SEL_B,&PIN_FDD_DRV_SEL_A,&PIN_FDD_DRATE};
#define MOTOR_SPINUP_MS g_timing.spinup_ms
#define SELECT_SETTLE_US g_timing.select_settle_us
static bool is_shugart(drive_type_t d){return d>=DRIVE_SHUGART_DS0 && d<=DRIVE_SHUGART_DS3;}
static bool is_apple(drive_type_t d){return d==DRIVE_APPLE_II || d==DRIVE_APPLE2;}
static void bus_out(const int* p,bool v){writes++;if(*p==1)density_level=v;else drate_level=v;}
static void delay_us(uint32_t n){(void)n;}
static void HAL_Delay(uint32_t n){(void)n;}
static void apl_release(void){}
static bool board_v10;                  /* v1.0: drive id 11 is the Mac drive on J23 */
static bool is_mac(drive_type_t d){return d==DRIVE_APPLE2 && board_v10;}
static int mac,mac_enables;
static void mac_reset(int* d){*d=0;}
static void mac_enable(void){mac_enables++;}
static bool ufi_board_has_apple(void){return true;}
static const gpio_pin_t* amiga_sel(drive_type_t d){return d==DRIVE_AMIGA2?&PIN_FDD_DRV_SEL_A:&PIN_FDD_DRV_SEL_B;}
void ufi_drive_set_timing(const drive_timing_t* t);
int ufi_drive_set_rpm(uint16_t rpm);
void ufi_drive_rpm_config(uint8_t line,bool low);
'''
    for name in ('ufi_drive_init','ufi_drive_select','line_conflicts','ufi_drive_port_report','ufi_drive_set_timing',
                 'ufi_drive_timing_checked','ufi_drive_rpm_config','ufi_drive_set_rpm',
                 'ufi_drive_density_line','ufi_drive_lines_set','ufi_drive_operating_profile',
                 'ufi_drive_select_native'):
        code+=function('ufi_drive.c',name)
    code+=r'''
int main(void){
    uint8_t p[40],q[40],report[24];
    uint32_t token=ufi_config_generation();
    assert(!ufi_drive_operating_profile(NULL,p));p[35]=2;p[37]=0x68;p[38]=1;
    assert(token==ufi_config_generation());
    assert(!ufi_drive_operating_profile(p,q) && drate_level && q[39]==1);
    assert(token!=ufi_config_generation());token=ufi_config_generation();
    unsigned before=writes;
    assert(ufi_drive_lines_set(false,false)==UFI_ERR_BAD_ARGS && writes==before);
    assert(token==ufi_config_generation());
    g_current_drive=DRIVE_SHUGART_DS0;
    assert(ufi_drive_set_rpm(300)==UFI_ERR_BAD_ARGS && writes==before);
    assert(ufi_drive_operating_profile(p,q)==UFI_ERR_BAD_ARGS && writes==before);
    ufi_drive_port_report(report);assert(report[7]==1 && report[3]==15 && (report[2]&38)==38);
    g_current_drive=DRIVE_SHUGART_B;p[35]=1;p[17]=1;p[37]=0x2c;p[38]=1;
    assert(!ufi_drive_operating_profile(p,q) && !density_level);
    drive_timing_t conflict=g_timing;conflict.tg43=1;before=writes;
    assert(ufi_drive_timing_checked(&conflict)==UFI_ERR_BAD_ARGS && !g_timing.tg43 && writes==before);
    assert(ufi_drive_density_line(true)==UFI_ERR_BAD_ARGS && writes==before);
    assert(!ufi_drive_select_native(DRIVE_SHUGART_A) && rpm_line==2 && rpm_set==360 && !g_timing.double_step);
    assert(!ufi_drive_select_native(DRIVE_SHUGART_B) && rpm_line==1 && rpm_set==300 && g_timing.double_step==1);
    token=ufi_config_generation();
    assert(!ufi_drive_select_native(DRIVE_SHUGART_A));
    assert(!ufi_drive_select_native(DRIVE_SHUGART_B));
    assert(token!=ufi_config_generation() && g_timing.double_step==1 && rpm_set==300);
    p[35]=0;p[37]=p[38]=0;p[21]=1;
    assert(!ufi_drive_operating_profile(p,q) && g_timing.tg43);
    before=writes;assert(ufi_drive_lines_set(true,true)==UFI_ERR_BAD_ARGS && writes==before);
    p[39]=1;assert(ufi_drive_operating_profile(p,q)==UFI_ERR_BAD_ARGS);p[39]=0;
    g_current_drive=DRIVE_APPLE_II;assert(ufi_drive_operating_profile(p,q)==UFI_ERR_BAD_ARGS);
    ufi_drive_port_report(report);assert(report[7]==4);
    assert(ufi_drive_select_native(DRIVE_TYPE_COUNT)==UFI_ERR_BAD_ARGS);
    token=ufi_config_generation();before=writes;
    assert(ufi_drive_select((drive_type_t)-1)==UFI_ERR_BAD_ARGS && writes==before);
    assert(token==ufi_config_generation());
    ufi_drive_init();assert(token!=ufi_config_generation() && g_current_drive==DRIVE_NONE);
    assert(media[DRIVE_APPLE2].drive[1]==39 && media[DRIVE_APPLE2].drive[2]==1 && !media[DRIVE_APPLE2].drive[3]);
    assert(!ufi_drive_select(DRIVE_APPLE2) && !mac_enables);              /* v0.7: J15 Disk II, no registers */
    board_v10=true;ufi_drive_init();                                     /* v1.0: id 11 = Mac drive on J23 */
    assert(media[DRIVE_APPLE2].drive[1]==79 && media[DRIVE_APPLE2].drive[2]==2 && media[DRIVE_APPLE2].drive[3]);
    assert(media[DRIVE_APPLE_II].drive[1]==39 && media[DRIVE_APPLE_II].drive[2]==1);
    assert(!ufi_drive_select(DRIVE_APPLE2) && mac_enables==1 && g_current_drive==DRIVE_APPLE2);
    assert(!ufi_drive_select(DRIVE_APPLE_II) && mac_enables==1);
    puts("drive profiles: atomic rejection, shared lines, native switching, port report and Mac drive identity passed");
}
'''
    run('regression_drive_profiles',code,('src/ufi_media.c','src/ufi_drive_extra.c','src/ufi_capabilities.c'))


if __name__=='__main__':
    main()
