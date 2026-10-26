/* Macintosh 3.5" Sony register interface (ufi_mac.c) against a simulated drive.
 * The model follows the register map of ufi_mac.h: status bits on RD for the addressed
 * register, commands latched on the LSTRB rising edge, the step busy for 12 ms, the
 * tachometer at 60 pulses per revolution, eject only with a long strobe.  Time advances
 * with wait_us() and by one microsecond on every ticks() call. */
#include "ufi_mac.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

typedef struct {
    uint64_t now;               /* microseconds */
    uint32_t tick_base;         /* added to ticks(): wrap-around test */
    uint8_t reg;
    bool strobe;
    uint64_t strobe_at;
    uint8_t latched;
    /* drive */
    bool present, rails, unstable, flip;
    uint8_t kind;
    int track;
    bool dir_out;
    bool motor, disk, wprot;
    uint64_t step_busy_until;
    bool step_hangs, tk0_stuck, tk0_dead;
    unsigned rpm;               /* spindle speed with motor and disk */
    /* counters */
    unsigned steps, dir_cmds, strobes;
} sim_t;

static sim_t sim;

static void s_lines(void* c, uint8_t reg) { (void)c; assert(reg < 16u); sim.reg = reg; }

static void s_strobe(void* c, bool high)
{
    (void)c;
    if (high && !sim.strobe) {
        sim.strobes++;
        sim.strobe_at = sim.now;
        sim.latched = sim.reg;
        const uint8_t addr = sim.reg & 7u;
        const bool ca2 = (sim.reg & 8u) != 0;
        if (sim.present && sim.rails) {
            if (addr == 0u) { sim.dir_out = ca2; sim.dir_cmds++; }
            else if (addr == 2u && !ca2) {
                sim.steps++;
                sim.step_busy_until = sim.step_hangs ? UINT64_MAX : sim.now + 12000u;
                if (sim.dir_out) { if (sim.track > 0) sim.track--; }
                else if (sim.track < 79) sim.track++;
            } else if (addr == 4u) sim.motor = !ca2 && sim.disk;
        }
    } else if (!high && sim.strobe) {
        if (sim.latched == MAC_CMD_EJECT && sim.now - sim.strobe_at >= 500000u && sim.present) {
            sim.disk = false;   /* model: eject needs a long strobe */
            sim.motor = false;
        }
    }
    sim.strobe = high;
}

static bool status_bit(uint8_t r)
{
    switch (r) {
        case MAC_REG_DIRTN: return sim.dir_out;
        case MAC_REG_CSTIN: return !sim.disk;
        case MAC_REG_STEP: return sim.now >= sim.step_busy_until;
        case MAC_REG_WRTPRT: return !sim.wprot;
        case MAC_REG_MOTORON: return !sim.motor;
        case MAC_REG_TK0: return sim.tk0_dead ? true : sim.tk0_stuck ? false : sim.track != 0;
        case MAC_REG_TACH: {
            if (!sim.motor || !sim.rpm) return false;
            const uint64_t period = 1000000ull / sim.rpm;  /* rpm pulses per second at 60 per rev */
            return (sim.now % period) < period / 2u;
        }
        case MAC_REG_RDDATA0: case MAC_REG_RDDATA1: return (sim.now / 3u) & 1u;
        case MAC_REG_SUPERDR: return sim.kind == MAC_KIND_FDHD;
        case MAC_REG_SIDES: return sim.kind != MAC_KIND_400K;
        case MAC_REG_READY: return !(sim.motor && sim.disk);
        case MAC_REG_DRVIN: return false;
        case MAC_REG_HD: return true;
        default: return false;
    }
}

static bool s_rd(void* c)
{
    (void)c;
    if (!sim.rails) return false;               /* U17 input pulled to a dead rail */
    if (!sim.present) return true;              /* 10k pull-up to FDD_5V */
    if (sim.unstable) { sim.flip = !sim.flip; return sim.flip; }
    return status_bit(sim.reg);
}

static void s_wait(void* c, uint32_t us) { (void)c; sim.now += us; }
static uint32_t s_ticks(void* c) { (void)c; sim.now++; return sim.tick_base + (uint32_t)(sim.now * 3u); }

static const mac_io_t io = {s_lines, s_strobe, s_rd, s_wait, s_ticks, 3u, NULL};

static void fresh(uint8_t kind)
{
    memset(&sim, 0, sizeof sim);
    sim.present = sim.rails = true;
    sim.kind = kind;
    sim.disk = true;
    sim.track = 37;
    sim.rpm = 394;
}

static void rests_on_data(const mac_drive_t* d) { assert(sim.reg == (MAC_REG_RDDATA0 | d->side)); }

int main(void)
{
    mac_drive_t d;

    /* classification: what answers on RD */
    static const uint8_t kinds[] = {MAC_KIND_400K, MAC_KIND_800K, MAC_KIND_FDHD};
    for (unsigned i = 0; i < 3u; i++) {
        fresh(kinds[i]); mac_reset(&d);
        assert(mac_probe(&io, &d) == kinds[i]);
        rests_on_data(&d);
    }
    fresh(MAC_KIND_800K); sim.present = false; mac_reset(&d);
    assert(mac_probe(&io, &d) == MAC_KIND_NONE);            /* RD pulled high: all ones */
    fresh(MAC_KIND_800K); sim.rails = false; mac_reset(&d);
    assert(mac_probe(&io, &d) == MAC_KIND_NONE);            /* rails off: all zeros */
    fresh(MAC_KIND_800K); sim.unstable = true; mac_reset(&d);
    assert(mac_probe(&io, &d) == MAC_KIND_NONE);            /* floating / noisy line */
    assert(mac_classify(0x0000u, 0x0000u) == MAC_KIND_NONE);
    assert(mac_classify(0xFFFFu, 0xFFFFu) == MAC_KIND_NONE);
    assert(mac_classify(0x5002u, 0x5002u) == MAC_KIND_NONE);  /* DRVIN high: not installed */
    assert(mac_classify(0x1002u, 0x1382u) == MAC_KIND_800K);  /* tach/data bits may change */
    assert(mac_classify(0x1002u, 0x1003u) == MAC_KIND_NONE);  /* DIRTN must not */

    /* status flags: active-low bits */
    fresh(MAC_KIND_800K); sim.wprot = true; mac_reset(&d);
    (void)mac_sense_all(&io, &d);
    uint8_t f = mac_flags(d.sense);
    assert((f & MAC_F_DISK) && (f & MAC_F_WPROT) && !(f & MAC_F_MOTOR) && !(f & MAC_F_TRACK0) && !(f & MAC_F_STEPPING));
    sim.disk = false; sim.wprot = false; sim.track = 0;
    (void)mac_sense_all(&io, &d);
    f = mac_flags(d.sense);
    assert(!(f & MAC_F_DISK) && !(f & MAC_F_WPROT) && (f & MAC_F_TRACK0));

    /* recalibrate from track 37: outwards to TK0, one in, back */
    fresh(MAC_KIND_800K); mac_reset(&d);
    assert(mac_recalibrate(&io, &d) == MAC_OK && d.track == 0 && sim.track == 0);
    assert(sim.steps == 37u + 2u && sim.dir_cmds == 3u);       /* out, in, out */
    rests_on_data(&d);

    /* seek: direction latched only on a change, positions agree with the model */
    sim.steps = sim.dir_cmds = 0;
    assert(mac_seek(&io, &d, 79) == MAC_OK && d.track == 79 && sim.track == 79);
    assert(sim.steps == 79u && sim.dir_cmds == 1u);
    assert(mac_step(&io, &d, 1) == MAC_ERR_SEEK && sim.track == 79 && d.track == 79);  /* end refused, no strobe */
    assert(sim.steps == 79u);
    assert(mac_seek(&io, &d, 40) == MAC_OK && sim.track == 40 && sim.dir_cmds == 2u);
    assert(mac_seek(&io, &d, 0) == MAC_OK && sim.track == 0 && d.track == 0);
    assert(mac_seek(&io, &d, 80) == MAC_ERR_BAD_ARGS);
    mac_head(&io, &d, 1); rests_on_data(&d);
    assert(mac_seek(&io, &d, 12) == MAC_OK && sim.track == 12);
    rests_on_data(&d);

    /* unknown position: the seek recalibrates first */
    fresh(MAC_KIND_FDHD); mac_reset(&d); sim.track = 5;
    assert(mac_seek(&io, &d, 3) == MAC_OK && sim.track == 3 && d.track == 3);

    /* faults: step that never finishes, TK0 stuck asserted, TK0 never asserted */
    fresh(MAC_KIND_800K); mac_reset(&d); d.track = 10; sim.track = 10; sim.step_hangs = true;
    const uint64_t t0 = sim.now;
    assert(mac_step(&io, &d, -1) == MAC_ERR_SEEK && d.track == -1);
    assert(sim.now - t0 >= MAC_STEP_TIMEOUT_US && sim.now - t0 < 2u * MAC_STEP_TIMEOUT_US);
    rests_on_data(&d);
    fresh(MAC_KIND_800K); mac_reset(&d); sim.tk0_stuck = true;
    assert(mac_recalibrate(&io, &d) == MAC_ERR_SEEK && d.track == -1);
    fresh(MAC_KIND_800K); mac_reset(&d); sim.tk0_dead = true;
    assert(mac_recalibrate(&io, &d) == MAC_ERR_SEEK && sim.steps == MAC_RECAL_STEPS);
    fresh(MAC_KIND_800K); mac_reset(&d); d.track = 4; sim.track = 9;   /* lost steps: TK0 disagrees */
    assert(mac_seek(&io, &d, 0) == MAC_ERR_SEEK && d.track == -1);

    /* motor and eject */
    fresh(MAC_KIND_800K); mac_reset(&d);
    mac_motor(&io, &d, true);
    assert(sim.motor && d.motor);
    (void)mac_sense_all(&io, &d);
    assert(mac_flags(d.sense) & MAC_F_MOTOR);
    mac_motor(&io, &d, false);
    assert(!sim.motor && !d.motor);
    mac_command(&io, &d, MAC_CMD_EJECT, MAC_STROBE_US);       /* raw short strobe: model keeps the disk */
    assert(sim.disk);
    d.track = 3;
    mac_eject(&io, &d);
    assert(!sim.disk && d.track == -1 && !d.motor);
    rests_on_data(&d);

    /* tachometer: 60 pulses per revolution, speed from the revolution time */
    fresh(MAC_KIND_800K); mac_reset(&d);
    uint32_t mn, avg, mx;
    assert(mac_tach_revs(&io, &d, 3, &mn, &avg, &mx) == MAC_ERR_NO_TACH);   /* motor off */
    rests_on_data(&d);
    mac_motor(&io, &d, true);
    static const unsigned speeds[] = {394, 429, 472, 525, 590};
    for (unsigned i = 0; i < 5u; i++) {
        sim.rpm = speeds[i];
        sim.tick_base = 0xFFFFFFFFu - 50000u * i;             /* the counter wraps during the run */
        assert(mac_tach_revs(&io, &d, 3, &mn, &avg, &mx) == MAC_OK);
        const unsigned rpm100 = mac_rpm_x100(avg, io.ticks_per_us);
        assert(rpm100 + 100u * speeds[i] / 200u >= 100u * speeds[i] && rpm100 <= 100u * speeds[i] + 100u * speeds[i] / 200u);
        assert(d.tach_rpm_x100 == rpm100 && mn <= avg && avg <= mx);
        rests_on_data(&d);
    }
    assert(mac_tach_revs(&io, &d, 0, &mn, &avg, &mx) == MAC_ERR_BAD_ARGS);
    assert(mac_rpm_x100(0, 3) == 0 && mac_rpm_x100(1, 3) == 0xFFFFu);

    puts("mac drive: classification, status bits, recalibrate/seek/step faults, motor, eject, tachometer passed");
    return 0;
}
