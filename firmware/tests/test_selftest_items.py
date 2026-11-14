"""Single SELFTEST items of ufi_selftest.c with their limits (P06): timer, usb_sof, psram, sd,
rail_5v / rail_12v, board_id, usbc_cc.

Production functions, extracted verbatim; in t_usb_sof only the address of the USB device
registers is replaced (a 32-bit register address on the board, an object of this test here).
Time is virtual: every HAL_GetTick() advances it by one microsecond and sets TIM2->CNT and the
USB frame number from it, with a settable deviation of the flux timer and a settable host.

Covered: the +-2 % window of the timer item from any phase; the crystal measurement over 128
frames with its +-500 ppm limit, no host, a host that stops, a frame number that jumps (bench
finding: about -1 000 000 ppm), small steps and the wrap of the 14-bit frame number; the PSRAM
pattern (what is written, what is left alone, every skip reason, the reported offset of a bad
word); the status rules of the remaining items at their boundaries.
"""
import re

from test_regressions import ERRORS, FW, typedef, function, run


def main():
    source = (FW / 'src/ufi_selftest.c').read_text(encoding='utf-8')
    board = (FW / 'include/board.h').read_text(encoding='utf-8')
    header = FW / 'include/ufi_firmware.h'
    code = '#include <stdint.h>\n#include <stdbool.h>\n#include <stdio.h>\n#include <string.h>\n#include <assert.h>\n'
    code += '#define __packed __attribute__((packed))\n'
    code += re.search(r'^#define FLUX_TIMER_FREQ\s+\w+', header.read_text(encoding='utf-8'), re.M).group() + '\n'
    for name in ('BOARD_ID_V05_MV', 'BOARD_ID_V06_MV', 'BOARD_ID_V07_MV', 'BOARD_ID_V10_MV', 'BOARD_HAS_PSRAM'):
        code += re.search(r'^#define ' + name + r'\s+\w+', board, re.M).group() + '\n'
    code += ERRORS
    for name in ('capture_state_t', 'write_state_t', 'sd_info_t', 'usb_power_t', 'board_status_t'):
        code += typedef(header, name)
    code += re.search(r'^enum \{\n    ST_OK = 0,.*?^\};\n', source, re.M | re.S).group()
    code += re.search(r'^typedef struct \{\n    uint8_t id;.*?^\} st_item_t;\n', source, re.M | re.S).group()
    code += r'''
enum { ADC_CH_BOARD_ID = 18 };
typedef struct { volatile uint32_t CNT; } TIM_TypeDef;
typedef struct { volatile uint32_t DSTS; } USB_OTG_DeviceTypeDef;
static TIM_TypeDef tim2;
static USB_OTG_DeviceTypeDef otg;
#define TIM2 (&tim2)
#define USB_OTG_DSTS_FNSOF (0x3FFFu << 8)
static struct { capture_state_t state; } g_capture;

/* ---- virtual time ---- */
static uint64_t t_us;
static int32_t tim_ppm;                     /* deviation of the flux timer from 275 MHz */
static bool host;                           /* start-of-frame packets arrive */
static uint64_t host_until_us;              /* != 0: the host stops at this time */
static uint64_t jump_at_us;                 /* != 0: the frame number jumps by jump_frames at this time */
static uint32_t jump_frames, frame_base;
static uint32_t HAL_GetTick(void)
{
    t_us += 1u;
    const int64_t nominal = (int64_t)t_us * 275;
    tim2.CNT = (uint32_t)(nominal + nominal * tim_ppm / 1000000);
    const uint64_t host_us = (host_until_us && t_us > host_until_us) ? host_until_us : t_us;
    uint32_t frame = frame_base + (host ? (uint32_t)(host_us / 1000u) : 0u);
    if (jump_at_us && t_us >= jump_at_us) frame += jump_frames;
    otg.DSTS = (frame & 0x3FFFu) << 8;
    return (uint32_t)(t_us / 1000u);
}
static void clock(uint64_t start_us, int32_t ppm, bool with_host)
{
    tim_ppm = ppm; host = with_host; host_until_us = jump_at_us = 0; jump_frames = frame_base = 0;
    t_us = start_us - 1u; (void)HAL_GetTick();      /* registers show this moment, not the last test's */
}
static void set_frame_base(uint32_t base) { frame_base = base; t_us -= 1u; (void)HAL_GetTick(); }

/* ---- drivers ---- */
static int noted_ppm; static unsigned notes;
static void ufi_sense_note_clock_ppm(int32_t ppm) { noted_ppm = ppm; notes++; }
static uint32_t store[20000], words;
static bool is_psram;
static unsigned invalidations, flushes;
static int64_t bad_word = -1;               /* word of the test window that reads back wrong */
static write_state_t wstate;
static uint32_t* ufi_flux_store(uint32_t* n) { *n = words; return store; }
static bool ufi_flux_store_is_psram(void) { return is_psram; }
static write_state_t ufi_write_get_state(void) { return wstate; }
static void ufi_flux_invalidate(void) { invalidations++; }
static void SCB_CleanInvalidateDCache(void) { flushes++; if (bad_word >= 0) store[words - 16384u + (uint32_t)bad_word] ^= 0x00010000u; }
static sd_info_t sd; static int sd_result;
static int ufi_sd_info(sd_info_t* info) { *info = sd; return sd_result; }
static board_status_t board;
static board_status_t ufi_board_status(void) { return board; }
static uint16_t id_mv;
static uint16_t ufi_adc_mv(uint32_t ch) { assert(ch == ADC_CH_BOARD_ID); return id_mv; }
static usb_power_t usbc; static int usbc_result;
static int ufi_usb_power(usb_power_t* p) { *p = usbc; return usbc_result; }
'''
    sof = function('ufi_selftest.c', 't_usb_sof')
    address = ('    USB_OTG_DeviceTypeDef* dev =\n'
               '        (USB_OTG_DeviceTypeDef*)((uint32_t)hpcd_USB_OTG_HS.Instance + USB_OTG_DEVICE_BASE);\n')
    assert sof.count(address) == 1, 't_usb_sof: register address expression changed'
    code += function('ufi_selftest.c', 't_timer') + sof.replace(address, '    USB_OTG_DeviceTypeDef* dev = &otg;\n')
    for name in ('t_psram', 't_sd', 't_rail', 't_board_id', 't_usbc'):
        code += function('ufi_selftest.c', name)
    code += r'''
static st_item_t timer(uint64_t start_us, int32_t ppm) { st_item_t it = {.id = 1}; clock(start_us, ppm, true); t_timer(&it); return it; }
static st_item_t sof(void) { st_item_t it = {.id = 2, .status = 0xEE, .value = 0xEEEEEEEEu}; notes = 0; t_usb_sof(&it); return it; }
static bool near(st_item_t it, int32_t ppm) { const int32_t v = (int32_t)it.value; return v >= ppm - 1 && v <= ppm + 9; }
static st_item_t psram(void) { st_item_t it = {.id = 3}; t_psram(&it); return it; }
static void fresh_store(void)
{
    for (unsigned i = 0; i < 20000u; i++) store[i] = 0xC0FFEE00u + i;
    words = 20000; is_psram = true; invalidations = flushes = 0; bad_word = -1; wstate = WRITE_IDLE; g_capture.state = CAPTURE_IDLE;
}

int main(void)
{
    st_item_t it;
    /* ---- timer: ticks per millisecond over 20 ms, OK inside +-2 % (exclusive) ---- */
    const uint32_t nominal = FLUX_TIMER_FREQ / 1000u;
    it = timer(1, 0); assert(it.status == ST_OK && it.value == nominal);
    for (uint64_t phase = 1; phase < 1000u; phase += 111u) {    /* the start phase inside a millisecond does not matter */
        it = timer(5000u + phase, 0); assert(it.status == ST_OK && it.value == nominal);
    }
    it = timer(1, -19900); assert(it.status == ST_OK && it.value == 269527u);    /* 275000 x (1 - 0.0199), rounded down */
    it = timer(1, 19900); assert(it.status == ST_OK);
    it = timer(1, -20000); assert(it.status == ST_FAIL && it.value == nominal - nominal / 50u);    /* exactly -2 %: outside */
    it = timer(1, 20000); assert(it.status == ST_FAIL && it.value == nominal + nominal / 50u);
    it = timer(1, -500000); assert(it.status == ST_FAIL && it.value == nominal / 2u);               /* wrong prescaler */
    it = timer(1, 1000000); assert(it.status == ST_FAIL && it.value == 2u * nominal);

    /* ---- usb_sof: crystal against 128 host frames ----
     * The loop of t_usb_sof reads the timer one pass after the 128th frame edge; a pass is one
     * microsecond in this model (about 50 ns on the board), which shows as +7 ppm here. */
    clock(300, 0, false); it = sof();
    assert(it.status == ST_SKIP && it.value == 0xEEEEEEEEu && !notes);          /* not enumerated: no frames */
    assert(t_us < 300u + 30000u);                                               /* gives up after about 20 ms */
    clock(300, 0, true); it = sof(); assert(it.status == ST_OK && near(it, 0) && notes == 1 && noted_ppm == (int32_t)it.value);
    assert(t_us > 128000u && t_us < 131000u);                                   /* one frame to synchronise + 128 */
    clock(777, 480, true); it = sof(); assert(it.status == ST_OK && near(it, 480) && noted_ppm == (int32_t)it.value);
    clock(777, -480, true); it = sof(); assert(it.status == ST_OK && near(it, -480));
    clock(777, 520, true); it = sof(); assert(it.status == ST_WARN && near(it, 520) && notes == 1);     /* outside +-500 ppm: warning, still noted */
    clock(777, -520, true); it = sof(); assert(it.status == ST_WARN && near(it, -520));
    clock(777, 60, true); it = sof(); assert(it.status == ST_OK && near(it, 60));
    /* a jump of the frame number restarts the measurement instead of counting thousands of frames */
    clock(300, 0, true); jump_at_us = 50000; jump_frames = 1000; it = sof();
    assert(it.status == ST_OK && near(it, 0) && t_us > 50000u + 128000u && t_us < 50000u + 131000u);
    clock(300, 0, true); jump_at_us = 50500; jump_frames = 5; it = sof(); assert(it.status == ST_OK && near(it, 0) && t_us > 178000u);
    /* a step of up to four frame numbers is taken as that many frames; here no time passed for them, so the
       result is off by 4/128 and warns (on the board such a step means the loop was held up that long) */
    clock(300, 0, true); jump_at_us = 50500; jump_frames = 4; it = sof();
    assert(it.status == ST_WARN && t_us < 127000u && (int32_t)it.value > -31260 && (int32_t)it.value < -31230);    /* 124 ms for 128 frame numbers */
    /* the 14-bit frame number wraps inside the measurement */
    clock(300, 0, true); set_frame_base(0x3FF0u); it = sof(); assert(it.status == ST_OK && near(it, 0));
    /* the host stops in the middle: skipped after 400 ms, nothing noted */
    clock(300, 0, true); host_until_us = 60500; it = sof();
    assert(it.status == ST_SKIP && !notes && t_us > 400000u && t_us < 403000u);

    /* ---- psram: pattern over the top 64 KB of the flux store ---- */
    fresh_store(); it = psram();
    assert(it.status == ST_OK && it.value == 20000u * 4u && invalidations == 1 && flushes == 1);
    for (unsigned i = 0; i < 20000u - 16384u; i++) assert(store[i] == 0xC0FFEE00u + i);     /* below the window: untouched */
    for (unsigned i = 0; i < 16384u; i++) assert(store[20000u - 16384u + i] == ((i * 0x9E3779B9u) ^ 0xA5A5A5A5u));
    for (int64_t bad = 0; bad < 16384; bad += 16383) {
        fresh_store(); bad_word = bad; it = psram(); assert(it.status == ST_FAIL && it.value == (uint32_t)bad * 4u);
    }
    fresh_store(); is_psram = false; it = psram();
    assert(it.status == (BOARD_HAS_PSRAM ? ST_FAIL : ST_SKIP) && it.value == 80000u && !invalidations && store[19999] == 0xC0FFEE00u + 19999u);
    /* never while the store is in use, and not on a store smaller than the window */
    for (int why = 0; why < 4; why++) {
        fresh_store();
        if (why == 0) g_capture.state = CAPTURE_WAITING_INDEX;
        if (why == 1) g_capture.state = CAPTURE_RUNNING;
        if (why == 2) wstate = WRITE_ACTIVE;
        if (why == 3) words = 16383;
        it = psram(); assert(it.status == ST_SKIP && !invalidations && !flushes);
        for (unsigned i = 0; i < 20000u; i++) assert(store[i] == 0xC0FFEE00u + i);
    }
    fresh_store(); words = 16384; it = psram(); assert(it.status == ST_OK && store[0] == 0xA5A5A5A5u && store[16384] == 0xC0FFEE00u + 16384u);
    fresh_store(); g_capture.state = CAPTURE_COMPLETE; it = psram(); assert(it.status == ST_OK && invalidations == 1);    /* a held capture is given up */

    /* ---- sd ---- */
    it = (st_item_t){.id = 4}; sd = (sd_info_t){.present = 1, .capacity_mb = 3724}; sd_result = 0; t_sd(&it); assert(it.status == ST_OK && it.value == 3724);
    it = (st_item_t){.id = 4}; sd.present = 0; t_sd(&it); assert(it.status == ST_FAIL && it.value == 0);
    it = (st_item_t){.id = 4}; sd.present = 1; sd_result = UFI_ERR_STORAGE; t_sd(&it); assert(it.status == ST_FAIL && it.value == 0);

    /* ---- drive rails: trip = fail, off = skipped, the trip wins ---- */
    board = (board_status_t){.power = 0x03, .i5_ma = 410, .i12_ma = 120};
    it = (st_item_t){.id = 5}; t_rail(&it, 0); assert(it.status == ST_OK && it.value == 410);
    it = (st_item_t){.id = 6}; t_rail(&it, 1); assert(it.status == ST_OK && it.value == 120);
    board.power = 0x01; it = (st_item_t){.id = 6}; t_rail(&it, 1); assert(it.status == ST_SKIP);
    it = (st_item_t){.id = 5}; t_rail(&it, 0); assert(it.status == ST_OK);
    board.power = 0x02; board.flags = 0x02; it = (st_item_t){.id = 5}; t_rail(&it, 0); assert(it.status == ST_FAIL);
    it = (st_item_t){.id = 6}; t_rail(&it, 1); assert(it.status == ST_OK);
    board.power = 0x01; board.flags = 0x04; it = (st_item_t){.id = 6}; t_rail(&it, 1); assert(it.status == ST_FAIL);
    board.flags = 0xF9; board.power = 0x0F; it = (st_item_t){.id = 5}; t_rail(&it, 0); assert(it.status == ST_OK);     /* other flag bits do not count */

    /* ---- board id: each known divider +-200 mV (exclusive), anything else warns ---- */
    static const uint16_t ids[] = {BOARD_ID_V05_MV, BOARD_ID_V06_MV, BOARD_ID_V07_MV, BOARD_ID_V10_MV};
    for (unsigned k = 0; k < 4; k++) {
        static const int off[] = {0, -199, 199};
        for (unsigned o = 0; o < 3; o++) {
            id_mv = (uint16_t)(ids[k] + off[o]); it = (st_item_t){.id = 7}; t_board_id(&it);
            assert(it.status == ST_OK && it.value == id_mv);
        }
        id_mv = (uint16_t)(ids[k] - 200); it = (st_item_t){.id = 7}; t_board_id(&it); assert(it.status == ST_WARN && it.value == id_mv);
        id_mv = (uint16_t)(ids[k] + 200); it = (st_item_t){.id = 7}; t_board_id(&it); assert(it.status == ST_WARN);
        for (unsigned j = 0; j < k; j++) assert(ids[k] > ids[j] ? ids[k] - ids[j] >= 400 : ids[j] - ids[k] >= 400);   /* windows do not overlap */
    }
    id_mv = 0; it = (st_item_t){.id = 7}; t_board_id(&it); assert(it.status == ST_WARN);
    id_mv = 3300; it = (st_item_t){.id = 7}; t_board_id(&it); assert(it.status == ST_WARN && it.value == 3300);

    /* ---- usb-c ---- */
    usbc = (usb_power_t){.current_ma = 3000}; usbc_result = 0; it = (st_item_t){.id = 8}; t_usbc(&it); assert(it.status == ST_OK && it.value == 3000);
    usbc.current_ma = 0; it = (st_item_t){.id = 8}; t_usbc(&it); assert(it.status == ST_WARN && it.value == 0);
    usbc.current_ma = 1500; usbc_result = UFI_ERR_NOT_IMPL; it = (st_item_t){.id = 8}; t_usbc(&it); assert(it.status == ST_SKIP && it.value == 0);
    puts("selftest items: timer window, crystal ppm with host faults, psram pattern and skips, sd, rails, board id, usb-c");
    return 0;
}
'''
    run('regression_selftest_items', code)


if __name__ == '__main__':
    main()
