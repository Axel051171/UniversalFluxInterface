/* Native tests of the image file helpers (firmware/src/ufi_image.c): kind and geometry
 * from name + size, Commodore zones, SCP headers and the cell -> tick conversion (same
 * rounding as the host's scp_to_ticks), HFE header/table/cells (1.17). */
#include "ufi_image.h"
#include "check.h"
#include <string.h>

static image_geo_t g;
static uint32_t out[16];

int main(void)
{
    CHECK(image_identify("DISK.IMG", 1474560u, &g) == IMG_SECTORS && g.cyls == 80 && g.heads == 2 &&
          g.spt == 18 && g.rate_bps == 500000u && !g.double_step, "1.44M image");
    CHECK(image_identify("disk.ima", 737280u, &g) == IMG_SECTORS && g.spt == 9 && g.rate_bps == 250000u, "720K .ima");
    CHECK(image_identify("A.DSK", 368640u, &g) == IMG_SECTORS && g.cyls == 40 && g.double_step && g.rate_by_rpm,
          "360K: double step, rate by rpm");
    CHECK(image_identify("X.ST", 829440u, &g) == IMG_SECTORS && g.cyls == 81 && g.spt == 10, "ST 810K");
    CHECK(image_identify("DMF.IMG", 1720320u, &g) == IMG_SECTORS && g.spt == 21 && g.gap3 == 0x0C, "DMF");
    CHECK(image_identify("GAME.ADF", 901120u, &g) == IMG_AMIGA && g.spt == 11 && g.rate_bps == 250000u, "ADF 880K");
    CHECK(image_identify("GAME.ADF", 1802240u, &g) == IMG_AMIGA && g.spt == 22 && g.rate_bps == 500000u, "ADF HD");
    CHECK(image_identify("GAME.ADF", 901121u, &g) == IMG_UNKNOWN, "odd ADF size refused");
    CHECK(image_identify("C64.D64", 174848u, &g) == IMG_CBM && g.cbm_kind == 41 && g.cbm_tracks == 35, "D64 35");
    CHECK(image_identify("C64.D64", 196608u + 768u, &g) == IMG_CBM && g.cbm_tracks == 40, "D64 40 + error info");
    CHECK(image_identify("C64.D71", 349696u, &g) == IMG_CBM && g.cbm_kind == 71 && g.cbm_tracks == 70, "D71");
    CHECK(image_identify("C64.D81", 819200u, &g) == IMG_CBM && g.cbm_kind == 81 && g.cbm_tracks == 80 &&
          g.spt == 10 && g.head_id_swap && g.rate_bps == 250000u, "D81: IEC and 1581 MFM geometry");
    CHECK(image_identify("FLUX.scp", 12345u, &g) == IMG_SCP, "SCP by extension");
    CHECK(image_identify("NOPE.IMG", 1000u, &g) == IMG_UNKNOWN, "unknown size");
    CHECK(image_identify("NOEXT", 737280u, &g) == IMG_SECTORS, "no extension: by size");

    image_identify("DISK.IMG", 1474560u, &g);
    CHECK(image_track_offset(&g, 0, 1) == 18u * 512u && image_track_offset(&g, 1, 0) == 36u * 512u, "track offsets");
    CHECK(image_cbm_spt(41, 1) == 21 && image_cbm_spt(41, 18) == 19 && image_cbm_spt(41, 25) == 18 &&
          image_cbm_spt(41, 31) == 17 && image_cbm_spt(41, 36) == 17, "1541 zones, tracks 36-40 = 17");
    CHECK(image_cbm_spt(71, 36) == 21 && image_cbm_spt(71, 70) == 17 && image_cbm_spt(81, 5) == 40, "1571 mirror, 1581");
    uint32_t total = 0;
    for (uint8_t t = 1; t <= 40; t++) total += image_cbm_spt(41, t);
    CHECK(total == 768u, "40-track D64 = 768 blocks (%u)", total);

    /* SCP header */
    uint8_t hdr[16] = {'S', 'C', 'P', 0x22, 0x80, 3, 0, 159, 0x01, 0, 0, 0, 0, 0, 0, 0};
    scp_header_t h;
    CHECK(scp_parse_header(hdr, &h) && h.revs == 3 && h.start == 0 && h.end == 159 && h.heads == 0 && h.index, "SCP header");
    hdr[10] = 2;
    CHECK(scp_parse_header(hdr, &h) && h.heads == 2, "SCP side 1 only");
    hdr[0] = 'X';
    CHECK(!scp_parse_header(hdr, &h), "bad magic refused");
    hdr[0] = 'S'; hdr[5] = 0;
    CHECK(!scp_parse_header(hdr, &h), "0 revolutions refused");
    hdr[5] = 3; hdr[9] = 8;
    CHECK(!scp_parse_header(hdr, &h), "unsupported SCP cell width refused");
    hdr[9] = 0; hdr[11] = 1;
    CHECK(!scp_parse_header(hdr, &h), "SD writer refuses unsupported SCP resolution");
    hdr[11] = 0; hdr[8] |= 0x40;
    CHECK(!scp_parse_header(hdr, &h), "extended SCP table refused");

    /* TRK header: 3 revolutions, revolution 0 at offset 40 */
    uint8_t trk[4 + 12 * 3] = {'T', 'R', 'K', 7};
    const uint32_t idx = 8000000u, words = 50000u, off = 4u + 12u * 3u;
    memcpy(&trk[4], &idx, 4); memcpy(&trk[8], &words, 4); memcpy(&trk[12], &off, 4);
    uint32_t a, b, c;
    CHECK(scp_parse_track(trk, 3, 0, 7, &a, &b, &c) && a == idx && b == words && c == off, "TRK header rev 0");
    CHECK(!scp_parse_track(trk, 3, 0, 8, &a, &b, &c), "TRK wrong track number refused");
    CHECK(!scp_parse_track(trk, 3, 3, 7, &a, &b, &c), "TRK rev beyond count refused");

    /* cells -> ticks: 1000, overflow, 500, 2000 cells */
    const uint8_t cells[] = {0x03, 0xE8, 0x00, 0x00, 0x01, 0xF4, 0x07, 0xD0};
    uint32_t n = 0, acc = 0, prev = 0, pend = 0;
    const uint32_t made = scp_cells_to_deltas(cells, 4, out, 8, &n, &acc, &prev, &pend);
    CHECK(made == 3 && n == 3 && out[0] == 6875u && out[1] == 453998u && out[2] == 13750u,
          "cells -> deltas (%u: %u %u %u)", n, out[0], out[1], out[2]);
    CHECK(acc == 69036u && prev == 474623u && pend == 0, "conversion state");
    /* against the host formula round(acc * 275 / 40) over a long run: no drift */
    uint32_t sum = 0, acc2 = 0, prev2 = 0;
    bool same = true;
    acc = 0; prev = 0; pend = 0;
    for (uint32_t i = 0; i < 20000u && same; i++) {
        const uint16_t cv = (uint16_t)(1 + (i * 7919u) % 3000u);
        const uint8_t w[2] = {(uint8_t)(cv >> 8), (uint8_t)cv};
        n = 0;
        scp_cells_to_deltas(w, 1, out, 1, &n, &acc, &prev, &pend);
        acc2 += cv;
        const uint32_t now = (uint32_t)(((uint64_t)acc2 * 275u + 20u) / 40u);
        same = (out[0] == now - prev2) && (out[0] >= 1u);
        prev2 = now;
        sum += out[0];
    }
    CHECK(same && sum == prev && sum > 0u, "20000 cells match the host rounding, sum %u", sum);
    /* max stops the conversion mid-way and the rest can continue */
    n = 0; acc = 0; prev = 0; pend = 0;
    CHECK(scp_cells_to_deltas(cells, 4, out, 2, &n, &acc, &prev, &pend) == 2 && n == 2, "stops at max");
    n = 0; acc = 0; prev = 0; pend = UINT32_MAX - 10u;
    CHECK(scp_cells_to_deltas(cells, 4, out, 16, &n, &acc, &prev, &pend) == UINT32_MAX && n == 0,
          "SCP pending overflow cannot wrap into a short write pulse");

    /* ---- HFE (1.17) ---- */
    CHECK(image_identify("DISK.hfe", 500u, &g) == IMG_HFE, "HFE by extension");
    static uint8_t hh[512];
    hfe_make_header(hh, 80, 2, HFE_ENC_ISOIBM_MFM, 250000u, 300, false);
    hfe_header_t hf;
    hh[8] = 1;
    CHECK(!hfe_parse_header(hh, &hf), "HFEv2 opcodes must not become raw cells");
    hh[8] = 255;
    CHECK(!hfe_parse_header(hh, &hf), "unknown HFE revision refused");
    hh[8] = 0;
    CHECK(hfe_parse_header(hh, &hf) && hf.tracks == 80 && hf.sides == 2 && hf.encoding == 0 &&
          hf.rate_bps == 250000u && hf.rpm == 300 && hf.lut_block == 1 && !hf.double_step &&
          hf.interface_mode == HFE_IF_IBMPC_DD && hh[20] == 0xFF && hh[511] == 0xFF, "HFE header round trip");
    hfe_make_header(hh, 40, 1, HFE_ENC_AMIGA_MFM, 500000u, 0, true);
    CHECK(hfe_parse_header(hh, &hf) && hf.double_step && hf.interface_mode == HFE_IF_AMIGA_HD && hf.rpm == 0,
          "HFE header: double step, Amiga HD");
    CHECK(hfe_interface_mode(1000000u, 0) == HFE_IF_IBMPC_ED && hfe_interface_mode(300000u, 0) == HFE_IF_IBMPC_DD &&
          hfe_interface_mode(250000u, HFE_ENC_AMIGA_MFM) == HFE_IF_AMIGA_DD, "interface modes");
    hh[0] = 'X';
    CHECK(!hfe_parse_header(hh, &hf), "HFE bad magic refused");
    hh[0] = 'H'; hh[10] = 3;
    CHECK(!hfe_parse_header(hh, &hf), "HFE 3 sides refused");
    uint8_t le[4];
    uint16_t blk = 0, ln = 0;
    hfe_make_lut_entry(le, 0x1234, 25000);
    hfe_lut_entry(le, &blk, &ln);
    CHECK(le[0] == 0x34 && le[1] == 0x12 && blk == 0x1234 && ln == 25000, "LUT entry little endian");

    /* cell length: 250 kbit/s = 550 ticks exactly; 300 kbit/s = 458.33 */
    uint32_t pm = 0;
    CHECK(hfe_fit_cell16(250000u, 0, 0, &pm) == 550u << 16 && pm == 1000, "nominal cell 250k");
    CHECK(hfe_fit_cell16(300000u, 0, 0, &pm) == 30037333u, "nominal cell 300k (%u)", hfe_fit_cell16(300000u, 0, 0, NULL));
    /* 100000 cells at 250k = 55 000 000 ticks = exactly one 300 rpm revolution: fit to 98.5 % */
    CHECK(hfe_fit_cell16(250000u, 100000u, 55000000u, &pm) == (uint32_t)(((550ull << 16) * 985u + 500u) / 1000u) && pm == 985,
          "fit: full revolution -> 0.985 (%u)", pm);
    CHECK(hfe_fit_cell16(250000u, 100000u, 50000000u, &pm) == (uint32_t)(((550ull << 16) * 970u + 500u) / 1000u) && pm == 970,
          "fit: too long -> limited to 0.97 (%u)", pm);
    CHECK(hfe_fit_cell16(250000u, 50000u, 55000000u, &pm) == (uint32_t)(((550ull << 16) * 1030u + 500u) / 1000u) && pm == 1030,
          "fit: short track -> limited to 1.03 (%u)", pm);
    hfe_fit_cell16(250000u, 99000u, 55000000u, &pm);
    CHECK(pm == 995, "fit: 99 %% track -> 0.995 (%u)", pm);

    /* bits -> deltas: 0x12 = bits 1 and 4 set (LSB first) -> transitions at cells 2 and 5 */
    const uint8_t bits[2] = {0x12, 0x80};
    uint32_t cl = 0, pv = 0;
    n = 0;
    CHECK(hfe_bits_to_deltas(bits, 2, 550u << 16, out, 8, &n, &cl, &pv) == 3 && n == 3 &&
          out[0] == 1100u && out[1] == 1650u && out[2] == 11u * 550u && cl == 16 && pv == 16u * 550u,
          "bits -> deltas (%u %u %u)", out[0], out[1], out[2]);
    /* 300 kbit/s: 120000 cells (one revolution) end within one tick of the exact time */
    static uint8_t ones[15000];
    memset(ones, 0x80, sizeof(ones));                           /* one transition per 8 cells */
    static uint32_t big[15000];
    cl = 0; pv = 0; n = 0;
    hfe_bits_to_deltas(ones, sizeof(ones), 30037333u, big, 15000, &n, &cl, &pv);
    CHECK(n == 15000 && cl == 120000u && pv >= 54999999u && pv <= 55000001u, "300k: no drift over a revolution (%u)", pv);

    /* flux -> cells -> flux round trip with jitter: an MFM-like pattern of 2/3/4-cell gaps */
    static uint8_t hb[4096], hb2[4096];
    uint32_t nb = 0, rng = 7, stored = 0;
    for (uint32_t i = 0; i < 9000u; i++) {
        rng = rng * 1103515245u + 12345u;
        const uint32_t k = 2u + (rng >> 16) % 3u;
        if (hfe_put_cells(hb, sizeof(hb) * 8u, &nb, k, true) != k) break;
        stored++;
    }
    CHECK(stored == 9000u && nb > 20000u && nb <= sizeof(hb) * 8u, "pattern stored (%u bits)", nb);
    const uint32_t nbytes = (nb + 7u) / 8u;
    static uint32_t fl[12000];
    cl = 0; pv = 0; n = 0;
    hfe_bits_to_deltas(hb, nbytes, 550u << 16, fl, 12000, &n, &cl, &pv);
    CHECK(n == stored, "deltas from the pattern (%u)", n);
    hfe_pll_t pll;
    hfe_pll_init(&pll, 250000u);
    uint32_t nb2 = 0;
    same = true;
    rng = 99;
    for (uint32_t i = 0; i < n && same; i++) {
        rng = rng * 1103515245u + 12345u;
        const int32_t j = (int32_t)((rng >> 8) % 13u) - 6;     /* +-6 % jitter on every interval */
        const uint32_t t = (uint32_t)((int64_t)fl[i] + (int64_t)fl[i] * j / 100);
        const uint32_t c = hfe_pll_cells(&pll, t);
        same = hfe_put_cells(hb2, sizeof(hb2) * 8u, &nb2, c, true) == c;
    }
    CHECK(same && nb2 == nb && memcmp(hb, hb2, nbytes) == 0, "PLL: jittered flux -> same bits (%u/%u)", nb2, nb);
    /* a drifting clock (+5 %) is followed */
    hfe_pll_init(&pll, 250000u);
    nb2 = 0;
    for (uint32_t i = 0; i < n; i++) {
        hfe_put_cells(hb2, sizeof(hb2) * 8u, &nb2, hfe_pll_cells(&pll, fl[i] * 105u / 100u), true);
    }
    CHECK(nb2 == nb && memcmp(hb, hb2, nbytes) == 0, "PLL: 5 %% slow drive -> same bits");
    /* long gap: 50 cells stay 50 zeros, no resync */
    hfe_pll_init(&pll, 250000u);
    CHECK(hfe_pll_cells(&pll, 50u * 550u) == 50u && pll.period == pll.nominal, "long gap kept as cells");
    /* a block boundary: 5 cells into a 4-bit buffer -> 4 stored, the 1 comes with the rest */
    uint8_t small[1];
    nb = 0;
    CHECK(hfe_put_cells(small, 4, &nb, 5, true) == 4 && nb == 4 && small[0] == 0, "block full: zeros stored, 1 pending");
    nb = 0;
    CHECK(hfe_put_cells(small, 4, &nb, 1, true) == 1 && small[0] == 0x01, "rest of the cells: the 1 lands first");
    CHECK(hfe_put_cells(small, 8, &nb, 3, false) == 3 && nb == 4 && small[0] == 0x01, "zero cells: no mark");
    CHECK_SUMMARY("test_image");
}
