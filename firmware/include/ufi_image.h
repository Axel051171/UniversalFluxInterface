/**
 * UFI Flux Engine - disk image files on the SD NAND (1.16, pure C, no HAL)
 *
 * What the stand-alone write (UFI.CFG button_a=write / WRITE_START) needs to know about a
 * file before the drive moves: the kind from the extension and size, the geometry of
 * sector images, the SCP header and track headers, and the SCP cell -> tick conversion
 * (same rounding on absolute time as the host's scp_to_ticks()).
 */
#ifndef UFI_IMAGE_H
#define UFI_IMAGE_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    IMG_UNKNOWN = 0,
    IMG_SCP,            /* flux, revolution 0 of every track */
    IMG_SECTORS,        /* IBM MFM sector image (IMG/IMA/DSK/ST/D81 on a PC drive) */
    IMG_AMIGA,          /* ADF (11 or 22 sectors) */
    IMG_CBM,            /* D64/D71/D81 through an IEC drive */
} image_kind_t;

typedef struct {
    image_kind_t kind;
    uint8_t cyls, heads, spt;
    uint32_t rate_bps;          /* 250000 at 300 rpm; a 360K image gets 300000 in a 360 rpm drive */
    uint8_t gap3;
    bool head_id_swap;          /* Commodore 1581 */
    bool double_step;           /* 40-cylinder image: two steps per track in an 80-track drive */
    bool rate_by_rpm;           /* 360K: choose 250k/300k from the measured revolution */
    uint8_t cbm_kind;           /* 41, 71, 81 */
    uint8_t cbm_tracks;         /* 35/40/70/80 */
} image_geo_t;

/* name = 8.3 file name (case-insensitive extension), size in bytes */
image_kind_t image_identify(const char* name, uint32_t size, image_geo_t* g);

/* byte offset of a track's sector data in a sector image */
uint32_t image_track_offset(const image_geo_t* g, uint8_t cyl, uint8_t head);

/* 1541/1571/1581 sectors on track t (1-based) */
uint8_t image_cbm_spt(uint8_t cbm_kind, uint8_t t);

/* SCP file header (16 bytes) */
typedef struct {
    uint8_t revs;
    uint8_t start, end;         /* SCP track numbers (cyl * 2 + head) */
    uint8_t heads;              /* 0 both, 1 side 0 only, 2 side 1 only */
    bool index;                 /* flags bit 0: index cued */
    bool ok;
} scp_header_t;
bool scp_parse_header(const uint8_t* hdr, scp_header_t* h);

/* TRK header: revolution 0 of the track (index time in cells, flux words, byte offset
 * from the TRK header) */
bool scp_parse_track(const uint8_t* trk, uint8_t revs, uint8_t rev, uint8_t track_no,
                     uint32_t* index_cells, uint32_t* words, uint32_t* offset);

/* SCP cells (u16 big endian, 0 = +65536 to the next) -> 275 MHz deltas appended to out.
 * State across calls: *acc_cells (cells since the index), *prev_ticks, *pending (overflow
 * cells carried into the next value).  Returns the number of deltas written; stops at max. */
uint32_t scp_cells_to_deltas(const uint8_t* words, uint32_t n_words, uint32_t* out, uint32_t max,
                             uint32_t* n_out, uint32_t* acc_cells, uint32_t* prev_ticks,
                             uint32_t* pending);

#endif /* UFI_IMAGE_H */
