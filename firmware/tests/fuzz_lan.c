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
 *   3  the TCP v2 session (ufi_lan_session.c, L4), see fuzz_session().
 *   4  the discovery responder (ufi_lan_status.c, L5), see fuzz_disc().
 * Checks: ASan/UBSan and the asserts below. */
#include "ufi_lan_cfg.h"
#include "ufi_net.h"
#include "ufi_lan_session.h"
#include "ufi_lan_status.h"
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

/* 3: the TCP session (ufi_lan_session.c, L4): byte 0 = key on/off and reply room pattern,
 * then the TCP stream in pieces of 1 + (byte & 63) bytes.  Every dispatched frame has a
 * valid length, nothing passes before authentication, every reply is a well-formed frame. */
static lan_session_t sess;
static uint8_t sess_frame[LAN_V2_FRAME_MAX];
static uint8_t room_bits;
static unsigned room_i;
static bool keyed;
static bool fz_can(void* ctx) { (void)ctx; return (room_bits >> (room_i++ & 7u)) & 1u; }
static void fz_frame(void* ctx, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t* p, uint16_t len)
{
    (void)ctx; (void)cmd; (void)seq;
    assert(len <= LAN_V2_MAX_PAYLOAD && p == sess_frame + LAN_V2_HDR && sess.authed);
    assert(type != 0u);
}
static void fz_nonce(void* ctx, uint8_t out[LAN_NONCE_SIZE]) { (void)ctx; memset(out, 0x11, LAN_NONCE_SIZE); }
static const lan_session_cb_t fz_cb = {NULL, fz_can, fz_frame, fz_nonce, NULL, NULL};

static void fuzz_session(const uint8_t* data, size_t size)
{
    if (!size) {
        return;
    }
    fake_init(&fake);
    const w5500_t d = fake_dev(&fake);
    (void)w5500_reset(&d);
    (void)w5500_setup(&d, FMAC);
    keyed = data[0] & 1u;
    room_bits = (uint8_t)(data[0] | 1u);
    room_i = 0;
    lan_session_init(&sess, 55100, keyed ? "key" : "", keyed ? 3u : 0u, sess_frame);
    static const uint8_t peer[4] = {10, 0, 0, 2};
    lan_session_service(&sess, &d, 1, &fz_cb);
    (void)fake_tcp_accept(&fake, 0, peer, 4000);
    lan_session_service(&sess, &d, 1, &fz_cb);
    for (size_t i = 1; i < size && lan_session_connected(&sess); ) {
        const size_t take = 1u + (data[i] & 63u);
        const size_t n = (take <= size - i - 1u) ? take : size - i - 1u;
        fake_tcp_rx(&fake, 0, data + i + 1, (unsigned)n);
        i += 1u + n;
        for (unsigned k = 0; k < 3u; k++) {
            lan_session_service(&sess, &d, 2, &fz_cb);
        }
    }
    /* every byte the session sent is a sequence of valid frames */
    for (uint32_t o = 0; o < fake.tcp_out_n; ) {
        assert(fake.tcp_out_n - o >= 10u && fake.tcp_out[o] == 0x55u && fake.tcp_out[o + 1] == 0xAAu);
        const uint16_t len = (uint16_t)(fake.tcp_out[o + 6] | fake.tcp_out[o + 7] << 8);
        assert(len <= 34u && fake.tcp_out_n - o >= 10u + len);
        const uint16_t crc = (uint16_t)(fake.tcp_out[o + 8 + len] | fake.tcp_out[o + 9 + len] << 8);
        assert(crc == lan_crc16(&fake.tcp_out[o + 2], 6u + len));
        o += 10u + len;
    }
}

/* 4: the discovery responder (ufi_lan_status.c, L5): datagrams [len][bytes] from a host on
 * the subnet or (len bit7) from elsewhere; every reply is a well-formed "UFI!" packet to
 * the asker or to the broadcast address. */
static void fuzz_disc(const uint8_t* data, size_t size)
{
    fake_init(&fake);
    const w5500_t d = fake_dev(&fake);
    (void)w5500_reset(&d);
    (void)w5500_setup(&d, FMAC);
    lan_status_t s;
    memset(&s, 0, sizeof(s));
    static const uint8_t ip[4] = {192, 168, 1, 50}, mask[4] = {255, 255, 255, 0};
    static const uint8_t near[4] = {192, 168, 1, 9}, far[4] = {10, 1, 2, 3};
    memcpy(s.ip, ip, 4); memcpy(s.mask, mask, 4); memcpy(s.mac, FMAC, 6);
    s.port = 55100; s.mode = 1;
    strcpy(s.host, "ufi-02cab3");
    w5500_set_ip(&d, ip, mask, ip);
    uint8_t buf[LAN_DISC_REPLY_MAX];
    bool inflight = false;
    (void)lan_disc_service(&d, &s, buf, &inflight);
    for (size_t i = 0; i < size; ) {
        const size_t want = data[i] & 127u;
        const size_t take = want <= size - i - 1u ? want : size - i - 1u;
        fake_udp_rx(&fake, W5500_SOCK_DISCOVER, (data[i] & 128u) ? far : near, 40000, data + i + 1, (uint16_t)take);
        i += 1u + take;
        (void)lan_disc_service(&d, &s, buf, &inflight);
    }
    for (unsigned k = 0; k < fake.out_n; k++) {
        const fake_pkt_t* p = &fake.out[k];
        assert(p->sock == W5500_SOCK_DISCOVER && p->len == 32u && !memcmp(p->data, "UFI!", 4));
        assert(!memcmp(p->ip, near, 4) || (p->ip[0] == 255u && p->ip[3] == 255u));
    }
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    if (!size || size > 4096u) {
        return 0;
    }
    switch (data[0] % 5u) {
        case 0: fuzz_cfg(data + 1, size - 1u); break;
        case 1: fuzz_dhcp(data + 1, size - 1u); break;
        case 2: fuzz_net(data + 1, size - 1u); break;
        case 3: fuzz_session(data + 1, size - 1u); break;
        default: fuzz_disc(data + 1, size - 1u); break;
    }
    return 0;
}
