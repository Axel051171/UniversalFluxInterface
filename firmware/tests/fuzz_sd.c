/* LibFuzzer harness for the stand-alone SD module (ufi_dump.c compiled as a whole with
 * the real image/MFM/HFE/verify/quality modules; drivers are generated stubs).
 *
 * The FatFs API is implemented here on top of the fuzz input: every file opened for
 * reading returns the input bytes (zero padded to the size the image kind needs),
 * files opened for writing are bounded sinks whose source buffers ASan checks.
 * Captures are synthesised from the input as well, so the dump writers and the track
 * quality decoders run on fuzzed flux.
 *
 * Input: byte 0 = operation (0 SCP, 1 HFE, 2 ADF, 3 IMG, 4 D64, 5 D81 written from SD to
 * the disk; 6 UFI.CFG parsing and the host file API; 7 a dump to SD in a fuzzed format),
 * the rest is the file content.  The oracle (fuzz_oracle.h) takes the tail of the input.
 * Checks: ASan/UBSan, f_mkfs is never called, every job ends, no hang. */
#include "ufi_firmware.h"
#include "ufi_cancel.h"
#include "ufi_mfm.h"
#include "ufi_resources.h"
#include "ff.h"
#include "fuzz_oracle.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

capture_context_t g_capture;

/* ---- oracle ---------------------------------------------------------------- */
static const uint8_t* tail;
static size_t tail_n;
static uint32_t prng;
static uint8_t zero_buf[8192];

uint8_t fuzz_byte(void)
{
    if (tail_n) return tail[--tail_n];
    prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5;
    return (uint8_t)prng;
}
bool fuzz_bit(void) { return fuzz_byte() >= 224u; } /* 1 in 8, and zero padding means "no obstacle" */
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
int fuzz_err(void)
{
    const uint8_t b = fuzz_byte();
    return b < 22u ? -(int)b : 0;
}
void* fuzz_zero_buffer(void) { return zero_buf; }
const char* fuzz_string(void) { return ""; }

/* ---- in-memory FatFs ---------------------------------------------------------- */
static const uint8_t* img;
static uint32_t img_len, img_declared;      /* declared size may exceed the content (zero padded) */
static uint64_t written_total;
static const char* cfg_preset;              /* UFI.CFG content while the per-input reset runs */
enum { F_CLOSED, F_READ, F_WRITE, F_PRESET };

static FRESULT rare(uint8_t trigger, FRESULT fr) { return fuzz_byte() == trigger ? fr : FR_OK; }

/* FUZZ_TRACE=1 in the environment: log every file operation with the dump state (for
 * reading a crash input: fuzz_sd.exe <artifact>) */
#include <stdio.h>
#include <stdlib.h>
static int trace = -1;
static void tr(const char* op, const void* fp, const char* name)
{
    if (trace < 0) trace = getenv("FUZZ_TRACE") != NULL;
    if (trace) fprintf(stderr, "  %-8s %p %-14s state %u\n", op, fp, name ? name : "", ufi_dump_status().state);
}
static void tr_input(const uint8_t* d, size_t n)
{
    if (trace < 0) trace = getenv("FUZZ_TRACE") != NULL;
    if (!trace) return;
    fprintf(stderr, "INPUT %zu:", n);               /* hex, so a stateful sequence can be replayed */
    for (size_t i = 0; i < n; i++) fprintf(stderr, "%02x", d[i]);
    fputc('\n', stderr);
}

FRESULT f_mount(FATFS* fs, const TCHAR* path, BYTE opt)
{
    (void)fs; (void)path; (void)opt;
    return rare(0xFE, FR_NO_FILESYSTEM);
}
FRESULT f_stat(const TCHAR* path, FILINFO* fno)
{
    assert(path);
    if (!strncmp(path, "DUMP", 4)) return fuzz_byte() == 0xFDu ? FR_OK : FR_NO_FILE;
    if (fno) { memset(fno, 0, sizeof *fno); fno->fsize = img_declared; }
    return rare(0xFC, FR_NO_FILE);
}
FRESULT f_open(FIL* fp, const TCHAR* path, BYTE mode)
{
    assert(fp && path && *path);
    tr("open", fp, path);
    /* Re-opening a FIL whose close failed is what FatFs without FF_FS_LOCK allows (the
     * old handle is abandoned); the firmware ignores close errors on read-only files. */
    const FRESULT fr = rare(0xFB, FR_DENIED);
    if (fr != FR_OK) return fr;                          /* a failed open leaves the FIL closed */
    memset(fp, 0, sizeof *fp);
    fp->flag = (mode & FA_WRITE) ? F_WRITE : F_READ;
    if (fp->flag == F_READ) fp->obj.objsize = img_declared;
    if (fp->flag == F_READ && cfg_preset && !strcmp(path, "UFI.CFG")) {
        fp->flag = F_PRESET;
        fp->obj.objsize = (FSIZE_t)strlen(cfg_preset);
    }
    return FR_OK;
}
FRESULT f_close(FIL* fp)
{
    tr("close", fp, NULL);
    assert(fp && fp->flag != F_CLOSED);
    const FRESULT fr = rare(0xFA, FR_DISK_ERR);
    if (fr != FR_OK) return fr;                          /* FatFs: a failed sync leaves the file open */
    fp->flag = F_CLOSED;
    return FR_OK;
}
FRESULT f_read(FIL* fp, void* buff, UINT btr, UINT* br)
{
    tr("read", fp, NULL);
    assert(fp && fp->flag != F_CLOSED && (buff || !btr) && br);
    if (fuzz_byte() == 0xF9u) { *br = 0; return FR_DISK_ERR; }
    const uint32_t pos = fp->fptr;
    const uint32_t avail = pos < fp->obj.objsize ? fp->obj.objsize - pos : 0;
    UINT n = btr < avail ? btr : avail;
    if (fuzz_byte() == 0xF8u) n /= 2u;                   /* short read */
    uint8_t* out = buff;
    const uint8_t* src = fp->flag == F_PRESET ? (const uint8_t*)cfg_preset : img;
    const uint32_t src_len = fp->flag == F_PRESET ? (uint32_t)strlen(cfg_preset) : img_len;
    const uint32_t real = (fp->flag != F_WRITE && pos < src_len) ? (src_len - pos < n ? src_len - pos : n) : 0;
    if (real) memcpy(out, src + pos, real);
    if (n > real) memset(out + real, 0, n - real);      /* declared size beyond the content: zeros */
    fp->fptr += n;
    *br = n;
    return FR_OK;
}
FRESULT f_write(FIL* fp, const void* buff, UINT btw, UINT* bw)
{
    assert(fp && fp->flag == F_WRITE && (buff || !btw) && bw);
    if (fuzz_byte() == 0xF7u) { *bw = 0; return FR_DISK_ERR; }
    volatile uint8_t sink = 0;
    for (UINT i = 0; i < btw; i++) sink ^= ((const uint8_t*)buff)[i];   /* ASan reads the source */
    UINT n = fuzz_byte() == 0xF6u ? btw / 2u : btw;      /* disk full */
    fp->fptr += n;
    if (fp->fptr > fp->obj.objsize) fp->obj.objsize = fp->fptr;
    written_total += n;
    assert(written_total < (1ull << 31));
    *bw = n;
    return FR_OK;
}
FRESULT f_lseek(FIL* fp, FSIZE_t ofs)
{
    tr("lseek", fp, NULL);
    assert(fp && fp->flag != F_CLOSED);
    if (fp->flag != F_WRITE && ofs > fp->obj.objsize) ofs = fp->obj.objsize;
    fp->fptr = ofs;
    if (fp->flag == F_WRITE && ofs > fp->obj.objsize) fp->obj.objsize = ofs;
    return rare(0xF5, FR_DISK_ERR);
}
FRESULT f_truncate(FIL* fp) { assert(fp && fp->flag == F_WRITE); fp->obj.objsize = fp->fptr; return FR_OK; }
FRESULT f_sync(FIL* fp) { assert(fp && fp->flag != F_CLOSED); return rare(0xF4, FR_DISK_ERR); }
FRESULT f_opendir(DIR* dp, const TCHAR* path) { assert(dp && path); memset(dp, 0, sizeof *dp); return rare(0xF3, FR_NO_PATH); }
FRESULT f_closedir(DIR* dp) { assert(dp); return FR_OK; }
FRESULT f_readdir(DIR* dp, FILINFO* fno)
{
    assert(dp && fno);
    memset(fno, 0, sizeof *fno);
    if (dp->dptr < 3u) {
        memcpy(fno->fname, "DUMP0001.SCP", 13);
        fno->fname[7] = (char)('1' + dp->dptr);
        fno->fsize = fuzz_u32();
        fno->fattrib = dp->dptr == 2u ? AM_DIR : 0;
        dp->dptr++;
    }
    return rare(0xF2, FR_DISK_ERR);
}
FRESULT f_mkdir(const TCHAR* path) { assert(path); return rare(0xF1, FR_EXIST); }
FRESULT f_unlink(const TCHAR* path) { assert(path); return rare(0xF0, FR_NO_FILE); }
FRESULT f_rename(const TCHAR* a, const TCHAR* b) { assert(a && b); return rare(0xEF, FR_EXIST); }
FRESULT f_getfree(const TCHAR* path, DWORD* nclst, FATFS** fatfs) { assert(path && nclst && fatfs); *nclst = fuzz_u32(); return FR_OK; }
FRESULT f_mkfs(const TCHAR* path, const void* opt, void* work, UINT len)
{
    (void)path; (void)opt; (void)work; (void)len;
    assert(!"f_mkfs: the firmware must never format the card on its own");
    return FR_DISK_ERR;
}

/* ---- time, store and capture doubles -------------------------------------------- */
static uint32_t ticks;
static uint64_t flux_now;
static uint32_t flux_words[65536];
static flux_sample_t samples[3 * 65536];                 /* three revolutions of up to 65536 transitions */
static uint32_t deltas[65536];
static uint8_t sector_data[18 * 1024];                  /* up to 18 sectors of size code 3 */
static flux_revolution_t revs[REVOLUTIONS_BUFFER];
static uint8_t rev_count;

board_status_t ufi_board_status(void)
{
    board_status_t s;
    memset(&s, 0, sizeof s);
    s.power = fuzz_byte() & 3u;
    s.flags = fuzz_byte();                               /* buttons, write lock, trips */
    return s;
}

uint32_t HAL_GetTick(void)
{
    ticks += 1u;
    if (fuzz_byte() == 0x7Fu) ticks += 20000u;
    return ticks;
}
void HAL_Delay(uint32_t ms) { ticks += ms; }
uint64_t ufi_flux_now64(void) { return flux_now += 1u << 20; }
uint32_t* ufi_flux_store(uint32_t* words)
{
    *words = fuzz_bit() ? 64u + fuzz_u16() % 4033u : 4096u + fuzz_u16() % (65536u - 4096u + 1u);
    return flux_words;
}
/* A valid IBM-MFM track (sector bytes from the input) so the quality decoders, the
 * image writers and the double-step probe see real sectors; otherwise random intervals. */
static uint32_t synth_revolution(uint32_t pos, uint32_t limit, uint32_t* index_time)
{
    uint32_t ts = 0, count;
    if (fuzz_byte() < 128u) {
        static const uint32_t rates[] = {250000u, 300000u, 500000u, 250000u};
        const uint8_t kind = fuzz_byte();
        const mfm_track_fmt_t f = {.rate_bps = rates[kind & 3u], .track_cells = rates[kind & 3u] / 5u * 2u,
                                   .spt = (uint8_t)(4u + fuzz_byte() % 15u), .gap3 = (uint8_t)(20u + fuzz_byte() % 80u),
                                   .head_id_swap = (kind & 4u) != 0, .size_code = (uint8_t)(kind & 8u ? fuzz_byte() & 3u : 0u)};
        for (uint32_t i = 0; i < sizeof sector_data; i++) sector_data[i] = img_len ? img[i % img_len] : 0u;
        /* a 40-track disk in an 80-track drive stamps cylinder track/2 (double-step probe) */
        const uint8_t cyl = fuzz_byte() < 64u ? (uint8_t)(g_capture.current_track / 2u) : g_capture.current_track;
        count = mfm_encode_track(&f, cyl, g_capture.current_side, sector_data, deltas, limit);
        /* weak track: a few damaged spots make the quality fall below spt (retry, +-1 search) */
        for (uint8_t k = fuzz_byte() < 96u ? 1u + (fuzz_byte() & 3u) : 0u; k && count; k--) {
            deltas[fuzz_u16() % count] += 400u;
        }
        for (uint32_t i = 0; i < count; i++) {
            ts += deltas[i];
            samples[pos + i].timestamp = ts;
        }
        *index_time = ts + 2000u;
        return count;
    }
    count = fuzz_u16() % 1365u;
    if (count > limit) count = limit;
    for (uint32_t i = 0; i < count; i++) {
        ts += 250u + 4u * (img_len ? img[(pos + i) % img_len] : 0u);   /* 0.9..3.6 us per interval */
        samples[pos + i].timestamp = ts;
    }
    *index_time = ts + 500u;
    return count;
}
int ufi_capture_start(uint8_t track, uint8_t side, uint8_t revolutions, uint32_t period_ticks)
{
    (void)period_ticks;
    const int e = fuzz_err();
    if (e) return e;
    g_capture.current_track = track;
    g_capture.current_side = side;
    rev_count = revolutions ? (revolutions > 3u ? 3u : revolutions) : 1u;
    uint32_t pos = 0;
    for (uint8_t r = 0; r < rev_count; r++) {
        uint32_t index_time = 0;
        const uint32_t count = synth_revolution(pos, (uint32_t)(sizeof samples / sizeof samples[0]) / 3u, &index_time);
        revs[r].samples = &samples[pos];
        revs[r].count = count;
        revs[r].revolution = r;
        revs[r].index_time = index_time;
        pos += count;
    }
    g_capture.state = CAPTURE_COMPLETE;
    g_capture.current_track = track;
    g_capture.current_side = side;
    g_capture.revolutions_requested = revolutions;
    g_capture.revolutions_captured = rev_count;
    g_capture.buffer = revs;
    return UFI_OK;
}
capture_state_t ufi_capture_get_state(void) { return g_capture.state; }
int ufi_capture_abort(void) { g_capture.state = CAPTURE_IDLE; return UFI_OK; }
/* re-capture at the current position (search_track, recapture): same synthesis */
int ufi_flux_capture_start(uint8_t revolutions, uint32_t period_ticks)
{
    return ufi_capture_start(g_capture.current_track, g_capture.current_side, revolutions, period_ticks);
}
int ufi_flux_capture_stop(void) { g_capture.state = CAPTURE_IDLE; return UFI_OK; }
/* the drive module is a stub here; keep the timing the dump module sets (double step) */
static drive_timing_t timing = {.step_pulse_us = 10, .step_rate_us = 3000, .settle_us = 15000, .precomp_ns = 0xFFFF};
drive_timing_t ufi_drive_get_timing(void) { return timing; }
void ufi_drive_set_timing(const drive_timing_t* t) { timing = *t; }
uint8_t ufi_flux_get_revolution_count(void) { return rev_count; }
flux_revolution_t* ufi_flux_get_revolution(uint8_t k)
{
    assert(k < rev_count);
    return &revs[k];
}
int ufi_format_revolution(uint32_t* t)
{
    *t = 40000000u + ((uint32_t)fuzz_u16() << 9);        /* 145..270 ms at 275 MHz */
    return fuzz_byte() == 0xFFu ? UFI_ERR_NO_INDEX : UFI_OK;
}
int ufi_iec_read_block(uint8_t device, uint8_t track, uint8_t sector, uint8_t* buf, uint16_t* got)
{
    (void)device; (void)track; (void)sector;
    memset(buf, fuzz_byte(), 256);
    *got = fuzz_bit() ? 256u : (uint16_t)fuzz_byte();
    return fuzz_err();
}

/* ---- one input -------------------------------------------------------------- */
static void run_job(void)
{
    /* a whole 1.44 MB image is 160 tracks; most inputs stop early (abort path), one in
     * 256 runs to completion so job_finish and the file publication are reached too */
    const int limit = fuzz_byte() == 0x5Au ? 6000 : 8 + (fuzz_byte() & 63);
    for (int k = 0; k < limit && ufi_dump_active(); k++) {
        ufi_dump_service();
        if ((k & 7) == 7) ufi_buttons_service();         /* B aborts, A would start a dump */
    }
    if (ufi_dump_active()) {
        ufi_dump_abort();
        ufi_dump_service();
    }
    assert(!ufi_dump_active());
}

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    static const struct { const char* name; uint32_t declared; } kinds[] = {
        {"FUZZ.SCP", 0}, {"FUZZ.HFE", 0}, {"FUZZ.ADF", 901120u}, {"FUZZ.IMG", 1474560u},
        {"FUZZ.D64", 174848u}, {"FUZZ.D81", 819200u}};
    if (n < 2) return 0;
    tail = d; tail_n = n; prng = 0x6A09E667u ^ (uint32_t)n; ticks = 0; flux_now = 0;
    tr_input(d, n);
    tr("input", NULL, NULL);
    img = d + 1; img_len = (uint32_t)(n - 1); img_declared = img_len;
    written_total = 0; rev_count = 0;
    memset(zero_buf, 0, sizeof zero_buf);
    memset(&g_capture, 0, sizeof g_capture);
    ufi_cancel_clear();
    ufi_resources_reset();
    if (ufi_dump_active()) { ufi_dump_abort(); ufi_dump_service(); }
    /* known configuration per input (the module keeps UFI.CFG settings in statics) */
    static const char* const presets[] = {
        "drive=a\n", "drive=b\ndouble_step=auto\ntrack_search=1\nrevs=2\n", "drive=iec\niec_device=9\n",
        "drive=amiga\nformat=adf\n", "drive=ds1\nformat=img\ncopy_from=ds1\ncopy_to=ds2\n",
        "drive=a\nformat=hfe\nbutton_a=write\nwrite_file=FUZZ.IMG\n", "drive=b\ndouble_step=1\ntrack_search=1\nformat=img\n",
        "drive=b\nauto_dump=1\nformat=hfe3\n"};
    cfg_preset = presets[fuzz_byte() & 7u];
    ufi_config_load();
    cfg_preset = NULL;

    const uint8_t op = d[0] & 7u;
    if (op < 6u) {
        if (kinds[op].declared) {
            img_declared = kinds[op].declared;
            if (op == 3u) img_declared = (const uint32_t[]){368640u, 737280u, 1228800u, 1474560u}[fuzz_byte() & 3u];
        }
        ufi_sdwrite_start(kinds[op].name);
        run_job();
    } else if (op == 6u) {
        ufi_config_load();
        for (int k = 0; k < 24; k++) {                   /* idle: auto dump, buttons */
            ufi_dump_service();
            ufi_buttons_service();
        }
        if (ufi_dump_active()) run_job();
        static uint8_t out[UFI_V2_MAX_PAYLOAD];
        uint16_t len = 0;
        ufi_file_dir("", fuzz_u16(), out, (uint16_t)(fuzz_u16() % (UFI_V2_MAX_PAYLOAD + 1u)), &len);
        assert(len <= UFI_V2_MAX_PAYLOAD);
        ufi_file_read("FUZZ.IMG", fuzz_u32(), out, (uint16_t)(fuzz_u16() % (UFI_V2_MAX_PAYLOAD + 1u)), &len);
        assert(len <= UFI_V2_MAX_PAYLOAD);
        ufi_file_write("NEW.BIN", fuzz_u32(), fuzz_bit(), img, (uint16_t)(img_len > 4096u ? 4096u : img_len), &len);
        ufi_file_delete("NEW.BIN");
    } else {
        const dump_config_t c = {.drive = (uint8_t)(1u + fuzz_byte() % 11u), .tracks = (uint8_t)(1u + (fuzz_byte() & 3u)),
                                 .sides = (uint8_t)(1u + fuzz_bit()), .revs = (uint8_t)(1u + fuzz_byte() % 3u)};
        if (fuzz_bit()) ufi_copy_start();                /* disk to disk with the configured drives */
        else ufi_dump_start_fmt(&c, (uint8_t)(fuzz_byte() % 5u));
        run_job();
    }
    return 0;
}
