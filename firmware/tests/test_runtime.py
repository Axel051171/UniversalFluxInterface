"""Production USB queue and IEC reset state machine with controlled completion/time."""
from test_regressions import BASE, ERRORS, function, run


def usb_queue_tests():
    code=BASE+ERRORS+r'''
#define UFI_V2_FRAME_MAX 4106
#define UFI_USB_COMPOSITE 4
#define UFI_OWNER_NATIVE 1
#define USBD_STATE_CONFIGURED 1
#define USBD_OK 0
#define USBD_EP_TYPE_BULK 2
#define BULK_IN 0x83
static struct {int dev_state;} hUsbDevice={1};
static uint8_t replies[2][UFI_V2_FRAME_MAX];
static uint16_t reply_len[2];
static volatile uint8_t reply_head,reply_count;
static uint8_t reply_tail;
static volatile bool tx_busy,reply_active;
static uint32_t tx_len,tx_since,tx_timeouts,now,mask;
static volatile uint32_t tx_epoch;
static volatile bool tx_fault;
static int mode=4,owner=1,fail_tx,cancelled;
static const uint8_t *sent;static uint32_t sent_len;
static uint32_t __get_PRIMASK(void) {return mask;}
static void __disable_irq(void) {mask=1;}
static void __set_PRIMASK(uint32_t v) {mask=v;}
static unsigned __get_IPSR(void) {return 0;}
static uint32_t HAL_GetTick(void) {return now;}
static int ufi_usb_get_mode(void) {return mode;}
static int ufi_resources_owner(void) {return owner;}
static void ufi_cancel_request(void) {cancelled++;}
static void ufi_v2_clear_comms(void) {}
static int ufi_usb_tx_start(const uint8_t*p,uint32_t n) {(void)p;(void)n;return -99;}
static int ufi_usb_tx_blocking(const uint8_t*p,uint32_t n) {(void)p;(void)n;return -99;}
static int USBD_LL_Transmit(void*d,unsigned ep,uint8_t*p,uint32_t n) {
    (void)d;assert(ep==0x83);sent=p;sent_len=n;return fail_tx;
}
static void USBD_LL_CloseEP(void*d,unsigned ep) {(void)d;assert(ep==0x83);}
static void USBD_LL_OpenEP(void*d,unsigned ep,unsigned type,unsigned size) {
    (void)d;assert(ep==0x83 && type==2 && size==64);
}
typedef void USBD_HandleTypeDef;
static uint8_t cdc_in(void*d,uint8_t ep) {(void)d;(void)ep;return 0;}
static struct {uint8_t (*DataIn)(void*,uint8_t);} USBD_CDC={cdc_in};
'''
    for name in ('tx_reset','data_in','tx_raw','ufi_native_tx_start',
                 'ufi_native_tx_blocking','ufi_native_tx_service'):
        code+=function('ufi_composite.c',name)
    code+=r'''
int main(void) {
uint8_t source[64];memset(source,0x5a,64);
assert(ufi_native_tx_blocking(source,64)==0 && reply_count==1 && !tx_busy);
source[0]=0;ufi_native_tx_service();assert(tx_busy && reply_active && sent_len==64 && sent[0]==0x5a);
assert(ufi_native_tx_blocking(source,3)==0 && reply_count==2);
assert(ufi_native_tx_blocking(source,1)==UFI_ERR_BUSY);
assert(ufi_native_tx_start(source,1)==UFI_ERR_BUSY);
data_in(&hUsbDevice,3);assert(tx_busy && reply_count==2 && sent_len==0); /* ZLP */
data_in(&hUsbDevice,3);assert(!tx_busy && reply_count==1);
ufi_native_tx_service();assert(tx_busy && sent_len==3 && sent[0]==0);
data_in(&hUsbDevice,3);assert(!tx_busy && !reply_count);
assert(ufi_native_tx_start(source,3)==0);
assert(ufi_native_tx_blocking(source,1)==0);
data_in(&hUsbDevice,3);assert(reply_count==1 && !tx_busy); /* direct stream did not pop queue */
ufi_native_tx_service();now=1000;ufi_native_tx_service();
assert(cancelled==1 && tx_timeouts==1 && !tx_busy && !reply_count);
assert(ufi_native_tx_blocking(source,1)==UFI_ERR_USB);tx_fault=false;
assert(ufi_native_tx_blocking(source,1)==0);fail_tx=1;ufi_native_tx_service();
assert(cancelled==2 && !tx_busy && !reply_count);fail_tx=0;
tx_fault=false;
assert(ufi_native_tx_blocking(source,1)==0);ufi_native_tx_service();tx_reset();
assert(!reply_count && !tx_busy && mask==0);
return 0;
}
'''
    run('regression_usb_reply_queue',code)


def iec_reset_tests():
    code=BASE+ERRORS+r'''
static uint8_t reset_phase;
static uint32_t reset_since,now;
static bool cancelled,asserted;
static int PIN_IEC_RESET_OUT;
static uint32_t HAL_GetTick(void) {return now;}
static bool ufi_cancel_requested(void) {return cancelled;}
static void bus_out(void*p,bool v) {(void)p;asserted=v;}
static void ufi_iec_init(void) {reset_phase=0;asserted=false;}
static void ufi_nib_invalidate(void) {}
'''
    for name in ('ufi_iec_reset_begin','ufi_iec_reset_step'):
        code+=function('ufi_iec.c',name)
    code+=r'''
int main(void) {
assert(ufi_iec_reset_begin()==0 && asserted);
assert(ufi_iec_reset_begin()==UFI_ERR_BUSY);
now=19;assert(ufi_iec_reset_step()==UFI_ERR_BUSY && asserted);
now=20;assert(ufi_iec_reset_step()==UFI_ERR_BUSY && !asserted);
now=519;assert(ufi_iec_reset_step()==UFI_ERR_BUSY);
now=520;assert(ufi_iec_reset_step()==0 && !reset_phase);
now=UINT32_MAX-10;assert(ufi_iec_reset_begin()==0);
now=10;assert(ufi_iec_reset_step()==UFI_ERR_BUSY && !asserted);
cancelled=true;assert(ufi_iec_reset_step()==UFI_ERR_CANCELLED && !reset_phase);
cancelled=false;assert(ufi_iec_reset_begin()==0);cancelled=true;
assert(ufi_iec_reset_step()==UFI_ERR_CANCELLED && !asserted);
return 0;
}
'''
    run('regression_iec_reset_states',code)


if __name__=='__main__':
    usb_queue_tests()
    iec_reset_tests()
