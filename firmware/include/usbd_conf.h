/**
 * usbd_conf.h - USB Device Configuration Header
 * UFI Flux Engine
 */

#ifndef USBD_CONF_H
#define USBD_CONF_H

#include "stm32h7xx.h"
#include "stm32h7xx_hal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* USB Device Configuration */
#define USBD_MAX_NUM_INTERFACES        2
#define USBD_MAX_NUM_CONFIGURATION     1
#define USBD_MAX_STR_DESC_SIZ          512
#define USBD_DEBUG_LEVEL               0
#define USBD_SELF_POWERED              1
#define USBD_CDC_INTERVAL              2000

/* Memory Management */
#define USBD_malloc               (void *)USBD_static_malloc
#define USBD_free                 USBD_static_free

/* Debug Macros */
#if (USBD_DEBUG_LEVEL > 0)
#define USBD_UsrLog(...)    printf(__VA_ARGS__);\
                            printf("\n");
#else
#define USBD_UsrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 1)
#define USBD_ErrLog(...)    printf("ERROR: ");\
                            printf(__VA_ARGS__);\
                            printf("\n");
#else
#define USBD_ErrLog(...)
#endif

#if (USBD_DEBUG_LEVEL > 2)
#define USBD_DbgLog(...)    printf("DEBUG: ");\
                            printf(__VA_ARGS__);\
                            printf("\n");
#else
#define USBD_DbgLog(...)
#endif

/* Memory Functions */
void *USBD_static_malloc(uint32_t size);
void USBD_static_free(void *p);

/* External PCD Handle */
extern PCD_HandleTypeDef hpcd_USB_OTG_HS;

#endif /* USBD_CONF_H */
