/**
 * UFI Flux Engine - disk image files (see ufi_image.h)
 */
#include "ufi_image.h"
#include <string.h>

static bool ext_is(const char* name, const char* ext)
{
    const char* dot = strrchr(name, '.');
    if (!dot) {
        return false;
    }
    for (int i = 0; i < 4; i++) {
        char c = dot[1 + i];
        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != ext[i]) {
            return false;
        }
        if (c == 0) {
            return true;
        }
    }
    return false;
}

uint8_t image_cbm_spt(uint8_t cbm_kind, uint8_t t)
{
    if (cbm_kind == 81u) {
        return 40u;
    }
    if (cbm_kind == 71u && t > 35u) {
        t = (uint8_t)(t - 35u);
    }
    return t <= 17u ? 21u : t <= 24u ? 19u : t <= 30u ? 18u : 17u;
}

/* PC / Atari / 1581 sector layouts by file size */
static bool sector_geometry(uint32_t size, image_geo_t* g)
{
    static const struct { uint32_t size; uint8_t cyls, heads, spt, gap3; uint32_t rate; bool dstep, by_rpm; } t[] = {
        {1474560u, 80, 2, 18, 0x6C, 500000u, false, false},   /* 1.44M */
        {737280u,  80, 2,  9, 0x50, 250000u, false, false},   /* 720K / ST */
        {1228800u, 80, 2, 15, 0x54, 500000u, false, false},   /* 1.2M (360 rpm drive) */
        {368640u,  40, 2,  9, 0x50, 250000u, true,  true},    /* 360K: double step, rate by rpm */
        {184320u,  40, 1,  9, 0x50, 250000u, true,  true},    /* 180K */
        {163840u,  40, 1,  8, 0x50, 250000u, true,  true},    /* 160K */
        {327680u,  40, 2,  8, 0x50, 250000u, true,  true},    /* 320K */
        {1720320u, 80, 2, 21, 0x0C, 500000u, false, false},   /* DMF 1.68M */
        {819200u,  80, 2, 10, 0x28, 250000u, false, false},   /* ST 800K / 1581 (swap by extension) */
        {829440u,  81, 2, 10, 0x28, 250000u, false, false},   /* ST 810K */
        {2949120u, 80, 2, 36, 0x53, 1000000u, false, false},  /* ED 2.88M (write pulse: refused later) */
    };
    for (uint32_t i = 0; i < sizeof(t) / sizeof(t[0]); i++) {
        if (t[i].size == size) {
            g->cyls = t[i].cyls;
            g->heads = t[i].heads;
            g->spt = t[i].spt;
            g->gap3 = t[i].gap3;
            g->rate_bps = t[i].rate;
            g->double_step = t[i].dstep;
            g->rate_by_rpm = t[i].by_rpm;
            return true;
        }
    }
    return false;
}

image_kind_t image_identify(const char* name, uint32_t size, image_geo_t* g)
{
    memset(g, 0, sizeof(*g));
    if (ext_is(name, "SCP")) {
        g->kind = IMG_SCP;
        return g->kind;
    }
    if (ext_is(name, "HFE")) {
        g->kind = IMG_HFE;              /* geometry from the HFE header */
        return g->kind;
    }
    if (ext_is(name, "ADF")) {
        if (size == 901120u) {
            g->cyls = 80; g->heads = 2; g->spt = 11; g->rate_bps = 250000u;
        } else if (size == 1802240u) {
            g->cyls = 80; g->heads = 2; g->spt = 22; g->rate_bps = 500000u;
        } else {
            return IMG_UNKNOWN;
        }
        g->kind = IMG_AMIGA;
        return g->kind;
    }
    if (ext_is(name, "D64") || ext_is(name, "D71")) {
        g->kind = IMG_CBM;
        g->cbm_kind = ext_is(name, "D71") ? 71u : 41u;
        if (size == 174848u || size == 174848u + 683u) {
            g->cbm_tracks = 35;
        } else if (size == 196608u || size == 196608u + 768u) {
            g->cbm_tracks = 40;
        } else if (g->cbm_kind == 71u && (size == 349696u || size == 349696u + 1366u)) {
            g->cbm_tracks = 70;
        } else {
            return IMG_UNKNOWN;
        }
        return g->kind;
    }
    if (ext_is(name, "D81")) {
        if (size != 819200u && size != 819200u + 3200u) {
            return IMG_UNKNOWN;
        }
        /* geometry for both uses: IEC (cbm) and a PC drive (1581 MFM layout, side id swapped) */
        g->kind = IMG_CBM;
        g->cbm_kind = 81u;
        g->cbm_tracks = 80;
        g->cyls = 80; g->heads = 2; g->spt = 10; g->gap3 = 0x23; g->rate_bps = 250000u;
        g->head_id_swap = true;
        return g->kind;
    }
    /* IMG / IMA / DSK / ST and anything else: a plain sector image by size */
    if (sector_geometry(size, g)) {
        g->kind = IMG_SECTORS;
        return g->kind;
    }
    return IMG_UNKNOWN;
}

uint32_t image_track_offset(const image_geo_t* g, uint8_t cyl, uint8_t head)
{
    return ((uint32_t)cyl * g->heads + head) * (uint32_t)g->spt * 512u;
}

bool scp_parse_header(const uint8_t* hdr, scp_header_t* h)
{
    memset(h, 0, sizeof(*h));
    if (hdr[0] != 'S' || hdr[1] != 'C' || hdr[2] != 'P') {
        return false;
    }
    h->revs = hdr[5];
    h->start = hdr[6];
    h->end = hdr[7];
    h->index = (hdr[8] & 0x01u) != 0;
    h->heads = hdr[10];
    h->ok = h->revs >= 1u && h->end >= h->start && h->end < 168u && h->heads <= 2u &&
            !(hdr[8] & 0x40u) && (hdr[9] == 0u || hdr[9] == 16u) && hdr[11] == 0u;
    return h->ok;
}

static uint32_t le32(const uint8_t* p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

bool scp_parse_track(const uint8_t* trk, uint8_t revs, uint8_t rev, uint8_t track_no,
                     uint32_t* index_cells, uint32_t* words, uint32_t* offset)
{
    if (trk[0] != 'T' || trk[1] != 'R' || trk[2] != 'K' || trk[3] != track_no || rev >= revs) {
        return false;
    }
    const uint8_t* e = trk + 4u + 12u * rev;
    *index_cells = le32(e);
    *words = le32(e + 4);
    *offset = le32(e + 8);
    return *words > 0u && *offset >= 4u + 12u * revs;
}

uint32_t scp_cells_to_deltas(const uint8_t* w, uint32_t n_words, uint32_t* out, uint32_t max,
                             uint32_t* n_out, uint32_t* acc_cells, uint32_t* prev_ticks,
                             uint32_t* pending)
{
    uint32_t made = 0;
    for (uint32_t i = 0; i < n_words && *n_out < max; i++) {
        const uint32_t c = ((uint32_t)w[2 * i] << 8) | w[2 * i + 1];
        const uint64_t total = (uint64_t)*acc_cells + *pending + (c ? c : 0x10000u);
        if ((total * 55u + 4u) / 8u > UINT32_MAX) {
            return UINT32_MAX;                    /* refuse timestamp/overflow wrap */
        }
        if (c == 0u) {
            *pending += 0x10000u;               /* overflow: the next value is 65536 later */
            continue;
        }
        *acc_cells += c + *pending;
        *pending = 0;
        /* round(acc * 275 MHz / 40 MHz) on absolute time: no drift over the revolution */
        const uint32_t now = (uint32_t)(((uint64_t)*acc_cells * 55u + 4u) / 8u);
        out[(*n_out)++] = now - *prev_ticks;
        *prev_ticks = now;
        made++;
    }
    return made;
}


/* ============================================================================
 * HFE (1.17)
 * ============================================================================ */

#define HFE_TICK_HZ     275000000u

static uint16_t le16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

bool hfe_parse_header(const uint8_t* hdr, hfe_header_t* h)
{
    memset(h, 0, sizeof(*h));
    h->v3=memcmp(hdr,"HXCHFEV3",8)==0;
    if ((!h->v3 && memcmp(hdr, "HXCPICFE", 8) != 0) || hdr[8] != 0u) {
        return false;
    }
    h->tracks = hdr[9];
    h->sides = hdr[10];
    h->encoding = hdr[11];
    h->rate_bps = (uint32_t)le16(&hdr[12]) * 1000u;
    h->rpm = le16(&hdr[14]);
    h->interface_mode = hdr[16];
    h->lut_block = le16(&hdr[18]);
    h->double_step = (hdr[21] == 0u);
    return h->tracks >= 1u && h->tracks <= 128u && h->sides >= 1u && h->sides <= 2u &&
           h->rate_bps >= 125000u && h->rate_bps <= 1000000u && h->lut_block >= 1u;
}

uint8_t hfe_interface_mode(uint32_t rate_bps, uint8_t encoding)
{
    if (encoding == HFE_ENC_AMIGA_MFM) {
        return rate_bps >= 500000u ? HFE_IF_AMIGA_HD : HFE_IF_AMIGA_DD;
    }
    return rate_bps >= 1000000u ? HFE_IF_IBMPC_ED : rate_bps >= 500000u ? HFE_IF_IBMPC_HD : HFE_IF_IBMPC_DD;
}

void hfe_make_header(uint8_t* hdr, uint8_t tracks, uint8_t sides, uint8_t encoding, uint32_t rate_bps,
                     uint16_t rpm, bool double_step)
{
    memset(hdr, 0xFF, HFE_BLOCK);
    memcpy(hdr, "HXCPICFE", 8);
    const uint16_t kbps = (uint16_t)((rate_bps + 500u) / 1000u);
    hdr[8] = 0;                                 /* format revision */
    hdr[9] = tracks;
    hdr[10] = sides;
    hdr[11] = encoding;
    hdr[12] = (uint8_t)kbps;
    hdr[13] = (uint8_t)(kbps >> 8);
    hdr[14] = (uint8_t)rpm;
    hdr[15] = (uint8_t)(rpm >> 8);
    hdr[16] = hfe_interface_mode(rate_bps, encoding);
    hdr[17] = 1;                                /* dnu */
    hdr[18] = 1;                                /* track table in block 1 */
    hdr[19] = 0;
    hdr[20] = 0xFF;                             /* write allowed */
    hdr[21] = double_step ? 0x00u : 0xFFu;      /* single step */
    /* 22..25: track 0 alternate encoding off (0xFF) */
}

void hfe_lut_entry(const uint8_t* e, uint16_t* block, uint16_t* len)
{
    *block = le16(e);
    *len = le16(e + 2);
}

void hfe_make_lut_entry(uint8_t* e, uint16_t block, uint16_t len)
{
    e[0] = (uint8_t)block;
    e[1] = (uint8_t)(block >> 8);
    e[2] = (uint8_t)len;
    e[3] = (uint8_t)(len >> 8);
}

uint32_t hfe_fit_cell16(uint32_t rate_bps, uint32_t cells, uint32_t rev_ticks, uint32_t* permille)
{
    const uint64_t nominal16 = ((uint64_t)HFE_TICK_HZ << 16) / (2u * (uint64_t)rate_bps);
    uint32_t pm = 1000;
    if (rev_ticks && cells) {
        const uint64_t track = (uint64_t)cells * HFE_TICK_HZ / (2u * (uint64_t)rate_bps);
        pm = (uint32_t)(((uint64_t)rev_ticks * 985u + track / 2u) / track);
        if (pm < 970u) pm = 970u;
        if (pm > 1030u) pm = 1030u;
    }
    if (permille) {
        *permille = pm;
    }
    return (uint32_t)((nominal16 * pm + 500u) / 1000u);
}

uint32_t hfe_bits_to_deltas(const uint8_t* bytes, uint32_t n_bytes, uint32_t cell16, uint32_t* out,
                            uint32_t max, uint32_t* n_out, uint32_t* cells, uint32_t* prev_ticks)
{
    uint32_t made = 0;
    for (uint32_t i = 0; i < n_bytes && *n_out < max; i++) {
        const uint8_t b = bytes[i];
        for (uint32_t k = 0; k < 8u && *n_out < max; k++) {
            (*cells)++;
            if (b & (1u << k)) {
                const uint32_t now = (uint32_t)(((uint64_t)*cells * cell16 + 32768u) >> 16);
                out[(*n_out)++] = now - *prev_ticks;
                *prev_ticks = now;
                made++;
            }
        }
    }
    return made;
}

void hfe_pll_init(hfe_pll_t* p, uint32_t rate_bps)
{
    p->nominal = (uint32_t)(((uint64_t)HFE_TICK_HZ * 256u) / (2u * (uint64_t)rate_bps));
    p->period = p->nominal;
    p->pmin = p->nominal - p->nominal / 8u;
    p->pmax = p->nominal + p->nominal / 8u;
    p->phase = 0;
}

uint32_t hfe_pll_cells(hfe_pll_t* p, uint32_t ticks)
{
    const int64_t t = (int64_t)ticks * 256 + p->phase;
    int64_t n = (t + p->period / 2) / p->period;
    if (n < 1) {
        n = 1;
    }
    if (n > 8) {                            /* gap or damage: keep the cells, no adaptation */
        p->phase = 0;
        return (uint32_t)n;
    }
    const int64_t err = t - n * (int64_t)p->period;
    int64_t per = (int64_t)p->period + err / (n * 16);
    if (per < p->pmin) per = p->pmin;
    if (per > p->pmax) per = p->pmax;
    p->period = (uint32_t)per;
    p->phase = (int32_t)(err / 2);
    return (uint32_t)n;
}

uint32_t hfe_put_cells(uint8_t* buf, uint32_t cap_bits, uint32_t* nbits, uint32_t n, bool mark)
{
    uint32_t done = 0;
    while (done < n && *nbits < cap_bits) {
        if ((*nbits & 7u) == 0u) {
            buf[*nbits >> 3] = 0;
        }
        done++;
        if (done == n && mark) {
            buf[*nbits >> 3] |= (uint8_t)(1u << (*nbits & 7u));
        }
        (*nbits)++;
    }
    return done;
}
