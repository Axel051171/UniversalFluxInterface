/**
 * UFI Flux Engine - disk image files on the SD NAND (1.16, pure C, no HAL; HFE 1.17)
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
    IMG_HFE,            /* HxC HFE v1 bit cells, one revolution per track (1.17) */
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
/* Returns UINT32_MAX if the accumulated timestamp exceeds the tick range. */
uint32_t scp_cells_to_deltas(const uint8_t* words, uint32_t n_words, uint32_t* out, uint32_t max,
                             uint32_t* n_out, uint32_t* acc_cells, uint32_t* prev_ticks,
                             uint32_t* pending);

/* ============================================================================
 * HxC HFE v1 (1.17): one MFM cell per bit, LSB of every byte first, 512-byte blocks
 * holding 256 bytes of side 0 and 256 bytes of side 1, one revolution per track.
 * Header block 0, track table block 1 (offset in blocks, length in bytes for both sides).
 * ============================================================================ */

#define HFE_BLOCK               512u
#define HFE_ENC_ISOIBM_MFM      0u
#define HFE_ENC_AMIGA_MFM       1u
#define HFE_ENC_ISOIBM_FM       2u
#define HFE_ENC_UNKNOWN         0xFFu
#define HFE_IF_IBMPC_DD         0u
#define HFE_IF_IBMPC_HD         1u
#define HFE_IF_AMIGA_DD         4u
#define HFE_IF_AMIGA_HD         5u
#define HFE_IF_IBMPC_ED         8u

typedef struct {
    uint8_t tracks, sides, encoding, interface_mode;
    uint32_t rate_bps;          /* header bit rate (kbit/s) x 1000 */
    uint16_t rpm;               /* 0 = unknown */
    uint16_t lut_block;         /* track table position in 512-byte blocks */
    bool double_step;           /* single_step == 0: 40-track image */
    bool v3;                    /* HXCHFEV3, explicit bytecode timing */
} hfe_header_t;

/* 512-byte header -> fields; false if magic/counts are unusable */
bool hfe_parse_header(const uint8_t* hdr, hfe_header_t* h);
/* 512-byte header for a new file (track table in block 1) */
void hfe_make_header(uint8_t* hdr, uint8_t tracks, uint8_t sides, uint8_t encoding, uint32_t rate_bps,
                     uint16_t rpm, bool double_step);
/* interface mode byte for the header: IBM PC DD/HD/ED or Amiga DD/HD by rate and encoding */
uint8_t hfe_interface_mode(uint32_t rate_bps, uint8_t encoding);
/* 4-byte track table entry <-> block offset and byte length (both sides together) */
void hfe_lut_entry(const uint8_t* e, uint16_t* block, uint16_t* len);
void hfe_make_lut_entry(uint8_t* e, uint16_t block, uint16_t len);

/* cell length in 1/65536 tick: nominal for rate_bps, or scaled so that `cells` cells fill
 * 98.5 % of rev_ticks (limited to 0.97..1.03 x nominal; rev_ticks 0 = nominal).
 * *permille receives the applied factor x 1000. */
uint32_t hfe_fit_cell16(uint32_t rate_bps, uint32_t cells, uint32_t rev_ticks, uint32_t* permille);

/* bits (LSB first) -> deltas appended to out (rounding on absolute time).  State across
 * calls: *cells (cells since the index), *prev_ticks.  Returns the deltas written. */
uint32_t hfe_bits_to_deltas(const uint8_t* bytes, uint32_t n_bytes, uint32_t cell16, uint32_t* out,
                            uint32_t max, uint32_t* n_out, uint32_t* cells, uint32_t* prev_ticks);

/* flux -> cells: the clock recovery of the MFM decoder (1/16 of the error into the
 * period, half of the residual into the phase, +-12 %), but long gaps stay as zeros */
typedef struct {
    uint32_t nominal, period, pmin, pmax;   /* 1/256 tick */
    int32_t phase;
} hfe_pll_t;
void hfe_pll_init(hfe_pll_t* p, uint32_t rate_bps);
/* one flux interval -> number of cells it spans (>= 1); the last of them carries the 1 */
uint32_t hfe_pll_cells(hfe_pll_t* p, uint32_t ticks);
/* append n cells at bit *nbits of buf (LSB first, cap_bits in total): n - 1 zeros and a 1
 * (mark), or n zeros (the gap before the index).  Returns how many of the n were stored;
 * fewer when buf is full - flush, set *nbits = 0 and call again with the rest. */
uint32_t hfe_put_cells(uint8_t* buf, uint32_t cap_bits, uint32_t* nbits, uint32_t n, bool mark);

#endif /* UFI_IMAGE_H */
