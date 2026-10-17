/* Bounded on-device sector evidence. No heap, no HAL, no media verdict. */
#pragma once
#include "ufi_verify.h"
#define QUALITY_REVS 8u
#define QUALITY_SECTORS 64u
#define QUALITY_BINS 128u
#define QUALITY_WIRE_SIZE 1824u
#define QUALITY_EXTRA_SIZE 1920u
#define QUALITY_DIAG_SIZE 304u
#define QUALITY_MAX_WIRE (QUALITY_WIRE_SIZE + QUALITY_EXTRA_SIZE + QUALITY_DIAG_SIZE)
#define QUALITY_EVENTS 48u
#define QUALITY_CANDIDATES 24u
#define RECOG_PROFILE_PRESENT 1u
#define RECOG_FORMAT_OBSERVED 2u
#define RECOG_SECTOR_SET_CONFIRMED 4u
#define RECOG_GEOMETRY_UNCONFIRMED 8u
#define RECOG_AMBIGUOUS 16u
#define RECOG_HEADER_POSITION_MISMATCH 32u
#define RECOG_CONTENT_CONFIRMED 64u
#define RECOG_INCOMPLETE 128u
#define RECOG_POSITION_CHECKED 256u
#define RECOG_PROFILE_DECLARED 512u
#define QUALITY_LOCAL_WINDOWS 32u
#define QUALITY_LOCAL_SIZE (32u + QUALITY_LOCAL_WINDOWS * 40u)
typedef struct {
    uint64_t sum;
    uint32_t count,min,max,rejected,errors,conflicts;
    uint16_t multiples;
} quality_local_t;
#define QUALITY_FEATURE_CODING  0x0001u
#define QUALITY_FEATURE_MARKS   0x0002u
#define QUALITY_FEATURE_MARGIN  0x0004u
#define QUALITY_FEATURE_MASK    0x0007u
typedef struct {
    uint8_t flags,format,limit,window,bits;
    uint8_t side,position;      /* request byte 1: side = bit 0; position = 0 or 0x80 | quarter << 4 (Apple, 1.18) */
    uint16_t features,margin_permille;
    uint32_t rate,glitch,gap,cell,pattern,budget_ms;
    uint16_t report_mask;
    uint32_t compare_tolerance;
} quality_options_t;
typedef struct {
    uint32_t count,min,max,short_count,long_count,end;
    uint64_t sum,squares;
} quality_timing_t;
typedef struct {
    quality_options_t options;
    quality_timing_t timing[QUALITY_REVS];
    uint32_t diagnostics[QUALITY_REVS][3][3]; /* coding, address marks, margin */
    uint8_t events[QUALITY_EVENTS][32];
    uint32_t dropped,shift,position,index,history[4],field_start;
    uint8_t count,history_count,valid_bits,field_rev,completed,saturated;
    uint8_t profile_complete[3];
} quality_extra_t;
bool quality_options(const uint8_t* payload, uint16_t length, quality_options_t* options);
typedef struct {
    mfm_id_t id;
    uint32_t crc;
    uint8_t seen, nominal, bad, flags; /* flags bit0: conflicting valid content */
} quality_sector_t;
typedef struct { uint64_t seen; uint32_t header_errors, data_errors; } quality_attempt_t;
typedef struct {
    uint8_t format, revs, n, status; /* 0 unknown, 1 IBM MFM, 2 Amiga MFM, 3 IBM FM, 4 HP MMFM, 5 CBM GCR */
    uint32_t rate, dropped;
    quality_sector_t sectors[QUALITY_SECTORS];
    quality_attempt_t attempts[QUALITY_REVS][3];
    uint32_t histogram[QUALITY_BINS];
    uint32_t revolution_ticks[QUALITY_REVS], flux_count[QUALITY_REVS], max_gap[QUALITY_REVS];
    uint16_t candidates[QUALITY_CANDIDATES]; /* +18..23 Macintosh GCR */
    uint8_t profiles, ambiguous;
    uint8_t track, side, position;  /* position byte of the capture (report offset 19) */
    uint32_t generation, elapsed_ms;
    uint8_t profile, revolution;
    quality_extra_t extra;
    quality_local_t local[QUALITY_LOCAL_WINDOWS];
    mfm_decoder_t* dec;
    verify_match_t* match;
} quality_t;
uint16_t quality_observation_pack(const quality_t* q,uint8_t* out,uint16_t capacity);
uint32_t quality_recognition(const quality_t* q,uint64_t expected,uint8_t c,uint8_t h,uint8_t n,
                             bool check_position,uint32_t* valid,uint32_t* mismatches);
uint16_t quality_local_pack(const quality_t* q,uint8_t* out,uint16_t capacity);
uint16_t ufi_local_report(uint8_t* out,uint16_t capacity,uint32_t generation);
void quality_init(quality_t* q, uint8_t format, uint32_t rate, uint8_t revs);
void quality_begin(quality_t* q, mfm_decoder_t* dec, uint8_t profile, verify_match_t* match);
void quality_revolution(quality_t* q, uint8_t revolution);
void quality_interval(quality_t* q, uint32_t ticks);
void quality_end_revolution(quality_t* q);
uint16_t quality_pack(const quality_t* q, uint8_t* out, uint16_t capacity);
int ufi_quality_verify(const verify_ref_t* ref, bool count_ok, uint8_t* level);

int ufi_quality_analyze_options(const quality_options_t* options);

void quality_tail(quality_t* q,uint32_t previous,uint32_t end);

/* Cooperative analysis over a completed capture. One step consumes <=256 edges. */
int ufi_quality_async_start(const quality_options_t* options);
int ufi_quality_async_step(void); /* BUSY until terminal */
void ufi_quality_async_cancel(void);
void ufi_quality_async_progress(uint8_t* phase,uint8_t* candidate,uint8_t* rev,uint32_t* edge);
const quality_t* ufi_quality_result(void);
