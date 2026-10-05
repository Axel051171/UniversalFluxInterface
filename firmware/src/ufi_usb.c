/**
 * UFI Flux Engine - USB Communication
 * 
 * USB High-Speed Bulk Transfer für Flux-Daten zum CM5
 */

#include "ufi_fixes.h"  /* MUSS zuerst für Error Codes! */
#include "ufi_firmware.h"
#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_cdc_if.h"
#include <string.h>  /* für memcpy */

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
 * USB HANDLES
 * ============================================================================ */

USBD_HandleTypeDef hUsbDevice;

/* Flux data is sent straight from the flux store in chunks (no copy buffer).
 * One CDC transfer may span up to 1023 FS packets; 32 KB keeps well below that. */
#define USB_TX_CHUNK        (32 * 1024)
#define USB_TX_TIMEOUT_MS   500

/* ============================================================================
 * HELPER FUNCTIONS
 * ============================================================================ */

/* CDC class keeps TxState in its handle (1 = transfer in progress) */
static uint32_t USBD_CDC_GetTxState(USBD_HandleTypeDef* pdev) {
    USBD_CDC_HandleTypeDef* hcdc = (USBD_CDC_HandleTypeDef*)pdev->pClassData;
    return hcdc ? hcdc->TxState : 0;
}

// ⚠️ FIX #8: Warte auf TX Complete statt HAL_Delay
static inline void usb_wait_tx_complete(void) {
    const uint32_t start = HAL_GetTick();
    while (USBD_CDC_GetTxState(&hUsbDevice) != 0) {
        if (HAL_GetTick() - start > 50) break;
    }
}

/* Blocking transmit of an arbitrary-length buffer; p must stay valid until return */
static int usb_tx_blocking(const uint8_t* p, uint32_t len) {
    while (len > 0) {
        const uint32_t n = (len > USB_TX_CHUNK) ? USB_TX_CHUNK : len;
        const uint32_t start = HAL_GetTick();
        while (USBD_CDC_GetTxState(&hUsbDevice) != 0) {
            if (HAL_GetTick() - start > USB_TX_TIMEOUT_MS) {
                return UFI_ERR_USB;
            }
        }
        USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)p, n);
        if (USBD_CDC_TransmitPacket(&hUsbDevice) != USBD_OK) {
            return UFI_ERR_USB;
        }
        p += n;
        len -= n;
    }
    const uint32_t start = HAL_GetTick();
    while (USBD_CDC_GetTxState(&hUsbDevice) != 0) {
        if (HAL_GetTick() - start > USB_TX_TIMEOUT_MS) {
            return UFI_ERR_USB;
        }
    }
    return UFI_OK;
}

// Command Buffer
static uint8_t cmd_buffer[64];
static volatile uint8_t cmd_ready = 0;

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

/* USB Interrupt ist in stm32h7xx_it.c */

/* ============================================================================
 * USB CALLBACK (aufgerufen von usbd_cdc_if.c)
 * ============================================================================ */

// Daten empfangen (von CM5)
void ufi_usb_receive_callback(uint8_t* buf, uint32_t len) {
    // Befehl in Command-Buffer kopieren
    if (len <= sizeof(cmd_buffer)) {
        memcpy(cmd_buffer, buf, len);
        cmd_ready = 1;
    }
}

/* ============================================================================
 * FLUX-DATEN SENDEN
 * ============================================================================ */

/* Packet = flux_packet_header_t followed by sample_count x uint32 timestamps */
int ufi_usb_send_flux(flux_packet_header_t* header, flux_sample_t* data) {
    static flux_packet_header_t hdr;    /* must outlive the transfer */
    hdr = *header;
    int ret = usb_tx_blocking((const uint8_t*)&hdr, sizeof(hdr));
    if (ret == UFI_OK) {
        ret = usb_tx_blocking((const uint8_t*)data, hdr.sample_count * sizeof(flux_sample_t));
    }
    return ret;
}

/* ============================================================================
 * BEFEHLE VERARBEITEN
 * ============================================================================ */

int ufi_usb_process_command(void) {
    if (!cmd_ready) {
        return 0;
    }
    
    cmd_ready = 0;
    
    uint8_t cmd = cmd_buffer[0];
    ufi_response_header_t response = {
        .command = cmd,
        .status = 0,
        .length = 0
    };
    
    switch (cmd) {
        case UFI_CMD_NOP:
            // Nichts tun
            break;
            
        case UFI_CMD_GET_INFO: {
            // Geräte-Info senden
            static const char info[] = "UFI Flux Engine v1.0\0STM32H723\0";
            response.length = sizeof(info);
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            usb_wait_tx_complete();
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)info, sizeof(info));
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_GET_STATUS: {
            // Aktuellen Status senden
            drive_status_t status = ufi_drive_get_status();
            response.length = sizeof(status);
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            usb_wait_tx_complete();
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&status, sizeof(status));
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_SELECT_DRIVE: {
            drive_type_t type = (drive_type_t)cmd_buffer[1];
            if (ufi_drive_select(type) != 0) {
                response.status = 1;
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_MOTOR_ON:
            if (ufi_drive_motor(true) != 0) {
                response.status = 1;
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
            
        case UFI_CMD_MOTOR_OFF:
            ufi_drive_motor(false);
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
            
        case UFI_CMD_SEEK: {
            uint8_t track = cmd_buffer[1];
            if (ufi_drive_seek(track) != 0) {
                response.status = 1;
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_RECALIBRATE:
            if (ufi_drive_recalibrate() != 0) {
                response.status = 1;
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
            
        case UFI_CMD_SELECT_SIDE: {
            uint8_t side = cmd_buffer[1];
            ufi_drive_select_side(side);
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_READ_TRACK:
        case UFI_CMD_READ_TRACK_RAW: {
            uint8_t track = cmd_buffer[1];
            uint8_t side = cmd_buffer[2];
            uint8_t revolutions = cmd_buffer[3];
            
            if (revolutions == 0) revolutions = 1;
            if (revolutions > 5) revolutions = 5;
            
            // Capture starten (asynchron)
            if (ufi_capture_start(track, side, revolutions) != 0) {
                response.status = 1;
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            // Daten werden in ufi_main_loop gesendet wenn fertig
            break;
        }
        
        case UFI_CMD_ABORT_READ:
            ufi_capture_abort();
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
            
        case UFI_CMD_IEC_RESET:
            ufi_iec_reset();
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
            
        case UFI_CMD_IEC_SEND: {
            uint8_t byte = cmd_buffer[1];
            bool eoi = cmd_buffer[2] != 0;
            if (ufi_iec_send_byte(byte, eoi) != 0) {
                response.status = 1;
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_IEC_RECEIVE: {
            /* payload: [byte, eoi] */
            static uint8_t rx[2];
            bool eoi = false;
            if (ufi_iec_receive_byte(&rx[0], &eoi) == 0) {
                rx[1] = eoi ? 1 : 0;
                response.length = 2;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, rx, 2);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else {
                response.status = 1;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            }
            break;
        }
        
        case UFI_CMD_RESET:
            // Software Reset
            NVIC_SystemReset();
            break;
            
        case UFI_CMD_BOOTLOADER:
            // In DFU Bootloader springen
            // Setze Magic Word und Reset
            *((uint32_t*)0x20000000) = 0xDEADBEEF;
            NVIC_SystemReset();
            break;
        
        /* Fix #11: Write-Support Stub */
        case UFI_CMD_WRITE_TRACK:
        case UFI_CMD_WRITE_TRACK_VERIFY: {
            // Format: [CMD, track, side, flux_count_lo, flux_count_hi, flux_count_hi2, flux_count_hi3]
            uint8_t track = cmd_buffer[1];
            uint8_t side = cmd_buffer[2];
            uint32_t flux_count = cmd_buffer[3] | (cmd_buffer[4] << 8) | 
                                  (cmd_buffer[5] << 16) | (cmd_buffer[6] << 24);
            bool verify = (cmd_buffer[0] == UFI_CMD_WRITE_TRACK_VERIFY);
            
            int ret = ufi_write_prepare(track, side, flux_count, verify);
            if (ret != 0) {
                response.status = (uint8_t)(-ret);
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            // Danach werden Flux-Daten in Chunks gesendet
            break;
        }
        
        case UFI_CMD_ERASE_TRACK: {
            uint8_t track = cmd_buffer[1];
            uint8_t side = cmd_buffer[2];
            
            int ret = ufi_erase_track(track, side);
            if (ret != 0) {
                response.status = (uint8_t)(-ret);
            }
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
        }
        
        case UFI_CMD_DEBUG_GPIO: {
            uint8_t subcmd = cmd_buffer[1];
            
            if (subcmd == 0) {
                // Read all GPIO
                gpio_status_t status = ufi_debug_gpio_read();
                response.length = sizeof(gpio_status_t);
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&status, sizeof(status));
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else if (subcmd == 1) {
                // Set single GPIO
                uint8_t gpio_id = cmd_buffer[2];
                uint8_t state = cmd_buffer[3];
                int ret = ufi_debug_gpio_set(gpio_id, state);
                if (ret != 0) response.status = (uint8_t)(-ret);
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else if (subcmd == 2) {
                // LED Test
                ufi_debug_led_test();
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else if (subcmd == 3) {
                // Selftest
                uint8_t result = ufi_debug_selftest();
                response.length = 1;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, &result, 1);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else {
                response.status = 0xFF;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            }
            break;
        }
        
        case UFI_CMD_DEBUG_TIMER: {
            uint8_t subcmd = cmd_buffer[1];
            
            if (subcmd == 0) {
                // Read timer status
                timer_status_t status = ufi_debug_timer_read();
                response.length = sizeof(timer_status_t);
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&status, sizeof(status));
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else if (subcmd == 1) {
                // Measure index timing
                uint32_t ticks = ufi_debug_measure_index();
                response.length = 4;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&ticks, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else if (subcmd == 2) {
                // Measure RPM
                uint16_t rpm = ufi_debug_measure_rpm();
                response.length = 2;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&rpm, 2);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else if (subcmd == 3) {
                // Memory info
                memory_info_t info = ufi_debug_memory_read();
                response.length = sizeof(memory_info_t);
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
                usb_wait_tx_complete();
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&info, sizeof(info));
                USBD_CDC_TransmitPacket(&hUsbDevice);
            } else {
                response.status = 0xFF;
                USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
                USBD_CDC_TransmitPacket(&hUsbDevice);
            }
            break;
        }
            
        default:
            response.status = 0xFF;  // Unbekannter Befehl
            USBD_CDC_SetTxBuffer(&hUsbDevice, (uint8_t*)&response, 4);
            USBD_CDC_TransmitPacket(&hUsbDevice);
            break;
    }
    
    return 1;
}

/* USB CDC Interface ist in usbd_cdc_if.c definiert */
