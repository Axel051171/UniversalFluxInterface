/* Host-side stand-in for the ST USB CDC class header (see stm32h7xx_hal.h here). */
#ifndef USBD_CDC_STUB_H
#define USBD_CDC_STUB_H
#include "usbd_def.h"

typedef struct { int unused; } USBD_CDC_ItfTypeDef;

uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef* pdev);

#endif
