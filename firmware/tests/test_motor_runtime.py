"""Production motor control: no repeated spin-up, cancellation turns the motor off."""
from test_regressions import BASE, ERRORS, FW, typedef, function, run


def main():
    code=BASE+ERRORS+typedef(FW/'include/ufi_firmware.h','drive_type_t')+r'''
static drive_type_t g_current_drive;
static struct {bool motor_on;} g_drive_status[DRIVE_TYPE_COUNT];
static uint32_t motor_started_ms,now;
static bool motor_step_pending,cancelled;
static unsigned waits,outputs;
#define MOTOR_SPINUP_MS 500u
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
static const int PIN_FDD_MOTOR_A=1,PIN_FDD_MOTOR_B=2;
static const struct {int port,pin;} pin={0,0};
static const void* apl_en(drive_type_t t);
static bool ufi_drive_motor_is_on(void){return g_drive_status[g_current_drive].motor_on;}
static uint32_t HAL_GetTick(void){return now;}
static bool ufi_delay_ms(unsigned ms){waits++;now+=cancelled?1:ms;return !cancelled;}
static void bus_out(const int* p,bool value){(void)p;(void)value;outputs++;}
static void amiga_motor(bool on){(void)on;outputs++;}
static void HAL_GPIO_WritePin(int port,int p,int value){(void)port;(void)p;(void)value;outputs++;}
int ufi_drive_motor(bool on);
'''
    code=code.replace('static const void* apl_en(drive_type_t t);',
        'static const __typeof__(pin)* apl_en(drive_type_t t){(void)t;return &pin;}')
    code+=function('ufi_drive.c','ufi_drive_motor')+r'''
int main(void){
    const drive_type_t drives[]={DRIVE_SHUGART_A,DRIVE_SHUGART_B,DRIVE_AMIGA,DRIVE_AMIGA2,
        DRIVE_APPLE_II,DRIVE_APPLE2,DRIVE_SHUGART_DS0,DRIVE_SHUGART_DS3};
    for(unsigned i=0;i<sizeof drives/sizeof drives[0];i++){
        memset(g_drive_status,0,sizeof g_drive_status);g_current_drive=drives[i];
        now=waits=outputs=0;cancelled=false;
        assert(!ufi_drive_motor(true) && waits==1 && now==500 && ufi_drive_motor_is_on() && motor_step_pending);
        assert(motor_started_ms==0);
        unsigned edges=outputs;
        assert(!ufi_drive_motor(true) && waits==1 && now==500 && outputs==edges);
        assert(!ufi_drive_motor(false) && !ufi_drive_motor_is_on() && !motor_step_pending);
        cancelled=true;
        assert(ufi_drive_motor(true)==UFI_ERR_CANCELLED && !ufi_drive_motor_is_on() && !motor_step_pending);
    }
    puts("motor: first spin-up, idempotent warm start, all port families and cancellation passed");
    return 0;
}
'''
    run('regression_motor_runtime',code)


if __name__=='__main__':main()
