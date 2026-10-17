/**
 * UFI Flux Engine - write verification with evidence levels (firmware 1.15)
 *
 * Pure functions (no HAL), shared with the native tests.  A WRITE that asks for
 * verification gets one of these levels in its END frame:
 *   1 write_completed      timer/DMA finished without a hardware error
 *   2 readback_plausible   the track reads back with a plausible transition count only
 *   3 content_verified     every sector of the written track was found again with the
 *                          same data (CRC-32) and valid CRCs
 *   4 unverifiable         (reserved: no reference and no plausible read-back)
 *
 * Reference: the packed periods of the track about to be written are decoded (IBM MFM,
 * then Amiga) at the data rate guessed from the shortest common period. The u32
 * extended path additionally tries IBM FM, HP MMFM and Commodore GCR. Sector ids and
 * data CRC-32 (Amiga: including 16 label bytes) go into a small table, so the flux
 * store may be reused for read-back. Valid conflicting duplicates fail verification.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "ufi_mfm.h"

#define VERIFY_MAX_SECTORS  64u

enum {
    VERIFY_NONE = 0, VERIFY_WRITE_COMPLETED = 1, VERIFY_READBACK_PLAUSIBLE = 2,
    VERIFY_CONTENT = 3, VERIFY_UNVERIFIABLE = 4,
};

typedef struct {
    uint8_t c, h, r, n;         /* ID field; n = size code (128 << n data bytes) */
    uint32_t crc;               /* CRC-32: IBM 128 << n bytes; Amiga 512 data + 16 label */
} verify_sector_t;

typedef struct {
    verify_sector_t s[VERIFY_MAX_SECTORS];
    uint8_t n;                  /* reference sectors (0 = track not decodable: no content check) */
    bool overflow;              /* more than VERIFY_MAX_SECTORS: content check not possible */
    bool amiga;
    uint8_t format; /* 0 legacy caller, 1 IBM MFM, 2 Amiga, 3 FM, 4 HP MMFM, 5 CBM GCR */
    mfm_decoder_t* source_decoder; /* only used during reference construction */
    bool conflicting;          /* checksum-valid duplicate IDs with different content */
    uint32_t rate_bps;          /* guessed data rate, 0 = unknown */
} verify_ref_t;

/* per-sector outcome of the read-back (op 40 report, 1.18) */
enum {
    VERIFY_SECTOR_MISSING = 0,  /* never read back with a usable header */
    VERIFY_SECTOR_OK = 1,       /* CRC valid and same content */
    VERIFY_SECTOR_BAD_CRC = 2,  /* read back with a CRC/checksum error only */
    VERIFY_SECTOR_DIFFERENT = 3 /* read back CRC-valid with different content */
};
typedef struct {
    const verify_ref_t* ref;
    mfm_decoder_t* dec;
    uint64_t found;             /* bit i: reference sector i read back with the same data */
    uint8_t bad_crc;            /* reference sectors read back with a CRC error (capped) */
    bool mismatch;             /* any checksum-valid reference sector differs */
    uint8_t state[VERIFY_MAX_SECTORS];  /* VERIFY_SECTOR_* per reference sector; ok wins, then different, then bad CRC */
} verify_match_t;

uint32_t ufi_crc32(uint32_t crc, const uint8_t* p, uint32_t n);     /* init 0, IEEE, final xor done */

/* data rate from the shortest common period (ticks - 1 values as in the ARR array);
 * one of 250000, 300000, 500000, 1000000 or 0 */
uint32_t verify_guess_rate(const uint16_t* arr, uint32_t n);

/* build the reference from the packed track (fed twice: sectors across the splice) */
void verify_ref_build(verify_ref_t* ref, mfm_decoder_t* dec, const uint16_t* arr, uint32_t n);
bool verify_ref_build_u32(verify_ref_t* ref, mfm_decoder_t* dec, const uint32_t* deltas,
                          uint32_t n, uint32_t index_ticks, bool (*poll)(void));
void verify_match_accept(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok);

/* match the read-back: feed every interval (ticks), then ask for the level */
void verify_match_begin(verify_match_t* m, const verify_ref_t* ref, mfm_decoder_t* dec);
void verify_match_interval(verify_match_t* m, uint32_t ticks);
/* count_ok = transition count of the read-back within tolerance.  Returns the level
 * (VERIFY_CONTENT / VERIFY_READBACK_PLAUSIBLE) or 0 when the verification failed. */
uint8_t verify_match_result(const verify_match_t* m, bool count_ok);
/* Exact declared CHRN set: rejects a partial reference before any disk write. */
bool verify_ref_expected(const verify_ref_t* ref, uint64_t sectors, uint8_t c, uint8_t h, uint8_t n);
