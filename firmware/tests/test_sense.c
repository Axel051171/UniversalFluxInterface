/* Native test of the HAL-free helpers of the v1.0 sensing (ufi_sense_calc.c, stage A10):
 * SHT40 CRC-8 and conversion, rail divider scaling, calendar checks and text, the TIM8 ->
 * TIM2 time mapping of the SYNC capture.  Reference values: Sensirion SHT4x datasheet
 * (CRC example 0xBEEF -> 0x92, conversion formulas in section 4.6). */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ufi_sense.h"

static void put_word(uint8_t* p, uint16_t v)
{
    p[0] = (uint8_t)(v >> 8);
    p[1] = (uint8_t)v;
    p[2] = ufi_sht40_crc8(p, 2);
}

static void test_crc(void)
{
    const uint8_t beef[2] = {0xBE, 0xEF};
    assert(ufi_sht40_crc8(beef, 2) == 0x92);                   /* datasheet example */
    const uint8_t zero[2] = {0, 0};
    assert(ufi_sht40_crc8(zero, 2) == 0x81);                   /* init 0xFF, poly 0x31 */
}

static void test_convert(void)
{
    uint8_t raw[6];
    int16_t t;
    uint16_t rh;
    put_word(raw, 0);                                           /* T = -45.00 degC */
    put_word(raw + 3, 0);                                       /* RH = -6 -> clamped 0 */
    assert(ufi_sht40_convert(raw, &t, &rh) && t == -4500 && rh == 0);
    put_word(raw, 65535);                                       /* T = 130.00 */
    put_word(raw + 3, 65535);                                   /* RH = 119 -> clamped 100.00 */
    assert(ufi_sht40_convert(raw, &t, &rh) && t == 13000 && rh == 10000);
    put_word(raw, 0x6666);                                      /* 0.4 * 175 - 45 = 25.00 (26214/65535 = 0.39999) */
    put_word(raw + 3, 0x8000);                                  /* 0.5 * 125 - 6 = 56.50 */
    assert(ufi_sht40_convert(raw, &t, &rh));
    assert(t >= 2499 && t <= 2500);
    assert(rh >= 5649 && rh <= 5650);
    raw[2] ^= 1u;                                               /* broken CRC */
    assert(!ufi_sht40_convert(raw, &t, &rh));
    char ts[8], rs[8];
    ufi_sht40_text(ts, rs, -320, 4567);
    assert(strcmp(ts, "-3.20") == 0 && strcmp(rs, "45.67") == 0);
    ufi_sht40_text(ts, rs, 2345, 5);
    assert(strcmp(ts, "23.45") == 0 && strcmp(rs, "0.05") == 0);
    ufi_sht40_text(ts, rs, 13000, 10000);
    assert(strcmp(ts, "130.00") == 0 && strcmp(rs, "100.00") == 0);
}

static void test_rails(void)
{
    assert(ufi_rail_mv(2500, 0) == 5000);                       /* 10k/10k */
    assert(ufi_rail_mv(2250, 0) == 4500);
    assert(ufi_rail_mv(2790, 1) == 11997);                      /* 33k/10k: x4.3 */
    assert(ufi_rail_mv(2512, 1) == 10802);
    assert(ufi_rail_mv(0, 1) == 0);
    assert(ufi_rail_mv(65535, 0) == 65535);                     /* saturates */
}

static void test_calendar(void)
{
    assert(ufi_calendar_valid(2026, 10, 25, 14, 3, 11));
    assert(ufi_calendar_valid(2024, 2, 29, 0, 0, 0));           /* leap year */
    assert(!ufi_calendar_valid(2023, 2, 29, 0, 0, 0));
    assert(!ufi_calendar_valid(2026, 4, 31, 0, 0, 0));
    assert(!ufi_calendar_valid(1999, 12, 31, 23, 59, 59));
    assert(!ufi_calendar_valid(2100, 1, 1, 0, 0, 0));
    assert(!ufi_calendar_valid(2026, 13, 1, 0, 0, 0));
    assert(!ufi_calendar_valid(2026, 1, 0, 0, 0, 0));
    assert(!ufi_calendar_valid(2026, 1, 1, 24, 0, 0));
    assert(!ufi_calendar_valid(2026, 1, 1, 0, 60, 0));
    assert(!ufi_calendar_valid(2026, 1, 1, 0, 0, 60));
    assert(ufi_calendar_yyyymmdd(2026, 10, 25) == 20261025u);
    ufi_rtc_time_t t = {true, 2026, 10, 25, 14, 3, 11};
    char text[20];
    ufi_calendar_text(text, &t);
    assert(strcmp(text, "2026-10-25T14:03:11") == 0);
    t.ready = false;
    ufi_calendar_text(text, &t);
    assert(text[0] == 0);
}

static void test_sync_map(void)
{
    /* capture at TIM8 = 1000, ISR runs at TIM8 = 1234 and TIM2 = 5000000: pulse at 4999766 */
    assert(ufi_sync_map_t2(5000000u, 1234, 1000) == 5000000u - 234u);
    /* TIM8 wrapped between the capture (0xFFF0) and the ISR (0x0010): age 32 */
    assert(ufi_sync_map_t2(100u, 0x0010, 0xFFF0) == 100u - 32u);
    /* TIM2 wraps as well */
    assert(ufi_sync_map_t2(5u, 20, 10) == 0xFFFFFFFBu);
    /* zero age */
    assert(ufi_sync_map_t2(777u, 42, 42) == 777u);
}

int main(void)
{
    test_crc();
    test_convert();
    test_rails();
    test_calendar();
    test_sync_map();
    printf("test_sense: ok\n");
    return 0;
}
