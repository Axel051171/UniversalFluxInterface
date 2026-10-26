/**
 * UFI Flux Engine - Macintosh 3.5" drive: Sony register interface (HAL-free)
 *
 * See ufi_mac.h for the register numbering and the hardware contract.  Every function
 * leaves the address lines on the RDDATA register of the current head, so RD carries
 * read data again when a capture or write follows (the same address selects the head
 * the drive writes with while /WRREQ is low).
 */

#include "ufi_mac.h"

static uint8_t rest_reg(const mac_drive_t* d)
{
    return (uint8_t)(MAC_REG_RDDATA0 | (d->side & 1u));
}

static bool read_reg(const mac_io_t* io, uint8_t reg)
{
    io->lines(io->ctx, (uint8_t)(reg & 15u));
    io->wait_us(io->ctx, MAC_SENSE_SETTLE_US);
    return io->rd(io->ctx);
}

static bool elapsed(const mac_io_t* io, uint32_t since, uint32_t us)
{
    return io->ticks(io->ctx) - since > us * io->ticks_per_us;
}

void mac_reset(mac_drive_t* d)
{
    *d = (mac_drive_t){0};
    d->track = -1;
}

bool mac_sense(const mac_io_t* io, const mac_drive_t* d, uint8_t reg)
{
    const bool v = read_reg(io, reg);
    io->lines(io->ctx, rest_reg(d));
    return v;
}

void mac_command(const mac_io_t* io, const mac_drive_t* d, uint8_t cmd, uint32_t hold_us)
{
    io->lines(io->ctx, (uint8_t)(cmd & 15u));
    io->wait_us(io->ctx, MAC_SETUP_US);
    io->strobe(io->ctx, true);
    io->wait_us(io->ctx, hold_us);
    io->strobe(io->ctx, false);
    io->wait_us(io->ctx, MAC_SETUP_US);
    io->lines(io->ctx, rest_reg(d));
}

void mac_head(const mac_io_t* io, mac_drive_t* d, uint8_t side)
{
    d->side = side & 1u;
    io->lines(io->ctx, rest_reg(d));
    io->wait_us(io->ctx, MAC_SENSE_SETTLE_US);
}

uint16_t mac_sense_all(const mac_io_t* io, mac_drive_t* d)
{
    uint16_t s = 0;
    for (uint8_t r = 0; r < 16u; r++) {
        if (read_reg(io, r)) {
            s |= (uint16_t)(1u << r);
        }
    }
    io->lines(io->ctx, rest_reg(d));
    d->sense = s;
    return s;
}

/* Two complete status reads.  Tachometer and the two data registers change on their own,
 * every other bit must read the same twice.  A floating or stuck RD line (no drive, drive
 * unpowered) gives identical or unstable bits; DRVIN must say "installed". */
uint8_t mac_classify(uint16_t first, uint16_t second)
{
    const uint16_t stable = (uint16_t)~((1u << MAC_REG_TACH) | (1u << MAC_REG_RDDATA0) |
                                        (1u << MAC_REG_RDDATA1));
    if ((first ^ second) & stable) {
        return MAC_KIND_NONE;
    }
    const uint16_t s = first & stable;
    if (s == 0u || s == stable || (first & (1u << MAC_REG_DRVIN))) {
        return MAC_KIND_NONE;
    }
    if (!(first & (1u << MAC_REG_SIDES))) {
        return MAC_KIND_400K;
    }
    return (first & (1u << MAC_REG_SUPERDR)) ? MAC_KIND_FDHD : MAC_KIND_800K;
}

uint8_t mac_probe(const mac_io_t* io, mac_drive_t* d)
{
    const uint16_t a = mac_sense_all(io, d);
    const uint16_t b = mac_sense_all(io, d);
    d->kind = mac_classify(a, b);
    return d->kind;
}

uint8_t mac_flags(uint16_t s)
{
    uint8_t f = 0;
    if (!(s & (1u << MAC_REG_CSTIN))) f |= MAC_F_DISK;
    if (!(s & (1u << MAC_REG_WRTPRT))) f |= MAC_F_WPROT;
    if (!(s & (1u << MAC_REG_MOTORON))) f |= MAC_F_MOTOR;
    if (!(s & (1u << MAC_REG_READY))) f |= MAC_F_READY;
    if (!(s & (1u << MAC_REG_TK0))) f |= MAC_F_TRACK0;
    if (!(s & (1u << MAC_REG_STEP))) f |= MAC_F_STEPPING;
    return f;
}

/* One track: latch the direction when it changes, strobe STEP, wait until the STEP bit
 * reports the step finished.  A step that never finishes loses the position. */
int mac_step(const mac_io_t* io, mac_drive_t* d, int dir)
{
    if (dir == 0) {
        return MAC_ERR_BAD_ARGS;
    }
    dir = dir > 0 ? 1 : -1;
    if (dir > 0 && d->track >= (int)MAC_TRACKS - 1) {
        return MAC_ERR_SEEK;
    }
    if (d->dir != dir) {
        mac_command(io, d, dir > 0 ? MAC_CMD_DIR_IN : MAC_CMD_DIR_OUT, MAC_STROBE_US);
        d->dir = (int8_t)dir;
    }
    mac_command(io, d, MAC_CMD_STEP, MAC_STROBE_US);
    io->wait_us(io->ctx, MAC_STEP_START_US);
    const uint32_t t0 = io->ticks(io->ctx);
    while (!read_reg(io, MAC_REG_STEP)) {
        if (elapsed(io, t0, MAC_STEP_TIMEOUT_US)) {
            io->lines(io->ctx, rest_reg(d));
            d->track = -1;
            return MAC_ERR_SEEK;
        }
        io->wait_us(io->ctx, MAC_STEP_POLL_US);
    }
    io->lines(io->ctx, rest_reg(d));
    if (d->track >= 0) {
        const int t = d->track + dir;
        d->track = (int8_t)(t < 0 ? 0 : t);
    }
    return MAC_OK;
}

/* Out to TK0, one step in (TK0 must release, otherwise the sensor sticks), back to TK0. */
int mac_recalibrate(const mac_io_t* io, mac_drive_t* d)
{
    d->track = -1;
    unsigned n = 0;
    while (mac_sense(io, d, MAC_REG_TK0)) {
        if (++n > MAC_RECAL_STEPS) {
            return MAC_ERR_SEEK;
        }
        const int ret = mac_step(io, d, -1);
        if (ret) {
            return ret;
        }
    }
    int ret = mac_step(io, d, 1);
    if (ret) {
        return ret;
    }
    if (!mac_sense(io, d, MAC_REG_TK0)) {
        return MAC_ERR_SEEK;                /* TK0 stuck asserted */
    }
    ret = mac_step(io, d, -1);
    if (ret) {
        return ret;
    }
    if (mac_sense(io, d, MAC_REG_TK0)) {
        return MAC_ERR_SEEK;
    }
    d->track = 0;
    return MAC_OK;
}

int mac_seek(const mac_io_t* io, mac_drive_t* d, uint8_t track)
{
    if (track >= MAC_TRACKS) {
        return MAC_ERR_BAD_ARGS;
    }
    if (d->track < 0) {
        const int ret = mac_recalibrate(io, d);
        if (ret) {
            return ret;
        }
    }
    while (d->track != (int)track) {
        const int ret = mac_step(io, d, (int)track > d->track ? 1 : -1);
        if (ret) {
            d->track = -1;
            return ret;
        }
    }
    if (mac_sense(io, d, MAC_REG_TK0) == (track == 0u)) {
        d->track = -1;                      /* TK0 disagrees with the counted position */
        return MAC_ERR_SEEK;
    }
    return MAC_OK;
}

void mac_motor(const mac_io_t* io, mac_drive_t* d, bool on)
{
    mac_command(io, d, on ? MAC_CMD_MOTOR_ON : MAC_CMD_MOTOR_OFF, MAC_STROBE_US);
    d->motor = on;
}

void mac_eject(const mac_io_t* io, mac_drive_t* d)
{
    mac_command(io, d, MAC_CMD_EJECT, MAC_EJECT_HOLD_US);
    d->track = -1;
    d->motor = false;
}

uint16_t mac_rpm_x100(uint32_t rev_ticks, uint32_t ticks_per_us)
{
    if (!rev_ticks) {
        return 0;
    }
    const uint64_t r = (6000000000ull * ticks_per_us + rev_ticks / 2u) / rev_ticks;
    return r > 0xFFFFu ? 0xFFFFu : (uint16_t)r;
}

/* Revolution times from the tachometer: MAC_TACH_PER_REV rising edges per revolution,
 * timed from the first rising edge.  Busy polling; the pulses are >= 1.5 ms apart. */
int mac_tach_revs(const mac_io_t* io, mac_drive_t* d, uint8_t revs,
                  uint32_t* min_ticks, uint32_t* avg_ticks, uint32_t* max_ticks)
{
    if (revs == 0u || revs > 50u) {
        return MAC_ERR_BAD_ARGS;
    }
    *min_ticks = UINT32_MAX;
    *avg_ticks = *max_ticks = 0;
    bool prev = read_reg(io, MAC_REG_TACH);
    uint32_t t = io->ticks(io->ctx);
    for (;;) {                              /* first rising edge */
        const bool v = io->rd(io->ctx);
        if (v && !prev) {
            break;
        }
        prev = v;
        if (elapsed(io, t, MAC_TACH_REV_TIMEOUT_US)) {
            io->lines(io->ctx, rest_reg(d));
            *min_ticks = 0;
            return MAC_ERR_NO_TACH;
        }
    }
    const uint32_t start = io->ticks(io->ctx);
    uint32_t last = start;
    unsigned edges = 0, done = 0;
    prev = true;
    while (done < revs) {
        const bool v = io->rd(io->ctx);
        if (v && !prev && ++edges == MAC_TACH_PER_REV) {
            const uint32_t now = io->ticks(io->ctx);
            const uint32_t p = now - last;
            if (p < *min_ticks) *min_ticks = p;
            if (p > *max_ticks) *max_ticks = p;
            last = now;
            edges = 0;
            done++;
        }
        prev = v;
        if (elapsed(io, last, MAC_TACH_REV_TIMEOUT_US)) {
            io->lines(io->ctx, rest_reg(d));
            if (!done) {
                *min_ticks = 0;
            }
            return MAC_ERR_TIMEOUT;
        }
    }
    io->lines(io->ctx, rest_reg(d));
    *avg_ticks = (last - start) / revs;
    d->tach_rpm_x100 = mac_rpm_x100(*avg_ticks, io->ticks_per_us);
    return MAC_OK;
}

/* ---- speed zones ------------------------------------------------------------------------- */

uint8_t mac_zone(uint8_t track)
{
    return track >= 64u ? 4u : (uint8_t)(track / 16u);
}

uint16_t mac_zone_rpm_x100(uint8_t zone)
{
    static const uint16_t rpm[MAC_ZONES] = {39400u, 42900u, 47200u, 52500u, 59000u};
    return zone < MAC_ZONES ? rpm[zone] : 0u;
}

bool mac_speed_ok(uint16_t rpm_x100, uint8_t zone, uint16_t tol_permille)
{
    const uint32_t target = mac_zone_rpm_x100(zone);
    if (!target || !rpm_x100) {
        return false;
    }
    const uint32_t diff = rpm_x100 > target ? rpm_x100 - target : target - rpm_x100;
    return diff * 1000u <= target * tol_permille;
}

void mac_pwm_apply(const mac_io_t* io, mac_drive_t* d, uint16_t permille)
{
    if (!io->pwm) {
        return;
    }
    if (permille > 1000u) {
        permille = 1000u;
    }
    io->pwm(io->ctx, permille);
    d->pwm_permille = permille;
}

static int pwm_measure(const mac_io_t* io, mac_drive_t* d, uint16_t permille, uint16_t* rpm_x100)
{
    mac_pwm_apply(io, d, permille);
    io->wait_us(io->ctx, MAC_PWM_SETTLE_US);
    uint32_t mn, avg, mx;
    const int ret = mac_tach_revs(io, d, 2, &mn, &avg, &mx);
    *rpm_x100 = ret ? 0u : mac_rpm_x100(avg, io->ticks_per_us);
    return ret;
}

static uint32_t rpm_error(uint16_t rpm_x100, uint16_t target)
{
    return rpm_x100 > target ? (uint32_t)(rpm_x100 - target) : (uint32_t)(target - rpm_x100);
}

/* 400K: find the duty that turns the spindle at the zone speed.  Both ends of the range are
 * measured first: that gives the direction of the duty-to-speed relation and shows whether
 * the zone speed is reachable at all; then bisection.  The best duty found stays applied;
 * it is stored for the zone only when it is within MAC_PWM_TOL_PERMILLE. */
int mac_pwm_tune(const mac_io_t* io, mac_drive_t* d, uint8_t zone)
{
    if (!io->pwm || zone >= MAC_ZONES) {
        return MAC_ERR_BAD_ARGS;
    }
    const uint16_t target = mac_zone_rpm_x100(zone);
    uint16_t lo = MAC_PWM_MIN, hi = MAC_PWM_MAX, r_lo = 0, r_hi = 0;
    int ret = pwm_measure(io, d, lo, &r_lo);
    if (!ret) {
        ret = pwm_measure(io, d, hi, &r_hi);
    }
    if (ret) {
        mac_pwm_apply(io, d, 0);
        return ret;
    }
    uint16_t best = rpm_error(r_lo, target) <= rpm_error(r_hi, target) ? lo : hi;
    uint16_t best_rpm = best == lo ? r_lo : r_hi;
    const bool rising = r_hi > r_lo;
    const uint16_t slow = rising ? r_lo : r_hi, fast = rising ? r_hi : r_lo;
    if (r_lo == r_hi || target < slow || target > fast) {
        mac_pwm_apply(io, d, best);         /* no effect or out of reach: keep the closest end */
        return MAC_ERR_SPEED;
    }
    for (unsigned k = 0; k < MAC_PWM_STEPS; k++) {
        const uint16_t mid = (uint16_t)((lo + hi) / 2u);
        uint16_t r = 0;
        ret = pwm_measure(io, d, mid, &r);
        if (ret) {
            mac_pwm_apply(io, d, best);
            return ret;
        }
        if (rpm_error(r, target) < rpm_error(best_rpm, target)) {
            best = mid;
            best_rpm = r;
        }
        if ((r < target) == rising) {
            lo = mid;
        } else {
            hi = mid;
        }
    }
    mac_pwm_apply(io, d, best);
    d->tach_rpm_x100 = best_rpm;
    if (!mac_speed_ok(best_rpm, zone, MAC_PWM_TOL_PERMILLE)) {
        return MAC_ERR_SPEED;
    }
    d->zone_pwm[zone] = best;
    return MAC_OK;
}

/* After a seek: the tuned duty of the new zone (400K with the PWM running only). */
void mac_pwm_zone(const mac_io_t* io, mac_drive_t* d)
{
    if (d->kind != MAC_KIND_400K || !d->pwm_permille || d->track < 0) {
        return;
    }
    const uint16_t duty = d->zone_pwm[mac_zone((uint8_t)d->track)];
    if (duty && duty != d->pwm_permille) {
        mac_pwm_apply(io, d, duty);
        io->wait_us(io->ctx, MAC_PWM_SETTLE_US);
    }
}
