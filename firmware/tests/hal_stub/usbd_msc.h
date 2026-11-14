/* Host-side stand-in for the ST USB mass storage class header (see stm32h7xx_hal.h here). */
#ifndef USBD_MSC_STUB_H
#define USBD_MSC_STUB_H
#include "usbd_def.h"

typedef struct { int unused; } USBD_StorageTypeDef;

extern USBD_ClassTypeDef USBD_MSC;

uint8_t USBD_MSC_RegisterStorage(USBD_HandleTypeDef* pdev, USBD_StorageTypeDef* fops);

#endif
