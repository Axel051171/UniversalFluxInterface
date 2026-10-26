/* Native test of the READ END read context block (ufi_read_info.c, READ flag bit5, 4.24):
 * wire layout, flags, stream time over several revolutions and a 32-bit timer wrap,
 * index-less boundaries, loss entries with open end and before the first index. */
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "ufi_read_info.h"

static uint32_t rd32(const uint8_t* p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static uint64_t rd64(const uint8_t* p) { return rd32(p) | (uint64_t)rd32(p + 4) << 32; }

static void test_stream_time(void)
{
    /* three index pulses 55 M ticks apart (200 ms at 275 MHz), crossing the 32-bit wrap */
    volatile uint32_t idx[4] = {0xF0000000u, 0xF0000000u + 55000000u, 0xF0000000u + 110000000u, 0};
    uint64_t t;
    assert(ufi_read_stream_time(idx, 0, 0, 123, &t) == 0 && t == 0);          /* no index: no stream time */
    assert(ufi_read_stream_time(idx, 3, 0, 0xEFFFFFFFu, &t) == 0 && t == 0);  /* before the first index */
    assert(ufi_read_stream_time(idx, 3, 0, idx[0] + 7u, &t) == 0 && t == 7u);
    assert(ufi_read_stream_time(idx, 3, 0, idx[1], &t) == 1 && t == 55000000u);   /* at FD 1 */
    assert(ufi_read_stream_time(idx, 3, 0, idx[2] + 5u, &t) == 2 && t == 110000005u);
    assert(ufi_read_stream_time(idx, 3, 0, idx[2] + 0x7FFFFFF0u, &t) == 2 && t == 110000000ull + 0x7FFFFFF0u);
    /* index-less: one recorded boundary, the next ones follow from the period */
    volatile uint32_t slice[2] = {1000u, 0};
    assert(ufi_read_stream_time(slice, 1, 500u, 1000u + 1250u, &t) == 2 && t == 1250u);
}

static void test_pack(void)
{
    volatile uint32_t idx[3] = {100u, 1100u, 2100u};
    ufi_read_info_t in;
    memset(&in, 0, sizeof(in));
    in.revs_requested = 0;
    in.revs_effective = 1;
    in.quarter = 0x80u | (2u << 4);
    in.start = (ufi_read_sample_t){.density = 1, .drate = 0, .track = 40, .side = 1, .status = 0x01};
    in.end = (ufi_read_sample_t){.density = 1, .drate = 1, .track = 41, .side = 1, .status = 0x01};
    in.tcxo = true;
    in.ppm_valid = true;
    in.ppm = -12;
    in.ppm_age_ms = 60000u;
    in.climate_valid = true;
    in.temp_x100 = -150;
    in.rh_x100 = 4567;
    in.climate_age_ms = 5u;
    in.loss_total = in.loss_n = 1;
    in.loss[0] = (ufi_loss_raw_t){.kind = READ_LOSS_RDATA, .start = 1150u, .end = 2200u, .samples = 77u};
    uint8_t out[UFI_READ_INFO_SIZE + 4];
    memset(out, 0xA5, sizeof(out));
    assert(ufi_read_info_pack(&in, idx, 3, 0, out) == UFI_READ_INFO_SIZE && out[UFI_READ_INFO_SIZE] == 0xA5);
    assert(out[0] == 1 && out[1] == UFI_READ_INFO_SIZE);
    assert(out[2] == (READ_INFO_REVS_CLAMPED | READ_INFO_LINES_CHANGED | READ_INFO_HEAD_MOVED | READ_INFO_PPM |
                      READ_INFO_CLIMATE | READ_INFO_TCXO));
    assert(out[3] == 0 && out[4] == 1 && out[5] == 1 && out[6] == 0 && out[7] == 3);
    assert(out[8] == 40 && out[9] == 1 && out[10] == 0xA0 && out[11] == 1 && out[12] == 41 && out[13] == 1 && out[14] == 1);
    assert(out[15] == 1 && (int32_t)rd32(out + 16) == -12 && rd32(out + 20) == 60000u);
    assert((int16_t)(out[24] | out[25] << 8) == -150 && (out[26] | out[27] << 8) == 4567 && rd32(out + 28) == 5u);
    assert(out[32] == 1 && !out[33] && !out[34] && !out[35]);
    const uint8_t* e = out + 36;
    assert(e[0] == READ_LOSS_RDATA && e[1] == 1 && e[2] == 2 && rd64(e + 4) == 1050u && rd64(e + 12) == 2100u &&
           rd32(e + 20) == 77u);
    for (unsigned i = 36u + UFI_READ_LOSS_SIZE; i < UFI_READ_INFO_SIZE; i++) assert(out[i] == 0);   /* unused slots */

    /* nothing clamped/changed, no cached values: flags 0 and zero value fields */
    memset(&in, 0, sizeof(in));
    in.revs_requested = in.revs_effective = 3;
    in.loss_total = in.loss_n = 1;
    in.loss[0] = (ufi_loss_raw_t){.kind = READ_LOSS_FULL | READ_LOSS_OPEN_END, .start = 2000u, .end = 9u, .samples = 5u};
    assert(ufi_read_info_pack(&in, idx, 3, 0, out) == UFI_READ_INFO_SIZE);
    assert(out[2] == 0 && rd32(out + 16) == 0 && rd32(out + 28) == 0);
    assert(e[1] == 1 && e[2] == 1 && rd64(e + 4) == 1900u && rd64(e + 12) == 0 && rd32(e + 20) == 5u);   /* open end */
    /* before the first index: kind only, no stream time */
    in.loss[0] = (ufi_loss_raw_t){.kind = READ_LOSS_INDEX | READ_LOSS_BEFORE_INDEX, .start = 50u, .end = 60u};
    assert(ufi_read_info_pack(&in, idx, 0, 0, out) == UFI_READ_INFO_SIZE);
    assert(e[0] == (READ_LOSS_INDEX | READ_LOSS_BEFORE_INDEX) && !e[1] && !e[2] && !rd64(e + 4) && !rd64(e + 12));
    /* more windows than slots: the list keeps four, the flag tells */
    in.loss_total = 9;
    in.loss_n = 9;
    assert(ufi_read_info_pack(&in, idx, 3, 0, out) == UFI_READ_INFO_SIZE);
    assert(out[15] == 9 && out[32] == UFI_READ_INFO_SLOTS && (out[2] & READ_INFO_LOSS_TRUNCATED));
}

int main(void)
{
    test_stream_time();
    test_pack();
    puts("read info: stream time over wraps and slices, block layout, flags, loss entries passed");
    return 0;
}
