"""USB device descriptors of usbd_desc.c (P06): identity per personality and the string descriptors.

The descriptor tables and functions are taken verbatim from the source (everything behind the
includes); USBD_GetString() is a double that builds a string descriptor the way the ST library
does, the unique ID is an array of this test.

Covered: VID, product ID, device class and product name of the five personalities as documented
in docs/USB_Protokoll.md 1.1, an unknown mode falls back to the flux device, the fixed fields of
the device descriptor, and that every string descriptor is well formed whatever was asked for
before - the serial number carried the length byte of the previous string until 2026-11-14.
"""
from test_regressions import FW, run


def main():
    source = (FW / 'src/usbd_desc.c').read_text(encoding='utf-8')
    body = source[source.index('/* USB VID/PID */'):]
    code = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#define __ALIGN_BEGIN
#define __ALIGN_END
#define USB_LEN_DEV_DESC 0x12u
#define USB_LEN_LANGID_STR_DESC 0x04u
#define USB_LEN_DEV_QUALIFIER_DESC 0x0Au
#define USB_DESC_TYPE_DEVICE 0x01u
#define USB_DESC_TYPE_STRING 0x03u
#define USB_DESC_TYPE_DEVICE_QUALIFIER 0x06u
#define USB_MAX_EP0_SIZE 64u
#define USBD_IDX_MFC_STR 0x01u
#define USBD_IDX_PRODUCT_STR 0x02u
#define USBD_IDX_SERIAL_STR 0x03u
#define USBD_MAX_NUM_CONFIGURATION 1u
#define USBD_MAX_STR_DESC_SIZ 0x100u
#define USB_SIZ_STRING_SERIAL 0x1A
#define LOBYTE(x) ((uint8_t)((x) & 0x00FFu))
#define HIBYTE(x) ((uint8_t)(((x) & 0xFF00u) >> 8))
typedef int USBD_SpeedTypeDef;
typedef uint8_t* (*desc_fn)(USBD_SpeedTypeDef, uint16_t*);
typedef struct { desc_fn device, langid, manufacturer, product, serial, config, interface; } USBD_DescriptorsTypeDef;
static uint32_t uid_words[3];
#define UID_BASE ((uintptr_t)uid_words)
/* as USBD_GetString() of the ST device library: UTF-16LE with a two-byte header */
static void USBD_GetString(uint8_t* desc, uint8_t* unicode, uint16_t* len)
{
    const size_t n = strlen((const char*)desc);
    assert(n * 2u + 2u <= USBD_MAX_STR_DESC_SIZ);
    *len = (uint16_t)(n * 2u + 2u);
    unicode[0] = (uint8_t)*len; unicode[1] = USB_DESC_TYPE_STRING;
    for (size_t i = 0; i < n; i++) { unicode[2u + 2u * i] = desc[i]; unicode[3u + 2u * i] = 0; }
}
''' + body + r'''
/* a string descriptor as the host sees it -> ASCII text; asserts that it is well formed */
static const char* text(uint8_t* (*get)(USBD_SpeedTypeDef, uint16_t*))
{
    static char out[128];
    uint16_t len = 0xFFFF;
    const uint8_t* d = get(0, &len);
    assert(d && len >= 2u && len <= USBD_MAX_STR_DESC_SIZ && (len & 1u) == 0u);
    assert(d[0] == len && d[1] == USB_DESC_TYPE_STRING);        /* bLength = what is sent, bDescriptorType = string */
    unsigned n = 0;
    for (unsigned i = 2; i < len; i += 2) { assert(d[i] >= 0x20 && d[i] < 0x7F && d[i + 1u] == 0 && n < sizeof out - 1u); out[n++] = (char)d[i]; }
    out[n] = 0;
    return out;
}

int main(void)
{
    static const struct { uint8_t mode; uint16_t pid; uint8_t cls, sub, proto; const char* name; } modes[] = {
        {0, 0x4F54, 0x02, 0x02, 0x00, "UFI Flux Engine"},
        {1, 0x4F55, 0x00, 0x00, 0x00, "UFI Flux Storage"},
        {2, 0x4F56, 0x00, 0x00, 0x00, "UFI USB Floppy"},
        {3, 0x4F57, 0x02, 0x02, 0x00, "UFI Flux Engine (gw-compat)"},
        {4, 0x4F58, 0xEF, 0x02, 0x01, "UFI Native + Flux (gw-compat)"},
    };
    uid_words[0] = 0x00123456u; uid_words[1] = 0xABCD9876u; uid_words[2] = 0x10000000u;
    const char* serial = "10123456ABCD";                        /* word0 + word2 as eight digits, the top four of word1 */

    /* before any mode is set: the flux device */
    uint16_t len = 0;
    const uint8_t* dev = USBD_HS_DeviceDescriptor(0, &len);
    assert(len == 18 && dev[0] == 18 && dev[1] == USB_DESC_TYPE_DEVICE && dev[2] == 0x00 && dev[3] == 0x02 && dev[7] == 64);
    assert(dev[8] == 0x09 && dev[9] == 0x12 && dev[10] == 0x54 && dev[11] == 0x4F && dev[4] == 0x02);
    assert(dev[14] == 1 && dev[15] == 2 && dev[16] == 3 && dev[17] == 1);      /* string indices, one configuration */

    for (unsigned k = 0; k < 5; k++) {
        usbd_desc_set_mode(modes[k].mode);
        dev = USBD_HS_DeviceDescriptor(0, &len);
        assert(len == 18 && dev[8] == 0x09 && dev[9] == 0x12);                  /* VID 1209 in every personality */
        assert(dev[10] == (modes[k].pid & 0xFF) && dev[11] == (modes[k].pid >> 8));
        assert(dev[4] == modes[k].cls && dev[5] == modes[k].sub && dev[6] == modes[k].proto);
        for (unsigned j = 0; j < k; j++) assert(modes[j].pid != modes[k].pid);  /* no driver binding is reused */
        assert(!strcmp(text(USBD_HS_ProductStrDescriptor), modes[k].name));
        /* the serial number is the same board in every personality, and well formed after any other string */
        uint8_t* (*const others[])(USBD_SpeedTypeDef, uint16_t*) = {USBD_HS_ProductStrDescriptor, USBD_HS_ManufacturerStrDescriptor,
            USBD_HS_ConfigStrDescriptor, USBD_HS_InterfaceStrDescriptor, USBD_HS_SerialStrDescriptor};
        for (unsigned o = 0; o < 5; o++) {
            uint16_t ignore;
            (void)others[o](0, &ignore);
            assert(!strcmp(text(USBD_HS_SerialStrDescriptor), serial));
        }
        assert(!strcmp(text(USBD_HS_ManufacturerStrDescriptor), "UFT Project"));
        assert(strstr(modes[k].name, "gw-compat") ? (modes[k].mode >= 3) : (modes[k].mode < 3));   /* what the gw tools look for */
    }
    /* an unknown mode is the flux device, not an index behind the tables */
    usbd_desc_set_mode(5); dev = USBD_HS_DeviceDescriptor(0, &len);
    assert(dev[10] == 0x54 && dev[11] == 0x4F && dev[4] == 0x02 && !strcmp(text(USBD_HS_ProductStrDescriptor), "UFI Flux Engine"));
    assert(dev[5] == 0x02 && dev[6] == 0x00);                                  /* nothing of the composite descriptor is left */
    usbd_desc_set_mode(0xFF); assert(!strcmp(text(USBD_HS_ProductStrDescriptor), "UFI Flux Engine"));

    /* serial number: twelve upper-case hex digits; a blank ID still gives a proper string */
    uid_words[0] = 0xFFFFFFFFu; uid_words[1] = 0x0000FFFFu; uid_words[2] = 0x00000002u;
    assert(!strcmp(text(USBD_HS_SerialStrDescriptor), "000000010000"));          /* the sum wraps; only the top half of word 1 is used */
    uid_words[0] = uid_words[1] = uid_words[2] = 0;
    (void)USBD_HS_ProductStrDescriptor(0, &len);
    assert(!strcmp(text(USBD_HS_SerialStrDescriptor), "000000000000"));
    /* language ID: English (US) */
    const uint8_t* lang = USBD_HS_LangIDStrDescriptor(0, &len);
    assert(len == 4 && lang[0] == 4 && lang[1] == USB_DESC_TYPE_STRING && lang[2] == 0x09 && lang[3] == 0x04);
    assert(HS_Desc.device == USBD_HS_DeviceDescriptor && HS_Desc.serial == USBD_HS_SerialStrDescriptor && HS_Desc.product == USBD_HS_ProductStrDescriptor);
    puts("usb descriptors: five personalities (VID:PID, class, name), well-formed strings, serial number from the unique ID");
    return 0;
}
'''
    run('regression_usbd_desc', code)


if __name__ == '__main__':
    main()
