#pragma once
/* IP address of the LAN port (stage L3): own DHCP client (RFC 2131/2132) with host name
 * option, RFC 3927 link-local fallback 169.254.x.y with an ARP probe, or a fixed address.
 * HAL-free state machine over the W5500 driver; time comes in as `now` (ms). */
#include "ufi_lan_cfg.h"
#include "ufi_w5500.h"
#include <stdbool.h>
#include <stdint.h>

#define NET_PKT_MAX         600u        /* DHCP message buffer: 576-byte messages + slack */
#define DHCP_CLIENT_PORT    68u
#define DHCP_SERVER_PORT    67u
#define NET_PROBE_PORT      49152u      /* local port of the probe socket */
#define NET_LEASE_MAX_S     (20u * 86400u)  /* longer leases renew as if 20 days (ms timer range) */

/* address source (ufi net "mode") */
enum { NET_MODE_NONE = 0, NET_MODE_DHCP = 1, NET_MODE_LINKLOCAL = 2, NET_MODE_STATIC = 3 };

enum {
    NET_IDLE = 0,           /* stopped (link down / LAN off) */
    NET_DISCOVER,           /* DHCPDISCOVER out, waiting for an offer */
    NET_REQUEST,            /* DHCPREQUEST for the offer, waiting for ACK / NAK */
    NET_CHECK,              /* ACK received: ARP probe of the address before using it */
    NET_DECLINED,           /* address was in use: DHCPDECLINE sent, 10 s pause */
    NET_BOUND,              /* DHCP address in use, renewal timers running */
    NET_RENEW,              /* T1: unicast REQUEST to the server */
    NET_REBIND,             /* T2: broadcast REQUEST */
    NET_LL_WAIT,            /* random wait (or conflict pause) before the next probe */
    NET_LL_PROBE,           /* ARP probe of the link-local candidate */
    NET_LL_BOUND,           /* link-local address in use, DHCP retried every 60 s */
    NET_STATIC,             /* fixed address in use */
};

typedef struct {
    uint8_t state, mode;
    bool use_dhcp;
    uint8_t ip[4], mask[4], gw[4], dns[4];
    uint8_t server[4];              /* DHCP server identifier */
    uint8_t offer[4];               /* offered / acknowledged address, not yet probed */
    uint8_t offer_mask[4], offer_gw[4], offer_dns[4];
    uint32_t offer_lease;
    uint8_t ll[4];                  /* link-local candidate */
    uint8_t mac[6];
    char host[LAN_HOST_MAX + 1];
    uint32_t xid, rng;
    uint32_t since, wait;           /* state timer: entered / last sent at `since`, timeout `wait` ms */
    uint8_t tries;
    uint8_t conflicts;              /* link-local probes that found the address in use */
    bool ll_fallback;               /* DHCP attempt while a link-local address is in use */
    bool conflict_seen;             /* W5500 IR.CONFLICT since the address was set */
    uint32_t bound_ms, lease_s, t1_s, t2_s;
    uint16_t changes;               /* +1 on every address change (NET.TXT, session restart) */
    uint8_t* pkt;                   /* NET_PKT_MAX bytes of scratch, owned by the caller */
} net_t;

void net_start(net_t* n, const w5500_t* d, const lan_cfg_t* cfg, const uint8_t mac[6],
               const char* host, uint8_t* pkt, uint32_t now);
void net_stop(net_t* n, const w5500_t* d);
/* session_busy: a TCP session is connected, so no address change by choice (link-local ->
 * DHCP); expiry and conflicts still change it */
void net_service(net_t* n, const w5500_t* d, uint32_t now, bool session_busy);
bool net_has_address(const net_t* n);
uint32_t net_lease_left(const net_t* n, uint32_t now);   /* s; 0xFFFFFFFF infinite, 0 none */

/* DHCP messages, exposed for the tests and the fuzz target */
enum { DHCP_DISCOVER = 1, DHCP_OFFER, DHCP_REQUEST, DHCP_DECLINE, DHCP_ACK, DHCP_NAK, DHCP_RELEASE };
typedef struct {
    uint8_t type;
    uint8_t yiaddr[4], server[4], mask[4], router[4], dns[4];
    bool has_server, has_mask, has_router, has_dns;
    uint32_t lease_s, t1_s, t2_s;   /* 0 = option absent */
} dhcp_reply_t;
/* ciaddr, req_ip, server may be NULL (zero / option left out) */
uint16_t dhcp_build(uint8_t* out, uint8_t type, uint32_t xid, const uint8_t mac[6], const char* host,
                    const uint8_t ciaddr[4], const uint8_t req_ip[4], const uint8_t server[4]);
bool dhcp_parse(const uint8_t* p, uint16_t n, uint32_t xid, const uint8_t mac[6], dhcp_reply_t* r);
