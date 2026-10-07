/**
 * UFI Flux Engine - USB Communication
 *
 * USB CDC (Full Speed, internal PHY) for commands and flux data.
 * Every reply = ufi_response_header_t + payload, sent blocking from static memory.
 */

#include "ufi_firmware.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc_if.h"
#include "usbd_msc.h"
#include <string.h>

extern USBD_StorageTypeDef ufi_msc_fops;    /* ufi_msc.c: SD NAND */
extern USBD_StorageTypeDef ufi_floppy_fops; /* ufi_msc.c: disk in drive A */
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* ============================================================================
 * USB DESCRIPTORS
 * ============================================================================ */

#define UFI_VID             0x1209  // pid.codes VID
#define UFI_PID             0x4F54  // "OT" für Open Tool

// Device Descriptor
__ALIGN_BEGIN uint8_t USBD_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,                       // bLength
    USB_DESC_TYPE_DEVICE,       // bDescriptorType
    0x00, 0x02,                 // bcdUSB = 2.00
    0x02,                       // bDeviceClass (CDC)
    0x02,                       // bDeviceSubClass
    0x00,                       // bDeviceProtocol
    USB_MAX_EP0_SIZE,           // bMaxPacketSize0
    LOBYTE(UFI_VID), HIBYTE(UFI_VID),  // idVendor
    LOBYTE(UFI_PID), HIBYTE(UFI_PID),  // idProduct
    0x00, 0x01,                 // bcdDevice = 1.00
    USBD_IDX_MFC_STR,           // iManufacturer
    USBD_IDX_PRODUCT_STR,       // iProduct
    USBD_IDX_SERIAL_STR,        // iSerialNumber
    USBD_MAX_NUM_CONFIGURATION  // bNumConfigurations
};

// String Descriptors (serial number: STM32 unique ID, usbd_desc.c)
const uint8_t* USBD_Manufacturer_String = (uint8_t*)"UFT Project";
const uint8_t* USBD_Product_String = (uint8_t*)"UFI Flux Engine";

/* ============================================================================
 * USB HANDLES / BUFFERS
 * ============================================================================ */

USBD_HandleTypeDef hUsbDevice;

/* Flux data is sent straight from the flux store in chunks (no copy buffer).
 * One CDC transfer may span up to 1023 FS packets; 32 KB keeps well below that. */
#define USB_TX_CHUNK        (32 * 1024)
#define USB_TX_TIMEOUT_MS   500
#define REPLY_MAX_PAYLOAD   64

// Command Buffer (filled from the CDC receive callback)
static uint8_t cmd_buffer[64];
static volatile uint8_t cmd_ready = 0;
static volatile uint32_t cmd_len = 0;

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/* v0.6: the device is either CDC (commands) or USB mass storage (SD NAND or the floppy
 * disk, ufi_msc.c); msc_mode = no CDC interface */
static bool msc_mode;
static uint8_t usb_mode = UFI_USB_FLUX;

/* CDC class keeps TxState in its handle (1 = transfer in progress) */
static uint32_t cdc_tx_busy(void) {
    if (msc_mode) {
        return 0;                           /* class data is the MSC handle */
    }
    USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef*)hUsbDevice.pClassData;
    return hcdc ? hcdc->TxState : 0;
}

static bool cdc_wait_idle(void) {
    if (msc_mode) {
        return false;                       /* no CDC interface: nothing can be sent */
    }
    const uint32_t start = HAL_GetTick();
    while (cdc_tx_busy() != 0) {
        if (HAL_GetTick() - start > USB_TX_TIMEOUT_MS) {
            return false;
        }
    }
    return true;
}

/* Blocking transmit of an arbitrary-length buffer; p must stay valid until return */
static int usb_tx_blocking(const uint8_t* p, uint32_t len) {
    while (len > 0) {
        const uint32_t n = (len > USB_TX_CHUNK) ? USB_TX_CHUNK : len;
        if (!cdc_wait_idle()) {
            return UFI_ERR_USB;
        }
        USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)p, n);
        if (USBD_CDC_TransmitPacket(&hUsbDevice) != USBD_OK) {
            return UFI_ERR_USB;
        }
        p += n;
        len -= n;
    }
    return cdc_wait_idle() ? UFI_OK : UFI_ERR_USB;
}

int ufi_usb_tx_blocking(const uint8_t* p, uint32_t len) {
    return usb_tx_blocking(p, len);
}

bool ufi_usb_tx_idle(void) {
    return cdc_tx_busy() == 0;
}

/* Start one transfer (<= USB_TX_CHUNK) and return; the streamer overlaps encoding with it */
int ufi_usb_tx_start(const uint8_t* p, uint32_t len) {
    if (len == 0 || len > USB_TX_CHUNK) {
        return UFI_ERR_USB;
    }
    if (msc_mode) {
        return UFI_ERR_USB;
    }
    if (cdc_tx_busy() != 0) {
        return UFI_ERR_BUSY;
    }
    USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)p, len);
    return (USBD_CDC_TransmitPacket(&hUsbDevice) == USBD_OK) ? UFI_OK : UFI_ERR_USB;
}

/* Reply = header + optional payload in one transfer (static: outlives the transfer) */
static int reply(uint8_t cmd, uint8_t status, const void* payload, uint16_t len) {
    static uint8_t buf[sizeof(ufi_response_header_t) + REPLY_MAX_PAYLOAD];
    if (len > REPLY_MAX_PAYLOAD) {
        len = 0;
        status = 0xFE;
    }
    ufi_response_header_t hdr = {.command = cmd, .status = status, .length = len};
    memcpy(buf, &hdr, sizeof(hdr));
    if (len) {
        memcpy(buf + sizeof(hdr), payload, len);
    }
    return usb_tx_blocking(buf, sizeof(hdr) + len);
}

/* Append s plus its NUL to a reply text of max REPLY_MAX_PAYLOAD bytes (cut if too long) */
static uint16_t str_add(char* buf, uint16_t n, const char* s) {
    while (*s && n < REPLY_MAX_PAYLOAD - 1u) {
        buf[n++] = *s++;
    }
    buf[n++] = '\0';
    return n;
}

#ifndef UFI_FW_VERSION
#define UFI_FW_VERSION "1.8"
#endif
#ifndef UFI_GIT_REV
#define UFI_GIT_REV "dev"
#endif

/* UFI error code (negative) -> wire status byte */
static uint8_t st(int ret) {
    return (ret < 0) ? (uint8_t)(-ret) : 0;
}

/* ============================================================================
 * USB INITIALISIERUNG
 * ============================================================================ */

void ufi_usb_init(void) {
    /* PA11/PA12 AF10, OTG_HS clock and NVIC: HAL_PCD_MspInit() in usbd_conf.c */
    USBD_Init(&hUsbDevice, &HS_Desc, 0);
    USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
    USBD_CDC_RegisterInterface(&hUsbDevice, &USBD_Interface_fops_HS);
    USBD_Start(&hUsbDevice);
}

bool ufi_usb_msc_active(void) {
    return msc_mode;
}

uint8_t ufi_usb_get_mode(void) {
    return usb_mode;
}

int ufi_usb_set_msc(bool on) {
    return ufi_usb_set_mode(on ? UFI_USB_SD : ufi_usb_flux_mode());
}

/* Flux personality for the switch in the middle: UFI protocol or Greaseweazle (UFI.CFG) */
uint8_t ufi_usb_flux_mode(void) {
    return ufi_config_protocol_gw() ? UFI_USB_GW : UFI_USB_FLUX;
}

/* Re-enumerate as flux device (CDC), SD drive or USB floppy (both USB mass storage).
 * The soft disconnect is held 200 ms so the host sees the device go away and reads the
 * new descriptors.  USB floppy: the USB interrupt stays off and ufi_usb_poll() runs the
 * stack from the main loop, so the blocking disk accesses in the storage callbacks
 * delay no interrupt. */
int ufi_usb_set_mode(uint8_t mode) {
    if (mode == usb_mode) {
        return UFI_OK;
    }
    if (mode > UFI_USB_GW) {
        return UFI_ERR_NOT_IMPL;
    }
    const bool cdc = (mode == UFI_USB_FLUX || mode == UFI_USB_GW);
    if (!cdc && ufi_dump_active()) {
        return UFI_ERR_BUSY;
    }
    if (mode == UFI_USB_SD && ufi_sd_init() != UFI_OK) {
        return UFI_ERR_STORAGE;
    }
    if (usb_mode == UFI_USB_FLOPPY) {
        ufi_floppy_end();                   /* write back cached tracks first */
    }
    if (usb_mode == UFI_USB_GW) {
        ufi_gw_end();                       /* abort a read, motors off */
    }
    USBD_Stop(&hUsbDevice);
    USBD_DeInit(&hUsbDevice);
    HAL_Delay(200);
    usb_mode = mode;
    msc_mode = !cdc;
    cmd_ready = 0;
    if (mode == UFI_USB_GW) {
        ufi_gw_begin();
    }
    usbd_desc_set_mode(mode);
    USBD_Init(&hUsbDevice, &HS_Desc, 0);
    if (cdc) {
        USBD_RegisterClass(&hUsbDevice, &USBD_CDC);
        USBD_CDC_RegisterInterface(&hUsbDevice, &USBD_Interface_fops_HS);
    } else {
        USBD_RegisterClass(&hUsbDevice, &USBD_MSC);
        USBD_MSC_RegisterStorage(&hUsbDevice, mode == UFI_USB_SD ? &ufi_msc_fops : &ufi_floppy_fops);
    }
    if (mode == UFI_USB_FLOPPY) {
        ufi_floppy_begin();
        HAL_NVIC_DisableIRQ(OTG_HS_IRQn);   /* polled: ufi_usb_poll() */
    }
    USBD_Start(&hUsbDevice);
    led_set(&PIN_LED_USB, msc_mode);        /* USB LED steady = mass storage (SD or floppy) */
    return UFI_OK;
}

/* Main loop: in USB floppy mode the USB stack runs here instead of the interrupt */
void ufi_usb_poll(void) {
    if (usb_mode == UFI_USB_FLOPPY) {
        HAL_PCD_IRQHandler(&hpcd_USB_OTG_HS);
        NVIC_ClearPendingIRQ(OTG_HS_IRQn);
    }
}

/* ============================================================================
 * USB CALLBACK (aufgerufen von usbd_cdc_if.c, Interrupt-Kontext)
 * ============================================================================ */

/* Returns false if the OUT endpoint must stay paused (Greaseweazle flow control) */
bool ufi_usb_receive_callback(uint8_t* buf, uint32_t len) {
    if (usb_mode == UFI_USB_GW) {
        return ufi_gw_rx(buf, len);
    }
    // While a write is being prepared, every OUT packet is flux data
    if (ufi_write_get_state() == WRITE_RECEIVING) {
        ufi_write_receive_chunk(buf, len);
        return true;
    }
    if (len > 0 && len <= sizeof(cmd_buffer) && !cmd_ready) {
        memcpy(cmd_buffer, buf, len);
        cmd_len = len;
        cmd_ready = 1;
    }
    return true;
}

/* CDC SET_LINE_CODING (interrupt context): the Greaseweazle tools clear the channel by
 * setting 10000 baud */
void ufi_usb_line_coding(uint32_t baud) {
    if (usb_mode == UFI_USB_GW && baud == 10000u) {
        ufi_gw_clear_comms();
    }
}

/* ============================================================================
 * FLUX-DATEN SENDEN
 * ============================================================================ */

/* Packet = flux_packet_header_t followed by sample_count x uint32 timestamps */
int ufi_usb_send_flux(flux_packet_header_t* header, flux_sample_t* data) {
    /* {UFI_EVT_FLUX, 0, 12} + flux header in one transfer, then the samples */
    static uint8_t msg[sizeof(ufi_response_header_t) + sizeof(flux_packet_header_t)];
    const ufi_response_header_t rh = {.command = UFI_EVT_FLUX, .status = 0,
                                      .length = sizeof(flux_packet_header_t)};
    memcpy(msg, &rh, sizeof(rh));
    memcpy(msg + sizeof(rh), header, sizeof(*header));
    int ret = usb_tx_blocking(msg, sizeof(msg));
    if (ret == UFI_OK) {
        ret = usb_tx_blocking((const uint8_t*)data, header->sample_count * sizeof(flux_sample_t));
    }
    return ret;
}

/* End of a capture: status = error code of the capture (0 = all revolutions) */
int ufi_usb_send_read_done(int result, uint8_t revolutions) {
    return reply(UFI_EVT_READ_DONE, st(result), &revolutions, 1);
}

/* Unsolicited completion notice (e.g. write finished) */
int ufi_usb_send_event(uint8_t command, int result) {
    return reply(command, st(result), NULL, 0);
}

/* ============================================================================
 * BEFEHLE VERARBEITEN
 * ============================================================================ */

static void cmd_debug_gpio(uint8_t cmd) {
    switch (cmd_buffer[1]) {
        case 0: {
            gpio_status_t s = ufi_debug_gpio_read();
            reply(cmd, 0, &s, sizeof(s));
            break;
        }
        case 1:
            reply(cmd, st(ufi_debug_gpio_set(cmd_buffer[2], cmd_buffer[3])), NULL, 0);
            break;
        case 2:
            ufi_debug_led_test();
            reply(cmd, 0, NULL, 0);
            break;
        case 3: {
            uint8_t r = ufi_debug_selftest();
            reply(cmd, 0, &r, 1);
            break;
        }
        default:
            reply(cmd, 0xFF, NULL, 0);
            break;
    }
}

static void cmd_debug_timer(uint8_t cmd) {
    switch (cmd_buffer[1]) {
        case 0: {
            timer_status_t s = ufi_debug_timer_read();
            reply(cmd, 0, &s, sizeof(s));
            break;
        }
        case 1: {
            uint32_t ticks = ufi_debug_measure_index();
            reply(cmd, 0, &ticks, sizeof(ticks));
            break;
        }
        case 2: {
            uint16_t rpm = ufi_debug_measure_rpm();
            reply(cmd, 0, &rpm, sizeof(rpm));
            break;
        }
        case 3: {
            memory_info_t info = ufi_debug_memory_read();
            reply(cmd, 0, &info, sizeof(info));
            break;
        }
        default:
            reply(cmd, 0xFF, NULL, 0);
            break;
    }
}

int ufi_usb_process_command(void) {
    if (usb_mode == UFI_USB_GW) {
        ufi_gw_service();                   /* Greaseweazle protocol (ufi_gw.c) */
        return 0;
    }
    if (!cmd_ready) {
        return 0;
    }

    const uint8_t cmd = cmd_buffer[0];
    ufi_board_activity();                   /* motor idle timer */

    /* a stand-alone dump owns the drive: only status queries and abort meanwhile */
    if (ufi_dump_active() && cmd != UFI_CMD_NOP && cmd != UFI_CMD_GET_INFO &&
        cmd != UFI_CMD_GET_STATUS && cmd != UFI_CMD_BOARD_STATUS &&
        cmd != UFI_CMD_DUMP_STATUS && cmd != UFI_CMD_DUMP_ABORT) {
        reply(cmd, st(UFI_ERR_BUSY), NULL, 0);
        cmd_ready = 0;
        return 1;
    }

    switch (cmd) {
        case UFI_CMD_NOP:
            reply(cmd, 0, NULL, 0);
            break;

        case UFI_CMD_GET_INFO: {
            // NUL-separated: firmware (version, git revision, build date), board (+ revision
            // from the BOARD_ID divider), MCU, PSRAM self-test result
            char info[REPLY_MAX_PAYLOAD];
            uint16_t n = 0;
            n = str_add(info, n, "UFI Flux Engine v" UFI_FW_VERSION " " UFI_GIT_REV " " __DATE__);
            const uint16_t id = ufi_adc_mv(ADC_CH_BOARD_ID);
            n = str_add(info, n, (id + 200u > BOARD_ID_V05_MV && id < BOARD_ID_V05_MV + 200u) ? BOARD_NAME " v0.5"
                               : (id + 200u > BOARD_ID_V06_MV && id < BOARD_ID_V06_MV + 200u) ? BOARD_NAME " v0.6"
                               : BOARD_NAME " rev ?");
            n = str_add(info, n, "STM32H723");
            n = str_add(info, n, ufi_psram_result());
            reply(cmd, 0, info, n);
            break;
        }

        case UFI_CMD_GET_STATUS: {
            const drive_status_t s = ufi_drive_get_status();
            const drive_status_wire_t w = {
                .type = (uint8_t)s.type, .motor_on = s.motor_on,
                .write_protected = s.write_protected, .track0 = s.track0,
                .disk_changed = s.disk_changed, .ready = s.ready,
                .current_track = s.current_track, .current_side = s.current_side,
                .rpm = s.rpm};
            reply(cmd, 0, &w, sizeof(w));
            break;
        }

        case UFI_CMD_SELECT_DRIVE:
            reply(cmd, st(ufi_drive_select((drive_type_t)cmd_buffer[1])), NULL, 0);
            break;
        case UFI_CMD_MOTOR_ON:
            reply(cmd, st(ufi_drive_motor(true)), NULL, 0);
            break;
        case UFI_CMD_MOTOR_OFF:
            reply(cmd, st(ufi_drive_motor(false)), NULL, 0);
            break;
        case UFI_CMD_SEEK:
            reply(cmd, st(ufi_drive_seek(cmd_buffer[1])), NULL, 0);
            break;
        case UFI_CMD_RECALIBRATE:
            reply(cmd, st(ufi_drive_recalibrate()), NULL, 0);
            break;
        case UFI_CMD_SELECT_SIDE:
            reply(cmd, st(ufi_drive_select_side(cmd_buffer[1])), NULL, 0);
            break;
        case UFI_CMD_CHECK_DISK: {
            bool changed = false, present = false;
            int ret = ufi_drive_check_disk(&changed, &present);
            const uint8_t r[2] = {changed, present};
            reply(cmd, st(ret), r, ret == UFI_OK ? 2 : 0);
            break;
        }
        case UFI_CMD_DRIVE_TIMING: {
            // a shorter payload (older hosts: 7 fields) overwrites only the leading fields
            if (cmd_len >= 3) {
                drive_timing_t t = ufi_drive_get_timing();
                const uint32_t n = (cmd_len - 1 < sizeof(t)) ? (cmd_len - 1) & ~1u : sizeof(t);
                memcpy(&t, &cmd_buffer[1], n);
                ufi_drive_set_timing(&t);
            }
            const drive_timing_t cur = ufi_drive_get_timing();
            reply(cmd, 0, &cur, sizeof(cur));
            break;
        }
        case UFI_CMD_SET_LINES:             // [density, drate]; meaning is drive dependent
            ufi_drive_density_line(cmd_buffer[1] != 0);
            bus_out(&PIN_FDD_DRATE, cmd_buffer[2] != 0);
            reply(cmd, 0, NULL, 0);
            break;
        case UFI_CMD_BOARD_STATUS: {        // [mask] optional: bit0 FDD_5V, bit1 FDD_12V
            if (cmd_len >= 2) {
                ufi_board_power(cmd_buffer[1]);
            }
            const board_status_t s = ufi_board_status();
            reply(cmd, 0, &s, sizeof(s));
            break;
        }
        case UFI_CMD_PROBE_TRACKS: {
            uint8_t highest = 0;
            int ret = ufi_drive_probe_tracks(&highest);
            reply(cmd, st(ret), &highest, ret == UFI_OK ? 1 : 0);
            break;
        }
        case UFI_CMD_SEEK_TEST:             // [a, b, cycles]; reply when done (blocking)
            reply(cmd, st(ufi_drive_seek_test(cmd_buffer[1], cmd_buffer[2], cmd_buffer[3])), NULL, 0);
            break;
        case UFI_CMD_WRITE_PATTERN:         // [track, side, interval_ns u16, duration_ms u16]
            reply(cmd, st(ufi_write_pattern(cmd_buffer[1], cmd_buffer[2],
                                            (uint16_t)(cmd_buffer[3] | (cmd_buffer[4] << 8)),
                                            (uint16_t)(cmd_buffer[5] | (cmd_buffer[6] << 8)))), NULL, 0);
            break;
        case UFI_CMD_SD_INFO: {
            sd_info_t info = {0};
            int ret = ufi_sd_info(&info);
            reply(cmd, st(ret), &info, sizeof(info));
            break;
        }
        case UFI_CMD_USB_POWER: {
            usb_power_t p = {0};
            int ret = ufi_usb_power(&p);
            reply(cmd, st(ret), &p, ret == UFI_OK ? sizeof(p) : 0);
            break;
        }
        case UFI_CMD_AMIGA_ID: {
            uint32_t id = 0;
            int ret = ufi_drive_amiga_id(&id);
            reply(cmd, st(ret), &id, ret == UFI_OK ? 4 : 0);
            break;
        }

        case UFI_CMD_READ_TRACK:
        case UFI_CMD_READ_TRACK_RAW: {
            // [CMD, track, side, revolutions, flags, period_ms u16]; READ_TRACK streams
            // UFI_EVT_FLUX_STREAM while the disk turns (ufi_stream.c, up to 200 revolutions),
            // READ_TRACK_RAW sends UFI_EVT_FLUX packets after the capture (up to 20).
            // flags bit0: no index pulses, revolution boundaries every period_ms (0 = 200)
            const uint8_t max_revs = (cmd == UFI_CMD_READ_TRACK) ? REVOLUTIONS_STREAM : REVOLUTIONS_BUFFER;
            uint8_t revolutions = cmd_buffer[3];
            if (revolutions == 0) revolutions = 1;
            if (revolutions > max_revs) revolutions = max_revs;
            uint32_t period_ticks = 0;
            if (cmd_len >= 5 && (cmd_buffer[4] & 0x01u)) {
                uint32_t ms = (cmd_len >= 7) ? (uint32_t)cmd_buffer[5] | ((uint32_t)cmd_buffer[6] << 8) : 0u;
                if (ms == 0 || ms > 2000u) ms = 200u;
                period_ticks = ms * (FLUX_TIMER_FREQ / 1000u);
            }
            int ret = ufi_capture_start(cmd_buffer[1], cmd_buffer[2], revolutions, period_ticks);
            if (ret == UFI_OK && cmd == UFI_CMD_READ_TRACK) {
                ufi_stream_begin();
            }
            reply(cmd, st(ret), NULL, 0);
            break;
        }

        case UFI_CMD_ABORT_READ:
            ufi_capture_abort();
            ufi_write_abort();
            reply(cmd, 0, NULL, 0);
            break;

        case UFI_CMD_IEC_RESET:
            reply(cmd, st(ufi_iec_reset()), NULL, 0);
            break;

        case UFI_CMD_IEC_SEND:
            reply(cmd, st(ufi_iec_send_byte(cmd_buffer[1], cmd_buffer[2] != 0)), NULL, 0);
            break;

        case UFI_CMD_IEC_RECEIVE: {
            uint8_t rx[2] = {0, 0};             // payload: [byte, eoi]
            bool eoi = false;
            int ret = ufi_iec_receive_byte(&rx[0], &eoi);
            rx[1] = eoi ? 1 : 0;
            reply(cmd, st(ret), rx, (ret == UFI_OK) ? 2 : 0);
            break;
        }

        case UFI_CMD_WRITE_TRACK:
        case UFI_CMD_WRITE_TRACK_VERIFY: {
            // [CMD, track, side, flux_count (u32 LE)]; then flux_count x u32 deltas as
            // raw OUT data; completion is reported by an event with the same command
            uint32_t flux_count = (uint32_t)cmd_buffer[3] | ((uint32_t)cmd_buffer[4] << 8) |
                                  ((uint32_t)cmd_buffer[5] << 16) | ((uint32_t)cmd_buffer[6] << 24);
            int ret = ufi_write_prepare(cmd_buffer[1], cmd_buffer[2], flux_count,
                                        cmd == UFI_CMD_WRITE_TRACK_VERIFY);
            reply(cmd, st(ret), NULL, 0);
            break;
        }

        case UFI_CMD_WRITE_TRACK_C: {
            // [CMD, track, side, flux_count u32, byte_count u32, verify]; then byte_count
            // bytes in the READ_TRACK stream code; completion event = UFI_CMD_WRITE_TRACK_C
            const uint32_t flux_count = (uint32_t)cmd_buffer[3] | ((uint32_t)cmd_buffer[4] << 8) |
                                        ((uint32_t)cmd_buffer[5] << 16) | ((uint32_t)cmd_buffer[6] << 24);
            const uint32_t byte_count = (uint32_t)cmd_buffer[7] | ((uint32_t)cmd_buffer[8] << 8) |
                                        ((uint32_t)cmd_buffer[9] << 16) | ((uint32_t)cmd_buffer[10] << 24);
            int ret = ufi_write_prepare_compact(cmd_buffer[1], cmd_buffer[2], flux_count, byte_count,
                                                cmd_len >= 12 && cmd_buffer[11] != 0);
            reply(cmd, st(ret), NULL, 0);
            break;
        }

        case UFI_CMD_ERASE_TRACK:
            reply(cmd, st(ufi_erase_track(cmd_buffer[1], cmd_buffer[2])), NULL, 0);
            break;

        case UFI_CMD_DUMP_START: {          // [drive, tracks, sides, revs] optional
            int ret;
            if (cmd_len >= 1 + sizeof(dump_config_t)) {
                dump_config_t c;
                memcpy(&c, &cmd_buffer[1], sizeof(c));
                ret = ufi_dump_start(&c);
            } else {
                ret = ufi_dump_start(NULL);
            }
            reply(cmd, st(ret), NULL, 0);
            break;
        }
        case UFI_CMD_DUMP_STATUS: {
            const dump_status_t s = ufi_dump_status();
            reply(cmd, 0, &s, sizeof(s));
            break;
        }
        case UFI_CMD_DUMP_ABORT:
            ufi_dump_abort();
            reply(cmd, 0, NULL, 0);
            break;
        case UFI_CMD_COPY_START:            // progress / result via DUMP_STATUS
            reply(cmd, st(ufi_copy_start()), NULL, 0);
            break;
        case UFI_CMD_USB_MSC: {             // [mode] optional: 1 SD drive (default), 2 USB floppy
            const uint8_t mode = (cmd_len >= 2) ? cmd_buffer[1] : UFI_USB_SD;
            int ret = (mode == UFI_USB_SD || mode == UFI_USB_FLOPPY) ? UFI_OK : UFI_ERR_NOT_IMPL;
            if (ret == UFI_OK && ufi_dump_active()) {
                ret = UFI_ERR_BUSY;
            }
            if (ret == UFI_OK && mode == UFI_USB_SD && ufi_sd_init() != UFI_OK) {
                ret = UFI_ERR_STORAGE;
            }
            reply(cmd, st(ret), NULL, 0);   // reply first, the CDC interface goes away
            if (ret == UFI_OK) {
                HAL_Delay(20);              // let the host read the reply
                ufi_usb_set_mode(mode);
            }
            break;
        }

        case UFI_CMD_DEBUG_GPIO:
            cmd_debug_gpio(cmd);
            break;
        case UFI_CMD_DEBUG_TIMER:
            cmd_debug_timer(cmd);
            break;

        case UFI_CMD_RESET:
            reply(cmd, 0, NULL, 0);
            NVIC_SystemReset();
            break;

        case UFI_CMD_BOOTLOADER:
            // reply first, then reset into the ROM USB-DFU bootloader
            reply(cmd, 0, NULL, 0);
            HAL_Delay(20);                  // let the host read the reply
            ufi_request_bootloader();
            break;

        default:
            reply(cmd, 0xFF, NULL, 0);  // Unbekannter Befehl
            break;
    }

    cmd_ready = 0;  // release the command buffer only after it was fully used
    return 1;
}
