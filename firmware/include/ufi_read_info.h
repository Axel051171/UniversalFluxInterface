/**
 * UFI Flux Engine - read context block of the READ END frame (READ flag bit5, 1.18-dev,
 * docs/USB_Protokoll.md 4.24): requested/effective revolutions, head position and the
 * DENSITY/DRATE levels at the start and the end of the read, cached clock and climate values,
 * and where data was lost.  Only metadata: the flux stream itself is never changed.
 *
 * The packing helpers are HAL-free so the native tests can check the wire layout.
 */
#ifndef UFI_READ_INFO_H
#define UFI_READ_INFO_H

#include <stdbool.h>
#include <stdint.h>

#define UFI_READ_INFO_SIZE   132u   /* schema 1: 36-byte head + UFI_READ_INFO_SLOTS loss entries */
#define UFI_READ_INFO_SLOTS  4u
#define UFI_READ_LOSS_SIZE   24u

/* loss entry kind bits */
enum {
    READ_LOSS_RDATA = 0x01,         /* RDATA overcapture: at least one flux edge lost */
    READ_LOSS_INDEX = 0x02,         /* INDEX/SYNC overcapture: an index pulse lost */
    READ_LOSS_DMA = 0x04,           /* DMA transfer/FIFO error */
    READ_LOSS_FULL = 0x08,          /* store full or host too slow: nothing recorded after start */
    READ_LOSS_OPEN_END = 0x10,      /* no end stamp: the window lasts until the end of the capture */
    READ_LOSS_BEFORE_INDEX = 0x20,  /* before the first index pulse: no stream time, start/end 0 */
};

/* head flags (offset 2) */
enum {
    READ_INFO_REVS_CLAMPED = 0x01,  /* requested revolutions 0 (-> 1) or > 200 (-> 200) */
    READ_INFO_LINES_CHANGED = 0x02, /* DENSITY/DRATE at the END differ from the start */
    READ_INFO_HEAD_MOVED = 0x04,    /* track/side/status at the END differ from the start */
    READ_INFO_PPM = 0x08,           /* offset 16/20: a self-test clock measurement exists */
    READ_INFO_CLIMATE = 0x10,       /* offset 24-31: a cached SHT40 value exists */
    READ_INFO_TCXO = 0x20,          /* HSE bypass (TCXO, board v1.0), else crystal oscillator */
    READ_INFO_LOSS_TRUNCATED = 0x40 /* more loss windows than slots */
};

/* One loss window in flux-timer stamps (TIM2, 275 MHz, 32 bit). */
typedef struct {
    uint8_t kind;                   /* READ_LOSS_* */
    uint32_t start;                 /* last check that saw the capture intact */
    uint32_t end;                   /* detection, valid without READ_LOSS_OPEN_END */
    uint32_t samples;               /* transitions stored before start (lower bound) */
} ufi_loss_raw_t;

/* Sampled at the start and at the END of the read. */
typedef struct {
    uint8_t density, drate;         /* 1 = asserted, as op 8 context offset 42/43 */
    uint8_t track, side, status;    /* op 13: mechanical track counter, side, status bits low byte */
} ufi_read_sample_t;

typedef struct {
    uint8_t revs_requested, revs_effective, quarter;
    ufi_read_sample_t start, end;
    bool tcxo, ppm_valid, climate_valid;
    int32_t ppm;
    uint32_t ppm_age_ms;
    int16_t temp_x100;
    uint16_t rh_x100;
    uint32_t climate_age_ms;
    uint8_t loss_total;             /* windows seen in this read */
    uint8_t loss_n;                 /* entries in loss[] */
    ufi_loss_raw_t loss[UFI_READ_INFO_SLOTS];
} ufi_read_info_t;

/* Stream time of flux-timer stamp t: returns the index pulses after the first one at or before
 * t (= FD marks in the stream before t), *ticks = ticks since the first index (64 bit, exact
 * over timer wraps as long as t lies less than 2^31 ticks after the last index).  Without an
 * index pulse, or for t before the first one, both are 0.  Index-less reads (period != 0):
 * boundaries every period ticks after idx_time[0], also past the recorded ones. */
uint8_t ufi_read_stream_time(const volatile uint32_t* idx_time, uint8_t idx_count, uint32_t period,
                             uint32_t t, uint64_t* ticks);

/* Wire block of UFI_READ_INFO_SIZE bytes; returns the size. */
uint16_t ufi_read_info_pack(const ufi_read_info_t* in, const volatile uint32_t* idx_time, uint8_t idx_count,
                            uint32_t period,
                            uint8_t* out);

#endif
