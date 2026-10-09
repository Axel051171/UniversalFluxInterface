/**
 * UFI Flux Engine - stand-alone disk dump to the SD NAND (board v0.6)
 *
 * Button A held >= 1 s (or DUMP_START from the host) reads the whole disk and writes
 * DUMPnnnn.SCP (first free number) to the FAT32 volume on the SD NAND.  Button B
 * aborts.  The file is byte-identical to `ufi read-disk` (software/ufi_host scp.py):
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
#include "ff.h"
#include "ufi_mfm.h"
#include <string.h>

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

typedef enum { D_IDLE, D_START, D_CAPTURE, D_WAIT, D_WRITE, D_FINISH, D_DONE, D_ERROR, D_COPY } dump_state_t;

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
    if (fr == FR_NO_FILESYSTEM) {                     /* blank chip: format once */
        fr = f_mkfs("", FM_FAT32, 0, stage, sizeof(stage));
        if (fr == FR_OK) {
            fr = f_mount(&fs, "", 1);
        }
        if (fr == FR_OK) {
            f_setlabel("UFI");
        }
    }
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
        memcpy(&name[8], ".SCP", 5);
        if (f_stat(name, &fi) == FR_NO_FILE) {
            if (f_open(&file, name, FA_WRITE | FA_CREATE_NEW) != FR_OK) {
                return UFI_ERR_STORAGE;
            }
            file_open = 1;
            file_no = n;
            return UFI_OK;
        }
    }
    return UFI_ERR_STORAGE;
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
                                (apple && !ufi_config_apple_sync()) ? 0u : SCP_FLAG_INDEX, 0, (uint8_t)(cfg.sides == 2 ? 0 : 1), 0,
                                (uint8_t)checksum, (uint8_t)(checksum >> 8),
                                (uint8_t)(checksum >> 16), (uint8_t)(checksum >> 24)};
    UINT bw = 0;
    if (f_lseek(&file, 0) != FR_OK || f_write(&file, hdr, sizeof(hdr), &bw) != FR_OK ||
        f_write(&file, table, sizeof(table), &bw) != FR_OK || f_close(&file) != FR_OK) {
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
static bool log_open, copying, button_copy, protocol_gw, apple_sync;
static uint16_t index_sim;              /* UFI.CFG index_sim=300|360 (J9 pin 6), 0 = off */
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
} qctx_t;

static void q_sector(void* p, const mfm_id_t* id, const uint8_t* data, bool crc_ok)
{
    (void)data;
    qctx_t* q = p;
    if (crc_ok && id->r >= 1u && id->r <= 63u && id->c == q->cyl) {
        q->good |= 1ull << (id->r - 1u);
        if (id->r > q->max_r) {
            q->max_r = id->r;
        }
    }
}

/* good sectors of the captured track (all revolutions as one stream) */
static uint8_t count_good(uint32_t rate, bool amiga, uint8_t* max_r)
{
    static mfm_decoder_t d;
    qctx_t q = {.cyl = cyl};
    if (amiga) {
        mfm_dec_init_amiga(&d, rate, q_sector, &q);
    } else {
        mfm_dec_init(&d, rate, q_sector, &q);
    }
    uint32_t carry = 0;
    for (uint8_t k = 0; k < ufi_flux_get_revolution_count(); k++) {
        const flux_revolution_t* r = ufi_flux_get_revolution(k);
        uint32_t prev = 0;
        for (uint32_t i = 0; i < r->count; i++) {
            mfm_dec_interval(&d, r->samples[i].timestamp - prev + carry);
            carry = 0;
            prev = r->samples[i].timestamp;
        }
        carry = r->index_time - prev;
    }
    *max_r = q.max_r;
    uint8_t n = 0;
    for (uint64_t g = q.good; g; g &= g - 1u) {
        n++;
    }
    return n;
}

/* on track 0.0 probe IBM 500k/250k/300k/1M and Amiga; afterwards decode that format */
static uint8_t track_quality(void)
{
    static const uint32_t rates[] = {500000u, 250000u, 300000u, 1000000u, 250000u};
    uint8_t max_r = 0;
    if (cyl == 0 && head == 0 && tries == 0) {
        uint8_t best = 0;
        for (uint32_t i = 0; i < 5; i++) {
            const uint8_t g = count_good(rates[i], i == 4u, &max_r);
            if (g > best) {
                best = g;
                q_rate = rates[i];
                q_amiga = (i == 4u);
                q_spt = max_r;
            }
        }
        q_known = best >= 4u;
        return best;
    }
    return q_known ? count_good(q_rate, q_amiga, &max_r) : 0u;
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
    log_puts("UFI dump quality report\r\ntrack  good/expected  rpm  reads\r\n");
}

static void log_track(uint8_t good)
{
    if (!log_open) {
        return;
    }
    char line[64], *p = line;
    const flux_revolution_t* r = ufi_flux_get_revolution(0);
    const uint32_t rpm10 = r->index_time ? (uint32_t)(600ull * FLUX_TIMER_FREQ / r->index_time) : 0u;
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

/* copy: revolution 0 of the source (index to index) is written to the destination,
 * then the destination is read back: same good-sector count (known format) or a flux
 * count within 5 % */
static int copy_track(void)
{
    const flux_revolution_t* r = ufi_flux_get_revolution(0);
    const uint32_t n = r->count;
    if (n < 3u) {
        return UFI_ERR_NO_INDEX;
    }
    uint8_t max_r = 0;
    const uint8_t src_good = q_known ? count_good(q_rate, q_amiga, &max_r) : 0u;
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
    int ret = ufi_write_local(cyl, head, n);
    if (ret == UFI_OK) {
        ret = ufi_capture_start(cyl, head, 2, 0);
        const uint32_t t0 = HAL_GetTick();
        while (ret == UFI_OK) {
            const capture_state_t s = ufi_capture_get_state();
            if (s == CAPTURE_COMPLETE) {
                break;
            }
            if (s == CAPTURE_ERROR || HAL_GetTick() - t0 > 2000u) {
                ufi_capture_abort();
                ret = UFI_ERR_NO_INDEX;
            }
        }
    }
    if (ret == UFI_OK) {
        const uint32_t m = ufi_flux_get_revolution(0)->count;
        const uint32_t diff = (m > n) ? m - n : n - m;
        if (q_known ? count_good(q_rate, q_amiga, &max_r) < src_good : diff > n / 20u) {
            ret = UFI_ERR_DMA;                         /* verify failed */
        }
    }
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
           (d >= DRIVE_SHUGART_DS0 && d <= DRIVE_SHUGART_DS3);
}

static bool config_valid(const dump_config_t* c)
{
    return drive_ok(c->drive) && c->tracks >= 1 && c->tracks <= 84 && c->sides >= 1 &&
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

bool ufi_config_apple_sync(void)
{
    return apple_sync;
}

uint16_t ufi_config_index_sim(void)
{
    return index_sim;
}

/* ============================================================================
 * UFI.CFG on the SD NAND: drive and dump settings for stand-alone use, editable in the
 * SD drive mode.  Lines "key=value", '#' comments; written with defaults if missing.
 * ============================================================================ */

static const char cfg_default[] =
    "# UFI stand-alone settings (dump with button A, USB floppy mode)\r\n"
    "# drive: a, b (PC cable), amiga, amiga2 (DF2, needs JP3), ds0-ds3 (Shugart bus; ds3 needs JP1),\r\n"
    "#        apple, apple2 (Disk II port, board v0.7: use tracks=35 sides=1)\r\n"
    "drive=a\r\n"
    "tracks=80\r\n"
    "sides=2\r\n"
    "revs=3\r\n"
    "# button A: dump (disk -> DUMPnnnn.SCP + .LOG) or copy (copy_from -> copy_to)\r\n"
    "button_a=dump\r\n"
    "copy_from=a\r\n"
    "copy_to=b\r\n"
    "# flux protocol with the mode switch in the middle: ufi (ufi host tool) or gw\r\n"
    "# (Greaseweazle host tools: gw read, gw write, ...)\r\n"
    "protocol=ufi\r\n"
    "# Disk II sync (index) sensor on J19: 1 = Apple reads/writes use its index pulse\r\n"
    "apple_sync=0\r\n"
    "# index simulation on J9 pin 6 (open drain, wire to a drive's index sensor output for\r\n"
    "# flippy disks): 0 = off, 300 or 360 rpm; not with the mode switch on J13\r\n"
    "index_sim=0\r\n";

static const struct { const char* name; uint8_t type; } drive_names[] = {
    {"a", DRIVE_SHUGART_A}, {"b", DRIVE_SHUGART_B}, {"amiga", DRIVE_AMIGA},
    {"ds0", DRIVE_SHUGART_DS0}, {"ds1", DRIVE_SHUGART_DS1},
    {"ds2", DRIVE_SHUGART_DS2}, {"ds3", DRIVE_SHUGART_DS3}, {"amiga2", DRIVE_AMIGA2},
    {"apple", DRIVE_APPLE_II}, {"apple2", DRIVE_APPLE2},
};

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
    } else if (!strncmp(k, "protocol=", 9)) {
        protocol_gw = word_is(v, "gw");
    } else if (!strncmp(k, "apple_sync=", 11)) {
        apple_sync = (num == 1u);
    } else if (!strncmp(k, "index_sim=", 10)) {
        index_sim = (uint16_t)num;
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
    ufi_index_sim_set(index_sim, 0);    /* refused (stays off) with the switch on SD */
}

int ufi_dump_start(const dump_config_t* c)
{
    if (state != D_IDLE && state != D_DONE && state != D_ERROR) {
        return UFI_ERR_BUSY;
    }
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
            } else {
                if (mount() != UFI_OK) { fail(1); break; }
                if (open_next_file() != UFI_OK || begin_file() != UFI_OK) { fail(1); break; }
                open_log();
                if (prepare_drive(cfg.drive) != UFI_OK) { fail(2); break; }
            }
            cyl = 0;
            head = 0;
            tries = 0;
            q_known = false;
            bad_tracks = 0;
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
                    const uint8_t good = track_quality();
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
            const int ret = write_track((uint8_t)(cyl * 2u + head));
            g_capture.state = CAPTURE_IDLE;
            if (ret != UFI_OK) { fail(ret == UFI_ERR_NO_INDEX ? 2 : 3); break; }
            HAL_GPIO_TogglePin(PIN_LED_ACT.port, PIN_LED_ACT.pin);
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
            if (finish_file() != UFI_OK) { fail(3); break; }
            unmount();
            led_set(&PIN_LED_ACT, true);
            state = D_DONE;
            break;

        default:
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
        if (button_copy) {                             /* UFI.CFG button_a=copy */
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
