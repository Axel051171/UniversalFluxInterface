/**
 * UFI Flux Engine - MFM sector codecs
 *
 *  - IBM System/34 MFM (PC 360K-2.88M, DMF, Atari ST, Commodore 1581): sectors with ID
 *    field (C H R N) and data field, A1 sync, CRC-16/CCITT
 *  - Amiga trackdisk: 11 sectors per track (880K, 250 kbit/s) or 22 (HD 1.76M,
 *    500 kbit/s), 0x4489 0x4489 sync, odd/even split longwords, XOR checksums
 *  - IBM FM (128/256/512/1024-byte sectors), HP 9895 MMFM (256-byte sectors,
 *    physical byte order, zero-based sector numbers). Read-side only.
 *  - Commodore 1541/1571 GCR: four channel rates, XOR checksums and disk ID.
 *    No GCR sector encoder; extended native WRITE supports content references.
 *  - Macintosh 400K/800K GCR: 12 tags + 512 payload + header format byte.
 * Pure C, no HAL: flux intervals in TIM2 ticks (275 MHz) in, sectors out, and whole tracks
 * from sector data to flux intervals for the write path.
 */
#ifndef UFI_MFM_H
#define UFI_MFM_H

#include <stdbool.h>
#include <stdint.h>

#define MFM_TICK_HZ         275000000u
#define MFM_MAX_SECTOR      1024u
#define AMIGA_SECTORS       11u     /* DD 880K */
#define AMIGA_SECTORS_HD    22u     /* HD 1.76M (1.16) */
#define AMIGA_RAW_LONGS     270u    /* info 2 + label 8 + 2 checksums x2 + data 256 */

typedef struct {
    uint8_t c, h, r, n;
} mfm_id_t;

/* called for every sector found.  IBM: after a valid ID field.  Amiga: id = {cyl,
 * head, sector + 1, 2} from the track number in the header, crc_ok = both checksums.
 * Amiga data[0..511] is payload, data[512..527] contains the 16 label bytes,
 * data[528] is the sectors-until-gap header field. Macintosh: data[0..11] tags,
 * data[12..523] payload, data[524] format byte; N=2, sectors zero-based. Apple II 5.25
 * (format 7): id = {track, 0, physical sector 0-15, 1}, data[0..255] payload, volume in
 * the decoder's disk_id; DOS 3.3 / ProDOS logical order is applied by the host. */
typedef void (*mfm_sector_cb)(void* ctx, const mfm_id_t* id, const uint8_t* data, bool crc_ok);

typedef struct {
    /* clock recovery: cell period in 1/256 tick, limited to +-12 % of nominal */
    uint32_t nominal, period, pmin, pmax;
    int32_t phase;
    /* raw cell shift register and field state */
    uint64_t sr;
    bool amiga;
    uint8_t gcr_ones,gcr_half,gcr_value;
    uint16_t disk_id;
    uint8_t encoding; /* 1 IBM MFM, 2 Amiga MFM, 3 FM, 4 HP, 5 CBM, 6 Macintosh GCR, 7 Apple II 5.25 GCR */
    bool field_bad, previous_transition;
    uint8_t sector_flags, id_flags, last_mark;
    uint32_t code_errors, marks, margin_errors;
    uint16_t margin_permille; /* 0 disabled; residual threshold relative to half-cell */
    bool check_encoding; /* optional for existing MFM decoders; always on for FM/MMFM */
    uint8_t state, mark;
    uint16_t cells, want, got;
    uint16_t crc;
    mfm_id_t id;
    bool id_ok;
    uint32_t id_age;            /* cells since the last good ID field */
    uint8_t buf[MFM_MAX_SECTOR + 2];
    uint32_t raw[AMIGA_RAW_LONGS];
    mfm_sector_cb cb;
    void* ctx;
    uint32_t ids_seen;          /* good ID fields / good Amiga headers (format detection) */
    uint8_t head_ids;           /* IBM: bit h set for every head number seen in IDs */
    uint8_t period_divisor, phase_divisor;
    uint32_t header_errors, data_errors;
} mfm_decoder_t;

uint16_t mfm_crc16(uint16_t crc, const uint8_t* p, uint32_t n);

/* rate_bps: 250000 (DD), 300000 (DD in a 360 rpm drive), 500000 (HD), 1000000 (ED) */
void mfm_dec_init(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx);
void mfm_dec_init_amiga(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx);
void mfm_dec_interval(mfm_decoder_t* d, uint32_t ticks);
void mfm_dec_init_format(mfm_decoder_t* d, uint8_t format, uint32_t rate_bps, mfm_sector_cb cb, void* ctx);
void mfm_dec_profile(mfm_decoder_t* d, uint8_t profile); /* 0 standard, 1 fast, 2 damped */

typedef struct {
    uint32_t rate_bps;
    uint32_t track_cells;       /* cells to fill (the last gap pads up to here) */
    uint8_t spt;                /* sectors numbered 1..spt (512 bytes unless size_code) */
    uint8_t gap3;               /* nominal; reduced (and the IAM dropped) if it does not fit */
    bool head_id_swap;          /* Commodore 1581: ID side field = 1 - physical head */
    uint8_t size_code;          /* sector size 128 << code; 0 = 512 bytes (1.17) */
} mfm_track_fmt_t;

/* Encode one IBM track starting at the index: deltas[] = flux intervals in ticks.
 * Returns the number of intervals, 0 if max is too small or the sectors do not fit. */
uint32_t mfm_encode_track(const mfm_track_fmt_t* f, uint8_t cyl, uint8_t head,
                          const uint8_t* sectors, uint32_t* deltas, uint32_t max);

/* Encode one Amiga track (spt = 11 or 22 x 512 bytes), track number = cyl * 2 + head */
uint32_t mfm_encode_amiga_track(uint32_t rate_bps, uint32_t track_cells, uint8_t cyl,
                                uint8_t head, uint8_t spt, const uint8_t* sectors,
                                uint32_t* deltas, uint32_t max);

#endif /* UFI_MFM_H */
