/**
 * UFI Flux Engine - USB floppy mode: PC disks as a USB mass storage device
 *
 * The stand-alone drive (UFI.CFG drive=, default A) is read and written track by track:
 *  - format detection on track 0 side 0: IBM decode at 500k / 250k / 300k / 1M and Amiga
 *    decode at 250k, most good headers wins; sectors per track from the IDs, then the boot
 *    sector BPB (PC, Atari ST) for heads and cylinders.  PC 360K / 720K / 1.2M / 1.44M,
 *    DMF 1.68M, ED 2.88M (read only: the write pulse is too long for 1 Mbit), Atari ST
 *    9 / 10 sectors (up to 83 cylinders) and 11 sectors (read only), Commodore 1581 800K
 *    (ID side field swapped), Amiga 880K (block device in ADF order)
 *  - two cached tracks (one cylinder); a missing track is read with 2 revolutions,
 *    up to 3 captures until every sector has a good CRC
 *  - writes go to the cache; a dirty track is written back as a whole new IBM track
 *    (ufi_mfm.c) when it is evicted, after 1 s without writes, or before a mode change,
 *    then read back and compared (one more write on a mismatch)
 *  - DSKCHG (disk change line) drops the cache and the format; without a disk the
 *    check that steps the head runs at most every 2 s
 * Only 512-byte sectors; copy protection and other formats stay with the flux mode.
 * Called from the main loop (the USB stack is polled in this mode, see ufi_usb.c), so
 * the blocking drive accesses do not hold up any interrupt.
 */

#include "ufi_firmware.h"
#include "ufi_mfm.h"
#include <string.h>

#define MAX_SPT             36u     /* ED 2.88M */
#define SECTOR              512u
#define READ_TRIES          3u
#define FLUSH_IDLE_MS       1000u
#define EMPTY_CHECK_MS      2000u
#define DETECT_RETRY_MS     5000u
#define CAPTURE_TIMEOUT_MS  2000u
#define FLOPPY_DRIVE        ((drive_type_t)ufi_standalone_drive())   /* UFI.CFG drive= */

extern capture_context_t g_capture;

#define BIT(s)              (1ull << (s))

typedef struct {
    uint32_t rate;              /* bits per second */
    uint32_t rev_ticks;         /* measured revolution */
    uint8_t spt, heads, cyls, gap3;
    bool dstep;                 /* 40-track disk in an 80-track drive */
    bool amiga;                 /* Amiga trackdisk instead of IBM sectors */
    bool head_id_swap;          /* Commodore 1581 */
    bool writable;              /* false: ED (write pulse too long), Atari 11 sectors */
} fmt_t;

typedef struct {
    int16_t cyl;                /* -1 = empty */
    uint8_t head;
    bool dirty;
    uint64_t ok;                /* bit s = sector s+1 valid */
    uint32_t used;              /* LRU stamp */
    uint8_t data[MAX_SPT * SECTOR];
} slot_t;

typedef struct {
    slot_t* slot;               /* fill mode */
    const uint8_t* expect;      /* compare mode (verify) */
    uint8_t cyl, head, spt;
    bool amiga;
    uint64_t match;
    uint8_t max_r;
    uint8_t head_ids;           /* IBM: head numbers seen in ID fields */
    bool wrong_cyl;
} dec_ctx_t;

/* CPU access only: DTCM */
__attribute__((section(".dtcm"), aligned(4))) static slot_t slots[2];

static fmt_t fmt;
static bool fmt_ok, active;
static drive_timing_t saved_timing;
static uint32_t stamp, last_write_ms, last_check_ms, detect_fail_ms;
static bool detect_failed;
static bool shugart_motor_was_on;

/* ============================================================================
 * drive access
 * ============================================================================ */

static void motor_ready(void)
{
    ufi_board_activity();
    if (!ufi_drive_motor_is_on()) {
        ufi_drive_select(FLOPPY_DRIVE);
        ufi_drive_motor(true);                         /* waits for spin-up */
    }
}

static void set_double_step(bool on)
{
    drive_timing_t t = ufi_drive_get_timing();
    t.double_step = on ? 1u : 0u;
    ufi_drive_set_timing(&t);
}

/* capture revolutions of cyl/head into the flux store */
static int capture(uint8_t cyl, uint8_t head, uint8_t revs)
{
    motor_ready();
    int ret = ufi_capture_start(cyl, head, revs, 0);
    if (ret != UFI_OK) {
        return ret;
    }
    const uint32_t t0 = HAL_GetTick();
    for (;;) {
        UFI_WATCHDOG_FEED();
        const capture_state_t s = ufi_capture_get_state();
        if (s == CAPTURE_COMPLETE) {
            return UFI_OK;
        }
        if (s == CAPTURE_ERROR || HAL_GetTick() - t0 > CAPTURE_TIMEOUT_MS) {
            ufi_capture_abort();
            g_capture.state = CAPTURE_IDLE;
            return UFI_ERR_NO_INDEX;
        }
    }
}

static void on_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    dec_ctx_t* c = p;
    if (id->r > c->max_r && id->r <= MAX_SPT) {
        c->max_r = id->r;
    }
    if (!crc_ok || id->n != 2u || id->r < 1u || id->r > c->spt) {
        return;
    }
    /* IBM: the ID head field is not checked (1581 swaps it); Amiga: track = cyl/head */
    if (id->c != c->cyl || (c->amiga && id->h != c->head)) {
        c->wrong_cyl = true;
        return;
    }
    const uint32_t s = id->r - 1u;
    if (c->slot) {
        if (!(c->slot->ok & BIT(s))) {
            memcpy(&c->slot->data[s * SECTOR], data, SECTOR);
            c->slot->ok |= BIT(s);
        }
    } else if (c->expect && !memcmp(&c->expect[s * SECTOR], data, SECTOR)) {
        c->match |= BIT(s);
    }
}

/* decode all captured revolutions as one continuous flux stream */
static uint32_t decode_capture(uint32_t rate, dec_ctx_t* c)
{
    static mfm_decoder_t d;                            /* ~2 KB, keep it off the stack */
    if (c->amiga) {
        mfm_dec_init_amiga(&d, rate, on_sector, c);
    } else {
        mfm_dec_init(&d, rate, on_sector, c);
    }
    uint32_t carry = 0;
    const uint8_t n = ufi_flux_get_revolution_count();
    for (uint8_t k = 0; k < n; k++) {
        const flux_revolution_t* r = ufi_flux_get_revolution(k);
        uint32_t prev = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            const uint32_t t = r->samples[i].timestamp;
            mfm_dec_interval(&d, t - prev + carry);
            carry = 0;
            prev = t;
        }
        carry = r->index_time - prev;                  /* gap across the index pulse */
    }
    c->head_ids = d.head_ids;
    return d.ids_seen;
}

static uint64_t all_sectors(void)
{
    return (fmt.spt >= 64u) ? ~0ull : (BIT(fmt.spt) - 1u);
}

static int read_track(slot_t* s, uint8_t cyl, uint8_t head)
{
    s->cyl = cyl;
    s->head = head;
    s->dirty = false;
    s->ok = 0;
    const uint64_t all = all_sectors();
    int ret = UFI_ERR_NO_INDEX;
    for (uint32_t tries = 0; tries < READ_TRIES && s->ok != all; tries++) {
        ret = capture(cyl, head, 2);
        if (ret != UFI_OK) {
            continue;
        }
        dec_ctx_t c = {.slot = s, .cyl = cyl, .head = head, .spt = fmt.spt, .amiga = fmt.amiga};
        decode_capture(fmt.rate, &c);
        g_capture.state = CAPTURE_IDLE;
        if (c.wrong_cyl) {
            ufi_drive_recalibrate();                   /* head lost its position */
        }
    }
    led_set(&PIN_LED_FDD, false);
    return s->ok == all ? UFI_OK : (s->ok ? UFI_ERR_DMA : ret);
}

static int write_track(slot_t* s)
{
    if (!fmt.writable || ufi_drive_write_protected() || ufi_board_write_locked()) {
        return UFI_ERR_WRITE_PROT;
    }
    const uint64_t all = all_sectors();
    if (s->ok != all) {
        return UFI_ERR_DMA;                            /* never write a half-known track */
    }
    const mfm_track_fmt_t f = {
        .rate_bps = fmt.rate, .spt = fmt.spt, .gap3 = fmt.gap3, .head_id_swap = fmt.head_id_swap,
        /* 98 % of the revolution: the splice stays in gap 4b at drive speed tolerance */
        .track_cells = (uint32_t)(((uint64_t)fmt.rev_ticks * 2u * fmt.rate / MFM_TICK_HZ) * 98u / 100u),
    };
    int ret = UFI_ERR_DMA;
    for (int attempt = 0; attempt < 2; attempt++) {
        motor_ready();
        uint32_t words = 0;
        uint32_t* store = ufi_flux_store(&words);
        const uint32_t n = fmt.amiga
            ? mfm_encode_amiga_track(fmt.rate, f.track_cells, (uint8_t)s->cyl, s->head, s->data, store, words)
            : mfm_encode_track(&f, (uint8_t)s->cyl, s->head, s->data, store, words);
        if (n == 0) {
            return UFI_ERR_BUFFER_FULL;
        }
        ret = ufi_write_local((uint8_t)s->cyl, s->head, n);
        if (ret != UFI_OK) {
            continue;
        }
        ret = capture((uint8_t)s->cyl, s->head, 2);    /* verify: read back and compare */
        if (ret != UFI_OK) {
            continue;
        }
        dec_ctx_t c = {.expect = s->data, .cyl = (uint8_t)s->cyl, .head = s->head,
                       .spt = fmt.spt, .amiga = fmt.amiga};
        decode_capture(fmt.rate, &c);
        g_capture.state = CAPTURE_IDLE;
        ret = (c.match == all) ? UFI_OK : UFI_ERR_DMA;
        if (ret == UFI_OK) {
            s->dirty = false;
            break;
        }
    }
    led_set(&PIN_LED_FDD, false);
    return ret;
}

/* ============================================================================
 * format detection
 * ============================================================================ */

static uint16_t le16(const uint8_t* p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int detect(void)
{
    /* IBM at every rate (ED 1 Mbit too), Amiga at DD; most good headers wins */
    static const uint32_t rates[] = {500000u, 250000u, 300000u, 1000000u, 250000u};
    set_double_step(false);
    motor_ready();
    ufi_drive_recalibrate();
    int ret = capture(0, 0, 2);
    if (ret != UFI_OK) {
        return ret;
    }
    uint32_t best = 0, best_i = 0;
    for (uint32_t i = 0; i < 5; i++) {
        dec_ctx_t c = {.cyl = 0, .spt = MAX_SPT, .amiga = (i == 4u)};
        const uint32_t ids = decode_capture(rates[i], &c);
        if (ids > best) {
            best = ids;
            best_i = i;
        }
    }
    if (best < 4u) {
        g_capture.state = CAPTURE_IDLE;
        return UFI_ERR_NOT_IMPL;                       /* unformatted or not MFM */
    }
    /* sectors per track from the IDs, sector data into cache slot 0 */
    slot_t* s = &slots[0];
    memset(s, 0, sizeof(*s) - sizeof(s->data));
    dec_ctx_t c = {.slot = s, .cyl = 0, .head = 0, .spt = MAX_SPT, .amiga = (best_i == 4u)};
    decode_capture(rates[best_i], &c);
    fmt.rev_ticks = ufi_flux_get_revolution(0)->index_time;
    g_capture.state = CAPTURE_IDLE;
    led_set(&PIN_LED_FDD, false);

    fmt.rate = rates[best_i];
    fmt.amiga = (best_i == 4u);
    fmt.spt = c.max_r;
    fmt.heads = 2;
    fmt.cyls = 80;
    fmt.writable = true;
    /* physical head 0 carries ID side 1 only: Commodore 1581 */
    fmt.head_id_swap = !fmt.amiga && c.head_ids == 0x02u;
    if (fmt.amiga) {
        if (fmt.spt != AMIGA_SECTORS) {
            return UFI_ERR_NOT_IMPL;                   /* Amiga 880K (HD 1.76M not yet) */
        }
    } else if (fmt.rate == 1000000u && fmt.spt == 36u) {
        fmt.gap3 = 0x53;  fmt.writable = false;        /* ED 2.88M: write pulse too long */
    } else if (fmt.rate == 500000u && fmt.spt == 21u) {
        fmt.gap3 = 0x0C;                               /* DMF 1.68M */
    } else if (fmt.rate == 500000u && fmt.spt == 18u) {
        fmt.gap3 = 0x6C;                               /* 1.44M */
    } else if (fmt.rate == 500000u && fmt.spt == 15u) {
        fmt.gap3 = 0x54;                               /* 1.2M */
    } else if (fmt.rate != 500000u && fmt.spt == 9u) {
        fmt.gap3 = 0x50;  fmt.cyls = (fmt.rate == 300000u) ? 40 : 80;   /* 360K / 720K / ST */
    } else if (fmt.rate == 250000u && fmt.spt == 10u) {
        fmt.gap3 = fmt.head_id_swap ? 0x23 : 0x28;     /* 1581 800K / Atari ST 10 sectors */
    } else if (fmt.rate == 250000u && fmt.spt == 11u) {
        fmt.gap3 = 0x02;  fmt.writable = false;        /* Atari ST 11 sectors: read only */
    } else {
        return UFI_ERR_NOT_IMPL;
    }
    if (!fmt.amiga && !fmt.head_id_swap && (s->ok & 1u)) {   /* boot sector BPB (PC, ST) */
        const uint8_t* b = s->data;
        const uint16_t bps = le16(&b[11]), spt = le16(&b[24]), heads = le16(&b[26]);
        const uint16_t total = le16(&b[19]);
        if (bps == SECTOR && spt == fmt.spt && (heads == 1 || heads == 2) && total &&
            total % (spt * heads) == 0) {
            const uint32_t cyls = total / (spt * heads);
            if (cyls >= 35u && cyls <= 83u) {           /* Atari ST disks often 81-83 */
                fmt.heads = (uint8_t)heads;
                fmt.cyls = (uint8_t)cyls;
            }
        }
    }
    fmt.dstep = (fmt.rate == 300000u && fmt.cyls <= 42u);
    set_double_step(fmt.dstep);
    s->cyl = 0;
    s->head = 0;
    s->ok &= all_sectors();
    s->used = ++stamp;
    slots[1].cyl = -1;
    fmt_ok = true;
    return UFI_OK;
}

/* ============================================================================
 * cache
 * ============================================================================ */

static void drop_cache(void)
{
    slots[0].cyl = slots[1].cyl = -1;
    slots[0].dirty = slots[1].dirty = false;
}

static int flush_all(void)
{
    int ret = UFI_OK;
    for (int i = 0; i < 2; i++) {
        if (slots[i].cyl >= 0 && slots[i].dirty) {
            const int r = write_track(&slots[i]);
            if (r != UFI_OK) {
                ret = r;
            }
        }
    }
    return ret;
}

/* slot for cyl/head; load = read the track if it is not cached */
static slot_t* get_slot(uint8_t cyl, uint8_t head, int* ret)
{
    for (int i = 0; i < 2; i++) {
        if (slots[i].cyl == cyl && slots[i].head == head) {
            slots[i].used = ++stamp;
            *ret = UFI_OK;
            if (slots[i].ok != all_sectors() && !slots[i].dirty) {
                *ret = read_track(&slots[i], cyl, head);   /* earlier read was incomplete */
            }
            return &slots[i];
        }
    }
    slot_t* v = (slots[0].cyl < 0 || (slots[1].cyl >= 0 && slots[0].used < slots[1].used))
                ? &slots[0] : &slots[1];
    if (v->cyl >= 0 && v->dirty && (*ret = write_track(v)) != UFI_OK) {
        return NULL;
    }
    *ret = read_track(v, cyl, head);
    v->used = ++stamp;
    return v;                                          /* may be partial: check ok bits */
}

static bool lba_to_chs(uint32_t lba, uint8_t* c, uint8_t* h, uint8_t* s)
{
    const uint32_t per_cyl = (uint32_t)fmt.spt * fmt.heads;
    if (!fmt_ok || lba >= per_cyl * fmt.cyls) {
        return false;
    }
    *c = (uint8_t)(lba / per_cyl);
    *h = (uint8_t)((lba / fmt.spt) % fmt.heads);
    *s = (uint8_t)(lba % fmt.spt);
    return true;
}

/* ============================================================================
 * API (USB mass storage callbacks, mode changes, main loop)
 * ============================================================================ */

void ufi_floppy_begin(void)
{
    saved_timing = ufi_drive_get_timing();
    drop_cache();
    fmt_ok = false;
    detect_failed = false;
    active = true;
    ufi_drive_select(FLOPPY_DRIVE);
}

int ufi_floppy_end(void)
{
    if (!active) {
        return UFI_OK;
    }
    const int ret = flush_all();
    active = false;
    fmt_ok = false;
    drop_cache();
    ufi_drive_motor(false);
    ufi_drive_set_timing(&saved_timing);
    return ret;
}

int ufi_floppy_ready(void)
{
    if (!active) {
        return UFI_ERR_NOT_IMPL;
    }
    const uint32_t now = HAL_GetTick();
    if (!ufi_drive_motor_is_on()) {
        ufi_drive_select(FLOPPY_DRIVE);                /* DSKCHG is valid while selected */
    }
    if (ufi_drive_is_shugart_bus()) {
        /* Shugart bus: no disk change line, J6 pin 34 is READY (valid while the motor
         * runs).  A disk swapped while the motor is off goes unseen, so the cached tracks
         * are dropped when the motor stops (never write one disk's track onto another). */
        const bool on = ufi_drive_motor_is_on();
        if (shugart_motor_was_on && !on && !slots[0].dirty && !slots[1].dirty) {
            drop_cache();
        }
        shugart_motor_was_on = on;
        if (on && !ufi_drive_ready()) {
            fmt_ok = false;
            drop_cache();
            return UFI_ERR_NO_DRIVE;
        }
    } else if (ufi_drive_disk_changed()) {             /* disk out (or swapped) */
        fmt_ok = false;
        drop_cache();
        detect_failed = false;
        if (now - last_check_ms < EMPTY_CHECK_MS) {
            return UFI_ERR_NO_DRIVE;
        }
        last_check_ms = now;
        bool changed = false, present = false;
        ufi_drive_check_disk(&changed, &present);     /* steps once to clear DSKCHG */
        if (!present) {
            return UFI_ERR_NO_DRIVE;
        }
    }
    if (!fmt_ok) {
        /* Shugart bus: a failed detection spun the motor, retry less often */
        const uint32_t retry = ufi_drive_is_shugart_bus() ? 30000u : DETECT_RETRY_MS;
        if (detect_failed && now - detect_fail_ms < retry) {
            return UFI_ERR_NOT_IMPL;
        }
        if (detect() != UFI_OK) {
            detect_failed = true;
            detect_fail_ms = now;
            return UFI_ERR_NOT_IMPL;
        }
    }
    return UFI_OK;
}

uint32_t ufi_floppy_blocks(void)
{
    return fmt_ok ? (uint32_t)fmt.cyls * fmt.heads * fmt.spt : 0u;
}

bool ufi_floppy_write_protected(void)
{
    return ufi_board_write_locked() || ufi_drive_write_protected() || (fmt_ok && !fmt.writable);
}

int ufi_floppy_read(uint8_t* buf, uint32_t lba, uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        uint8_t c, h, s;
        if (!lba_to_chs(lba + i, &c, &h, &s)) {
            return UFI_ERR_SEEK_FAIL;
        }
        int ret;
        slot_t* sl = get_slot(c, h, &ret);
        if (!sl || !(sl->ok & BIT(s))) {
            return ret != UFI_OK ? ret : UFI_ERR_DMA;
        }
        memcpy(buf + i * SECTOR, &sl->data[s * SECTOR], SECTOR);
    }
    return UFI_OK;
}

int ufi_floppy_write(const uint8_t* buf, uint32_t lba, uint32_t count)
{
    if (ufi_floppy_write_protected()) {
        return UFI_ERR_WRITE_PROT;
    }
    for (uint32_t i = 0; i < count; i++) {
        uint8_t c, h, s;
        if (!lba_to_chs(lba + i, &c, &h, &s)) {
            return UFI_ERR_SEEK_FAIL;
        }
        int ret;
        slot_t* sl = get_slot(c, h, &ret);
        if (!sl) {
            return ret;
        }
        memcpy(&sl->data[s * SECTOR], buf + i * SECTOR, SECTOR);
        sl->ok |= BIT(s);
        sl->dirty = true;
    }
    last_write_ms = HAL_GetTick();
    return UFI_OK;
}

int ufi_floppy_flush(void)
{
    return active ? flush_all() : UFI_OK;
}

void ufi_floppy_service(void)
{
    if (!active || (!slots[0].dirty && !slots[1].dirty)) {
        return;
    }
    if (HAL_GetTick() - last_write_ms >= FLUSH_IDLE_MS) {
        if (flush_all() != UFI_OK) {
            led_set(&PIN_LED_ERR, true);               /* data not on the disk yet */
            last_write_ms = HAL_GetTick();             /* next attempt after another second */
        }
    }
}
