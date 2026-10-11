/* Native tests of the write verification (firmware/src/ufi_verify.c with the real MFM codec
 * and write codec): same track back -> content verified; same transition count with other
 * data -> fails; jitter -> still verified; undecodable track -> plausible only. */
#include "ufi_verify.h"
#include "ufi_write_codec.h"
#include "check.h"
#include <string.h>

#define MAXN 80000u
static uint32_t deltas[MAXN], deltas2[MAXN];
static uint16_t arr[MAXN], arr2[MAXN];
static uint8_t sectors[22 * 512], sectors2[22 * 512];
static mfm_decoder_t dec;

static uint8_t size_code;       /* IBM sector size for make_track(), 0 = 512 */

static uint32_t make_track(uint32_t rate, uint8_t spt, uint8_t cyl, uint8_t head, const uint8_t* data,
                           uint32_t* out, uint16_t* packed, bool amiga)
{
    const uint32_t cells = (uint32_t)(((uint64_t)55000000u * 2u * rate / FLUX_TIMER_FREQ) * 98u / 100u);
    uint32_t n;
    if (amiga) {
        n = mfm_encode_amiga_track(rate, cells, cyl, head, spt, data, out, MAXN);
    } else {
        const mfm_track_fmt_t f = {.rate_bps = rate, .track_cells = cells, .spt = spt, .gap3 = 0x6C,
                                   .size_code = size_code};
        n = mfm_encode_track(&f, cyl, head, data, out, MAXN);
    }
    const pack_cfg_t cfg = {.scale = 1000, .precomp_ns = 0, .track = 0, .clamp = false};
    uint32_t clamped;
    CHECK(n > 0 && write_pack(out, n, &cfg, packed, &clamped) == 0, "track %u/%u encodes", rate, spt);
    return n;
}

static uint8_t level_for(const verify_ref_t* ref, const uint16_t* back, uint32_t n, uint32_t jitter_pct,
                         uint32_t seed, bool count_ok)
{
    verify_match_t m;
    verify_match_begin(&m, ref, &dec);
    uint32_t rng = seed;
    for (int pass = 0; pass < 2; pass++) {                  /* two revolutions read back */
        for (uint32_t i = 0; i < n; i++) {
            uint32_t t = (uint32_t)back[i] + 1u;
            if (jitter_pct) {
                rng = rng * 1103515245u + 12345u;
                const int32_t j = (int32_t)((rng >> 8) % (2u * jitter_pct + 1u)) - (int32_t)jitter_pct;
                t = (uint32_t)((int64_t)t + (int64_t)t * j / 100);
            }
            verify_match_interval(&m, t);
        }
    }
    return verify_match_result(&m, count_ok);
}

int main(void)
{
    for (uint32_t i = 0; i < sizeof(sectors); i++) {
        sectors[i] = (uint8_t)(i * 7u + (i >> 9));
        sectors2[i] = (uint8_t)(i * 13u + 1u);
    }

    /* 1.44M track: reference has 18 sectors at 500 kbit/s */
    uint32_t n = make_track(500000u, 18, 5, 1, sectors, deltas, arr, false);
    verify_ref_t ref;
    verify_ref_build(&ref, &dec, arr, n);
    CHECK(ref.rate_bps == 500000u && ref.n == 18 && !ref.amiga && !ref.overflow,
          "reference 1.44M: rate %u sectors %u", ref.rate_bps, ref.n);

    CHECK(level_for(&ref, arr, n, 0, 1, true) == VERIFY_CONTENT, "identical read-back -> content verified");
    CHECK(level_for(&ref, arr, n, 3, 7, true) == VERIFY_CONTENT, "3 %% jitter -> still content verified");
    CHECK(level_for(&ref, arr, n, 0, 1, false) == VERIFY_CONTENT, "content beats a count mismatch");

    /* other data, same geometry and (nearly) same transition count -> must fail */
    uint32_t n2 = make_track(500000u, 18, 5, 1, sectors2, deltas2, arr2, false);
    CHECK(level_for(&ref, arr2, n2, 0, 1, true) == 0, "same count, other data -> fails (n %u vs %u)", n, n2);
    /* same data on another cylinder id -> fails */
    n2 = make_track(500000u, 18, 6, 1, sectors, deltas2, arr2, false);
    CHECK(level_for(&ref, arr2, n2, 0, 1, true) == 0, "other cylinder id -> fails");
    /* one sector lost: drop the read-back after 17 sectors worth of data */
    CHECK(level_for(&ref, arr, n * 9u / 10u, 0, 1, true) == 0, "truncated read-back -> fails");

    /* 720K at 250 kbit/s and Amiga */
    n = make_track(250000u, 9, 40, 0, sectors, deltas, arr, false);
    verify_ref_build(&ref, &dec, arr, n);
    CHECK(ref.rate_bps == 250000u && ref.n == 9, "reference 720K: rate %u sectors %u", ref.rate_bps, ref.n);
    CHECK(level_for(&ref, arr, n, 2, 3, true) == VERIFY_CONTENT, "720K verified with jitter");
    n = make_track(250000u, 11, 10, 1, sectors, deltas, arr, true);
    verify_ref_build(&ref, &dec, arr, n);
    CHECK(ref.rate_bps == 250000u && ref.n == 11 && ref.amiga, "reference Amiga: sectors %u amiga %d", ref.n, ref.amiga);
    CHECK(level_for(&ref, arr, n, 2, 5, true) == VERIFY_CONTENT, "Amiga verified with jitter");
    n2 = make_track(250000u, 11, 10, 1, sectors2, deltas2, arr2, true);
    CHECK(level_for(&ref, arr2, n2, 0, 1, true) == 0, "Amiga other data -> fails");
    /* Amiga HD (1.16): 22 sectors at 500 kbit/s decode and verify like DD */
    n = make_track(500000u, 22, 40, 1, sectors, deltas, arr, true);
    verify_ref_build(&ref, &dec, arr, n);
    CHECK(ref.rate_bps == 500000u && ref.n == 22 && ref.amiga, "reference Amiga HD: rate %u sectors %u", ref.rate_bps, ref.n);
    CHECK(level_for(&ref, arr, n, 2, 9, true) == VERIFY_CONTENT, "Amiga HD verified with jitter");
    n2 = make_track(500000u, 22, 40, 1, sectors2, deltas2, arr2, true);
    CHECK(level_for(&ref, arr2, n2, 0, 1, true) == 0, "Amiga HD other data -> fails");
    CHECK(mfm_encode_amiga_track(500000u, 200000u, 0, 0, 12, sectors, deltas, MAXN) == 0, "Amiga spt 12 refused");

    /* undecodable track (random intervals): no reference, count decides, never 'verified' */
    uint32_t rng = 99;
    for (uint32_t i = 0; i < 30000u; i++) {
        rng = rng * 1103515245u + 12345u;
        arr[i] = (uint16_t)(500u + (rng >> 8) % 3000u);
    }
    verify_ref_build(&ref, &dec, arr, 30000u);
    CHECK(ref.n == 0, "random track: no reference sectors (%u)", ref.n);
    CHECK(level_for(&ref, arr, 30000u, 0, 1, true) == VERIFY_READBACK_PLAUSIBLE, "random: plausible by count");
    CHECK(level_for(&ref, arr, 30000u, 0, 1, false) == 0, "random: count off -> fails");

    /* CRC-32 reference value */
    CHECK(ufi_crc32(0, (const uint8_t*)"123456789", 9) == 0xCBF43926u, "crc32 check value");

    /* 1024-byte sectors (N = 3): the whole sector is compared, not the first 512 bytes */
    size_code = 3;
    n = make_track(500000u, 8, 3, 0, sectors, deltas, arr, false);
    verify_ref_build(&ref, &dec, arr, n);
    CHECK(ref.n == 8 && ref.s[0].n == 3, "reference 8 x 1024: sectors %u n %u", ref.n, ref.s[0].n);
    CHECK(level_for(&ref, arr, n, 2, 3, true) == VERIFY_CONTENT, "1024-byte sectors verified");
    memcpy(sectors2, sectors, sizeof(sectors2));
    sectors2[900] ^= 0x01u;                                 /* byte 900 of sector 1 */
    n2 = make_track(500000u, 8, 3, 0, sectors2, deltas2, arr2, false);
    CHECK(level_for(&ref, arr2, n2, 0, 1, true) == 0, "1024-byte sector changed at byte 900 -> fails");
    /* 256-byte sectors (N = 1): no stale bytes beyond the sector in the checksum */
    size_code = 1;
    n = make_track(250000u, 16, 2, 1, sectors, deltas, arr, false);
    verify_ref_build(&ref, &dec, arr, n);
    CHECK(ref.n == 16 && ref.s[0].n == 1, "reference 16 x 256: sectors %u", ref.n);
    CHECK(level_for(&ref, arr, n, 2, 5, true) == VERIFY_CONTENT, "256-byte sectors verified");
    sectors2[300] ^= 0x80u;                                 /* byte 44 of sector 2 */
    n2 = make_track(250000u, 16, 2, 1, sectors2, deltas2, arr2, false);
    CHECK(level_for(&ref, arr2, n2, 0, 1, true) == 0, "256-byte sector changed -> fails");
    size_code = 0;
    CHECK_SUMMARY("test_write_verify");
}
