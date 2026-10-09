/**
 * UFI Flux Engine - Drive Control Module
 *
 * Shugart/PC (34-pin J6) and Amiga (DB23 header J7) drives on the shared FDD bus.
 * Pins and polarity come from board.h: bus_out(pin, true) asserts a line (low on the
 * cable), bus_in(pin) is true while the drive asserts it.
 *
 * Amiga J7 wiring: MTRXD = MOTOR_B, SEL1B = DRV_SEL_B, SEL2B = DRV_SEL_A (v0.7, only
 * with JP3 bridged).  Amiga drives latch MTRXD on the falling edge of their select line,
 * so the motor is switched via a select pulse and each drive keeps its own motor state.
 */

#include "ufi_firmware.h"

/* ============================================================================
 * GLOBALE VARIABLEN
 * ============================================================================ */

static drive_type_t g_current_drive = DRIVE_NONE;
static drive_status_t g_drive_status[DRIVE_TYPE_COUNT];

// Timing-Konstanten (µs)
// Timing (host-tunable per drive via UFI_CMD_DRIVE_TIMING, defaults = PC/Amiga safe values)
static drive_timing_t g_timing = {
    .step_pulse_us = 10, .step_rate_us = 3000, .settle_us = 15000, .dir_change_us = 0,
    .side_settle_us = 200, .spinup_ms = 500, .select_settle_us = 10000,
    .motor_off_s = 30, .double_step = 0, .precomp_ns = PRECOMP_AUTO,
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
 * APPLE DISK II (board v0.7: J14 = drive 1, J15 = /ENABLE of drive 2)
 * Four-phase stepper, one phase step = half a track, track n sits at phase (2n) & 3.
 * Positions are kept in quarter tracks (1.14): even quarters = one phase, odd quarters =
 * the two neighbouring phases energised together (head between the poles, phases stay on).
 * No track 0 sensor: recalibrate = 80 half steps outwards against the stop, ending on
 * phase 0 (as the Apple RWTS does).  /ENABLE runs the spindle and powers the stepper.
 * ============================================================================ */

#define APL_HALFSTEP_US     4000u       /* per half track (RWTS: ~2.5-3 ms at full speed) */
#define APL_OVERLAP_US      1000u       /* next phase on before the previous goes off */
#define APL_SETTLE_US       25000u
#define APL_MAX_TRACK       39

static const gpio_pin_t* const apl_ph[4] = {&PIN_APL_PH0, &PIN_APL_PH1, &PIN_APL_PH2, &PIN_APL_PH3};
static int16_t apl_qt[2] = {-1, -1};    /* quarter-track position per drive, -1 = unknown */
#define APL_MAX_QT          (4 * APL_MAX_TRACK)

static bool is_apple(drive_type_t t)
{
    return t == DRIVE_APPLE_II || t == DRIVE_APPLE2;
}

static const gpio_pin_t* apl_en(drive_type_t t)
{
    return t == DRIVE_APPLE2 ? &PIN_APL_EN2 : &PIN_APL_EN1;
}

static void apl_phase(int p, bool on)
{
    HAL_GPIO_WritePin(apl_ph[p & 3]->port, apl_ph[p & 3]->pin, on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

static void apl_release(void)
{
    for (int p = 0; p < 4; p++) {
        apl_phase(p, false);
    }
    for (int d = 0; d < 2; d++) {
        if (apl_qt[d] > 0 && (apl_qt[d] & 1)) {
            apl_qt[d] = -1;                 /* unpowered on a quarter: head snaps to a pole */
        }
    }
    HAL_GPIO_WritePin(PIN_APL_EN1.port, PIN_APL_EN1.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PIN_APL_EN2.port, PIN_APL_EN2.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_SET);
}

/* Move the current Apple drive to quarter track `target` (from qt, which may be assumed) */
static void apl_move_qt(int qt, int target)
{
    int h = qt / 2;                         /* half track; an odd qt sits between h and h+1 */
    if (qt & 1) {
        apl_phase(h + 1, false);            /* back onto the pole of h first */
        delay_us(APL_HALFSTEP_US);
    }
    const int th = target / 2;
    while (h != th) {
        const int next = h + (th > h ? 1 : -1);
        apl_phase(next, true);
        delay_us(APL_OVERLAP_US);
        apl_phase(h, false);
        delay_us(APL_HALFSTEP_US);
        h = next;
    }
    if (target & 1) {
        apl_phase(th, true);                /* both poles: the head settles in between and */
        apl_phase(th + 1, true);            /* the phases stay energised while it is there */
        delay_us(APL_SETTLE_US);
    } else {
        delay_us(APL_SETTLE_US);
        apl_phase(th, false);               /* stepper unpowered between seeks: no heating */
    }
    apl_qt[g_current_drive == DRIVE_APPLE2] = (int16_t)target;
    g_drive_status[g_current_drive].current_track = (uint8_t)(target / 4);
}

static int apl_recalibrate(void)
{
    apl_move_qt(4 * (APL_MAX_TRACK + 1), 0);    /* assume the far end: hits the stop on the way */
    g_drive_status[g_current_drive].track0 = true;
    return UFI_OK;
}

static int apl_seek_qt(int target)
{
    if (target < 0 || target > APL_MAX_QT) {
        return UFI_ERR_SEEK_FAIL;
    }
    const int qt = apl_qt[g_current_drive == DRIVE_APPLE2];
    if (qt < 0) {
        apl_recalibrate();
        apl_move_qt(0, target);
    } else {
        apl_move_qt(qt, target);
    }
    return UFI_OK;
}

static int apl_seek(uint8_t track)
{
    return apl_seek_qt(4 * track);
}

int ufi_drive_apple_step(int direction)
{
    const int qt = apl_qt[g_current_drive == DRIVE_APPLE2];
    if (qt < 0) {
        return apl_recalibrate();
    }
    int target = qt + 4 * (direction > 0 ? 1 : -1);
    if (target < 0) target = 0;
    if (target > APL_MAX_QT) target = APL_MAX_QT;
    apl_move_qt(qt, target);
    return UFI_OK;
}

/* Seek with a quarter-track offset (Apple only; quarter 0 = plain seek on any drive) */
int ufi_drive_seek_q(uint8_t track, uint8_t quarter)
{
    if (quarter == 0) {
        return ufi_drive_seek(track);
    }
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    if (!is_apple(g_current_drive) || quarter > 3u) {
        return UFI_ERR_BAD_ARGS;
    }
    return apl_seek_qt(4 * track + quarter);
}

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_drive_init(void)
{
    /* GPIO modes/levels are set by board_gpio_init(); all lines released */
    for (int i = 0; i < DRIVE_TYPE_COUNT; i++) {
        g_drive_status[i] = (drive_status_t){0};
        g_drive_status[i].type = DRIVE_NONE;
    }
    g_current_drive = DRIVE_NONE;
}

/* ============================================================================
 * LAUFWERK AUSWÄHLEN
 * ============================================================================ */

/* Shugart bus: select line per DS number (pin 10 = MOTOR_A line, pin 6 = DRATE line via
 * JP1); pin 16 = MOTOR ON shared by all drives */
static const gpio_pin_t* const shugart_sel[4] = {
    &PIN_FDD_MOTOR_A, &PIN_FDD_DRV_SEL_B, &PIN_FDD_DRV_SEL_A, &PIN_FDD_DRATE,
};

static bool is_shugart(drive_type_t t)
{
    return t >= DRIVE_SHUGART_DS0 && t <= DRIVE_SHUGART_DS3;
}

static bool is_amiga(drive_type_t t)
{
    return t == DRIVE_AMIGA || t == DRIVE_AMIGA2;
}

/* Amiga select line: DF1 SEL1B = DRV_SEL_B, DF2 SEL2B = DRV_SEL_A (JP3) */
static const gpio_pin_t* amiga_sel(drive_type_t t)
{
    return t == DRIVE_AMIGA2 ? &PIN_FDD_DRV_SEL_A : &PIN_FDD_DRV_SEL_B;
}

int ufi_drive_select(drive_type_t type)
{
    bus_out(&PIN_FDD_DRV_SEL_A, false);
    bus_out(&PIN_FDD_DRV_SEL_B, false);
    if (is_apple(g_current_drive) || is_apple(type)) {
        apl_release();                      /* both /ENABLE high: spindles stop */
        g_drive_status[DRIVE_APPLE_II].motor_on = false;
        g_drive_status[DRIVE_APPLE2].motor_on = false;
    }
    if (is_shugart(type) || is_shugart(g_current_drive)) {
        bus_out(&PIN_FDD_MOTOR_A, false);   /* DS0 line (PC: motor A) */
        bus_out(&PIN_FDD_DRATE, false);     /* DS3 line */
    }
    if (is_shugart(type)) {
        bus_out(shugart_sel[type - DRIVE_SHUGART_DS0], true);
        g_current_drive = type;
        g_drive_status[type].type = type;
        delay_us(SELECT_SETTLE_US);
        return UFI_OK;
    }

    switch (type) {
        case DRIVE_SHUGART_A:
            bus_out(&PIN_FDD_DRV_SEL_A, true);
            break;
        case DRIVE_SHUGART_B:
        case DRIVE_AMIGA:                   /* J7 SEL1B = DRV_SEL_B */
        case DRIVE_AMIGA2:                  /* J7 SEL2B = DRV_SEL_A via JP3 */
            /* the select edge latches MTRXD: present the drive's own motor state so
             * re-selecting (copy DF1 <-> DF2) does not stop it */
            bus_out(&PIN_FDD_MOTOR_B, g_drive_status[type].motor_on);
            delay_us(2);
            bus_out(amiga_sel(type), true);
            delay_us(2);
            bus_out(&PIN_FDD_MOTOR_B, false);
            break;
        case DRIVE_IEC:                     /* IEC needs no select */
            break;
        case DRIVE_APPLE_II:
        case DRIVE_APPLE2:
            if (!ufi_board_has_apple()) {
                g_current_drive = DRIVE_NONE;   /* no Apple port on this board */
                return UFI_ERR_NOT_IMPL;
            }
            /* /ENABLE starts the spindle and powers the stepper: on with the motor */
            break;
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
    const gpio_pin_t* sel = amiga_sel(g_current_drive);
    bus_out(sel, false);
    delay_us(2);
    bus_out(&PIN_FDD_MOTOR_B, on);
    delay_us(2);
    bus_out(sel, true);                     /* latch */
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
        case DRIVE_AMIGA2:
            amiga_motor(on);
            break;
        case DRIVE_IEC:                     /* 1541 controls its own motor */
            break;
        case DRIVE_APPLE_II:                /* Apple: motor follows /ENABLE (active low) */
        case DRIVE_APPLE2:
            HAL_GPIO_WritePin(apl_en(g_current_drive)->port, apl_en(g_current_drive)->pin,
                              on ? GPIO_PIN_RESET : GPIO_PIN_SET);
            break;
        case DRIVE_SHUGART_DS0:
        case DRIVE_SHUGART_DS1:
        case DRIVE_SHUGART_DS2:
        case DRIVE_SHUGART_DS3: {
            /* MOTOR ON (pin 16) is shared: all Shugart drives spin together */
            const bool was_on = g_drive_status[g_current_drive].motor_on;
            bus_out(&PIN_FDD_MOTOR_B, on);
            for (int t = DRIVE_SHUGART_DS0; t <= DRIVE_SHUGART_DS3; t++) {
                g_drive_status[t].motor_on = on;
            }
            if (on && !was_on) {
                HAL_Delay(MOTOR_SPINUP_MS);
            }
            return UFI_OK;
        }
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
    if (is_apple(g_current_drive)) {
        return ufi_drive_apple_step(direction);
    }

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
    if (is_apple(g_current_drive)) {
        return apl_seek(track);
    }
    const uint8_t max_track = 83;
    /* double step: logical track n sits at physical track 2n (40-track disk, 80-track drive);
     * current_track counts physical tracks */
    const uint32_t phys = g_timing.double_step ? 2u * track : track;
    if (phys > max_track) {
        return UFI_ERR_SEEK_FAIL;
    }
    track = (uint8_t)phys;

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
    if (is_apple(g_current_drive)) {
        return apl_recalibrate();
    }

    /* Three phases (concept from FloppyControl's seektrk00): out to TRK0, a few steps in
     * (TRK0 must release, otherwise the sensor sticks or the head sits beyond track 0),
     * then out again to TRK0. */
    drive_status_t* status = &g_drive_status[g_current_drive];
    int i = 0;
    while (!ufi_drive_at_track0()) {
        if (++i > 100) {
            return UFI_ERR_SEEK_FAIL;               /* no TRK0 within 100 steps */
        }
        ufi_drive_step(-1);
    }
    for (int k = 0; k < 6; k++) {
        ufi_drive_step(1);
    }
    if (ufi_drive_at_track0()) {
        return UFI_ERR_SEEK_FAIL;                   /* TRK0 stuck asserted */
    }
    for (i = 0; !ufi_drive_at_track0(); i++) {
        if (i > 10) {
            return UFI_ERR_SEEK_FAIL;
        }
        ufi_drive_step(-1);
    }
    status->current_track = 0;
    status->track0 = true;
    delay_us(SETTLE_TIME_US);
    return UFI_OK;
}

/* ============================================================================
 * SEITE / DENSITY
 * ============================================================================ */

int ufi_drive_select_side(uint8_t side)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    if (is_apple(g_current_drive)) {
        return side ? UFI_ERR_NOT_IMPL : UFI_OK;   /* Disk II: one head */
    }
    /* SIDE asserted (low) = head 1 */
    bus_out(&PIN_FDD_SIDE_SEL, side != 0);
    g_drive_status[g_current_drive].current_side = side;
    delay_us(g_timing.side_settle_us);
    return UFI_OK;
}

/* 3-mode drives (1.14): spindle speed 300/360 rpm selected by a bus line.  Which line and
 * which level mean 360 rpm depends on the drive's jumpers: UFI.CFG rpm_line=density|drate,
 * rpm_360=low|high.  DRATE is J6 pin 6 (JP1) and doubles as the Shugart DS3 line. */
static uint8_t rpm_line;                /* 0 none, 1 DENSITY (pin 2), 2 DRATE (pin 6) */
static bool rpm_360_low = true;         /* line asserted (low on the cable) = 360 rpm */
static uint16_t rpm_set;                /* 0 = not set since start */

void ufi_drive_rpm_config(uint8_t line, bool low_is_360)
{
    rpm_line = line > 2u ? 0u : line;
    rpm_360_low = low_is_360;
    rpm_set = 0;
}

uint8_t ufi_drive_rpm_line(void)
{
    return rpm_line;
}

uint16_t ufi_drive_get_rpm(void)
{
    return rpm_set;
}

int ufi_drive_set_rpm(uint16_t rpm)
{
    if (rpm != 300u && rpm != 360u) {
        return UFI_ERR_BAD_ARGS;
    }
    if (!rpm_line) {
        return UFI_ERR_NOT_IMPL;            /* no speed-select line configured */
    }
    const bool assert = (rpm == 360u) == rpm_360_low;
    bus_out(rpm_line == 2u ? &PIN_FDD_DRATE : &PIN_FDD_DENSITY, assert);
    const bool changed = rpm_set != rpm;
    rpm_set = rpm;
    if (changed && g_current_drive != DRIVE_NONE && g_drive_status[g_current_drive].motor_on) {
        HAL_Delay(MOTOR_SPINUP_MS);         /* let the spindle settle at the new speed */
    }
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
    if (is_apple(g_current_drive)) {
        return apl_qt[g_current_drive == DRIVE_APPLE2] == 0;   /* no sensor: position count */
    }
    return bus_in(&PIN_FDD_TRACK0);
}

bool ufi_drive_write_protected(void)
{
    if (is_apple(g_current_drive)) {        /* WRPROT high = protected (also: no drive) */
        return HAL_GPIO_ReadPin(PIN_APL_WRPROT.port, PIN_APL_WRPROT.pin) == GPIO_PIN_SET;
    }
    return bus_in(&PIN_FDD_WPROT);
}

/* J6 pin 34 is DSKCHG on the PC bus but READY on the Shugart bus (same input PF2);
 * the Shugart disk change output on pin 2 is not readable (pin 2 = DENSITY output).
 * The Disk II has neither line. */
bool ufi_drive_disk_changed(void)
{
    return (is_shugart(g_current_drive) || is_apple(g_current_drive)) ? false : bus_in(&PIN_FDD_DKCHG);
}

bool ufi_drive_ready(void)
{
    if (is_apple(g_current_drive)) {
        return true;
    }
    return bus_in(is_shugart(g_current_drive) ? &PIN_FDD_DKCHG : &PIN_FDD_READY);
}

bool ufi_drive_is_apple(void)
{
    return is_apple(g_current_drive);
}

bool ufi_drive_is_shugart_bus(void)
{
    return is_shugart(g_current_drive);
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

bool ufi_drive_motor_is_on(void)
{
    return g_current_drive != DRIVE_NONE && g_drive_status[g_current_drive].motor_on;
}

/* Everything that could move the head or write: off/released (USB lost, abort) */
void ufi_drive_safe_state(void)
{
    if (ufi_drive_motor_is_on()) {
        ufi_drive_motor(false);
    }
    /* Amiga drives keep their latched motor state while deselected */
    static const drive_type_t amiga_drives[2] = {DRIVE_AMIGA, DRIVE_AMIGA2};
    for (int i = 0; i < 2; i++) {
        if (g_drive_status[amiga_drives[i]].motor_on) {
            ufi_drive_select(amiga_drives[i]);
            ufi_drive_motor(false);
        }
    }
    bus_out(&PIN_FDD_WGATE, false);
    apl_release();                          /* Apple: /WRREQ, both /ENABLE high, phases off */
    g_drive_status[DRIVE_APPLE_II].motor_on = false;
    g_drive_status[DRIVE_APPLE2].motor_on = false;
    bus_out(&PIN_FDD_STEP, false);
    bus_out(&PIN_FDD_MOTOR_A, false);
    bus_out(&PIN_FDD_MOTOR_B, false);
    bus_out(&PIN_FDD_DRATE, false);         /* Shugart DS3 line */
    for (int t = DRIVE_SHUGART_DS0; t <= DRIVE_SHUGART_DS3; t++) {
        g_drive_status[t].motor_on = false;
    }
    ufi_drive_select(DRIVE_NONE);
}

/* Diagnostics: seek back and forth between two tracks (stepper / head positioning test,
 * audible and on a scope at STEP).  Blocking; cycles capped at 50. */
int ufi_drive_seek_test(uint8_t a, uint8_t b, uint8_t cycles)
{
    if (cycles > 50) {
        cycles = 50;
    }
    for (uint8_t i = 0; i < cycles; i++) {
        if (ufi_drive_seek(a) != UFI_OK || ufi_drive_seek(b) != UFI_OK) {
            return UFI_ERR_SEEK_FAIL;
        }
        UFI_WATCHDOG_FEED();
    }
    return UFI_OK;
}

/* Highest reachable track: from track 0 step in until the mechanical end stop (the head
 * simply stops there), then count the steps back to TRK0.  Gentle: double step rate.
 * Note: some drives knock audibly at the end stop. */
int ufi_drive_probe_tracks(uint8_t* highest)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC ||
        g_current_drive == DRIVE_APPLE_II) {
        return UFI_ERR_NO_DRIVE;
    }
    if (ufi_drive_recalibrate() != UFI_OK) {
        return UFI_ERR_SEEK_FAIL;
    }
    const uint16_t rate = g_timing.step_rate_us;
    g_timing.step_rate_us = (rate < 30000u) ? (uint16_t)(rate * 2u) : rate;
    for (int i = 0; i < 90; i++) {
        ufi_drive_step(1);
    }
    int n = 0;
    while (!ufi_drive_at_track0() && n < 100) {
        ufi_drive_step(-1);
        n++;
    }
    g_timing.step_rate_us = rate;
    if (!ufi_drive_at_track0()) {
        return UFI_ERR_SEEK_FAIL;
    }
    g_drive_status[g_current_drive].current_track = 0;
    g_drive_status[g_current_drive].track0 = true;
    *highest = (uint8_t)n;
    return UFI_OK;
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
    if (!is_amiga(g_current_drive)) {
        return UFI_ERR_NO_DRIVE;
    }
    const gpio_pin_t* sel = amiga_sel(g_current_drive);
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
    g_drive_status[g_current_drive].motor_on = false;
    *id = v;
    return UFI_OK;
}
