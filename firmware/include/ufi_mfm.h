/**
 * UFI Flux Engine - IBM MFM sector codec (PC 360K / 720K / 1.2M / 1.44M)
 *
 * Pure C, no HAL: flux intervals in TIM2 ticks (275 MHz) in, sectors out, and a whole
 * track (IBM System/34 layout) from sector data to flux intervals for the write path.
 */
#ifndef UFI_MFM_H
#define UFI_MFM_H

#include <stdbool.h>
#include <stdint.h>

#define MFM_TICK_HZ         275000000u
#define MFM_MAX_SECTOR      1024u

typedef struct {
    uint8_t c, h, r, n;
} mfm_id_t;

/* called for every data field that follows a valid ID field */
typedef void (*mfm_sector_cb)(void* ctx, const mfm_id_t* id, const uint8_t* data, bool crc_ok);

typedef struct {
    /* clock recovery: cell period in 1/256 tick, limited to +-12 % of nominal */
    uint32_t nominal, period, pmin, pmax;
    int32_t phase;
    /* raw cell shift register and field state */
    uint64_t sr;
    uint8_t state, mark;
    uint16_t cells, want, got;
    uint16_t crc;
    mfm_id_t id;
    bool id_ok;
    uint32_t id_age;            /* cells since the last good ID field */
    uint8_t buf[MFM_MAX_SECTOR + 2];
    mfm_sector_cb cb;
    void* ctx;
    uint32_t ids_seen;          /* good ID fields (format detection) */
} mfm_decoder_t;

uint16_t mfm_crc16(uint16_t crc, const uint8_t* p, uint32_t n);

/* rate_bps: 250000 (DD), 300000 (DD in a 360 rpm drive), 500000 (HD) */
void mfm_dec_init(mfm_decoder_t* d, uint32_t rate_bps, mfm_sector_cb cb, void* ctx);
void mfm_dec_interval(mfm_decoder_t* d, uint32_t ticks);

typedef struct {
    uint32_t rate_bps;
    uint32_t track_cells;       /* cells to fill (gap 4b pads up to here) */
    uint8_t spt;                /* 512-byte sectors, numbered 1..spt */
    uint8_t gap3;
} mfm_track_fmt_t;

/* Encode one track starting at the index: deltas[] = flux intervals in ticks.
 * Returns the number of intervals, 0 if max is too small or the sectors do not fit. */
uint32_t mfm_encode_track(const mfm_track_fmt_t* f, uint8_t cyl, uint8_t head,
                          const uint8_t* sectors, uint32_t* deltas, uint32_t max);

#endif /* UFI_MFM_H */
