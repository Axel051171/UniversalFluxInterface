"""Start-up order of ufi_init() (ufi_main.c) and ufi_board_init() (ufi_board.c) (P06).

Both functions extracted verbatim; every callee is a double that appends its name to a log.
The test states the order rules that matter, not the whole sequence:

- the reset cause is captured and the stack painted before anything else;
- the buttons are read after the pins are set up and have settled, and before the first thing
  that can hang (PSRAM start in ufi_flux_init, bench run of 2026-11-11);
- button A goes to the ROM bootloader, button B skips the PSRAM, keeps the drive supplies off
  and leaves the ERR LED on (it went off again until 2026-11-14);
- drive supplies: 5 V, 50 ms, then 12 V, and only after the drive pins and the write path are idle;
- transport state before USB, SD before UFI.CFG, UFI.CFG before LAN and the mode switch, the
  crash log after the SD is up;
- a failed PSRAM blinks ERR three times and ends dark.
"""
import re

from test_regressions import ERRORS, FW, typedef, function, run


def main():
    board = (FW / 'src/ufi_board.c').read_text(encoding='utf-8')
    stagger = re.search(r'^#define RAIL_STAGGER_MS\s+\w+', board, re.M).group() + '\n'
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'
    code += ERRORS + typedef(FW / 'include/ufi_firmware.h', 'capture_state_t') + stagger
    code += r'''
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int port; unsigned pin; } gpio_pin_t;
static const gpio_pin_t PIN_BTN_A = {1, 1u}, PIN_BTN_B = {1, 2u}, PIN_LED_ERR = {2, 1u};
static struct { capture_state_t state; } g_capture;
static struct { uint32_t CYCCNT, CTRL; } dwt;
#define DWT (&dwt)
#define DWT_CTRL_CYCCNTENA_Msk 1u
#define BOARD_HAS_PSRAM 1
static uint32_t SystemCoreClock = 550000000u;

static char log_[1024];
static void note(const char* s)
{
    const size_t at = strlen(log_), n = strlen(s);
    assert(at + n + 2u < sizeof log_);
    memcpy(log_ + at, s, n); log_[at + n] = 0x20; log_[at + n + 1u] = 0;
}
#define STEP(name) static void name(void) { note(#name); }
STEP(ufi_stack_paint) STEP(ufi_crash_capture_reset) STEP(HAL_Init) STEP(SystemClock_Config) STEP(board_gpio_init)
STEP(ufi_drive_init) STEP(ufi_iec_init) STEP(ufi_flux_init) STEP(ufi_write_init) STEP(ufi_clock_rtc_init)
STEP(ufi_v2_init) STEP(ufi_usb_init) STEP(ufi_config_load) STEP(ufi_lan_init) STEP(ufi_crash_boot)
STEP(ufi_mode_init) STEP(ufi_sense_init) STEP(ufi_sync_init)
static int ufi_sd_init(void) { note("ufi_sd_init"); return 0; }
#define UFI_DWT_INIT() (note("dwt_init"), dwt.CTRL = 1u)
#define UFI_WATCHDOG_INIT() note("watchdog_init")
#define __NOP() (dwt.CYCCNT++)
static bool cycles_ok;
static void ufi_cycles_init(uint32_t hz, bool counting) { assert(hz == 550000000u); cycles_ok = counting; note("ufi_cycles_init"); }

static unsigned buttons;                    /* bit0 A pressed, bit1 B pressed */
static uint32_t now, last_activity_ms;
static bool gpio_ready, bootloader, psram_skipped, psram_ok, led, safe_mode;
static unsigned led_ons;
static uint8_t rev, rails;
static uint32_t HAL_GetTick(void) { return now; }
static void HAL_Delay(uint32_t ms) { char t[24]; snprintf(t, sizeof t, "delay%u", (unsigned)ms); note(t); now += ms; }
static GPIO_PinState HAL_GPIO_ReadPin(int port, unsigned pin)
{
    assert(port == 1 && gpio_ready);        /* never before board_gpio_init() */
    note(pin == 1u ? "read_A" : "read_B");
    return (buttons & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}
static void ufi_request_bootloader(void) { note("bootloader"); bootloader = true; }
static void ufi_psram_skip(const char* why) { assert(strstr(why, "safe start")); note("psram_skip"); psram_skipped = true; }
static bool ufi_flux_store_is_psram(void) { return psram_ok && !psram_skipped; }
static void led_set(const gpio_pin_t* p, bool on) { assert(p == &PIN_LED_ERR); if (on && !led) led_ons++; led = on; note(on ? "err_on" : "err_off"); }
static uint8_t ufi_board_rev(void) { return rev; }
static void ufi_board_power(uint8_t mask) { char t[24]; snprintf(t, sizeof t, "power%u", mask); note(t); rails = mask; }
void ufi_board_init(bool power_on);
void ufi_init(void);
'''
    code += function('ufi_board.c', 'ufi_board_init')
    init = function('ufi_main.c', 'ufi_init')
    assert init.count('    board_gpio_init();') == 1
    code += init.replace('    board_gpio_init();', '    board_gpio_init(); gpio_ready = true;')
    code += r'''
static void start(unsigned pressed, uint8_t board, bool psram)
{
    log_[0] = 0; buttons = pressed; rev = board; psram_ok = psram; now = 0; rails = 0xFF; led = false; led_ons = 0;
    gpio_ready = bootloader = psram_skipped = false; g_capture.state = CAPTURE_ERROR; dwt.CYCCNT = dwt.CTRL = 0;
    ufi_init();
    assert(g_capture.state == CAPTURE_IDLE && cycles_ok && last_activity_ms <= now);
}
static const char* at(const char* step) { const char* p = strstr(log_, step); if (!p) { printf("missing: %s\n%s\n", step, log_); assert(p); } return p; }
static bool before(const char* a, const char* b) { return at(a) < at(b); }
static bool has(const char* step) { return strstr(log_, step) != NULL; }
static unsigned count(const char* step) { unsigned n = 0; for (const char* p = log_; (p = strstr(p, step)) != NULL; p += strlen(step)) n++; return n; }

int main(void)
{
    /* ---- normal start of a v1.0 board ---- */
    start(0, 10, true);
    assert(!strncmp(log_, "ufi_stack_paint ufi_crash_capture_reset HAL_Init SystemClock_Config ", 68));
    assert(before("SystemClock_Config", "dwt_init") && before("dwt_init", "ufi_cycles_init") && before("ufi_cycles_init", "watchdog_init"));
    assert(before("watchdog_init", "board_gpio_init"));
    /* buttons: after the pins and 5 ms, before the PSRAM start and before USB */
    assert(before("board_gpio_init", "delay5 ") && before("delay5 ", "read_A") && before("read_A", "read_B"));
    assert(before("read_B", "ufi_drive_init") && before("read_B", "ufi_flux_init") && before("read_B", "ufi_usb_init"));
    assert(!bootloader && !psram_skipped && !safe_mode);
    /* supplies: drive pins, IEC, flux and write path idle first; 5 V, 50 ms, 12 V */
    assert(before("ufi_drive_init", "power1") && before("ufi_iec_init", "power1") && before("ufi_flux_init", "power1") && before("ufi_write_init", "power1"));
    assert(has("power1 delay50 power3 ") && rails == 3 && RAIL_STAGGER_MS == 50u && count("power") == 2);
    assert(before("ufi_sense_init", "power1") && before("ufi_sync_init", "power1"));
    /* transports and storage */
    assert(before("power3", "ufi_v2_init") && before("ufi_v2_init", "ufi_usb_init"));
    assert(before("ufi_sd_init", "ufi_config_load") && before("ufi_config_load", "ufi_lan_init") && before("ufi_config_load", "ufi_mode_init"));
    assert(before("ufi_sd_init", "ufi_crash_boot") && before("ufi_usb_init", "ufi_mode_init"));
    assert(has("ufi_clock_rtc_init") && before("power3", "ufi_clock_rtc_init"));
    assert(!has("err_on") && !led && !strcmp(strrchr(log_, 'u'), "ufi_mode_init "));     /* nothing after the mode switch */
    /* every step exactly once */
    static const char* const once[] = {"ufi_stack_paint", "ufi_crash_capture_reset", "HAL_Init", "SystemClock_Config", "watchdog_init",
        "board_gpio_init", "ufi_drive_init", "ufi_iec_init", "ufi_flux_init", "ufi_write_init", "ufi_v2_init", "ufi_usb_init", "ufi_sd_init",
        "ufi_config_load", "ufi_lan_init", "ufi_crash_boot", "ufi_mode_init", "ufi_sense_init", "ufi_sync_init", "read_A", "read_B"};
    for (unsigned i = 0; i < sizeof once / sizeof once[0]; i++) assert(count(once[i]) == 1);

    /* ---- older boards have no calendar crystal ---- */
    start(0, 7, true); assert(!has("ufi_clock_rtc_init") && has("power1 delay50 power3 "));
    start(0, 9, true); assert(!has("ufi_clock_rtc_init"));

    /* ---- button A: ROM bootloader, asked for before the PSRAM, the supplies and USB ---- */
    start(1, 10, true);
    assert(bootloader && before("bootloader", "ufi_flux_init") && before("bootloader", "power") && before("bootloader", "ufi_usb_init"));
    assert(before("read_A", "bootloader") && before("delay5 ", "bootloader"));

    /* ---- button B: safe start ---- */
    start(2, 10, true);
    assert(psram_skipped && before("psram_skip", "ufi_flux_init") && before("read_B", "psram_skip"));
    assert(has("power0 ") && !has("power1") && !has("power3") && !has("delay50") && rails == 0 && safe_mode);
    assert(led && led_ons == 1 && !has("err_off") && !has("delay150"));          /* ERR stays on, no "PSRAM failed" blinking */
    assert(before("power0", "err_on") && has("ufi_usb_init") && has("ufi_mode_init"));     /* the device still starts completely */
    start(2, 7, false); assert(led && rails == 0 && !has("delay150"));

    /* ---- PSRAM fitted but not usable: three ERR blinks of 150 ms, dark afterwards ---- */
    start(0, 10, false);
    assert(count("err_on delay150 err_off delay150 ") == 3 && led_ons == 3 && !led && rails == 3);
    assert(before("ufi_mode_init", "err_on") && !psram_skipped);
    puts("start-up: order rules, buttons before the PSRAM, bootloader, safe start, supply stagger, PSRAM blink code");
    return 0;
}
'''
    run('regression_startup', code)


if __name__ == '__main__':
    main()
