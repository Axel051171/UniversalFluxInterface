/* Host-side stand-in for the ST USB device library types (see stm32h7xx_hal.h here). */
#ifndef USBD_DEF_STUB_H
#define USBD_DEF_STUB_H
#include <stdint.h>

typedef struct { int unused; } USBD_ClassTypeDef;
typedef struct { int unused; } USBD_HandleTypeDef;

#define USBD_OK 0u
#define USBD_BUSY 1u
#define USBD_FAIL 3u

#endif
