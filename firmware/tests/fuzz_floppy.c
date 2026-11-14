/* LibFuzzer harness for the USB floppy mode (ufi_floppy.c compiled as a whole with the real
 * MFM codec; the drive is a model in this file, the rest generated stubs, see fuzz_stubs.py).
 *
 * The two untrusted inputs of that module are the disk in the drive (format detection from the
 * IDs of track 0 and the BIOS parameter block of the boot sector) and the block addresses of the
 * USB host.  The model disk has one of the formats of the table below; every capture the
 * firmware asks for is encoded from the model with the production encoder, every write is
 * decoded back into the model with the production decoder.
 *
 * Input: byte 0 = format, byte 1 = flags (bit0: Commodore 1581 head IDs), bytes 2..9 = the BPB
 * fields bytes/sector, total sectors, sectors/track, heads (u16 LE each); the operations come
 * from the oracle tail of the same input (see the switch in LLVMFuzzerTestOneInput).
 *
 * Checks: ASan/UBSan, no hang, the firmware never asks for a track outside 84 x 2; without an
 * injected fault on track 0 the reported size follows the documented rule; a read that reports
 * success returns exactly the data last written to that block (or the disk's); after a
 * successful ufi_floppy_end() every accepted write is on the disk. */
#include "ufi_firmware.h"
#include "ufi_mfm.h"
#include "fuzz_oracle.h"
#include <assert.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* API of ufi_floppy.c (declared in ufi_firmware.h as far as other modules use it) */
void ufi_floppy_begin(void);
int ufi_floppy_end(void);
int ufi_floppy_ready(void);
uint32_t ufi_floppy_blocks(void);
bool ufi_floppy_write_protected(void);
int ufi_floppy_read(uint8_t* buf, uint32_t lba, uint32_t count);
int ufi_floppy_write(const uint8_t* buf, uint32_t lba, uint32_t count);
int ufi_floppy_flush(void);
void ufi_floppy_service(void);

static const uint8_t* tail;
static size_t tail_n;
static uint32_t prng;
static uint8_t zero_buf[8192];

/* The generated stubs (drive select, LED, ...) must not eat the script: they get a generator
 * of their own, the script reads the input from its end. */
uint8_t fuzz_byte(void)
{
    prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5;
    return (uint8_t)prng;
}
static uint8_t script(void) { return tail_n ? tail[--tail_n] : 0u; }
/* block address: two script bytes (most disks: below 3328), or a wide value / the last blocks */
static uint32_t script_lba(uint8_t op, uint32_t size)
{
    const uint32_t hi = script();
    const uint32_t lo = script();
    if (!(op & 0x80u)) return hi * 13u + lo;
    switch (hi >> 6) {
        case 0: return 0xFFFFFFFFu - lo;
        case 1: return (hi & 63u) << 8 | lo;
        case 2: return (uint32_t)lo << (hi & 31u);
        default: return size + (lo & 7u) - 4u;      /* around the end of the disk */
    }
}
bool fuzz_bit(void) { return (fuzz_byte() & 1u) != 0; }
uint16_t fuzz_u16(void) { return (uint16_t)fuzz_u32(); }
uint32_t fuzz_u32(void)
{
    const uint8_t b = fuzz_byte();
    switch (b >> 6) {
        case 0: return b & 63u;
        case 1: return 0xFFFFFFFFu - (b & 63u);
        case 2: return (uint32_t)(b & 63u) << 8;
        default: return 1u << (b & 31u);
    }
}
int fuzz_err(void) { return 0; }
void* fuzz_zero_buffer(void) { return zero_buf; }
const char* fuzz_string(void) { return ""; }

/* ---- the disk ------------------------------------------------------------------------ */
enum { CYLS = 84, HEADS = 2, SPT_MAX = 36, SEC = 512, MAX_DELTAS = 260000 };

static const struct {
    uint32_t rate, rev_ticks;
    uint8_t spt, gap3;
    bool amiga;
} FORMATS[] = {
    {500000u, 55000000u, 18, 0x6C, false},      /* 1.44M */
    {500000u, 45833333u, 15, 0x54, false},      /* 1.2M, 360 rpm */
    {500000u, 55000000u, 21, 0x0C, false},      /* DMF */
    {250000u, 55000000u, 9, 0x50, false},       /* 720K */
    {300000u, 45833333u, 9, 0x50, false},       /* 360K in a 1.2M drive */
    {250000u, 55000000u, 10, 0x28, false},      /* Atari ST 10 sectors / 1581 */
    {250000u, 66000000u, 11, 0x02, false},      /* Atari ST 11 sectors (read only); the production encoder
                                                   needs a longer revolution than a real disk to fit them */
    {1000000u, 55000000u, 36, 0x53, false},     /* ED 2.88M (read only) */
    {250000u, 55000000u, 11, 0, true},          /* Amiga DD */
    {500000u, 55000000u, 22, 0, true},          /* Amiga HD */
    {250000u, 55000000u, 5, 0x50, false},       /* not a supported layout */
    {500000u, 55000000u, 12, 0x50, false},      /* not a supported layout */
};
enum { FORMAT_COUNT = sizeof FORMATS / sizeof FORMATS[0] };

static uint8_t pristine[CYLS * HEADS][SPT_MAX][SEC];
static uint8_t model[CYLS * HEADS][SPT_MAX][SEC];       /* what is on the disk */
static uint8_t expect[CYLS * HEADS][SPT_MAX][SEC];      /* the disk plus accepted, not yet flushed writes */
static bool pristine_ready;
static unsigned fi;                         /* format index */
static bool swap_ids;                       /* 1581: ID side field = 1 - physical head */
static bool uncertain;                      /* track 0 was captured with a fault: geometry may differ */
static bool faulted;                        /* a fault or a disk change was injected in this input */
/* Boot sector as it was when the firmware last had to detect the format (mode entry, disk change).
 * A host that rewrites block 0 does not change the geometry in use; the firmware reads it again
 * at the next detection. */
static uint8_t boot[SEC];
static bool disk_changed, write_protect, capture_error, write_error;
static uint32_t cut;                        /* fault: only cut/256 of every captured track arrives (0 = all) */

static uint32_t deltas[MAX_DELTAS], wstore[MAX_DELTAS];
static flux_sample_t samples[MAX_DELTAS];
static flux_revolution_t revs[2];
static capture_state_t cap_state;
static uint32_t ticks;
static bool motor;
static drive_timing_t timing;
static uint32_t serial;
static bool trace;                          /* FUZZ_TRACE=1: one line per operation */
#define TRACE(...) do { if (trace) fprintf(stderr, __VA_ARGS__); } while (0)

capture_context_t g_capture;

static uint32_t track_cells(void)
{
    return (uint32_t)(((uint64_t)FORMATS[fi].rev_ticks * 2u * FORMATS[fi].rate / MFM_TICK_HZ) * 98u / 100u);
}

static uint32_t encode(uint8_t cyl, uint8_t head, const uint8_t* sectors, uint32_t* out)
{
    if (FORMATS[fi].amiga)
        return mfm_encode_amiga_track(FORMATS[fi].rate, track_cells(), cyl, head, FORMATS[fi].spt, sectors, out, MAX_DELTAS);
    const mfm_track_fmt_t f = {.rate_bps = FORMATS[fi].rate, .track_cells = track_cells(), .spt = FORMATS[fi].spt,
                               .gap3 = FORMATS[fi].gap3, .head_id_swap = swap_ids};
    return mfm_encode_track(&f, cyl, head, sectors, out, MAX_DELTAS);
}

/* ---- drive doubles ------------------------------------------------------------------- */
uint32_t HAL_GetTick(void) { return ticks += 700u; }

int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks)
{
    (void)revolutions; (void)period_ticks;
    assert(track < CYLS && side < HEADS && revolutions >= 1u && period_ticks == 0u);
    uint32_t n = encode(track, side, model[track * HEADS + side][0], deltas);
    if (cut) {
        n = (uint32_t)((uint64_t)n * cut / 256u);
        if (track == 0u && side == 0u) uncertain = true;
    }
    uint32_t t = 0;
    for (uint32_t i = 0; i < n; i++) samples[i].timestamp = (t += deltas[i]);
    for (int k = 0; k < 2; k++) {
        revs[k].samples = samples;
        revs[k].count = k ? 0u : n;         /* the data arrives once; the second revolution is the empty rest */
        revs[k].index_time = t > FORMATS[fi].rev_ticks ? t : FORMATS[fi].rev_ticks;
        revs[k].revolution = (uint8_t)k;
    }
    if (capture_error && track == 0u && side == 0u) uncertain = true;
    cap_state = capture_error ? CAPTURE_ERROR : CAPTURE_COMPLETE;
    return UFI_OK;
}
capture_state_t ufi_capture_get_state(void) { return cap_state; }
uint8_t ufi_flux_get_revolution_count(void) { return 2; }
flux_revolution_t* ufi_flux_get_revolution(uint8_t index) { assert(index < 2u); return &revs[index]; }
uint32_t* ufi_flux_store(uint32_t* words) { *words = MAX_DELTAS; return wstore; }

typedef struct { uint8_t cyl, head; } wr_ctx_t;
static void on_written(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    const wr_ctx_t* c = p;
    if (crc_ok && id->n == 2u && id->r >= 1u && id->r <= FORMATS[fi].spt && id->c == c->cyl)
        memcpy(model[c->cyl * HEADS + c->head][id->r - 1u], data, SEC);
}

int ufi_write_local(uint8_t track, uint8_t side, uint32_t flux_count, bool clamp)
{
    static mfm_decoder_t d;
    (void)clamp;
    assert(track < CYLS && side < HEADS && flux_count >= 1u && flux_count <= MAX_DELTAS && !clamp);
    assert(!write_protect);                 /* the firmware asks the drive before it writes */
    if (write_error) return UFI_ERR_DMA;
    wr_ctx_t c = {track, side};
    if (FORMATS[fi].amiga) mfm_dec_init_amiga(&d, FORMATS[fi].rate, on_written, &c);
    else mfm_dec_init(&d, FORMATS[fi].rate, on_written, &c);
    for (uint32_t i = 0; i < flux_count; i++) mfm_dec_interval(&d, wstore[i]);
    return UFI_OK;
}

bool ufi_drive_motor_is_on(void) { return motor; }
int ufi_drive_motor(bool on) { motor = on; return UFI_OK; }
drive_timing_t ufi_drive_get_timing(void) { return timing; }
void ufi_drive_set_timing(const drive_timing_t* t) { timing = *t; }
bool ufi_drive_is_shugart_bus(void) { return false; }
bool ufi_drive_ready(void) { return true; }
bool ufi_drive_write_protected(void) { return write_protect; }
bool ufi_board_write_locked(void) { return false; }
bool ufi_drive_disk_changed(void)
{
    if (!disk_changed) return false;
    memcpy(expect, model, sizeof expect);   /* the firmware drops its cache now, flushed or not */
    memcpy(boot, model[0][0], SEC);
    uncertain = false;
    return true;
}
int ufi_drive_check_disk(bool* changed, bool* present)
{
    *changed = disk_changed;
    *present = true;
    disk_changed = false;
    return UFI_OK;
}

/* ---- the documented geometry rule (docs/USB_Protokoll.md, USB floppy) ------------------ */
static uint16_t le16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

static bool geometry(uint32_t* cyls, uint32_t* heads)
{
    const uint32_t rate = FORMATS[fi].rate, spt = FORMATS[fi].spt;
    *cyls = 80; *heads = 2;
    if (FORMATS[fi].amiga) return spt == (rate == 500000u ? 22u : 11u);
    if (!((rate == 1000000u && spt == 36u) || (rate == 500000u && (spt == 21u || spt == 18u || spt == 15u)) ||
          (rate != 500000u && spt == 9u) || (rate == 250000u && (spt == 10u || spt == 11u))))
        return false;
    if (rate == 300000u && spt == 9u) *cyls = 40;
    if (!swap_ids) {
        const uint8_t* b = boot;
        const uint32_t bps = le16(&b[11]), total = le16(&b[19]), bspt = le16(&b[24]), bheads = le16(&b[26]);
        if (bps == SEC && bspt == spt && (bheads == 1u || bheads == 2u) && total && total % (bspt * bheads) == 0u) {
            const uint32_t c = total / (bspt * bheads);
            if (c >= 35u && c <= 83u) { *cyls = c; *heads = bheads; }
        }
    }
    return true;
}

static uint8_t* block(uint8_t image[][SPT_MAX][SEC], uint32_t lba)
{
    uint32_t cyls, heads;
    const bool ok = geometry(&cyls, &heads);
    const uint32_t spt = FORMATS[fi].spt;
    (void)ok;
    assert(ok && lba < cyls * heads * spt);
    return image[(lba / (spt * heads)) * HEADS + (lba / spt) % heads][lba % spt];
}

static void begin(void)
{
    ufi_floppy_begin();                     /* drops the cache */
    memcpy(expect, model, sizeof expect);
    memcpy(boot, model[0][0], SEC);
    uncertain = false;
}

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    if (n < 12) return 0;
    if (!pristine_ready) {
        for (unsigned t = 0; t < CYLS * HEADS; t++)
            for (unsigned s = 0; s < SPT_MAX; s++)
                for (unsigned i = 0; i < SEC; i++) pristine[t][s][i] = (uint8_t)(t * 7u + s * 13u + i + (i >> 8));
        pristine_ready = true;
    }
    trace = getenv("FUZZ_TRACE") != NULL;
    tail = d + 10; tail_n = n - 10; prng = 0x2545F491u ^ (uint32_t)n;
    fi = d[0] % FORMAT_COUNT;
    TRACE("format %u, %u bytes\n", fi, (unsigned)n);
    swap_ids = (d[1] & 1u) != 0u && !FORMATS[fi].amiga;
    memcpy(model, pristine, sizeof model);
    memcpy(&model[0][0][11], &d[2], 2);     /* bytes per sector */
    memcpy(&model[0][0][19], &d[4], 2);     /* total sectors */
    memcpy(&model[0][0][24], &d[6], 2);     /* sectors per track */
    memcpy(&model[0][0][26], &d[8], 2);     /* heads */
    ticks = 0; serial = 0; cut = 0; motor = false;
    disk_changed = write_protect = capture_error = write_error = faulted = false;
    memset(&timing, 0, sizeof timing);
    begin();

    for (unsigned step = 0; step < 24u && tail_n; step++) {
        const uint8_t op = script();
        uint32_t cyls, heads;
        const bool supported = geometry(&cyls, &heads);
        const uint32_t size = supported ? cyls * heads * FORMATS[fi].spt : 0u;
        switch (op & 7u) {
            case 0: {
                const int ret = ufi_floppy_ready();
                TRACE("ready %d blocks %u (model %u)%s\n", ret, (unsigned)ufi_floppy_blocks(), (unsigned)size, uncertain ? " uncertain" : "");
                if (ret != UFI_OK) assert(ufi_floppy_blocks() == 0u);
                else if (!uncertain) assert(supported && ufi_floppy_blocks() == size);
                if (!faulted) assert((ret == UFI_OK) == supported);    /* an undisturbed disk is recognised or refused by the rule */
                break;
            }
            case 1: {
                const uint32_t count = 1u + ((op >> 3) & 3u);
                const uint32_t lba = script_lba(op, size);
                uint8_t* buf = malloc(count * SEC);
                assert(buf);
                const int ret = ufi_floppy_read(buf, lba, count);
                TRACE("read %u+%u -> %d\n", (unsigned)lba, (unsigned)count, ret);
                if (ret == UFI_OK && !uncertain) {
                    assert(supported && lba < size && count <= size - lba);
                    for (uint32_t i = 0; i < count; i++) assert(!memcmp(buf + i * SEC, block(expect, lba + i), SEC));
                }
                free(buf);
                break;
            }
            case 2: {
                const uint32_t count = 1u + ((op >> 3) & 3u);
                const uint32_t lba = script_lba(op, size);
                uint8_t* buf = malloc(count * SEC);
                assert(buf);
                for (uint32_t i = 0; i < count * SEC; i++) buf[i] = (uint8_t)(serial * 31u + i * 5u + 1u);
                serial++;
                const int ret = ufi_floppy_write(buf, lba, count);
                TRACE("write %u+%u -> %d\n", (unsigned)lba, (unsigned)count, ret);
                if (ret == UFI_OK && !uncertain) {
                    assert(supported && lba < size && count <= size - lba && !write_protect);
                    for (uint32_t i = 0; i < count; i++) memcpy(block(expect, lba + i), buf + i * SEC, SEC);
                } else if (ret != UFI_OK && ret != UFI_ERR_WRITE_PROT && count > 1u) {
                    uncertain = true;       /* the blocks before the failing one were taken: not visible from outside */
                }
                free(buf);
                break;
            }
            case 3: { const int ret = ufi_floppy_flush(); TRACE("flush %d\n", ret); (void)ret; break; }
            case 4: ticks += 1100u; ufi_floppy_service(); break;
            case 5:
                switch ((op >> 3) & 7u) {
                    case 0: cut = 0; capture_error = write_error = false; break;
                    case 1: cut = 1u + script(); faulted = true; break;
                    case 2: capture_error = faulted = true; break;
                    case 3: write_error = faulted = true; break;
                    case 4: write_protect = !write_protect; break;
                    default: disk_changed = faulted = true; break;
                }
                break;
            case 6:
                cut = 0; capture_error = write_error = write_protect = false;
                break;
            default: {
                const int ret = ufi_floppy_end();
                TRACE("end %d%s\n", ret, uncertain ? " uncertain" : "");
                if (ret == UFI_OK && !uncertain) assert(!memcmp(model, expect, sizeof model));
                begin();
                break;
            }
        }
    }
    cut = 0; capture_error = write_error = write_protect = false;
    if (ufi_floppy_end() == UFI_OK && !uncertain) assert(!memcmp(model, expect, sizeof model));
    return 0;
}
