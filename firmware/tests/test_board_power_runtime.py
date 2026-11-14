"""Drive supply monitoring of ufi_board.c against register doubles.

Production functions, extracted verbatim: ufi_board_power, sample_faults, sample_rails,
ufi_board_jumper_locked, ufi_board_write_enable, ufi_board_soft_lock, ufi_board_status,
ufi_board_activity and ufi_board_service.  The doubles model pin levels, ADC readings and the
millisecond tick; they say nothing about the INA180, the eFuses or real timing.

Covered: overcurrent trip per rail after OVERCURRENT_SAMPLES samples in a row, the shunt scale
per board revision, eFuse fault mask and its POWER event on change, one status per event,
status polling that does not count towards the trip, status flag bits, write enable on v1.0 against v0.x, the
motor idle-off timer and the sample period.
"""
from test_regressions import FW, typedef, function, run
from test_board_safety_runtime import define


def main():
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
'''
    code += typedef(FW / 'include/ufi_firmware.h', 'capture_state_t')
    code += typedef(FW / 'include/ufi_firmware.h', 'write_state_t')
    code += typedef(FW / 'include/ufi_firmware.h', 'board_status_t')
    for name in ('SAMPLE_MS', 'OVERCURRENT_MA', 'OVERCURRENT_SAMPLES', 'USB_LOST_MS'):
        code += define('ufi_board.c', name)
    code += r'''
typedef struct { uint32_t ODR, IDR; } GPIO_TypeDef;
typedef struct { uint32_t Pin, Mode, Pull, Speed; } GPIO_InitTypeDef;
typedef enum { GPIO_PIN_RESET, GPIO_PIN_SET } GPIO_PinState;
typedef struct { GPIO_TypeDef* port; uint16_t pin; } gpio_pin_t;
enum { GPIO_MODE_INPUT = 7, GPIO_NOPULL = 0, GPIO_SPEED_FREQ_LOW = 0 };
enum { ADC_CH_I_FDD5 = 1, ADC_CH_I_FDD12 = 2, ADC_CH_BOARD_ID = 7 };
enum { UFI_V2_EVT_POWER = 0x81, UFI_ROUTE_LAN = 2, USBD_STATE_CONFIGURED = 3 };
static GPIO_TypeDef port;
static const gpio_pin_t PIN_FDD5_EN = {&port, 0x001}, PIN_FDD12_EN = {&port, 0x002},
    PIN_FLT5 = {&port, 0x004}, PIN_FLT12 = {&port, 0x008}, PIN_WLOCK = {&port, 0x010},
    PIN_WREN_SENSE = {&port, 0x020}, PIN_BTN_A = {&port, 0x040}, PIN_BTN_B = {&port, 0x080},
    PIN_LED_ERR = {&port, 0x100};
enum { IDLE_INPUTS = 0x004 | 0x008 | 0x040 | 0x080 };       /* FLT and buttons are active low */

/* state of ufi_board.c */
static uint8_t power_mask, trip_mask, fault_mask, oc_count[2];
static bool safe_mode, usb_seen, usb_lost;
static uint16_t rail_ma[2];
static uint32_t last_sample_ms, last_activity_ms, usb_ok_ms;
static const gpio_pin_t* const rail_en[2] = {&PIN_FDD5_EN, &PIN_FDD12_EN};
static const gpio_pin_t* const rail_flt[2] = {&PIN_FLT5, &PIN_FLT12};
static const uint32_t rail_adc[2] = {ADC_CH_I_FDD5, ADC_CH_I_FDD12};
static struct { int dev_state; } hUsbDevice = {USBD_STATE_CONFIGURED};
static struct { capture_state_t state; } g_capture;
typedef struct { uint16_t motor_off_s; } drive_timing_t;

/* doubles */
static uint8_t rev;
static uint16_t adc[8];
static uint32_t now;
static unsigned conversions, statuses, events, inits, motor_offs, unsafe;
static bool led, sd, motor_on;
static uint8_t ev_code;
static board_status_t ev;
static GPIO_InitTypeDef last_init;
static drive_timing_t timing;
static uint8_t ufi_board_rev(void) { return rev; }
static uint16_t ufi_adc_mv(uint32_t ch) { assert(ch < 8u); conversions++; return adc[ch]; }
static uint32_t HAL_GetTick(void) { return now; }
static void HAL_GPIO_WritePin(GPIO_TypeDef* p, uint32_t pins, GPIO_PinState s)
{
    if (s == GPIO_PIN_SET) p->ODR |= pins; else p->ODR &= ~pins;
}
static GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef* p, uint32_t pin) { return (p->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET; }
static void HAL_GPIO_Init(GPIO_TypeDef* p, GPIO_InitTypeDef* g) { assert(p == &port); inits++; last_init = *g; }
static void led_set(const gpio_pin_t* p, bool on) { assert(p == &PIN_LED_ERR); led = on; }
static bool ufi_sd_present(void) { statuses++; return sd; }     /* called once per ufi_board_status() */
static void ufi_v2_event(uint8_t code, const void* payload, uint16_t len)
{
    assert(len == sizeof ev);
    events++; ev_code = code; memcpy(&ev, payload, sizeof ev);
}
static bool ufi_dump_active(void) { return false; }
static uint8_t ufi_v2_owner(void) { return 0; }
static void ufi_capture_abort(void) { unsafe++; }
static void ufi_write_abort(void) { unsafe++; }
static void ufi_drive_safe_state(void) { unsafe++; }
static bool ufi_stream_active(void) { return false; }
static write_state_t ufi_write_get_state(void) { return WRITE_IDLE; }
static drive_timing_t ufi_drive_get_timing(void) { return timing; }
static bool ufi_drive_motor_is_on(void) { return motor_on; }
static int ufi_drive_motor(bool on) { assert(!on); motor_offs++; motor_on = false; return 0; }
static void ufi_sync_service(void) {}
board_status_t ufi_board_status(void);
void ufi_board_power(uint8_t mask);
void ufi_board_soft_lock(void);
bool ufi_board_jumper_locked(void);
void ufi_board_write_enable(bool on);
void ufi_board_activity(void);
void ufi_board_service(void);
'''
    code += function('ufi_board.c', 'ufi_board_power') + function('ufi_board.c', 'sample_faults')
    code += function('ufi_board.c', 'measure_rails') + function('ufi_board.c', 'sample_rails')
    code += 'static bool session_lock;\n' + function('ufi_board.c', 'ufi_board_soft_lock')
    code += function('ufi_board.c', 'ufi_board_jumper_locked') + function('ufi_board.c', 'ufi_board_write_enable')
    code += function('ufi_board.c', 'ufi_board_status') + function('ufi_board.c', 'ufi_board_activity')
    code += function('ufi_board.c', 'ufi_board_service')
    code += r'''
/* both rails on, nothing drawn, no fault, counters cleared */
static void fresh(uint8_t r)
{
    rev = r; port.ODR = 0; port.IDR = IDLE_INPUTS; memset(adc, 0, sizeof adc);
    events = statuses = 0; led = false; fault_mask = 0; rail_ma[0] = rail_ma[1] = 0;
    ufi_board_power(0x03);
    assert(power_mask == 3 && (port.ODR & 3u) == 3u && !trip_mask);
}
static void samples(unsigned n) { while (n--) sample_rails(); }
/* the trip window documented in ufi_board.c and docs: 50 ms above 1.5 A */
_Static_assert(OVERCURRENT_SAMPLES * SAMPLE_MS == 50u && OVERCURRENT_MA == 1500u, "overcurrent window changed");

static void overcurrent(void)
{
    /* v0.x, 2 mV per mA: 3002 mV = 1501 mA is over the limit */
    fresh(7); adc[ADC_CH_I_FDD5] = 3002;
    samples(OVERCURRENT_SAMPLES - 1u);
    assert(power_mask == 3 && !trip_mask && !events);
    adc[ADC_CH_I_FDD5] = 3000; samples(1);                  /* one sample at the limit restarts the count */
    adc[ADC_CH_I_FDD5] = 3002; samples(OVERCURRENT_SAMPLES - 1u);
    assert(power_mask == 3 && !trip_mask && !events && !led);
    samples(1);                                             /* the fifth in a row trips this rail only */
    assert(power_mask == 2 && trip_mask == 1 && !(port.ODR & PIN_FDD5_EN.pin) && (port.ODR & PIN_FDD12_EN.pin));
    assert(led && events == 1 && ev_code == UFI_V2_EVT_POWER);
    assert(ev.power == 0x02 && (ev.flags & 0x06) == 0x02 && ev.i5_ma == 1501 && ev.i12_ma == 0);
    assert(statuses == events);                             /* one status per event: the recursion ended */
    samples(20); assert(events == 1 && power_mask == 2);    /* a tripped rail is not reported again */

    /* switching a rail on clears its own trip only */
    ufi_board_power(0x02); assert(trip_mask == 1 && power_mask == 2);
    ufi_board_power(0x01); assert(!trip_mask && power_mask == 1 && (port.ODR & 3u) == 1u);

    /* a host polling the status does not shorten the window (it did until 2026-11-14: every
       ufi_board_status() counted as a sample), and the report still carries a fresh current */
    fresh(7); adc[ADC_CH_I_FDD5] = 3002;
    for (unsigned i = 0; i < 100u; i++) assert(ufi_board_status().i5_ma == 1501);
    assert(power_mask == 3 && !trip_mask && !events && !oc_count[0]);
    samples(OVERCURRENT_SAMPLES - 1u);
    for (unsigned i = 0; i < 100u; i++) (void)ufi_board_status();
    assert(power_mask == 3 && !trip_mask && !events && oc_count[0] == OVERCURRENT_SAMPLES - 1u);
    samples(1); assert(power_mask == 2 && trip_mask == 1 && events == 1);
    /* nor does it see an eFuse fault first: that is the tick's job as well */
    fresh(10); port.IDR &= ~(uint32_t)PIN_FLT5.pin;
    (void)ufi_board_status(); assert(!fault_mask && !events);
    samples(1); assert(fault_mask == 1 && events == 1);

    /* exactly the limit never trips */
    fresh(7); adc[ADC_CH_I_FDD5] = 3000; adc[ADC_CH_I_FDD12] = 3001;
    samples(4u * OVERCURRENT_SAMPLES); assert(power_mask == 3 && !trip_mask && !events);
    assert(rail_ma[0] == OVERCURRENT_MA && rail_ma[1] == OVERCURRENT_MA);

    /* both rails over at once: both off in the same sample, one event each, both final */
    fresh(7); adc[ADC_CH_I_FDD5] = adc[ADC_CH_I_FDD12] = 3200;
    samples(OVERCURRENT_SAMPLES - 1u); assert(power_mask == 3 && !events);
    samples(1);
    assert(!power_mask && trip_mask == 3 && !(port.ODR & 3u) && events == 2 && statuses == 2);
    assert(ev.power == 0 && (ev.flags & 0x06) == 0x06);

    /* a rail that is off does not count, whatever the amplifier reads */
    fresh(7); ufi_board_power(0x01); adc[ADC_CH_I_FDD12] = 3300;
    samples(3u * OVERCURRENT_SAMPLES); assert(power_mask == 1 && !trip_mask && !events);

    /* v1.0, 1 mV per mA: 1500 mV is the limit, 1501 mV trips */
    fresh(10); adc[ADC_CH_I_FDD12] = 1500;
    samples(3u * OVERCURRENT_SAMPLES); assert(power_mask == 3 && rail_ma[1] == 1500);
    adc[ADC_CH_I_FDD12] = 1501; samples(OVERCURRENT_SAMPLES);
    assert(power_mask == 1 && trip_mask == 2 && events == 1 && ev.i12_ma == 1501 && (ev.flags & 0x06) == 0x04);
}

static void efuse(void)
{
    /* v0.x has no FLT line: the pin level is not looked at */
    fresh(7); port.IDR &= ~(uint32_t)PIN_FLT5.pin;
    samples(3); assert(!fault_mask && !events && !led);

    /* v1.0: FLT low while the rail is on = fault, reported once per change in power bits 2/3 */
    fresh(10); port.IDR &= ~(uint32_t)PIN_FLT12.pin;
    samples(1);
    assert(fault_mask == 2 && events == 1 && statuses == 1 && led && ev_code == UFI_V2_EVT_POWER && ev.power == 0x0B);
    assert(power_mask == 3 && (port.ODR & 3u) == 3u);        /* the rail stays enabled */
    samples(5); assert(events == 1);
    port.IDR |= PIN_FLT12.pin; samples(1);
    assert(!fault_mask && events == 2 && ev.power == 0x03);
    /* FLT is also low while the rail is disabled: not a fault */
    ufi_board_power(0x01); port.IDR &= ~(uint32_t)PIN_FLT12.pin;
    samples(2); assert(!fault_mask && events == 2);
    ufi_board_power(0x03); port.IDR &= ~(uint32_t)PIN_FLT5.pin;
    samples(1); assert(fault_mask == 3 && events == 3 && ev.power == 0x0F && statuses == 3);
}

static void status_and_lock(void)
{
    fresh(10); adc[ADC_CH_BOARD_ID] = 2970; adc[ADC_CH_I_FDD5] = 321; adc[ADC_CH_I_FDD12] = 45;
    board_status_t s = ufi_board_status();
    assert(s.power == 0x03 && s.flags == 0 && s.i5_ma == 321 && s.i12_ma == 45 && s.board_id_mv == 2970);
    rev = 7; s = ufi_board_status(); assert(s.i5_ma == 160 && s.i12_ma == 22);
    sd = true; safe_mode = true; port.IDR &= ~(uint32_t)(PIN_BTN_A.pin | PIN_BTN_B.pin);
    s = ufi_board_status(); assert(s.flags == (0x08 | 0x10 | 0x20 | 0x40));
    sd = false; safe_mode = false; port.IDR = IDLE_INPUTS;

    /* hardware lock: v1.0 reads the front switch line, v0.x the jumper on the WLOCK pin */
    rev = 10; port.IDR |= PIN_WLOCK.pin; assert(!ufi_board_jumper_locked());
    port.IDR |= PIN_WREN_SENSE.pin; assert(ufi_board_jumper_locked() && (ufi_board_status().flags & 0x01));
    rev = 7; assert(ufi_board_jumper_locked());
    port.IDR &= ~(uint32_t)PIN_WLOCK.pin; assert(!ufi_board_jumper_locked() && !(ufi_board_status().flags & 0x01));
    port.IDR = IDLE_INPUTS;

    /* v1.0: WLOCK_FW is an output, low only while writing; nothing is reconfigured */
    rev = 10; port.ODR = 0;
    ufi_board_write_enable(false); assert(port.ODR == PIN_WLOCK.pin);
    ufi_board_write_enable(true); assert(port.ODR == 0);
    ufi_board_write_enable(false); assert(port.ODR == PIN_WLOCK.pin && !inits);
    /* v0.x: the pin is the jumper input - made an input once, never driven */
    rev = 7; port.ODR = 0;
    ufi_board_write_enable(true);
    assert(inits == 1 && last_init.Pin == PIN_WLOCK.pin && last_init.Mode == GPIO_MODE_INPUT && port.ODR == 0);
    ufi_board_write_enable(false); ufi_board_write_enable(true);
    assert(inits == 1 && port.ODR == 0);

    ufi_board_soft_lock();
    assert(ufi_board_status().flags == 0x80);
}

static void service(void)
{
    fresh(7); timing.motor_off_s = 3; motor_on = true;
    now = 1000; ufi_board_activity();
    unsigned c = conversions;
    ufi_board_service(); assert(conversions == c + 2u);     /* first call samples both rails */
    now += SAMPLE_MS - 1u; ufi_board_service(); assert(conversions == c + 2u);
    now += 1u; ufi_board_service(); assert(conversions == c + 4u);

    /* idle motor: off after more than motor_off_s without activity */
    now = 4000; ufi_board_service(); assert(!motor_offs && motor_on);
    now = 4001; ufi_board_service(); assert(motor_offs == 1 && !motor_on);
    ufi_board_service(); assert(motor_offs == 1);           /* already off: not switched again */

    /* a running capture counts as activity */
    motor_on = true; ufi_board_activity(); g_capture.state = CAPTURE_RUNNING;
    now = 20000; ufi_board_service(); assert(motor_offs == 1);
    g_capture.state = CAPTURE_IDLE;
    now = 23000; ufi_board_service(); assert(motor_offs == 1);
    now = 23001; ufi_board_service(); assert(motor_offs == 2);

    /* motor_off_s 0: the timer is off */
    motor_on = true; timing.motor_off_s = 0;
    now += 4000000u; ufi_board_service(); assert(motor_offs == 2 && motor_on);
    assert(!unsafe && !usb_lost && usb_seen);               /* USB stayed configured throughout */
}

int main(void)
{
    overcurrent();
    efuse();
    status_and_lock();
    service();
    puts("board power: overcurrent trip, eFuse fault event, status flags, write enable per revision, motor idle-off");
    return 0;
}
'''
    run('regression_board_power', code)


if __name__ == '__main__':
    main()
