/**
 * UFI Flux Engine - write path codec (pure functions, no HAL)
 *
 * The two transformations of the write path that must be exact: the compact stream
 * decoder (WRITE data bytes -> u32 flux intervals) and the packing of u32 intervals into
 * 16-bit timer periods (scale, write precompensation, range check).  Both are used by
 * the firmware (ufi_write.c) and compiled unchanged for the host tests in
 * firmware/tests, so what is tested is what runs.
 *
 * Result codes equal the firmware status codes (ufi_fixes.h) so callers can pass them on.
 */

#pragma once
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifndef FLUX_TIMER_FREQ
#define FLUX_TIMER_FREQ     275000000UL     /* TIM2/TIM3 ticks per second */
#endif

#define WRITE_CODEC_ERR_DATA    (-10)       /* = UFI_ERR_BUFFER_FULL: malformed / too many */
#define WRITE_CODEC_ERR_RANGE   (-18)       /* = UFI_ERR_RANGE: interval not representable */

/* ---- compact stream decoder (READ stream code without index marks) ----------------
 *   0x01..0xEF        interval = byte
 *   0xF0..0xFC, b     interval = 240 + ((byte - 0xF0) << 8) + b
 *   0xFE, u32 LE      interval = u32 (must not be 0)
 *   0x00, 0xFD, 0xFF  not allowed in write data (reserved / index mark)
 * The decoder stops at the first error and ignores everything after it. */
typedef struct {
    uint32_t* out;              /* decoded intervals */
    uint32_t max;               /* capacity = expected count */
    uint32_t decoded;           /* intervals decoded so far */
    uint8_t code[5];            /* multi-byte code being assembled */
    uint8_t len, need;          /* bytes collected / needed (0 = between codes) */
    bool bad;                   /* error seen: reserved byte, zero interval, overflow */
} compact_dec_t;

void compact_init(compact_dec_t* d, uint32_t* out, uint32_t max);
/* Feed bytes; 0 = ok so far, WRITE_CODEC_ERR_DATA = invalid data (d->bad set) */
int compact_feed(compact_dec_t* d, const uint8_t* p, uint32_t n);
/* true when exactly max intervals arrived and no code is pending */
bool compact_complete(const compact_dec_t* d);

/* ---- packing u32 intervals -> u16 timer periods ----------------------------------- */
#define WRITE_PULSE_NS      400u    /* WDATA low pulse */
#define WRITE_PULSE_TICKS   ((WRITE_PULSE_NS * (FLUX_TIMER_FREQ / 1000000UL)) / 1000UL)
#define WRITE_MIN_DELTA     (4u * WRITE_PULSE_TICKS)    /* 440 ticks = 1.6 us */
#define WRITE_MAX_DELTA     65536u                      /* 16-bit timer: 238 us */

/* write precompensation per data rate (82077 defaults); 0 = off */
#define PRECOMP_DD_HD_NS    125u
#define PRECOMP_ED_NS       42u

typedef struct {
    uint16_t scale;             /* time scale in 1/1000, 1000 = unchanged (500..2000) */
    uint32_t precomp_ns;        /* late/early shift of the outer bit of a short-long pair */
    uint8_t track;              /* precompensation from track 40 (half) and 61 (full) */
    bool clamp;                 /* true: clamp out-of-range periods and count them */
    bool precomp_fixed;         /* 1.18: precomp_ns applies as given, no track rule (caller chose it) */
} pack_cfg_t;

/* Precompensation amount from the data: ED (1 Mbit/s) has intervals below 1.5 us */
uint32_t write_precomp_auto_ns(const uint32_t* deltas, uint32_t n);

/* Pass 1 checks every period (scaled, precompensated) against the representable range;
 * only when all pass, pass 2 packs in place (arr may alias deltas: halfword i never
 * overlaps a word >= i).  Returns 0, or WRITE_CODEC_ERR_RANGE with the input untouched
 * (cfg->clamp = false).  *clamped = periods moved into range (cfg->clamp = true). */
int write_pack(const uint32_t* deltas, uint32_t n, const pack_cfg_t* cfg, uint16_t* arr,
               uint32_t* clamped);
