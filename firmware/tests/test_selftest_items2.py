"""Single SELFTEST items of ufi_selftest.c, second half (P06): fdd_lines, iec_loop, index_loop,
stack_free, rtc, sht40, sync, v_fdd5 / v_fdd12, wgate_lock, fdd_pins, psram_dma, last_reset.

Production functions, extracted verbatim, against doubles for pins, sensors and drivers.  The
write lock item runs against a model of the gate: the WGATE bus follows the firmware's WGATE
output only while the firmware write enable is open, unless the model is told to leak.

Covered: every status rule at its boundary, every skip reason, what an item restores (index
simulator, SYNC capture) and, for the write lock item, that WGATE is never driven before the
lock is closed, is released again on every path and the lock stays closed afterwards.
"""
from test_regressions import ERRORS, FW, typedef, function, run


def main():
    header = FW / 'include/ufi_firmware.h'
    source = (FW / 'src/ufi_selftest.c').read_text(encoding='utf-8')
    start, end = source.index('enum {\n    ST_OK = 0,'), source.index('} st_item_t;\n') + len('} st_item_t;\n')
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'
    code += '#include "ufi_sense.h"\n#include "ufi_psram.h"\n' + ERRORS
    for name in ('drive_type_t', 'capture_state_t', 'write_state_t'):
        code += typedef(header, name)
    code += source[start:end]
    code += r'''
#define INDEX_SIM_PIN 0x01u
typedef enum { GPIO_PIN_RESET = 0, GPIO_PIN_SET } GPIO_PinState;
typedef struct { int port; unsigned pin; } gpio_pin_t;
static const gpio_pin_t PIN_FDD_INDEX = {1, 1u}, PIN_FDD_TRACK0 = {1, 2u}, PIN_FDD_WPROT = {1, 4u}, PIN_FDD_DKCHG = {1, 8u},
                        PIN_FDD_READY = {1, 16u}, PIN_FDD_RDATA = {1, 32u}, PIN_FDD_WGATE = {2, 1u}, PIN_WGATE_SENSE = {3, 1u};
static struct { capture_state_t state; } g_capture;

static uint32_t now;
static unsigned low_pins;                   /* drive bus inputs that read low (bit order of the pin constants) */
static drive_type_t current;
static uint32_t HAL_GetTick(void) { return ++now; }
static void HAL_Delay(uint32_t ms) { now += ms; }
static uint8_t rev;
static uint8_t ufi_board_rev(void) { return rev; }
static drive_type_t ufi_drive_get_current(void) { return current; }
static write_state_t wstate;
static write_state_t ufi_write_get_state(void) { return wstate; }

/* index simulator: the pulse arrives on INDEX after wire_ms, if there is a wire */
static uint16_t sim_rpm; static uint8_t sim_mode; static int sim_result; static unsigned sim_sets;
static bool wire; static uint32_t wire_ms, sim_on_at; static bool sim_on;
static uint16_t ufi_index_sim_rpm(void) { return sim_rpm; }
static uint8_t ufi_index_sim_mode(void) { return sim_mode; }
static int ufi_index_sim_set(uint16_t rpm, uint16_t pulse_us, uint8_t mode)
{
    sim_sets++;
    if (sim_result) return sim_result;
    sim_rpm = rpm; sim_mode = mode; sim_on = pulse_us != 0; sim_on_at = now;
    return 0;
}
static GPIO_PinState HAL_GPIO_ReadPin(int port, unsigned pin)
{
    assert(port == 1);
    if (pin == PIN_FDD_INDEX.pin && sim_on && wire && now - sim_on_at >= wire_ms) return GPIO_PIN_RESET;
    return (low_pins & pin) ? GPIO_PIN_RESET : GPIO_PIN_SET;
}

static uint8_t iec; static uint8_t ufi_iec_loopback(void) { return iec; }
static uint32_t stack_free; static uint32_t ufi_stack_free(void) { return stack_free; }
static uint8_t crash; static uint8_t ufi_crash_kind(void) { return crash; }
static ufi_rtc_time_t rtc;
bool ufi_sense_rtc_get(ufi_rtc_time_t* t) { *t = rtc; return rtc.ready; }
uint32_t ufi_calendar_yyyymmdd(uint16_t year, uint8_t mon, uint8_t day) { return year * 10000u + mon * 100u + day; }
static ufi_sht40_t sht;
int ufi_sense_sht40(ufi_sht40_t* r) { *r = sht; return 0; }
static ufi_rails_t rails;
void ufi_sense_rails(ufi_rails_t* r) { *r = rails; }
static uint8_t pins_mask;
uint8_t ufi_sense_fdd_pins(void) { return pins_mask; }

/* SYNC capture: counts sync_rate pulses per 250 ms while enabled */
static ufi_sync_status_t sync_state; static int sync_result; static uint32_t sync_rate, sync_since; static unsigned sync_calls;
void ufi_sync_status(ufi_sync_status_t* s)
{
    if (sync_state.active) { sync_state.count += (now - sync_since) / 250u * sync_rate; sync_since = now; }
    *s = sync_state;
}
int ufi_sync_enable(bool on)
{
    sync_calls++;
    if (sync_result) return sync_result;
    if (on && !sync_state.active) sync_since = now;
    sync_state.active = on;
    return 0;
}

/* write lock: the gate of the board.  log = order of what the firmware did */
static bool enable_open, wgate_out, leak, stuck;
static char log_[64]; static unsigned log_n;
static void note(char c) { assert(log_n < sizeof log_ - 1u); log_[log_n++] = c; log_[log_n] = 0; }
static void ufi_board_write_enable(bool on) { enable_open = on; note(on ? 'E' : 'e'); }
static void bus_out(const gpio_pin_t* p, bool assert_) { assert(p == &PIN_FDD_WGATE); wgate_out = assert_; note(assert_ ? 'W' : 'w'); }
static bool bus_in(const gpio_pin_t* p)
{
    assert(p == &PIN_WGATE_SENSE);
    note('r');
    return stuck || (wgate_out && (enable_open || leak));
}
static bool is_psram; static uint32_t dma_result; static unsigned dma_runs;
static bool ufi_flux_store_is_psram(void) { return is_psram; }
static uint32_t ufi_flux_dma_selftest(void) { dma_runs++; return dma_result; }
'''
    for name in ('t_fdd_lines', 't_iec_loop', 't_index_loop', 't_stack', 't_rtc', 't_sht40', 't_sync', 't_vrail',
                 't_wgate_lock', 't_fdd_pins', 't_psram_dma', 't_last_reset'):
        code += function('ufi_selftest.c', name)
    code += r'''
#define RUN(fn, ...) (it = (st_item_t){.status = 0xEE, .value = 0xEEEEEEEEu}, fn(&it, ##__VA_ARGS__), it)
static void lock_case(bool leaking, bool is_stuck) { enable_open = true; wgate_out = false; leak = leaking; stuck = is_stuck; log_n = 0; log_[0] = 0; }

int main(void)
{
    st_item_t it;
    /* ---- fdd_lines: inputs with nothing selected; any low line is a warning with its bit ---- */
    current = DRIVE_NONE; low_pins = 0; RUN(t_fdd_lines); assert(it.status == ST_OK && it.value == 0);
    for (unsigned bit = 0; bit < 6u; bit++) { low_pins = 1u << bit; RUN(t_fdd_lines); assert(it.status == ST_WARN && it.value == (1u << bit)); }
    low_pins = 0x3F; RUN(t_fdd_lines); assert(it.status == ST_WARN && it.value == 0x3F);
    current = (drive_type_t)1; RUN(t_fdd_lines); assert(it.status == ST_SKIP && it.value == 0xEEEEEEEEu);     /* a drive is selected */

    /* ---- iec_loop: low nibble = line not asserted (fail), high nibble = not released (warn) ---- */
    iec = 0; RUN(t_iec_loop); assert(it.status == ST_OK && it.value == 0);
    for (unsigned bit = 0; bit < 8u; bit++) {
        iec = (uint8_t)(1u << bit); RUN(t_iec_loop); assert(it.status == (bit < 4u ? ST_FAIL : ST_WARN) && it.value == iec);
    }
    iec = 0x11; RUN(t_iec_loop); assert(it.status == ST_FAIL);          /* the failure wins */

    /* ---- index_loop: simulator pulse must show on INDEX within 250 ms; the setting is restored ---- */
    current = DRIVE_NONE; low_pins = 0; sim_result = 0; sim_rpm = 360; sim_mode = 0x05; sim_sets = 0; wire = true; wire_ms = 200; now = 1000;
    RUN(t_index_loop);
    assert(it.status == ST_OK && it.value == 1 && sim_sets == 2 && sim_rpm == 360 && sim_mode == 0x05 && !sim_on);
    wire_ms = 249; sim_sets = 0; RUN(t_index_loop); assert(it.status == ST_OK && sim_rpm == 360);
    wire_ms = 251; RUN(t_index_loop); assert(it.status == ST_SKIP && it.value == 0 && sim_rpm == 360 && sim_mode == 0x05 && !sim_on);
    wire = false; sim_rpm = 0; sim_mode = 0; RUN(t_index_loop); assert(it.status == ST_SKIP && it.value == 0 && sim_rpm == 0 && !sim_on);
    wire = true; wire_ms = 0; sim_result = UFI_ERR_BUSY; sim_sets = 0; RUN(t_index_loop);
    assert(it.status == ST_SKIP && sim_sets == 1 && it.value == 0xEEEEEEEEu);     /* pin in use: nothing changed, nothing to restore */
    sim_result = 0; current = (drive_type_t)2; sim_sets = 0; RUN(t_index_loop); assert(it.status == ST_SKIP && !sim_sets);

    /* ---- stack_free: fail below 512 bytes, warn below 2048 ---- */
    stack_free = 0; RUN(t_stack); assert(it.status == ST_FAIL && it.value == 0);
    stack_free = 511; RUN(t_stack); assert(it.status == ST_FAIL);
    stack_free = 512; RUN(t_stack); assert(it.status == ST_WARN && it.value == 512);
    stack_free = 2047; RUN(t_stack); assert(it.status == ST_WARN);
    stack_free = 2048; RUN(t_stack); assert(it.status == ST_OK && it.value == 2048);

    /* ---- rtc: a missing calendar fails on v1.0 only ---- */
    rtc = (ufi_rtc_time_t){.ready = true, .year = 2026, .mon = 11, .day = 14}; rev = 10;
    RUN(t_rtc); assert(it.status == ST_OK && it.value == 20261114u);
    rtc.ready = false; RUN(t_rtc); assert(it.status == ST_FAIL && it.value == 0xEEEEEEEEu);
    rev = 7; RUN(t_rtc); assert(it.status == ST_SKIP);
    rev = 9; RUN(t_rtc); assert(it.status == ST_SKIP);

    /* ---- sht40: value = temperature << 16 | humidity; no answer fails on v1.0 only ---- */
    rev = 10; sht = (ufi_sht40_t){.status = SHT40_OK, .temp_x100 = 2345, .rh_x100 = 5012};
    RUN(t_sht40); assert(it.status == ST_OK && it.value == ((2345u << 16) | 5012u));
    sht.temp_x100 = -1050; RUN(t_sht40); assert(it.status == ST_OK && (int16_t)(it.value >> 16) == -1050 && (it.value & 0xFFFFu) == 5012u);
    for (uint8_t why = SHT40_NO_SENSOR; why <= SHT40_NO_BUS; why++) {
        sht.status = why; rev = 10; RUN(t_sht40); assert(it.status == ST_FAIL && it.value == why);
        rev = 7; RUN(t_sht40); assert(it.status == ST_SKIP && it.value == why);
    }

    /* ---- sync: pulses in 250 ms; the capture is switched off again unless it was running ---- */
    now = 5000; sync_result = 0; sync_rate = 1; sync_state = (ufi_sync_status_t){0}; sync_calls = 0;
    RUN(t_sync); assert(it.status == ST_OK && it.value == 1 && !sync_state.active && sync_calls == 2);
    sync_rate = 0; RUN(t_sync); assert(it.status == ST_SKIP && it.value == 0 && !sync_state.active);     /* no sensor: nothing to judge */
    sync_rate = 3; sync_state.active = 1; sync_state.count = 1000; sync_since = now; sync_calls = 0;
    RUN(t_sync); assert(it.status == ST_OK && it.value == 3 && sync_state.active && sync_calls == 1);  /* index source: stays on */
    sync_state = (ufi_sync_status_t){0}; sync_result = UFI_ERR_NOT_IMPL; RUN(t_sync); assert(it.status == ST_SKIP && it.value == 0xEEEEEEEEu);
    sync_result = 0;

    /* ---- v_fdd5 / v_fdd12: fail when low while on, skip when off or without dividers ---- */
    rails = (ufi_rails_t){.v5_mv = 5020, .v12_mv = 11900, .flags = RAIL_FLAG_FITTED | RAIL_FLAG_5V_ON | RAIL_FLAG_12V_ON};
    RUN(t_vrail, 0); assert(it.status == ST_OK && it.value == 5020);
    RUN(t_vrail, 1); assert(it.status == ST_OK && it.value == 11900);
    rails.flags |= RAIL_FLAG_5V_LOW; RUN(t_vrail, 0); assert(it.status == ST_FAIL); RUN(t_vrail, 1); assert(it.status == ST_OK);
    rails.flags = RAIL_FLAG_FITTED | RAIL_FLAG_5V_ON | RAIL_FLAG_12V_ON | RAIL_FLAG_12V_LOW;
    RUN(t_vrail, 1); assert(it.status == ST_FAIL); RUN(t_vrail, 0); assert(it.status == ST_OK);
    rails.flags = RAIL_FLAG_FITTED | RAIL_FLAG_5V_ON | RAIL_FLAG_12V_LOW;       /* 12 V off: its low flag does not count */
    RUN(t_vrail, 1); assert(it.status == ST_SKIP && it.value == 11900); RUN(t_vrail, 0); assert(it.status == ST_OK);
    rails.flags = RAIL_FLAG_FITTED | RAIL_FLAG_12V_ON | RAIL_FLAG_5V_LOW; RUN(t_vrail, 0); assert(it.status == ST_SKIP);
    rails.flags = RAIL_FLAG_5V_ON | RAIL_FLAG_12V_ON | RAIL_FLAG_5V_LOW | RAIL_FLAG_12V_LOW;          /* v0.x: no dividers */
    RUN(t_vrail, 0); assert(it.status == ST_SKIP); RUN(t_vrail, 1); assert(it.status == ST_SKIP);

    /* ---- wgate_lock: with the firmware lock closed an asserted WGATE must not reach the bus ---- */
    rev = 10; current = DRIVE_NONE; wstate = WRITE_IDLE;
    lock_case(false, false); RUN(t_wgate_lock);
    assert(it.status == ST_OK && it.value == 0 && !strcmp(log_, "erWrw"));       /* lock, look, assert, look, release */
    assert(!enable_open && !wgate_out);
    lock_case(true, false); RUN(t_wgate_lock);                                  /* the gate lets it through */
    assert(it.status == ST_FAIL && it.value == 1 && !strcmp(log_, "erWrw") && !enable_open && !wgate_out);
    lock_case(false, true); RUN(t_wgate_lock);                                  /* bus asserted before the test: WGATE is not touched */
    assert(it.status == ST_FAIL && it.value == 2 && !strcmp(log_, "er") && !enable_open && !wgate_out);
    /* not on older boards, not with a drive selected, not while a write is prepared or runs: nothing is driven */
    lock_case(true, false); rev = 9; RUN(t_wgate_lock); assert(it.status == ST_SKIP && !log_n && enable_open);
    rev = 10; current = (drive_type_t)1; RUN(t_wgate_lock); assert(it.status == ST_SKIP && !log_n);
    current = DRIVE_NONE;
    for (int w = WRITE_IDLE + 1; w <= WRITE_ERROR; w++) { wstate = (write_state_t)w; RUN(t_wgate_lock); assert(it.status == ST_SKIP && !log_n); }
    wstate = WRITE_IDLE;

    /* ---- fdd_pins: informational; WGATE seen on the bus or an eFuse fault warns ---- */
    rev = 10; pins_mask = 0; RUN(t_fdd_pins); assert(it.status == ST_OK && it.value == 0);
    for (unsigned bit = 0; bit < 8u; bit++) {
        pins_mask = (uint8_t)(1u << bit); RUN(t_fdd_pins);
        const bool warn = pins_mask == FDD_PIN_WGATE_SENSE || pins_mask == FDD_PIN_FLT5 || pins_mask == FDD_PIN_FLT12;
        assert(it.status == (warn ? ST_WARN : ST_OK) && it.value == pins_mask);
    }
    rev = 9; pins_mask = 0xFF; RUN(t_fdd_pins); assert(it.status == ST_SKIP && it.value == 0xEEEEEEEEu);

    /* ---- psram_dma: 0 ok, "no request served" warns with value 0, anything else fails with the code ---- */
    is_psram = true; g_capture.state = CAPTURE_IDLE; wstate = WRITE_IDLE; dma_runs = 0;
    dma_result = 0; RUN(t_psram_dma); assert(it.status == ST_OK && it.value == 0 && dma_runs == 1);
    dma_result = UFI_DMA_TEST_NOT_RUN; RUN(t_psram_dma); assert(it.status == ST_WARN && it.value == 0);
    dma_result = 0x0203; RUN(t_psram_dma); assert(it.status == ST_FAIL && it.value == 0x0203);
    dma_result = 1; RUN(t_psram_dma); assert(it.status == ST_FAIL && it.value == 1);
    dma_runs = 0;                                                           /* the test destroys the store: never while it is in use */
    is_psram = false; RUN(t_psram_dma); assert(it.status == ST_SKIP);
    is_psram = true; g_capture.state = CAPTURE_WAITING_INDEX; RUN(t_psram_dma); assert(it.status == ST_SKIP);
    g_capture.state = CAPTURE_RUNNING; RUN(t_psram_dma); assert(it.status == ST_SKIP);
    g_capture.state = CAPTURE_IDLE; wstate = WRITE_ACTIVE; RUN(t_psram_dma); assert(it.status == ST_SKIP && !dma_runs);
    wstate = WRITE_IDLE;

    /* ---- last_reset: a fault record from the last run warns ---- */
    crash = 0; RUN(t_last_reset); assert(it.status == ST_OK && it.value == 0);
    crash = 3; RUN(t_last_reset); assert(it.status == ST_WARN && it.value == 3);
    puts("selftest items 2: bus lines, loops, stack, calendar, sensor, sync, rail voltages, write lock, read-backs, dma, last reset");
    return 0;
}
'''
    run('regression_selftest_items2', code)


if __name__ == '__main__':
    main()
