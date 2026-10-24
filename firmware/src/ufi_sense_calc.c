/**
 * UFI Flux Engine - pure helpers of the v1.0 sensing (stage A10), no HAL: tested natively
 * in tests/test_sense.c.
 *
 * - SHT40 CRC-8 (polynomial 0x31, init 0xFF, no reflection, no final xor; Sensirion SHT4x
 *   datasheet section 4.4) and the conversion of the two 16-bit raw words:
 *   T = -45 + 175 * S_T / 65535 degC, RH = -6 + 125 * S_RH / 65535 %RH (clamped to 0..100)
 * - divider scaling of the rail measurements: FDD_5V through 10k/10k (x2), FDD_12V through
 *   33k/10k (x4.3)
 * - calendar checks and text
 * - TIM8 capture -> TIM2 time: both timers run from the same 275 MHz kernel clock, so the
 *   ticks that passed in TIM8 since the capture are the same number of TIM2 ticks.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ufi_sense.h"

uint8_t ufi_sht40_crc8(const uint8_t* p, uint32_t n)
{
    uint8_t crc = 0xFFu;
    while (n--) {
        crc ^= *p++;
        for (int b = 0; b < 8; b++) {
            crc = (crc & 0x80u) ? (uint8_t)((crc << 1) ^ 0x31u) : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

bool ufi_sht40_convert(const uint8_t raw[6], int16_t* temp_x100, uint16_t* rh_x100)
{
    if (ufi_sht40_crc8(raw, 2) != raw[2] || ufi_sht40_crc8(raw + 3, 2) != raw[5]) {
        return false;
    }
    const uint32_t st = ((uint32_t)raw[0] << 8) | raw[1];
    const uint32_t srh = ((uint32_t)raw[3] << 8) | raw[4];
    /* 0.01 degC: -4500 + 17500 * st / 65535, rounded */
    const int32_t t = -4500 + (int32_t)((17500ull * st + 65535u / 2u) / 65535u);
    int32_t rh = -600 + (int32_t)((12500ull * srh + 65535u / 2u) / 65535u);
    if (rh < 0) rh = 0;
    if (rh > 10000) rh = 10000;
    *temp_x100 = (int16_t)t;
    *rh_x100 = (uint16_t)rh;
    return true;
}

/* decimal with two places: 2345 -> "23.45", -320 -> "-3.20" (buffer of 8) */
static void x100_text(char* out, int32_t v)
{
    char* p = out;
    if (v < 0) {
        *p++ = '-';
        v = -v;
    }
    const uint32_t whole = (uint32_t)v / 100u, frac = (uint32_t)v % 100u;
    char tmp[6];
    int n = 0;
    uint32_t w = whole;
    do {
        tmp[n++] = (char)('0' + w % 10u);
        w /= 10u;
    } while (w && n < 5);
    while (n) {
        *p++ = tmp[--n];
    }
    *p++ = '.';
    *p++ = (char)('0' + frac / 10u);
    *p++ = (char)('0' + frac % 10u);
    *p = 0;
}

void ufi_sht40_text(char temp[8], char rh[8], int16_t temp_x100, uint16_t rh_x100)
{
    x100_text(temp, temp_x100);
    x100_text(rh, rh_x100);
}

uint16_t ufi_rail_mv(uint16_t adc_mv, int rail)
{
    /* 5 V: 10k/10k -> x2; 12 V: 33k/10k -> x4.3 = x43/10, rounded */
    const uint32_t v = rail ? ((uint32_t)adc_mv * 43u + 5u) / 10u : (uint32_t)adc_mv * 2u;
    return (uint16_t)(v > 65535u ? 65535u : v);
}

bool ufi_calendar_valid(uint16_t year, uint8_t mon, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (year < 2000u || year > 2099u || mon < 1u || mon > 12u || day < 1u || hour > 23u || min > 59u || sec > 59u) {
        return false;
    }
    uint8_t dim = mdays[mon - 1u];
    if (mon == 2u && (year % 4u) == 0u) {      /* 2000..2099: every fourth year is a leap year */
        dim = 29u;
    }
    return day <= dim;
}

uint32_t ufi_calendar_yyyymmdd(uint16_t year, uint8_t mon, uint8_t day)
{
    return (uint32_t)year * 10000u + (uint32_t)mon * 100u + day;
}

static char* two(char* p, uint32_t v)
{
    *p++ = (char)('0' + (v / 10u) % 10u);
    *p++ = (char)('0' + v % 10u);
    return p;
}

void ufi_calendar_text(char out[20], const ufi_rtc_time_t* t)
{
    if (!t->ready) {
        out[0] = 0;
        return;
    }
    char* p = out;
    p = two(p, t->year / 100u);
    p = two(p, t->year % 100u);
    *p++ = '-';
    p = two(p, t->mon);
    *p++ = '-';
    p = two(p, t->day);
    *p++ = 'T';
    p = two(p, t->hour);
    *p++ = ':';
    p = two(p, t->min);
    *p++ = ':';
    p = two(p, t->sec);
    *p = 0;
}

uint32_t ufi_sync_map_t2(uint32_t t2_now, uint16_t t8_now, uint16_t ccr)
{
    /* age of the capture in TIM8 ticks (16-bit wrap-safe), subtracted from the TIM2 counter
     * read at the same moment; valid while the interrupt latency stays below one TIM8
     * period (65536 / 275 MHz = 238 us) */
    return t2_now - (uint32_t)(uint16_t)(t8_now - ccr);
}
