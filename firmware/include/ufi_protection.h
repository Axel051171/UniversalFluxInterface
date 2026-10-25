/* Optional evidence classification. Findings never prove intent or absence of protection. */
#pragma once
#include "ufi_quality.h"
#include "ufi_compare.h"
#define PROTECT_CONTENT  0x01u
#define PROTECT_ERRORS   0x02u
#define PROTECT_LAYOUT   0x04u
#define PROTECT_TIMING   0x08u
#define PROTECT_POSITION 0x10u
#define PROTECT_SIGNATURE 0x20u
#define PROTECT_MASK     0x3fu
#define PROTECT_RECORDS 48u
#define PROTECT_INCOMPLETE 1u
#define PROTECT_OVERFLOW 2u
#define PROTECT_FEW_REVS 4u
#define PROTECT_NO_SECTORS 8u
#define PROTECT_NO_COMPARE 16u
#define PROTECT_ALIGNMENT 32u
#define PROTECT_AMBIGUOUS 64u
#define PROTECT_NO_TIMING 128u
enum {PROTECT_DISABLED,PROTECT_NO_INDICATOR,PROTECT_ANOMALY,PROTECT_CANDIDATE,
      PROTECT_KNOWN_SIGNATURE,PROTECT_INSUFFICIENT};
enum {PR_CONTENT=1,PR_FLUX_VARIATION,PR_STABLE_BAD_CRC,PR_CODING,PR_DUPLICATE,
      PR_HEADER_POSITION,PR_MIXED_LENGTH,PR_RATE_CHANGE,PR_LONG_GAP,PR_POSITION,
      PR_ORDER,PR_CONTENT_BAD_CRC,PR_HEADER_LENGTH,PR_COPYLOCK_SECTOR6=0x1001};
typedef struct {
    uint16_t flags,timing_permille;
    uint8_t min_revs,min_repeat;
} protection_options_t;
typedef struct {
    uint16_t rule,category;
    uint32_t position,extent,value,reference;
    mfm_id_t id;
    uint8_t revs,level,qualifiers,unit;
    uint32_t support;
} protection_record_t;
typedef struct {
    mfm_id_t id;
    uint32_t crc,hash,positions[QUALITY_REVS];
    uint8_t seen,valid,bad,coding,changed,duplicates,header_mismatch,bad_changed;
} protection_sector_t;
typedef struct {
    uint32_t crc,hash;
    uint8_t sector,revs;
} protection_bad_variant_t;
typedef struct protection_state {
    protection_options_t options;
    protection_sector_t sectors[QUALITY_SECTORS];
    protection_bad_variant_t bad_variants[QUALITY_SECTORS];
    protection_record_t records[PROTECT_RECORDS];
    uint8_t order[QUALITY_REVS][QUALITY_SECTORS],order_n[QUALITY_REVS];
    uint8_t count,n,state,signature_revs,scanned,scan_rev,order_lost,bad_count;
    uint16_t checked,insufficient,limitations;
    uint32_t dropped,observation_dropped,signature_position;
    /* Independent bounded Copylock DD scanner: one complete sector, nominal pass only. */
    uint8_t signature_data[512];
    uint32_t shift,scan_start,scan_period;
    int32_t scan_phase;
    uint16_t scan_bytes;
    uint8_t scan_bits,scan_state,scan_previous;
} protection_state_t;
bool protection_options_decode(const uint8_t* p,uint16_t n,protection_options_t* out);
void protection_begin(protection_state_t* s,quality_t* q,const protection_options_t* options);
void protection_sector(quality_t* q,const mfm_id_t* id,const uint8_t* data,bool accepted);
void protection_sector_hashed(quality_t* q,const mfm_id_t* id,const uint8_t* data,bool accepted,
                              const verify_fingerprint_t* fingerprint);
void protection_interval(quality_t* q,uint32_t ticks);
void protection_finish(quality_t* q,const track_compare_t* comparison);
uint16_t protection_pack(const quality_t* q,uint8_t first,uint8_t count,uint8_t* out,uint16_t capacity);
int ufi_protection_options(const uint8_t* p,uint16_t n,uint8_t* out);
