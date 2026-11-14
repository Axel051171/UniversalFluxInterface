/* Host-side stand-in for the ST USB device library types (see stm32h7xx_hal.h here).
 * Only what the firmware's own sources touch: device state, class data, descriptor macros. */
#ifndef USBD_DEF_STUB_H
#define USBD_DEF_STUB_H
#include <stdint.h>

typedef struct { int unused; } USBD_ClassTypeDef;
typedef struct { int unused; } USBD_DescriptorsTypeDef;
typedef struct {
    uint8_t dev_state;
    void* pClassData;
} USBD_HandleTypeDef;

#define USBD_OK 0u
#define USBD_BUSY 1u
#define USBD_FAIL 3u

#define USBD_STATE_DEFAULT 1u
#define USBD_STATE_ADDRESSED 2u
#define USBD_STATE_CONFIGURED 3u
#define USBD_STATE_SUSPENDED 4u

#define USB_LEN_DEV_DESC 0x12u
#define USB_DESC_TYPE_DEVICE 0x01u
#define USB_MAX_EP0_SIZE 64u
#define USBD_IDX_MFC_STR 0x01u
#define USBD_IDX_PRODUCT_STR 0x02u
#define USBD_IDX_SERIAL_STR 0x03u
#ifndef USBD_MAX_NUM_CONFIGURATION
#define USBD_MAX_NUM_CONFIGURATION 1u
#endif
#ifndef LOBYTE
#define LOBYTE(x) ((uint8_t)((x) & 0x00FFu))
#define HIBYTE(x) ((uint8_t)(((x) & 0xFF00u) >> 8))
#endif

#endif
