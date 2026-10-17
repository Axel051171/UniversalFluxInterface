/**
 * UFI Flux Engine - stand-alone disk dump to the SD NAND (board v0.6)
 *
 * Button A held >= 1 s (or DUMP_START from the host) reads the whole disk and writes
 * DUMPnnnn.SCP (first free number) to the FAT32 volume on the SD NAND, or with UFI.CFG
 * format=hfe (1.17) HxC bit cells, format=img|adf (1.16) the decoded sectors of the format found on track 0 as
 * DUMPnnnn.IMG/.ADF (cylinder-major, missing sectors zero).  Button B aborts.  The file is byte-identical to `ufi read-disk` (software/ufi_host scp.py):
 * same SCP v2.2 layout, same 275 MHz -> 25 ns rounding on absolute times.
 *
 * Runs as a state machine from the main loop, one step per call; a track is captured
 * with the normal (non-streaming) capture, then written in one go (~0.2 MB, polled SD).
 * LEDs: FDD on while capturing, ACT blinks per track, ACT steady = done,
 * ERR blinks the error code (1 storage, 2 drive/read, 3 disk full/write, 4 aborted).
 *
 * The USB mass storage mode (ufi_msc.c) and a dump never run at the same time.
 */

#include "ufi_firmware.h"
#include "ufi_media.h"              /* ufi_drive_profiles_load after UFI.CFG */
#include "ufi_resources.h"
#include "ff.h"
#include "ufi_mfm.h"
#include "ufi_image.h"
#include "ufi_hfe_v3.h"
#include "ufi_cancel.h"
#include <string.h>

#ifndef UFI_FW_VERSION
#define UFI_FW_VERSION "0.0"
#endif
#ifndef UFI_GIT_REV
#define UFI_GIT_REV "dev"
#endif

#define SCP_TRACKS          168u
#define SCP_HDR_LEN         0x10u
#define SCP_TABLE_LEN       (4u * SCP_TRACKS)
#define SCP_FLAG_INDEX      0x01u
#define SCP_DISK_OTHER      0x80u
#define SCP_DISK_APPLE2     0x40u   /* manufacturer Apple, Apple II */
#define SCP_VERSION         0x22u
#define HOLD_START_MS       1000u   /* button A */
#define HOLD_MSC_MS         2000u   /* button B: USB mass storage mode on/off */

extern capture_context_t g_capture;

typedef enum { D_IDLE, D_START, D_CAPTURE, D_WAIT, D_WRITE, D_FINISH, D_DONE, D_ERROR, D_COPY,
               D_IEC, D_WSTART, D_WTRACK, D_WIEC } dump_state_t;   /* D_W*: write from SD (1.16) */

/* drive=iec (1.14): D64/D71/D81 dump through the drive's own controller, one block per
 * service call.  iec_type: 0 = auto (DOS message after a bus reset), 41, 71, 81 */
static const char* file_ext = ".SCP";
static uint8_t iec_sec, iec_tracks, iec_type, iec_kind;

/* UFI.CFG auto_dump=1 (1.14): a dump starts by itself when a disk is inserted (DSKCHG) */
static bool auto_dump;
static uint8_t ad_state;                /* 0 wait for the slot to empty, 1 wait for a disk, 2 settle */
static uint32_t ad_t;
#define AD_POLL_MS          500u
#define AD_STEP_MS          2000u       /* step pair that clears the DSKCHG latch */
#define AD_SETTLE_MS        1000u
/* UFI.CFG double_step=0|1|auto (1.14): 0/1 fixed, 2 = detect a 40-track disk on track 0 */
static uint8_t dstep_cfg;
static bool dstep_pending, tracks_auto;
static char iec_msg[40];                /* DOS power-up message for the log */
static uint16_t iec_bad;
/* DUMPnnnn.JSN (1.16): extension of the image written, rpm sum over the logged tracks */
static char cur_ext[5] = ".SCP";
static char pending_name[13],completed_name[13];
static uint32_t rpm_sum, rpm_n;
static void write_json(void);
/* format=scp|img|adf|hfe: 0 flux (SCP), 1/2 decoded sectors (1.16), 3 HFE bit cells (1.17) */
static uint8_t dump_fmt_cfg, dump_fmt;
static uint16_t q_size = 512;           /* sector size of the probed format */

/* CPU-polled SD transfers: the staging buffer may live in DTCM (not zeroed at start) */
__attribute__((section(".dtcm"), aligned(4))) static uint8_t stage[32 * 1024];

static FATFS fs;
static FIL file;
static dump_state_t state;
static dump_config_t cfg = {.drive = DRIVE_SHUGART_A, .tracks = 80, .sides = 2, .revs = 3};
static uint8_t cyl, head, err_code, file_open;
static uint16_t file_no;
static uint32_t table[SCP_TRACKS];
static uint32_t checksum, stage_len;
static int fs_result;

/* ============================================================================
 * SCP encoding (mirrors scp.py: ticks_to_scp + _encode_cells)
 * ============================================================================ */

/* round(t * 40 MHz / 275 MHz) as floor((80 t + 275) / 550), split so 32 bit suffice */
static inline uint32_t tick_to_scp(uint32_t t)
{
    return 8u * (t / 55u) + (16u * (t % 55u) + 55u) / 110u;
}

static int flush_stage(void)
{
    UINT bw = 0;
    if (stage_len && (f_write(&file, stage, stage_len, &bw) != FR_OK || bw != stage_len)) {
        return UFI_ERR_STORAGE;
    }
    stage_len = 0;
    return UFI_OK;
}

static int put(const void* p, uint32_t n)
{
    const uint8_t* b = p;
    while (n--) {
        if (stage_len == sizeof(stage) && flush_stage() != UFI_OK) {
            return UFI_ERR_STORAGE;
        }
        checksum += *b;
        stage[stage_len++] = *b++;
    }
    return UFI_OK;
}

static int put_u32(uint32_t v)
{
    const uint8_t b[4] = {(uint8_t)v, (uint8_t)(v >> 8), (uint8_t)(v >> 16), (uint8_t)(v >> 24)};
    return put(b, 4);
}

/* SCP words for one revolution; emit = false only counts them */
static uint32_t encode_rev(const flux_revolution_t* r, bool emit, int* ret)
{
    uint32_t words = 0, prev = 0;
    for (uint32_t i = 0; i < r->count; i++) {
        const uint32_t now = tick_to_scp(r->samples[i].timestamp);
        if (now <= prev) {
            continue;
        }
        uint32_t c = now - prev;
        prev = now;
        if ((c & 0xFFFFu) == 0u) {
            *ret = UFI_ERR_NOT_IMPL;  /* zero words are continuation, never a transition */
            return 0;
        }
        while (c > 0xFFFFu) {
            words++;
            if (emit) {
                *ret |= put("\0\0", 2);
            }
            c -= 0x10000u;
        }
        words++;
        if (emit) {
            const uint8_t be[2] = {(uint8_t)(c >> 8), (uint8_t)c};
            *ret |= put(be, 2);
        }
    }
    return words;
}

static int write_track(uint8_t scp_track)
{
    const uint8_t n = cfg.revs;
    if (ufi_flux_get_revolution_count() < n) {
        return UFI_ERR_NO_INDEX;
    }
    table[scp_track] = (uint32_t)f_tell(&file) + stage_len;

    uint32_t words[REVOLUTIONS_BUFFER];
    int ret = put("TRK", 3) | put(&scp_track, 1);
    uint32_t off = 4u + 12u * n;
    for (uint8_t k = 0; k < n; k++) {
        const flux_revolution_t* r = ufi_flux_get_revolution(k);
        words[k] = encode_rev(r, false, &ret);
        const uint32_t idx = (uint32_t)(((uint64_t)r->index_time * 40u + 137u) / 275u);
        ret |= put_u32(idx) | put_u32(words[k]) | put_u32(off);
        off += 2u * words[k];
    }
    for (uint8_t k = 0; k < n && ret == UFI_OK; k++) {
        UFI_WATCHDOG_FEED();
        encode_rev(ufi_flux_get_revolution(k), true, &ret);
    }
    return ret == UFI_OK ? flush_stage() : UFI_ERR_STORAGE;
}

/* ============================================================================
 * file system
 * ============================================================================ */

static int mount(void)
{
    if (ufi_usb_msc_active() || ufi_sd_init() != UFI_OK) {
        return UFI_ERR_STORAGE;
    }
    FRESULT fr = f_mount(&fs, "", 1);
    /* A damaged filesystem must never be mistaken for a blank chip and erased.
     * Initial FAT32 formatting is explicit through USB mass storage. */
    fs_result = (int)fr;
    return fr == FR_OK ? UFI_OK : UFI_ERR_STORAGE;
}

static void unmount(void)
{
    if (file_open) {
        f_close(&file);
        file_open = 0;
    }
    f_mount(NULL, "", 0);
}

static int open_next_file(void)
{
    char name[13];
    FILINFO fi;
    for (uint16_t n = 1; n <= 9999u; n++) {
        name[0] = 'D'; name[1] = 'U'; name[2] = 'M'; name[3] = 'P';
        name[4] = (char)('0' + n / 1000u);
        name[5] = (char)('0' + n / 100u % 10u);
        name[6] = (char)('0' + n / 10u % 10u);
        name[7] = (char)('0' + n % 10u);
        memcpy(&name[8], file_ext, 5);
        if (f_stat(name, &fi) == FR_NO_FILE) {
            memcpy(completed_name,name,sizeof(name));
            memcpy(name+8,".PRT",5);
            if(f_stat(name,&fi)!=FR_NO_FILE)continue; /* retain interrupted captures */
            if (f_open(&file, name, FA_WRITE | FA_CREATE_NEW) != FR_OK) {
                return UFI_ERR_STORAGE;
            }
            memcpy(pending_name,name,sizeof(name));
            file_open = 1;
            file_no = n;
            memcpy(cur_ext, file_ext, 5);
            return UFI_OK;
        }
    }
    return UFI_ERR_STORAGE;
}

/* Called only after all data, final headers and f_close() succeeded. No overwrite. */
static int publish_capture(void){
    return f_rename(pending_name,completed_name)==FR_OK?UFI_OK:UFI_ERR_STORAGE;
}

/* header + table placeholder now, real values in finish() */
static int begin_file(void)
{
    memset(table, 0, sizeof(table));
    checksum = 0;
    stage_len = 0;
    memset(stage, 0, SCP_HDR_LEN + SCP_TABLE_LEN);
    stage_len = SCP_HDR_LEN + SCP_TABLE_LEN;           /* not part of the checksum yet */
    return UFI_OK;
}

static int finish_file(void)
{
    if (flush_stage() != UFI_OK) {
        return UFI_ERR_STORAGE;
    }
    const uint8_t* t = (const uint8_t*)table;          /* little endian, as on the wire */
    for (uint32_t i = 0; i < sizeof(table); i++) {
        checksum += t[i];
    }
    const uint8_t last = (uint8_t)((cfg.tracks - 1u) * 2u + (cfg.sides - 1u));
    /* Apple Disk II: SCP disk type Apple II, no index pulse (200 ms slices, not index cued) */
    const bool apple = (cfg.drive == DRIVE_APPLE_II || cfg.drive == DRIVE_APPLE2);
    uint8_t hdr[SCP_HDR_LEN] = {'S', 'C', 'P', SCP_VERSION, apple ? SCP_DISK_APPLE2 : SCP_DISK_OTHER,
                                cfg.revs, 0, last,
                                0x80u | ((apple && !ufi_config_apple_sync()) ? 0u : SCP_FLAG_INDEX), 0, (uint8_t)(cfg.sides == 2 ? 0 : 1), 0,
                                (uint8_t)checksum, (uint8_t)(checksum >> 8),
                                (uint8_t)(checksum >> 16), (uint8_t)(checksum >> 24)};
    UINT bw = 0;
    if (f_lseek(&file, 0) != FR_OK || f_write(&file, hdr, sizeof(hdr), &bw) != FR_OK || bw!=sizeof(hdr) ||
        f_write(&file, table, sizeof(table), &bw) != FR_OK || bw!=sizeof(table) || f_close(&file) != FR_OK) {
        return UFI_ERR_STORAGE;
    }
    file_open = 0;
    return UFI_OK;
}

/* ============================================================================
 * state machine
 * ============================================================================ */

/* ============================================================================
 * quality report (DUMPnnnn.LOG) and disk-to-disk copy
 * ============================================================================ */

static FIL logfile;
static bool log_open, copying, button_copy, button_write, protocol_gw, protocol_composite, apple_sync;
static uint16_t index_sim;              /* UFI.CFG index_sim=300|360 (J9 pin 6), 0 = off */
static uint8_t index_sim_mode;          /* UFI.CFG index_sim_mode=pin|internal|both */
static uint8_t rpm_line;                /* UFI.CFG rpm_line=none|density|drate (3-mode drives) */
static bool rpm_360_low = true;         /* UFI.CFG rpm_360=low|high */
static uint16_t rpm_start;              /* UFI.CFG rpm=300|360 applied at start, 0 = untouched */
static uint8_t iec_device = 8;          /* UFI.CFG iec_device=8..30: 1541 for drive=iec dumps */
static bool cfg_tg43;                   /* UFI.CFG tg43=1 (8" drives): DENSITY = TG43 from track 43 */

/* sectors on a 1541/1571 track (1-based; 1571 side 2 = 36-70) or a 1581 track */
static uint8_t iec_spt(uint8_t t)
{
    if (iec_kind == 81u) {
        return 40u;
    }
    if (iec_kind == 71u && t > 35u) {       /* 1571 side 2 mirrors the zones; 1541 tracks 36-40: 17 */
        t = (uint8_t)(t - 35u);
    }
    return t <= 17u ? 21u : t <= 24u ? 19u : t <= 30u ? 18u : 17u;
}

/* which drive answers: UFI.CFG iec_type, or the power-up message ("73,CBM DOS V2.6 1541",
 * "73,CBM DOS V3.0 1571", "73,COPYRIGHT CBM DOS V10 1581") after a bus reset */
static uint8_t iec_detect(void)
{
    if (iec_type) {
        return iec_type;
    }
    iec_msg[0] = 0;
    ufi_iec_reset();
    if (ufi_iec_wait_ready(iec_device, 5000u) != UFI_OK ||
        ufi_iec_read_status(iec_device, iec_msg, sizeof(iec_msg)) <= 0) {
        return 41u;
    }
    return strstr(iec_msg, "1581") ? 81u : strstr(iec_msg, "1571") ? 71u : 41u;
}
static bool track_search;               /* UFI.CFG track_search=1: weak track -> try +-1 physical track */
static uint8_t copy_src = DRIVE_SHUGART_A, copy_dst = DRIVE_SHUGART_B;
static uint8_t tries, q_spt, bad_tracks;
static bool q_known, q_amiga;
static uint32_t q_rate;

static int prepare_drive(uint8_t d)
{
    if (ufi_drive_select((drive_type_t)d) != UFI_OK) {
        return UFI_ERR_NO_DRIVE;
    }
    if (!ufi_drive_motor_is_on() && ufi_drive_motor(true) != UFI_OK) {
        return UFI_ERR_NO_DRIVE;
    }
    return ufi_drive_recalibrate();
}

typedef struct {
    uint64_t good;
    uint8_t max_r;
    uint8_t cyl;
    uint16_t size;              /* 128 << n of the first good sector */
} qctx_t;

static void q_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    (void)data;
    qctx_t* q = p;
    if (crc_ok && id->r >= 1u && id->r <= 63u && id->c == q->cyl) {
        q->good |= 1ull << (id->r - 1u);
        if (!q->size && id->n <= 3u) {
            q->size = (uint16_t)(128u << id->n);
        }
        if (id->r > q->max_r) {
            q->max_r = id->r;
        }
    }
}

/* the captured track (all revolutions as one stream) through a decoder */
static void feed_capture(mfm_decoder_t* d)
{
    uint32_t carry = 0;
    for (uint8_t k = 0; k < ufi_flux_get_revolution_count(); k++) {
        const flux_revolution_t* r = ufi_flux_get_revolution(k);
        uint32_t prev = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            mfm_dec_interval(d, r->samples[i].timestamp - prev + carry);
            carry = 0;
            prev = r->samples[i].timestamp;
        }
        carry = r->index_time - prev;
    }
}

/* good sectors of the captured track */
static uint8_t count_good(uint32_t rate, bool amiga, uint8_t* max_r, uint16_t* size)
{
    __attribute__((section(".dtcm"))) static mfm_decoder_t d;
    qctx_t q = {.cyl = cyl};
    if (amiga) {
        mfm_dec_init_amiga(&d, rate, q_sector, &q);
    } else {
        mfm_dec_init(&d, rate, q_sector, &q);
    }
    feed_capture(&d);
    *max_r = q.max_r;
    if (size) {
        *size = q.size ? q.size : 512u;
    }
    uint8_t n = 0;
    for (uint64_t g = q.good; g; g &= g - 1u) {
        n++;
    }
    return n;
}

/* on track 0.0 probe IBM 500k/250k/300k/1M and Amiga DD/HD; afterwards decode that format */
static uint8_t track_quality(void)
{
    static const uint32_t rates[] = {500000u, 250000u, 300000u, 1000000u, 250000u, 500000u};
    uint8_t max_r = 0;
    uint16_t size = 512;
    if (cyl == 0 && head == 0 && tries == 0) {
        uint8_t best = 0;
        for (uint32_t i = 0; i < 6; i++) {
            const uint8_t g = count_good(rates[i], i >= 4u, &max_r, &size);
            if (g > best) {
                best = g;
                q_rate = rates[i];
                q_amiga = (i >= 4u);
                q_spt = max_r;
                q_size = size;
            }
        }
        q_known = best >= 4u;
        return best;
    }
    return q_known ? count_good(q_rate, q_amiga, &max_r, NULL) : 0u;
}

static void ds_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    (void)data;
    uint8_t* n = p;                     /* n[0] = IDs saying cylinder 1, n[1] = cylinder 2 */
    if (crc_ok && (id->c == 1u || id->c == 2u) && n[id->c - 1u] < 255u) {
        n[id->c - 1u]++;
    }
}

/* double_step=auto: on physical track 2 a 48-tpi (40-track) disk still shows cylinder 1
 * in its sector IDs; an 80-track disk shows cylinder 2.  Format from the track-0 probe. */
static bool probe_double_step(void)
{
    __attribute__((section(".dtcm"))) static mfm_decoder_t d;
    uint8_t n[2] = {0, 0};
    g_capture.state = CAPTURE_IDLE;
    if (ufi_capture_start(2, 0, 1, 0) != UFI_OK) {
        return false;
    }
    const uint32_t t0 = HAL_GetTick();
    capture_state_t cs;
    while ((cs = ufi_capture_get_state()) != CAPTURE_COMPLETE && cs != CAPTURE_ERROR) {
        if (HAL_GetTick() - t0 > 3000u) {
            ufi_capture_abort();
            g_capture.state = CAPTURE_IDLE;
            return false;
        }
    }
    if (cs == CAPTURE_COMPLETE) {
        if (q_amiga) {
            mfm_dec_init_amiga(&d, q_rate, ds_sector, n);
        } else {
            mfm_dec_init(&d, q_rate, ds_sector, n);
        }
        feed_capture(&d);
    }
    g_capture.state = CAPTURE_IDLE;
    return n[0] >= 2u && n[0] > n[1];
}

/* ============================================================================
 * format=img|adf (1.16): the decoded sectors of the probed format instead of flux
 * ============================================================================ */

typedef struct {
    uint64_t got;
    uint8_t cyl;
} ictx_t;

static void img_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    ictx_t* c = p;
    if (!crc_ok || id->c != c->cyl || id->r < 1u || id->r > q_spt || (128u << id->n) != q_size) {
        return;
    }
    memcpy(&stage[(uint32_t)(id->r - 1u) * q_size], data, q_size);
    c->got |= 1ull << (id->r - 1u);
}

/* decode the captured track into stage[] (missing sectors stay zero) and append it */
static int write_image_track(void)
{
    __attribute__((section(".dtcm"))) static mfm_decoder_t d;
    if (!q_known) {
        return UFI_ERR_NOT_IMPL;                    /* no MFM/Amiga format on track 0 */
    }
    const uint32_t len = (uint32_t)q_spt * q_size;
    if (len > sizeof(stage)) {
        return UFI_ERR_NOT_IMPL;
    }
    memset(stage, 0, len);
    ictx_t c = {.cyl = cyl};
    if (q_amiga) {
        mfm_dec_init_amiga(&d, q_rate, img_sector, &c);
    } else {
        mfm_dec_init(&d, q_rate, img_sector, &c);
    }
    feed_capture(&d);
    for (uint32_t i = 0; i < len; i++) {
        checksum += stage[i];
    }
    UINT bw = 0;
    UFI_WATCHDOG_FEED();
    return (f_write(&file, stage, len, &bw) == FR_OK && bw == len) ? UFI_OK : UFI_ERR_STORAGE;
}

/* ============================================================================
 * format=hfe (1.17): HxC HFE v1 - revolution 0 of every track as MFM bit cells.
 * Side 0 of a cylinder fixes the side length and writes whole 512-byte blocks (second
 * half zero); side 1 fills the second halves.  Header and track table in blocks 0/1
 * are written in D_FINISH.  table[cyl] = block << 16 | length (both sides).
 * ============================================================================ */

static void log_puts(const char* s);
static uint16_t hfe_next_block;         /* next free 512-byte block */
static uint32_t hfe_side_bytes;         /* bytes per side of the current cylinder */
static uint32_t hfe_rate;               /* bit rate written into the header */
static uint8_t hfe_enc;
static uint32_t hfe_nbits;              /* bits in the current 256-byte half block */
static uint16_t hfe_blk;                /* half blocks flushed for this side */

static int hfe_flush_half(void)
{
    const uint32_t base = ((uint32_t)(table[cyl] >> 16) + hfe_blk) * HFE_BLOCK + (head ? 256u : 0u);
    UINT bw = 0;
    memset(&stage[(hfe_nbits + 7u) / 8u], 0, 256u - (hfe_nbits + 7u) / 8u);   /* no stale bytes */
    for (uint32_t i = 0; i < 256u; i++) {
        checksum += stage[i];
    }
    if (head == 0) {
        memset(&stage[256], 0, 256);                    /* side 1 half: filled later or stays empty */
    }
    if (f_lseek(&file, base) != FR_OK ||
        f_write(&file, stage, head ? 256u : 512u, &bw) != FR_OK || bw != (head ? 256u : 512u)) {
        return UFI_ERR_STORAGE;
    }
    hfe_blk++;
    hfe_nbits = 0;
    return UFI_OK;
}

/* n cells into the half block buffer, flushing as it fills; limit = bits per side */
static int hfe_emit(uint32_t n, bool mark, uint32_t limit)
{
    while (n) {
        const uint32_t have = (uint32_t)hfe_blk * 2048u + hfe_nbits;
        if (have >= limit) {
            return UFI_OK;                              /* side 1 longer than side 0: cut */
        }
        uint32_t cap = 2048u;
        if (limit - have < 2048u - hfe_nbits) {
            cap = hfe_nbits + (limit - have);
        }
        const uint32_t w = hfe_put_cells(stage, cap, &hfe_nbits, n, mark);
        n -= w;
        if (hfe_nbits >= 2048u || (n && w == 0u)) {
            const int ret = hfe_flush_half();
            if (ret != UFI_OK) {
                return ret;
            }
        }
    }
    return UFI_OK;
}

static int write_hfe_track(void)
{
    if (ufi_flux_get_revolution_count() < 1u) {
        return UFI_ERR_NO_INDEX;
    }
    const flux_revolution_t* r = ufi_flux_get_revolution(0);
    if (cyl == 0 && head == 0) {
        /* bit rate from the track-0 probe; without a format: by the transition count */
        hfe_rate = q_known ? q_rate : (r->count > 55000u ? 500000u : 250000u);
        hfe_enc = q_known ? (q_amiga ? HFE_ENC_AMIGA_MFM : HFE_ENC_ISOIBM_MFM) : HFE_ENC_UNKNOWN;
        if (!q_known) {
            log_puts(hfe_rate == 500000u ? "hfe: no MFM format on track 0, 500 kbit/s by flux density\r\n"
                                         : "hfe: no MFM format on track 0, 250 kbit/s by flux density\r\n");
        }
        hfe_next_block = 2;
    }
    hfe_pll_t pll;
    hfe_pll_init(&pll, hfe_rate);
    uint32_t limit;
    if (head == 0) {
        /* side length: cells in one revolution at the nominal rate, rounded up to bytes */
        const uint32_t cells = (uint32_t)(((uint64_t)r->index_time * 2u * hfe_rate + FLUX_TIMER_FREQ / 2u) / FLUX_TIMER_FREQ);
        hfe_side_bytes = (cells + 7u) / 8u;
        if (hfe_side_bytes * 2u > 65535u || hfe_next_block + (hfe_side_bytes + 255u) / 256u > 65535u) {
            return UFI_ERR_NOT_IMPL;                    /* ED / file too large for HFE v1 */
        }
        table[cyl] = ((uint32_t)hfe_next_block << 16) | (hfe_side_bytes * 2u);
        hfe_next_block = (uint16_t)(hfe_next_block + (hfe_side_bytes + 255u) / 256u);
    }
    limit = hfe_side_bytes * 8u;
    hfe_nbits = 0;
    hfe_blk = 0;
    uint32_t prev = 0;
    int ret = UFI_OK;
    for (uint32_t i = 0; i < r->count && ret == UFI_OK; i++) {
        if ((i & 1023u) == 0u) {
            UFI_WATCHDOG_FEED();
        }
        ret = hfe_emit(hfe_pll_cells(&pll, r->samples[i].timestamp - prev), true, limit);
        prev = r->samples[i].timestamp;
    }
    if (ret == UFI_OK) {
        /* the gap before the index and any shortfall: zero cells up to the side length */
        const uint32_t have = (uint32_t)hfe_blk * 2048u + hfe_nbits;
        if (have < limit) {
            ret = hfe_emit(limit - have, false, limit);
        }
    }
    if (ret == UFI_OK && hfe_nbits) {
        ret = hfe_flush_half();
    }
    return ret;
}

/* v3 export of recovered bitcells, with an explicit nominal bitrate and index.
 * No invented weak markers; raw capture timing remains available through SCP. */
static int write_hfe_v3_track(void){
    if(!ufi_flux_get_revolution_count())return UFI_ERR_NO_INDEX;
    const flux_revolution_t* r=ufi_flux_get_revolution(0);
    if(!r || !r->index_time || !r->count)return UFI_ERR_BAD_ARGS;
    if(!cyl && !head){
        hfe_rate=q_known?q_rate:r->count>55000u?500000u:250000u;
        hfe_enc=q_known?(q_amiga?HFE_ENC_AMIGA_MFM:HFE_ENC_ISOIBM_MFM):HFE_ENC_UNKNOWN;
        hfe_next_block=2;
    }
    uint32_t period=(18000000u+hfe_rate/2u)/hfe_rate;
    uint64_t wanted=((uint64_t)r->index_time*2u*hfe_rate+FLUX_TIMER_FREQ/2u)/FLUX_TIMER_FREQ;
    if(!period || period>255 || !wanted || wanted>262136u)return UFI_ERR_RANGE;
    hfe_v3_encoder_t enc;
    if(!hfe_v3_encode_begin(&enc,stage,32767,(uint8_t)period))return UFI_ERR_BUFFER_FULL;
    hfe_pll_t pll;hfe_pll_init(&pll,hfe_rate);uint32_t have=0,previous=0;
    for(uint32_t i=0;i<r->count;i++){
        if(!(i&1023u)){UFI_WATCHDOG_FEED();if(ufi_cancel_requested())return UFI_ERR_CANCELLED;}
        uint32_t timestamp=r->samples[i].timestamp;
        if(timestamp<=previous || timestamp>r->index_time)return UFI_ERR_BAD_ARGS;
        uint32_t cells=hfe_pll_cells(&pll,timestamp-previous);previous=timestamp;
        if(cells>wanted-have || !hfe_v3_encode_cells(&enc,cells,true))return UFI_ERR_BUFFER_FULL;
        have+=cells;
    }
    if(have<wanted && !hfe_v3_encode_cells(&enc,(uint32_t)wanted-have,false))return UFI_ERR_BUFFER_FULL;
    if(!hfe_v3_encode_end(&enc))return UFI_ERR_BUFFER_FULL;
    /* Fixed maximum extent lets either side grow independently. NOP padding adds no cells. */
    memset(stage+enc.length,0x0f,sizeof(stage)-enc.length);
    if(!head){table[cyl]=((uint32_t)hfe_next_block<<16)|65534u;hfe_next_block+=128u;}
    for(unsigned block=0;block<128;block++){
        UINT written=0;UFI_WATCHDOG_FEED();
        if(f_lseek(&file,((table[cyl]>>16)+block)*512u+head*256u)!=FR_OK ||
           f_write(&file,stage+block*256u,256,&written)!=FR_OK || written!=256)return UFI_ERR_STORAGE;
    }
    return UFI_OK;
}

/* header + track table into blocks 0 and 1 */
static int finish_hfe(void)
{
    const uint32_t rpm10 = rpm_n ? (rpm_sum + rpm_n / 2u) / rpm_n : 0u;
    hfe_make_header(stage, cfg.tracks, cfg.sides, hfe_enc, hfe_rate, (uint16_t)(rpm10 / 10u),
                    ufi_drive_get_timing().double_step != 0u);
    if(dump_fmt==4u)memcpy(stage,"HXCHFEV3",8);
    memset(&stage[HFE_BLOCK], 0xFF, HFE_BLOCK);
    for (uint32_t t = 0; t < cfg.tracks && t < HFE_BLOCK / 4u; t++) {
        hfe_make_lut_entry(&stage[HFE_BLOCK + 4u * t], (uint16_t)(table[t] >> 16), (uint16_t)table[t]);
    }
    UINT bw = 0;
    if (f_lseek(&file, 0) != FR_OK || f_write(&file, stage, 2u * HFE_BLOCK, &bw) != FR_OK ||
        bw != 2u * HFE_BLOCK || f_close(&file) != FR_OK) {
        return UFI_ERR_STORAGE;
    }
    file_open = 0;
    return UFI_OK;
}

static void log_puts(const char* s)
{
    UINT bw = 0;
    if (log_open) {
        f_write(&logfile, s, (UINT)strlen(s), &bw);
    }
}

static char* put_num(char* p, uint32_t v, int width)
{
    char tmp[10];
    int n = 0;
    do {
        tmp[n++] = (char)('0' + v % 10u);
        v /= 10u;
    } while (v && n < 10);
    while (n < width) {
        tmp[n++] = ' ';
    }
    while (n) {
        *p++ = tmp[--n];
    }
    return p;
}

static void open_log(void)
{
    char name[13];
    memcpy(name, "DUMP0000.LOG", 13);
    name[4] = (char)('0' + file_no / 1000u);
    name[5] = (char)('0' + file_no / 100u % 10u);
    name[6] = (char)('0' + file_no / 10u % 10u);
    name[7] = (char)('0' + file_no % 10u);
    log_open = (f_open(&logfile, name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
}

static void log_track(uint8_t good)
{
    if (!log_open) {
        return;
    }
    char line[64], *p = line;
    const flux_revolution_t* r = ufi_flux_get_revolution(0);
    const uint32_t rpm10 = r->index_time ? (uint32_t)(600ull * FLUX_TIMER_FREQ / r->index_time) : 0u;
    rpm_sum += rpm10;
    rpm_n++;
    p = put_num(p, cyl, 2);
    *p++ = '.';
    p = put_num(p, head, 1);
    p = put_num(p, good, 6);
    *p++ = '/';
    p = put_num(p, q_known ? q_spt : 0u, 2);
    p = put_num(p, rpm10 / 10u, 9);
    *p++ = '.';
    p = put_num(p, rpm10 % 10u, 1);
    p = put_num(p, tries + 1u, 4);
    if (q_known && good < q_spt) {
        memcpy(p, "  BAD", 5);
        p += 5;
    }
    *p++ = '\r';
    *p++ = '\n';
    *p = 0;
    log_puts(line);
}

static void close_log(void)
{
    if (!log_open) {
        return;
    }
    char line[48], *p = line;
    memcpy(p, "bad tracks: ", 12);
    p = put_num(p + 12, bad_tracks, 1);
    memcpy(p, q_known ? "\r\n" : " (format not recognised)\r\n", q_known ? 3 : 27);
    log_puts(line);
    f_close(&logfile);
    log_open = false;
}

/* Copy keeps a source-sector fingerprint before readback reuses the flux store. */
static int copy_track(void)
{
    const flux_revolution_t* r = ufi_flux_get_revolution(0);
    const uint32_t n = r->count;
    if (n < 3u) {
        return UFI_ERR_NO_INDEX;
    }
    uint32_t words = 0;
    uint32_t* store = ufi_flux_store(&words);
    uint32_t prev = 0;
    for (uint32_t i = 0; i < n; i++) {                 /* timestamps -> deltas, in place */
        const uint32_t t = r->samples[i].timestamp;
        store[i] = t - prev;
        prev = t;
    }
    g_capture.state = CAPTURE_IDLE;
    ufi_drive_select((drive_type_t)copy_dst);
    uint8_t level=0;
    int ret=ufi_write_local_checked(cyl,head,n,false,r->index_time,0,head,&level);
    if(!ret)log_puts(level==3?"copy: decoded source content verified\r\n":
                            "copy: pulse-count plausible; content UNVERIFIED\r\n");
    ufi_drive_select((drive_type_t)copy_src);
    return ret;
}

int ufi_copy_start(void)
{
    if (copy_src == copy_dst) {
        return UFI_ERR_NOT_IMPL;
    }
    copying = true;
    const int ret = ufi_dump_start(NULL);
    if (ret != UFI_OK) {
        copying = false;
    }
    return ret;
}

/* Blocking re-capture at the current physical position (no seek); quality or 0 */
static uint8_t recapture(void)
{
    g_capture.state = CAPTURE_IDLE;
    if (ufi_flux_capture_start(cfg.revs, 0) != UFI_OK) {
        return 0;
    }
    const uint32_t t0 = HAL_GetTick();
    capture_state_t cs;
    while ((cs = ufi_capture_get_state()) != CAPTURE_COMPLETE && cs != CAPTURE_ERROR) {
        if (HAL_GetTick() - t0 > 3000u) {
            ufi_flux_capture_stop();
            return 0;
        }
    }
    return cs == CAPTURE_COMPLETE ? track_quality() : 0u;
}

/* 40-track disk in an 80-track drive (double_step): the wide track may read better one
 * physical track in or out.  Tries both, ends on the best position with its capture. */
static uint8_t search_track(uint8_t good)
{
    uint8_t best = good;
    int best_pos = 0;
    ufi_drive_step(-1);
    const uint8_t q_out = recapture();
    if (q_out > best) { best = q_out; best_pos = -1; }
    ufi_drive_step(1);
    ufi_drive_step(1);
    const uint8_t q_in = recapture();
    if (q_in > best) { best = q_in; best_pos = 1; }
    if (best_pos != 1) {
        ufi_drive_step(-1);
        if (best_pos == -1) {
            ufi_drive_step(-1);
        }
        best = recapture();                 /* the capture must match the final position */
        ufi_drive_step(1);                  /* back to the nominal track for the next seek */
        if (best_pos == -1) {
            ufi_drive_step(1);
        }
    } else {
        ufi_drive_step(-1);
    }
    return best;
}

/* auto_dump=1: PC/Amiga drive with a DSKCHG line.  The latch is set without a disk and
 * after a change; a step pair clears it once a disk is in.  Idle polling is silent, the
 * step pair (a click) only while waiting for a disk; a finished dump waits for the disk
 * to be taken out before it arms again. */
static void auto_dump_service(void)
{
    if (!auto_dump || copying || protocol_gw || cfg.drive == DRIVE_IEC ||
        cfg.drive == DRIVE_APPLE_II || cfg.drive == DRIVE_APPLE2) {
        return;
    }
    if (ufi_usb_msc_active() || ufi_stream_active() ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return;
    }
    const drive_type_t cur = ufi_drive_get_current();
    if (cur != DRIVE_NONE && cur != (drive_type_t)cfg.drive) {
        return;                                     /* the host works with another drive */
    }
    const uint32_t now = HAL_GetTick();
    const uint32_t wait = ad_state == 1u ? AD_STEP_MS : ad_state == 2u ? AD_SETTLE_MS : AD_POLL_MS;
    if (now - ad_t < wait) {
        return;
    }
    ad_t = now;
    if (cur == DRIVE_NONE && ufi_drive_select((drive_type_t)cfg.drive) != UFI_OK) {
        return;
    }
    switch (ad_state) {
        case 0:                                     /* slot empty (latch set): arm */
            if (ufi_drive_disk_changed()) {
                ad_state = 1;
            }
            break;
        case 1:                                     /* clear the latch; still set = no disk */
            ufi_drive_step(1);
            ufi_drive_step(-1);
            if (!ufi_drive_disk_changed()) {
                ad_state = 2;
            }
            break;
        default:                                    /* disk in and settled: go */
            ad_state = 0;
            ufi_dump_start(NULL);
            return;                                 /* the dump keeps the drive selected */
    }
    if (cur == DRIVE_NONE) {
        ufi_drive_select(DRIVE_NONE);
    }
}

static void fail(uint8_t code)
{
    close_log();
    ufi_capture_abort();
    g_capture.state = CAPTURE_IDLE;                    /* main loop must not send it to USB */
    if (file_open) {
        f_close(&file);                                /* keep what was written */
        file_open = 0;
    }
    unmount();
    ufi_drive_motor(false);
    led_set(&PIN_LED_FDD, false);
    led_set(&PIN_LED_ACT, false);
    err_code = code;
    copying = false;
    state = D_ERROR;
}

/* stand-alone drive: PC A/B, Amiga, Shugart DS0-DS3 (no Apple, no IEC) */
static bool drive_ok(uint8_t d)
{
    return d == DRIVE_SHUGART_A || d == DRIVE_SHUGART_B || d == DRIVE_AMIGA || d == DRIVE_AMIGA2 ||
           ((d == DRIVE_APPLE_II || d == DRIVE_APPLE2) && ufi_board_has_apple()) ||
           (d >= DRIVE_SHUGART_DS0 && d <= DRIVE_SHUGART_DS3) || d == DRIVE_IEC;
}

static bool config_valid(const dump_config_t* c)
{
    return drive_ok(c->drive) && c->tracks <= 84 && c->sides >= 1 &&      /* 0 = probe */
           c->sides <= 2 && c->revs >= 1 && c->revs <= REVOLUTIONS_BUFFER;
}

uint8_t ufi_standalone_drive(void)
{
    return cfg.drive;
}

bool ufi_config_protocol_gw(void)
{
    return protocol_gw;
}

bool ufi_config_protocol_composite(void) { return protocol_composite; }

bool ufi_config_apple_sync(void)
{
    return apple_sync;
}

uint16_t ufi_config_index_sim(void)
{
    return index_sim;
}

uint8_t ufi_config_index_sim_mode(void)
{
    return index_sim_mode;
}

uint8_t ufi_config_iec_device(void)
{
    return iec_device;
}

/* ============================================================================
 * UFI.CFG on the SD NAND: drive and dump settings for stand-alone use, editable in the
 * SD drive mode.  Lines "key=value", '#' comments; written with defaults if missing.
 * ============================================================================ */

static const char cfg_default[] =
    "# UFI stand-alone settings (dump with button A, USB floppy mode)\r\n"
    "# drive: a, b (PC cable), amiga, amiga2 (DF2, needs JP3), ds0-ds3 (Shugart bus; ds3 needs JP1),\r\n"
    "#        apple, apple2 (Disk II port, board v0.7: use tracks=35 sides=1), iec (D64 dump)\r\n"
    "drive=a\r\n"
    "tracks=80\r\n"
    "sides=2\r\n"
    "revs=3\r\n"
    "# button A: dump (disk -> DUMPnnnn.SCP + .LOG), copy (copy_from -> copy_to) or write\r\n"
    "# (write_file from the SD NAND onto the disk: SCP flux, IMG/ADF/D81 sectors, D64 via iec)\r\n"
    "button_a=dump\r\n"
    "write_file=\r\n"
    "copy_from=a\r\n"
    "copy_to=b\r\n"
    "# flux protocol with the mode switch in the middle: ufi (ufi host tool) or gw\r\n"
    "# (Greaseweazle host tools: gw read, gw write, ...)\r\n"
    "protocol=ufi\r\n"
    "# Disk II sync (index) sensor on J19: 1 = Apple reads/writes use its index pulse\r\n"
    "apple_sync=0\r\n"
    "# index simulation on J9 pin 6 (open drain, wire to a drive's index sensor output for\r\n"
    "# flippy disks): 0 = off, 300 or 360 rpm; not with the mode switch on J13\r\n"
    "index_sim=0\r\n"
    "# pin: pulses on J9 pin 6; internal: pulses replace the drive's INDEX line for\r\n"
    "# reading/writing (no wire needed); both\r\n"
    "index_sim_mode=pin\r\n"
    "# 3-mode drive (300/360 rpm): which bus line selects the speed (none, density = pin 2,\r\n"
    "# drate = pin 6 via JP1), which level means 360 rpm (low|high), speed at start (0 = untouched)\r\n"
    "rpm_line=none\r\n"
    "rpm_360=low\r\n"
    "rpm=0\r\n"
    "# tg43=1: 8-inch drives, pin 2 carries TG43 (reduced write current from track 43);\r\n"
    "# not together with rpm_line=density\r\n"
    "tg43=0\r\n"
    "# drive=iec: 1541/1571/1581 on J8, dump via the drive's own controller to\r\n"
    "# DUMPnnnn.D64/.D71/.D81 (tracks=0: whole disk; 1541: 35 or 40; 1571: 35 = one side);\r\n"
    "# device number on the bus; iec_type=auto (DOS message after a bus reset), 1541, 1571, 1581\r\n"
    "iec_device=8\r\n"
    "iec_type=auto\r\n"
    "# tracks=0: find the highest readable track first (80-83); track_search=1: with\r\n"
    "# double_step (40-track disk in an 80-track drive) re-read weak tracks one physical\r\n"
    "# track in and out and keep the best position\r\n"
    "track_search=0\r\n"
    "# double_step: 0 = 96-tpi disk, 1 = 40-track disk in an 80-track drive (two steps per\r\n"
    "# track), auto = detect on track 0 (sector IDs on physical track 2); tracks > 42 are\r\n"
    "# then cut to 40 (tracks=0) or 42\r\n"
    "double_step=auto\r\n"
    "# auto_dump=1: start a dump by itself when a disk is inserted (PC/Amiga drive with\r\n"
    "# a disk-change line; waits for the disk to be taken out before the next one)\r\n"
    "auto_dump=0\r\n"
    "# format: scp (flux, everything), img or adf (decoded sectors of the MFM/Amiga format\r\n"
    "# found on track 0, missing sectors zero; error 5 = no format recognised)\r\n"
    "format=scp\r\n";

static const struct { const char* name; uint8_t type; } drive_names[] = {
    {"a", DRIVE_SHUGART_A}, {"b", DRIVE_SHUGART_B}, {"amiga", DRIVE_AMIGA}, {"iec", DRIVE_IEC},
    {"ds0", DRIVE_SHUGART_DS0}, {"ds1", DRIVE_SHUGART_DS1},
    {"ds2", DRIVE_SHUGART_DS2}, {"ds3", DRIVE_SHUGART_DS3}, {"amiga2", DRIVE_AMIGA2},
    {"apple", DRIVE_APPLE_II}, {"apple2", DRIVE_APPLE2},
};

/* ============================================================================
 * DUMPnnnn.JSN (1.16): machine-readable metadata next to the image (8.3 names, so .JSN)
 * ============================================================================ */

static char* js_text(char* p, const char* s)
{
    while (*s) {
        if (*s == '"' || *s == '\\') {
            *p++ = '\\';
        }
        *p++ = *s++;
    }
    return p;
}

static char* js_key(char* p, bool* first, const char* k)
{
    if (!*first) {
        *p++ = ',';
    }
    *first = false;
    memcpy(p, "\n  \"", 4);
    p = js_text(p + 4, k);
    memcpy(p, "\": ", 3);
    return p + 3;
}

static char* js_str(char* p, bool* first, const char* k, const char* v)
{
    p = js_key(p, first, k);
    *p++ = '"';
    p = js_text(p, v);
    *p++ = '"';
    return p;
}

static char* js_num(char* p, bool* first, const char* k, uint32_t v)
{
    return put_num(js_key(p, first, k), v, 1);
}

static const char* drive_name(uint8_t d)
{
    for (uint32_t i = 0; i < sizeof(drive_names) / sizeof(drive_names[0]); i++) {
        if (drive_names[i].type == d) {
            return drive_names[i].name;
        }
    }
    return "?";
}

/* after the image is closed: firmware, board, drive, geometry, format, rpm, errors, checksum */
static void write_json(void)
{
    char name[13], fmt[5], buf[768], *p = buf;
    memcpy(name, "DUMP0000.JSN", 13);
    name[4] = (char)('0' + file_no / 1000u);
    name[5] = (char)('0' + file_no / 100u % 10u);
    name[6] = (char)('0' + file_no / 10u % 10u);
    name[7] = (char)('0' + file_no % 10u);
    for (int i = 0; i < 4; i++) {                        /* ".SCP" -> "scp" */
        const char c = cur_ext[i + 1];
        fmt[i] = (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
    }
    fmt[4] = 0;
    bool first = true;
    *p++ = '{';
    memcpy(&name[8], cur_ext, 5);
    p = js_str(p, &first, "file", name);
    memcpy(&name[8], ".JSN", 5);
    p = js_str(p, &first, "format", fmt);
    p = js_str(p, &first, "firmware", UFI_FW_VERSION);
    p = js_str(p, &first, "git", UFI_GIT_REV);
    p = js_num(p, &first, "board", ufi_board_rev());
    p = js_str(p, &first, "drive", drive_name(cfg.drive));
    p = js_num(p, &first, "sample_hz", FLUX_TIMER_FREQ);
    if (cfg.drive == DRIVE_IEC) {
        p = js_str(p, &first, "dos", iec_msg);
        p = js_num(p, &first, "tracks", iec_tracks);
        p = js_num(p, &first, "bad_blocks", iec_bad);
    } else {
        p = js_num(p, &first, "tracks", cfg.tracks);
        p = js_num(p, &first, "sides", cfg.sides);
        p = js_num(p, &first, "revs", cfg.revs);
        p = js_num(p, &first, "double_step", ufi_drive_get_timing().double_step ? 1u : 0u);
        const uint32_t rpm10 = rpm_n ? (rpm_sum + rpm_n / 2u) / rpm_n : 0u;
        p = put_num(js_key(p, &first, "rpm"), rpm10 / 10u, 1);
        *p++ = '.';
        p = put_num(p, rpm10 % 10u, 1);
        p = js_str(p, &first, "encoding", q_known ? (q_amiga ? "amiga" : "mfm") : "unknown");
        p = js_num(p, &first, "rate_bps", dump_fmt >= 3u ? hfe_rate : q_known ? q_rate : 0u);
        p = js_num(p, &first, "spt", q_known ? q_spt : 0u);
        p = js_num(p, &first, "sector_bytes", q_known ? q_size : 0u);
        p = js_num(p, &first, "bad_tracks", bad_tracks);
    }
    p = js_num(p, &first, "checksum", checksum);
    memcpy(p, "\n}\n", 3);
    p += 3;
    UINT bw = 0;
    if (f_open(&logfile, name, FA_WRITE | FA_CREATE_ALWAYS) == FR_OK) {   /* the log is closed by now */
        f_write(&logfile, buf, (UINT)(p - buf), &bw);
        f_close(&logfile);
    }
}

/* ============================================================================
 * write an image from the SD NAND onto the disk (1.16): UFI.CFG button_a=write +
 * write_file=NAME, or WRITE_START from the host.  SCP: revolution 0 of every track as
 * flux (odd periods clamped); HFE (1.17): bit cells at the header rate, scaled to the
 * measured revolution (98.5 %, +-3 %); IMG/ADF/D81: sectors encoded like FORMAT; D64/D71/D81 with
 * drive=iec: U2 block writes.  Each track is read back (sector count / flux count).
 * Report in WRITE.LOG.  Errors: 1 storage, 2 drive, 3 write, 4 aborted, 5 image not
 * usable, 6 write-protected, 7 file not found.
 * ============================================================================ */

static char write_file[64];             /* UFI.CFG write_file= */
static char job_name[64];
static image_geo_t geo;
static scp_header_t scph;
static uint32_t job_cells, job_written, job_bad;
static uint8_t job_h0, job_h1;          /* heads to write: [h0, h1) */
static uint32_t job_rev;                /* measured revolution (ticks) for the HFE fit */
static uint32_t job_permille = 1000;    /* HFE: cell scale applied to the current track */
static hfe_header_t hfeh;
static uint32_t job_source_ticks;
typedef struct {uint32_t block,length,cached;uint8_t side;} hfe_sd_reader_t;
static bool hfe_sd_byte(void* context,uint32_t offset,uint8_t* byte){
    hfe_sd_reader_t* r=context;uint32_t chunk=offset/256u;
    if(offset>=r->length || ufi_cancel_requested())return false;
    if(chunk!=r->cached){
        uint32_t take=r->length-chunk*256u;if(take>256)take=256;
        UINT got=0;UFI_WATCHDOG_FEED();
        if(f_lseek(&file,(r->block+chunk)*512u+r->side*256u)!=FR_OK ||
           f_read(&file,stage,take,&got)!=FR_OK || got!=take)return false;
        r->cached=chunk;
    }
    *byte=stage[offset%256u];return true;
}
static int load_hfe_v3(uint32_t* store,uint32_t max,uint32_t* n){
    hfe_sd_reader_t reader={table[cyl]>>16,(table[cyl]&65535u)/2u,UINT32_MAX,head};
    hfe_v3_report_t report;
    int ret=hfe_v3_decode(hfe_sd_byte,&reader,reader.length,store,max,&report);
    *n=report.flux_count;job_source_ticks=report.ticks;job_permille=1000;
    return ret;
}

/* Validate the entire SCP before the first destructive media operation. */
static bool scp_checksum_ok(const uint8_t* hdr)
{
    const uint32_t expected = (uint32_t)hdr[12] | ((uint32_t)hdr[13] << 8) |
                              ((uint32_t)hdr[14] << 16) | ((uint32_t)hdr[15] << 24);
    if (f_size(&file) < SCP_HDR_LEN + SCP_TABLE_LEN) return false;
    if (!expected && (hdr[8] & 0x10u)) return true; /* mutable image has no checksum */
    if (f_lseek(&file, SCP_HDR_LEN) != FR_OK) return false;
    uint32_t sum = 0, left = (uint32_t)f_size(&file) - SCP_HDR_LEN;
    while (left) {
        const UINT take = left > sizeof(stage) ? sizeof(stage) : left;
        UINT got = 0;
        UFI_WATCHDOG_FEED();
        if (f_read(&file, stage, take, &got) != FR_OK || got != take) return false;
        for (UINT i = 0; i < got; i++) sum += stage[i];
        left -= got;
    }
    return sum == expected && f_lseek(&file, SCP_HDR_LEN) == FR_OK;
}

static void job_log(const char* tag)
{
    char line[48], *p = line;
    p = put_num(p, cyl, 2);
    *p++ = '.';
    p = put_num(p, head, 1);
    *p++ = ' ';
    *p++ = ' ';
    memcpy(p, tag, strlen(tag));
    p += strlen(tag);
    *p++ = '\r';
    *p++ = '\n';
    *p = 0;
    log_puts(line);
}

/* the current track from the file into the flux store; *n = deltas, false = not in the file */
static bool job_load_track(uint32_t* n, int* err)
{
    uint32_t words = 0;
    uint32_t* store = ufi_flux_store(&words);
    const uint32_t max = words>16u?words-16u:0;
    UINT got = 0;
    *n = 0;
    *err = UFI_OK;
    job_source_ticks=0;
    if (geo.kind == IMG_SCP) {
        const uint8_t t = (uint8_t)(cyl * 2u + head);
        if (t < scph.start || t > scph.end || table[t] == 0u) {
            return false;
        }
        uint8_t trk[16];
        uint32_t idx_cells, nw, off;
        const uint32_t size = (uint32_t)f_size(&file);
        if (table[t] < SCP_HDR_LEN + SCP_TABLE_LEN || table[t] > size || size - table[t] < 16u ||
            f_lseek(&file, table[t]) != FR_OK || f_read(&file, trk, 16, &got) != FR_OK || got != 16u ||
            !scp_parse_track(trk, scph.revs, 0, t, &idx_cells, &nw, &off) ||
            off > size - table[t] || nw > (size - table[t] - off) / 2u || nw > max ||
            f_lseek(&file, table[t] + off) != FR_OK) {
            *err = UFI_ERR_NOT_IMPL;
            return false;
        }
        uint32_t acc = 0, prev = 0, pend = 0;
        uint64_t period=((uint64_t)idx_cells*275u+20u)/40u;
        if(period>UINT32_MAX){*err=UFI_ERR_RANGE;return false;}
        job_source_ticks=(uint32_t)period;
        while (nw > 0 && *n < max) {
            const uint32_t chunk = (nw > sizeof(stage) / 2u) ? sizeof(stage) / 2u : nw;
            UFI_WATCHDOG_FEED();
            if (f_read(&file, stage, chunk * 2u, &got) != FR_OK || got != chunk * 2u) {
                *err = UFI_ERR_STORAGE;
                return false;
            }
            if (scp_cells_to_deltas(stage, chunk, store, max, n, &acc, &prev, &pend) == UINT32_MAX) {
                *err = UFI_ERR_BAD_ARGS;
                return false;
            }
            nw -= chunk;
        }
        if (nw || pend || *n < 3u) {
            *err = UFI_ERR_BAD_ARGS;
            return false;
        }
        return true;
    }
    if (geo.kind == IMG_HFE) {
        if (cyl >= hfeh.tracks || (table[cyl] & 0xFFFFu) < 2u) {
            return false;
        }
        const uint32_t side_bytes = (table[cyl] & 0xFFFFu) / 2u;
        if(hfeh.v3){*err=load_hfe_v3(store,max,n);return *err==UFI_OK;}
        const uint32_t cell16 = hfe_fit_cell16(geo.rate_bps, side_bytes * 8u, job_rev, &job_permille);
        job_source_ticks=(uint32_t)(((uint64_t)side_bytes*8u*cell16+32768u)>>16);
        uint32_t left = side_bytes, blk = 0, cells = 0, prev = 0;
        while (left > 0 && *n < max) {
            const uint32_t chunk = left > 256u ? 256u : left;
            UFI_WATCHDOG_FEED();
            if (f_lseek(&file, ((table[cyl] >> 16) + blk) * HFE_BLOCK + (head ? 256u : 0u)) != FR_OK ||
                f_read(&file, stage, chunk, &got) != FR_OK || got != chunk) {
                *err = UFI_ERR_STORAGE;
                return false;
            }
            hfe_bits_to_deltas(stage, chunk, cell16, store, max, n, &cells, &prev);
            left -= chunk;
            blk++;
        }
        if(left || cells!=side_bytes*8u){*err=UFI_ERR_BUFFER_FULL;return false;}
        if(*n<3u){*err=UFI_ERR_BAD_ARGS;return false;}
        return true;
    }
    const uint32_t len = (uint32_t)geo.spt * 512u;
    job_source_ticks=job_rev;
    if (f_lseek(&file, image_track_offset(&geo, cyl, head)) != FR_OK ||
        f_read(&file, stage, len, &got) != FR_OK || got != len) {
        *err = UFI_ERR_STORAGE;
        return false;
    }
    if (geo.kind == IMG_AMIGA) {
        *n = mfm_encode_amiga_track(geo.rate_bps, job_cells, cyl, head, geo.spt, stage, store, max);
    } else {
        const mfm_track_fmt_t f = {.rate_bps = geo.rate_bps, .track_cells = job_cells, .spt = geo.spt,
                                   .gap3 = geo.gap3, .head_id_swap = geo.head_id_swap};
        *n = mfm_encode_track(&f, cyl, head, stage, store, max);
    }
    if (*n == 0u) {
        *err = UFI_ERR_BUFFER_FULL;             /* the sectors do not fit the revolution */
        return false;
    }
    return true;
}

static void job_finish(void)
{
    ufi_drive_motor(false);
    char line[48], *p = line;
    memcpy(p, "written ", 8);
    p = put_num(p + 8, job_written, 1);
    memcpy(p, " tracks, bad ", 13);
    p = put_num(p + 13, job_bad, 1);
    *p++ = '\r'; *p++ = '\n'; *p = 0;
    log_puts(line);
    if (log_open) {
        f_close(&logfile);
        log_open = false;
    }
    if (file_open) {
        f_close(&file);
        file_open = 0;
    }
    unmount();
    led_set(&PIN_LED_FDD, false);
    led_set(&PIN_LED_ACT, true);
    state = D_DONE;
}

int ufi_sdwrite_start(const char* name)
{
    if (state != D_IDLE && state != D_DONE && state != D_ERROR) {
        return UFI_ERR_BUSY;
    }
    if (ufi_usb_msc_active() || ufi_stream_active() ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (!name || !*name) {
        name = write_file;
    }
    if (!*name) {
        return UFI_ERR_NOT_FOUND;               /* no write_file= configured */
    }
    strncpy(job_name, name, sizeof(job_name) - 1u);
    job_name[sizeof(job_name) - 1u] = 0;
    ufi_flux_hard_sectors(0);
    copying = false;
    err_code = 0;
    file_no = 0;
    led_set(&PIN_LED_ERR, false);
    led_set(&PIN_LED_ACT, false);
    state = D_WSTART;
    ufi_cancel_clear();
    return UFI_OK;
}

static void sdwrite_service(void)
{
    if(ufi_cancel_requested()){fail(4);return;}
    switch (state) {
        case D_WSTART: {
            if (mount() != UFI_OK) { fail(1); break; }
            FILINFO fi;
            if (f_stat(job_name, &fi) != FR_OK || f_open(&file, job_name, FA_READ) != FR_OK) { fail(7); break; }
            file_open = 1;
            if (image_identify(job_name, (uint32_t)fi.fsize, &geo) == IMG_UNKNOWN) { fail(5); break; }
            log_open = (f_open(&logfile, "WRITE.LOG", FA_WRITE | FA_CREATE_ALWAYS) == FR_OK);
            log_puts("UFI write from SD: ");
            log_puts(job_name);
            log_puts("\r\n");
            job_written = 0;
            job_bad = 0;
            cyl = 0;
            head = 0;
            if (geo.kind == IMG_CBM && cfg.drive == DRIVE_IEC) {
                ufi_drive_select(DRIVE_IEC);
                if (ufi_iec_command(iec_device, "I0", 2) != UFI_OK) { fail(2); break; }
                if (geo.cbm_kind == 71u && geo.cbm_tracks == 70u &&
                    ufi_iec_command(iec_device, "U0>M1", 5) != UFI_OK) { fail(2); break; }
                log_puts("rejected blocks (track sector dos):\r\n");
                cyl = 1;
                iec_sec = 0;
                state = D_WIEC;
                break;
            }
            if (cfg.drive == DRIVE_IEC || (geo.kind == IMG_CBM && geo.cbm_kind != 81u)) {
                fail(5);                        /* D64/D71 need the IEC drive, flux files a floppy */
                break;
            }
            if (geo.kind == IMG_CBM) {
                geo.kind = IMG_SECTORS;         /* D81 on a PC drive: 1581 MFM layout */
            }
            job_h0 = 0;
            job_h1 = geo.heads;
            if (geo.kind == IMG_SCP) {
                uint8_t hdr[SCP_HDR_LEN];
                UINT got = 0;
                if (f_read(&file, hdr, sizeof(hdr), &got) != FR_OK || got != sizeof(hdr) ||
                    !scp_parse_header(hdr, &scph) || !scp_checksum_ok(hdr) ||
                    f_read(&file, table, sizeof(table), &got) != FR_OK || got != sizeof(table)) {
                    fail(5);
                    break;
                }
                geo.cyls = (uint8_t)(scph.end / 2u + 1u);
                job_h0 = (scph.heads == 2u) ? 1u : 0u;
                job_h1 = (scph.heads == 1u) ? 1u : 2u;
            }
            if (geo.kind == IMG_HFE) {
                UINT got = 0;
                if (f_read(&file, stage, HFE_BLOCK, &got) != FR_OK || got != HFE_BLOCK ||
                    !hfe_parse_header(stage, &hfeh) ||
                    f_lseek(&file, (uint32_t)hfeh.lut_block * HFE_BLOCK) != FR_OK ||
                    f_read(&file, stage, 4u * hfeh.tracks, &got) != FR_OK || got != 4u * hfeh.tracks) {
                    fail(5);
                    break;
                }
                memset(table, 0, sizeof(table));
                for (uint32_t t = 0; t < hfeh.tracks; t++) {
                    uint16_t blk = 0, len = 0;
                    hfe_lut_entry(&stage[4u * t], &blk, &len);
                    table[t] = ((uint32_t)blk << 16) | len;
                }
                bool valid=true;
                uint32_t first=(uint32_t)hfeh.lut_block+1u,size=(uint32_t)f_size(&file);
                for(unsigned t=0;t<hfeh.tracks;t++){
                    uint32_t len=table[t]&65535u,block=table[t]>>16;
                    if(!len)continue;
                    if(len<2 || (len&1) || block<first){valid=false;break;}
                    uint32_t bytes=len/2u;
                    uint64_t end=(uint64_t)block*512u+((bytes-1u)/256u)*512u+
                                 (hfeh.sides-1u)*256u+(bytes-1u)%256u+1u;
                    if(end>size){valid=false;break;}
                }
                if(!valid){fail(5);break;}
                geo.cyls = hfeh.tracks;
                geo.heads = hfeh.sides;
                geo.rate_bps = hfeh.rate_bps;
                geo.double_step = hfeh.double_step;
                job_h1 = hfeh.sides;
                /* All v3 streams must be representable before any physical write. */
                if(hfeh.v3){
                    uint32_t words=0,*store=ufi_flux_store(&words),n=0;
                    for(cyl=0;cyl<hfeh.tracks && valid;cyl++)for(head=0;head<hfeh.sides;head++){
                        if(!(table[cyl]&65535u))continue;
                        if(load_hfe_v3(store,words>16?words-16:0,&n)){valid=false;break;}
                    }
                    cyl=head=0;
                    if(!valid){fail(5);break;}
                }
            }
            drive_timing_t dt = ufi_drive_get_timing();
            dt.double_step = (dstep_cfg == 1u || (dstep_cfg == 2u && geo.double_step)) ? 1u : 0u;
            ufi_drive_set_timing(&dt);
            if (prepare_drive(cfg.drive) != UFI_OK) { fail(2); break; }
            if (ufi_board_write_locked() || ufi_drive_write_protected()) { fail(6); break; }
            if (geo.kind != IMG_SCP && !(geo.kind==IMG_HFE && hfeh.v3)) {
                uint32_t rev = 0;
                if (ufi_format_revolution(&rev) != UFI_OK) { fail(2); break; }
                job_rev = rev;
                if (geo.rate_by_rpm) {          /* 360K: 300 kbit/s in a 360 rpm drive */
                    geo.rate_bps = (rev < FLUX_TIMER_FREQ / 1000u * 180u) ? 300000u : 250000u;
                }
                if (geo.rate_bps == 1000000u) { fail(5); break; }   /* ED: write pulse too long */
                job_cells = (uint32_t)(((uint64_t)rev * 2u * geo.rate_bps / FLUX_TIMER_FREQ) * 98u / 100u);
            }
            head = job_h0;
            log_puts("track  result\r\n");
            state = D_WTRACK;
            break;
        }

        case D_WTRACK: {
            ufi_board_activity();
            uint32_t n = 0;
            int err = UFI_OK;
            if (job_load_track(&n, &err)) {
                led_set(&PIN_LED_FDD, true);
                job_permille = (geo.kind == IMG_HFE) ? job_permille : 1000u;
                const bool v3=geo.kind==IMG_HFE && hfeh.v3;
                uint8_t level=0;
                const bool sector_image=geo.kind!=IMG_SCP && geo.kind!=IMG_HFE;
                const uint64_t expected=sector_image?((1ull<<(geo.spt+1u))-2u):0;
                const int ret=ufi_write_local_checked(cyl,head,n,v3,job_source_ticks,
                    expected,(uint8_t)(head^(geo.head_id_swap?1u:0u)),&level);
                led_set(&PIN_LED_FDD, false);
                if(ret==UFI_ERR_CANCELLED){fail(4);break;}
                if(ret==UFI_ERR_MEDIA_CHANGED){fail(2);break;}
                if (ret == UFI_ERR_WRITE_PROT) { fail(6); break; }
                if (ret != UFI_OK) {
                    job_bad++;
                    job_log(ret == UFI_ERR_RANGE ? "write error (period out of range)"
                          : ret == UFI_ERR_TRACK_LONG ? "write error (track longer than a revolution)"
                          : ret == UFI_ERR_NO_INDEX ? "write error (no index)" : "write error");
                } else {
                    job_written++;
                    job_log(level==3?(sector_image?"source content verified":"decoded source sectors verified"):
                                     "pulse-count only; content UNVERIFIED");
                }
            } else if (err == UFI_ERR_STORAGE) {
                fail(ufi_cancel_requested()?4:1);
                break;
            } else if (err != UFI_OK) {
                job_bad++;
                job_log(err == UFI_ERR_BUFFER_FULL ? "sectors do not fit the revolution" : "track header unreadable");
            } else {
                job_log("not in the file");
            }
            g_capture.state = CAPTURE_IDLE;
            HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
            if (++head >= job_h1) {
                head = job_h0;
                cyl++;
            }
            if (cyl >= geo.cyls) {
                job_finish();
            }
            break;
        }

        case D_WIEC: {                          /* one block per call */
            ufi_board_activity();
            led_set(&PIN_LED_FDD, true);
            uint8_t block[256], dos = 255;
            UINT got = 0;
            if (f_read(&file, block, sizeof(block), &got) != FR_OK || got != sizeof(block)) { fail(5); break; }
            const int ret = ufi_iec_write_block(iec_device, cyl, iec_sec, block, &dos);
            if (ret == UFI_OK && dos == 26u) { fail(6); break; }
            if (ret != UFI_OK || (dos != 0u && dos != 1u)) {
                job_bad++;
                char line[20], *p = line;
                p = put_num(p, cyl, 2);
                *p++ = ' ';
                p = put_num(p, iec_sec, 2);
                *p++ = ' ';
                p = put_num(p, (ret == UFI_OK) ? dos : 99u, 2);
                *p++ = '\r'; *p++ = '\n'; *p = 0;
                log_puts(line);
            }
            if (++iec_sec >= image_cbm_spt(geo.cbm_kind, cyl)) {
                iec_sec = 0;
                cyl++;
                job_written++;
                led_set(&PIN_LED_FDD, false);
                HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
            }
            if (cyl > geo.cbm_tracks) {
                job_finish();
            }
            break;
        }

        default:
            break;
    }
}

static bool word_is(const char* p, const char* w)
{
    while (*w) {
        char ch = *p++;
        if (ch >= 'A' && ch <= 'Z') {
            ch = (char)(ch - 'A' + 'a');
        }
        if (ch != *w++) {
            return false;
        }
    }
    return *p == '\r' || *p == '\n' || *p == ' ' || *p == '\t' || *p == '\0' || *p == '#';
}

static void parse_line(const char* k, dump_config_t* c)
{
    const char* v = k;
    while (*v && *v != '=' && *v != '\n') {
        v++;
    }
    if (*v != '=') {
        return;
    }
    v++;
    uint32_t num = 0;
    for (const char* d = v; *d >= '0' && *d <= '9'; d++) {
        num = num * 10u + (uint32_t)(*d - '0');
    }
    if (!strncmp(k, "drive=", 6)) {
        for (uint32_t i = 0; i < sizeof(drive_names) / sizeof(drive_names[0]); i++) {
            if (word_is(v, drive_names[i].name)) {
                c->drive = drive_names[i].type;
            }
        }
    } else if (!strncmp(k, "tracks=", 7)) {
        c->tracks = (uint8_t)num;
    } else if (!strncmp(k, "sides=", 6)) {
        c->sides = (uint8_t)num;
    } else if (!strncmp(k, "revs=", 5)) {
        c->revs = (uint8_t)num;
    } else if (!strncmp(k, "button_a=", 9)) {
        button_copy = word_is(v, "copy");
        button_write = word_is(v, "write");
    } else if (!strncmp(k, "write_file=", 11)) {
        uint32_t i = 0;
        for (; i < sizeof(write_file) - 1u && v[i] > ' ' && v[i] != '#'; i++) {
            write_file[i] = (v[i] >= 'a' && v[i] <= 'z') ? (char)(v[i] - 'a' + 'A') : v[i];
        }
        write_file[i] = 0;
    } else if (!strncmp(k, "protocol=", 9)) {
        protocol_gw = word_is(v, "gw");
        protocol_composite = word_is(v, "composite");
    } else if (!strncmp(k, "apple_sync=", 11)) {
        apple_sync = (num == 1u);
    } else if (!strncmp(k, "index_sim=", 10)) {
        index_sim = (uint16_t)num;
    } else if (!strncmp(k, "index_sim_mode=", 15)) {
        index_sim_mode = word_is(v, "internal") ? INDEX_SIM_INTERNAL
                       : word_is(v, "both") ? (INDEX_SIM_PIN | INDEX_SIM_INTERNAL) : INDEX_SIM_PIN;
    } else if (!strncmp(k, "rpm_line=", 9)) {
        rpm_line = word_is(v, "density") ? 1u : word_is(v, "drate") ? 2u : 0u;
    } else if (!strncmp(k, "rpm_360=", 8)) {
        rpm_360_low = !word_is(v, "high");
    } else if (!strncmp(k, "rpm=", 4)) {
        rpm_start = (num == 300u || num == 360u) ? (uint16_t)num : 0u;
    } else if (!strncmp(k, "tg43=", 5)) {
        cfg_tg43 = (num == 1u);
    } else if (!strncmp(k, "iec_device=", 11)) {
        iec_device = (num >= 4u && num <= 30u) ? (uint8_t)num : 8u;
    } else if (!strncmp(k, "iec_type=", 9)) {
        iec_type = num == 1541u ? 41u : num == 1571u ? 71u : num == 1581u ? 81u : 0u;
    } else if (!strncmp(k, "track_search=", 13)) {
        track_search = (num == 1u);
    } else if (!strncmp(k, "double_step=", 12)) {
        dstep_cfg = word_is(v, "auto") ? 2u : (num == 1u) ? 1u : 0u;
    } else if (!strncmp(k, "auto_dump=", 10)) {
        auto_dump = (num == 1u);
    } else if (!strncmp(k, "format=", 7)) {
        dump_fmt_cfg = word_is(v, "img") ? 1u : word_is(v, "adf") ? 2u : word_is(v, "hfe") ? 3u : word_is(v,"hfe3")?4u:0u;
    } else if (!strncmp(k, "copy_from=", 10) || !strncmp(k, "copy_to=", 8)) {
        for (uint32_t i = 0; i < sizeof(drive_names) / sizeof(drive_names[0]); i++) {
            if (word_is(v, drive_names[i].name)) {
                *(k[5] == 'f' ? &copy_src : &copy_dst) = drive_names[i].type;
            }
        }
    }
}

void ufi_config_load(void)
{
    if (mount() != UFI_OK) {
        return;
    }
    UINT n = 0;
    if (f_open(&file, "UFI.CFG", FA_READ) == FR_OK) {
        f_read(&file, stage, 1023, &n);
        f_close(&file);
        stage[n] = 0;
        dump_config_t c = cfg;
        index_sim = 0;
        index_sim_mode = INDEX_SIM_PIN;
        dstep_cfg = 0;
        auto_dump = false;
        ad_state = 0;
        rpm_line = 0;
        rpm_360_low = true;
        rpm_start = 0;
        cfg_tg43 = false;
        dump_fmt_cfg = 0;
        button_write = false;
        write_file[0] = 0;
        for (char* p = (char*)stage; *p; ) {
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p != '#') {
                parse_line(p, &c);
            }
            while (*p && *p != '\n') {
                p++;
            }
            if (*p) {
                p++;
            }
        }
        if (config_valid(&c)) {
            cfg = c;
        }
    } else if (f_open(&file, "UFI.CFG", FA_WRITE | FA_CREATE_NEW) == FR_OK) {
        f_write(&file, cfg_default, sizeof(cfg_default) - 1u, &n);
        f_close(&file);
    }
    unmount();
    ufi_drive_profiles_load();                          /* UFIPROF.BIN, mounts on its own (1.18) */
    ufi_index_sim_set(index_sim, 0, index_sim_mode);    /* pin mode: refused with the switch on SD */
    ufi_drive_rpm_config(rpm_line, rpm_360_low);
    if (rpm_start) {
        ufi_drive_set_rpm(rpm_start);
    }
    drive_timing_t dt = ufi_drive_get_timing();
    dt.tg43 = cfg_tg43 ? 1u : 0u;
    ufi_drive_set_timing(&dt);
}

int ufi_dump_start(const dump_config_t* c)
{
    return ufi_dump_start_fmt(c, dump_fmt_cfg);
}

int ufi_dump_start_fmt(const dump_config_t* c, uint8_t fmt)
{
    if (ufi_usb_get_mode()==UFI_USB_COMPOSITE && !ufi_resources_acquire(UFI_OWNER_NATIVE))
        return UFI_ERR_BUSY;
    if (fmt > 4u) {
        return UFI_ERR_BAD_ARGS;
    }
    if (state != D_IDLE && state != D_DONE && state != D_ERROR) {
        return UFI_ERR_BUSY;
    }
    ufi_flux_hard_sectors(0);               /* dumps and copies are soft-sectored */
    if (ufi_usb_msc_active() || ufi_stream_active() ||
        g_capture.state == CAPTURE_WAITING_INDEX || g_capture.state == CAPTURE_RUNNING) {
        return UFI_ERR_BUSY;
    }
    if (c) {
        if (!config_valid(c)) {
            return UFI_ERR_NOT_IMPL;
        }
        cfg = *c;
    }
    dump_fmt = fmt;
    err_code = 0;
    file_no = 0;
    led_set(&PIN_LED_ERR, false);
    led_set(&PIN_LED_ACT, false);
    state = D_START;
    return UFI_OK;
}

void ufi_dump_abort(void)
{
    if (ufi_dump_active()) {
        fail(4);
    }
}

bool ufi_dump_active(void)
{
    return state != D_IDLE && state != D_DONE && state != D_ERROR;
}

dump_status_t ufi_dump_status(void)
{
    return (dump_status_t){.state = (uint8_t)state, .error = err_code, .track = cyl, .side = head,
                           .file_no = file_no, .fs_result = (uint8_t)fs_result,
                           .storage_ready = ufi_sd_present()};
}

void ufi_dump_service(void)
{
    switch (state) {
        case D_START:
            if (copying) {                             /* copy: no files, both drives ready */
                if (prepare_drive(copy_dst) != UFI_OK || prepare_drive(copy_src) != UFI_OK) {
                    fail(2);
                    break;
                }
            } else if (cfg.drive == DRIVE_IEC) {
                if (mount() != UFI_OK) { fail(1); break; }
                ufi_drive_select(DRIVE_IEC);
                /* the log is numbered with the image: find the number first */
                file_no = 0;
                log_open = false;
                iec_kind = iec_detect();
                file_ext = iec_kind == 81u ? ".D81"
                         : (iec_kind == 71u && cfg.tracks != 35u && cfg.tracks != 40u) ? ".D71" : ".D64";
                if (open_next_file() != UFI_OK) { fail(1); break; }
                file_ext = ".SCP";
                checksum = 0;
                stage_len = 0;
                open_log();
                log_puts(iec_kind == 81u ? "UFI 1581 dump via IEC (U1 block reads)\r\n"
                       : iec_kind == 71u ? "UFI 1571 dump via IEC (U1 block reads)\r\n"
                                         : "UFI 1541 dump via IEC (U1 block reads)\r\n");
                if (iec_msg[0]) {
                    log_puts("drive: ");
                    log_puts(iec_msg);
                    log_puts("\r\n");
                }
                log_puts("unreadable blocks (track sector):\r\n");
                if (ufi_iec_command(iec_device, "I0", 2) != UFI_OK) { fail(2); break; }
                if (iec_kind == 71u && cfg.tracks != 35u && cfg.tracks != 40u &&
                    ufi_iec_command(iec_device, "U0>M1", 5) != UFI_OK) {   /* double-sided mode */
                    fail(2);
                    break;
                }
                iec_tracks = iec_kind == 81u ? 80u
                           : iec_kind == 71u ? ((cfg.tracks == 35u || cfg.tracks == 40u) ? cfg.tracks : 70u)
                           : (cfg.tracks == 40u) ? 40u : 35u;
                iec_sec = 0;
                iec_bad = 0;
                cyl = 1;                                /* 1541 tracks are 1-based */
                head = 0;
                state = D_IEC;
                break;
            } else {
                if (mount() != UFI_OK) { fail(1); break; }
                file_ext = dump_fmt == 1u ? ".IMG" : dump_fmt == 2u ? ".ADF" : dump_fmt >= 3u ? ".HFE" : ".SCP";
                const int fr = open_next_file();
                file_ext = ".SCP";
                if (fr != UFI_OK) { fail(1); break; }
                if (dump_fmt) {
                    checksum = 0;
                    stage_len = 0;
                    if (dump_fmt >= 3u) {           /* blocks 0/1: header + table, filled at the end */
                        UINT bw = 0;
                        memset(stage, 0xFF, 2u * HFE_BLOCK);
                        if (f_write(&file, stage, 2u * HFE_BLOCK, &bw) != FR_OK || bw != 2u * HFE_BLOCK) { fail(1); break; }
                        memset(table, 0, sizeof(table));
                    }
                } else if (begin_file() != UFI_OK) { fail(1); break; }
                open_log();
                log_puts("UFI dump quality report\r\ntrack  good/expected  rpm  reads\r\n");
                drive_timing_t dt = ufi_drive_get_timing();
                dt.double_step = (dstep_cfg == 1u) ? 1u : 0u;
                ufi_drive_set_timing(&dt);
                dstep_pending = (dstep_cfg == 2u) &&
                                cfg.drive != DRIVE_APPLE_II && cfg.drive != DRIVE_APPLE2;
                tracks_auto = (cfg.tracks == 0);
                if (prepare_drive(cfg.drive) != UFI_OK) { fail(2); break; }
                if (cfg.tracks == 0) {                      /* tracks=0: highest readable + 1 */
                    uint8_t hi = 79;
                    if (ufi_drive_probe_tracks(&hi) != UFI_OK) { fail(2); break; }
                    cfg.tracks = (uint8_t)(hi + 1u > 84u ? 84u : hi + 1u);
                    ufi_drive_recalibrate();
                }
            }
            if (cfg.tracks == 0) {
                cfg.tracks = cfg.drive == DRIVE_IEC ? 35u : 80u;
            }
            cyl = 0;
            head = 0;
            tries = 0;
            q_known = false;
            bad_tracks = 0;
            rpm_sum = 0;
            rpm_n = 0;
            state = D_CAPTURE;
            break;

        case D_CAPTURE:
            ufi_board_activity();
            if (copying) {
                ufi_drive_select((drive_type_t)copy_src);
            }
            if (ufi_capture_start(cyl, head, copying ? 2 : cfg.revs, 0) != UFI_OK) { fail(2); break; }
            state = D_WAIT;
            break;

        case D_WAIT:
            switch (ufi_capture_get_state()) {
                case CAPTURE_COMPLETE: {
                    /* quality: decode the track (format probed on 0.0); retry weak tracks */
                    uint8_t good = track_quality();
                    if (q_known && good < q_spt && tries == 0 && track_search && !copying &&
                        ufi_drive_get_timing().double_step && !ufi_drive_is_apple()) {
                        good = search_track(good);          /* +-1 physical track */
                    }
                    if (q_known && good < q_spt && tries < 2u) {
                        tries++;
                        g_capture.state = CAPTURE_IDLE;
                        state = D_CAPTURE;
                        break;
                    }
                    if (q_known && good < q_spt) {
                        bad_tracks++;
                    }
                    log_track(good);
                    tries = 0;
                    state = copying ? D_COPY : D_WRITE;
                    break;
                }
                case CAPTURE_ERROR:    fail(2); break;
                default:               break;
            }
            break;

        case D_COPY: {
            led_set(&PIN_LED_FDD, false);
            const int ret = copy_track();
            g_capture.state = CAPTURE_IDLE;
            if (ret != UFI_OK) { fail(ret == UFI_ERR_WRITE_PROT ? 3 : 2); break; }
            HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
            if (++head >= cfg.sides) {
                head = 0;
                cyl++;
            }
            if (cyl >= cfg.tracks) {
                ufi_drive_select((drive_type_t)copy_dst);
                ufi_drive_motor(false);
                ufi_drive_select((drive_type_t)copy_src);
                ufi_drive_motor(false);
                led_set(&PIN_LED_ACT, true);
                copying = false;
                state = D_DONE;
            } else {
                state = D_CAPTURE;
            }
            break;
        }

        case D_WRITE: {
            led_set(&PIN_LED_FDD, false);
            const int ret = dump_fmt == 4u ? write_hfe_v3_track() : dump_fmt == 3u ? write_hfe_track()
                          : dump_fmt ? write_image_track() : write_track((uint8_t)(cyl * 2u + head));
            g_capture.state = CAPTURE_IDLE;
            if (ret != UFI_OK) { fail(ret == UFI_ERR_NO_INDEX ? 2 : ret == UFI_ERR_NOT_IMPL ? 5 : 3); break; }
            HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
            if (dstep_pending && cyl == 0 && head == 0) {
                dstep_pending = false;
                if (q_known && probe_double_step()) {
                    drive_timing_t dt = ufi_drive_get_timing();
                    dt.double_step = 1;
                    ufi_drive_set_timing(&dt);
                    ufi_drive_recalibrate();
                    if (cfg.tracks > 42u) {
                        cfg.tracks = tracks_auto ? 40u : 42u;
                    }
                    log_puts("double step: on (40-track disk)\r\n");
                }
            }
            if (++head >= cfg.sides) {
                head = 0;
                cyl++;
            }
            state = (cyl >= cfg.tracks) ? D_FINISH : D_CAPTURE;
            break;
        }

        case D_FINISH:
            ufi_drive_motor(false);
            close_log();
            if (dump_fmt >= 3u) {
                if (finish_hfe() != UFI_OK) { fail(3); break; }
            } else if (dump_fmt) {
                if (f_close(&file) != FR_OK) { fail(3); break; }
                file_open = 0;
            } else if (finish_file() != UFI_OK) { fail(3); break; }
            if(publish_capture()!=UFI_OK){fail(3);break;}
            write_json();
            unmount();
            led_set(&PIN_LED_ACT, true);
            state = D_DONE;
            break;

        case D_IEC: {                                   /* one 256-byte block per call */
            ufi_board_activity();
            led_set(&PIN_LED_FDD, true);
            uint8_t block[256];
            uint16_t got = 0;
            if (ufi_iec_read_block(iec_device, cyl, iec_sec, block, &got) != UFI_OK || got != 256u) {
                memset(block, 0, sizeof(block));        /* unreadable: zeros, noted in the log */
                iec_bad++;
                char line[16], *p = line;
                p = put_num(p, cyl, 2);
                *p++ = ' ';
                p = put_num(p, iec_sec, 2);
                *p++ = '\r'; *p++ = '\n'; *p = 0;
                log_puts(line);
            }
            if (put(block, sizeof(block)) != UFI_OK) { fail(3); break; }
            if (++iec_sec >= iec_spt(cyl)) {
                iec_sec = 0;
                cyl++;
                led_set(&PIN_LED_FDD, false);
                HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
            }
            if (cyl > iec_tracks) {
                char line[32], *p = line;
                memcpy(p, "unreadable blocks: ", 19); p += 19;
                p = put_num(p, iec_bad, 1);
                *p++ = '\r'; *p++ = '\n'; *p = 0;
                log_puts(line);
                if (log_open) { f_close(&logfile); log_open = false; }
                if (flush_stage() != UFI_OK || f_close(&file) != FR_OK) { fail(3); break; }
                file_open = 0;
                if(publish_capture()!=UFI_OK){fail(3);break;}
                write_json();
                unmount();
                led_set(&PIN_LED_FDD, false);
                led_set(&PIN_LED_ACT, true);
                state = D_DONE;
            }
            break;
        }

        case D_WSTART:
        case D_WTRACK:
        case D_WIEC:
            sdwrite_service();
            break;

        default:                                    /* idle, done, error */
            auto_dump_service();
            break;
    }
}

/* ============================================================================
 * buttons (runtime) and error blink code
 * ============================================================================ */

void ufi_buttons_service(void)
{
    static uint32_t a_since, b_since, blink_t;
    static bool armed, a_used, b_used, a_down;
    static uint8_t blink_n;
    const uint32_t now = HAL_GetTick();
    const bool a = HAL_GPIO_ReadPin(PIN_BTN_A.port, PIN_BTN_A.pin) == GPIO_PIN_RESET;
    const bool b = HAL_GPIO_ReadPin(PIN_BTN_B.port, PIN_BTN_B.pin) == GPIO_PIN_RESET;

    if (!armed) {                                      /* start-up buttons: wait for release */
        armed = !a && !b;
        return;
    }
    if (!a) {
        if (a_down && !a_used) {                       /* short press: UFI v2 event only */
            const uint8_t ev[2] = {0, 0};
            ufi_v2_event(UFI_V2_EVT_BUTTON, ev, 2);
        }
        a_since = now;
        a_used = false;
    }
    a_down = a;
    if (!b) {
        if (b_since && !b_used) {
            const uint8_t ev[2] = {1, 0};
            ufi_v2_event(UFI_V2_EVT_BUTTON, ev, 2);
            if (ufi_dump_active()) {                   /* short press of B aborts */
                ufi_dump_abort();
            }
        }
        b_since = 0;
        b_used = false;
    } else if (!b_since) {
        b_since = now;
    }

    if (a && !a_used && now - a_since >= HOLD_START_MS) {
        a_used = true;
        const uint8_t ev[2] = {0, 1};
        ufi_v2_event(UFI_V2_EVT_BUTTON, ev, 2);
        if (button_write) {                            /* UFI.CFG button_a=write */
            ufi_sdwrite_start(NULL);
        } else if (button_copy) {                      /* UFI.CFG button_a=copy */
            ufi_copy_start();
        } else {
            ufi_dump_start(NULL);                      /* busy / mass storage mode: ignored */
        }
    }
    if (b && b_since && !b_used && !ufi_dump_active() && now - b_since >= HOLD_MSC_MS) {
        b_used = true;
        const uint8_t ev[2] = {1, 1};
        ufi_v2_event(UFI_V2_EVT_BUTTON, ev, 2);
        ufi_mode_button_b();                          /* SD drive on/off (switch in middle) */
    }

    /* ERR LED: n short blinks, 1 s pause, until the next dump starts */
    if (state == D_ERROR && err_code) {
        if (now - blink_t >= (blink_n ? 250u : 1000u)) {
            blink_t = now;
            if (blink_n == 0) {
                blink_n = (uint8_t)(err_code * 2u);
            }
            blink_n--;
            led_set(&PIN_LED_ERR, (blink_n & 1u) != 0);
        }
    }
}

/* ============================================================================
 * file access for the host (UFI v2 DIR / READ_FILE / WRITE_FILE / DELETE)
 * Each call mounts, works and unmounts; a running dump owns the file system.
 * ============================================================================ */

static int fr_status(FRESULT fr)
{
    switch (fr) {
        case FR_OK:             return UFI_OK;
        case FR_NO_FILE:
        case FR_NO_PATH:        return UFI_ERR_NOT_FOUND;
        case FR_INVALID_NAME:   return UFI_ERR_BAD_ARGS;
        case FR_WRITE_PROTECTED: return UFI_ERR_WRITE_PROT;
        default:                return UFI_ERR_STORAGE;
    }
}

static int file_begin(void)
{
    if (ufi_dump_active()) {
        return UFI_ERR_BUSY;
    }
    return mount();
}

/* out: count u8, more u8, per entry size u32, attr u8, nlen u8, name */
int ufi_file_dir(const char* path, uint16_t start, uint8_t* out, uint16_t max, uint16_t* len)
{
    *len = 0;
    int ret = file_begin();
    if (ret != UFI_OK) {
        return ret;
    }
    DIR dir;
    FILINFO fi;
    FRESULT fr = f_opendir(&dir, path);
    uint16_t n = 2, idx = 0;
    uint8_t count = 0, more = 0;
    if (fr == FR_OK) {
        while ((fr = f_readdir(&dir, &fi)) == FR_OK && fi.fname[0]) {
            if (idx++ < start) {
                continue;
            }
            const uint8_t nl = (uint8_t)strlen(fi.fname);
            if (count == 255u || n + 6u + nl > max) {
                more = 1;
                break;
            }
            const uint32_t size = (uint32_t)fi.fsize;
            memcpy(&out[n], &size, 4);
            out[n + 4] = fi.fattrib;
            out[n + 5] = nl;
            memcpy(&out[n + 6], fi.fname, nl);
            n = (uint16_t)(n + 6u + nl);
            count++;
        }
        f_closedir(&dir);
    }
    unmount();
    out[0] = count;
    out[1] = more;
    *len = (fr == FR_OK) ? n : 0u;
    return fr_status(fr);
}

/* Fewer bytes than asked (also 0) = end of file */
int ufi_file_read(const char* name, uint32_t offset, uint8_t* out, uint16_t len, uint16_t* got)
{
    *got = 0;
    int ret = file_begin();
    if (ret != UFI_OK) {
        return ret;
    }
    FRESULT fr = f_open(&file, name, FA_READ);
    if (fr == FR_OK) {
        file_open = 1;
        UINT n = 0;
        fr = f_lseek(&file, offset);
        if (fr == FR_OK) {
            fr = f_read(&file, out, len, &n);
        }
        *got = (uint16_t)n;
    }
    unmount();
    return fr_status(fr);
}

/* create: new file / truncate to 0 first; a short write (volume full) = UFI_ERR_STORAGE */
int ufi_file_write(const char* name, uint32_t offset, bool create, const uint8_t* data,
                   uint16_t len, uint16_t* written)
{
    *written = 0;
    int ret = file_begin();
    if (ret != UFI_OK) {
        return ret;
    }
    FRESULT fr = f_open(&file, name, FA_WRITE | (create ? FA_CREATE_ALWAYS : FA_OPEN_EXISTING));
    UINT n = 0;
    if (fr == FR_OK) {
        file_open = 1;
        fr = f_lseek(&file, offset);
        if (fr == FR_OK && len) {
            fr = f_write(&file, data, len, &n);
        }
        if (fr == FR_OK) {
            fr = f_close(&file);
            file_open = 0;
        }
        *written = (uint16_t)n;
    }
    unmount();
    ret = fr_status(fr);
    return (ret == UFI_OK && n < len) ? UFI_ERR_STORAGE : ret;
}

int ufi_file_delete(const char* name)
{
    int ret = file_begin();
    if (ret != UFI_OK) {
        return ret;
    }
    const FRESULT fr = f_unlink(name);
    unmount();
    return fr_status(fr);
}
