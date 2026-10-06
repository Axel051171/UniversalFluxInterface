/**
 * UFI Flux Engine - Drive Control Module
 *
 * Shugart/PC (34-pin J6) and Amiga (DB23 header J7) drives on the shared FDD bus.
 * Pins and polarity come from board.h: bus_out(pin, true) asserts a line (low on the
 * cable), bus_in(pin) is true while the drive asserts it.
 *
 * Amiga J7 wiring: MTRXD = MOTOR_B, SEL1B = DRV_SEL_B.  Amiga drives latch MTRXD on
 * the falling edge of their select line, so the motor is switched via a select pulse.
 */

#include "ufi_firmware.h"

/* ============================================================================
 * GLOBALE VARIABLEN
 * ============================================================================ */

static drive_type_t g_current_drive = DRIVE_NONE;
static drive_status_t g_drive_status[DRIVE_IEC + 1];

// Timing-Konstanten (µs)
// Timing (host-tunable per drive via UFI_CMD_DRIVE_TIMING, defaults = PC/Amiga safe values)
static drive_timing_t g_timing = {
    .step_pulse_us = 3, .step_rate_us = 3000, .settle_us = 15000, .dir_change_us = 0,
    .side_settle_us = 200, .spinup_ms = 500, .select_settle_us = 10000,
};
#define STEP_PULSE_US       (g_timing.step_pulse_us)
#define STEP_RATE_US        (g_timing.step_rate_us)
#define SETTLE_TIME_US      (g_timing.settle_us)
#define MOTOR_SPINUP_MS     (g_timing.spinup_ms)
#define DIR_SETUP_US        1
#define SELECT_SETTLE_US    (g_timing.select_settle_us)
static int g_last_dir;              /* +1 / -1, for the direction-change settle */

static void delay_us(uint32_t us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

/* ============================================================================
 * APPLE DISK II (nur Boards mit Apple-Port; board.h liefert dann APPLE_PORT und
 * APPLE_{ENABLE,PH0..PH3,WRREQ}_PIN)
 * ============================================================================ */

#if BOARD_HAS_APPLE
static uint8_t apple_phase = 0;

int ufi_drive_apple_step(int direction)
{
    // Apple verwendet 4-Phasen Stepper
    // Phase-Sequenz: 0-1-2-3-0-1-2-3 (vorwärts)
    //                0-3-2-1-0-3-2-1 (rückwärts)
    static const uint16_t phase_pins[4] = {
        APPLE_PH0_PIN, APPLE_PH1_PIN, APPLE_PH2_PIN, APPLE_PH3_PIN
    };

    HAL_GPIO_WritePin(APPLE_PORT, phase_pins[apple_phase], GPIO_PIN_RESET);
    if (direction > 0) {
        apple_phase = (apple_phase + 1) & 3;
    } else {
        apple_phase = (apple_phase - 1) & 3;
    }
    HAL_GPIO_WritePin(APPLE_PORT, phase_pins[apple_phase], GPIO_PIN_SET);

    delay_us(5000);  // 5ms Phase-Zeit

    // Track-Counter (2 Phasen = 1 Track)
    static uint8_t phase_count = 0;
    phase_count++;
    if (phase_count >= 2) {
        phase_count = 0;
        drive_status_t* status = &g_drive_status[DRIVE_APPLE_II];
        if (direction > 0 && status->current_track < 39) {
            status->current_track++;
        } else if (direction < 0 && status->current_track > 0) {
            status->current_track--;
        }
    }
    return UFI_OK;
}
#endif

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_drive_init(void)
{
    /* GPIO modes/levels are set by board_gpio_init(); all lines released */
    for (int i = 0; i <= DRIVE_IEC; i++) {
        g_drive_status[i] = (drive_status_t){0};
        g_drive_status[i].type = DRIVE_NONE;
    }
    g_current_drive = DRIVE_NONE;
}

/* ============================================================================
 * LAUFWERK AUSWÄHLEN
 * ============================================================================ */

int ufi_drive_select(drive_type_t type)
{
    bus_out(&PIN_FDD_DRV_SEL_A, false);
    bus_out(&PIN_FDD_DRV_SEL_B, false);
#if BOARD_HAS_APPLE
    HAL_GPIO_WritePin(APPLE_PORT, APPLE_ENABLE_PIN, GPIO_PIN_RESET);
#endif

    switch (type) {
        case DRIVE_SHUGART_A:
            bus_out(&PIN_FDD_DRV_SEL_A, true);
            break;
        case DRIVE_SHUGART_B:
        case DRIVE_AMIGA:                   /* J7 SEL1B = DRV_SEL_B */
            bus_out(&PIN_FDD_DRV_SEL_B, true);
            break;
        case DRIVE_IEC:                     /* IEC needs no select */
            break;
        case DRIVE_APPLE_II:
#if BOARD_HAS_APPLE
            HAL_GPIO_WritePin(APPLE_PORT, APPLE_ENABLE_PIN, GPIO_PIN_SET);
            break;
#else
            g_current_drive = DRIVE_NONE;   /* no Apple port on this board */
            return UFI_ERR_NOT_IMPL;
#endif
        case DRIVE_NONE:
        default:
            g_current_drive = DRIVE_NONE;
            return UFI_OK;
    }

    g_current_drive = type;
    g_drive_status[type].type = type;
    delay_us(SELECT_SETTLE_US);
    return UFI_OK;
}

drive_type_t ufi_drive_get_current(void)
{
    return g_current_drive;
}

/* ============================================================================
 * MOTOR STEUERUNG
 * ============================================================================ */

/* Amiga: MTRXD level is latched by the drive on the select falling edge */
static void amiga_motor(bool on)
{
    bus_out(&PIN_FDD_DRV_SEL_B, false);
    delay_us(2);
    bus_out(&PIN_FDD_MOTOR_B, on);
    delay_us(2);
    bus_out(&PIN_FDD_DRV_SEL_B, true);      /* latch */
    delay_us(2);
    bus_out(&PIN_FDD_MOTOR_B, false);       /* MTRXD may be released after the latch */
}

int ufi_drive_motor(bool on)
{
    switch (g_current_drive) {
        case DRIVE_SHUGART_A:
            bus_out(&PIN_FDD_MOTOR_A, on);
            break;
        case DRIVE_SHUGART_B:
            bus_out(&PIN_FDD_MOTOR_B, on);
            break;
        case DRIVE_AMIGA:
            amiga_motor(on);
            break;
        case DRIVE_IEC:                     /* 1541 controls its own motor */
            break;
#if BOARD_HAS_APPLE
        case DRIVE_APPLE_II:                /* Apple: motor follows ENABLE */
            HAL_GPIO_WritePin(APPLE_PORT, APPLE_ENABLE_PIN, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
            break;
#endif
        default:
            return UFI_ERR_NO_DRIVE;
    }

    g_drive_status[g_current_drive].motor_on = on;
    if (on) {
        HAL_Delay(MOTOR_SPINUP_MS);
    }
    return UFI_OK;
}

/* ============================================================================
 * STEP FUNKTIONEN
 * ============================================================================ */

int ufi_drive_step(int direction)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
#if BOARD_HAS_APPLE
    if (g_current_drive == DRIVE_APPLE_II) {
        return ufi_drive_apple_step(direction);
    }
#endif

    /* DIR asserted = step in (towards the spindle) */
    bus_out(&PIN_FDD_DIR, direction > 0);
    delay_us(DIR_SETUP_US);
    if (direction != g_last_dir && g_timing.dir_change_us) {
        delay_us(g_timing.dir_change_us);
    }
    g_last_dir = direction;

    bus_out(&PIN_FDD_STEP, true);
    delay_us(STEP_PULSE_US);
    bus_out(&PIN_FDD_STEP, false);
    delay_us(STEP_RATE_US);

    drive_status_t* status = &g_drive_status[g_current_drive];
    if (direction > 0 && status->current_track < 83) {
        status->current_track++;
    } else if (direction < 0 && status->current_track > 0) {
        status->current_track--;
    }
    return UFI_OK;
}

/* ============================================================================
 * SEEK & RECALIBRATE
 * ============================================================================ */

int ufi_drive_seek(uint8_t track)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    const uint8_t max_track = (g_current_drive == DRIVE_APPLE_II) ? 39 : 83;
    if (track > max_track) {
        return UFI_ERR_SEEK_FAIL;
    }

    drive_status_t* status = &g_drive_status[g_current_drive];
    if (!status->track0 && status->current_track == 0) {
        /* head position unknown: find track 0 first */
        if (ufi_drive_recalibrate() != UFI_OK) {
            return UFI_ERR_SEEK_FAIL;
        }
    }

    int steps = (int)track - (int)status->current_track;
    int direction = (steps > 0) ? 1 : -1;
    steps = (steps > 0) ? steps : -steps;

    for (int i = 0; i < steps; i++) {
        ufi_drive_step(direction);
        if (direction < 0 && ufi_drive_at_track0()) {
            status->current_track = 0;
            break;
        }
    }
    status->track0 = ufi_drive_at_track0();

    delay_us(SETTLE_TIME_US);
    return UFI_OK;
}

int ufi_drive_recalibrate(void)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }

    drive_status_t* status = &g_drive_status[g_current_drive];
    for (int i = 0; i < 90; i++) {
        if (ufi_drive_at_track0()) {
            status->current_track = 0;
            status->track0 = true;
            delay_us(SETTLE_TIME_US);
            return UFI_OK;
        }
        ufi_drive_step(-1);
    }
    return UFI_ERR_SEEK_FAIL;
}

/* ============================================================================
 * SEITE / DENSITY
 * ============================================================================ */

int ufi_drive_select_side(uint8_t side)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    /* SIDE asserted (low) = head 1 */
    bus_out(&PIN_FDD_SIDE_SEL, side != 0);
    g_drive_status[g_current_drive].current_side = side;
    delay_us(g_timing.side_settle_us);
    return UFI_OK;
}

int ufi_drive_density_line(bool assert)
{
    bus_out(&PIN_FDD_DENSITY, assert);
    return UFI_OK;
}

/* ============================================================================
 * STATUS ABFRAGEN
 * ============================================================================ */

bool ufi_drive_at_track0(void)
{
    return bus_in(&PIN_FDD_TRACK0);
}

bool ufi_drive_write_protected(void)
{
    return bus_in(&PIN_FDD_WPROT);
}

bool ufi_drive_disk_changed(void)
{
    return bus_in(&PIN_FDD_DKCHG);
}

bool ufi_drive_ready(void)
{
    return bus_in(&PIN_FDD_READY);
}

drive_status_t ufi_drive_get_status(void)
{
    if (g_current_drive == DRIVE_NONE) {
        drive_status_t empty = {0};
        return empty;
    }

    drive_status_t* status = &g_drive_status[g_current_drive];
    status->track0 = ufi_drive_at_track0();
    status->write_protected = ufi_drive_write_protected();
    status->disk_changed = ufi_drive_disk_changed();
    status->ready = ufi_drive_ready();
    return *status;
}

/* ============================================================================
 * TIMING / DISK CHECK / AMIGA DRIVE ID
 * ============================================================================ */

drive_timing_t ufi_drive_get_timing(void)
{
    return g_timing;
}

void ufi_drive_set_timing(const drive_timing_t* t)
{
    g_timing = *t;
    if (g_timing.step_pulse_us == 0) {
        g_timing.step_pulse_us = 1;
    }
}

/* DSKCHG stays asserted after a media change until the head steps with a disk in the
 * drive.  Read it with the drive selected, then step once (away and back) to re-arm it;
 * if it is still asserted afterwards there is no disk. */
int ufi_drive_check_disk(bool* changed, bool* present)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    *changed = ufi_drive_disk_changed();
    if (*changed) {
        const int dir = ufi_drive_at_track0() ? 1 : -1;
        ufi_drive_step(dir);
        ufi_drive_step(-dir);
        delay_us(SETTLE_TIME_US);
    }
    *present = !ufi_drive_disk_changed();
    return UFI_OK;
}

/* Amiga drive identification (Amiga HRM / Linux amiflop fd_get_drive_id, concept only):
 * motor on + off via the select latch resets the ID shift register, then 32 select
 * pulses shift the ID out on /RDY (asserted = 1).  0xFFFFFFFF 3.5" DD, 0xAAAAAAAA 3.5" HD
 * (HD media), 0x55555555 5.25" 40 track, 0x00000000 no drive. */
int ufi_drive_amiga_id(uint32_t* id)
{
    if (g_current_drive != DRIVE_AMIGA) {
        return UFI_ERR_NO_DRIVE;
    }
    const gpio_pin_t* sel = &PIN_FDD_DRV_SEL_B;
    bus_out(sel, false);
    for (int on = 1; on >= 0; on--) {           /* motor on, then off: resets the ID */
        bus_out(&PIN_FDD_MOTOR_B, on);
        delay_us(2);
        bus_out(sel, true);
        delay_us(2);
        bus_out(sel, false);
        delay_us(2);
    }
    uint32_t v = 0;
    for (int i = 0; i < 32; i++) {
        bus_out(sel, true);
        delay_us(2);
        v = (v << 1) | (bus_in(&PIN_FDD_READY) ? 1u : 0u);
        bus_out(sel, false);
        delay_us(2);
    }
    bus_out(sel, true);                         /* leave the drive selected, motor off */
    g_drive_status[DRIVE_AMIGA].motor_on = false;
    *id = v;
    return UFI_OK;
}
