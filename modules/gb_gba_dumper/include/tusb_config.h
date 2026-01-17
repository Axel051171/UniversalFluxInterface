/*
 * GB/GBA USB Dumper - TinyUSB Configuration
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * COMMON CONFIGURATION
 * ============================================================================ */

#define CFG_TUSB_MCU                OPT_MCU_RP2040
#define CFG_TUSB_RHPORT0_MODE       OPT_MODE_DEVICE
#define CFG_TUSB_OS                 OPT_OS_NONE

/* Memory */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

/* ============================================================================
 * DEVICE CONFIGURATION
 * ============================================================================ */

#define CFG_TUD_ENABLED             1

/* Endpoint 0 size */
#define CFG_TUD_ENDPOINT0_SIZE      64

/* ============================================================================
 * CLASS DRIVER CONFIGURATION
 * ============================================================================ */

/* CDC Class */
#define CFG_TUD_CDC                 1
#define CFG_TUD_CDC_RX_BUFSIZE      512
#define CFG_TUD_CDC_TX_BUFSIZE      512

/* Disable other classes */
#define CFG_TUD_MSC                 0
#define CFG_TUD_HID                 0
#define CFG_TUD_MIDI                0
#define CFG_TUD_VENDOR              0

/* ============================================================================
 * USB DESCRIPTORS
 * ============================================================================ */

#define USB_VID                     0x1209  /* pid.codes VID */
#define USB_PID                     0x4742  /* "GB" */
#define USB_BCD_DEVICE              0x0100

#define USB_MANUFACTURER            "UFI Project"
#define USB_PRODUCT                 "GB/GBA USB Dumper"
#define USB_SERIAL                  "UFI-GB-001"

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
