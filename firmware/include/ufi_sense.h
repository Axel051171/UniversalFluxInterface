/**
 * UFI Flux Engine - board v1.0 sensing (stage A10): calendar access, SHT40, rail voltages,
 * SYNC capture, J6 pin read-backs (ufi_sense.c, ufi_sense_calc.c, ufi_sync.c).
 *
 * This header is HAL-free: the v2 dispatcher and the dump writer are compiled natively
 * against tests/hal_stub with generated stubs for everything declared here, so no HAL type
 * appears in a prototype.  The pure helpers (ufi_sense_calc.c) are tested on the host in
 * tests/test_sense.c.
 */

#ifndef UFI_SENSE_H
#define UFI_SENSE_H

#include <stdbool.h>
#include <stdint.h>

/* ---- calendar (RTC on the LSE crystal, v1.0 only; ufi_clock.c keeps the registers) ---- */
typedef struct {
    bool ready;                 /* false: no LSE / RTC not counting (v0.x boards): fields are 0 */
    uint16_t year;              /* 2000..2099 */
    uint8_t mon, day, hour, min, sec;
} ufi_rtc_time_t;

bool ufi_sense_rtc_get(ufi_rtc_time_t* t);          /* false = not available (t->ready false) */
int ufi_sense_rtc_set(const ufi_rtc_time_t* t);     /* UFI_OK, UFI_ERR_NOT_IMPL (no RTC), UFI_ERR_BAD_ARGS */
bool ufi_calendar_valid(uint16_t year, uint8_t mon, uint8_t day, uint8_t hour, uint8_t min, uint8_t sec);
uint32_t ufi_calendar_yyyymmdd(uint16_t year, uint8_t mon, uint8_t day);
void ufi_calendar_text(char out[20], const ufi_rtc_time_t* t);   /* "2026-10-25T14:03:11", "" when not ready */

/* ---- SHT40 (U25, I2C1 PB6/PB7, address 0x44, measure 0xFD, 6 bytes with CRC-8 0x31) ---- */
enum { SHT40_OK = 0, SHT40_NO_SENSOR = 1, SHT40_CRC = 2, SHT40_NO_BUS = 3 };
typedef struct {
    uint8_t status;             /* SHT40_* */
    int16_t temp_x100;          /* 0.01 degC */
    uint16_t rh_x100;           /* 0.01 %RH, clamped to 0..10000 */
} ufi_sht40_t;

int ufi_sense_sht40(ufi_sht40_t* r);                /* always returns UFI_OK, r->status tells; <= 30 ms */
bool ufi_sense_sht40_present(void);                 /* result of the probe at start-up */
/* Cached values for the READ END (4.24) and the provenance block (4.12): never measure, never
 * block.  Climate = the last valid SHT40 reading (probe, SENSOR op, log header); clock = the last
 * self-test usb_sof result (TIM2 against the host's USB frames).  false = none since reset. */
bool ufi_sense_climate(int16_t* temp_x100, uint16_t* rh_x100, uint32_t* age_ms);
void ufi_sense_note_clock_ppm(int32_t ppm);
bool ufi_sense_clock_ppm(int32_t* ppm, uint32_t* age_ms);
uint8_t ufi_sht40_crc8(const uint8_t* p, uint32_t n);
bool ufi_sht40_convert(const uint8_t raw[6], int16_t* temp_x100, uint16_t* rh_x100);   /* false = CRC */
void ufi_sht40_text(char temp[8], char rh[8], int16_t temp_x100, uint16_t rh_x100);     /* "-12.34", "45.67" */

/* ---- drive supply voltages (PC4 = FDD_5V / 2, PC5 = FDD_12V / 4.3; v1.0 only) ---- */
#define RAIL_5V_MIN_MV      4500u
#define RAIL_12V_MIN_MV     10800u
#define RAIL_FLAG_5V_LOW    0x01u   /* rail on and below RAIL_5V_MIN_MV */
#define RAIL_FLAG_12V_LOW   0x02u
#define RAIL_FLAG_5V_ON     0x04u
#define RAIL_FLAG_12V_ON    0x08u
#define RAIL_FLAG_FITTED    0x10u   /* dividers R160-R163 on the board (board_rev >= 10); otherwise mV = 0 */
typedef struct {
    uint16_t v5_mv, v12_mv;
    uint8_t flags;              /* RAIL_FLAG_* */
} ufi_rails_t;

void ufi_sense_rails(ufi_rails_t* r);
uint16_t ufi_rail_mv(uint16_t adc_mv, int rail);    /* rail 0: x2, rail 1: x4.3 */

/* ---- metadata for every recording (DUMPnnnn.JSN / .LOG) ---- */
typedef struct {
    bool rtc;                   /* time[] valid */
    char time[20];              /* "YYYY-MM-DDThh:mm:ss" */
    bool rails;                 /* v5_mv / v12_mv measured (v1.0) */
    uint16_t v5_mv, v12_mv;
    bool sensor;                /* temp_c / rh valid */
    char temp_c[8], rh[8];      /* decimal text, JSON number syntax */
} ufi_sense_meta_t;

void ufi_sense_meta(ufi_sense_meta_t* m);
uint16_t ufi_sense_log_header(char* buf, uint16_t n);   /* one text line, CR LF, NUL; returns its length */

/* ---- v1.0 read-backs and the two new outputs on J6 (A11d) ---- */
#define FDD_PIN_PIN2_IN     0x01u   /* J6 pin 2 low (through U26) */
#define FDD_PIN_PIN4_IN     0x02u   /* J6 pin 4 low */
#define FDD_PIN_CSTOUT      0x04u   /* J23 pin 20 !CSTOUT asserted */
#define FDD_PIN_WGATE_SENSE 0x08u   /* WGATE bus asserted (U10F) */
#define FDD_PIN_WREN_SENSE  0x10u   /* WLOCK_HW high = write locked by the front switch / cable off */
#define FDD_PIN_FLT5        0x20u   /* eFuse U28 FLT low */
#define FDD_PIN_FLT12       0x40u   /* eFuse U29 FLT low */
#define FDD_PIN_SYNC        0x80u   /* SYNC level high (sensor pulse) */
uint8_t ufi_sense_fdd_pins(void);                   /* 0 on boards before v1.0 */
int ufi_sense_fdd_pins_set(uint8_t pin4, uint8_t pin33);   /* 0 release, 1 assert, 0xFF leave; NOT_IMPL before v1.0 */
uint8_t ufi_sense_fdd_pins_out(void);               /* bit0 J6 pin 4 asserted, bit1 pin 33 high */

void ufi_sense_init(void);                          /* ufi_board_init(): state, PC4/PC5 analog, I2C1, SHT40 probe */

/* ---- SYNC sensor capture on TIM8_CH1 (PC6), board v1.0 (ufi_sync.c) ---- */
typedef struct {
    uint8_t active;             /* TIM8 capture running */
    uint8_t source;             /* 1 = SYNC replaces the INDEX line (Apple drive + apple_sync=1 on v1.0) */
    uint8_t level;              /* SYNC input level now (1 = pulse) */
    uint32_t count;             /* pulses since the capture started */
    uint32_t last_t2;           /* last pulse in the TIM2 flux time base (32 bit, 275 MHz) */
    uint32_t last_t32;          /* last pulse in the TIM8 time base with the overflow counter (32 bit) */
    uint32_t period;            /* ticks between the last two pulses (275 MHz) */
} ufi_sync_status_t;

void ufi_sync_init(void);                           /* ufi_board_init(): state in DTCM, board revision */
int ufi_sync_enable(bool on);                       /* start/stop counting; UFI_ERR_NOT_IMPL before v1.0 */
void ufi_sync_service(void);                        /* main loop: switch the index source with the drive selection */
void ufi_sync_select(bool apple);                   /* capture start: same decision without waiting for the loop */
void ufi_sync_status(ufi_sync_status_t* s);
bool ufi_sync_asserted(void);                       /* SYNC level (ufi_flux_index_asserted when it is the source) */
uint32_t ufi_sync_map_t2(uint32_t t2_now, uint16_t t8_now, uint16_t ccr);   /* TIM8 capture -> TIM2 time */
void ufi_flux_index_source_sync(bool on);           /* ufi_flux.c: SYNC pulses replace the INDEX capture */

#endif /* UFI_SENSE_H */
