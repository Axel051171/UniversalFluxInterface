/**
 * usbd_desc.h - USB Device Descriptors Header
 * UFI Flux Engine
 */

#ifndef USBD_DESC_H
#define USBD_DESC_H

#include "usbd_def.h"

/* String Descriptor Sizes */
#define USB_SIZ_STRING_SERIAL      0x1A

/* Exported Descriptor */
extern USBD_DescriptorsTypeDef HS_Desc;
void usbd_desc_set_msc(uint8_t msc);   /* 1 = mass storage descriptor (own PID) */

#endif /* USBD_DESC_H */
