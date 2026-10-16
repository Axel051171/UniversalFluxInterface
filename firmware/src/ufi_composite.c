#include "ufi_firmware.h"
#include "ufi_composite.h"
#include "usbd_cdc.h"
#include "usbd_ctlreq.h"
#include "ufi_resources.h"
#include "ufi_cancel.h"
#include <string.h>
extern USBD_HandleTypeDef hUsbDevice;
#define BULK_IN 0x83u
#define BULK_OUT 0x03u
static uint8_t rx_packet[64];
static volatile bool tx_busy, disconnected;
static uint32_t tx_len;
/* CPU FIFO writes, USB DMA is disabled. Keep queued reply storage off AXI. */
__attribute__((section(".dtcm"), aligned(32))) static uint8_t replies[2][UFI_V2_FRAME_MAX];
static uint16_t reply_len[2];
static volatile uint8_t reply_head, reply_count;
static uint8_t reply_tail;
static volatile bool reply_active;
static uint32_t tx_since, tx_timeouts;
static volatile uint32_t tx_epoch;
static volatile bool tx_fault;
static int tx_raw(const uint8_t *p, uint32_t n);
static void tx_reset(void)
{
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    /* Deactivate the endpoint, not merely its FIFO: cancel the old transfer. */
    if(hUsbDevice.dev_state==USBD_STATE_CONFIGURED) {
        USBD_LL_CloseEP(&hUsbDevice,BULK_IN);
        USBD_LL_OpenEP(&hUsbDevice,BULK_IN,USBD_EP_TYPE_BULK,64);
    }
    tx_busy=reply_active=false;reply_head=reply_tail=reply_count=0;tx_len=0;
    tx_epoch++;
    __set_PRIMASK(mask);
}
static uint8_t config[128], compat[40], properties[142];
static uint16_t config_len;
static const uint8_t os_string[18] = {18,3,'M',0,'S',0,'F',0,'T',0,'1',0,'0',0,'0',0,0x20,0};
static void le16(uint8_t *p, uint16_t v) { p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); }
static void le32(uint8_t *p, uint32_t v) { le16(p,(uint16_t)v); le16(p+2,(uint16_t)(v>>16)); }
static void descriptors(void)
{
    uint16_t n;
    const uint8_t *cdc=USBD_CDC.GetFSConfigDescriptor(&n);
    static const uint8_t iad[]={8,11,0,2,2,2,1,0};
    static const uint8_t vendor[]={9,4,2,0,2,0xff,0,0,0,7,5,BULK_OUT,2,64,0,0,7,5,BULK_IN,2,64,0,0};
    memcpy(config,cdc,9); memcpy(config+9,iad,8);
    memcpy(config+17,cdc+9,n-9); memcpy(config+n+8,vendor,sizeof(vendor));
    config_len=n+8+sizeof(vendor); le16(config+2,config_len); config[4]=3;
    memset(compat,0,sizeof(compat)); le32(compat,40); le16(compat+4,0x100);
    le16(compat+6,4); compat[8]=1; compat[16]=2; compat[17]=1;
    memcpy(compat+18,"WINUSB",6);
    memset(properties,0,sizeof(properties)); le32(properties,142); le16(properties+4,0x100);
    le16(properties+6,5); le16(properties+8,1); le32(properties+10,132);
    le32(properties+14,1); le16(properties+18,40);
    const char *name="DeviceInterfaceGUID", *guid="{9F78D994-00A5-4F27-A1D0-071747F159A8}";
    for (unsigned i=0;name[i];i++) properties[20+i*2]=name[i];
    le32(properties+60,78);
    for (unsigned i=0;guid[i];i++) properties[64+i*2]=guid[i];
}
static uint8_t *get_config(uint16_t *n) { descriptors(); *n=config_len; return config; }
static uint8_t *get_string(USBD_HandleTypeDef *d,uint8_t index,uint16_t *n)
{ (void)d; *n=index==0xee?18:0; return index==0xee?(uint8_t*)os_string:NULL; }
static uint8_t init(USBD_HandleTypeDef *d,uint8_t cfg)
{
    uint8_t ret=USBD_CDC.Init(d,cfg); if(ret!=USBD_OK)return ret;
    tx_fault=false;tx_busy=reply_active=false;reply_head=reply_tail=reply_count=0; descriptors();
    USBD_LL_OpenEP(d,BULK_IN,USBD_EP_TYPE_BULK,64);
    USBD_LL_OpenEP(d,BULK_OUT,USBD_EP_TYPE_BULK,64);
    d->ep_in[3].is_used=1; d->ep_out[3].is_used=1;
    USBD_LL_PrepareReceive(d,BULK_OUT,rx_packet,64);
    return USBD_OK;
}
void ufi_composite_disconnect(void) { disconnected=true; tx_busy=false; ufi_cancel_request(); }
static uint8_t deinit(USBD_HandleTypeDef *d,uint8_t cfg)
{
    USBD_LL_CloseEP(d,BULK_IN); USBD_LL_CloseEP(d,BULK_OUT);
    d->ep_in[3].is_used=0; d->ep_out[3].is_used=0;
    ufi_composite_disconnect(); return USBD_CDC.DeInit(d,cfg);
}
static uint8_t setup(USBD_HandleTypeDef *d,USBD_SetupReqTypedef *r)
{
    if(r->bmRequest==0x41 && r->bRequest==0x22 && r->wIndex==2 && !r->wLength && !r->wValue) {
        if(ufi_resources_owner()==UFI_OWNER_NATIVE) ufi_cancel_request();
        return USBD_OK;
    }
    if (r->bmRequest==0x41 && r->bRequest==0x21 && r->wIndex==2 && !r->wLength && !r->wValue) {
        tx_reset();tx_fault=false;
        ufi_v2_clear_comms(); return USBD_OK;
    }
    if ((r->bmRequest & 0x60)==0x40 && r->bRequest==0x20 && (r->bmRequest&0x80)) {
        const uint8_t *p=NULL; uint16_t n=0;
        if(r->wIndex==4 && r->wValue==0) {p=compat;n=sizeof(compat);}
        if(r->wIndex==5 && r->wValue==2) {p=properties;n=sizeof(properties);}
        if(p) { USBD_CtlSendData(d,(uint8_t*)p,r->wLength<n?r->wLength:n); return USBD_OK; }
    }
    if ((r->bmRequest&0x1f)==1 && r->wIndex==2) {
        static uint8_t zero[2];
        if(r->bRequest==USB_REQ_GET_INTERFACE && r->wLength==1) {
            USBD_CtlSendData(d,zero,1); return USBD_OK;
        }
        if(r->bRequest==USB_REQ_SET_INTERFACE && r->wValue==0 && r->wLength==0) return USBD_OK;
        if(r->bRequest==USB_REQ_GET_STATUS && r->wLength==2) {
            USBD_CtlSendData(d,zero,2); return USBD_OK;
        }
        USBD_CtlError(d,r); return USBD_FAIL;
    }
    return USBD_CDC.Setup(d,r);
}
static uint8_t ep0_rx(USBD_HandleTypeDef *d) {return USBD_CDC.EP0_RxReady(d);}
static uint8_t data_in(USBD_HandleTypeDef *d,uint8_t ep)
{
    if(ep!=3)return USBD_CDC.DataIn(d,ep);
    if(tx_fault)return USBD_OK;
    if(tx_len && !(tx_len%64)) {tx_len=0;USBD_LL_Transmit(d,BULK_IN,NULL,0);}
    else {
        if(reply_active) {reply_head^=1u;reply_count--;reply_active=false;}
        tx_busy=false;
    }
    return USBD_OK;
}
static uint8_t data_out(USBD_HandleTypeDef *d,uint8_t ep)
{
    if(ep!=3)return USBD_CDC.DataOut(d,ep);
    if(ufi_v2_rx(rx_packet,USBD_LL_GetRxDataSize(d,BULK_OUT)))
        USBD_LL_PrepareReceive(d,BULK_OUT,rx_packet,64);
    return USBD_OK;
}
USBD_ClassTypeDef UFI_Composite={.Init=init,.DeInit=deinit,.Setup=setup,
    .EP0_RxReady=ep0_rx,.DataIn=data_in,.DataOut=data_out,
    .GetHSConfigDescriptor=get_config,.GetFSConfigDescriptor=get_config,
    .GetOtherSpeedConfigDescriptor=get_config,.GetUsrStrDescriptor=get_string};
bool ufi_native_tx_idle(void)
{return ufi_usb_get_mode()==UFI_USB_COMPOSITE?(!tx_busy && !reply_count):ufi_usb_tx_idle();}
static int tx_raw(const uint8_t *p,uint32_t n)
{
    if(tx_fault)return UFI_ERR_USB;
    if(!n || n>65535 || hUsbDevice.dev_state!=USBD_STATE_CONFIGURED)return UFI_ERR_USB;
    if(tx_busy)return UFI_ERR_BUSY;
    tx_busy=true;tx_len=n;tx_since=HAL_GetTick();
    if (USBD_LL_Transmit(&hUsbDevice,BULK_IN,(uint8_t*)p,n)!=USBD_OK) {
        tx_busy=false; return UFI_ERR_USB;
    }
    return UFI_OK;
}
int ufi_native_tx_start(const uint8_t *p,uint32_t n)
{
    if(ufi_usb_get_mode()!=UFI_USB_COMPOSITE)return ufi_usb_tx_start(p,n);
    if(reply_count)return UFI_ERR_BUSY;
    return tx_raw(p,n);
}
int ufi_native_tx_blocking(const uint8_t *p,uint32_t n)
{
    if(ufi_usb_get_mode()!=UFI_USB_COMPOSITE)return ufi_usb_tx_blocking(p,n);
    if(__get_IPSR() || __get_PRIMASK())return UFI_ERR_USB;
    if(tx_fault)return UFI_ERR_USB;
    if(!n || n>sizeof(replies[0]) || hUsbDevice.dev_state!=USBD_STATE_CONFIGURED)return UFI_ERR_USB;
    if(reply_count==2)return UFI_ERR_BUSY;
    const uint32_t epoch=tx_epoch;
    const uint8_t slot=reply_tail;
    memcpy(replies[slot],p,n);reply_len[slot]=(uint16_t)n;
    const uint32_t mask=__get_PRIMASK();__disable_irq();
    if(epoch!=tx_epoch) {__set_PRIMASK(mask);return UFI_ERR_CANCELLED;}
    reply_tail^=1u;reply_count++;
    __set_PRIMASK(mask);
    return UFI_OK; /* Composite replies are queued; legacy CDC keeps its old API. */
}
bool ufi_native_tx_room(void)
{return ufi_usb_get_mode()!=UFI_USB_COMPOSITE || (!tx_fault && reply_count==0);}
uint32_t ufi_native_tx_timeouts(void) {return tx_timeouts;}
void ufi_native_tx_service(void)
{
    if(ufi_usb_get_mode()!=UFI_USB_COMPOSITE)return;
    if(tx_busy && HAL_GetTick()-tx_since>=1000u) {
        tx_timeouts++;tx_fault=true;tx_reset();ufi_v2_clear_comms();
        if(ufi_resources_owner()==UFI_OWNER_NATIVE)ufi_cancel_request();
        return;
    }
    if(!tx_busy && reply_count) {
        reply_active=true;
        if(tx_raw(replies[reply_head],reply_len[reply_head])!=UFI_OK) {
            tx_fault=true;tx_reset();ufi_v2_clear_comms();
            if(ufi_resources_owner()==UFI_OWNER_NATIVE)ufi_cancel_request();
        }
    }
}
bool ufi_native_tx_flush(uint32_t timeout_ms)
{
    const uint32_t start=HAL_GetTick();
    do {
        ufi_native_tx_service();UFI_WATCHDOG_FEED();
        if(ufi_native_tx_idle())return true;
    } while(HAL_GetTick()-start<timeout_ms && !ufi_cancel_requested());
    return false;
}
void ufi_native_rx_resume(void)
{
    if(ufi_usb_get_mode()==UFI_USB_COMPOSITE)
        USBD_LL_PrepareReceive(&hUsbDevice,BULK_OUT,rx_packet,64);
    else USBD_CDC_ReceivePacket(&hUsbDevice);
}
void ufi_composite_service(void)
{
    if(disconnected) {
        disconnected=false; ufi_capture_abort(); ufi_write_abort();
        ufi_gw_end(); ufi_v2_reset(); ufi_resources_reset();
        ufi_cancel_clear();
    }
    ufi_native_tx_service();
}
