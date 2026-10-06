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
#include <string.h>

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

// String Descriptors
const uint8_t* USBD_Manufacturer_String = (uint8_t*)"UFT Project";
const uint8_t* USBD_Product_String = (uint8_t*)"UFI Flux Engine";
const uint8_t* USBD_Serial_String = (uint8_t*)"UFI-001";

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

/* CDC class keeps TxState in its handle (1 = transfer in progress) */
static uint32_t cdc_tx_busy(void) {
    USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef*)hUsbDevice.pClassData;
    return hcdc ? hcdc->TxState : 0;
}

static bool cdc_wait_idle(void) {
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

/* ============================================================================
 * USB CALLBACK (aufgerufen von usbd_cdc_if.c, Interrupt-Kontext)
 * ============================================================================ */

void ufi_usb_receive_callback(uint8_t* buf, uint32_t len) {
    // While a write is being prepared, every OUT packet is flux data
    if (ufi_write_get_state() == WRITE_RECEIVING) {
        ufi_write_receive_chunk(buf, len);
        return;
    }
    if (len > 0 && len <= sizeof(cmd_buffer) && !cmd_ready) {
        memcpy(cmd_buffer, buf, len);
        cmd_len = len;
        cmd_ready = 1;
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
    if (!cmd_ready) {
        return 0;
    }

    const uint8_t cmd = cmd_buffer[0];

    switch (cmd) {
        case UFI_CMD_NOP:
            reply(cmd, 0, NULL, 0);
            break;

        case UFI_CMD_GET_INFO: {
            static const char info[] = "UFI Flux Engine v1.1\0" BOARD_NAME "\0STM32H723\0";
            reply(cmd, 0, info, sizeof(info));
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
            if (cmd_len >= 1 + sizeof(drive_timing_t)) {
                drive_timing_t t;
                memcpy(&t, &cmd_buffer[1], sizeof(t));
                ufi_drive_set_timing(&t);
            }
            const drive_timing_t cur = ufi_drive_get_timing();
            reply(cmd, 0, &cur, sizeof(cur));
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
            // [CMD, track, side, revolutions]; flux packets follow from ufi_main_loop
            uint8_t revolutions = cmd_buffer[3];
            if (revolutions == 0) revolutions = 1;
            if (revolutions > REVOLUTIONS_BUFFER) revolutions = REVOLUTIONS_BUFFER;
            reply(cmd, st(ufi_capture_start(cmd_buffer[1], cmd_buffer[2], revolutions)), NULL, 0);
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

        case UFI_CMD_ERASE_TRACK:
            reply(cmd, st(ufi_erase_track(cmd_buffer[1], cmd_buffer[2])), NULL, 0);
            break;

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
