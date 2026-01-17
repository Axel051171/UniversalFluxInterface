/**
 * usbd_cdc_if.h - USB CDC Interface Header
 * UFI Flux Engine
 */

#ifndef USBD_CDC_IF_H
#define USBD_CDC_IF_H

#include "usbd_cdc.h"

/* Exported Interface */
extern USBD_CDC_ItfTypeDef USBD_Interface_fops_HS;

/* Public API */
uint8_t CDC_Transmit_HS(uint8_t *Buf, uint16_t Len);
uint8_t CDC_GetTxState_HS(void);

#endif /* USBD_CDC_IF_H */
