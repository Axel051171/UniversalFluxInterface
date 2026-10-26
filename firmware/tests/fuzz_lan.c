/* LibFuzzer harness for the HAL-free LAN parsers.  Byte 0 selects the parser, the rest
 * is its input:
 *   0  LAN.CFG text (ufi_lan_cfg.c, L2): whatever the file holds, the settings stay within
 *      their bounds and every accepted address / MAC / name is one the firmware may use.
 *   1  one DHCP reply (ufi_net.c, L3): xid (4 bytes, big endian), then the message.
 *   2  the address state machine (ufi_net.c, L3) against the register-level W5500 fake,
 *      driven by records [op][len][bytes]: op & 3 = 0 deliver a datagram to the DHCP
 *      socket (from port 67, with bit2 from another port; bits 4-5 add 256 bytes each to
 *      the length), 1 let len * 100 ms pass, 2 toggle
 *      ARP answers for the address being probed, 3 raise IR.CONFLICT; bit3 = a session is
 *      connected during this step.
 * Checks: ASan/UBSan and the asserts below. */
#include "ufi_lan_cfg.h"
#include "ufi_net.h"
#include "w5500_fake.h"
#include <assert.h>
#include <stddef.h>
#include <string.h>

static w5500_fake_t fake;
static const uint8_t FMAC[6] = {0x02, 0x20, 0x64, 0x02, 0xca, 0xb3};

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
    char host[LAN_HOST_MAX + 1];
    lan_cfg_hostname(&c, FMAC, host);
    assert(strlen(host) <= LAN_HOST_MAX && host[0]);
    /* any dotted quad the parser accepts formats back to the same address */
    uint8_t ip[4], back[4];
    if (lan_parse_ip((const char*)data, size, ip)) {
        char t[16];
        const size_t n = lan_fmt_ip(t, ip);
        assert(n <= 15u && lan_parse_ip(t, n, back) && !memcmp(ip, back, 4));
    }
}

static void fuzz_dhcp(const uint8_t* data, size_t size)
{
    if (size < 4u || size > 4u + NET_PKT_MAX) {
        return;
    }
    const uint32_t xid = ((uint32_t)data[0] << 24) | ((uint32_t)data[1] << 16) | ((uint32_t)data[2] << 8) | data[3];
    dhcp_reply_t r;
    if (dhcp_parse(data + 4, (uint16_t)(size - 4u), xid, FMAC, &r)) {
        assert(r.type != 0u);
    }
}

static void fuzz_net(const uint8_t* data, size_t size)
{
    static net_t n;
    static uint8_t pkt[NET_PKT_MAX];
    fake_init(&fake);
    const w5500_t d = fake_dev(&fake);
    (void)w5500_reset(&d);
    (void)w5500_setup(&d, FMAC);
    lan_cfg_t cfg;
    lan_cfg_defaults(&cfg);
    uint32_t now = 1;
    memset(&n, 0, sizeof(n));
    net_start(&n, &d, &cfg, FMAC, "ufi-02cab3", pkt, now);
    static const uint8_t server[4] = {192, 168, 1, 1};
    for (size_t i = 0; i + 2u <= size; ) {
        const uint8_t op = data[i], len = data[i + 1];
        i += 2u;
        const size_t want = (size_t)len + 256u * ((op >> 4) & 3u);  /* datagrams up to 1023 bytes */
        const size_t take = (want <= size - i) ? want : size - i;
        switch (op & 3u) {
            case 0:
                fake_udp_rx(&fake, W5500_SOCK_DHCP, server, (op & 4u) ? 1234u : 67u, data + i, (uint16_t)take);
                i += take;
                break;
            case 1:
                now += (uint32_t)len * 100u;
                break;
            case 2:
                if (fake.arp_n) fake.arp_n = 0;
                else fake_arp_add(&fake, n.state == NET_CHECK ? n.offer : n.ll);
                break;
            default:
                fake.common[W5500_IR] |= W5500_IR_CONFLICT;
                break;
        }
        now += 50u;
        net_service(&n, &d, now, (op & 8u) != 0u);
        assert(n.state <= NET_STATIC && n.mode <= NET_MODE_STATIC);
        if (n.mode == NET_MODE_LINKLOCAL) {
            assert(n.ip[0] == 169u && n.ip[1] == 254u && n.ip[2] >= 1u && n.ip[2] <= 254u);
        }
        if (n.mode == NET_MODE_DHCP) {
            assert(n.ip[0] && n.ip[0] != 127u && n.ip[0] < 224u);
            assert(n.lease_s == 0xFFFFFFFFu ||
                   (n.lease_s <= NET_LEASE_MAX_S && n.t1_s < n.t2_s && n.t2_s <= n.lease_s));
        }
        /* outside a probe the chip carries exactly the address the state machine reports */
        if (n.state != NET_CHECK && n.state != NET_LL_PROBE) {
            assert(!memcmp(&fake.common[W5500_SIPR], n.ip, 4));
        }
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!size || size > 4096u) {
        return 0;
    }
    switch (data[0] % 3u) {
        case 0: fuzz_cfg(data + 1, size - 1u); break;
        case 1: fuzz_dhcp(data + 1, size - 1u); break;
        default: fuzz_net(data + 1, size - 1u); break;
    }
    return 0;
}
