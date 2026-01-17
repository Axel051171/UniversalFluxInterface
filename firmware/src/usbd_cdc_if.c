/**
 * usbd_cdc_if.c - USB CDC Interface
 * UFI Flux Engine
 */

#include "usbd_cdc_if.h"
#include "ufi_firmware.h"

/* CDC Buffer Sizes */
#define APP_RX_DATA_SIZE  2048
#define APP_TX_DATA_SIZE  2048

/* Receive Buffer */
uint8_t UserRxBufferHS[APP_RX_DATA_SIZE];

/* Transmit Buffer */
uint8_t UserTxBufferHS[APP_TX_DATA_SIZE];

/* USB Handle */
extern USBD_HandleTypeDef hUsbDevice;

/* Command Callback - to be implemented in ufi_usb.c */
extern void ufi_usb_receive_callback(uint8_t *buf, uint32_t len);

/* ============================================================================
 * CDC Interface Callbacks
 * ============================================================================ */

/**
 * @brief  Initializes the CDC media low layer
 */
static int8_t CDC_Init_HS(void)
{
    /* Set Application Buffers */
    USBD_CDC_SetTxBuffer(&hUsbDevice, UserTxBufferHS, 0);
    USBD_CDC_SetRxBuffer(&hUsbDevice, UserRxBufferHS);
    return USBD_OK;
}

/**
 * @brief  DeInitializes the CDC media low layer
 */
static int8_t CDC_DeInit_HS(void)
{
    return USBD_OK;
}

/**
 * @brief  Manage the CDC class requests
 * @param  cmd: Command code
 * @param  pbuf: Buffer containing command data
 * @param  length: Number of data to be sent
 */
static int8_t CDC_Control_HS(uint8_t cmd, uint8_t *pbuf, uint16_t length)
{
    (void)pbuf;
    (void)length;

    switch (cmd)
    {
        case CDC_SEND_ENCAPSULATED_COMMAND:
            break;

        case CDC_GET_ENCAPSULATED_RESPONSE:
            break;

        case CDC_SET_COMM_FEATURE:
            break;

        case CDC_GET_COMM_FEATURE:
            break;

        case CDC_CLEAR_COMM_FEATURE:
            break;

        case CDC_SET_LINE_CODING:
            /* Line coding is not used - we're raw binary */
            break;

        case CDC_GET_LINE_CODING:
            /* Return dummy values */
            pbuf[0] = (uint8_t)(115200);
            pbuf[1] = (uint8_t)(115200 >> 8);
            pbuf[2] = (uint8_t)(115200 >> 16);
            pbuf[3] = (uint8_t)(115200 >> 24);
            pbuf[4] = 0;  /* Stop bits: 1 */
            pbuf[5] = 0;  /* Parity: None */
            pbuf[6] = 8;  /* Data bits: 8 */
            break;

        case CDC_SET_CONTROL_LINE_STATE:
            break;

        case CDC_SEND_BREAK:
            break;

        default:
            break;
    }

    return USBD_OK;
}

/**
 * @brief  Data received over USB OUT endpoint
 * @param  Buf: Buffer of data
 * @param  Len: Number of data received
 */
static int8_t CDC_Receive_HS(uint8_t *Buf, uint32_t *Len)
{
    /* Forward to UFI command handler */
    ufi_usb_receive_callback(Buf, *Len);

    /* Prepare for next reception */
    USBD_CDC_SetRxBuffer(&hUsbDevice, &Buf[0]);
    USBD_CDC_ReceivePacket(&hUsbDevice);

    return USBD_OK;
}

/**
 * @brief  Data transmitted callback
 * @param  Buf: Buffer of data
 * @param  Len: Number of data transmitted
 * @param  epnum: Endpoint number
 */
static int8_t CDC_TransmitCplt_HS(uint8_t *Buf, uint32_t *Len, uint8_t epnum)
{
    (void)Buf;
    (void)Len;
    (void)epnum;

    /* TX Complete - could signal semaphore here */
    return USBD_OK;
}

/* CDC Interface Functions */
USBD_CDC_ItfTypeDef USBD_Interface_fops_HS = {
    CDC_Init_HS,
    CDC_DeInit_HS,
    CDC_Control_HS,
    CDC_Receive_HS,
    CDC_TransmitCplt_HS
};

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * @brief  CDC_Transmit_HS
 *         Data to send over USB IN endpoint
 * @param  Buf: Buffer of data
 * @param  Len: Number of data to send
 * @retval USBD_OK if all operations are OK
 */
uint8_t CDC_Transmit_HS(uint8_t *Buf, uint16_t Len)
{
    uint8_t result = USBD_OK;
    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDevice.pClassData;

    if (hcdc->TxState != 0)
    {
        return USBD_BUSY;
    }

    USBD_CDC_SetTxBuffer(&hUsbDevice, Buf, Len);
    result = USBD_CDC_TransmitPacket(&hUsbDevice);

    return result;
}

/**
 * @brief  Get TX State
 * @retval 0 if idle, non-zero if busy
 */
uint8_t CDC_GetTxState_HS(void)
{
    USBD_CDC_HandleTypeDef *hcdc = (USBD_CDC_HandleTypeDef *)hUsbDevice.pClassData;
    if (hcdc == NULL) return 1;
    return hcdc->TxState;
}
