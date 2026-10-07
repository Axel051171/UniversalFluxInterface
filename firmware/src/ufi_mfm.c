/**
 * UFI Flux Engine - MFM sector codecs (see ufi_mfm.h)
 *
 * Decoder: digital PLL turns flux intervals into MFM cells.
 *  IBM: three A1 sync words (raw 0x4489, missing clock) start a field: FE = ID (C H R N
 *  + CRC), FB/F8 = data (128 << N bytes + CRC).  CRC-16/CCITT (0x1021, init 0xFFFF) over
 *  A1 A1 A1 + mark + field; the CRC bytes included the remainder is 0.
 *  Amiga: 0x4489 0x4489, then 270 raw longwords: info (format 0xFF, track, sector,
 *  sectors to gap), label, header and data checksum, 512 data bytes, each value as its
 *  odd bits then its even bits; checksums = XOR of the raw longs masked with 0x55555555.
 * Encoder: cells to flux intervals on absolute time so fractional cell periods do not
 * drift.  IBM: System/34 track; gap 3 shrinks (and the IAM is dropped) for dense formats
 * (DMF, 1581, Atari 10 sectors) so the track fits 98 % of a revolution.
 */

#include "ufi_mfm.h"
#include <string.h>

#define CRC_A1A1A1      0xCDB4u     /* CRC after the three A1 sync bytes */
#define SYNC3           0x448944894489ull
#define SYNC_A1         0x4489u
#define SYNC_C2         0x5224u
#define SYNC_AMIGA      0x44894489u
#define ID_DATA_CELLS   2000u       /* data field must follow its ID within this */
#define M55             0x55555555u

enum { S_SEARCH, S_MARK, S_FIELD };

uint16_t mfm_crc16(uint16_t crc, const uint8_t* p, uint32_t n)
{
    while (n--) {
        crc ^= (uint16_t)(*p++ << 8);
        for (int i = 0; i < 8; i++) {
            crc = (crc & 0x8000u) ? (uint16_t)((crc << 1) ^ 0x1021u) : (uint16_t)(crc << 1);
        }
    }
    return crc;
}

/* ============================================================================
 * decoder
 * ============================================================================ */

void mfm_dec_init(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx)
{
    memset(d, 0, sizeof(*d));
    d->nominal = (uint32_t)(((uint64_t)MFM_TICK_HZ * 256u) / (2u * rate_bps));
    d->period = d->nominal;
    d->pmin = d->nominal - d->nominal / 8u;
    d->pmax = d->nominal + d->nominal / 8u;
    d->cb = cb;
    d->ctx = ctx;
}

void mfm_dec_init_amiga(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx)
{
    mfm_dec_init(d, rate_bps, cb, ctx);
    d->amiga = true;
}

static uint8_t raw_to_byte(uint32_t raw)
{
    uint8_t b = 0;
    for (int i = 7; i >= 0; i--) {
        b = (uint8_t)((b << 1) | ((raw >> (2 * i)) & 1u));
    }
    return b;
}

static void field_byte(mfm_decoder_t* d, uint8_t b)
{
    d->crc = mfm_crc16(d->crc, &b, 1);
    if (d->state == S_MARK) {
        d->mark = b;
        d->got = 0;
        if (b == 0xFE) {
            d->want = 6;
        } else if ((b == 0xFB || b == 0xF8) && d->id_ok && d->id_age < ID_DATA_CELLS &&
                   d->id.n <= 3) {
            d->want = (uint16_t)((128u << d->id.n) + 2u);
        } else {
            d->state = S_SEARCH;
            return;
        }
        d->state = S_FIELD;
        return;
    }
    d->buf[d->got++] = b;
    if (d->got < d->want) {
        return;
    }
    if (d->mark == 0xFE) {
        d->id_ok = (d->crc == 0);
        if (d->id_ok) {
            d->id = (mfm_id_t){d->buf[0], d->buf[1], d->buf[2], d->buf[3]};
            d->id_age = 0;
            d->ids_seen++;
            if (d->id.h < 8u) {
                d->head_ids |= (uint8_t)(1u << d->id.h);
            }
        }
    } else {
        if (d->cb) {
            d->cb(d->ctx, &d->id, d->buf, d->crc == 0);
        }
        d->id_ok = false;
    }
    d->state = S_SEARCH;
}

static uint32_t odd_even(uint32_t odd, uint32_t even)
{
    return ((odd & M55) << 1) | (even & M55);
}

static void amiga_sector(mfm_decoder_t* d)
{
    const uint32_t* r = d->raw;
    const uint32_t info = odd_even(r[0], r[1]);
    const uint8_t track = (uint8_t)(info >> 16), sector = (uint8_t)(info >> 8);
    if ((info >> 24) != 0xFFu || sector >= AMIGA_SECTORS) {
        return;
    }
    uint32_t hsum = 0, dsum = 0;
    for (int i = 0; i < 10; i++) {
        hsum ^= r[i];
    }
    for (int i = 14; i < (int)AMIGA_RAW_LONGS; i++) {
        dsum ^= r[i];
    }
    const bool hdr_ok = (hsum & M55) == odd_even(r[10], r[11]);
    const bool data_ok = (dsum & M55) == odd_even(r[12], r[13]);
    if (!hdr_ok) {
        return;
    }
    d->ids_seen++;
    for (int i = 0; i < 128; i++) {
        const uint32_t v = odd_even(r[14 + i], r[14 + 128 + i]);
        d->buf[4 * i] = (uint8_t)(v >> 24);
        d->buf[4 * i + 1] = (uint8_t)(v >> 16);
        d->buf[4 * i + 2] = (uint8_t)(v >> 8);
        d->buf[4 * i + 3] = (uint8_t)v;
    }
    const mfm_id_t id = {(uint8_t)(track >> 1), (uint8_t)(track & 1u), (uint8_t)(sector + 1u), 2};
    if (d->cb) {
        d->cb(d->ctx, &id, d->buf, data_ok);
    }
}

static void amiga_cell(mfm_decoder_t* d)
{
    if (d->state == S_SEARCH) {
        if ((uint32_t)d->sr == SYNC_AMIGA) {
            d->state = S_FIELD;
            d->cells = 0;
            d->got = 0;
        }
        return;
    }
    if (++d->cells == 32u) {
        d->cells = 0;
        d->raw[d->got++] = (uint32_t)d->sr;
        if (d->got == 2u && (odd_even(d->raw[0], d->raw[1]) >> 24) != 0xFFu) {
            d->state = S_SEARCH;            /* not a sector header */
        } else if (d->got == AMIGA_RAW_LONGS) {
            amiga_sector(d);
            d->state = S_SEARCH;
        }
    }
}

static void cell(mfm_decoder_t* d, uint32_t bit)
{
    d->sr = (d->sr << 1) | bit;
    if (d->amiga) {
        amiga_cell(d);
        return;
    }
    d->id_age++;
    if (d->state == S_SEARCH) {
        if ((d->sr & 0xFFFFFFFFFFFFull) == SYNC3) {
            d->state = S_MARK;
            d->cells = 0;
            d->crc = CRC_A1A1A1;
        }
        return;
    }
    if (++d->cells == 16u) {
        d->cells = 0;
        field_byte(d, raw_to_byte((uint32_t)d->sr & 0xFFFFu));
    }
}

void mfm_dec_interval(mfm_decoder_t* d, uint32_t ticks)
{
    const int64_t t = (int64_t)ticks * 256 + d->phase;
    int64_t n = (t + d->period / 2) / d->period;
    if (n < 1) {
        n = 1;
    }
    if (n > 8) {                        /* no flux / damage: resynchronise */
        d->state = S_SEARCH;
        d->phase = 0;
        d->period = d->nominal;
        for (int i = 0; i < 8; i++) {
            cell(d, 0);
        }
        return;
    }
    /* frequency: 1/16 of the per-cell error; phase: keep half of the residual */
    const int64_t err = t - n * (int64_t)d->period;
    int64_t p = (int64_t)d->period + err / (n * 16);
    if (p < d->pmin) p = d->pmin;
    if (p > d->pmax) p = d->pmax;
    d->period = (uint32_t)p;
    d->phase = (int32_t)(err / 2);
    for (int64_t i = 1; i < n; i++) {
        cell(d, 0);
    }
    cell(d, 1);
}

/* ============================================================================
 * encoder
 * ============================================================================ */

typedef struct {
    uint32_t* out;
    uint32_t max, count;
    uint64_t pos;               /* cell index from the index pulse */
    uint64_t last_t;            /* tick time of the previous transition */
    uint32_t rate;
    uint8_t prev;               /* previous data bit */
} enc_t;

static void put_cell(enc_t* e, uint32_t bit)
{
    if (bit) {
        const uint64_t t = (e->pos * MFM_TICK_HZ) / (2u * e->rate);
        if (e->count < e->max) {
            e->out[e->count] = (uint32_t)(t - e->last_t);
        }
        e->count++;
        e->last_t = t;
    }
    e->pos++;
}

/* n data bits, MSB first, with MFM clock bits */
static void put_bits(enc_t* e, uint32_t v, int n)
{
    for (int i = n - 1; i >= 0; i--) {
        const uint8_t bit = (v >> i) & 1u;
        put_cell(e, (!e->prev && !bit) ? 1u : 0u);     /* clock */
        put_cell(e, bit);
        e->prev = bit;
    }
}

static void put_byte(enc_t* e, uint8_t b)
{
    put_bits(e, b, 8);
}

static void put_raw(enc_t* e, uint16_t raw)
{
    for (int i = 15; i >= 0; i--) {
        put_cell(e, (raw >> i) & 1u);
    }
    e->prev = raw & 1u;
}

static void put_run(enc_t* e, uint8_t b, uint32_t n)
{
    while (n--) {
        put_byte(e, b);
    }
}

static void put_crc_field(enc_t* e, uint8_t mark, const uint8_t* p, uint32_t n)
{
    put_run(e, 0x00, 12);
    put_raw(e, SYNC_A1);
    put_raw(e, SYNC_A1);
    put_raw(e, SYNC_A1);
    put_byte(e, mark);
    uint16_t crc = mfm_crc16(CRC_A1A1A1, &mark, 1);
    crc = mfm_crc16(crc, p, n);
    for (uint32_t i = 0; i < n; i++) {
        put_byte(e, p[i]);
    }
    put_byte(e, (uint8_t)(crc >> 8));
    put_byte(e, (uint8_t)crc);
}

#define IBM_SECTOR_BYTES    (22u + 22u + 530u)  /* ID part, gap 2, data part (no gap 3) */
#define IBM_HEAD_IAM        (80u + 12u + 4u + 50u)
#define IBM_HEAD_NO_IAM     16u

uint32_t mfm_encode_track(const mfm_track_fmt_t* f, uint8_t cyl, uint8_t head,
                          const uint8_t* sectors, uint32_t* deltas, uint32_t max)
{
    /* layout: standard (IAM, nominal gap 3) if it fits, else shorter gap 3, else no IAM */
    const uint32_t avail = f->track_cells / 16u;
    uint32_t gap3 = 0;
    bool iam = false;
    for (int with_iam = 1; with_iam >= 0; with_iam--) {
        const uint32_t hdr = with_iam ? IBM_HEAD_IAM : IBM_HEAD_NO_IAM;
        if (avail <= hdr || !f->spt) {
            continue;
        }
        const uint32_t per = (avail - hdr) / f->spt;
        if (per >= IBM_SECTOR_BYTES + 8u) {
            gap3 = per - IBM_SECTOR_BYTES;
            if (gap3 > f->gap3) {
                gap3 = f->gap3;
            }
            iam = with_iam;
            break;
        }
    }
    if (gap3 == 0) {
        return 0;                       /* does not fit (e.g. Atari 11 sectors): read only */
    }
    enc_t e = {.out = deltas, .max = max, .rate = f->rate_bps};

    if (iam) {
        put_run(&e, 0x4E, 80);
        put_run(&e, 0x00, 12);
        put_raw(&e, SYNC_C2);
        put_raw(&e, SYNC_C2);
        put_raw(&e, SYNC_C2);
        put_byte(&e, 0xFC);
        put_run(&e, 0x4E, 50);
    } else {
        put_run(&e, 0x4E, IBM_HEAD_NO_IAM);
    }
    const uint8_t id_head = f->head_id_swap ? (uint8_t)(head ^ 1u) : head;
    for (uint8_t s = 0; s < f->spt; s++) {
        const uint8_t id[4] = {cyl, id_head, (uint8_t)(s + 1u), 2u};
        put_crc_field(&e, 0xFE, id, 4);
        put_run(&e, 0x4E, 22);
        put_crc_field(&e, 0xFB, sectors + (uint32_t)s * 512u, 512);
        put_run(&e, 0x4E, gap3);
    }
    while (e.pos + 16u <= f->track_cells) {     /* gap 4b up to the track length */
        put_byte(&e, 0x4E);
    }
    return e.count <= max ? e.count : 0;
}

/* Amiga: odd bits (31, 29 .. 1) or even bits (30 .. 0) of v as 16 data bits */
static uint16_t odd16(uint32_t v)
{
    uint16_t w = 0;
    for (int i = 15; i >= 0; i--) {
        w = (uint16_t)((w << 1) | ((v >> (2 * i + 1)) & 1u));
    }
    return w;
}

static uint16_t even16(uint32_t v)
{
    uint16_t w = 0;
    for (int i = 15; i >= 0; i--) {
        w = (uint16_t)((w << 1) | ((v >> (2 * i)) & 1u));
    }
    return w;
}

/* data bits of a 16-bit word spread to the even positions (= raw long & 0x55555555) */
static uint32_t spread16(uint16_t w)
{
    uint32_t v = 0;
    for (int i = 0; i < 16; i++) {
        v |= ((uint32_t)(w >> i) & 1u) << (2 * i);
    }
    return v;
}

static void put_word(enc_t* e, uint16_t w, uint32_t* sum)
{
    put_bits(e, w, 16);
    if (sum) {
        *sum ^= spread16(w);
    }
}

static void put_long_odd_even(enc_t* e, uint32_t v, uint32_t* sum)
{
    put_word(e, odd16(v), sum);
    put_word(e, even16(v), sum);
}

uint32_t mfm_encode_amiga_track(uint32_t rate_bps, uint32_t track_cells, uint8_t cyl,
                                uint8_t head, const uint8_t* sectors, uint32_t* deltas,
                                uint32_t max)
{
    if (track_cells < AMIGA_SECTORS * 1088u * 8u) {
        return 0;
    }
    enc_t e = {.out = deltas, .max = max, .rate = rate_bps};
    const uint8_t track = (uint8_t)(cyl * 2u + head);
    for (uint8_t s = 0; s < AMIGA_SECTORS; s++) {
        const uint8_t* d = sectors + (uint32_t)s * 512u;
        put_run(&e, 0x00, 2);
        put_raw(&e, SYNC_A1);
        put_raw(&e, SYNC_A1);
        uint32_t hsum = 0, dsum = 0;
        const uint32_t info = 0xFF000000u | ((uint32_t)track << 16) | ((uint32_t)s << 8) |
                              (AMIGA_SECTORS - s);
        put_long_odd_even(&e, info, &hsum);
        for (int i = 0; i < 4; i++) {
            put_word(&e, 0, &hsum);                   /* label odd halves */
        }
        for (int i = 0; i < 4; i++) {
            put_word(&e, 0, &hsum);                   /* label even halves */
        }
        uint32_t longs[128];
        for (int i = 0; i < 128; i++) {
            longs[i] = ((uint32_t)d[4 * i] << 24) | ((uint32_t)d[4 * i + 1] << 16) |
                       ((uint32_t)d[4 * i + 2] << 8) | d[4 * i + 3];
            dsum ^= spread16(odd16(longs[i])) ^ spread16(even16(longs[i]));
        }
        put_long_odd_even(&e, hsum, NULL);
        put_long_odd_even(&e, dsum, NULL);
        for (int i = 0; i < 128; i++) {
            put_word(&e, odd16(longs[i]), NULL);
        }
        for (int i = 0; i < 128; i++) {
            put_word(&e, even16(longs[i]), NULL);
        }
    }
    while (e.pos + 16u <= track_cells) {               /* track gap */
        put_byte(&e, 0x00);
    }
    return e.count <= max ? e.count : 0;
}
