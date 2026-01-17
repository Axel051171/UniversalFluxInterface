/**
 * usbd_desc.c - USB Device Descriptors
 * UFI Flux Engine
 */

#include "usbd_core.h"
#include "usbd_desc.h"
#include "usbd_conf.h"

/* USB VID/PID */
#define USBD_VID                     0x1209  /* pid.codes VID */
#define USBD_PID                     0x4F54  /* "OT" for Open Tool */
#define USBD_LANGID_STRING           0x409   /* English US */
#define USBD_MANUFACTURER_STRING     "UFT Project"
#define USBD_PRODUCT_STRING          "UFI Flux Engine"
#define USBD_CONFIGURATION_STRING    "CDC Config"
#define USBD_INTERFACE_STRING        "CDC Interface"

/* Serial Number */
#define USBD_SERIAL_NBR              "UFI-001"

/* USB Standard Device Descriptor */
__ALIGN_BEGIN uint8_t USBD_HS_DeviceDesc[USB_LEN_DEV_DESC] __ALIGN_END = {
    0x12,                       /* bLength */
    USB_DESC_TYPE_DEVICE,       /* bDescriptorType */
    0x00, 0x02,                 /* bcdUSB = 2.00 */
    0x02,                       /* bDeviceClass: CDC */
    0x02,                       /* bDeviceSubClass */
    0x00,                       /* bDeviceProtocol */
    USB_MAX_EP0_SIZE,           /* bMaxPacketSize */
    LOBYTE(USBD_VID), HIBYTE(USBD_VID),  /* idVendor */
    LOBYTE(USBD_PID), HIBYTE(USBD_PID),  /* idProduct */
    0x00, 0x02,                 /* bcdDevice = 2.00 */
    USBD_IDX_MFC_STR,           /* iManufacturer */
    USBD_IDX_PRODUCT_STR,       /* iProduct */
    USBD_IDX_SERIAL_STR,        /* iSerialNumber */
    USBD_MAX_NUM_CONFIGURATION  /* bNumConfigurations */
};

/* USB Language ID Descriptor */
__ALIGN_BEGIN uint8_t USBD_LangIDDesc[USB_LEN_LANGID_STR_DESC] __ALIGN_END = {
    USB_LEN_LANGID_STR_DESC,
    USB_DESC_TYPE_STRING,
    LOBYTE(USBD_LANGID_STRING), HIBYTE(USBD_LANGID_STRING)
};

/* USB String Buffer */
__ALIGN_BEGIN uint8_t USBD_StrDesc[USBD_MAX_STR_DESC_SIZ] __ALIGN_END;

/* Device Qualifier Descriptor */
__ALIGN_BEGIN uint8_t USBD_DeviceQualifierDesc[USB_LEN_DEV_QUALIFIER_DESC] __ALIGN_END = {
    USB_LEN_DEV_QUALIFIER_DESC,
    USB_DESC_TYPE_DEVICE_QUALIFIER,
    0x00, 0x02,
    0x00,
    0x00,
    0x00,
    USB_MAX_EP0_SIZE,
    0x01,
    0x00,
};

/* Private function prototypes */
static void Get_SerialNum(void);
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len);

/**
 * @brief  Return the device descriptor
 */
uint8_t *USBD_HS_DeviceDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_HS_DeviceDesc);
    return USBD_HS_DeviceDesc;
}

/**
 * @brief  Return the LangID string descriptor
 */
uint8_t *USBD_HS_LangIDStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = sizeof(USBD_LangIDDesc);
    return USBD_LangIDDesc;
}

/**
 * @brief  Return the manufacturer string descriptor
 */
uint8_t *USBD_HS_ManufacturerStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_MANUFACTURER_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

/**
 * @brief  Return the product string descriptor
 */
uint8_t *USBD_HS_ProductStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_PRODUCT_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

/**
 * @brief  Return the serial number string descriptor
 */
uint8_t *USBD_HS_SerialStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    *length = USB_SIZ_STRING_SERIAL;

    /* Get unique device ID */
    Get_SerialNum();

    return (uint8_t *)USBD_StrDesc;
}

/**
 * @brief  Return the configuration string descriptor
 */
uint8_t *USBD_HS_ConfigStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_CONFIGURATION_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

/**
 * @brief  Return the interface string descriptor
 */
uint8_t *USBD_HS_InterfaceStrDescriptor(USBD_SpeedTypeDef speed, uint16_t *length)
{
    (void)speed;
    USBD_GetString((uint8_t *)USBD_INTERFACE_STRING, USBD_StrDesc, length);
    return USBD_StrDesc;
}

/**
 * @brief  Create the serial number string descriptor
 */
static void Get_SerialNum(void)
{
    uint32_t deviceserial0, deviceserial1, deviceserial2;

    /* Read STM32 Unique ID */
    deviceserial0 = *(uint32_t *)UID_BASE;
    deviceserial1 = *(uint32_t *)(UID_BASE + 4);
    deviceserial2 = *(uint32_t *)(UID_BASE + 8);

    deviceserial0 += deviceserial2;

    if (deviceserial0 != 0)
    {
        IntToUnicode(deviceserial0, &USBD_StrDesc[2], 8);
        IntToUnicode(deviceserial1, &USBD_StrDesc[18], 4);
    }
}

/**
 * @brief  Convert Hex 32Bits value into char
 */
static void IntToUnicode(uint32_t value, uint8_t *pbuf, uint8_t len)
{
    uint8_t idx = 0;

    for (idx = 0; idx < len; idx++)
    {
        if (((value >> 28)) < 0xA)
        {
            pbuf[2 * idx] = (value >> 28) + '0';
        }
        else
        {
            pbuf[2 * idx] = (value >> 28) + 'A' - 10;
        }

        value = value << 4;
        pbuf[2 * idx + 1] = 0;
    }
}

/* USBD Descriptor Callbacks */
USBD_DescriptorsTypeDef HS_Desc = {
    USBD_HS_DeviceDescriptor,
    USBD_HS_LangIDStrDescriptor,
    USBD_HS_ManufacturerStrDescriptor,
    USBD_HS_ProductStrDescriptor,
    USBD_HS_SerialStrDescriptor,
    USBD_HS_ConfigStrDescriptor,
    USBD_HS_InterfaceStrDescriptor
};
