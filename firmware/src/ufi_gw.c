/**
 * UFI Flux Engine - Greaseweazle-compatible protocol (UFI.CFG protocol=gw)
 *
 * Lets the Greaseweazle host tools ("gw read", "gw write", "gw info", ...) drive the board.
 * Command set, flux code and control channel follow cdc_acm_protocol.h of the Greaseweazle
 * firmware (Keir Fraser, public domain / Unlicense); this is an independent implementation
 * on top of the UFI drive, capture (ufi_stream.c) and write (ufi_write.c) paths.
 *
 * - Own USB ID 1209:4F57; the product string contains "gw-compat", which the gw tools
 *   accept as a compatible device (found without --device).
 * - Sample clock 68.75 MHz (TIM2 275 MHz / 4): MFM intervals fit the 1-2 byte codes.
 * - Bus types IBM PC (units 0/1 = drive A/B) and Shugart (units 0-3 = DS0-DS3).  Amiga
 *   (J7), IEC and Apple drives are only reachable with the UFI protocol.
 * - Not supported (ACK_BAD_COMMAND): firmware update / mode switch, test mode, byte
 *   source/sink (gw bandwidth), no-click step (flippy drives), hard-sectored writes.
 * - Writes always start at the index pulse and end at the next one (cue_at_index and
 *   terminate_at_index as gw sends them by default); the firmware's own write
 *   precompensation is off, gw applies its own.
 *
 * Commands arrive as [cmd, len, args...] in the CDC byte stream (no packet alignment);
 * every reply starts with [cmd, ack].  The USB IRQ fills a ring buffer and pauses the OUT
 * endpoint when it is nearly full, so long write streams are flow controlled.
 */

#include "ufi_firmware.h"
#include "usbd_cdc_if.h"
#include <string.h>

extern USBD_HandleTypeDef hUsbDevice;
extern capture_context_t g_capture;

/* ============================================================================
 * PROTOCOL CONSTANTS (Greaseweazle cdc_acm_protocol.h)
 * ============================================================================ */

enum {
    CMD_GET_INFO = 0, CMD_UPDATE = 1, CMD_SEEK = 2, CMD_HEAD = 3, CMD_SET_PARAMS = 4,
    CMD_GET_PARAMS = 5, CMD_MOTOR = 6, CMD_READ_FLUX = 7, CMD_WRITE_FLUX = 8,
    CMD_GET_FLUX_STATUS = 9, CMD_SWITCH_FW_MODE = 11, CMD_SELECT = 12, CMD_DESELECT = 13,
    CMD_SET_BUS_TYPE = 14, CMD_SET_PIN = 15, CMD_RESET = 16, CMD_ERASE_FLUX = 17,
    CMD_SOURCE_BYTES = 18, CMD_SINK_BYTES = 19, CMD_GET_PIN = 20, CMD_TEST_MODE = 21,
    CMD_NOCLICK_STEP = 22, CMD_MAX = 22,
};

enum { BUS_NONE = 0, BUS_IBMPC = 1, BUS_SHUGART = 2 };

enum {
    ACK_OKAY = 0, ACK_BAD_COMMAND = 1, ACK_NO_INDEX = 2, ACK_NO_TRK0 = 3,
    ACK_FLUX_OVERFLOW = 4, ACK_FLUX_UNDERFLOW = 5, ACK_WRPROT = 6, ACK_NO_UNIT = 7,
    ACK_NO_BUS = 8, ACK_BAD_UNIT = 9, ACK_BAD_PIN = 10, ACK_BAD_CYLINDER = 11,
    ACK_OUT_OF_SRAM = 12,
};

enum { GETINFO_FIRMWARE = 0, GETINFO_BW_STATS = 1, GETINFO_CURRENT_DRIVE = 7, GETINFO_DRIVE0 = 8 };
enum { FLUXOP_INDEX = 1, FLUXOP_SPACE = 2, FLUXOP_ASTABLE = 3 };

#define GW_HW_MODEL     0x55u           /* not a Greaseweazle model: gw info shows "Unknown" */
#define GW_HW_SUBMODEL  0x01u
#define GW_MCU_MHZ      550u
#define GW_SRAM_KB      564u
#define GW_MAX_CYL      83

/* ============================================================================
 * STATE
 * ============================================================================ */

#define RX_SIZE         8192u           /* power of two */
#define RX_PAUSE_FREE   1024u           /* pause the OUT endpoint below this much room */
#define RX_RESUME_FREE  4096u

static uint8_t rx[RX_SIZE];
static volatile uint32_t rx_head;       /* written by the USB IRQ */
static uint32_t rx_tail;                /* main loop */
static volatile bool rx_paused, clear_req;

typedef enum { ST_CMD, ST_READ, ST_WDATA } gw_state_t;
static gw_state_t state;

static uint8_t cbuf[64];                /* command being assembled */
static uint8_t clen;

/* Greaseweazle delay parameters (CMD_{GET,SET}_PARAMS index 0), all u16 */
typedef struct {
    uint16_t select_delay;              /* us */
    uint16_t step_delay;                /* us */
    uint16_t seek_settle;               /* ms */
    uint16_t motor_delay;               /* ms */
    uint16_t watchdog;                  /* ms */
    uint16_t pre_write;                 /* us (kept, not used) */
    uint16_t post_write;                /* us (kept, not used) */
    uint16_t index_mask;                /* us (kept, not used) */
} gw_delays_t;
static gw_delays_t delays;

static uint8_t bus;
static int8_t unit = -1;                /* selected unit, -1 = none */
static int16_t cyl[4];
static bool cyl_valid[4], motor[4];
static uint8_t head;
static uint8_t flux_status;
static uint32_t last_cmd_ms;
static bool watchdog_armed;

/* write stream decoder */
static uint32_t w_count, w_space;
static bool w_overflow, w_bad;
static uint8_t w_code[6], w_have, w_need;

/* ============================================================================
 * HELPERS
 * ============================================================================ */

static uint8_t ack_of(int ret)
{
    switch (ret) {
        case UFI_OK:              return ACK_OKAY;
        case UFI_ERR_NO_INDEX:    return ACK_NO_INDEX;
        case UFI_ERR_SEEK_FAIL:   return ACK_NO_TRK0;
        case UFI_ERR_WRITE_PROT:  return ACK_WRPROT;
        case UFI_ERR_NO_DRIVE:    return ACK_NO_UNIT;
        default:                  return ACK_FLUX_OVERFLOW;
    }
}

static void put16(uint8_t* o, uint32_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* o, uint32_t v) { put16(o, v); put16(o + 2, v >> 16); }
static uint32_t get16(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8); }
static uint32_t get32(const uint8_t* p) { return get16(p) | (get16(p + 2) << 16); }

/* [cmd, ack] + payload */
static void reply(uint8_t cmd, uint8_t ack, const uint8_t* payload, uint32_t n)
{
    static uint8_t out[2 + 32];
    out[0] = cmd;
    out[1] = ack;
    if (n > 32u) {
        n = 32u;
    }
    if (n) {
        memcpy(&out[2], payload, n);
    }
    ufi_usb_tx_blocking(out, 2u + n);
}

static void send_byte(uint8_t b)
{
    static uint8_t out;
    out = b;
    ufi_usb_tx_blocking(&out, 1);
}

static drive_type_t unit_drive(int u)
{
    if (bus == BUS_IBMPC && u >= 0 && u <= 1) {
        return u ? DRIVE_SHUGART_B : DRIVE_SHUGART_A;
    }
    if (bus == BUS_SHUGART && u >= 0 && u <= 3) {
        return (drive_type_t)(DRIVE_SHUGART_DS0 + u);
    }
    return DRIVE_NONE;
}

static void delays_from_timing(void)
{
    const drive_timing_t t = ufi_drive_get_timing();
    delays.select_delay = t.select_settle_us;
    delays.step_delay = t.step_rate_us;
    delays.seek_settle = (uint16_t)(t.settle_us / 1000u);
    delays.motor_delay = t.spinup_ms;
}

static void delays_to_timing(void)
{
    drive_timing_t t = ufi_drive_get_timing();
    t.select_settle_us = delays.select_delay;
    t.step_rate_us = delays.step_delay;
    const uint32_t settle = (uint32_t)delays.seek_settle * 1000u;
    t.settle_us = (uint16_t)(settle > 65000u ? 65000u : settle);
    t.spinup_ms = delays.motor_delay;
    ufi_drive_set_timing(&t);
}

static void drives_off(void)
{
    ufi_drive_safe_state();             /* motors off, deselect, write lines released */
    unit = -1;
    memset(motor, 0, sizeof(motor));
}

static void power_on_state(void)
{
    drives_off();
    ufi_drive_density_line(false);
    bus = BUS_NONE;
    head = 0;
    memset(cyl_valid, 0, sizeof(cyl_valid));
    delays_from_timing();
    delays.watchdog = 10000u;
    delays.pre_write = 100u;
    delays.post_write = 1000u;
    delays.index_mask = 200u;
}

/* ============================================================================
 * USB SIDE (interrupt context)
 * ============================================================================ */

static uint32_t rx_free(void)
{
    return RX_SIZE - 1u - ((rx_head - rx_tail) & (RX_SIZE - 1u));
}

bool ufi_gw_rx(const uint8_t* buf, uint32_t len)
{
    if (clear_req) {
        return true;                    /* comms being cleared: drop */
    }
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

void ufi_gw_clear_comms(void)
{
    clear_req = true;
}

static void rx_resume_if_room(void)
{
    if (rx_paused && rx_free() >= RX_RESUME_FREE) {
        rx_paused = false;
        USBD_CDC_ReceivePacket(&hUsbDevice);
    }
}

/* ============================================================================
 * COMMANDS
 * ============================================================================ */

static void cmd_get_info(void)
{
    uint8_t p[32];
    memset(p, 0, sizeof(p));
    if (clen != 3) {
        reply(CMD_GET_INFO, ACK_BAD_COMMAND, NULL, 0);
        return;
    }
    const uint8_t idx = cbuf[2];
    if (idx == GETINFO_FIRMWARE) {
        /* version = UFI firmware version ("1.9" -> 1, 9); gw needs >= 0.31 */
        const char* v = UFI_FW_VERSION;
        uint32_t major = 0, minor = 0;
        while (*v >= '0' && *v <= '9') major = major * 10u + (uint32_t)(*v++ - '0');
        if (*v == '.') v++;
        while (*v >= '0' && *v <= '9') minor = minor * 10u + (uint32_t)(*v++ - '0');
        p[0] = (uint8_t)major;
        p[1] = (uint8_t)minor;
        p[2] = 1;                       /* main firmware */
        p[3] = CMD_MAX;
        put32(&p[4], GW_SAMPLE_FREQ);
        p[8] = GW_HW_MODEL;
        p[9] = GW_HW_SUBMODEL;
        p[10] = 0;                      /* full speed */
        p[11] = 0;                      /* MCU id: none of the gw-known ones */
        put16(&p[12], GW_MCU_MHZ);
        put16(&p[14], GW_SRAM_KB);
        put16(&p[16], 8);               /* stream buffer kB (ufi_stream.c) */
    } else if (idx == GETINFO_BW_STATS) {
        /* not measured: nominal full-speed bulk rate (8 KB per 8 ms) for min and max */
        put32(&p[0], 8192u); put32(&p[4], 8000u);
        put32(&p[8], 8192u); put32(&p[12], 8000u);
    } else if (idx == GETINFO_CURRENT_DRIVE || (idx >= GETINFO_DRIVE0 && idx < GETINFO_DRIVE0 + 4)) {
        const int u = (idx == GETINFO_CURRENT_DRIVE) ? unit : idx - GETINFO_DRIVE0;
        if (u < 0) {
            reply(CMD_GET_INFO, ACK_NO_UNIT, NULL, 0);
            return;
        }
        if (unit_drive(u) == DRIVE_NONE) {
            reply(CMD_GET_INFO, ACK_BAD_UNIT, NULL, 0);
            return;
        }
        put32(&p[0], (cyl_valid[u] ? 1u : 0u) | (motor[u] ? 2u : 0u));
        put32(&p[4], (uint32_t)(int32_t)cyl[u]);
    } else {
        reply(CMD_GET_INFO, ACK_BAD_COMMAND, NULL, 0);
        return;
    }
    reply(CMD_GET_INFO, ACK_OKAY, p, 32);
}

static uint8_t do_seek(int c)
{
    if (unit < 0) {
        return ACK_NO_UNIT;
    }
    if (c < 0 || c > GW_MAX_CYL) {
        return ACK_BAD_CYLINDER;        /* no flippy drives (negative cylinders) */
    }
    if (ufi_drive_seek((uint8_t)c) != UFI_OK) {
        cyl_valid[unit] = false;
        return ACK_NO_TRK0;
    }
    cyl[unit] = (int16_t)c;
    cyl_valid[unit] = true;
    return ACK_OKAY;
}

static uint8_t do_motor(int u, bool on)
{
    const drive_type_t d = unit_drive(u);
    if (bus == BUS_NONE) {
        return ACK_NO_BUS;
    }
    if (d == DRIVE_NONE) {
        return ACK_BAD_UNIT;
    }
    const drive_type_t prev = ufi_drive_get_current();
    if (prev != d) {
        ufi_drive_select(d);            /* motor control works on the current drive */
    }
    ufi_drive_motor(on);                /* waits motor_delay when switched on */
    if (prev != d) {
        ufi_drive_select(prev);
    }
    if (bus == BUS_SHUGART) {
        memset(motor, on, sizeof(motor));   /* pin 16 MOTOR ON is shared */
    } else {
        motor[u] = on;
    }
    return ACK_OKAY;
}

static uint8_t get_pin(uint8_t pin, uint8_t* level)
{
    const gpio_pin_t* p;
    switch (pin) {
        case 8:  p = &PIN_FDD_INDEX; break;
        case 26: p = &PIN_FDD_TRACK0; break;
        case 28: p = &PIN_FDD_WPROT; break;
        case 34: p = &PIN_FDD_DKCHG; break;     /* DSKCHG (PC) / READY (Shugart) */
        default: return ACK_BAD_PIN;
    }
    *level = bus_in(p) ? 0u : 1u;       /* asserted = low on the cable */
    return ACK_OKAY;
}

static void start_read(void)
{
    if (clen < 8 || clen > 12) {
        reply(CMD_READ_FLUX, ACK_BAD_COMMAND, NULL, 0);
        return;
    }
    if (unit < 0) {
        reply(CMD_READ_FLUX, ACK_NO_UNIT, NULL, 0);
        return;
    }
    const uint32_t ticks = get32(&cbuf[2]);
    const uint32_t max_index = get16(&cbuf[6]);
    uint32_t revs;
    if (max_index > 0) {
        revs = (max_index > 1u) ? max_index - 1u : 1u;     /* stream starts at an index */
    } else {
        revs = ticks / (GW_SAMPLE_FREQ / 5u) + 1u;         /* 200 ms per revolution */
    }
    if (revs > REVOLUTIONS_STREAM) {
        revs = REVOLUTIONS_STREAM;
    }
    const int ret = ufi_capture_start((uint8_t)cyl[unit], head, (uint8_t)revs, 0);
    if (ret != UFI_OK) {
        flux_status = ack_of(ret);
        reply(CMD_READ_FLUX, flux_status, NULL, 0);
        return;
    }
    flux_status = ACK_OKAY;
    reply(CMD_READ_FLUX, ACK_OKAY, NULL, 0);
    ufi_stream_begin_gw();
    state = ST_READ;
}

void ufi_gw_read_done(int result)
{
    flux_status = ack_of(result);
    if (state == ST_READ) {
        state = ST_CMD;
    }
    last_cmd_ms = HAL_GetTick();
}

static void start_write(void)
{
    if (clen < 4 || clen > 8) {
        reply(CMD_WRITE_FLUX, ACK_BAD_COMMAND, NULL, 0);
        return;
    }
    if (clen == 8 && get32(&cbuf[4]) != 0u) {
        reply(CMD_WRITE_FLUX, ACK_BAD_COMMAND, NULL, 0);    /* hard-sectored: not supported */
        return;
    }
    if (unit < 0) {
        reply(CMD_WRITE_FLUX, ACK_NO_UNIT, NULL, 0);
        return;
    }
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        reply(CMD_WRITE_FLUX, ACK_WRPROT, NULL, 0);
        return;
    }
    w_count = w_space = 0;
    w_overflow = w_bad = false;
    w_have = w_need = 0;
    reply(CMD_WRITE_FLUX, ACK_OKAY, NULL, 0);
    state = ST_WDATA;
}

static void do_erase(void)
{
    if (clen != 6) {
        reply(CMD_ERASE_FLUX, ACK_BAD_COMMAND, NULL, 0);
        return;
    }
    if (unit < 0) {
        reply(CMD_ERASE_FLUX, ACK_NO_UNIT, NULL, 0);
        return;
    }
    if (ufi_board_write_locked() || ufi_drive_write_protected()) {
        reply(CMD_ERASE_FLUX, ACK_WRPROT, NULL, 0);
        return;
    }
    reply(CMD_ERASE_FLUX, ACK_OKAY, NULL, 0);
    flux_status = ack_of(ufi_erase_track((uint8_t)cyl[unit], head));   /* one revolution */
    send_byte(flux_status);
}

static void process_command(void)
{
    const uint8_t cmd = cbuf[0];
    uint8_t ack = ACK_OKAY;

    ufi_board_activity();               /* motor idle timer */
    last_cmd_ms = HAL_GetTick();
    watchdog_armed = true;

    if (ufi_dump_active()) {            /* stand-alone dump owns the drive */
        reply(cmd, ACK_BAD_COMMAND, NULL, 0);
        return;
    }

    switch (cmd) {
        case CMD_GET_INFO:
            cmd_get_info();
            return;
        case CMD_SEEK:
            if (clen == 3) {
                ack = do_seek((int8_t)cbuf[2]);
            } else if (clen == 4) {
                ack = do_seek((int16_t)get16(&cbuf[2]));
            } else {
                ack = ACK_BAD_COMMAND;
            }
            break;
        case CMD_HEAD:
            if (clen != 3 || cbuf[2] > 1u) {
                ack = ACK_BAD_COMMAND;
            } else {
                head = cbuf[2];
                ufi_drive_select_side(head);
            }
            break;
        case CMD_SET_PARAMS:
            if (clen < 3 || cbuf[2] != 0u || clen > 3u + sizeof(delays)) {
                ack = ACK_BAD_COMMAND;
            } else {
                delays_from_timing();
                memcpy(&delays, &cbuf[3], clen - 3u);   /* little-endian u16 fields */
                delays_to_timing();
            }
            break;
        case CMD_GET_PARAMS:
            if (clen != 4 || cbuf[2] != 0u || cbuf[3] > sizeof(delays)) {
                ack = ACK_BAD_COMMAND;
            } else {
                delays_from_timing();
                reply(cmd, ACK_OKAY, (const uint8_t*)&delays, cbuf[3]);
                return;
            }
            break;
        case CMD_MOTOR:
            ack = (clen != 4 || cbuf[3] > 1u) ? ACK_BAD_COMMAND : do_motor(cbuf[2], cbuf[3] != 0u);
            break;
        case CMD_READ_FLUX:
            start_read();
            return;
        case CMD_WRITE_FLUX:
            start_write();
            return;
        case CMD_GET_FLUX_STATUS:
            ack = (clen != 2) ? ACK_BAD_COMMAND : flux_status;
            break;
        case CMD_SELECT:
            if (clen != 3) {
                ack = ACK_BAD_COMMAND;
            } else if (bus == BUS_NONE) {
                ack = ACK_NO_BUS;
            } else if (unit_drive(cbuf[2]) == DRIVE_NONE) {
                ack = ACK_BAD_UNIT;
            } else {
                ufi_drive_select(unit_drive(cbuf[2]));
                unit = (int8_t)cbuf[2];
                ufi_drive_select_side(head);
            }
            break;
        case CMD_DESELECT:
            ufi_drive_select(DRIVE_NONE);
            unit = -1;
            break;
        case CMD_SET_BUS_TYPE:
            if (clen != 3 || cbuf[2] > BUS_SHUGART) {
                ack = ACK_BAD_COMMAND;
            } else if (cbuf[2] != bus) {
                drives_off();
                bus = cbuf[2];
            }
            break;
        case CMD_SET_PIN:
            if (clen != 4 || cbuf[3] > 1u) {
                ack = ACK_BAD_COMMAND;
            } else if (cbuf[2] == 2u) {
                ufi_drive_density_line(cbuf[3] == 0u);  /* level 0 = line asserted */
            } else {
                ack = ACK_BAD_PIN;
            }
            break;
        case CMD_GET_PIN: {
            uint8_t level = 0;
            ack = (clen != 3) ? ACK_BAD_COMMAND : get_pin(cbuf[2], &level);
            reply(cmd, ack, &level, ack == ACK_OKAY ? 1u : 0u);
            return;
        }
        case CMD_RESET:
            if (clen != 2) {
                ack = ACK_BAD_COMMAND;
            } else {
                power_on_state();
            }
            break;
        case CMD_ERASE_FLUX:
            do_erase();
            return;
        default:                        /* update, mode switch, test mode, sink/source, ... */
            ack = ACK_BAD_COMMAND;
            break;
    }
    reply(cmd, ack, NULL, 0);
}

/* ============================================================================
 * WRITE STREAM DECODER (host flux code -> u32 deltas at 275 MHz in the flux store)
 * ============================================================================ */

static void w_put(uint32_t gw_ticks)
{
    uint32_t words;
    uint32_t* store = ufi_flux_store(&words);
    if (w_count >= words) {
        w_overflow = true;
        return;
    }
    store[w_count++] = gw_ticks << GW_TICK_SHIFT;
}

static uint32_t n28(const uint8_t* b)
{
    return ((uint32_t)(b[0] & 0xFEu) >> 1) | ((uint32_t)(b[1] & 0xFEu) << 6) |
           ((uint32_t)(b[2] & 0xFEu) << 13) | ((uint32_t)(b[3] & 0xFEu) << 20);
}

static void write_finish(void)
{
    int ret;
    if (w_overflow) {
        ret = UFI_ERR_BUFFER_FULL;
    } else if (w_bad || w_count < 3u) {
        ret = UFI_ERR_DMA;
    } else {
        ufi_write_set_precomp(false);   /* gw precompensates on the host */
        ret = ufi_write_local((uint8_t)cyl[unit], head, w_count);
        ufi_write_set_precomp(true);
    }
    flux_status = w_overflow ? ACK_OUT_OF_SRAM :
                  (ret == UFI_OK) ? ACK_OKAY :
                  (ret == UFI_ERR_WRITE_PROT) ? ACK_WRPROT :
                  (ret == UFI_ERR_NO_INDEX) ? ACK_NO_INDEX : ACK_FLUX_UNDERFLOW;
    send_byte(flux_status);             /* the host waits for this before GET_FLUX_STATUS */
    state = ST_CMD;
    last_cmd_ms = HAL_GetTick();
}

/* one byte of the write stream; returns false at end of stream */
static bool w_byte(uint8_t b)
{
    if (w_need) {                       /* multi-byte code in progress */
        w_code[w_have++] = b;
        if (w_have < w_need) {
            return true;
        }
        w_need = 0;
        if (w_code[0] == 0xFFu) {
            const uint32_t n = n28(&w_code[2]);
            if (w_code[1] == FLUXOP_SPACE) {
                w_space += n;
            } else if (w_code[1] == FLUXOP_ASTABLE && n > 0u) {
                /* no-flux area: regular transitions at period n over the preceding space */
                uint32_t left = w_space;
                w_space = 0;
                while (left >= 2u * n) {
                    w_put(n);
                    left -= n;
                }
                if (left) {
                    w_put(left);
                }
            } else {
                w_bad = true;           /* index ops are not allowed in a write stream */
            }
        } else {
            w_put(w_space + 250u + (uint32_t)(w_code[0] - 250u) * 255u + w_code[1] - 1u);
            w_space = 0;
        }
        return true;
    }
    if (b == 0u) {
        return false;                   /* end of stream */
    }
    if (b < 250u) {
        w_put(w_space + b);
        w_space = 0;
        return true;
    }
    w_code[0] = b;
    w_have = 1;
    w_need = (b == 0xFFu) ? 6u : 2u;
    return true;
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

static void reset_parser(void)
{
    clen = 0;
    state = ST_CMD;
}

void ufi_gw_begin(void)
{
    rx_tail = rx_head = 0;
    rx_paused = clear_req = false;
    reset_parser();
    drive_timing_t t = ufi_drive_get_timing();
    t.double_step = 0;                  /* gw steps by itself */
    ufi_drive_set_timing(&t);
    power_on_state();
    flux_status = ACK_OKAY;
    watchdog_armed = false;
}

void ufi_gw_end(void)
{
    if (state == ST_READ) {
        ufi_capture_abort();
    }
    reset_parser();
    drives_off();
}

void ufi_gw_service(void)
{
    if (clear_req) {                    /* host: SET_LINE_CODING 10000 baud */
        if (state == ST_READ) {
            ufi_capture_abort();
            g_capture.state = CAPTURE_IDLE;
        }
        reset_parser();
        rx_tail = rx_head;
        clear_req = false;
        if (rx_paused) {
            rx_paused = false;
            USBD_CDC_ReceivePacket(&hUsbDevice);
        }
        return;
    }

    if (state == ST_READ) {
        last_cmd_ms = HAL_GetTick();    /* the read keeps the watchdog quiet */
        return;                         /* ufi_stream.c sends; ends via ufi_gw_read_done() */
    }

    while (rx_tail != rx_head) {
        const uint8_t b = rx[rx_tail];
        rx_tail = (rx_tail + 1u) & (RX_SIZE - 1u);
        if (state == ST_WDATA) {
            if (!w_byte(b)) {
                rx_resume_if_room();
                write_finish();         /* blocking: index wait + one revolution */
                return;
            }
            continue;
        }
        cbuf[clen++] = b;
        if (clen >= 2u) {
            const uint8_t need = cbuf[1];
            if (need < 2u || need > sizeof(cbuf)) {
                reply(cbuf[0], ACK_BAD_COMMAND, NULL, 0);   /* garbage: resynchronise */
                clen = 0;
                continue;
            }
            if (clen == need) {
                process_command();
                clen = 0;
                if (state != ST_CMD) {
                    break;              /* read started / write data follows */
                }
            }
        }
    }
    rx_resume_if_room();

    /* watchdog: host gone quiet with a drive selected -> motors off, deselect */
    if (watchdog_armed && state == ST_CMD && delays.watchdog &&
        HAL_GetTick() - last_cmd_ms > delays.watchdog) {
        watchdog_armed = false;
        drives_off();
    }
}
