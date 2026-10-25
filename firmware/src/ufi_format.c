/**
 * UFI Flux Engine - FORMAT (UFI v2 0x26, firmware 1.14)
 *
 * The firmware lays out one IBM MFM (or Amiga trackdisk) track itself with the encoder
 * of ufi_mfm.c, writes it from the index pulse (ufi_write_local) and, on request, reads
 * it back and counts the sectors whose ID and data CRCs are good.  No flux upload: a
 * whole disk formats in about a minute, also from a slow host.
 *
 * Track length: the revolution time is measured once per motor session (DIAG_RPM style,
 * two index periods) and cached for 60 s; the track fills 98 % of it so the write splice
 * stays in gap 4b at drive speed tolerance (same rule as the USB floppy mode).
 */

#include "ufi_firmware.h"
#include "ufi_mfm.h"
#include <string.h>

extern capture_context_t g_capture;

#define FORMAT_REV_CACHE_MS     60000u
#define FORMAT_VERIFY_REVS      2u

static uint32_t rev_ticks, rev_at;          /* measured revolution, HAL tick of the measurement */
static drive_type_t rev_drive;

typedef struct {
    uint64_t good;
    uint8_t cyl, fill;
} vctx_t;

static void v_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    (void)data;
    vctx_t* v = p;
    if (!crc_ok || id->c != v->cyl || id->r < 1u || id->r > 63u) {
        return;
    }
    for (uint32_t i = 0; i < 512u; i++) {         /* the data must be the fill byte */
        if (data[i] != v->fill) {
            return;
        }
    }
    v->good |= 1ull << (id->r - 1u);
}

/* nominal gap 3 of the common layouts (the encoder shortens it if the track is short) */
static uint8_t default_gap3(uint32_t rate, uint8_t spt, bool swap)
{
    if (rate == 1000000u && spt == 36u) return 0x53;    /* 2.88M */
    if (rate == 500000u && spt == 21u) return 0x0C;     /* DMF 1.68M */
    if (rate == 500000u && spt == 18u) return 0x6C;     /* 1.44M */
    if (rate == 500000u && spt == 15u) return 0x54;     /* 1.2M */
    if (spt == 10u) return swap ? 0x23 : 0x28;          /* 1581 / Atari ST 10 sectors */
    if (spt == 11u) return 0x02;                        /* Atari ST 11 sectors */
    return 0x50;                                        /* 9 sectors: 360K / 720K / ST */
}

int ufi_format_revolution(uint32_t* ticks)
{
    const drive_type_t d = ufi_drive_get_current();
    if (rev_ticks && rev_drive == d && HAL_GetTick() - rev_at < FORMAT_REV_CACHE_MS &&
        ufi_drive_motor_is_on()) {
        *ticks = rev_ticks;
        return UFI_OK;
    }
    diag_rpm_t r;
    const int ret = ufi_diag_rpm(2, &r);                /* starts the motor if needed */
    if (ret != UFI_OK) {
        return ret;
    }
    rev_ticks = r.period_avg;
    rev_at = HAL_GetTick();
    rev_drive = d;
    *ticks = rev_ticks;
    return UFI_OK;
}

/* read the track back: good = sectors 1..spt with valid CRCs (bit mask count) */
static int verify(const format_req_t* q, uint32_t rate, uint8_t* good)
{
    __attribute__((section(".dtcm"))) static mfm_decoder_t d;
    vctx_t v = {.cyl = q->track, .fill = q->fill};
    g_capture.state = CAPTURE_IDLE;
    int ret = ufi_capture_start(q->track, q->side, FORMAT_VERIFY_REVS, 0);
    if (ret != UFI_OK) {
        return ret;
    }
    const uint32_t t0 = HAL_GetTick();
    capture_state_t cs;
    while ((cs = ufi_capture_get_state()) != CAPTURE_COMPLETE && cs != CAPTURE_ERROR) {
        if (HAL_GetTick() - t0 > 3000u) {
            ufi_capture_abort();
            g_capture.state = CAPTURE_IDLE;
            return UFI_ERR_TIMEOUT;
        }
    }
    if (cs != CAPTURE_COMPLETE) {
        g_capture.state = CAPTURE_IDLE;
        return UFI_ERR_NO_INDEX;
    }
    if (q->flags & FORMAT_AMIGA) {
        mfm_dec_init_amiga(&d, rate, v_sector, &v);
    } else {
        mfm_dec_init(&d, rate, v_sector, &v);
    }
    uint32_t carry = 0;
    for (uint8_t k = 0; k < ufi_flux_get_revolution_count(); k++) {
        const flux_revolution_t* r = ufi_flux_get_revolution(k);
        uint32_t prev = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            mfm_dec_interval(&d, r->samples[i].timestamp - prev + carry);
            carry = 0;
            prev = r->samples[i].timestamp;
        }
        carry = r->index_time - prev;
    }
    g_capture.state = CAPTURE_IDLE;
    uint8_t n = 0;
    for (uint64_t g = v.good; g; g &= g - 1u) {
        n++;
    }
    *good = n;
    return UFI_OK;
}

int ufi_format_track(const format_req_t* q, uint32_t* flux_count, uint8_t* good)
{
    *flux_count = 0;
    *good = 0xFF;
    const drive_type_t d = ufi_drive_get_current();
    if (d == DRIVE_NONE || d == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    const uint32_t rate = (uint32_t)q->rate_kbps * 1000u;
    const bool amiga = (q->flags & FORMAT_AMIGA) != 0;
    if (ufi_drive_is_apple() || q->track > 83u || q->side > 1u ||
        (rate != 250000u && rate != 300000u && rate != 500000u && rate != 1000000u) ||
        (amiga ? (q->spt != AMIGA_SECTORS && q->spt != AMIGA_SECTORS_HD)
               : (q->spt < 1u || q->spt > 36u))) {
        return UFI_ERR_BAD_ARGS;
    }
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        return UFI_ERR_WRITE_PROT;
    }
    if (g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING ||
        ufi_write_get_state() == WRITE_ACTIVE) {
        return UFI_ERR_BUSY;
    }

    uint32_t rev;
    int ret = ufi_format_revolution(&rev);
    if (ret != UFI_OK) {
        return ret;
    }
    const uint32_t cells = (uint32_t)(((uint64_t)rev * 2u * rate / FLUX_TIMER_FREQ) * 98u / 100u);

    /* sector data at the end of the flux store, the deltas grow from its start */
    uint32_t words = 0;
    uint32_t* store = ufi_flux_store(&words);
    ufi_flux_invalidate();
    const uint32_t data_words = ((uint32_t)q->spt * 512u + 3u) / 4u;
    if (words < data_words + 1024u) {
        return UFI_ERR_BUFFER_FULL;
    }
    uint8_t* data = (uint8_t*)(store + words - data_words);
    memset(data, q->fill, (size_t)q->spt * 512u);
    const uint32_t max = words - data_words - 16u;

    uint32_t n;
    if (amiga) {
        n = mfm_encode_amiga_track(rate, cells, q->track, q->side, q->spt, data, store, max);
    } else {
        const mfm_track_fmt_t f = {
            .rate_bps = rate, .track_cells = cells, .spt = q->spt,
            .gap3 = q->gap3 ? q->gap3 : default_gap3(rate, q->spt, (q->flags & FORMAT_HEAD_SWAP) != 0),
            .head_id_swap = (q->flags & FORMAT_HEAD_SWAP) != 0,
        };
        n = mfm_encode_track(&f, q->track, q->side, data, store, max);
    }
    if (n == 0) {
        return UFI_ERR_BUFFER_FULL;                 /* the sectors do not fit the revolution */
    }
    ret = ufi_write_local(q->track, q->side, n, false);
    if (ret != UFI_OK) {
        return ret;
    }
    *flux_count = n;
    if (q->flags & FORMAT_VERIFY) {
        ret = verify(q, rate, good);
    }
    led_set(&PIN_LED_FDD, false);
    return ret;
}
