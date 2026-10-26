/* LAN address (ufi_net.c) against the register-level W5500 fake: DHCP exchange with a
 * scripted server, address check and DECLINE, NAK, renewal / rebinding / expiry,
 * link-local fallback with ARP probes and conflicts, upgrade to DHCP, fixed address,
 * and the DHCP message codec on its own. */
#include "check.h"
#include "w5500_fake.h"
#include "ufi_net.h"
#include <string.h>

static w5500_fake_t f;
static w5500_t d;
static net_t net;
static uint8_t pkt[NET_PKT_MAX];
static lan_cfg_t cfg;
static const uint8_t MAC[6] = {0x02, 0x20, 0x64, 0x02, 0xca, 0xb3};
static const uint8_t SERVER[4] = {192, 168, 1, 1}, OFFER[4] = {192, 168, 1, 77};
static uint32_t now;
static unsigned seen;                       /* f.out entries already looked at */

static bool ip_is(const uint8_t* ip, unsigned a, unsigned b, unsigned c, unsigned e)
{
    return ip[0] == a && ip[1] == b && ip[2] == c && ip[3] == e;
}

/* next DHCP message the client sent, NULL if none */
static const fake_pkt_t* next_dhcp(uint8_t* type, uint32_t* xid)
{
    while (seen < f.out_n) {
        const fake_pkt_t* p = &f.out[seen++];
        if (p->sock == W5500_SOCK_DHCP && p->port == 67 && p->sport == 68 && p->len >= 243) {
            *xid = ((uint32_t)p->data[4] << 24) | ((uint32_t)p->data[5] << 16) | ((uint32_t)p->data[6] << 8) | p->data[7];
            *type = 0;
            for (unsigned i = 240; i + 2 < p->len && p->data[i] != 255; i += 2u + p->data[i + 1])
                if (p->data[i] == 53) *type = p->data[i + 2];
            return p;
        }
    }
    return NULL;
}

static const fake_pkt_t* next_probe(void)
{
    while (seen < f.out_n) {
        const fake_pkt_t* p = &f.out[seen++];
        if (p->sock == W5500_SOCK_PROBE) return p;
    }
    return NULL;
}

static bool has_option(const fake_pkt_t* p, uint8_t code, const uint8_t* val, uint8_t len)
{
    for (unsigned i = 240; i + 2 < p->len && p->data[i] != 255; i += 2u + p->data[i + 1])
        if (p->data[i] == code) return !val || (p->data[i + 1] == len && !memcmp(&p->data[i + 2], val, len));
    return false;
}

/* server reply: type, xid, yiaddr, lease */
static void reply(uint8_t type, uint32_t xid, const uint8_t yiaddr[4], uint32_t lease)
{
    uint8_t m[400];
    memset(m, 0, sizeof(m));
    m[0] = 2; m[1] = 1; m[2] = 6;
    m[4] = (uint8_t)(xid >> 24); m[5] = (uint8_t)(xid >> 16); m[6] = (uint8_t)(xid >> 8); m[7] = (uint8_t)xid;
    memcpy(&m[16], yiaddr, 4);
    memcpy(&m[28], MAC, 6);
    m[236] = 0x63; m[237] = 0x82; m[238] = 0x53; m[239] = 0x63;
    unsigned i = 240;
    m[i++] = 53; m[i++] = 1; m[i++] = type;
    m[i++] = 54; m[i++] = 4; memcpy(&m[i], SERVER, 4); i += 4;
    m[i++] = 0;                                         /* a pad byte */
    m[i++] = 1; m[i++] = 4; m[i++] = 255; m[i++] = 255; m[i++] = 255; m[i++] = 0;
    m[i++] = 3; m[i++] = 8; memcpy(&m[i], SERVER, 4); m[i + 4] = 10; i += 8;   /* two routers: the first counts */
    m[i++] = 6; m[i++] = 4; m[i++] = 9; m[i++] = 9; m[i++] = 9; m[i++] = 9;
    m[i++] = 51; m[i++] = 4; m[i++] = (uint8_t)(lease >> 24); m[i++] = (uint8_t)(lease >> 16);
    m[i++] = (uint8_t)(lease >> 8); m[i++] = (uint8_t)lease;
    m[i++] = 255;
    fake_udp_rx(&f, W5500_SOCK_DHCP, SERVER, 67, m, 300);
}

static void run(uint32_t ms, bool busy)
{
    for (uint32_t t = 0; t < ms; t += 50) { now += 50; net_service(&net, &d, now, busy); }
}

static void setup(bool dhcp)
{
    fake_init(&f);
    d = fake_dev(&f);
    (void)w5500_reset(&d);
    (void)w5500_setup(&d, MAC);
    lan_cfg_defaults(&cfg);
    cfg.dhcp = dhcp;
    seen = 0;
    now = 1000;
    memset(&net, 0, sizeof(net));
    net_start(&net, &d, &cfg, MAC, "ufi-02cab3", pkt, now);
}

/* DISCOVER -> OFFER -> REQUEST -> ACK -> probe -> bound; returns the REQUEST xid */
static uint32_t lease_up(uint32_t lease)
{
    uint8_t t; uint32_t x;
    const fake_pkt_t* p = next_dhcp(&t, &x);
    CHECK(p && t == DHCP_DISCOVER && ip_is(p->ip, 255, 255, 255, 255) && ip_is(p->src_ip, 0, 0, 0, 0), "DISCOVER from 0.0.0.0");
    reply(DHCP_OFFER, x, OFFER, lease);
    run(50, false);
    uint32_t x2;
    p = next_dhcp(&t, &x2);
    CHECK(p && t == DHCP_REQUEST && x2 == x && has_option(p, 50, OFFER, 4) && has_option(p, 54, SERVER, 4) &&
          net.state == NET_REQUEST, "REQUEST for the offer");
    reply(DHCP_ACK, x, OFFER, lease);
    run(50, false);
    p = next_probe();
    CHECK(p && ip_is(p->ip, 192, 168, 1, 77) && p->port == 9 && ip_is(p->src_ip, 0, 0, 0, 0) &&
          ip_is(p->mask, 0, 0, 0, 0) && ip_is(p->gw, 192, 168, 1, 77) && !p->arp_ok, "ARP probe of the offer");
    run(50, false);
    return x;
}

int main(void)
{
    /* ---- DHCP message codec --------------------------------------------------- */
    {
        uint8_t m[NET_PKT_MAX];
        const uint16_t n = dhcp_build(m, DHCP_DISCOVER, 0x11223344u, MAC, "ufi-02cab3", NULL, NULL, NULL);
        CHECK(n == 300 && m[0] == 1 && m[1] == 1 && m[2] == 6 && m[4] == 0x11 && m[7] == 0x44 && m[10] == 0x80 &&
              !memcmp(&m[28], MAC, 6) && m[236] == 0x63 && m[239] == 0x63, "DISCOVER header");
        fake_pkt_t p; memset(&p, 0, sizeof(p)); memcpy(p.data, m, n); p.len = n;
        const uint8_t cid[7] = {1, 0x02, 0x20, 0x64, 0x02, 0xca, 0xb3};
        CHECK(has_option(&p, 53, (const uint8_t*)"\x01", 1) && has_option(&p, 61, cid, 7) &&
              has_option(&p, 12, (const uint8_t*)"ufi-02cab3", 10) && has_option(&p, 55, NULL, 0) &&
              !has_option(&p, 50, NULL, 0), "DISCOVER options");
        const uint16_t n2 = dhcp_build(m, DHCP_DECLINE, 1, MAC, "x", NULL, OFFER, SERVER);
        memcpy(p.data, m, n2); p.len = n2;
        CHECK(has_option(&p, 50, OFFER, 4) && has_option(&p, 54, SERVER, 4) && !has_option(&p, 12, NULL, 0), "DECLINE options");
        char host[40]; memset(host, 'h', sizeof(host)); host[39] = 0;
        CHECK(dhcp_build(m, DHCP_REQUEST, 1, MAC, host, NULL, OFFER, SERVER) == 307 && m[240 + 3 + 9 + 6 + 6 + 1] == 32,
              "host name capped at 32");

        /* parser */
        uint8_t r[400]; memset(r, 0, sizeof(r));
        r[0] = 2; r[1] = 1; r[2] = 6; r[7] = 5; memcpy(&r[16], OFFER, 4); memcpy(&r[28], MAC, 6);
        r[236] = 0x63; r[237] = 0x82; r[238] = 0x53; r[239] = 0x63;
        const uint8_t opts[] = {53, 1, 5, 54, 4, 192, 168, 1, 1, 51, 4, 0, 0, 0x0e, 0x10, 58, 4, 0, 0, 0, 100, 255};
        memcpy(&r[240], opts, sizeof(opts));
        dhcp_reply_t o;
        CHECK(dhcp_parse(r, 300, 5, MAC, &o) && o.type == DHCP_ACK && o.has_server && o.lease_s == 3600 && o.t1_s == 100 &&
              ip_is(o.yiaddr, 192, 168, 1, 77) && !o.has_mask, "ACK parsed");
        CHECK(!dhcp_parse(r, 300, 6, MAC, &o), "other transaction");
        uint8_t mac2[6]; memcpy(mac2, MAC, 6); mac2[5] ^= 1;
        CHECK(!dhcp_parse(r, 300, 5, mac2, &o), "other client");
        CHECK(!dhcp_parse(r, 239, 5, MAC, &o), "shorter than the fixed part");
        r[0] = 1; CHECK(!dhcp_parse(r, 300, 5, MAC, &o), "BOOTREQUEST is not a reply"); r[0] = 2;
        r[239] = 0; CHECK(!dhcp_parse(r, 300, 5, MAC, &o), "no magic cookie"); r[239] = 0x63;
        CHECK(!dhcp_parse(r, 250, 5, MAC, &o), "option runs past the end (lease cut)");
        CHECK(!dhcp_parse(r, 241, 5, MAC, &o), "option length byte missing");
        r[240] = 0; r[241] = 0; r[242] = 255;
        CHECK(!dhcp_parse(r, 300, 5, MAC, &o), "no message type");
    }

    /* ---- DHCP: full lease ----------------------------------------------------- */
    setup(true);
    CHECK(net.state == NET_DISCOVER && !net_has_address(&net), "discovering");
    const uint16_t c0 = net.changes;
    lease_up(3600);
    CHECK(net.state == NET_BOUND && net.mode == NET_MODE_DHCP && ip_is(net.ip, 192, 168, 1, 77) &&
          ip_is(net.mask, 255, 255, 255, 0) && ip_is(net.gw, 192, 168, 1, 1) && ip_is(net.dns, 9, 9, 9, 9), "bound");
    CHECK(ip_is(&f.common[W5500_SIPR], 192, 168, 1, 77) && ip_is(&f.common[W5500_SUBR], 255, 255, 255, 0) &&
          ip_is(&f.common[W5500_GAR], 192, 168, 1, 1), "chip addresses");
    CHECK(f.common[W5500_RTR] == 0x07 && f.common[W5500_RTR + 1] == 0xD0 && f.common[W5500_RCR] == 8, "retry timing restored");
    CHECK(net.changes == c0 + 1 && net_lease_left(&net, now) == 3600, "one change, lease %u", (unsigned)net_lease_left(&net, now));

    /* renewal at T1 (1800 s): unicast to the server, ciaddr set */
    fake_arp_add(&f, SERVER);
    run(1799u * 1000u, false);
    uint8_t t; uint32_t x;
    CHECK(!next_dhcp(&t, &x), "quiet before T1");
    run(1000, false);
    const fake_pkt_t* p = next_dhcp(&t, &x);
    CHECK(p && t == DHCP_REQUEST && ip_is(p->ip, 192, 168, 1, 1) && !memcmp(&p->data[12], OFFER, 4) &&
          !has_option(p, 50, NULL, 0) && !has_option(p, 54, NULL, 0) && p->arp_ok && net.state == NET_RENEW, "RENEW");
    reply(DHCP_ACK, x, OFFER, 7200);
    run(50, false);
    CHECK(net.state == NET_BOUND && net_lease_left(&net, now) == 7200 && net.changes == c0 + 1, "renewed, address kept");
    /* no answer: retries every 10 s, rebinding at T2 (7/8), expiry drops the address */
    run(3600u * 1000u + 10000u, false);
    unsigned renews = 0;
    while ((p = next_dhcp(&t, &x))) renews += t == DHCP_REQUEST && ip_is(p->ip, 192, 168, 1, 1);
    CHECK(net.state == NET_RENEW && renews >= 2, "RENEW retries (%u)", renews);
    run(2700u * 1000u, false);
    bool rebind = false;
    while ((p = next_dhcp(&t, &x))) rebind = rebind || (t == DHCP_REQUEST && ip_is(p->ip, 255, 255, 255, 255));
    CHECK(net.state == NET_REBIND && rebind, "REBIND broadcast at T2");
    run(900u * 1000u, false);
    CHECK(!net_has_address(&net) && net.state == NET_DISCOVER && ip_is(&f.common[W5500_SIPR], 0, 0, 0, 0), "expired");

    /* NAK on REQUEST: start over */
    setup(true);
    CHECK(next_dhcp(&t, &x) && t == DHCP_DISCOVER, "DISCOVER");
    reply(DHCP_OFFER, x, OFFER, 600);
    run(50, false);
    CHECK(next_dhcp(&t, &x) && t == DHCP_REQUEST, "REQUEST");
    reply(DHCP_NAK, x, OFFER, 0);
    run(50, false);
    p = next_dhcp(&t, &x);
    CHECK(p && t == DHCP_DISCOVER && net.state == NET_DISCOVER, "NAK -> DISCOVER");

    /* acknowledged address in use: DECLINE, 10 s pause, DISCOVER again */
    setup(true);
    fake_arp_add(&f, OFFER);
    CHECK(next_dhcp(&t, &x) && t == DHCP_DISCOVER, "DISCOVER");
    reply(DHCP_OFFER, x, OFFER, 600);
    run(50, false);
    CHECK(next_dhcp(&t, &x) && t == DHCP_REQUEST, "REQUEST");
    reply(DHCP_ACK, x, OFFER, 600);
    run(100, false);
    p = next_dhcp(&t, &x);
    CHECK(p && t == DHCP_DECLINE && has_option(p, 50, OFFER, 4) && net.state == NET_DECLINED && !net_has_address(&net), "DECLINE");
    run(9900, false);
    CHECK(net.state == NET_DECLINED, "pause");
    run(200, false);
    CHECK(next_dhcp(&t, &x) && t == DHCP_DISCOVER && net.state == NET_DISCOVER, "DISCOVER after the pause");

    /* infinite lease: no renewal ever */
    setup(true);
    lease_up(0xFFFFFFFFu);
    CHECK(net.state == NET_BOUND && net_lease_left(&net, now) == 0xFFFFFFFFu, "infinite lease");
    fake_arp_add(&f, SERVER);
    run(3u * 3600u * 1000u, false);                 /* three hours: no T1 ever */
    CHECK(!next_dhcp(&t, &x), "no RENEW");
    /* very long lease: timed as 20 days */
    setup(true);
    lease_up(100u * 86400u);
    CHECK(net.lease_s == NET_LEASE_MAX_S && net.t1_s == NET_LEASE_MAX_S / 2, "lease clamped");

    /* ---- no server: link-local ------------------------------------------------ */
    setup(true);
    unsigned discovers = 0;
    run(19900, false);
    while ((p = next_dhcp(&t, &x))) discovers += t == DHCP_DISCOVER;
    CHECK(discovers == 3 && net.state == NET_DISCOVER, "three DISCOVERs (%u)", discovers);
    run(200, false);
    CHECK(net.state >= NET_LL_WAIT && net.state <= NET_LL_BOUND, "link-local after 20 s (%u)", net.state);
    run(1100, false);
    p = next_probe();
    CHECK(p && p->ip[0] == 169 && p->ip[1] == 254 && p->ip[2] >= 1 && p->ip[2] <= 254 && ip_is(p->src_ip, 0, 0, 0, 0) &&
          ip_is(p->mask, 0, 0, 0, 0) && !memcmp(p->gw, p->ip, 4), "probe of a 169.254 candidate");
    CHECK(p && p->rtr == 15000 && p->rcr == 2 && f.common[W5500_RTR] == 0x07 && f.common[W5500_RCR] == 8, "probe timing 1.5 s x 3, restored");
    uint8_t first[4]; memcpy(first, p->ip, 4);
    run(50, false);
    CHECK(net.state == NET_LL_BOUND && net.mode == NET_MODE_LINKLOCAL && !memcmp(net.ip, first, 4) &&
          ip_is(net.mask, 255, 255, 0, 0) && ip_is(net.gw, 0, 0, 0, 0) && !memcmp(&f.common[W5500_SIPR], first, 4),
          "link-local bound");
    /* the same MAC picks the same first candidate */
    setup(true);
    run(21300, false);
    p = next_probe();
    CHECK(p && !memcmp(p->ip, first, 4), "deterministic first candidate");

    /* first candidate in use: the next one */
    setup(true);
    fake_arp_add(&f, first);
    run(21300, false);
    p = next_probe();
    CHECK(p && !memcmp(p->ip, first, 4) && p->arp_ok, "first candidate answered");
    run(1200, false);
    p = next_probe();
    CHECK(p && p->ip[0] == 169 && memcmp(p->ip, first, 4) && net.conflicts == 1, "second candidate");
    run(50, false);
    CHECK(net.state == NET_LL_BOUND && memcmp(net.ip, first, 4), "bound to the second");

    /* conflict reported by the chip later: give the address up */
    uint8_t second[4]; memcpy(second, net.ip, 4);
    f.common[W5500_IR] |= W5500_IR_CONFLICT;
    run(50, false);
    CHECK(!net_has_address(&net) && net.conflict_seen && net.conflicts == 2 && net.state == NET_LL_WAIT, "conflict -> new probe");
    run(1200, false);
    CHECK(net.state == NET_LL_BOUND && memcmp(net.ip, second, 4), "third address");

    /* upgrade to DHCP after 60 s, not while a session is connected */
    run(61000, true);
    while ((p = next_dhcp(&t, &x))) CHECK(0, "no DISCOVER while a session runs");
    CHECK(net.state == NET_LL_BOUND, "stays link-local");
    run(50, false);
    p = next_dhcp(&t, &x);
    CHECK(p && t == DHCP_DISCOVER && net.mode == NET_MODE_LINKLOCAL && net.ll_fallback, "DHCP retry keeps the address");
    reply(DHCP_OFFER, x, OFFER, 3600);
    run(50, false);
    CHECK(next_dhcp(&t, &x) && t == DHCP_REQUEST, "REQUEST");
    reply(DHCP_ACK, x, OFFER, 3600);
    run(100, false);
    CHECK(net.state == NET_BOUND && net.mode == NET_MODE_DHCP && ip_is(net.ip, 192, 168, 1, 77), "upgraded to DHCP");
    /* retry without an answer returns to link-local with the address unchanged */
    setup(true);
    run(21400, false);
    memcpy(first, net.ip, 4);
    run(60000, false);
    CHECK(net.state == NET_DISCOVER && net.ll_fallback, "retry running");
    run(20100, false);
    CHECK(net.state == NET_LL_BOUND && !memcmp(net.ip, first, 4) && net.mode == NET_MODE_LINKLOCAL, "back to link-local");

    /* ---- fixed address --------------------------------------------------------- */
    fake_init(&f); d = fake_dev(&f); (void)w5500_reset(&d); (void)w5500_setup(&d, MAC);
    lan_cfg_parse(&cfg, "ip=10.1.2.3\nmask=255.0.0.0\ngw=10.0.0.1\ndns=10.0.0.2\n", 52);
    memset(&net, 0, sizeof(net)); seen = 0;
    net_start(&net, &d, &cfg, MAC, "x", pkt, 0);
    CHECK(net.state == NET_STATIC && net.mode == NET_MODE_STATIC && ip_is(&f.common[W5500_SIPR], 10, 1, 2, 3) &&
          ip_is(&f.common[W5500_SUBR], 255, 0, 0, 0) && ip_is(&f.common[W5500_GAR], 10, 0, 0, 1) &&
          ip_is(net.dns, 10, 0, 0, 2) && net_lease_left(&net, 5) == 0xFFFFFFFFu, "static");
    run(30000, false);
    CHECK(!next_dhcp(&t, &x) && net.state == NET_STATIC, "no DHCP traffic");
    f.common[W5500_IR] |= W5500_IR_CONFLICT;
    run(50, false);
    CHECK(net.conflict_seen && net_has_address(&net), "fixed address: conflict reported, kept");
    net_stop(&net, &d);
    CHECK(!net_has_address(&net) && net.state == NET_IDLE && ip_is(&f.common[W5500_SIPR], 0, 0, 0, 0) &&
          net_lease_left(&net, 0) == 0, "stopped");
    CHECK_SUMMARY("net");
}
