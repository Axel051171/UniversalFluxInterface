/**
 * UFI Flux Engine - LAN status: NET op, NET.TXT, discovery responder (stage L5)
 *
 * Discovery: UDP port 55100 (fixed, independent of the TCP `port`, so a host can ask before
 * it knows anything).  Request: "UFI?" + version 1 + 3 reserved bytes (longer requests are
 * accepted, the rest ignored).  Reply: "UFI!", version, flags, TCP port, address, MAC,
 * firmware version, board revision, host name.  The reply goes back to the asker's address
 * and port; when the asker is not on our subnet (we are link-local and it is not, or the
 * other way round) it goes to 255.255.255.255 instead, because the W5500 could only send a
 * unicast through a gateway that a link-local port does not have.
 *
 * Multi-byte fields little endian like UFI v2, addresses in network order.
 */

#include "ufi_lan_status.h"
#include <string.h>

static void put16(uint8_t* o, uint32_t v) { o[0] = (uint8_t)v; o[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t* o, uint32_t v) { put16(o, v); put16(o + 2, v >> 16); }

uint16_t lan_status_pack(const lan_status_t* s, uint8_t* out)
{
    out[0] = 1;                         /* schema */
    out[1] = s->state;
    out[2] = s->mode;
    out[3] = s->flags;
    memcpy(&out[4], s->ip, 4u);
    memcpy(&out[8], s->mask, 4u);
    memcpy(&out[12], s->gw, 4u);
    memcpy(&out[16], s->dns, 4u);
    memcpy(&out[20], s->mac, 6u);
    put16(&out[26], s->port);
    put16(&out[28], LAN_DISCOVER_PORT);
    put32(&out[30], s->lease_left);
    memcpy(&out[34], s->peer, 4u);
    put16(&out[38], s->peer_port);
    out[40] = s->owner;
    out[41] = s->version;
    size_t n = strlen(s->host);
    n = n > LAN_HOST_MAX ? LAN_HOST_MAX : n;
    out[42] = (uint8_t)n;
    memcpy(&out[43], s->host, n);
    return (uint16_t)(43u + n);
}

static size_t add(char* out, size_t at, const char* key, const char* value)
{
    const size_t k = strlen(key), v = strlen(value);
    memcpy(&out[at], key, k);
    memcpy(&out[at + k], value, v);
    memcpy(&out[at + k + v], "\r\n", 2u);
    return at + k + v + 2u;
}

size_t lan_status_text(const lan_status_t* s, char* out)
{
    static const char* const states[] = {"no-hardware", "no-chip", "off", "link-down", "link-up", "bound"};
    static const char* const modes[] = {"none", "dhcp", "link-local", "static"};
    char t[20];
    size_t n = add(out, 0, "# UFI LAN status, written by the firmware on every change", "");
    n = add(out, n, "state=", s->state < 6u ? states[s->state] : "?");
    n = add(out, n, "mode=", s->mode < 4u ? modes[s->mode] : "?");
    lan_fmt_ip(t, s->ip);
    n = add(out, n, "ip=", t);
    lan_fmt_ip(t, s->mask);
    n = add(out, n, "mask=", t);
    lan_fmt_ip(t, s->gw);
    n = add(out, n, "gw=", t);
    lan_fmt_ip(t, s->dns);
    n = add(out, n, "dns=", t);
    lan_fmt_mac(t, s->mac);
    n = add(out, n, "mac=", t);
    n = add(out, n, "hostname=", s->host);
    unsigned p = s->port, k = 0;
    char num[6];
    do { num[k++] = (char)('0' + p % 10u); p /= 10u; } while (p);
    for (unsigned i = 0; i < k; i++) t[i] = num[k - 1u - i];
    t[k] = '\0';
    n = add(out, n, "port=", t);
    out[n] = '\0';
    return n;
}

bool lan_disc_is_request(const uint8_t* p, uint16_t n)
{
    return n >= LAN_DISC_REQ_SIZE && p[0] == 'U' && p[1] == 'F' && p[2] == 'I' && p[3] == '?' &&
           p[4] == LAN_DISC_VERSION;
}

uint16_t lan_disc_reply(const lan_status_t* s, uint8_t* out)
{
    memcpy(out, "UFI!", 4u);
    out[4] = LAN_DISC_VERSION;
    out[5] = (uint8_t)(((s->flags & LAN_F_WRITE) ? 1u : 0u) | ((s->flags & LAN_F_KEY) ? 2u : 0u) |
                       ((s->flags & LAN_F_CONNECTED) ? 4u : 0u) | (s->mode == 1u ? 8u : 0u) |
                       (s->mode == 2u ? 16u : 0u) | (s->mode == 3u ? 32u : 0u));
    put16(&out[6], s->port);
    memcpy(&out[8], s->ip, 4u);
    memcpy(&out[12], s->mac, 6u);
    out[18] = s->fw_major;
    out[19] = s->fw_minor;
    out[20] = s->board_rev;
    size_t n = strlen(s->host);
    n = n > LAN_HOST_MAX ? LAN_HOST_MAX : n;
    out[21] = (uint8_t)n;
    memcpy(&out[22], s->host, n);
    return (uint16_t)(22u + n);
}

static bool same_subnet(const lan_status_t* s, const uint8_t ip[4])
{
    for (unsigned i = 0; i < 4u; i++) {
        if ((ip[i] & s->mask[i]) != (s->ip[i] & s->mask[i])) {
            return false;
        }
    }
    return true;
}

unsigned lan_disc_service(const w5500_t* d, const lan_status_t* s, uint8_t* buf, bool* inflight)
{
    const uint8_t sock = W5500_SOCK_DISCOVER;
    if (w5500_sock_status(d, sock) != W5500_SOCK_UDP) {
        (void)w5500_sock_open(d, sock, W5500_MODE_UDP, LAN_DISCOVER_PORT);
        *inflight = false;
        return 0;
    }
    if (*inflight) {                    /* one SEND at a time: wait for SEND_OK or the ARP timeout */
        if (!(w5500_sock_ir(d, sock) & (W5500_SIR_SEND_OK | W5500_SIR_TIMEOUT))) {
            return 0;
        }
        w5500_sock_ir_clear(d, sock, W5500_SIR_SEND_OK | W5500_SIR_TIMEOUT);
        *inflight = false;
    }
    w5500_sock_ir_clear(d, sock, W5500_SIR_RECV);
    unsigned sent = 0;
    for (unsigned k = 0; k < 4u && !sent; k++) {
        const uint16_t avail = w5500_rx_size(d, sock);
        if (avail < W5500_UDP_HDR) {
            break;
        }
        uint8_t h[W5500_UDP_HDR];
        w5500_rx_peek(d, sock, 0, h, sizeof(h));
        const uint16_t len = (uint16_t)((h[6] << 8) | h[7]);
        const uint16_t port = (uint16_t)((h[4] << 8) | h[5]);
        if (W5500_UDP_HDR + (uint32_t)len > 2048u || avail < W5500_UDP_HDR + len) {
            (void)w5500_sock_open(d, sock, W5500_MODE_UDP, LAN_DISCOVER_PORT);   /* lost track: restart */
            break;
        }
        const uint16_t take = len < LAN_DISC_REQ_SIZE ? len : (uint16_t)LAN_DISC_REQ_SIZE;
        w5500_rx_peek(d, sock, W5500_UDP_HDR, buf, take);
        w5500_rx_skip(d, sock, (uint16_t)(W5500_UDP_HDR + len));
        if (!lan_disc_is_request(buf, len) || !port) {
            continue;                   /* not ours: dropped */
        }
        static const uint8_t bcast[4] = {255, 255, 255, 255};
        const uint16_t n = lan_disc_reply(s, buf);
        if (w5500_tx_free(d, sock) < n) {
            break;
        }
        w5500_sock_dest(d, sock, same_subnet(s, h) ? h : bcast, port);
        w5500_tx_put(d, sock, buf, n);
        (void)w5500_sock_cmd(d, sock, W5500_CMD_SEND);
        *inflight = true;
        sent++;
    }
    return sent;
}
