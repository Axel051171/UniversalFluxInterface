/* Native tests of the write codec (firmware/src/ufi_write_codec.c, the real firmware code).
 * Build and run: python firmware/tests/run_tests.py */
#include "ufi_write_codec.h"
#include "check.h"
#include <string.h>

static uint32_t out[4096];

/* ---- compact decoder ------------------------------------------------------------- */

static void feed_split(compact_dec_t* d, const uint8_t* p, uint32_t n, uint32_t at, int* r1, int* r2)
{
    *r1 = compact_feed(d, p, at);
    *r2 = compact_feed(d, p + at, n - at);
}

static void test_first_bytes(void)
{
    /* every possible first byte: 0x01..0xEF one interval, F0..FC and FE start a code,
     * 00 / FD / FF are refused at once */
    for (int b = 0; b < 256; b++) {
        compact_dec_t d;
        compact_init(&d, out, 4);
        const uint8_t byte = (uint8_t)b;
        const int r = compact_feed(&d, &byte, 1);
        if (b >= 0x01 && b <= 0xEF) {
            CHECK(r == 0 && d.decoded == 1 && out[0] == (uint32_t)b && d.need == 0, "byte %02X", b);
        } else if ((b >= 0xF0 && b <= 0xFC) || b == 0xFE) {
            CHECK(r == 0 && d.decoded == 0 && d.need == (b == 0xFE ? 5 : 2) && d.len == 1, "prefix %02X", b);
        } else {
            CHECK(r == WRITE_CODEC_ERR_DATA && d.bad && d.decoded == 0, "reserved %02X", b);
            /* nothing after the error is interpreted, even valid bytes */
            const uint8_t ok = 0x10;
            CHECK(compact_feed(&d, &ok, 1) == WRITE_CODEC_ERR_DATA && d.decoded == 0, "after error %02X", b);
        }
    }
}

static void test_codes_and_splits(void)
{
    /* two-byte code 0xF3 0x21 = 240 + 3*256 + 33 = 1041; five-byte 0xFE 0x12345678 */
    const uint8_t data[] = {0x05, 0xF3, 0x21, 0xFE, 0x78, 0x56, 0x34, 0x12, 0xEF};
    const uint32_t want[] = {5, 1041, 0x12345678, 0xEF};
    for (uint32_t at = 0; at <= sizeof(data); at++) {
        compact_dec_t d;
        compact_init(&d, out, 4);
        int r1, r2;
        feed_split(&d, data, sizeof(data), at, &r1, &r2);
        CHECK(r1 == 0 && r2 == 0 && compact_complete(&d) && !memcmp(out, want, sizeof(want)),
              "split at %u: r=%d/%d decoded=%u", at, r1, r2, d.decoded);
    }
    /* one byte per call */
    compact_dec_t d;
    compact_init(&d, out, 4);
    int ok = 1;
    for (uint32_t i = 0; i < sizeof(data); i++) {
        ok &= compact_feed(&d, &data[i], 1) == 0;
    }
    CHECK(ok && compact_complete(&d), "byte-wise feed");
}

static void test_audit_f01_sequence(void)
{
    /* the audit's reproduction: 00 01 02 03 04 05 used to run past code[5] */
    const uint8_t bad[] = {0x00, 0x01, 0x02, 0x03, 0x04, 0x05};
    compact_dec_t d;
    compact_init(&d, out, 16);
    CHECK(compact_feed(&d, bad, sizeof(bad)) == WRITE_CODEC_ERR_DATA && d.decoded == 0 && d.len == 0,
          "F01 sequence refused at the first byte (decoded %u)", d.decoded);
    /* reserved byte at the end of a block, valid data before it */
    const uint8_t tail[] = {0x10, 0x20, 0xFD};
    compact_init(&d, out, 16);
    CHECK(compact_feed(&d, tail, sizeof(tail)) == WRITE_CODEC_ERR_DATA && d.decoded == 2, "FD at block end");
    const uint8_t tail2[] = {0x10, 0x20, 0xFF};
    compact_init(&d, out, 16);
    CHECK(compact_feed(&d, tail2, sizeof(tail2)) == WRITE_CODEC_ERR_DATA && d.decoded == 2, "FF at block end");
    /* a reserved byte inside a multi-byte code is data for that code (F0 00 = 240) */
    const uint8_t inner[] = {0xF0, 0x00};
    compact_init(&d, out, 1);
    CHECK(compact_feed(&d, inner, 2) == 0 && out[0] == 240 && compact_complete(&d), "F0 00 = 240");
}

static void test_limits(void)
{
    compact_dec_t d;
    /* more intervals than announced */
    const uint8_t three[] = {0x10, 0x10, 0x10};
    compact_init(&d, out, 2);
    CHECK(compact_feed(&d, three, 3) == WRITE_CODEC_ERR_DATA && d.decoded == 2, "overcount refused");
    /* zero interval in a five-byte code */
    const uint8_t zero[] = {0xFE, 0, 0, 0, 0};
    compact_init(&d, out, 2);
    CHECK(compact_feed(&d, zero, 5) == WRITE_CODEC_ERR_DATA && d.decoded == 0, "zero interval refused");
    /* incomplete code at the end is not complete */
    const uint8_t half[] = {0x10, 0xF2};
    compact_init(&d, out, 2);
    CHECK(compact_feed(&d, half, 2) == 0 && !compact_complete(&d) && d.need == 2, "pending code");
    /* too few intervals is not complete either */
    compact_init(&d, out, 3);
    CHECK(compact_feed(&d, three, 2) == 0 && !compact_complete(&d), "short");
    /* an error, then a fresh decoder works again (new write after a refused one) */
    compact_init(&d, out, 2);
    compact_feed(&d, zero, 5);
    compact_init(&d, out, 2);
    CHECK(compact_feed(&d, three, 2) == 0 && compact_complete(&d), "fresh decoder after error");
}

/* ---- packing -------------------------------------------------------------------- */

static uint32_t sum16(const uint16_t* a, uint32_t n)
{
    uint32_t s = 0;
    for (uint32_t i = 0; i < n; i++) s += a[i] + 1u;
    return s;
}

static void test_pack_range(void)
{
    uint16_t arr[8];
    uint32_t clamped = 99;
    const pack_cfg_t plain = {.scale = 1000, .precomp_ns = 0, .track = 0, .clamp = false};
    const uint32_t ok[] = {440, 1100, 65536, 2000};
    CHECK(write_pack(ok, 4, &plain, arr, &clamped) == 0 && clamped == 0 &&
          arr[0] == 439 && arr[1] == 1099 && arr[2] == 65535 && arr[3] == 1999, "in range");

    /* the audit's vector: 275, 1100, 100000 -> refused, input untouched */
    uint32_t bad[] = {275, 1100, 100000};
    const uint32_t keep[] = {275, 1100, 100000};
    memset(arr, 0xAA, sizeof(arr));
    CHECK(write_pack(bad, 3, &plain, (uint16_t*)bad, &clamped) == WRITE_CODEC_ERR_RANGE && clamped == 0 &&
          !memcmp(bad, keep, sizeof(keep)), "out of range refused, input untouched");
    /* with clamping: 440, 1100, 65536 and the count */
    const pack_cfg_t clamp = {.scale = 1000, .precomp_ns = 0, .track = 0, .clamp = true};
    CHECK(write_pack(bad, 3, &clamp, arr, &clamped) == 0 && clamped == 2 &&
          arr[0] == 439 && arr[1] == 1099 && arr[2] == 65535, "clamped %u", clamped);
    /* 439 is one below the limit, 65537 one above */
    const uint32_t edge_lo[] = {1000, 439, 1000}, edge_hi[] = {1000, 65537, 1000};
    CHECK(write_pack(edge_lo, 3, &plain, arr, &clamped) == WRITE_CODEC_ERR_RANGE, "439 refused");
    CHECK(write_pack(edge_hi, 3, &plain, arr, &clamped) == WRITE_CODEC_ERR_RANGE, "65537 refused");
}

static void test_pack_scale(void)
{
    /* the remainder carries: the sum of the scaled periods equals sum * scale (+-1) */
    uint32_t d[1000];
    uint64_t total = 0;
    for (uint32_t i = 0; i < 1000; i++) {
        d[i] = 1100u + (i * 7919u) % 1500u;
        total += d[i];
    }
    static uint16_t arr[1000];
    uint32_t clamped;
    const uint16_t scales[] = {500, 833, 1000, 1200, 2000};
    for (int k = 0; k < 5; k++) {
        const pack_cfg_t cfg = {.scale = scales[k], .precomp_ns = 0, .track = 0, .clamp = false};
        const int r = write_pack(d, 1000, &cfg, arr, &clamped);
        const uint64_t want = total * scales[k] / 1000u;
        const uint64_t got = sum16(arr, 1000);
        CHECK(r == 0 && (got == want || got + 1 == want || got == want + 1),
              "scale %u: sum %llu vs %llu", scales[k], (unsigned long long)got, (unsigned long long)want);
    }
    /* scale 0.5 pushes 800 below the limit: refused */
    const uint32_t low[] = {800, 800, 800};
    const pack_cfg_t half = {.scale = 500, .precomp_ns = 0, .track = 0, .clamp = false};
    CHECK(write_pack(low, 3, &half, arr, &clamped) == WRITE_CODEC_ERR_RANGE, "scaled below range refused");
}

/* reference: the former in-place routine (precompensation rule of ufi_write.c 1.14) */
static uint32_t ref_precomp(uint32_t t, uint32_t prev, uint32_t next, uint8_t track, uint32_t ns)
{
    if (track < 40) return t;
    const uint32_t pns = (track > 60) ? ns : ns / 2;
    const uint32_t ticks = (pns * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL;
    if (prev > t * 2) return (t > ticks) ? t - ticks : t;
    if (next > t * 2) return t + ticks;
    return t;
}

static void test_pack_precomp(void)
{
    uint32_t d[64];
    for (uint32_t i = 0; i < 64; i++) {
        d[i] = (i % 3 == 0) ? 1100u : (i % 3 == 1) ? 2200u : 1650u;     /* 4/8/6 us pattern */
    }
    d[10] = 1100; d[11] = 2300;                                        /* short-long pair */
    uint16_t arr[64];
    uint32_t clamped;
    const uint8_t tracks[] = {0, 39, 40, 60, 61, 79};
    for (int k = 0; k < 6; k++) {
        const pack_cfg_t cfg = {.scale = 1000, .precomp_ns = 125, .track = tracks[k], .clamp = false};
        CHECK(write_pack(d, 64, &cfg, arr, &clamped) == 0, "precomp track %u", tracks[k]);
        int same = 1;
        for (uint32_t i = 0; i < 64; i++) {
            const uint32_t want = (i > 0 && i + 1 < 64)
                ? ref_precomp(d[i], d[i - 1], d[i + 1], tracks[k], 125) : d[i];
            same &= arr[i] == (uint16_t)(want - 1);
        }
        CHECK(same, "precomp values track %u", tracks[k]);
    }
    /* in-place packing (arr aliases the deltas) gives the same result */
    uint32_t copy[64];
    memcpy(copy, d, sizeof(copy));
    const pack_cfg_t cfg = {.scale = 1000, .precomp_ns = 125, .track = 79, .clamp = false};
    write_pack(d, 64, &cfg, arr, &clamped);
    write_pack(copy, 64, &cfg, (uint16_t*)copy, &clamped);
    CHECK(!memcmp(arr, copy, 64 * sizeof(uint16_t)), "in-place == separate output");
    /* ED detection: mostly short intervals -> 42 ns */
    uint32_t ed[2000];
    for (uint32_t i = 0; i < 2000; i++) ed[i] = (i % 4) ? 300u : 2000u;
    CHECK(write_precomp_auto_ns(ed, 2000) == PRECOMP_ED_NS && write_precomp_auto_ns(d, 64) == PRECOMP_DD_HD_NS,
          "auto precomp by data");
}

int main(void)
{
    test_first_bytes();
    test_codes_and_splits();
    test_audit_f01_sequence();
    test_limits();
    test_pack_range();
    test_pack_scale();
    test_pack_precomp();
    CHECK_SUMMARY("test_write_codec");
}
