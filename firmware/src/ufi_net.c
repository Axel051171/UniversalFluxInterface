/**
 * UFI Flux Engine - LAN address: DHCP client, link-local fallback, fixed address (L3)
 *
 * DHCP (RFC 2131, options RFC 2132) on W5500 socket 1, UDP 68 -> 67.  Every message sets
 * the BROADCAST flag: until an address is bound SIPR is 0.0.0.0 and the W5500 drops
 * unicast replies to an address it does not have.  DISCOVER is repeated after 4 s and 8 s;
 * without an offer after the third attempt (20 s) the port takes a link-local address and
 * tries DHCP again every 60 s while no session is connected.  An acknowledged address is
 * ARP-probed before use (RFC 2131 3.1 step 5); in use -> DECLINE, 10 s pause, DISCOVER.
 * Renewal at T1 (unicast to the server), rebinding at T2 (broadcast), expiry drops the
 * address.  Leases above 20 days are timed as 20 days (32-bit millisecond timer).
 *
 * Link-local (RFC 3927): candidate 169.254.1.0 - 169.254.254.255 from a generator seeded
 * with the MAC (the same board picks the same first address after a restart), random
 * 0-1 s wait, then the probe; a conflict picks the next candidate, after 10 conflicts one
 * probe per minute.  A conflict reported later by the chip (IR.CONFLICT) moves a
 * link-local address too.  No ARP announcements: the W5500 has no gratuitous ARP outside
 * MACRAW, and hosts resolve the address on first contact anyway.
 *
 * ARP probe without MACRAW: socket 3 sends one UDP byte to the discard port of the
 * candidate while SIPR is 0.0.0.0.  The W5500 resolves the destination first, and that
 * ARP request carries sender address 0.0.0.0 - exactly the RFC 3927 probe.  SUBR 0.0.0.0
 * makes every destination local and GAR = candidate covers the routed path as well, so the
 * request asks for the candidate either way.  An ARP answer means the address is in use
 * (SEND_OK); no answer within RTR x (RCR + 1) means free (TIMEOUT).  Link-local probes run
 * with RTR 1.5 s, RCR 2 (three requests 1.5 s apart, PROBE_NUM / PROBE_MIN..MAX), the
 * DHCP check with 0.5 s, RCR 1.  Chosen over MACRAW because it needs no frame building
 * and no extra buffer; whether the chip really sends the probe with sender 0.0.0.0 can
 * only be confirmed with a capture on the bench.
 */

#include "ufi_net.h"
#include <string.h>

#define DHCP_MAGIC          0x63825363u
#define DHCP_MIN_LEN        300u            /* BOOTP minimum message */
#define T_DISCOVER_MS       4000u
#define T_DISCOVER2_MS      8000u
#define T_REQUEST_MS        4000u
#define T_RENEW_MS          10000u
#define T_DECLINE_MS        10000u
#define T_PROBE_MAX_MS      10000u          /* a probe never answered by the chip counts as free */
#define DHCP_TRIES          3u
#define LL_DHCP_RETRY_MS    60000u
#define LL_PROBE_WAIT_MS    1000u
#define LL_MAX_CONFLICTS    10u
#define LL_RATE_LIMIT_MS    60000u
#define RTR_DEFAULT         2000u           /* chip reset values: 200 ms, 8 retries */
#define RCR_DEFAULT         8u
#define RTR_PROBE_LL        15000u
#define RCR_PROBE_LL        2u
#define RTR_PROBE_DHCP      5000u
#define RCR_PROBE_DHCP      1u
#define DEFAULT_LEASE_S     3600u
#define UDP_RX_BUF          2048u           /* socket 1 RX buffer (w5500_setup) */

static const uint8_t ZERO[4] = {0, 0, 0, 0};
static const uint8_t BCAST[4] = {255, 255, 255, 255};
static const uint8_t LL_MASK[4] = {255, 255, 0, 0};

static uint32_t get32(const uint8_t* p)
{
    return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) | ((uint32_t)p[2] << 8) | p[3];
}

static void put32(uint8_t* p, uint32_t v)
{
    p[0] = (uint8_t)(v >> 24); p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v;
}

/* ============================================================================
 * DHCP MESSAGES
 * ============================================================================ */

uint16_t dhcp_build(uint8_t* o, uint8_t type, uint32_t xid, const uint8_t mac[6], const char* host,
                    const uint8_t ciaddr[4], const uint8_t req_ip[4], const uint8_t server[4])
{
    memset(o, 0, DHCP_MIN_LEN);
    o[0] = 1;                           /* BOOTREQUEST */
    o[1] = 1;                           /* Ethernet */
    o[2] = 6;
    put32(&o[4], xid);
    o[10] = 0x80u;                      /* flags: BROADCAST */
    if (ciaddr) {
        memcpy(&o[12], ciaddr, 4u);
    }
    memcpy(&o[28], mac, 6u);
    put32(&o[236], DHCP_MAGIC);
    uint16_t i = 240;
    o[i++] = 53; o[i++] = 1; o[i++] = type;
    o[i++] = 61; o[i++] = 7; o[i++] = 1;            /* client id: hardware type + MAC */
    memcpy(&o[i], mac, 6u); i += 6u;
    if (req_ip) {
        o[i++] = 50; o[i++] = 4; memcpy(&o[i], req_ip, 4u); i += 4u;
    }
    if (server) {
        o[i++] = 54; o[i++] = 4; memcpy(&o[i], server, 4u); i += 4u;
    }
    if (type != DHCP_DECLINE && type != DHCP_RELEASE) {
        size_t hl = host ? strlen(host) : 0u;
        hl = hl > LAN_HOST_MAX ? LAN_HOST_MAX : hl;
        if (hl) {
            o[i++] = 12; o[i++] = (uint8_t)hl; memcpy(&o[i], host, hl); i = (uint16_t)(i + hl);
        }
        static const uint8_t prl[] = {55, 6, 1, 3, 6, 51, 58, 59};   /* mask, router, DNS, lease, T1, T2 */
        memcpy(&o[i], prl, sizeof(prl)); i += (uint16_t)sizeof(prl);
    }
    o[i++] = 255;
    return i < DHCP_MIN_LEN ? (uint16_t)DHCP_MIN_LEN : i;
}

bool dhcp_parse(const uint8_t* p, uint16_t n, uint32_t xid, const uint8_t mac[6], dhcp_reply_t* r)
{
    memset(r, 0, sizeof(*r));
    if (n < 240u || p[0] != 2u || p[1] != 1u || p[2] != 6u || get32(&p[4]) != xid ||
        memcmp(&p[28], mac, 6u) != 0 || get32(&p[236]) != DHCP_MAGIC) {
        return false;
    }
    memcpy(r->yiaddr, &p[16], 4u);
    uint16_t i = 240;
    while (i < n) {
        const uint8_t code = p[i++];
        if (code == 0u) {
            continue;                   /* pad */
        }
        if (code == 255u) {
            break;
        }
        if (i >= n) {
            return false;
        }
        const uint8_t len = p[i++];
        if (len > n - i) {
            return false;               /* option runs past the message */
        }
        const uint8_t* v = &p[i];
        switch (code) {
            case 53: if (len >= 1u) r->type = v[0]; break;
            case 54: if (len >= 4u) { memcpy(r->server, v, 4u); r->has_server = true; } break;
            case 1:  if (len >= 4u) { memcpy(r->mask, v, 4u); r->has_mask = true; } break;
            case 3:  if (len >= 4u) { memcpy(r->router, v, 4u); r->has_router = true; } break;
            case 6:  if (len >= 4u) { memcpy(r->dns, v, 4u); r->has_dns = true; } break;
            case 51: if (len >= 4u) r->lease_s = get32(v); break;
            case 58: if (len >= 4u) r->t1_s = get32(v); break;
            case 59: if (len >= 4u) r->t2_s = get32(v); break;
            default: break;             /* overload (52) and the rest: not used */
        }
        i = (uint16_t)(i + len);
    }
    return r->type != 0u;
}

/* ============================================================================
 * HELPERS
 * ============================================================================ */

static uint32_t rnd(net_t* n)
{
    uint32_t x = n->rng;                /* xorshift32, never 0 once seeded non-zero */
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    n->rng = x;
    return x;
}

static void enter(net_t* n, uint8_t state, uint32_t now, uint32_t wait)
{
    n->state = state;
    n->since = now;
    n->wait = wait;
}

static void set_addr(net_t* n, const w5500_t* d, uint8_t mode, const uint8_t ip[4], const uint8_t mask[4],
                     const uint8_t gw[4], const uint8_t dns[4])
{
    memcpy(n->ip, ip, 4u);
    memcpy(n->mask, mask, 4u);
    memcpy(n->gw, gw, 4u);
    memcpy(n->dns, dns, 4u);
    n->mode = mode;
    n->changes++;
    n->conflict_seen = false;
    w5500_set_ip(d, ip, mask, gw);
    w5500_write8(d, W5500_COMMON, W5500_IR, W5500_IR_CONFLICT);    /* a stale conflict is not ours */
}

static void clear_addr(net_t* n, const w5500_t* d)
{
    if (n->mode != NET_MODE_NONE) {
        n->changes++;
    }
    n->mode = NET_MODE_NONE;
    memset(n->ip, 0, 4u);
    memset(n->mask, 0, 4u);
    memset(n->gw, 0, 4u);
    memset(n->dns, 0, 4u);
    w5500_set_ip(d, ZERO, ZERO, ZERO);
}

static void dhcp_send(net_t* n, const w5500_t* d, uint8_t type, const uint8_t dest[4],
                      const uint8_t ciaddr[4], const uint8_t req_ip[4], const uint8_t server[4])
{
    if (w5500_sock_status(d, W5500_SOCK_DHCP) != W5500_SOCK_UDP) {
        (void)w5500_sock_open(d, W5500_SOCK_DHCP, W5500_MODE_UDP, DHCP_CLIENT_PORT);
    }
    const uint16_t len = dhcp_build(n->pkt, type, n->xid, n->mac, n->host, ciaddr, req_ip, server);
    if (w5500_tx_free(d, W5500_SOCK_DHCP) < len) {
        return;                         /* previous message still leaving; the timer resends */
    }
    w5500_sock_dest(d, W5500_SOCK_DHCP, dest, DHCP_SERVER_PORT);
    w5500_tx_put(d, W5500_SOCK_DHCP, n->pkt, len);
    w5500_sock_ir_clear(d, W5500_SOCK_DHCP, W5500_SIR_SEND_OK | W5500_SIR_TIMEOUT);
    (void)w5500_sock_cmd(d, W5500_SOCK_DHCP, W5500_CMD_SEND);
}

/* Next DHCP reply for our transaction; other datagrams are dropped. */
static bool dhcp_recv(net_t* n, const w5500_t* d, dhcp_reply_t* r)
{
    if (w5500_sock_status(d, W5500_SOCK_DHCP) != W5500_SOCK_UDP) {
        return false;
    }
    w5500_sock_ir_clear(d, W5500_SOCK_DHCP, W5500_SIR_RECV);
    for (unsigned k = 0; k < 4u; k++) {
        const uint16_t avail = w5500_rx_size(d, W5500_SOCK_DHCP);
        if (avail < W5500_UDP_HDR) {
            return false;
        }
        uint8_t h[W5500_UDP_HDR];
        w5500_rx_peek(d, W5500_SOCK_DHCP, 0, h, sizeof(h));
        const uint16_t len = (uint16_t)((h[6] << 8) | h[7]);
        const uint16_t port = (uint16_t)((h[4] << 8) | h[5]);
        if (W5500_UDP_HDR + (uint32_t)len > UDP_RX_BUF || avail < W5500_UDP_HDR + len) {
            /* not a datagram boundary: start the socket over rather than parse garbage */
            (void)w5500_sock_open(d, W5500_SOCK_DHCP, W5500_MODE_UDP, DHCP_CLIENT_PORT);
            return false;
        }
        bool ok = false;
        if (len <= NET_PKT_MAX && port == DHCP_SERVER_PORT) {
            w5500_rx_peek(d, W5500_SOCK_DHCP, W5500_UDP_HDR, n->pkt, len);
            ok = dhcp_parse(n->pkt, len, n->xid, n->mac, r);
        }
        w5500_rx_skip(d, W5500_SOCK_DHCP, (uint16_t)(W5500_UDP_HDR + len));
        if (ok) {
            return true;
        }
    }
    return false;
}

static void probe_start(net_t* n, const w5500_t* d, const uint8_t target[4], uint16_t rtr, uint8_t rcr)
{
    (void)n;
    w5500_set_ip(d, ZERO, ZERO, target);
    w5500_retry(d, rtr, rcr);
    (void)w5500_sock_open(d, W5500_SOCK_PROBE, W5500_MODE_UDP, NET_PROBE_PORT);
    w5500_sock_dest(d, W5500_SOCK_PROBE, target, 9u);
    const uint8_t b = 0;
    w5500_tx_put(d, W5500_SOCK_PROBE, &b, 1u);
    (void)w5500_sock_cmd(d, W5500_SOCK_PROBE, W5500_CMD_SEND);
}

/* -1 still waiting, 0 free, 1 in use; restores timing and the address registers */
static int probe_result(net_t* n, const w5500_t* d, uint32_t elapsed)
{
    const uint8_t ir = w5500_sock_ir(d, W5500_SOCK_PROBE);
    if (!(ir & (W5500_SIR_SEND_OK | W5500_SIR_TIMEOUT)) && elapsed < T_PROBE_MAX_MS) {
        return -1;
    }
    w5500_sock_close(d, W5500_SOCK_PROBE);
    w5500_retry(d, RTR_DEFAULT, RCR_DEFAULT);
    w5500_set_ip(d, n->ip, n->mask, n->gw);
    return (ir & W5500_SIR_SEND_OK) ? 1 : 0;
}

static void dhcp_begin(net_t* n, const w5500_t* d, uint32_t now)
{
    n->xid = rnd(n) ^ now;
    n->tries = 1;
    dhcp_send(n, d, DHCP_DISCOVER, BCAST, NULL, NULL, NULL);
    enter(n, NET_DISCOVER, now, T_DISCOVER_MS);
}

static void ll_pick(net_t* n)
{
    const uint32_t r = rnd(n);
    n->ll[0] = 169u;
    n->ll[1] = 254u;
    n->ll[2] = (uint8_t)(1u + r % 254u);    /* 1..254: the first and last /24 are reserved */
    n->ll[3] = (uint8_t)(r >> 16);
}

static void ll_wait(net_t* n, uint32_t now)
{
    enter(n, NET_LL_WAIT, now, n->conflicts >= LL_MAX_CONFLICTS ? LL_RATE_LIMIT_MS : rnd(n) % LL_PROBE_WAIT_MS);
}

/* DHCP gave up (or was never possible): link-local, DHCP again later from NET_LL_BOUND */
static void dhcp_failed(net_t* n, uint32_t now)
{
    if (n->ll_fallback) {
        n->ll_fallback = false;
        enter(n, NET_LL_BOUND, now, LL_DHCP_RETRY_MS);  /* the link-local address stays */
    } else {
        ll_wait(n, now);
    }
}

static uint32_t clamp_lease(uint32_t s)
{
    return s > NET_LEASE_MAX_S ? NET_LEASE_MAX_S : s;
}

static void bind_dhcp(net_t* n, const w5500_t* d, uint32_t now)
{
    set_addr(n, d, NET_MODE_DHCP, n->offer, n->offer_mask, n->offer_gw, n->offer_dns);
    n->bound_ms = now;
    n->ll_fallback = false;
    enter(n, NET_BOUND, now, 0);
}

/* lease, T1, T2 from an ACK (defaults T1 = lease/2, T2 = 7/8 lease) */
static void take_lease(net_t* n, const dhcp_reply_t* r)
{
    if (r->lease_s == 0xFFFFFFFFu) {
        n->lease_s = n->t1_s = n->t2_s = 0xFFFFFFFFu;
        return;
    }
    n->lease_s = clamp_lease(r->lease_s ? r->lease_s : DEFAULT_LEASE_S);
    n->t1_s = (r->t1_s && r->t1_s < n->lease_s) ? r->t1_s : n->lease_s / 2u;
    n->t2_s = (r->t2_s && r->t2_s < n->lease_s && r->t2_s > n->t1_s) ? r->t2_s : n->lease_s - n->lease_s / 8u;
    if (n->t2_s <= n->t1_s) {
        n->t2_s = n->t1_s + 1u;
    }
}

static bool usable(const uint8_t ip[4])
{
    return ip[0] != 0u && ip[0] != 127u && ip[0] < 224u;
}

/* ============================================================================
 * API
 * ============================================================================ */

void net_start(net_t* n, const w5500_t* d, const lan_cfg_t* cfg, const uint8_t mac[6],
               const char* host, uint8_t* pkt, uint32_t now)
{
    const uint16_t changes = n->changes;    /* keeps counting across restarts */
    memset(n, 0, sizeof(*n));
    n->changes = changes;
    n->pkt = pkt;
    memcpy(n->mac, mac, 6u);
    strncpy(n->host, host ? host : "", LAN_HOST_MAX);
    n->host[LAN_HOST_MAX] = '\0';
    uint32_t seed = 2166136261u;            /* FNV-1a over the MAC */
    for (unsigned i = 0; i < 6u; i++) {
        seed = (seed ^ mac[i]) * 16777619u;
    }
    n->rng = seed ? seed : 1u;
    n->use_dhcp = cfg->dhcp;
    w5500_retry(d, RTR_DEFAULT, RCR_DEFAULT);
    w5500_set_ip(d, ZERO, ZERO, ZERO);
    if (!cfg->dhcp) {
        set_addr(n, d, NET_MODE_STATIC, cfg->ip, cfg->mask, cfg->gw, cfg->dns);
        enter(n, NET_STATIC, now, 0);
        return;
    }
    dhcp_begin(n, d, now);
}

void net_stop(net_t* n, const w5500_t* d)
{
    w5500_sock_close(d, W5500_SOCK_DHCP);
    w5500_sock_close(d, W5500_SOCK_PROBE);
    w5500_retry(d, RTR_DEFAULT, RCR_DEFAULT);
    clear_addr(n, d);
    n->state = NET_IDLE;
}

bool net_has_address(const net_t* n)
{
    return n->mode != NET_MODE_NONE;
}

uint32_t net_lease_left(const net_t* n, uint32_t now)
{
    if (n->mode != NET_MODE_DHCP) {
        return n->mode == NET_MODE_NONE ? 0u : 0xFFFFFFFFu;
    }
    if (n->lease_s == 0xFFFFFFFFu) {
        return 0xFFFFFFFFu;
    }
    const uint32_t s = (now - n->bound_ms) / 1000u;
    return s >= n->lease_s ? 0u : n->lease_s - s;
}

void net_service(net_t* n, const w5500_t* d, uint32_t now, bool session_busy)
{
    const uint32_t el = now - n->since;
    dhcp_reply_t r;

    if (n->state == NET_IDLE || n->state == NET_STATIC) {
        if (n->mode != NET_MODE_NONE && (w5500_read8(d, W5500_COMMON, W5500_IR) & W5500_IR_CONFLICT)) {
            w5500_write8(d, W5500_COMMON, W5500_IR, W5500_IR_CONFLICT);
            n->conflict_seen = true;        /* fixed address: reported, not changed */
        }
        return;
    }
    if (n->mode != NET_MODE_NONE && n->state != NET_CHECK && n->state != NET_LL_PROBE &&
        (w5500_read8(d, W5500_COMMON, W5500_IR) & W5500_IR_CONFLICT)) {
        w5500_write8(d, W5500_COMMON, W5500_IR, W5500_IR_CONFLICT);
        n->conflict_seen = true;
        if (n->mode == NET_MODE_LINKLOCAL && n->state == NET_LL_BOUND) {
            n->conflicts++;                 /* RFC 3927 2.5: give the address up, pick another */
            clear_addr(n, d);
            ll_pick(n);
            ll_wait(n, now);
            return;
        }
    }

    switch (n->state) {
        case NET_DISCOVER:
            if (dhcp_recv(n, d, &r) && r.type == DHCP_OFFER && r.has_server && usable(r.yiaddr)) {
                memcpy(n->offer, r.yiaddr, 4u);
                memcpy(n->server, r.server, 4u);
                n->tries = 1;
                dhcp_send(n, d, DHCP_REQUEST, BCAST, NULL, n->offer, n->server);
                enter(n, NET_REQUEST, now, T_REQUEST_MS);
            } else if (el >= n->wait) {
                if (n->tries < DHCP_TRIES) {
                    n->tries++;
                    dhcp_send(n, d, DHCP_DISCOVER, BCAST, NULL, NULL, NULL);
                    enter(n, NET_DISCOVER, now, T_DISCOVER2_MS);
                } else {
                    dhcp_failed(n, now);
                }
            }
            break;

        case NET_REQUEST:
            if (dhcp_recv(n, d, &r)) {
                if (r.type == DHCP_ACK && !memcmp(r.yiaddr, n->offer, 4u)) {
                    static const uint8_t mask24[4] = {255, 255, 255, 0};    /* no subnet option */
                    memcpy(n->offer_mask, r.has_mask ? r.mask : mask24, 4u);
                    memcpy(n->offer_gw, r.has_router ? r.router : ZERO, 4u);
                    memcpy(n->offer_dns, r.has_dns ? r.dns : ZERO, 4u);
                    take_lease(n, &r);
                    probe_start(n, d, n->offer, RTR_PROBE_DHCP, RCR_PROBE_DHCP);
                    enter(n, NET_CHECK, now, 0);
                } else if (r.type == DHCP_NAK) {
                    if (n->ll_fallback) dhcp_failed(n, now);
                    else dhcp_begin(n, d, now);
                }
            } else if (el >= n->wait) {
                if (n->tries < DHCP_TRIES) {
                    n->tries++;
                    dhcp_send(n, d, DHCP_REQUEST, BCAST, NULL, n->offer, n->server);
                    enter(n, NET_REQUEST, now, T_REQUEST_MS);
                } else if (n->ll_fallback) {
                    dhcp_failed(n, now);
                } else {
                    dhcp_begin(n, d, now);
                }
            }
            break;

        case NET_CHECK: {
            const int pr = probe_result(n, d, el);
            if (pr < 0) {
                break;
            }
            if (pr > 0) {                   /* someone has it: decline, pause, start over */
                dhcp_send(n, d, DHCP_DECLINE, BCAST, NULL, n->offer, n->server);
                enter(n, NET_DECLINED, now, T_DECLINE_MS);
            } else {
                bind_dhcp(n, d, now);
            }
            break;
        }

        case NET_DECLINED:
            if (el >= n->wait) {
                if (n->ll_fallback) dhcp_failed(n, now);
                else dhcp_begin(n, d, now);
            }
            break;

        case NET_BOUND:
        case NET_RENEW:
        case NET_REBIND: {
            if (n->lease_s == 0xFFFFFFFFu) {
                (void)dhcp_recv(n, d, &r);  /* infinite lease: nothing to renew, keep RX empty */
                break;
            }
            const uint32_t s = (now - n->bound_ms) / 1000u;
            if (s >= n->lease_s) {          /* expired: the address is no longer ours */
                clear_addr(n, d);
                dhcp_begin(n, d, now);
                break;
            }
            if (dhcp_recv(n, d, &r)) {
                if (r.type == DHCP_ACK && !memcmp(r.yiaddr, n->ip, 4u)) {
                    take_lease(n, &r);
                    n->bound_ms = now;
                    enter(n, NET_BOUND, now, 0);
                } else if (r.type == DHCP_NAK) {
                    clear_addr(n, d);
                    dhcp_begin(n, d, now);
                }
                break;
            }
            if (n->state == NET_BOUND && s >= n->t1_s) {
                n->xid = rnd(n) ^ now;
                dhcp_send(n, d, DHCP_REQUEST, n->server, n->ip, NULL, NULL);
                enter(n, NET_RENEW, now, T_RENEW_MS);
            } else if (n->state == NET_RENEW && s >= n->t2_s) {
                n->xid = rnd(n) ^ now;
                dhcp_send(n, d, DHCP_REQUEST, BCAST, n->ip, NULL, NULL);
                enter(n, NET_REBIND, now, T_RENEW_MS);
            } else if (n->state != NET_BOUND && el >= n->wait) {
                dhcp_send(n, d, DHCP_REQUEST, n->state == NET_RENEW ? n->server : BCAST, n->ip, NULL, NULL);
                n->since = now;
            }
            break;
        }

        case NET_LL_WAIT:
            if (el >= n->wait) {
                if (n->ll[0] != 169u) {
                    ll_pick(n);
                }
                probe_start(n, d, n->ll, RTR_PROBE_LL, RCR_PROBE_LL);
                enter(n, NET_LL_PROBE, now, 0);
            }
            break;

        case NET_LL_PROBE: {
            const int pr = probe_result(n, d, el);
            if (pr < 0) {
                break;
            }
            if (pr > 0) {
                if (n->conflicts < 255u) n->conflicts++;
                ll_pick(n);
                ll_wait(n, now);
            } else {
                set_addr(n, d, NET_MODE_LINKLOCAL, n->ll, LL_MASK, ZERO, ZERO);
                enter(n, NET_LL_BOUND, now, LL_DHCP_RETRY_MS);
            }
            break;
        }

        case NET_LL_BOUND:
            (void)dhcp_recv(n, d, &r);      /* stray replies */
            if (n->use_dhcp && !session_busy && el >= n->wait) {
                n->ll_fallback = true;      /* a routable address is preferred (RFC 3927 1.9) */
                dhcp_begin(n, d, now);
            }
            break;

        default:
            break;
    }
}
