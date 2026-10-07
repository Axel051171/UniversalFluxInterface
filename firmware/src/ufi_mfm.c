/**
 * UFI Flux Engine - IBM MFM sector codec (see ufi_mfm.h)
 *
 * Decoder: digital PLL turns flux intervals into MFM cells; three A1 sync words
 * (raw 0x4489, missing clock) start a field: FE = ID (C H R N + CRC), FB/F8 = data
 * (128 << N bytes + CRC).  CRC-16/CCITT (0x1021, init 0xFFFF) over A1 A1 A1 + mark +
 * field; the CRC bytes included the remainder is 0.
 * Encoder: IBM System/34 track (gap 4a, IAM, per sector ID + data, gap 4b) as cells,
 * cells to flux intervals on absolute time so fractional cell periods do not drift.
 */

#include "ufi_mfm.h"
#include <string.h>

#define CRC_A1A1A1      0xCDB4u     /* CRC after the three A1 sync bytes */
#define SYNC3           0x448944894489ull
#define SYNC_A1         0x4489u
#define SYNC_C2         0x5224u
#define ID_DATA_CELLS   2000u       /* data field must follow its ID within this */

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
        }
    } else {
        if (d->cb) {
            d->cb(d->ctx, &d->id, d->buf, d->crc == 0);
        }
        d->id_ok = false;
    }
    d->state = S_SEARCH;
}

static void cell(mfm_decoder_t* d, uint32_t bit)
{
    d->sr = (d->sr << 1) | bit;
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

static void put_byte(enc_t* e, uint8_t b)
{
    for (int i = 7; i >= 0; i--) {
        const uint8_t bit = (b >> i) & 1u;
        put_cell(e, (!e->prev && !bit) ? 1u : 0u);     /* clock */
        put_cell(e, bit);
        e->prev = bit;
    }
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

uint32_t mfm_encode_track(const mfm_track_fmt_t* f, uint8_t cyl, uint8_t head,
                          const uint8_t* sectors, uint32_t* deltas, uint32_t max)
{
    /* gap4a 80 + sync 12 + IAM 4 + gap1 50, per sector 12+4+4+2 + 22 + 12+4+512+2 + gap3 */
    const uint32_t need = (80u + 12u + 4u + 50u + f->spt * (22u + 22u + 518u + f->gap3)) * 16u;
    if (need > f->track_cells) {
        return 0;
    }
    enc_t e = {.out = deltas, .max = max, .rate = f->rate_bps};

    put_run(&e, 0x4E, 80);
    put_run(&e, 0x00, 12);
    put_raw(&e, SYNC_C2);
    put_raw(&e, SYNC_C2);
    put_raw(&e, SYNC_C2);
    put_byte(&e, 0xFC);
    put_run(&e, 0x4E, 50);
    for (uint8_t s = 0; s < f->spt; s++) {
        const uint8_t id[4] = {cyl, head, (uint8_t)(s + 1u), 2u};
        put_crc_field(&e, 0xFE, id, 4);
        put_run(&e, 0x4E, 22);
        put_crc_field(&e, 0xFB, sectors + (uint32_t)s * 512u, 512);
        put_run(&e, 0x4E, f->gap3);
    }
    while (e.pos + 16u <= f->track_cells) {     /* gap 4b up to the track length */
        put_byte(&e, 0x4E);
    }
    return e.count <= max ? e.count : 0;
}
