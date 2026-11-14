"""Operating mode switch of ufi_mode.c (P06): toggle switch, button B, ACT LED blink code.

Production functions, extracted verbatim: read_switch, busy, show_mode, apply, ufi_mode_init,
ufi_mode_button_b and ufi_mode_service.  The doubles model the two switch inputs, the tick,
the busy sources and ufi_usb_set_mode() (which can refuse).

Covered: the three switch positions and the invalid fourth, UFI.CFG protocol in the middle
position, start-up with the switch set, debounce (a bounce restarts the time), a change waits
while a capture / stream / write / dump runs, a refused change is retried once per second,
button B only in the middle position and never while busy, the blink count per mode.
"""
import re

from test_regressions import BASE, ERRORS, FW, typedef, function, run


def main():
    header = (FW / 'include/ufi_firmware.h').read_text(encoding='utf-8')
    modes = re.search(r'^enum \{ UFI_USB_FLUX = 0,[^}]*\};', header, re.M).group()
    source = (FW / 'src/ufi_mode.c').read_text(encoding='utf-8')
    defines = ''.join(re.search(r'^#define ' + n + r'\s+\w+', source, re.M).group() + '\n' for n in ('DEBOUNCE_MS', 'BLINK_MS'))
    code = BASE + ERRORS + modes + '\n' + defines
    code += typedef(FW / 'include/ufi_firmware.h', 'capture_state_t')
    code += typedef(FW / 'include/ufi_firmware.h', 'write_state_t')
    code += r'''
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int port; unsigned pin; } gpio_pin_t;
static const gpio_pin_t PIN_EXP_IO1 = {1, 1u}, PIN_EXP_IO2 = {1, 2u}, PIN_LED_ACT = {2, 1u};
static struct { capture_state_t state; } g_capture;
static uint8_t sw_mode, sw_raw;
static uint32_t sw_since, blink_t;
static uint32_t retry_at;
static bool pending;
static uint8_t blink_n;

static uint32_t now;
static unsigned inputs;                     /* bit0 EXP_IO1, bit1 EXP_IO2 */
static uint8_t usb_mode, cfg_mode, last_request;
static int set_result;
static unsigned requests, led_on, led_changes;
static bool led, dump, stream;
static write_state_t wstate;
static uint32_t HAL_GetTick(void) { return now; }
static GPIO_PinState HAL_GPIO_ReadPin(int port, unsigned pin) { assert(port == 1); return (inputs & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET; }
static void led_set(const gpio_pin_t* p, bool on)
{
    assert(p == &PIN_LED_ACT);
    if (on != led) led_changes++;
    if (on && !led) led_on++;
    led = on;
}
static uint8_t ufi_usb_flux_mode(void) { return cfg_mode; }
static uint8_t ufi_usb_get_mode(void) { return usb_mode; }
static int ufi_usb_set_mode(uint8_t mode)
{
    requests++; last_request = mode;
    if (set_result == UFI_OK) usb_mode = mode;
    return set_result;
}
static bool ufi_dump_active(void) { return dump; }
static bool ufi_stream_active(void) { return stream; }
static write_state_t ufi_write_get_state(void) { return wstate; }
void ufi_mode_init(void);
void ufi_mode_button_b(void);
void ufi_mode_service(void);
'''
    for name in ('read_switch', 'busy', 'show_mode', 'apply', 'ufi_mode_init', 'ufi_mode_button_b', 'ufi_mode_service'):
        code += function('ufi_mode.c', name)
    code += r'''
enum { MIDDLE = 0, FLOPPY_POS = 1, SD_POS = 2, BOTH = 3 };

static void fresh(unsigned position, uint8_t cfg)
{
    now = 1000; inputs = position; cfg_mode = cfg; usb_mode = UFI_USB_FLUX; set_result = UFI_OK;
    requests = led_on = led_changes = 0; led = false; dump = stream = false; wstate = WRITE_IDLE;
    g_capture.state = CAPTURE_IDLE; pending = false; blink_n = 0; retry_at = 0;
    ufi_mode_init();
}
static void run_ms(uint32_t ms) { while (ms--) { now++; ufi_mode_service(); } }
/* blinks shown for the mode just applied */
static unsigned blinks(void) { led_on = 0; run_ms(20u * BLINK_MS); assert(!led && !blink_n); return led_on; }

int main(void)
{
    /* start-up: the USB device starts as the UFI flux device, so the middle position asks for nothing */
    fresh(MIDDLE, UFI_USB_FLUX); assert(!requests && sw_mode == UFI_USB_FLUX && !pending);
    fresh(FLOPPY_POS, UFI_USB_FLUX); assert(requests == 1 && usb_mode == UFI_USB_FLOPPY && !pending && blinks() == 3);
    fresh(SD_POS, UFI_USB_FLUX); assert(requests == 1 && usb_mode == UFI_USB_SD && blinks() == 2);
    fresh(MIDDLE, UFI_USB_GW); assert(requests == 1 && usb_mode == UFI_USB_GW && blinks() == 4);
    fresh(MIDDLE, UFI_USB_COMPOSITE); assert(usb_mode == UFI_USB_COMPOSITE && blinks() == 5);
    /* both inputs high is a wiring fault: nothing is requested, now or later */
    fresh(BOTH, UFI_USB_FLUX); assert(!requests && sw_mode == 0xFF);
    run_ms(5000); assert(!requests && usb_mode == UFI_USB_FLUX);
    inputs = SD_POS; run_ms(DEBOUNCE_MS + 2u); assert(requests == 1 && usb_mode == UFI_USB_SD);
    inputs = BOTH; run_ms(5000); assert(requests == 1 && usb_mode == UFI_USB_SD && sw_mode == UFI_USB_SD);

    /* a start-up change that is refused is retried after one second, then once per second */
    set_result = UFI_ERR_BUSY; now = 1000; inputs = SD_POS; cfg_mode = UFI_USB_FLUX; usb_mode = UFI_USB_FLUX;
    requests = 0; pending = false; ufi_mode_init();
    assert(requests == 1 && pending && usb_mode == UFI_USB_FLUX);
    run_ms(999); assert(requests == 1);
    run_ms(1); assert(requests == 2);
    run_ms(1000); assert(requests == 3 && pending);
    set_result = UFI_OK; run_ms(1000); assert(requests == 4 && !pending && usb_mode == UFI_USB_SD);
    run_ms(5000); assert(requests == 4);

    /* debounce: the new position must be stable for DEBOUNCE_MS; a bounce restarts the time */
    fresh(MIDDLE, UFI_USB_FLUX);
    inputs = FLOPPY_POS; run_ms(DEBOUNCE_MS - 1u); assert(!requests);
    inputs = MIDDLE; run_ms(1); inputs = FLOPPY_POS; run_ms(DEBOUNCE_MS); assert(!requests);
    run_ms(1); assert(requests == 1 && last_request == UFI_USB_FLOPPY && usb_mode == UFI_USB_FLOPPY);
    assert(blinks() == 3);
    /* straight from one side to the other */
    inputs = SD_POS; run_ms(DEBOUNCE_MS + 1u); assert(requests == 2 && usb_mode == UFI_USB_SD);
    inputs = MIDDLE; run_ms(DEBOUNCE_MS + 1u); assert(requests == 3 && usb_mode == UFI_USB_FLUX && blinks() == 1);

    /* a change waits for the end of the running work, each source on its own */
    for (int source = 0; source < 5; source++) {
        fresh(MIDDLE, UFI_USB_FLUX);
        if (source == 0) dump = true;
        if (source == 1) stream = true;
        if (source == 2) g_capture.state = CAPTURE_WAITING_INDEX;
        if (source == 3) g_capture.state = CAPTURE_RUNNING;
        if (source == 4) wstate = WRITE_ACTIVE;
        inputs = SD_POS; run_ms(3000); assert(!requests && pending && usb_mode == UFI_USB_FLUX);
        ufi_mode_button_b(); assert(!requests);                 /* the switch is off the middle: no button */
        dump = stream = false; g_capture.state = CAPTURE_IDLE; wstate = WRITE_IDLE;
        run_ms(1); assert(requests == 1 && usb_mode == UFI_USB_SD && !pending);
    }
    /* button B in the middle position does nothing while work is running */
    fresh(MIDDLE, UFI_USB_FLUX); stream = true;
    ufi_mode_button_b(); assert(!requests && usb_mode == UFI_USB_FLUX);
    stream = false; ufi_mode_button_b(); assert(requests == 1 && usb_mode == UFI_USB_SD);
    /* a finished capture that is still held does not block */
    fresh(MIDDLE, UFI_USB_FLUX); g_capture.state = CAPTURE_COMPLETE;
    inputs = SD_POS; run_ms(DEBOUNCE_MS + 1u); assert(requests == 1);

    /* button B toggles the SD drive in the middle position only, back to the configured flux protocol */
    fresh(MIDDLE, UFI_USB_FLUX);
    ufi_mode_button_b(); assert(requests == 1 && usb_mode == UFI_USB_SD && blinks() == 2);
    ufi_mode_button_b(); assert(requests == 2 && usb_mode == UFI_USB_FLUX && blinks() == 1);
    fresh(MIDDLE, UFI_USB_GW);
    ufi_mode_button_b(); assert(usb_mode == UFI_USB_SD);
    ufi_mode_button_b(); assert(usb_mode == UFI_USB_GW);
    fresh(FLOPPY_POS, UFI_USB_FLUX); requests = 0;
    ufi_mode_button_b(); assert(!requests && usb_mode == UFI_USB_FLOPPY);       /* the switch decides */
    /* a refused button change shows no blink code and is not retried */
    fresh(MIDDLE, UFI_USB_FLUX); set_result = UFI_ERR_STORAGE;
    ufi_mode_button_b(); assert(requests == 1 && usb_mode == UFI_USB_FLUX && !blink_n && !pending);
    run_ms(3000); assert(requests == 1);

    /* the tick counter may wrap between a refusal and its retry */
    fresh(MIDDLE, UFI_USB_FLUX); now = 0xFFFFFF00u; sw_since = now; set_result = UFI_ERR_BUSY;
    inputs = SD_POS; run_ms(DEBOUNCE_MS + 1u); assert(requests == 1 && pending);
    set_result = UFI_OK; run_ms(999); assert(requests == 1);
    run_ms(2); assert(requests == 2 && usb_mode == UFI_USB_SD && now < 0x1000u);
    puts("mode switch: positions, start-up, debounce, busy wait, retry, button B, blink codes");
    return 0;
}
'''
    run('regression_mode', code)


if __name__ == '__main__':
    main()
