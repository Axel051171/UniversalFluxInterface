/* Native tests of the image file helpers (firmware/src/ufi_image.c): kind and geometry
 * from name + size, Commodore zones, SCP headers and the cell -> tick conversion (same
 * rounding as the host's scp_to_ticks). */
#include "ufi_image.h"
#include "check.h"
#include <string.h>

static image_geo_t g;

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
    uint32_t out[8], n = 0, acc = 0, prev = 0, pend = 0;
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
    CHECK_SUMMARY("test_image");
}
