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
#include <string.h>

#define SCP_TRACKS          168u
#define SCP_HDR_LEN         0x10u
#define SCP_TABLE_LEN       (4u * SCP_TRACKS)
#define SCP_FLAG_INDEX      0x01u
#define SCP_DISK_OTHER      0x80u
#define SCP_VERSION         0x22u
#define HOLD_START_MS       1000u   /* button A */
#define HOLD_MSC_MS         2000u   /* button B: USB mass storage mode on/off */

extern capture_context_t g_capture;

typedef enum { D_IDLE, D_START, D_CAPTURE, D_WAIT, D_WRITE, D_FINISH, D_DONE, D_ERROR } dump_state_t;

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
    uint8_t hdr[SCP_HDR_LEN] = {'S', 'C', 'P', SCP_VERSION, SCP_DISK_OTHER, cfg.revs, 0, last,
                                SCP_FLAG_INDEX, 0, (uint8_t)(cfg.sides == 2 ? 0 : 1), 0,
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

static void fail(uint8_t code)
{
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
    state = D_ERROR;
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
        if (c->tracks == 0 || c->tracks > 84 || c->sides == 0 || c->sides > 2 ||
            c->revs == 0 || c->revs > REVOLUTIONS_BUFFER) {
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
            if (mount() != UFI_OK) { fail(1); break; }
            if (open_next_file() != UFI_OK || begin_file() != UFI_OK) { fail(1); break; }
            if (ufi_drive_select((drive_type_t)cfg.drive) != UFI_OK ||
                ufi_drive_motor(true) != UFI_OK || ufi_drive_recalibrate() != UFI_OK) {
                fail(2);
                break;
            }
            cyl = 0;
            head = 0;
            state = D_CAPTURE;
            break;

        case D_CAPTURE:
            ufi_board_activity();
            if (ufi_capture_start(cyl, head, cfg.revs, 0) != UFI_OK) { fail(2); break; }
            state = D_WAIT;
            break;

        case D_WAIT:
            switch (ufi_capture_get_state()) {
                case CAPTURE_COMPLETE: state = D_WRITE; break;
                case CAPTURE_ERROR:    fail(2); break;
                default:               break;
            }
            break;

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
    static bool armed, a_used, b_used;
    static uint8_t blink_n;
    const uint32_t now = HAL_GetTick();
    const bool a = HAL_GPIO_ReadPin(PIN_BTN_A.port, PIN_BTN_A.pin) == GPIO_PIN_RESET;
    const bool b = HAL_GPIO_ReadPin(PIN_BTN_B.port, PIN_BTN_B.pin) == GPIO_PIN_RESET;

    if (!armed) {                                      /* start-up buttons: wait for release */
        armed = !a && !b;
        return;
    }
    if (!a) { a_since = now; a_used = false; }
    if (!b) {
        if (b_since && !b_used && ufi_dump_active()) { /* short press of B aborts */
            ufi_dump_abort();
        }
        b_since = 0;
        b_used = false;
    } else if (!b_since) {
        b_since = now;
    }

    if (a && !a_used && now - a_since >= HOLD_START_MS) {
        a_used = true;
        ufi_dump_start(NULL);                          /* busy / mass storage mode: ignored */
    }
    if (b && b_since && !b_used && !ufi_dump_active() && now - b_since >= HOLD_MSC_MS) {
        b_used = true;
        ufi_usb_set_msc(!ufi_usb_msc_active());
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
