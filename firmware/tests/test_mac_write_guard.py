"""Mac drive (board v1.0, J23) write guards in production code: write protection from the register
status (no drive, no disk or WRTPRT = protected, the J14 WRPROT line plays no part), whole tracks
only, never the SYNC sensor as index; on a v0.7 board id 11 stays the J15 Disk II."""
from test_regressions import BASE, ERRORS, FW, function, run, typedef


def main():
    code = BASE + ERRORS + typedef(FW / 'include/ufi_firmware.h', 'drive_type_t') + r'''
static drive_type_t g_current_drive;
static bool board_v10, apple_sync, apl_wrprot, fdd_wprot;
static unsigned status_reads;
#define MAC_KIND_NONE 0u
#define MAC_KIND_800K 2u
#define MAC_F_DISK 0x01u
#define MAC_F_WPROT 0x02u
static struct {uint8_t kind;} mac;
static uint8_t mac_flags_now;
static uint8_t mac_status_flags(void){status_reads++;return mac_flags_now;}
static bool is_apple(drive_type_t t){return t==DRIVE_APPLE_II || t==DRIVE_APPLE2;}
static bool is_mac(drive_type_t t){return t==DRIVE_APPLE2 && board_v10;}
static bool ufi_config_apple_sync(void){return apple_sync;}
typedef struct {int port,pin;} pin_t;
static const pin_t PIN_APL_WRPROT={1,2};
static const int PIN_FDD_WPROT=3;
#define GPIO_PIN_RESET 0
#define GPIO_PIN_SET 1
static int HAL_GPIO_ReadPin(int port,int pin){(void)port;(void)pin;return apl_wrprot?GPIO_PIN_SET:GPIO_PIN_RESET;}
static bool bus_in(const int* p){(void)p;return fdd_wprot;}
static struct {uint16_t double_step;} g_timing;
bool ufi_drive_write_protected(void);
bool ufi_drive_sync_index(void);
bool ufi_drive_quarter_ok(uint8_t quarter);
bool ufi_drive_is_mac(void);
'''
    for name in ('ufi_drive_write_protected', 'ufi_drive_sync_index', 'ufi_drive_quarter_ok', 'ufi_drive_is_mac'):
        code += function('ufi_drive.c', name)
    code += r'''
int main(void){
    board_v10=true;g_current_drive=DRIVE_APPLE2;
    mac.kind=MAC_KIND_NONE;mac_flags_now=MAC_F_DISK;
    assert(ufi_drive_write_protected());                            /* nothing answers on RD */
    mac.kind=MAC_KIND_800K;assert(!ufi_drive_write_protected() && status_reads==2);
    mac_flags_now=0;assert(ufi_drive_write_protected());            /* no disk (CSTIN high) */
    mac_flags_now=MAC_F_DISK|MAC_F_WPROT;assert(ufi_drive_write_protected());   /* WRTPRT low */
    apl_wrprot=true;mac_flags_now=MAC_F_DISK;assert(!ufi_drive_write_protected());  /* J14 pin 20 not used */
    apple_sync=true;assert(ufi_drive_is_mac() && !ufi_drive_sync_index());          /* no SYNC index for J23 */
    assert(ufi_drive_quarter_ok(0) && !ufi_drive_quarter_ok(1) && !ufi_drive_quarter_ok(2) && !ufi_drive_quarter_ok(3));
    board_v10=false;                                                /* v0.7: id 11 = J15 Disk II */
    const unsigned reads=status_reads;
    assert(!ufi_drive_is_mac() && ufi_drive_sync_index() && ufi_drive_quarter_ok(3));
    assert(ufi_drive_write_protected() && status_reads==reads);    /* the WRPROT line again */
    apl_wrprot=false;assert(!ufi_drive_write_protected());
    g_current_drive=DRIVE_SHUGART_A;fdd_wprot=true;
    assert(ufi_drive_write_protected() && !ufi_drive_sync_index() && !ufi_drive_is_mac());
    puts("mac write guards: protection from WRTPRT / no drive / no disk, whole tracks, no SYNC index, v0.7 Disk II unchanged passed");
    return 0;
}
'''
    run('regression_mac_write_guards', code)


if __name__ == '__main__':
    main()
