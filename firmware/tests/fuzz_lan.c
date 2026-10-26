/* LibFuzzer harness for the HAL-free LAN parsers.  Byte 0 selects the parser, the rest
 * is its input:
 *   0  LAN.CFG text (ufi_lan_cfg.c, L2): whatever the file holds, the settings stay within
 *      their bounds and every accepted address / MAC / name is one the firmware may use.
 * Checks: ASan/UBSan and the asserts below. */
#include "ufi_lan_cfg.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

static void fuzz_cfg(const uint8_t* data, size_t size)
{
    lan_cfg_t c;
    lan_cfg_parse(&c, (const char*)data, size);
    assert(c.key_len <= LAN_KEY_MAX && strlen(c.key) == c.key_len);
    assert(strlen(c.hostname) <= LAN_HOST_MAX && c.port != 0u);
    if (!c.dhcp) {
        assert(c.ip[0] != 0u && c.ip[0] != 127u && c.ip[0] < 224u);
    }
    if (c.mac_set) {
        assert(!(c.mac[0] & 1u));
    }
    static const uint8_t mac[6] = {2, 0, 0, 0xab, 0xcd, 0xef};
    char host[LAN_HOST_MAX + 1];
    lan_cfg_hostname(&c, mac, host);
    assert(strlen(host) <= LAN_HOST_MAX && host[0]);
    /* any dotted quad the parser accepts formats back to the same address */
    uint8_t ip[4], back[4];
    if (lan_parse_ip((const char*)data, size, ip)) {
        char t[16];
        const size_t n = lan_fmt_ip(t, ip);
        assert(n <= 15u && lan_parse_ip(t, n, back) && !memcmp(ip, back, 4));
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!size || size > 4096u) {
        return 0;
    }
    switch (data[0]) {
        case 0:
        default:
            fuzz_cfg(data + 1, size - 1u);
            break;
    }
    return 0;
}
