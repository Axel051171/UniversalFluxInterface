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
    uint16_t tolerance;         /* MFM_TOL_* for formats 1-6; 0 after init (analysis sets it) */
    bool id_seen;               /* an ID field completed (valid or not): its size code is known */
    uint8_t profile;            /* PLL profile from mfm_dec_profile(); Apple statistics count profile 0 only */
    uint16_t nib_bits, nib_trail, sync_run, last_sync; /* format 7 nibble framing in the search state */
    bool last_ff;               /* the nibble just framed was FF (sync candidate) */
    uint8_t nib_prev1, nib_prev2; /* the two nibbles framed before the current one (prologue variants) */
} mfm_decoder_t;

/* Apple II (format 7) nibble statistics of the last analysis, ANALYSIS_JOB op 35 (1.18).
 * Wire format = memory layout, 178 bytes.  Counted by the profile-0 decoder only; a
 * revolution is selected by mfm_apple_stats_revolution() (quality_revolution). */
#define APPLE_STATS_REVS 8u
typedef struct {
    uint8_t schema;                 /* 1 */
    uint8_t revs;                   /* revolutions with counts, 0 = no format 7 analysis yet */
    struct {
        uint16_t nibbles;           /* nibbles framed in this revolution (fields and syncs) */
        uint16_t sync_nibbles;      /* 10-bit self-sync FF nibbles */
        uint16_t address_fields;    /* address prologues seen */
        uint16_t data_fields;       /* data prologues accepted */
        uint16_t orphan_headers;    /* valid headers without a data field before the next header or 4000 cells */
    } rev[APPLE_STATS_REVS];
    struct {
        uint8_t volume;             /* from the last header of this physical sector */
        uint8_t sync_before_addr;   /* consecutive sync nibbles in front of the address prologue (cap 255) */
        uint8_t header_to_data;     /* cells / 8 from header end to data prologue end (cap 255) */
        uint8_t sync_before_data;   /* consecutive sync nibbles in front of the data prologue */
        uint8_t flags;              /* sector_flags of the last data field (0x08/0x20/0x40/0x80) */
        uint8_t seen;               /* revolution mask of data fields for this sector */
    } sector[16];
    /* schema 2 (appended, 124 bytes): timing bits, prologue variants, sector positions */
    struct {
        uint16_t trail1, trail2, trail3;    /* FF nibbles followed by 1, 2, >= 3 zero cells (>= 3 = timing bits) */
    } sync[APPLE_STATS_REVS];
    struct {
        uint8_t third;              /* third nibble after D5 AA that is neither configured prologue */
        uint8_t reserved;
        uint16_t count;
    } variant[4];                   /* the four most frequent, 0 count = unused */
    uint16_t position[16];          /* cells from revolution start to the address prologue of the last header (cap 65535) */
} apple_stats_t;                    /* 274 bytes, every field at its natural alignment */
void mfm_apple_stats_reset(void);
void mfm_apple_stats_revolution(uint8_t rev);
const apple_stats_t* mfm_apple_stats(void);

/* Tolerance flags for the IBM MFM, Amiga, FM, HP MMFM, Commodore and Macintosh decoders
 * (1.18, ANALYSIS_JOB op 34).  Same bit values and meaning as the Apple II options; the
 * error counters keep counting, the per-sector MFM_SF_* bits name what was tolerated. */
#define MFM_TOL_KEEP_BAD_HEADER       0x0001u  /* ID/header with failed CRC or checksum is still used */
#define MFM_TOL_IGNORE_EPILOGUES      0x0002u  /* Macintosh: DE AA trailers not required */
#define MFM_TOL_ANY_TRACK             0x0004u  /* no geometry limits (track/sector ranges, Amiga format byte) */
#define MFM_TOL_DATA_WITHOUT_CHECKSUM 0x0008u  /* a failed data CRC/checksum does not clear crc_ok */
#define MFM_TOL_DATA_WITHOUT_HEADER   0x0010u  /* data field without a preceding valid ID: id c = r = 255 */
#define MFM_TOL_MASK                  0x001Fu
/* per-sector findings in mfm_decoder_t.sector_flags, every format (quality report flag byte);
 * bits 1 (deleted data mark), 2 (HP defect mark) and 4 (coding error) are older */
#define MFM_SF_NO_HEADER        0x08u   /* data field accepted without a valid ID field */
#define MFM_SF_HEADER_CHECKSUM  0x20u   /* ID/header CRC or checksum failed (sector kept by flag) */
#define MFM_SF_DATA_CHECKSUM    0x40u   /* data CRC/checksum failed */
#define MFM_SF_EPILOGUE         0x80u   /* a trailer/epilogue did not match (GCR formats) */
void mfm_set_tolerance(uint16_t flags);      /* value analysis decoders start with */
uint16_t mfm_get_tolerance(void);

/* Apple II 5.25-inch GCR (format 7) decoder options, 1.18: marks and tolerance flags
 * for copy-protected or damaged disks.  Wire format = memory layout (12 bytes). */
typedef struct __attribute__((packed)) {
    uint8_t addr_prologue[3];   /* default D5 AA 96 */
    uint8_t data_prologue[3];   /* default D5 AA AD */
    uint8_t addr_epilogue[2];   /* default DE AA */
    uint8_t data_epilogue[2];   /* default DE AA */
    uint16_t flags;             /* APPLE_GCR_* */
} apple_gcr_options_t;
#define APPLE_GCR_KEEP_BAD_ADDR_CHECKSUM  0x0001u  /* report the sector although the 4-and-4 checksum fails */
#define APPLE_GCR_IGNORE_EPILOGUES        0x0002u  /* do not require the epilogue bytes */
#define APPLE_GCR_ANY_TRACK               0x0004u  /* no track < 40 / sector < 16 limit (sector < 64) */
#define APPLE_GCR_DATA_WITHOUT_CHECKSUM   0x0008u  /* a wrong checksum nibble does not clear crc_ok */
#define APPLE_GCR_DATA_WITHOUT_HEADER     0x0010u  /* accept a data field without a preceding valid header */
#define APPLE_GCR_FIVE_AND_THREE          0x0020u  /* 13-sector DOS 3.2 data fields: 410 5-and-3 nibbles (address prologue usually D5 AA B5) */
#define APPLE_GCR_FLAG_MASK               0x003Fu
/* loadable 6-and-2 symbol table (op 36): 64 symbols, each >= 0x80 and distinct; NULL = standard */
bool mfm_apple_table_valid(const uint8_t table[64]);
void mfm_apple_set_table(const uint8_t table[64]);
const uint8_t* mfm_apple_get_table(void);
/* per-sector diagnostic bits in mfm_decoder_t.sector_flags (OR-ed into the quality report);
 * same values as MFM_SF_* so every format reports its findings in the same bits */
#define APPLE_GCR_SF_ADDR_CHECKSUM  0x20u   /* header checksum failed (sector kept by flag) */
#define APPLE_GCR_SF_DATA_CHECKSUM  0x40u   /* data checksum nibble failed */
#define APPLE_GCR_SF_EPILOGUE       0x80u   /* an epilogue did not match */
void mfm_apple_set_options(const apple_gcr_options_t* o);   /* NULL = defaults */
apple_gcr_options_t mfm_apple_get_options(void);
bool mfm_apple_options_valid(const apple_gcr_options_t* o);

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
