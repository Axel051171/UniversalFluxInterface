/**
 * UFI Flux Engine - operating mode: flux engine / USB floppy / SD drive (board v0.6)
 *
 * Optional 3-position toggle switch (ON-OFF-ON) on J13 (1x3, switch pin order; the same
 * inputs are on the expansion header J9 pins 5/6):
 * common = J13 pin 2 (3V3), one side = pin 1 (EXP_IO1, PE0), the other = pin 3
 * (EXP_IO2, PE1); the MCU pull-downs keep both low without a switch.
 *   middle (both low)  = flux engine (CDC, ufi host tool), with UFI.CFG protocol=gw the
 *                        Greaseweazle-compatible flux device (gw host tools, ufi_gw.c)
 *   EXP_IO1 high       = USB floppy (disk in drive A as a USB drive)
 *   EXP_IO2 high       = SD drive (dumps on the SD NAND)
 * The switch is read at start-up and debounced at run time; a change takes effect as soon
 * as no capture, write or dump is running.  Button B >= 2 s toggles the SD drive only
 * while the switch is in the middle.  After every change the ACT LED blinks the mode:
 * 1 = flux, 2 = USB floppy, 3 = SD drive, 4 = Greaseweazle flux.
 */

#include "ufi_firmware.h"

#define DEBOUNCE_MS     50u
#define BLINK_MS        200u

extern capture_context_t g_capture;

static uint8_t sw_mode, sw_raw;         /* debounced / last raw switch position */
static uint32_t sw_since, blink_t;
static bool pending;
static uint8_t blink_n;

static uint8_t read_switch(void)
{
    const bool io1 = HAL_GPIO_ReadPin(PIN_EXP_IO1.port, PIN_EXP_IO1.pin) == GPIO_PIN_SET;
    const bool io2 = HAL_GPIO_ReadPin(PIN_EXP_IO2.port, PIN_EXP_IO2.pin) == GPIO_PIN_SET;
    if (io1 && io2) {
        return 0xFF;                    /* not a valid position (wiring fault): ignore */
    }
    return io1 ? UFI_USB_FLOPPY : io2 ? UFI_USB_SD : ufi_usb_flux_mode();   /* middle: UFI or GW */
}

static bool busy(void)
{
    const capture_state_t cs = g_capture.state;
    return ufi_dump_active() || ufi_stream_active() || cs == CAPTURE_WAITING_INDEX ||
           cs == CAPTURE_RUNNING || ufi_write_get_state() != WRITE_IDLE;
}

static void show_mode(uint8_t mode)
{
    blink_n = (uint8_t)((mode + 1u) * 2u);
    blink_t = HAL_GetTick();
    led_set(&PIN_LED_ACT, false);
}

static void apply(uint8_t mode)
{
    if (ufi_usb_set_mode(mode) == UFI_OK) {
        show_mode(mode);
    }
}

void ufi_mode_init(void)
{
    sw_raw = sw_mode = read_switch();
    sw_since = HAL_GetTick();
    if (sw_mode != 0xFF && sw_mode != UFI_USB_FLUX) {   /* USB starts as UFI flux device */
        apply(sw_mode);
    }
}

void ufi_mode_button_b(void)
{
    if (sw_mode != ufi_usb_flux_mode() || busy()) {
        return;                         /* the switch decides */
    }
    apply(ufi_usb_get_mode() == UFI_USB_SD ? ufi_usb_flux_mode() : UFI_USB_SD);
}

void ufi_mode_service(void)
{
    const uint32_t now = HAL_GetTick();
    const uint8_t raw = read_switch();
    if (raw != sw_raw) {
        sw_raw = raw;
        sw_since = now;
    } else if (raw != sw_mode && raw != 0xFF && now - sw_since >= DEBOUNCE_MS) {
        sw_mode = raw;
        pending = true;
    }
    if (pending && !busy()) {
        pending = false;
        if (sw_mode != ufi_usb_get_mode()) {
            apply(sw_mode);
        }
    }
    if (blink_n && now - blink_t >= BLINK_MS) {
        blink_t = now;
        blink_n--;
        led_set(&PIN_LED_ACT, (blink_n & 1u) != 0);
    }
}
