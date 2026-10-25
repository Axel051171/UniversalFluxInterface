"""Execute destructive diagnostic control flow with timed safety faults.

Production functions are extracted verbatim. Register doubles establish software
ordering and cleanup; they cannot establish the physical gate latency.
"""
from test_regressions import BASE, ERRORS, FW, typedef, function, run


def main():
    code=BASE+ERRORS+'#include "ufi_write_codec.h"\n'
    code+=typedef(FW/'include/ufi_firmware.h','write_state_t')
    code+=typedef(FW/'include/ufi_firmware.h','capture_state_t')
    code+=r'''
static struct {write_state_t state;} g_write;
static struct {capture_state_t state;} g_capture;
static struct {uint32_t DIER,ARR,EGR,SR,CCMR1,CR1,CCR1;} tim;
#define TIM3 (&tim)
static int htim3;
#define __HAL_TIM_DISABLE(p) ((void)(p),tim.CR1&=~1u)
#define __HAL_TIM_ENABLE(p) ((void)(p),tim.CR1|=1u)
#define TIM_DIER_UDE 256u
#define TIM_DIER_UIE 1u
#define TIM_CR1_DIR 16u
#define TIM_CCMR1_OC1PE 8u
#define TIM_CCMR1_OC1M 112u
#define TIM_OCMODE_PWM1 96u
#define TIM_EGR_UG 1u
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
static const int PIN_LED_FDD=1,PIN_FDD_WGATE=2;
static const struct {int port,pin;} PIN_APL_WRREQ={0,0};
static bool gate,apple_gate,led,cancelled,locked,soft_locked,protected,motor=true,apple,streaming;
static volatile bool diagnostic_write;
static uint32_t tick,gate_at,gate_duration,media=1,cancel_at,change_at,protect_at,lock_at;
static unsigned watchdogs,opened,side_calls,finished;
static int seek_error,side_error;
static bool no_index;
#define UFI_WATCHDOG_FEED() (++watchdogs)
static uint32_t HAL_GetTick(void){
    uint32_t t=tick++;
    if(cancel_at && t>=cancel_at)cancelled=true;
    if(change_at && t>=change_at)media=2;
    if(protect_at && t>=protect_at)protected=true;
    if(lock_at && t>=lock_at)locked=true;
    return t;
}
static void HAL_Delay(uint32_t n){while(n--)HAL_GetTick();}
static bool ufi_cancel_requested(void){return cancelled;}
static bool ufi_board_write_locked(void){return locked||soft_locked;}
static bool ufi_board_soft_locked(void){return soft_locked;}
static bool ufi_drive_write_protected(void){return protected;}
static bool ufi_drive_motor_is_on(void){return motor;}
static bool ufi_drive_is_apple(void){return apple;}
static bool ufi_stream_active(void){return streaming;}
static uint32_t ufi_drive_media_token(void){return media;}
static bool ufi_drive_media_matches(uint32_t m){return m==media;}
static int ufi_drive_seek(uint8_t t){(void)t;return seek_error;}
static int ufi_drive_select_side(uint8_t s){side_calls++;return s>1?UFI_ERR_BAD_ARGS:side_error;}
static bool ufi_flux_index_asserted(void){return !no_index && tick%10u==0;}
static void led_set(const int* p,bool b){(void)p;led=b;}
static void bus_out(const int* p,bool b){(void)p;gate=b;if(b){gate_at=tick;opened++;}}
static void HAL_GPIO_WritePin(int p,int pin,int level){
    (void)p;(void)pin;apple_gate=level==GPIO_PIN_RESET;
    if(apple_gate){gate_at=tick;opened++;}
}
static void wdata_idle(void){}
static void write_finish(write_state_t s){
    gate_duration=tick-gate_at;gate=apple_gate=led=false;tim.CR1&=~1u;
    g_write.state=s;diagnostic_write=false;finished++;
}
'''
    for name in ('blocking_write_guard','blocking_write_wait','ufi_erase_track_ms','ufi_write_pattern'):
        code+=function('ufi_write.c',name)
    code+=r'''
static void reset(void){
    gate=apple_gate=led=cancelled=locked=soft_locked=protected=apple=streaming=no_index=false;
    motor=true;tick=gate_at=gate_duration=cancel_at=change_at=protect_at=lock_at=0;media=1;
    seek_error=side_error=0;opened=watchdogs=side_calls=finished=0;
    g_write.state=WRITE_IDLE;g_capture.state=CAPTURE_IDLE;
    memset(&tim,0,sizeof tim);
}
static int operation(unsigned pattern,uint8_t side){
    return pattern?ufi_write_pattern(0,side,4000,5000):ufi_erase_track_ms(0,side,5000);
}
static void safe(void){assert(!gate && !apple_gate && !led && g_write.state==WRITE_IDLE);}
int main(void){
    (void)HAL_Delay; /* retained only to make the pre-fix implementation executable */
    for(unsigned pattern=0;pattern<2;pattern++){
        reset();assert(operation(pattern,2)==UFI_ERR_BAD_ARGS && !opened);safe();
        reset();side_error=UFI_ERR_NOT_IMPL;assert(operation(pattern,1)==UFI_ERR_NOT_IMPL && !opened);safe();
        reset();seek_error=UFI_ERR_CANCELLED;assert(operation(pattern,0)==UFI_ERR_CANCELLED && !opened);safe();
        reset();soft_locked=true;assert(operation(pattern,0)==UFI_ERR_SOFT_LOCK && !side_calls);safe();
        reset();motor=false;assert(operation(pattern,0)==UFI_ERR_NO_DRIVE && !opened);safe();
        reset();streaming=true;assert(operation(pattern,0)==UFI_ERR_BUSY && !opened);safe();
        reset();cancelled=true;assert(operation(pattern,0)==UFI_ERR_CANCELLED && !opened);safe();
        reset();cancel_at=10;assert(operation(pattern,0)==UFI_ERR_CANCELLED && !opened);safe();
        reset();cancel_at=20;assert(operation(pattern,0)==UFI_ERR_CANCELLED && !opened);safe(); /* exactly at settle completion */
        reset();cancel_at=100;assert(operation(pattern,0)==UFI_ERR_CANCELLED && opened==1);
        assert(gate_duration<100 && finished==1 && watchdogs);safe();
        reset();change_at=100;assert(operation(pattern,0)==UFI_ERR_MEDIA_CHANGED && opened==1);safe();
        reset();protect_at=100;assert(operation(pattern,0)==UFI_ERR_WRITE_PROT && opened==1);safe();
        reset();lock_at=100;assert(operation(pattern,0)==UFI_ERR_WRITE_PROT && opened==1);safe();
        reset();assert(!operation(pattern,0) && opened==1 && gate_duration>=5000 && gate_duration<5010);safe();
        reset();tick=UINT32_MAX-50u;assert(!operation(pattern,0));safe(); /* wrap */
    }
    reset();apple=true;cancel_at=100;
    assert(ufi_erase_track_ms(0,0,5000)==UFI_ERR_CANCELLED && opened==1 && finished==1);safe();
    reset();apple=true;assert(ufi_write_pattern(0,0,4000,10)==UFI_ERR_NOT_IMPL && !opened);safe();
    reset();no_index=true;cancel_at=100;
    assert(ufi_erase_track_ms(0,0,5000)==UFI_ERR_CANCELLED && !opened);safe();
    reset();no_index=true;assert(ufi_erase_track_ms(0,0,5000)==UFI_ERR_NO_INDEX && !opened && watchdogs);safe();
    reset();tim.CCMR1=TIM_CCMR1_OC1PE;tim.CCR1=0;
    assert(!ufi_write_pattern(0,0,4000,10) && tim.CCR1==WRITE_PULSE_TICKS-1u);
    assert((tim.CR1&TIM_CR1_DIR) && !(tim.CCMR1&TIM_CCMR1_OC1PE));safe();
    return 0;
}
'''
    run('regression_write_safety',code)


if __name__=='__main__':
    main()
