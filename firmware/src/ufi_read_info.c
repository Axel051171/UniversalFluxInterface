/**
 * UFI Flux Engine - read context block of the READ END frame (READ flag bit5, 1.18-dev,
 * docs/USB_Protokoll.md 4.24).  HAL-free packing; the samples come from ufi_main.c.
 */
#include "ufi_read_info.h"
#include <string.h>

static void put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* p, uint32_t v) { for (unsigned i = 0; i < 4u; i++) p[i] = (uint8_t)(v >> (8u * i)); }
static void put64(uint8_t* p, uint64_t v) { put32(p, (uint32_t)v); put32(p + 4, (uint32_t)(v >> 32)); }

uint8_t ufi_read_stream_time(const volatile uint32_t* idx_time, uint8_t idx_count, uint32_t period,
                             uint32_t t, uint64_t* ticks)
{
    *ticks = 0;
    if (!idx_count) return 0;
    uint8_t r = (uint8_t)(idx_count - 1u);
    while (r > 0u && (int32_t)(t - idx_time[r]) < 0) r--;
    if ((int32_t)(t - idx_time[r]) < 0) return 0;   /* before the first index: stream time 0 */
    uint64_t sum = 0;
    for (uint8_t k = 0; k < r; k++) sum += (uint32_t)(idx_time[k + 1u] - idx_time[k]);
    const uint32_t offset = (uint32_t)(t - idx_time[r]);
    *ticks = sum + offset;
    if (period) {                                   /* boundaries the poll has not recorded yet */
        const uint32_t more = offset / period;
        return (uint8_t)(r + more > 255u ? 255u : r + more);
    }
    return r;
}

static uint8_t sample_bits(const ufi_read_sample_t* s)
{
    return (uint8_t)((s->density ? 1u : 0u) | (s->drate ? 2u : 0u));
}

uint16_t ufi_read_info_pack(const ufi_read_info_t* in, const volatile uint32_t* idx_time, uint8_t idx_count,
                            uint32_t period, uint8_t* out)
{
    memset(out, 0, UFI_READ_INFO_SIZE);
    out[0] = 1;
    out[1] = UFI_READ_INFO_SIZE;
    const uint8_t n = in->loss_n > UFI_READ_INFO_SLOTS ? (uint8_t)UFI_READ_INFO_SLOTS : in->loss_n;
    const bool clamped = in->revs_requested != in->revs_effective;
    const bool lines = sample_bits(&in->start) != sample_bits(&in->end);
    const bool moved = in->start.track != in->end.track || in->start.side != in->end.side ||
                       in->start.status != in->end.status;
    out[2] = (uint8_t)((clamped ? READ_INFO_REVS_CLAMPED : 0u) | (lines ? READ_INFO_LINES_CHANGED : 0u) |
                       (moved ? READ_INFO_HEAD_MOVED : 0u) | (in->ppm_valid ? READ_INFO_PPM : 0u) |
                       (in->climate_valid ? READ_INFO_CLIMATE : 0u) | (in->tcxo ? READ_INFO_TCXO : 0u) |
                       (in->loss_total > n ? READ_INFO_LOSS_TRUNCATED : 0u));
    out[3] = in->revs_requested;
    out[4] = in->revs_effective;
    out[5] = in->start.density ? 1u : 0u;
    out[6] = in->start.drate ? 1u : 0u;
    out[7] = sample_bits(&in->end);
    out[8] = in->start.track;
    out[9] = in->start.side;
    out[10] = in->quarter;
    out[11] = in->start.status;
    out[12] = in->end.track;
    out[13] = in->end.side;
    out[14] = in->end.status;
    out[15] = in->loss_total;
    if (in->ppm_valid) {
        put32(out + 16, (uint32_t)in->ppm);
        put32(out + 20, in->ppm_age_ms);
    }
    if (in->climate_valid) {
        put16(out + 24, (uint16_t)in->temp_x100);
        put16(out + 26, in->rh_x100);
        put32(out + 28, in->climate_age_ms);
    }

    out[32] = n;
    for (uint8_t i = 0; i < n; i++) {
        const ufi_loss_raw_t* l = &in->loss[i];
        uint8_t* e = out + 36u + (uint32_t)i * UFI_READ_LOSS_SIZE;
        e[0] = l->kind;
        if (!(l->kind & READ_LOSS_BEFORE_INDEX)) {
            uint64_t t0, t1 = 0;
            e[1] = ufi_read_stream_time(idx_time, idx_count, period, l->start, &t0);
            e[2] = e[1];
            if (!(l->kind & READ_LOSS_OPEN_END)) e[2] = ufi_read_stream_time(idx_time, idx_count, period, l->end, &t1);
            put64(e + 4, t0);
            put64(e + 12, t1);
        }
        put32(e + 20, l->samples);
    }
    return UFI_READ_INFO_SIZE;
}
