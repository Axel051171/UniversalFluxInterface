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
#include "ufi_drive_extra.h"
#include "ufi_verify.h"             /* ufi_crc32 for the stored profile file */
#include "ufi_media.h"
#include "ufi_capabilities.h"
#include <string.h>
#define DEFAULT_EXTRA_INIT {0,0,20000,0,0,0}
static drive_extra_t extra_timing=DEFAULT_EXTRA_INIT;
static uint32_t motor_started_ms;
static bool motor_step_pending;
static void motor_step_wait(void){
    if(!motor_step_pending)return;
    uint32_t elapsed=HAL_GetTick()-motor_started_ms;
    if(elapsed<extra_timing.motor_step_ms)HAL_Delay(extra_timing.motor_step_ms-elapsed);
    motor_step_pending=false;
}
drive_extra_t ufi_drive_extra_get(void){return extra_timing;}
bool ufi_drive_extra_set(const drive_extra_t* t){if(!drive_extra_valid(t))return false;extra_timing=*t;ufi_config_changed();return true;}

/* ============================================================================
 * GLOBALE VARIABLEN
 * ============================================================================ */

static drive_type_t g_current_drive = DRIVE_NONE;
static drive_status_t g_drive_status[DRIVE_TYPE_COUNT];
static media_state_t media[DRIVE_TYPE_COUNT];

// Timing-Konstanten (µs)
// Timing (host-tunable per drive via UFI_CMD_DRIVE_TIMING, defaults = PC/Amiga safe values)
#define DEFAULT_TIMING_INIT { \
    .step_pulse_us = 10, .step_rate_us = 3000, .settle_us = 15000, .dir_change_us = 15000, \
    .side_settle_us = 200, .spinup_ms = 500, .select_settle_us = 10000, \
    .motor_off_s = 30, .double_step = 0, .precomp_ns = PRECOMP_AUTO, .tg43 = 0, \
}
static drive_timing_t g_timing = DEFAULT_TIMING_INIT;
static void tg43_apply(uint32_t phys);
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
            media[d?DRIVE_APPLE2:DRIVE_APPLE_II].position_valid=false;
        }
    }
    HAL_GPIO_WritePin(PIN_APL_EN1.port, PIN_APL_EN1.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PIN_APL_EN2.port, PIN_APL_EN2.pin, GPIO_PIN_SET);
    HAL_GPIO_WritePin(PIN_APL_WRREQ.port, PIN_APL_WRREQ.pin, GPIO_PIN_SET);
}

/* Move the current Apple drive to quarter track `target` (from qt, which may be assumed) */
static void apl_move_qt(int qt, int target)
{
    motor_step_wait();
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
    media[g_current_drive].position_valid=true;
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
    if(target/4>media[g_current_drive].drive[1])return UFI_ERR_SEEK_FAIL;
    if(!media[g_current_drive].position_valid)apl_qt[g_current_drive==DRIVE_APPLE2]=-1;
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
    /* Apple port only: on any other drive the phase lines move nothing, and the
     * recalibration below would mark that drive's position valid (found by fuzz_drive). */
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return UFI_ERR_NO_DRIVE;
    if(!is_apple(g_current_drive))return UFI_ERR_NOT_IMPL;
    if(!media[g_current_drive].position_valid)return apl_recalibrate();
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

static int seek_physical(uint32_t phys);

/* Quarter form accepted for the selected drive: Apple 0..3; Shugart only the half track (2)
 * when the profile double-steps, where logical track n sits at physical 2n and the half track
 * at 2n+1 (1.18).  A plain stepper has no position between its steps otherwise. */
bool ufi_drive_quarter_ok(uint8_t quarter)
{
    if (quarter == 0) return true;
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) return false;
    if (is_apple(g_current_drive)) return quarter <= 3u;
    return quarter == 2u && g_timing.double_step != 0;
}

/* Seek with a quarter-track offset (Apple 0..3; Shugart half track with double step; quarter 0 =
 * plain seek on any drive) */
int ufi_drive_seek_q(uint8_t track, uint8_t quarter)
{
    if (quarter == 0) {
        return ufi_drive_seek(track);
    }
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    if (!ufi_drive_quarter_ok(quarter)) {
        return UFI_ERR_BAD_ARGS;
    }
    if (!is_apple(g_current_drive)) {
        return seek_physical(2u * track + 1u);      /* half track between logical n and n+1 */
    }
    return apl_seek_qt(4 * track + quarter);
}

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_drive_init(void)
{
    ufi_config_changed();
    /* GPIO modes/levels are set by board_gpio_init(); all lines released */
    for (int i = 0; i < DRIVE_TYPE_COUNT; i++) {
        g_drive_status[i] = (drive_status_t){0};
        g_drive_status[i].type = DRIVE_NONE;
        bool apple=i==DRIVE_APPLE_II || i==DRIVE_APPLE2;
        media_init(&media[i],apple?39:83,apple?1:2,
                   i==DRIVE_SHUGART_A || i==DRIVE_SHUGART_B || i==DRIVE_AMIGA || i==DRIVE_AMIGA2);
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
    if((unsigned)type>=DRIVE_TYPE_COUNT)return UFI_ERR_BAD_ARGS;
    ufi_config_changed(); /* selection releases shared lines, even on failure */
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

uint32_t ufi_drive_media_token(void){
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return 0;
    media_state_t* s=&media[g_current_drive];
    if(ufi_drive_motor_is_on())media_observe(s,ufi_drive_disk_changed());
    return s->generation;
}
bool ufi_drive_media_matches(uint32_t token){
    return token && token==ufi_drive_media_token();
}
void ufi_drive_media_status(uint8_t out[40]){
    (void)ufi_drive_media_token();
    media_pack(&media[g_current_drive],(uint8_t)g_current_drive,g_drive_status[g_current_drive].current_track,
               g_drive_status[g_current_drive].current_side,is_apple(g_current_drive),out);
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC){out[2]=0;memset(out+4,0,4);}
}
int ufi_drive_profile_set(const uint8_t p[8]){
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return UFI_ERR_NO_DRIVE;
    if(!media_drive_valid(p) || (is_apple(g_current_drive) && (p[1]>39 || p[2]!=1)) ||
       ((is_apple(g_current_drive) || is_shugart(g_current_drive)) && p[3]))return UFI_ERR_BAD_ARGS;
    ufi_config_changed();
    media_state_t* s=&media[g_current_drive];memcpy(s->drive,p,8);s->position_valid=false;
    s->change_asserted=false;media_invalidate(s,3);(void)ufi_drive_media_token();return UFI_OK;
}
int ufi_drive_media_set(const uint8_t p[12]){
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return UFI_ERR_NO_DRIVE;
    if(!media_profile_valid(p))return UFI_ERR_BAD_ARGS;
    (void)ufi_drive_media_token();
    const uint8_t* d=media[g_current_drive].drive;
    if(p[0] && ((p[1] && (unsigned)(p[1]-1)*p[5]>d[1]) || p[2]>d[2]))return UFI_ERR_BAD_ARGS;
    memcpy(media[g_current_drive].media,p,12);ufi_config_changed();return UFI_OK;
}
void ufi_drive_media_notify(void){
    if(g_current_drive!=DRIVE_NONE && g_current_drive!=DRIVE_IEC)media_invalidate(&media[g_current_drive],2);
}
void ufi_drive_position_invalidate(void){
    for(unsigned i=0;i<DRIVE_TYPE_COUNT;i++)media[i].position_valid=false;
}
void ufi_drive_media_observed(uint8_t format,uint32_t rate){
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return;
    media_state_t* s=&media[g_current_drive];if(s->media[6]==1)return;
    uint8_t p[12]={0};p[0]=format;p[5]=g_timing.double_step?2:1;p[6]=1;memcpy(p+8,&rate,4);
    if(media_profile_valid(p)){p[6]=2;memcpy(s->media,p,12);}
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
    if(on && !ufi_drive_motor_is_on()){motor_started_ms=HAL_GetTick();motor_step_pending=true;}
    if(!on)motor_step_pending=false;
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
    motor_step_wait();
    (void)ufi_drive_media_token(); /* sample DSKCHG before STEP can clear its latch */
    if (is_apple(g_current_drive)) {
        return ufi_drive_apple_step(direction);
    }
    if(media[g_current_drive].position_valid && direction>0 &&
       g_drive_status[g_current_drive].current_track>=media[g_current_drive].drive[1])return UFI_ERR_SEEK_FAIL;

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
    /* double step: logical track n sits at physical track 2n (40-track disk, 80-track drive);
     * current_track counts physical tracks */
    return seek_physical(g_timing.double_step ? 2u * track : track);
}

/* Shugart step sequence to a physical track (half tracks of a double-step profile included) */
static int seek_physical(uint32_t phys)
{
    const uint8_t max_track = media[g_current_drive].drive[1];
    if (phys > max_track) {
        return UFI_ERR_SEEK_FAIL;
    }
    uint8_t track = (uint8_t)phys;

    drive_status_t* status = &g_drive_status[g_current_drive];
    if (!media[g_current_drive].position_valid) {
        /* head position unknown: find track 0 first */
        if (ufi_drive_recalibrate() != UFI_OK) {
            return UFI_ERR_SEEK_FAIL;
        }
    }

    int steps = (int)track - (int)status->current_track;
    int direction = (steps > 0) ? 1 : -1;
    steps = (steps > 0) ? steps : -steps;

    for (int i = 0; i < steps; i++) {
        if(ufi_drive_step(direction)!=UFI_OK){media[g_current_drive].position_valid=false;return UFI_ERR_SEEK_FAIL;}
        if (direction < 0 && ufi_drive_at_track0()) {
            status->current_track = 0;
            if(track!=0){media[g_current_drive].position_valid=false;return UFI_ERR_SEEK_FAIL;}
            break;
        }
    }
    status->track0 = ufi_drive_at_track0();
    if(status->track0!=(track==0)){media[g_current_drive].position_valid=false;return UFI_ERR_SEEK_FAIL;}
    tg43_apply(status->current_track);

    delay_us(SETTLE_TIME_US);
    return UFI_OK;
}

int ufi_drive_recalibrate(void)
{
    if (g_current_drive == DRIVE_NONE || g_current_drive == DRIVE_IEC) {
        return UFI_ERR_NO_DRIVE;
    }
    media[g_current_drive].position_valid=false;
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
    media[g_current_drive].position_valid=true;
    tg43_apply(0);
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
    if(side>=media[g_current_drive].drive[2])return UFI_ERR_BAD_ARGS;
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
static uint8_t operating_profiles[DRIVE_TYPE_COUNT][40];

/* Conflict bits: DRATE/DS3=1, DENSITY/TG43=2, unsupported port=4. */
static uint8_t line_conflicts(drive_type_t drive,uint8_t line,bool tg43)
{
    uint8_t reasons=0;
    if(line==2 && is_shugart(drive))reasons|=1;
    if(line==1 && tg43)reasons|=2;
    if((line || tg43) && (drive==DRIVE_NONE || drive==DRIVE_IEC || is_apple(drive)))reasons|=4;
    return reasons;
}

void ufi_drive_port_report(uint8_t out[24])
{
    memset(out,0,24);out[0]=1;out[1]=(uint8_t)g_current_drive;
    bool flux=g_current_drive!=DRIVE_NONE && g_current_drive!=DRIVE_IEC;
    out[2]=flux?1:0; /* shared flux engine */
    if(is_shugart(g_current_drive))out[2]|=2|4; /* shared motor, DRATE reserved for DS3 */
    if(flux && !is_apple(g_current_drive))out[2]|=8; /* density output */
    if(media[g_current_drive].drive[3] && flux)out[2]|=16;
    if(is_shugart(g_current_drive))out[2]|=32; /* pin 34 READY */
    if(g_current_drive==DRIVE_SHUGART_DS3 || g_current_drive==DRIVE_AMIGA2)out[2]|=64; /* jumper required */
    out[3]=is_shugart(g_current_drive)?15:(g_current_drive==DRIVE_SHUGART_A || g_current_drive==DRIVE_SHUGART_B)?3:0;
    out[4]=rpm_line;out[5]=rpm_360_low;out[6]=(uint8_t)g_timing.tg43;
    out[7]=line_conflicts(g_current_drive,rpm_line,g_timing.tg43!=0);
    memcpy(out+8,&rpm_set,2);out[10]=operating_profiles[g_current_drive][39];
    uint32_t limit=WRITE_MIN_DELTA;memcpy(out+12,&limit,4);
    limit=WRITE_MAX_DELTA;memcpy(out+16,&limit,4);
    limit=FLUX_TIMER_FREQ;memcpy(out+20,&limit,4);
}

int ufi_drive_timing_checked(const drive_timing_t* t)
{
    if(t->double_step>1 || t->tg43>1 || line_conflicts(g_current_drive,rpm_line,t->tg43!=0))return UFI_ERR_BAD_ARGS;
    ufi_drive_set_timing(t);return UFI_OK;
}

int ufi_drive_operating_profile(const uint8_t* input,uint8_t out[40])
{
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return UFI_ERR_NO_DRIVE;
    if(input){
        drive_timing_t timing;drive_extra_t extra;uint16_t rpm;
        memcpy(&timing,input+1,22);memcpy(&extra,input+23,12);memcpy(&rpm,input+37,2);
        if(input[0]!=1 || input[35]>2 || input[36]>1 || input[39] ||
           timing.double_step>1 || timing.tg43>1 || !timing.step_pulse_us ||
           !drive_extra_valid(&extra) || (rpm && rpm!=300 && rpm!=360) ||
           ((rpm!=0)!=(input[35]!=0)) || line_conflicts(g_current_drive,input[35],timing.tg43!=0))return UFI_ERR_BAD_ARGS;
        /* Validate everything before changing settings or physical lines. */
        g_timing=timing;extra_timing=extra;ufi_drive_rpm_config(input[35],input[36]!=0);
        if(rpm){int ret=ufi_drive_set_rpm(rpm);if(ret)return ret;}
        if(!is_apple(g_current_drive)){
            if(rpm_line!=1){
                bus_out(&PIN_FDD_DENSITY,g_timing.tg43 && media[g_current_drive].position_valid &&
                        g_drive_status[g_current_drive].current_track>=43);
            }
            if(rpm_line!=2 && !is_shugart(g_current_drive))bus_out(&PIN_FDD_DRATE,false);
            delay_us(extra_timing.density_us);
        }
        memcpy(operating_profiles[g_current_drive],input,40);operating_profiles[g_current_drive][39]=1;
    }
    memset(out,0,40);out[0]=1;memcpy(out+1,&g_timing,22);memcpy(out+23,&extra_timing,12);
    out[35]=rpm_line;out[36]=rpm_360_low;memcpy(out+37,&rpm_set,2);
    out[39]=operating_profiles[g_current_drive][39];return UFI_OK;
}

/* Named timing presets (1.18): 1 = factory defaults, 2 = conservative (the KryoFlux 3.00
 * defaults: step 8 ms, head settle 40 ms, 38 ms on direction reversal, 1 ms side select,
 * 800 ms spin-up, 60 ms drive select, 200 ms motor-to-first-step, density settle capped at
 * the 60 ms field limit).  Only the movement/settle timings change; step pulse, motor-off,
 * double step, precompensation, TG43, write settle, WGATE margins and the RPM line keep their
 * current values.  Sets the effective values like TIMING/Op 10 do and does not touch the
 * stored per-drive profile; the reply is the effective 40-byte profile. */
int ufi_drive_operating_preset(uint8_t id,uint8_t out[40])
{
    if(g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC)return UFI_ERR_NO_DRIVE;
    if(id<1 || id>2)return UFI_ERR_BAD_ARGS;
    drive_timing_t t=g_timing;drive_extra_t e=extra_timing;
    if(id==1){
        const drive_timing_t d=DEFAULT_TIMING_INIT;const drive_extra_t x=DEFAULT_EXTRA_INIT;
        t.step_rate_us=d.step_rate_us;t.settle_us=d.settle_us;t.dir_change_us=d.dir_change_us;
        t.side_settle_us=d.side_settle_us;t.spinup_ms=d.spinup_ms;t.select_settle_us=d.select_settle_us;
        e.density_us=x.density_us;e.motor_step_ms=x.motor_step_ms;
    }else{
        t.step_rate_us=8000;t.settle_us=40000;t.dir_change_us=38000;
        t.side_settle_us=1000;t.spinup_ms=800;t.select_settle_us=60000;
        e.density_us=60000;e.motor_step_ms=200;
    }
    ufi_drive_set_timing(&t);
    (void)ufi_drive_extra_set(&e);
    return ufi_drive_operating_profile(NULL,out);
}

/* Stored operating profiles survive a restart in UFIPROF.BIN on the SD NAND (1.18):
 * "UFIPROF" + schema byte 1, DRIVE_TYPE_COUNT x 40 profile bytes, CRC32 (IEEE) over all
 * of it.  Written when Op 18 stores a profile, read once after UFI.CFG.  A missing,
 * short, damaged or differently sized file leaves the profiles empty; a slot that does
 * not pass the drive-independent shape check is dropped on its own. */
#define PROFILE_FILE        "UFIPROF.BIN"
#define PROFILE_FILE_SIZE   (8u + 40u * DRIVE_TYPE_COUNT + 4u)

static bool profile_shape_ok(const uint8_t* p)
{
    drive_timing_t t;drive_extra_t e;uint16_t rpm;
    memcpy(&t,p+1,22);memcpy(&e,p+23,12);memcpy(&rpm,p+37,2);
    return p[0]==1 && p[39]==1 && t.step_pulse_us && t.double_step<=1 && t.tg43<=1 &&
        drive_extra_valid(&e) && p[35]<=2 && p[36]<=1 && (rpm==0 || rpm==300 || rpm==360) &&
        ((rpm!=0)==(p[35]!=0));
}

int ufi_drive_profiles_save(void)
{
    uint8_t buf[PROFILE_FILE_SIZE];
    memcpy(buf,"UFIPROF",7);buf[7]=1;
    for(unsigned i=0;i<DRIVE_TYPE_COUNT;i++)memcpy(buf+8+40*i,operating_profiles[i],40);
    const uint32_t crc=ufi_crc32(0,buf,PROFILE_FILE_SIZE-4u);
    memcpy(buf+PROFILE_FILE_SIZE-4u,&crc,4);
    uint16_t written=0;
    const int ret=ufi_file_write(PROFILE_FILE,0,true,buf,PROFILE_FILE_SIZE,&written);
    return ret?ret:(written==PROFILE_FILE_SIZE?UFI_OK:UFI_ERR_STORAGE);
}

void ufi_drive_profiles_load(void)
{
    uint8_t buf[PROFILE_FILE_SIZE+1];uint16_t got=0;
    if(ufi_file_read(PROFILE_FILE,0,buf,sizeof buf,&got) || got!=PROFILE_FILE_SIZE ||
       memcmp(buf,"UFIPROF",7) || buf[7]!=1)return;
    uint32_t crc;memcpy(&crc,buf+PROFILE_FILE_SIZE-4u,4);
    if(crc!=ufi_crc32(0,buf,PROFILE_FILE_SIZE-4u))return;
    for(unsigned i=0;i<DRIVE_TYPE_COUNT;i++){
        const uint8_t* p=buf+8+40*i;
        if(profile_shape_ok(p))memcpy(operating_profiles[i],p,40);
        else memset(operating_profiles[i],0,40);
    }
}

int ufi_drive_select_native(drive_type_t type)
{
    if(type>=DRIVE_TYPE_COUNT)return UFI_ERR_BAD_ARGS;
    drive_timing_t previous_timing=g_timing;drive_extra_t previous_extra=extra_timing;
    if(operating_profiles[type][39]){
        memcpy(&g_timing,operating_profiles[type]+1,22);
        memcpy(&extra_timing,operating_profiles[type]+23,12);
    }
    int ret=ufi_drive_select(type);
    if(ret){g_timing=previous_timing;extra_timing=previous_extra;return ret;}
    if(!operating_profiles[type][39])return UFI_OK;
    uint8_t profile[40],out[40];memcpy(profile,operating_profiles[type],40);profile[39]=0;
    return ufi_drive_operating_profile(profile,out);
}

void ufi_drive_rpm_config(uint8_t line, bool low_is_360)
{
    ufi_config_changed();
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
    if(line_conflicts(g_current_drive,rpm_line,g_timing.tg43!=0))return UFI_ERR_BAD_ARGS;
    if (rpm != 300u && rpm != 360u) {
        return UFI_ERR_BAD_ARGS;
    }
    if (!rpm_line) {
        return UFI_ERR_NOT_IMPL;            /* no speed-select line configured */
    }
    ufi_config_changed();
    const bool assert = (rpm == 360u) == rpm_360_low;
    bus_out(rpm_line == 2u ? &PIN_FDD_DRATE : &PIN_FDD_DENSITY, assert);
    delay_us(extra_timing.density_us);
    const bool changed = rpm_set != rpm;
    rpm_set = rpm;
    if (changed && g_current_drive != DRIVE_NONE && g_drive_status[g_current_drive].motor_on) {
        HAL_Delay(MOTOR_SPINUP_MS);         /* let the spindle settle at the new speed */
    }
    return UFI_OK;
}

int ufi_drive_density_line(bool assert)
{
    if(rpm_line==1 || g_timing.tg43)return UFI_ERR_BAD_ARGS;
    ufi_config_changed();
    bus_out(&PIN_FDD_DENSITY, assert);
    delay_us(extra_timing.density_us);
    return UFI_OK;
}
int ufi_drive_lines_set(bool density,bool drate){
    if(is_shugart(g_current_drive) || rpm_line || g_timing.tg43 ||
       g_current_drive==DRIVE_NONE || g_current_drive==DRIVE_IEC || is_apple(g_current_drive))return UFI_ERR_BAD_ARGS;
    ufi_config_changed();
    bus_out(&PIN_FDD_DENSITY,density);bus_out(&PIN_FDD_DRATE,drate);
    delay_us(extra_timing.density_us);
    return UFI_OK;
}

/* TG43 (1.16): 8" drives reduce the write current on the inner tracks; pin 2 of the
 * Shugart bus is TG43 there ("track greater than 43", active low).  With timing.tg43 the
 * DENSITY output follows the physical head position after every seek / recalibrate,
 * unless the line is in use as the 3-mode speed select. */
static void tg43_apply(uint32_t phys)
{
    if (g_timing.tg43 && rpm_line != 1u && !is_apple(g_current_drive)) {
        bus_out(&PIN_FDD_DENSITY,phys >= 43u);
        delay_us(extra_timing.density_us);
    }
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
    ufi_drive_position_invalidate();
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
    media[g_current_drive].position_valid=false; /* explicit end-stop probe counts actual return steps */
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
    media[g_current_drive].position_valid=true;
    *highest = (uint8_t)n;
    return UFI_OK;
}

void ufi_drive_set_timing(const drive_timing_t* t)
{
    ufi_config_changed();
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
