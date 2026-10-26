/**
 * UFI Flux Engine - Macintosh 3.5" drive (Sony register interface), HAL-free logic
 *
 * Board v1.0, connector J23 (Apple internal 20-pin layout): CA0/CA1/CA2/LSTRB share the
 * Disk II phase nets AP_PH0..AP_PH3, /ENBL is AP_EN2, RD is AP_RDDATA (TIM2_CH3), WR is
 * AP_WRDATA (TIM3_CH2), /WRREQ is AP_WRREQ, SEL comes from PB0 through U27.  The drive
 * multiplexes a status bit onto RD when a non-data register is addressed.
 *
 * Register address (this module's numbering): reg = CA2<<3 | CA1<<2 | CA0<<1 | SEL.
 *  - Sense: put the address on CA0-CA2/SEL, read RD (most bits are active low).
 *  - Command: CA0, CA1 and SEL address the control, CA2 carries the value; a high pulse on
 *    LSTRB latches it.  MAC_CMD_* below already contain the CA2 value.
 * Register meanings follow Apple's published drive documentation (Inside Macintosh /
 * Guide to the Macintosh Family Hardware); timings are conservative choices of this
 * firmware and still need confirmation on a real drive (docs/USB_Protokoll.md 1.7).
 *
 * The module only talks to the hardware through mac_io_t, so native tests run it against a
 * simulated drive.  Return values are the ufi_error_t codes (ufi_fixes.h) with the same
 * numbers; ufi_drive.c checks the equality at compile time.
 */

#ifndef UFI_MAC_H
#define UFI_MAC_H

#include <stdbool.h>
#include <stdint.h>

/* ---- sense registers (RD level; "0" = asserted for the active-low ones) ---------------- */
#define MAC_REG_DIRTN       0u      /* step direction latch: 0 = towards higher tracks */
#define MAC_REG_CSTIN       1u      /* 0 = disk in place */
#define MAC_REG_STEP        2u      /* 0 = step in progress */
#define MAC_REG_WRTPRT      3u      /* 0 = disk write protected */
#define MAC_REG_MOTORON     4u      /* 0 = spindle motor on */
#define MAC_REG_TK0         5u      /* 0 = head on track 0 */
#define MAC_REG_SWITCHED    6u      /* disk switched / eject latch (reported raw) */
#define MAC_REG_TACH        7u      /* tachometer, MAC_TACH_PER_REV pulses per revolution */
#define MAC_REG_RDDATA0     8u      /* read data, lower head (also selects the head for writing) */
#define MAC_REG_RDDATA1     9u      /* read data, upper head */
#define MAC_REG_SUPERDR     10u     /* 1 = SuperDrive (FDHD) */
#define MAC_REG_SIDES       12u     /* 0 = single-sided drive (400K) */
#define MAC_REG_READY       13u     /* 0 = drive ready */
#define MAC_REG_DRVIN       14u     /* 0 = drive installed */
#define MAC_REG_HD          15u     /* FDHD: high-density medium flag (reported raw) */

/* ---- commands: address and CA2 value, latched by an LSTRB pulse --------------------------- */
#define MAC_CMD_DIR_IN      0u      /* DIRTN := 0, next steps towards track 79 */
#define MAC_CMD_DIR_OUT     8u      /* DIRTN := 1, next steps towards track 0 */
#define MAC_CMD_STEP        2u      /* one track in the latched direction */
#define MAC_CMD_MOTOR_ON    4u
#define MAC_CMD_MOTOR_OFF   12u
#define MAC_CMD_EJECT       14u

/* ---- results (same numbers as ufi_error_t) ---------------------------------------------- */
#define MAC_OK              0
#define MAC_ERR_SEEK        (-3)    /* UFI_ERR_SEEK_FAIL: step not finished, no TK0, range */
#define MAC_ERR_NO_TACH     (-4)    /* UFI_ERR_NO_INDEX: no tachometer pulses */
#define MAC_ERR_TIMEOUT     (-5)    /* UFI_ERR_TIMEOUT: pulses stopped during a measurement */
#define MAC_ERR_BAD_ARGS    (-16)   /* UFI_ERR_BAD_ARGS */

/* ---- drive kinds ----------------------------------------------------------------------------- */
#define MAC_KIND_NONE       0u      /* nothing answers (no drive, no power, line stuck or floating) */
#define MAC_KIND_400K       1u      /* single-sided, speed from the PWM line (J23 pin 20, JP5) */
#define MAC_KIND_800K       2u      /* double-sided, speed zones set inside the drive */
#define MAC_KIND_FDHD       3u      /* SuperDrive: 800K GCR plus 1.44 MB MFM */

#define MAC_TRACKS          80u
#define MAC_ZONES           5u
#define MAC_TACH_PER_REV    60u     /* tachometer pulses per spindle revolution */

/* ---- timing (microseconds) ---------------------------------------------------------------- */
#define MAC_SENSE_SETTLE_US 10u     /* address change -> RD shows the selected status bit */
#define MAC_SETUP_US        2u      /* address and CA2 stable before LSTRB rises */
#define MAC_STROBE_US       2u      /* LSTRB high time of an ordinary command */
#define MAC_EJECT_HOLD_US   750000u /* LSTRB high time of the eject command */
#define MAC_STEP_START_US   100u    /* after the STEP strobe, before polling the STEP bit */
#define MAC_STEP_TIMEOUT_US 30000u  /* STEP bit back to 1 within this time */
#define MAC_STEP_POLL_US    50u
#define MAC_RECAL_STEPS     90u     /* outward steps at most until TK0 */
#define MAC_TACH_REV_TIMEOUT_US 400000u  /* one revolution at >= 150 rpm */

/* Hardware access.  `lines` sets CA2..CA0 from reg bits 3..1 and SEL from bit 0, `strobe`
 * drives LSTRB, `rd` returns the RD level (true = high), `ticks` is a free-running counter
 * with `ticks_per_us` counts per microsecond (unsigned differences are used, so it may wrap). */
typedef struct {
    void (*lines)(void* ctx, uint8_t reg);
    void (*strobe)(void* ctx, bool high);
    bool (*rd)(void* ctx);
    void (*wait_us)(void* ctx, uint32_t us);
    uint32_t (*ticks)(void* ctx);
    uint32_t ticks_per_us;
    void* ctx;
} mac_io_t;

typedef struct {
    uint8_t kind;           /* MAC_KIND_* of the last probe */
    int8_t track;           /* head position, -1 = unknown */
    uint8_t side;           /* head addressed by the RDDATA register */
    int8_t dir;             /* +1 / -1 latched step direction, 0 = unknown */
    bool motor;             /* MOTORON command state (what was sent) */
    uint16_t sense;         /* last complete status read, bit r = RD level of register r */
    uint16_t tach_rpm_x100; /* last tachometer measurement, 0 = none */
} mac_drive_t;

/* Status bits of mac_flags() */
#define MAC_F_DISK          0x01u
#define MAC_F_WPROT         0x02u
#define MAC_F_MOTOR         0x04u
#define MAC_F_READY         0x08u
#define MAC_F_TRACK0        0x10u
#define MAC_F_STEPPING      0x20u

void mac_reset(mac_drive_t* d);
bool mac_sense(const mac_io_t* io, const mac_drive_t* d, uint8_t reg);
void mac_command(const mac_io_t* io, const mac_drive_t* d, uint8_t cmd, uint32_t hold_us);
void mac_head(const mac_io_t* io, mac_drive_t* d, uint8_t side);
uint16_t mac_sense_all(const mac_io_t* io, mac_drive_t* d);
uint8_t mac_classify(uint16_t first, uint16_t second);
uint8_t mac_probe(const mac_io_t* io, mac_drive_t* d);
uint8_t mac_flags(uint16_t sense);
int mac_step(const mac_io_t* io, mac_drive_t* d, int dir);
int mac_recalibrate(const mac_io_t* io, mac_drive_t* d);
int mac_seek(const mac_io_t* io, mac_drive_t* d, uint8_t track);
void mac_motor(const mac_io_t* io, mac_drive_t* d, bool on);
void mac_eject(const mac_io_t* io, mac_drive_t* d);
int mac_tach_revs(const mac_io_t* io, mac_drive_t* d, uint8_t revs,
                  uint32_t* min_ticks, uint32_t* avg_ticks, uint32_t* max_ticks);
uint16_t mac_rpm_x100(uint32_t rev_ticks, uint32_t ticks_per_us);

#endif /* UFI_MAC_H */
