/**
 * UFI Flux Engine - UFI v2 protocol (docs/USB_Protokoll.md section 3)
 *
 * Framed requests in the UFI flux personality (1209:4F54), next to the old one-command-
 * per-packet v1 protocol (ufi_usb.c).  A USB packet starting with 55 AA latches the session
 * to v2 until SET_LINE_CODING 10000 baud or re-enumeration; from then on every OUT byte
 * goes through the ring buffer below.
 *
 * Frame (both directions, little endian):
 *   55 AA type cmd seq status len16 payload[len] crc16
 *   type 1 request, 2 reply, 3 event, 4 data, 5 end; crc = CRC-16/CCITT-FALSE over
 *   type..payload.  One reply per request; READ and WRITE reply at once and close with an
 *   END frame (READ: DATA frames from ufi_stream.c in between, WRITE: the host sends DATA
 *   frames with the compact stream code of ufi_write.c).
 *
 * The USB IRQ fills a ring buffer and pauses the OUT endpoint when it is nearly full (same
 * flow control as ufi_gw.c); the main loop parses frames, so they may span USB packets.
 * Events (0x80-0x84) are queued and only sent while USB is idle, so never inside a frame.
 */

#include "ufi_firmware.h"
#include "usbd_cdc_if.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDevice;
extern capture_context_t g_capture;

#ifndef UFI_FW_VERSION
#define UFI_FW_VERSION "0.0"
#endif
#ifndef UFI_GIT_REV
#define UFI_GIT_REV "dev"
#endif

#define RX_SIZE         8192u           /* power of two */
#define RX_PAUSE_FREE   1024u           /* pause the OUT endpoint below this much room */
#define RX_RESUME_FREE  4096u
#define EVQ_SIZE        4u
#define EVQ_PAYLOAD     8u              /* largest event payload: board / dump status */
#define DISK_POLL_MS    200u
#define NAME_MAX        64u             /* file name / path in a request (8.3, '/') */

/* caps bits (INFO) */
#define CAP_PSRAM       (1u << 0)
#define CAP_SD          (1u << 1)
#define CAP_IEC         (1u << 2)
#define CAP_AMIGA       (1u << 3)
#define CAP_AMIGA2      (1u << 4)
#define CAP_SHUGART     (1u << 5)
#define CAP_APPLE       (1u << 6)
#define CAP_APPLE_SYNC  (1u << 7)
#define CAP_GW          (1u << 8)
#define CAP_FILES       (1u << 9)
#define CAP_EVENTS      (1u << 10)
#define CAP_DIAG        (1u << 11)   /* 1.13: DIAG_RPM, DRIVE_SCAN, INDEX_SIM */

enum {
    V2_PING = 0x00, V2_INFO = 0x01, V2_STATUS = 0x02, V2_RESET = 0x03, V2_BOOTLOADER = 0x04,
    V2_USB_MODE = 0x05, V2_EVENTS = 0x06,
    V2_SELECT = 0x10, V2_MOTOR = 0x11, V2_SEEK = 0x12, V2_RECAL = 0x13, V2_SIDE = 0x14,
    V2_TIMING = 0x15, V2_LINES = 0x16, V2_CHECK_DISK = 0x17, V2_PROBE_TRACKS = 0x18,
    V2_SEEK_TEST = 0x19, V2_AMIGA_ID = 0x1A, V2_DIAG_RPM = 0x1B, V2_DRIVE_SCAN = 0x1C,
    V2_INDEX_SIM = 0x1D,
    V2_ABORT = 0x21, V2_ERASE = 0x23, V2_PATTERN = 0x24,
    V2_IEC_RESET = 0x30, V2_IEC_SEND = 0x31, V2_IEC_RECV = 0x32,
    V2_POWER = 0x40, V2_USB_POWER = 0x41, V2_SD_INFO = 0x42,
    V2_DIR = 0x50, V2_READ_FILE = 0x51, V2_WRITE_FILE = 0x52, V2_DELETE = 0x53,
    V2_CFG_RELOAD = 0x54,
    V2_DUMP_START = 0x60, V2_DUMP_STATUS = 0x61, V2_DUMP_ABORT = 0x62, V2_COPY_START = 0x63,
    V2_FRAME_ERROR = 0xFF,
};

/* ============================================================================
 * STATE
 * ============================================================================ */

static uint8_t rx[RX_SIZE];
static volatile uint32_t rx_head;       /* written by the USB IRQ */
static uint32_t rx_tail;                /* main loop */
static volatile uint32_t rx_discard;    /* clear comms: bytes before this index are dropped */
static volatile bool on, rx_paused, clear_req;

static uint8_t fb[UFI_V2_FRAME_MAX];    /* frame being assembled */
static uint32_t have;
static uint8_t tx[UFI_V2_FRAME_MAX];    /* replies / END frames */
static char name_buf[NAME_MAX + 1u];

static bool op_read, op_write;          /* END frame still owed */
static uint8_t read_seq, write_seq;

typedef struct {
    uint8_t code, len;
    uint8_t data[EVQ_PAYLOAD];
} v2_event_t;
static v2_event_t evq[EVQ_SIZE];
static uint8_t ev_head, ev_tail, ev_seq;
static uint8_t ev_buf[UFI_V2_HDR + EVQ_PAYLOAD + 2u];   /* event on USB (outlives the call) */
static uint32_t ev_mask, disk_poll_ms;
static dump_status_t last_dump;
static bool last_dc;

/* ============================================================================
 * HELPERS
 * ============================================================================ */

static void put16(uint8_t* o, uint32_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* o, uint32_t v) { put16(o, v); put16(o + 2, v >> 16); }
static uint32_t get16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t* p) { return get16(p) | (get16(p + 2) << 16); }

/* UFI error code (negative) -> status byte */
static uint8_t st(int ret)
{
    return (ret < 0) ? (uint8_t)(-ret) : 0u;
}

/* CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF), byte-wise without table */
static uint16_t crc16(const uint8_t* p, uint32_t n)
{
    uint16_t crc = 0xFFFFu;
    while (n--) {
        uint8_t x = (uint8_t)((crc >> 8) ^ *p++);
        x ^= (uint8_t)(x >> 4);
        crc = (uint16_t)((crc << 8) ^ ((uint16_t)x << 12) ^ ((uint16_t)x << 5) ^ x);
    }
    return crc;
}

uint32_t ufi_v2_frame(uint8_t* f, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t status,
                      uint16_t len)
{
    f[0] = 0x55;
    f[1] = 0xAA;
    f[2] = type;
    f[3] = cmd;
    f[4] = seq;
    f[5] = status;
    put16(&f[6], len);
    put16(&f[UFI_V2_HDR + len], crc16(&f[2], 6u + len));
    return UFI_V2_HDR + len + 2u;
}

/* Blocking send from tx; payload may already sit at tx + UFI_V2_HDR */
static void send(uint8_t type, uint8_t cmd, uint8_t seq, uint8_t status,
                 const uint8_t* payload, uint16_t len)
{
    if (len && payload != &tx[UFI_V2_HDR]) {
        memmove(&tx[UFI_V2_HDR], payload, len);
    }
    ufi_usb_tx_blocking(tx, ufi_v2_frame(tx, type, cmd, seq, status, len));
}

static uint8_t board_rev(void)
{
    const uint16_t id = ufi_adc_mv(ADC_CH_BOARD_ID);
    return (id + 200u > BOARD_ID_V05_MV && id < BOARD_ID_V05_MV + 200u) ? 5u
         : (id + 200u > BOARD_ID_V06_MV && id < BOARD_ID_V06_MV + 200u) ? 6u
         : (id + 200u > BOARD_ID_V07_MV && id < BOARD_ID_V07_MV + 200u) ? 7u : 0u;
}

static uint32_t caps(uint8_t rev)
{
    uint32_t c = CAP_IEC | CAP_AMIGA | CAP_SHUGART | CAP_GW | CAP_FILES | CAP_EVENTS | CAP_DIAG;
    c |= ufi_flux_store_is_psram() ? CAP_PSRAM : 0u;
    c |= ufi_sd_present() ? CAP_SD : 0u;
    c |= (rev >= 7u) ? CAP_AMIGA2 : 0u;
    c |= ufi_board_has_apple() ? CAP_APPLE : 0u;
    c |= ufi_config_apple_sync() ? CAP_APPLE_SYNC : 0u;
    return c;
}

/* "1.12" -> 1, 12 */
static void fw_version(uint8_t* v)
{
    v[0] = v[1] = 0;
    unsigned i = 0;
    for (const char* s = UFI_FW_VERSION; *s && i < 2u; s++) {
        if (*s == '.') {
            i++;
        } else if (*s >= '0' && *s <= '9') {
            v[i] = (uint8_t)(v[i] * 10u + (uint8_t)(*s - '0'));
        }
    }
}

/* Append s as a line of the INFO text ('\n' between lines, no NUL) */
static uint16_t text_add(uint8_t* o, uint16_t n, const char* s, bool first)
{
    if (!first) {
        o[n++] = '\n';
    }
    while (*s && n < UFI_V2_MAX_PAYLOAD) {
        o[n++] = (uint8_t)*s++;
    }
    return n;
}

/* Payload bytes as a NUL-terminated name; NULL if too long */
static const char* name_of(const uint8_t* p, uint32_t n)
{
    if (n > NAME_MAX) {
        return NULL;
    }
    memcpy(name_buf, p, n);
    name_buf[n] = '\0';
    return name_buf;
}

static void abort_ops(void)
{
    if (op_read) {
        ufi_capture_abort();
        g_capture.state = CAPTURE_IDLE;
        op_read = false;
    }
    if (op_write) {
        ufi_write_abort();
        op_write = false;
    }
}

/* ============================================================================
 * USB SIDE (interrupt context)
 * ============================================================================ */

static uint32_t rx_free(void)
{
    return RX_SIZE - 1u - ((rx_head - rx_tail) & (RX_SIZE - 1u));
}

bool ufi_v2_rx(const uint8_t* buf, uint32_t len)
{
    on = true;
    uint32_t h = rx_head;
    for (uint32_t i = 0; i < len && rx_free() > 0; i++) {
        rx[h] = buf[i];
        h = (h + 1u) & (RX_SIZE - 1u);
        rx_head = h;
    }
    if (rx_free() < RX_PAUSE_FREE) {
        rx_paused = true;               /* main loop re-arms the endpoint */
        return false;
    }
    return true;
}

/* Back to v1; the main loop drops what arrived so far and aborts a read / write */
void ufi_v2_clear_comms(void)
{
    rx_discard = rx_head;
    on = false;
    clear_req = true;
}

bool ufi_v2_active(void)
{
    return on;
}

/* A v2 WRITE is receiving (still true right after a clear until the main loop drops it):
 * its OUT data never goes to the v1 write path */
bool ufi_v2_write_pending(void)
{
    return op_write;
}

static void rx_resume_if_room(void)
{
    if (rx_paused && rx_free() >= RX_RESUME_FREE) {
        rx_paused = false;
        USBD_CDC_ReceivePacket(&hUsbDevice);
    }
}

/* ============================================================================
 * END FRAMES / EVENTS
 * ============================================================================ */

bool ufi_v2_end(uint8_t cmd, int result, const uint8_t* payload, uint16_t len)
{
    bool* op = (cmd == UFI_V2_READ) ? &op_read : &op_write;
    if (!*op) {
        return false;                   /* not a v2 operation: v1 completion message */
    }
    *op = false;
    if (cmd == UFI_V2_WRITE && result == UFI_ERR_TIMEOUT) {
        result = UFI_ERR_DMA;           /* verify capture timed out: verify failed (6) */
    }
    send(UFI_V2_END, cmd, (cmd == UFI_V2_READ) ? read_seq : write_seq, st(result), payload, len);
    return true;
}

void ufi_v2_event(uint8_t code, const void* payload, uint16_t len)
{
    if (!on || code < UFI_V2_EVT_DISK || !(ev_mask & (1u << (code - UFI_V2_EVT_DISK))) ||
        len > EVQ_PAYLOAD) {
        return;
    }
    const uint8_t next = (uint8_t)((ev_head + 1u) % EVQ_SIZE);
    if (next == ev_tail) {
        return;                         /* queue full: drop */
    }
    evq[ev_head].code = code;
    evq[ev_head].len = (uint8_t)len;
    memcpy(evq[ev_head].data, payload, len);
    ev_head = next;
}

/* One queued event while USB is idle (between frames) */
static void flush_event(void)
{
    if (ev_tail == ev_head || !ufi_usb_tx_idle()) {
        return;
    }
    const v2_event_t* e = &evq[ev_tail];
    memcpy(&ev_buf[UFI_V2_HDR], e->data, e->len);
    const uint32_t n = ufi_v2_frame(ev_buf, UFI_V2_EVENT, e->code, ev_seq, 0, e->len);
    if (ufi_usb_tx_start(ev_buf, n) != UFI_ERR_BUSY) {
        ev_seq++;
        ev_tail = (uint8_t)((ev_tail + 1u) % EVQ_SIZE);
    }
}

/* Mode switch: the event must be out before the device re-enumerates */
void ufi_v2_event_now(uint8_t code, const void* payload, uint16_t len)
{
    ufi_v2_event(code, payload, len);
    const uint32_t t0 = HAL_GetTick();
    while (ev_tail != ev_head && HAL_GetTick() - t0 < 100u) {
        flush_event();
    }
    while (!ufi_usb_tx_idle() && HAL_GetTick() - t0 < 100u) {
    }
}

/* Cheap polled sources: dump progress, disk change */
static void poll_events(void)
{
    if (!ev_mask) {
        return;
    }
    if (ev_mask & (1u << (UFI_V2_EVT_DUMP - UFI_V2_EVT_DISK))) {
        const dump_status_t d = ufi_dump_status();
        if (d.state != last_dump.state || d.track != last_dump.track || d.side != last_dump.side) {
            last_dump = d;
            ufi_v2_event(UFI_V2_EVT_DUMP, &d, sizeof(d));
        }
    }
    if ((ev_mask & 1u) && !op_read && !op_write && !ufi_dump_active() &&
        HAL_GetTick() - disk_poll_ms >= DISK_POLL_MS) {
        disk_poll_ms = HAL_GetTick();
        const drive_type_t d = ufi_drive_get_current();
        const bool changed = (d != DRIVE_NONE) && ufi_drive_disk_changed();
        if (changed && !last_dc) {
            const uint8_t v = (uint8_t)d;
            ufi_v2_event(UFI_V2_EVT_DISK, &v, 1);
        }
        last_dc = changed;
    }
}

/* ============================================================================
 * REQUESTS
 * ============================================================================ */

/* A stand-alone dump owns drive and file system: only these meanwhile */
static bool allowed_during_dump(uint8_t cmd)
{
    switch (cmd) {
        case V2_PING: case V2_INFO: case V2_STATUS: case V2_EVENTS: case V2_POWER:
        case V2_USB_POWER: case V2_SD_INFO: case V2_DUMP_STATUS: case V2_DUMP_ABORT:
        case V2_INDEX_SIM:
            return true;
        default:
            return false;
    }
}

/* Commands that move or use the drive: refused while a READ / WRITE runs */
static bool uses_drive(uint8_t cmd)
{
    return (cmd >= V2_SELECT && cmd <= V2_DRIVE_SCAN) || cmd == UFI_V2_READ ||
           cmd == UFI_V2_WRITE || cmd == V2_ERASE || cmd == V2_PATTERN ||
           cmd == V2_DUMP_START || cmd == V2_COPY_START;
}

static int cmd_read(uint8_t seq, const uint8_t* p, uint16_t len)
{
    if (len < 3u) {
        return UFI_ERR_BAD_ARGS;
    }
    uint8_t revs = p[2];
    if (revs == 0) revs = 1;
    if (revs > REVOLUTIONS_STREAM) revs = REVOLUTIONS_STREAM;
    uint32_t period_ticks = 0;
    if (len >= 4u && (p[3] & 0x01u)) {     /* no index: slices of period_ms (0 = 200) */
        uint32_t ms = (len >= 6u) ? get16(&p[4]) : 0u;
        if (ms == 0 || ms > 2000u) ms = 200u;
        period_ticks = ms * (FLUX_TIMER_FREQ / 1000u);
    }
    const int ret = ufi_capture_start(p[0], p[1], revs, period_ticks);
    if (ret == UFI_OK) {
        op_read = true;
        read_seq = seq;
        ufi_stream_begin_v2(seq);           /* DATA frames follow the reply */
    }
    return ret;
}

static int cmd_write(uint8_t seq, const uint8_t* p, uint16_t len)
{
    if (len < 11u) {
        return UFI_ERR_BAD_ARGS;
    }
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        return UFI_ERR_WRITE_PROT;
    }
    const int ret = ufi_write_prepare_compact(p[0], p[1], get32(&p[3]), get32(&p[7]),
                                              (p[2] & 0x01u) != 0);
    if (ret == UFI_OK) {
        op_write = true;
        write_seq = seq;
    }
    return ret;
}

/* DATA frame of a WRITE: compact stream code for ufi_write.c */
static void write_data(uint8_t* p, uint16_t len)
{
    if (ufi_write_receive_chunk(p, len) != UFI_OK) {
        ufi_write_abort();
        ufi_v2_end(UFI_V2_WRITE, UFI_ERR_BUFFER_FULL, NULL, 0);    /* more than byte_count */
    }
}

static void request(uint8_t cmd, uint8_t seq, uint8_t* p, uint16_t len)
{
    uint8_t* out = &tx[UFI_V2_HDR];
    uint16_t n = 0;
    int ret = UFI_OK;

    if ((ufi_dump_active() && !allowed_during_dump(cmd)) ||
        ((op_read || op_write) && uses_drive(cmd))) {
        send(UFI_V2_REPLY, cmd, seq, st(UFI_ERR_BUSY), NULL, 0);
        return;
    }

    switch (cmd) {
        /* ---- system ---- */
        case V2_PING:
            memmove(out, p, len);
            n = len;
            break;
        case V2_INFO: {
            const uint8_t rev = board_rev();
            out[0] = 2;                     /* protocol version */
            fw_version(&out[1]);
            out[3] = rev;
            put32(&out[4], caps(rev));
            put32(&out[8], FLUX_TIMER_FREQ);
            uint32_t words;
            ufi_flux_store(&words);
            put32(&out[12], words * 4u);
            put16(&out[16], UFI_V2_MAX_PAYLOAD);
            n = text_add(out, 18, "UFI Flux Engine v" UFI_FW_VERSION, true);
            n = text_add(out, n, UFI_GIT_REV, false);
            n = text_add(out, n, __DATE__, false);
            n = text_add(out, n, ufi_psram_result(), false);
            break;
        }
        case V2_STATUS: {
            const drive_status_t s = ufi_drive_get_status();
            out[0] = (uint8_t)s.type;
            out[1] = s.motor_on;
            out[2] = s.write_protected;
            out[3] = s.track0;
            out[4] = s.disk_changed;
            out[5] = s.ready;
            out[6] = s.current_track;
            out[7] = s.current_side;
            const board_status_t b = ufi_board_status();
            memcpy(&out[8], &b, sizeof(b));
            n = 8u + sizeof(b);
            break;
        }
        case V2_RESET:
            send(UFI_V2_REPLY, cmd, seq, 0, NULL, 0);
            NVIC_SystemReset();
            return;
        case V2_BOOTLOADER:
            send(UFI_V2_REPLY, cmd, seq, 0, NULL, 0);
            HAL_Delay(20);                  /* let the host read the reply */
            ufi_request_bootloader();
            return;
        case V2_USB_MODE: {
            const uint8_t mode = (len >= 1u) ? p[0] : 0xFFu;
            if (mode > UFI_USB_GW) {
                ret = UFI_ERR_BAD_ARGS;
            } else if ((mode == UFI_USB_SD || mode == UFI_USB_FLOPPY) && ufi_dump_active()) {
                ret = UFI_ERR_BUSY;
            } else if (mode == UFI_USB_SD && ufi_sd_init() != UFI_OK) {
                ret = UFI_ERR_STORAGE;
            }
            send(UFI_V2_REPLY, cmd, seq, st(ret), NULL, 0);    /* reply first: USB goes away */
            if (ret == UFI_OK && mode != UFI_USB_FLUX) {
                HAL_Delay(20);
                ufi_usb_set_mode(mode);
            }
            return;
        }
        case V2_EVENTS:
            if (len < 4u) {
                ret = UFI_ERR_BAD_ARGS;
                break;
            }
            ev_mask = get32(p);
            last_dump = ufi_dump_status();
            last_dc = false;
            break;

        /* ---- drive ---- */
        case V2_SELECT:
            ret = (len < 1u || p[0] >= DRIVE_TYPE_COUNT) ? UFI_ERR_BAD_ARGS
                                                       : ufi_drive_select((drive_type_t)p[0]);
            break;
        case V2_MOTOR:
            ret = (len < 1u) ? UFI_ERR_BAD_ARGS : ufi_drive_motor(p[0] != 0);
            break;
        case V2_SEEK:
            ret = (len < 1u) ? UFI_ERR_BAD_ARGS : ufi_drive_seek(p[0]);
            break;
        case V2_RECAL:
            ret = ufi_drive_recalibrate();
            break;
        case V2_SIDE:
            ret = (len < 1u) ? UFI_ERR_BAD_ARGS : ufi_drive_select_side(p[0]);
            break;
        case V2_TIMING: {                   /* fewer fields: only the leading ones are set */
            drive_timing_t t = ufi_drive_get_timing();
            if (len & 1u || len > sizeof(t)) {
                ret = UFI_ERR_BAD_ARGS;
                break;
            }
            if (len) {
                memcpy(&t, p, len);
                ufi_drive_set_timing(&t);
            }
            t = ufi_drive_get_timing();
            memcpy(out, &t, sizeof(t));
            n = sizeof(t);
            break;
        }
        case V2_LINES:
            if (len < 2u) {
                ret = UFI_ERR_BAD_ARGS;
                break;
            }
            ufi_drive_density_line(p[0] != 0);
            bus_out(&PIN_FDD_DRATE, p[1] != 0);
            break;
        case V2_CHECK_DISK: {
            bool changed = false, present = false;
            ret = ufi_drive_check_disk(&changed, &present);
            out[0] = changed;
            out[1] = present;
            n = (ret == UFI_OK) ? 2u : 0u;
            break;
        }
        case V2_PROBE_TRACKS:
            ret = ufi_drive_probe_tracks(&out[0]);
            n = (ret == UFI_OK) ? 1u : 0u;
            break;
        case V2_SEEK_TEST:
            ret = (len < 3u) ? UFI_ERR_BAD_ARGS : ufi_drive_seek_test(p[0], p[1], p[2]);
            break;
        case V2_AMIGA_ID: {
            uint32_t id = 0;
            ret = ufi_drive_amiga_id(&id);
            put32(out, id);
            n = (ret == UFI_OK) ? 4u : 0u;
            break;
        }
        case V2_DIAG_RPM: {                 /* revs u8 -> revs, min/avg/max, pulse, rpm*100 */
            diag_rpm_t r;
            ret = ufi_diag_rpm(len >= 1u ? p[0] : 0u, &r);
            if (ret == UFI_OK) {
                out[0] = r.revs;
                put32(&out[1], r.period_min);
                put32(&out[5], r.period_avg);
                put32(&out[9], r.period_max);
                put32(&out[13], r.pulse_avg);
                put16(&out[17], r.rpm_x100);
                n = 19u;
            }
            break;
        }
        case V2_DRIVE_SCAN: {               /* flags u8 (bit0: Shugart bus DS0-DS3) */
            uint8_t cnt = 0;
            ret = ufi_diag_scan(len >= 1u && (p[0] & 0x01u), out, &cnt);
            n = (ret == UFI_OK) ? cnt : 0u;
            break;
        }
        case V2_INDEX_SIM:                  /* rpm u16 (0 = off), pulse_us u16; - = query */
            if (len >= 2u) {
                ret = ufi_index_sim_set(get16(p), len >= 4u ? get16(&p[2]) : 0u);
            }
            put16(out, ufi_index_sim_rpm());
            n = 2u;
            break;

        /* ---- flux ---- */
        case UFI_V2_READ:
            ret = cmd_read(seq, p, len);
            break;
        case V2_ABORT: {                    /* the running operation ends with status 4 */
            const bool r = op_read, w = op_write;
            if (r) {
                ufi_capture_abort();
                g_capture.state = CAPTURE_IDLE;
            }
            if (w) {
                ufi_write_abort();
            }
            send(UFI_V2_REPLY, cmd, seq, 0, NULL, 0);
            if (r) {
                const uint8_t revs = 0;
                ufi_v2_end(UFI_V2_READ, UFI_ERR_NO_INDEX, &revs, 1);
            }
            if (w) {
                ufi_v2_end(UFI_V2_WRITE, UFI_ERR_NO_INDEX, NULL, 0);
            }
            return;
        }
        case UFI_V2_WRITE:
            ret = cmd_write(seq, p, len);
            break;
        case V2_ERASE:
            ret = (len < 2u) ? UFI_ERR_BAD_ARGS : ufi_erase_track(p[0], p[1]);
            break;
        case V2_PATTERN:
            ret = (len < 6u) ? UFI_ERR_BAD_ARGS
                             : ufi_write_pattern(p[0], p[1], (uint16_t)get16(&p[2]),
                                                 (uint16_t)get16(&p[4]));
            break;

        /* ---- Commodore IEC ---- */
        case V2_IEC_RESET:
            ret = ufi_iec_reset();
            break;
        case V2_IEC_SEND:
            ret = (len < 2u) ? UFI_ERR_BAD_ARGS : ufi_iec_send_byte(p[0], p[1] != 0);
            break;
        case V2_IEC_RECV: {
            bool eoi = false;
            ret = ufi_iec_receive_byte(&out[0], &eoi);
            out[1] = eoi ? 1u : 0u;
            n = (ret == UFI_OK) ? 2u : 0u;
            break;
        }

        /* ---- board ---- */
        case V2_POWER: {
            if (len >= 1u) {
                ufi_board_power(p[0]);
            }
            const board_status_t s = ufi_board_status();
            memcpy(out, &s, sizeof(s));
            n = sizeof(s);
            break;
        }
        case V2_USB_POWER: {
            usb_power_t u = {0};
            ret = ufi_usb_power(&u);
            memcpy(out, &u, sizeof(u));
            n = (ret == UFI_OK) ? sizeof(u) : 0u;
            break;
        }
        case V2_SD_INFO: {
            sd_info_t s = {0};
            ret = ufi_sd_info(&s);
            memcpy(out, &s, sizeof(s));
            n = sizeof(s);
            break;
        }

        /* ---- files ---- */
        case V2_DIR: {
            const char* path = (len >= 2u) ? name_of(&p[2], len - 2u) : NULL;
            ret = path ? ufi_file_dir(path, (uint16_t)get16(p), out, UFI_V2_MAX_PAYLOAD, &n)
                       : UFI_ERR_BAD_ARGS;
            break;
        }
        case V2_READ_FILE: {
            const char* name = (len >= 7u) ? name_of(&p[6], len - 6u) : NULL;
            const uint32_t want = (len >= 6u) ? get16(&p[4]) : 0u;
            ret = (name && want <= UFI_V2_MAX_PAYLOAD)
                      ? ufi_file_read(name, get32(p), out, (uint16_t)want, &n) : UFI_ERR_BAD_ARGS;
            break;
        }
        case V2_WRITE_FILE: {
            const uint32_t nlen = (len >= 6u) ? p[5] : 0u;
            const char* name = (nlen && 6u + nlen <= len) ? name_of(&p[6], nlen) : NULL;
            uint16_t written = 0;
            ret = name ? ufi_file_write(name, get32(p), (p[4] & 0x01u) != 0, &p[6u + nlen],
                                        (uint16_t)(len - 6u - nlen), &written)
                       : UFI_ERR_BAD_ARGS;
            put16(out, written);            /* short write (disk full): status 13 */
            n = (ret == UFI_OK || ret == UFI_ERR_STORAGE) ? 2u : 0u;
            break;
        }
        case V2_DELETE: {
            const char* name = (len >= 1u) ? name_of(p, len) : NULL;
            ret = name ? ufi_file_delete(name) : UFI_ERR_BAD_ARGS;
            break;
        }
        case V2_CFG_RELOAD:
            ufi_config_load();
            ret = ufi_sd_present() ? UFI_OK : UFI_ERR_STORAGE;
            break;

        /* ---- stand-alone ---- */
        case V2_DUMP_START:
            if (len == sizeof(dump_config_t)) {
                dump_config_t c;
                memcpy(&c, p, sizeof(c));
                ret = ufi_dump_start(&c);
            } else {
                ret = (len == 0) ? ufi_dump_start(NULL) : UFI_ERR_BAD_ARGS;
            }
            break;
        case V2_DUMP_STATUS: {
            const dump_status_t s = ufi_dump_status();
            memcpy(out, &s, sizeof(s));
            n = sizeof(s);
            break;
        }
        case V2_DUMP_ABORT:
            ufi_dump_abort();
            break;
        case V2_COPY_START:
            ret = ufi_copy_start();
            break;

        default:
            ret = UFI_ERR_UNKNOWN_CMD;
            break;
    }
    send(UFI_V2_REPLY, cmd, seq, st(ret), out, n);
}

/* ============================================================================
 * FRAME PARSER
 * ============================================================================ */

static void frame_error(void)
{
    send(UFI_V2_REPLY, V2_FRAME_ERROR, 0, st(UFI_ERR_FRAME), NULL, 0);
}

/* Drop the bad frame start: continue at the next 55 AA in what was collected */
static void resync(void)
{
    uint32_t k = 1;
    while (k < have && !(fb[k] == 0x55u && (k + 1u == have || fb[k + 1u] == 0xAAu))) {
        k++;
    }
    memmove(fb, &fb[k], have - k);
    have -= k;
}

static void check_frame(void)
{
    while (have >= UFI_V2_HDR) {
        const uint32_t len = get16(&fb[6]);
        if (len > UFI_V2_MAX_PAYLOAD) {
            frame_error();
            resync();
            continue;
        }
        if (have < UFI_V2_HDR + len + 2u) {
            return;
        }
        if (get16(&fb[UFI_V2_HDR + len]) != crc16(&fb[2], 6u + len)) {
            frame_error();
            resync();
            continue;
        }
        have = 0;
        const uint8_t type = fb[2], cmd = fb[3], seq = fb[4];
        if (type == UFI_V2_DATA) {
            if (cmd == UFI_V2_WRITE && op_write && seq == write_seq) {
                write_data(&fb[UFI_V2_HDR], (uint16_t)len);
            }
        } else if (type == UFI_V2_REQUEST) {
            ufi_board_activity();           /* motor idle timer */
            request(cmd, seq, &fb[UFI_V2_HDR], (uint16_t)len);
        }
        return;
    }
}

static void parse_byte(uint8_t b)
{
    if (have == 0) {
        if (b == 0x55u) {
            fb[have++] = b;
        }
        return;
    }
    if (have == 1) {
        if (b == 0xAAu) {
            fb[have++] = b;
        } else if (b != 0x55u) {
            have = 0;
        }
        return;
    }
    fb[have++] = b;
    check_frame();
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

void ufi_v2_reset(void)
{
    on = false;
    clear_req = false;
    abort_ops();
    have = 0;
    rx_tail = rx_head;
    rx_paused = false;
    ev_mask = 0;
    ev_head = ev_tail = 0;
}

void ufi_v2_service(void)
{
    if (clear_req) {                    /* host: SET_LINE_CODING 10000 baud */
        clear_req = false;
        abort_ops();
        have = 0;
        ev_mask = 0;
        ev_head = ev_tail = 0;
        rx_tail = rx_discard;           /* a new session may already follow */
        if (rx_paused) {
            rx_paused = false;
            USBD_CDC_ReceivePacket(&hUsbDevice);
        }
    }
    if (!on) {
        return;
    }
    while (rx_tail != rx_head && !clear_req) {
        const uint8_t b = rx[rx_tail];
        rx_tail = (rx_tail + 1u) & (RX_SIZE - 1u);
        parse_byte(b);
    }
    rx_resume_if_room();
    poll_events();
    flush_event();
}
