/* Host-side stand-in for the ST USB CDC class header (see stm32h7xx_hal.h here). */
#ifndef USBD_CDC_STUB_H
#define USBD_CDC_STUB_H
#include "usbd_def.h"

typedef struct { int unused; } USBD_CDC_ItfTypeDef;
typedef struct { volatile uint32_t TxState; } USBD_CDC_HandleTypeDef;

extern USBD_ClassTypeDef USBD_CDC;

uint8_t USBD_CDC_ReceivePacket(USBD_HandleTypeDef* pdev);
uint8_t USBD_CDC_RegisterInterface(USBD_HandleTypeDef* pdev, USBD_CDC_ItfTypeDef* fops);
uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef* pdev, uint8_t* buf, uint32_t len);
uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef* pdev);

#endif
