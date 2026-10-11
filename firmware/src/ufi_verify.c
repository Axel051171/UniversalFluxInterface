/**
 * UFI Flux Engine - write verification with evidence levels (see ufi_verify.h)
 */

#include "ufi_verify.h"
#include <string.h>

#ifndef FLUX_TIMER_FREQ
#define FLUX_TIMER_FREQ 275000000UL
#endif

uint32_t ufi_crc32(uint32_t crc, const uint8_t* p, uint32_t n)
{
    crc = ~crc;
    for (uint32_t i = 0; i < n; i++) {
        crc ^= p[i];
        for (int k = 0; k < 8; k++) {
            crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1u)));
        }
    }
    return ~crc;
}

/* ---- data rate ------------------------------------------------------------------ */

uint32_t verify_guess_rate(const uint16_t* arr, uint32_t n)
{
    /* histogram of periods in 32-tick bins up to 4096 ticks; the lowest bin that holds
     * at least 5 % of the periods is the shortest common interval = one bit time */
    uint16_t bins[128];
    memset(bins, 0, sizeof(bins));
    uint32_t total = 0;
    for (uint32_t i = 0; i < n; i++) {
        const uint32_t p = (uint32_t)arr[i] + 1u;
        if (p < 4096u) {
            if (bins[p / 32u] < 0xFFFFu) bins[p / 32u]++;
            total++;
        }
    }
    if (total < 100u) {
        return 0;
    }
    uint32_t shortest = 0;
    for (uint32_t b = 0; b < 128u; b++) {
        if ((uint32_t)bins[b] * 20u >= total) {
            shortest = b * 32u + 16u;
            break;
        }
    }
    if (!shortest) {
        return 0;
    }
    static const uint32_t rates[] = {250000u, 300000u, 500000u, 1000000u};
    for (int k = 0; k < 4; k++) {
        const uint32_t two_cells = (uint32_t)(FLUX_TIMER_FREQ / rates[k]);   /* one bit time = shortest MFM interval */
        const uint32_t tol = two_cells / 8u;                /* +-12 % */
        if (shortest + tol >= two_cells && shortest <= two_cells + tol) {
            return rates[k];
        }
    }
    return 0;
}

/* ---- reference ------------------------------------------------------------------ */

static void ref_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    verify_ref_t* ref = p;
    if (!crc_ok) {
        return;
    }
    for (uint8_t i = 0; i < ref->n; i++) {              /* fed twice: keep one copy */
        if (ref->s[i].c == id->c && ref->s[i].h == id->h && ref->s[i].r == id->r) {
            return;
        }
    }
    if (ref->n >= VERIFY_MAX_SECTORS) {
        ref->overflow = true;
        return;
    }
    ref->s[ref->n].c = id->c;
    ref->s[ref->n].h = id->h;
    ref->s[ref->n].r = id->r;
    ref->s[ref->n].crc = ufi_crc32(0, data, 512);
    ref->n++;
}

static void feed_twice(mfm_decoder_t* dec, const uint16_t* arr, uint32_t n)
{
    for (int pass = 0; pass < 2; pass++) {
        for (uint32_t i = 0; i < n; i++) {
            mfm_dec_interval(dec, (uint32_t)arr[i] + 1u);
        }
    }
}

void verify_ref_build(verify_ref_t* ref, mfm_decoder_t* dec, const uint16_t* arr, uint32_t n)
{
    memset(ref, 0, sizeof(*ref));
    ref->rate_bps = verify_guess_rate(arr, n);
    if (!ref->rate_bps) {
        return;
    }
    mfm_dec_init(dec, ref->rate_bps, ref_sector, ref);
    feed_twice(dec, arr, n);
    if (ref->n == 0) {
        mfm_dec_init_amiga(dec, ref->rate_bps, ref_sector, ref);
        feed_twice(dec, arr, n);
        ref->amiga = ref->n > 0;
    }
}

/* ---- match ---------------------------------------------------------------------- */

static void match_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    verify_match_t* m = p;
    for (uint8_t i = 0; i < m->ref->n; i++) {
        const verify_sector_t* s = &m->ref->s[i];
        if (s->c != id->c || s->h != id->h || s->r != id->r) {
            continue;
        }
        if (!crc_ok) {
            if (m->bad_crc < 255u) m->bad_crc++;
        } else if (ufi_crc32(0, data, 512) == s->crc) {
            m->found |= 1ull << i;
        }
        return;
    }
}

void verify_match_begin(verify_match_t* m, const verify_ref_t* ref, mfm_decoder_t* dec)
{
    memset(m, 0, sizeof(*m));
    m->ref = ref;
    m->dec = dec;
    if (ref->n) {
        if (ref->amiga) {
            mfm_dec_init_amiga(dec, ref->rate_bps, match_sector, m);
        } else {
            mfm_dec_init(dec, ref->rate_bps, match_sector, m);
        }
    }
}

void verify_match_interval(verify_match_t* m, uint32_t ticks)
{
    if (m->ref->n) {
        mfm_dec_interval(m->dec, ticks);
    }
}

uint8_t verify_match_result(const verify_match_t* m, bool count_ok)
{
    const verify_ref_t* ref = m->ref;
    if (ref->n == 0 || ref->overflow) {
        return count_ok ? VERIFY_READBACK_PLAUSIBLE : 0;   /* no content reference */
    }
    const uint64_t all = (ref->n >= 64u) ? ~0ull : ((1ull << ref->n) - 1ull);
    return (m->found == all) ? VERIFY_CONTENT : 0;        /* a lost or changed sector fails */
}
