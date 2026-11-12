"""Board safety service and pin start-up against register doubles.

Two production paths that decide what the drive lines do without a host command:

1. ufi_board_service() - the USB-loss reaction.  A lost USB connection ends USB-owned work
   and puts the drive into its safe state; work that a LAN session owns keeps running, the
   same rule ufi_composite_service() applies.
2. board_gpio_init() - the released level of the Shugart outputs depends on the board
   revision (LS07 on v0.x: high, MOSFET gates on v1.0: low).  With an unknown BOARD_ID
   neither level is safe, so those pins stay inputs and the board hardware keeps the lines
   released (100k gate pull-downs on v1.0, open LS07 inputs on v0.x).

The doubles model register writes and call order only, no electrical behaviour.
"""
import re

from test_regressions import BASE, ERRORS, FW, typedef, function, run


def define(file, name):
    """One '#define NAME value' line of a production source, verbatim."""
    source = (FW / 'src' / file).read_text(encoding='utf-8')
    return re.search(r'^#define ' + name + r'\s+\w+', source, re.M).group() + '\n'


def usb_loss():
    code = BASE + ERRORS
    code += typedef(FW / 'include/ufi_firmware.h', 'capture_state_t')
    code += typedef(FW / 'include/ufi_firmware.h', 'write_state_t')
    code += define('ufi_board.c', 'SAMPLE_MS') + define('ufi_board.c', 'USB_LOST_MS')
    code += r'''
enum { UFI_ROUTE_NONE = 0, UFI_ROUTE_USB = 1, UFI_ROUTE_LAN = 2 };
enum { USBD_STATE_DEFAULT = 1, USBD_STATE_CONFIGURED = 3, USBD_STATE_SUSPENDED = 4 };
static struct { int dev_state; } hUsbDevice;
static struct { capture_state_t state; } g_capture;
typedef struct { uint16_t motor_off_s; } drive_timing_t;
static uint32_t last_sample_ms, last_activity_ms, usb_ok_ms;
static bool usb_seen, usb_lost;

static uint32_t now;
static uint8_t owner;
static bool dump;
static unsigned capture_aborts, write_aborts, safe_states, samples;
static uint32_t HAL_GetTick(void) { return now; }
static bool ufi_dump_active(void) { return dump; }
static uint8_t ufi_v2_owner(void) { return owner; }
static void ufi_capture_abort(void) { capture_aborts++; }
static void ufi_write_abort(void) { write_aborts++; }
static void ufi_drive_safe_state(void) { safe_states++; }
static bool ufi_stream_active(void) { return false; }
static write_state_t ufi_write_get_state(void) { return WRITE_IDLE; }
static drive_timing_t ufi_drive_get_timing(void) { return (drive_timing_t){0}; }
static bool ufi_drive_motor_is_on(void) { return false; }
static int ufi_drive_motor(bool on) { (void)on; return 0; }
static void ufi_sync_service(void) {}
static void sample_rails(void) { samples++; }
'''
    code += function('ufi_board.c', 'ufi_board_service')
    code += r'''
static void connect(void) { hUsbDevice.dev_state = USBD_STATE_CONFIGURED; now += 20; ufi_board_service(); }
static void lose(int state) {
    hUsbDevice.dev_state = state;
    now += USB_LOST_MS; ufi_board_service();        /* exactly the limit: not yet */
    now += 1; ufi_board_service();
    now += 1000; ufi_board_service();               /* reported once per loss */
}
static unsigned aborts(void) { return capture_aborts + write_aborts + safe_states; }
int main(void) {
    g_capture.state = CAPTURE_IDLE;
    /* never connected (stand-alone, LAN only): no USB-loss reaction at all */
    hUsbDevice.dev_state = USBD_STATE_DEFAULT; now = 5000; ufi_board_service();
    assert(!aborts());

    /* USB owns the drive (or nobody does): the loss ends the work once */
    owner = UFI_ROUTE_USB; connect(); lose(USBD_STATE_SUSPENDED);
    assert(capture_aborts == 1 && write_aborts == 1 && safe_states == 1);
    owner = UFI_ROUTE_NONE; connect(); lose(USBD_STATE_DEFAULT);
    assert(capture_aborts == 2 && write_aborts == 2 && safe_states == 2);

    /* a LAN session owns the drive: unplugging USB or a sleeping PC must not end its work */
    owner = UFI_ROUTE_LAN; connect(); lose(USBD_STATE_SUSPENDED);
    assert(capture_aborts == 2 && write_aborts == 2 && safe_states == 2);
    connect(); lose(USBD_STATE_DEFAULT);
    assert(capture_aborts == 2 && write_aborts == 2 && safe_states == 2);
    assert(samples);                                /* the rest of the service kept running */

    /* stand-alone dump: unchanged, USB is not needed */
    owner = UFI_ROUTE_NONE; dump = true; connect(); lose(USBD_STATE_DEFAULT);
    assert(capture_aborts == 2 && write_aborts == 2 && safe_states == 2);

    puts("board service: USB loss ends USB-owned work once and leaves LAN-owned work alone");
    return 0;
}
'''
    run('regression_board_usb_loss', code)


def pin_start():
    code = BASE + r'''
#define BOARD_H                             /* the real headers pull the HAL; replaced below */
#define UFI_FIRMWARE_H
typedef struct { uint32_t out, ODR; } GPIO_TypeDef;        /* out: pins in push-pull output mode */
typedef struct { uint32_t Pin, Mode, Pull, Speed, Alternate; } GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { GPIO_TypeDef* port; uint16_t pin; } gpio_pin_t;
static GPIO_TypeDef ports[7];
#define GPIOA (&ports[0])
#define GPIOB (&ports[1])
#define GPIOC (&ports[2])
#define GPIOD (&ports[3])
#define GPIOE (&ports[4])
#define GPIOF (&ports[5])
#define GPIOG (&ports[6])
#define GPIO_PIN_0 0x0001u
#define GPIO_PIN_1 0x0002u
#define GPIO_PIN_2 0x0004u
#define GPIO_PIN_3 0x0008u
#define GPIO_PIN_4 0x0010u
#define GPIO_PIN_5 0x0020u
#define GPIO_PIN_6 0x0040u
#define GPIO_PIN_7 0x0080u
#define GPIO_PIN_8 0x0100u
#define GPIO_PIN_9 0x0200u
#define GPIO_PIN_10 0x0400u
#define GPIO_PIN_11 0x0800u
#define GPIO_PIN_12 0x1000u
#define GPIO_PIN_13 0x2000u
#define GPIO_PIN_14 0x4000u
#define GPIO_PIN_15 0x8000u
enum { GPIO_MODE_INPUT, GPIO_MODE_OUTPUT_PP, GPIO_MODE_AF_PP };
enum { GPIO_NOPULL, GPIO_PULLUP, GPIO_PULLDOWN };
enum { GPIO_SPEED_FREQ_LOW, GPIO_SPEED_FREQ_MEDIUM, GPIO_SPEED_FREQ_HIGH, GPIO_SPEED_FREQ_VERY_HIGH };
#define FDD_RDATA_AF 1u
#define FDD_INDEX_AF 1u
#define APL_RDDATA_AF 1u
#define BUS_OUT_RELEASE GPIO_PIN_SET
#define __HAL_RCC_GPIOA_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOB_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOC_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOD_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOE_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOF_CLK_ENABLE() ((void)0)
#define __HAL_RCC_GPIOG_CLK_ENABLE() ((void)0)
static void HAL_GPIO_WritePin(GPIO_TypeDef* p, uint32_t pins, GPIO_PinState s) {
    if (s == GPIO_PIN_SET) p->ODR |= pins; else p->ODR &= ~pins;
}
static void HAL_GPIO_Init(GPIO_TypeDef* p, GPIO_InitTypeDef* g) {
    if (g->Mode == GPIO_MODE_OUTPUT_PP) p->out |= g->Pin; else p->out &= ~g->Pin;
}
static uint8_t rev;
static uint8_t ufi_board_rev(void) { return rev; }
void board_gpio_init(void);
#include "../src/board_headless.c"

/* the Shugart outputs whose released level depends on the revision (WGATE is handled apart) */
static const gpio_pin_t* const shugart[] = {
    &PIN_FDD_MOTOR_A, &PIN_FDD_MOTOR_B, &PIN_FDD_DRV_SEL_A, &PIN_FDD_DRV_SEL_B, &PIN_FDD_STEP,
    &PIN_FDD_DIR, &PIN_FDD_SIDE_SEL, &PIN_FDD_DENSITY, &PIN_FDD_DRATE, &PIN_FDD_WDATA, &PIN_INDEX_SIM,
};
enum { SHUGART = sizeof shugart / sizeof shugart[0] };
static bool is_output(const gpio_pin_t* p) { return (p->port->out & p->pin) != 0; }
static bool is_high(const gpio_pin_t* p) { return (p->port->ODR & p->pin) != 0; }
static void start(uint8_t r) { memset(ports, 0, sizeof ports); rev = r; board_gpio_init(); }
/* true on every board: WGATE_n driven and released, supplies off, firmware write lock closed */
static void always(void) {
    assert(is_output(&PIN_FDD_WGATE) && is_high(&PIN_FDD_WGATE));
    assert(is_output(&PIN_FDD5_EN) && !is_high(&PIN_FDD5_EN) && is_output(&PIN_FDD12_EN) && !is_high(&PIN_FDD12_EN));
    assert(is_output(&PIN_WLOCK) && is_high(&PIN_WLOCK));
}
int main(void) {
    for (uint8_t r = 5; r <= 7; r++) {              /* v0.x: LS07 inputs, released = high */
        start(r); always(); assert(!board_fdd_mosfet);
        for (unsigned i = 0; i < SHUGART; i++) assert(is_output(shugart[i]) && is_high(shugart[i]));
    }
    start(10); always(); assert(board_fdd_mosfet);  /* v1.0: MOSFET gates, released = low */
    for (unsigned i = 0; i < SHUGART; i++) assert(is_output(shugart[i]) && !is_high(shugart[i]));
    assert((GPIOC->out & (GPIO_PIN_13 | GPIO_PIN_7)) == (GPIO_PIN_13 | GPIO_PIN_7));
    assert(!(GPIOC->ODR & (GPIO_PIN_13 | GPIO_PIN_7)));

    /* unknown BOARD_ID: either level asserts the lines on one board family, so none is driven */
    start(0); always(); assert(!board_fdd_mosfet);
    for (unsigned i = 0; i < SHUGART; i++) assert(!is_output(shugart[i]));

    puts("pin start-up: released levels per revision, no Shugart output driven with an unknown BOARD_ID");
    return 0;
}
'''
    run('regression_board_pin_start', code)


def main():
    usb_loss()
    pin_start()


if __name__ == '__main__':
    main()
