/* Host-side stand-in for the ST USB device core header (see stm32h7xx_hal.h here). */
#ifndef USBD_CORE_STUB_H
#define USBD_CORE_STUB_H
#include "usbd_def.h"

uint8_t USBD_Init(USBD_HandleTypeDef* pdev, USBD_DescriptorsTypeDef* pdesc, uint8_t id);
uint8_t USBD_DeInit(USBD_HandleTypeDef* pdev);
uint8_t USBD_RegisterClass(USBD_HandleTypeDef* pdev, USBD_ClassTypeDef* pclass);
uint8_t USBD_Start(USBD_HandleTypeDef* pdev);
uint8_t USBD_Stop(USBD_HandleTypeDef* pdev);

#endif
