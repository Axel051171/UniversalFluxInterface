/* LibFuzzer harness for the legacy v1 command path and the transmit helpers of ufi_usb.c
 * (compiled as a whole; every driver behind it is a generated stub, see fuzz_stubs.py).
 *
 * Input: the OUT packets a host would send, front to back; packet sizes (1..64), the calls
 * between them (mode switch, line coding, completion notices, flux packets) and everything the
 * stubbed drivers report come from the oracle tail of the same input.  Each packet is handed
 * over in a heap block of exactly its size, so a read behind the packet is an ASan finding.
 *
 * Checks: ASan/UBSan, no hang; every v1 reply is a header {command, status, length u16} followed
 * by exactly `length` <= 64 payload bytes; nothing is transmitted while the device is a mass
 * storage device; every transmitted byte is readable. */
#include "ufi_firmware.h"
#include "ufi_composite.h"
#include "usbd_core.h"
#include "usbd_cdc.h"
#include "usbd_cdc_if.h"
#include "usbd_desc.h"
#include "usbd_msc.h"
#include "fuzz_oracle.h"
#include <assert.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

/* declared the same way in usbd_cdc_if.c, the only caller in the firmware */
extern USBD_HandleTypeDef hUsbDevice;
extern bool ufi_usb_receive_callback(uint8_t* buf, uint32_t len);
extern void ufi_usb_line_coding(uint32_t baud);

static const uint8_t* tail;
static size_t tail_n;
static uint32_t prng;
static bool calm;                          /* stubs report "nothing": used to restore the start state */
static uint8_t zero_buf[8192];

uint8_t fuzz_byte(void)
{
    if (calm) return 0;
    if (tail_n) return tail[--tail_n];
    prng ^= prng << 13; prng ^= prng >> 17; prng ^= prng << 5;
    return (uint8_t)prng;
}
bool fuzz_bit(void) { return (fuzz_byte() & 1u) != 0; }
uint16_t fuzz_u16(void) { return (uint16_t)fuzz_u32(); }
uint32_t fuzz_u32(void)
{
    const uint8_t b = fuzz_byte();
    switch (b >> 6) {
        case 0: return b & 63u;
        case 1: return 0xFFFFFFFFu - (b & 63u);
        case 2: return (uint32_t)(b & 63u) << 8;
        default: return 1u << (b & 31u);
    }
}
int fuzz_err(void)
{
    const uint8_t b = fuzz_byte();
    return b < 22u ? -(int)b : 0;
}
void* fuzz_zero_buffer(void) { return zero_buf; }
const char* fuzz_string(void) { return ""; }

/* ---- data objects ufi_usb.c only takes the address of ------------------------------ */
USBD_ClassTypeDef USBD_CDC, USBD_MSC, UFI_Composite;
USBD_DescriptorsTypeDef HS_Desc;
USBD_CDC_ItfTypeDef USBD_Interface_fops_HS;
USBD_StorageTypeDef ufi_msc_fops, ufi_floppy_fops;
PCD_HandleTypeDef hpcd_USB_OTG_HS;

/* ---- doubles with behaviour -------------------------------------------------------- */
static USBD_CDC_HandleTypeDef cdc;
static uint32_t ticks;
static const uint8_t* tx_p;
static uint32_t tx_n, raw_expected;
static bool raw;                            /* the transfer in flight is not a v1 reply */

uint32_t HAL_GetTick(void) { return ticks += 1u; }
void HAL_Delay(uint32_t ms) { ticks += ms; }

uint8_t USBD_CDC_SetTxBuffer(USBD_HandleTypeDef* pdev, uint8_t* buf, uint32_t len)
{
    assert(pdev == &hUsbDevice && buf && len >= 1u && len <= 32u * 1024u);
    tx_p = buf;
    tx_n = len;
    return USBD_OK;
}

uint8_t USBD_CDC_TransmitPacket(USBD_HandleTypeDef* pdev)
{
    assert(pdev == &hUsbDevice && tx_p);
    assert(!ufi_usb_msc_active());                      /* no CDC interface in the mass storage modes */
    volatile uint8_t sum = 0;
    for (uint32_t i = 0; i < tx_n; i++) sum ^= tx_p[i];  /* every byte must be readable */
    (void)sum;
    if (raw) {
        assert(tx_n == raw_expected || tx_n == sizeof(ufi_response_header_t) + sizeof(flux_packet_header_t));
    } else {
        ufi_response_header_t h;
        assert(tx_n >= sizeof h);
        memcpy(&h, tx_p, sizeof h);
        assert(h.length <= 64u && tx_n == sizeof h + h.length);
    }
    tx_p = NULL;
    const uint8_t r = fuzz_byte();
    if (r == 0xFEu) cdc.TxState = 1;                    /* host stops reading: the wait must time out */
    return r == 0xFFu ? USBD_FAIL : USBD_OK;
}

static void restore(void)
{
    calm = true;
    cdc.TxState = 0;
    (void)ufi_usb_process_command();                    /* drains a command left in the buffer */
    const int ret = ufi_usb_set_mode(UFI_USB_FLUX);
    assert(ret == UFI_OK && ufi_usb_get_mode() == UFI_USB_FLUX && !ufi_usb_msc_active());
    calm = false;
}

int LLVMFuzzerTestOneInput(const uint8_t* d, size_t n)
{
    if (n < 2) return 0;
    tail = d; tail_n = n; prng = 0x2545F491u ^ (uint32_t)n; ticks = 0;
    memset(zero_buf, 0, sizeof zero_buf);
    hUsbDevice.pClassData = &cdc;
    hUsbDevice.dev_state = USBD_STATE_CONFIGURED;
    restore();

    size_t i = 0;
    while (i < n) {
        size_t chunk = 1u + (fuzz_byte() & 63u);
        if (chunk > n - i) chunk = n - i;
        uint8_t* packet = malloc(chunk);
        assert(packet);
        memcpy(packet, d + i, chunk);
        (void)ufi_usb_receive_callback(packet, (uint32_t)chunk);
        free(packet);
        i += chunk;

        cdc.TxState = 0;
        const uint8_t act = fuzz_byte();
        const int handled = ufi_usb_process_command();
        assert(handled == 0 || handled == 1);
        switch (act & 31u) {
            case 1: ufi_usb_line_coding((act & 32u) ? 10000u : 115200u); break;
            case 2: (void)ufi_usb_send_read_done(fuzz_err(), fuzz_byte()); break;
            case 3: (void)ufi_usb_send_event(fuzz_byte(), fuzz_err()); break;
            case 4: (void)ufi_usb_set_mode(fuzz_byte() & 7u); break;
            case 5: {
                flux_packet_header_t h = {.track = fuzz_byte(), .sample_count = fuzz_byte() & 63u};
                flux_sample_t* samples = malloc((h.sample_count + 1u) * sizeof *samples);
                assert(samples);
                memset(samples, 0xA5, (h.sample_count + 1u) * sizeof *samples);
                raw = true;
                raw_expected = h.sample_count * (uint32_t)sizeof *samples;
                (void)ufi_usb_send_flux(&h, samples);
                raw = false;
                free(samples);
                break;
            }
            case 6: ufi_usb_poll(); break;
            case 7: {
                const uint32_t len = fuzz_u16() % (sizeof zero_buf + 2u);
                raw = true;
                raw_expected = len;
                const int ret = ufi_usb_tx_start(zero_buf, len);
                assert(ret == UFI_OK || ret == UFI_ERR_USB || ret == UFI_ERR_BUSY);
                assert(len != 0u || ret == UFI_ERR_USB);
                raw = false;
                break;
            }
            case 8: hUsbDevice.dev_state = (act & 32u) ? USBD_STATE_SUSPENDED : USBD_STATE_CONFIGURED; break;
            case 9: (void)ufi_usb_set_msc((act & 32u) != 0u); break;
            default: break;
        }
    }
    hUsbDevice.dev_state = USBD_STATE_CONFIGURED;
    restore();
    return 0;
}
