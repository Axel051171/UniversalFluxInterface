/* Register-level W5500 double for the native LAN tests.
 *
 * It decodes the SPI byte stream exactly like the chip (address, control byte, data phase
 * with auto-increment, chip select ends the frame) and models what the firmware relies on:
 * common and socket registers with their reset values, TX/RX buffer memories mapped modulo
 * the configured socket buffer size, the socket commands (OPEN, LISTEN, DISCON, CLOSE,
 * SEND, RECV) with their status and interrupt bits, and ARP: a UDP SEND to a unicast
 * address finishes with SEND_OK only if a listed host answers ARP for the next hop (the
 * destination on the local subnet, else the gateway), otherwise with TIMEOUT.  What goes
 * out is recorded for the test; what comes in is injected with fake_udp_rx / fake_tcp_*. */
#pragma once
#include "ufi_w5500.h"
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#define FAKE_OUT_MAX    2048u
#define FAKE_PKT_MAX    640u

typedef struct {
    uint8_t sock, ip[4], src_ip[4], mask[4], gw[4];
    uint16_t port, sport, len;
    bool arp_ok;                    /* delivered (SEND_OK) or ARP timed out */
    uint16_t rtr;                   /* retry time and count when it was sent (ARP probe timing) */
    uint8_t rcr;
    uint8_t data[FAKE_PKT_MAX];
} fake_pkt_t;

typedef struct {
    uint8_t common[0x40];
    uint8_t sreg[8][0x30];
    uint8_t tx[8][16384], rx[8][16384];
    uint16_t rx_wr[8];
    /* SPI frame decoder */
    bool cs;
    uint8_t hdr[3];
    unsigned hdr_n;
    uint16_t addr;
    uint8_t block;
    bool write;
    unsigned frames;
    /* behaviour */
    uint8_t version, phy;
    bool reset_stuck, cr_stuck, shar_broken, hold_send_ok;
    uint8_t arp[8][4];
    unsigned arp_n;
    /* recorded output */
    fake_pkt_t out[FAKE_OUT_MAX];
    unsigned out_n;
    uint8_t tcp_out[65536];
    uint32_t tcp_out_n;
    unsigned sends[8];
} w5500_fake_t;

static inline unsigned fake_size(const w5500_fake_t* f, uint8_t s, bool tx)
{
    const unsigned kb = f->sreg[s][tx ? W5500_Sn_TXBUF_SIZE : W5500_Sn_RXBUF_SIZE];
    return (kb == 1 || kb == 2 || kb == 4 || kb == 8 || kb == 16) ? kb * 1024u : 0u;
}

static inline uint16_t fake_get16(const uint8_t* p) { return (uint16_t)((p[0] << 8) | p[1]); }
static inline void fake_put16(uint8_t* p, uint16_t v) { p[0] = (uint8_t)(v >> 8); p[1] = (uint8_t)v; }

static inline void fake_chip_reset(w5500_fake_t* f)
{
    memset(f->common, 0, sizeof(f->common));
    memset(f->sreg, 0, sizeof(f->sreg));
    memset(f->rx_wr, 0, sizeof(f->rx_wr));
    fake_put16(&f->common[W5500_RTR], 2000u);
    f->common[W5500_RCR] = 8u;
    f->common[W5500_VERSIONR] = f->version;
    for (unsigned s = 0; s < 8u; s++) {
        f->sreg[s][W5500_Sn_RXBUF_SIZE] = 2u;
        f->sreg[s][W5500_Sn_TXBUF_SIZE] = 2u;
        f->sreg[s][W5500_Sn_IMR] = 0xFFu;
        f->sreg[s][W5500_Sn_KPALVTR] = 0u;
    }
}

static inline void fake_init(w5500_fake_t* f)
{
    memset(f, 0, sizeof(*f));
    f->version = W5500_VERSION;
    f->phy = 0xB8u | W5500_PHY_LNK | W5500_PHY_SPD | W5500_PHY_DPX;   /* RST=1, all-capable auto */
    fake_chip_reset(f);
}

static inline bool fake_arp_answers(const w5500_fake_t* f, const uint8_t ip[4])
{
    for (unsigned i = 0; i < f->arp_n; i++) {
        if (!memcmp(f->arp[i], ip, 4)) return true;
    }
    return false;
}

static inline void fake_send(w5500_fake_t* f, uint8_t s)
{
    uint8_t* r = f->sreg[s];
    const unsigned size = fake_size(f, s, true);
    const uint16_t rd = fake_get16(&r[W5500_Sn_TX_RD]), wr = fake_get16(&r[W5500_Sn_TX_WR]);
    const uint16_t n = (uint16_t)(wr - rd);
    f->sends[s]++;
    uint8_t bits = W5500_SIR_SEND_OK;
    if (r[W5500_Sn_SR] == W5500_SOCK_UDP) {
        fake_pkt_t* p = (f->out_n < FAKE_OUT_MAX) ? &f->out[f->out_n++] : NULL;
        const uint8_t* dip = &r[W5500_Sn_DIPR];
        bool ok = true;
        if (!(dip[0] == 255 && dip[1] == 255 && dip[2] == 255 && dip[3] == 255)) {
            const uint8_t* sip = &f->common[W5500_SIPR];
            const uint8_t* m = &f->common[W5500_SUBR];
            bool local = true;
            for (unsigned i = 0; i < 4u; i++) local = local && ((dip[i] & m[i]) == (sip[i] & m[i]));
            ok = fake_arp_answers(f, local ? dip : &f->common[W5500_GAR]);
        }
        if (!ok) bits = W5500_SIR_TIMEOUT;
        if (p) {
            p->sock = s;
            memcpy(p->ip, dip, 4);
            memcpy(p->src_ip, &f->common[W5500_SIPR], 4);
            memcpy(p->mask, &f->common[W5500_SUBR], 4);
            memcpy(p->gw, &f->common[W5500_GAR], 4);
            p->port = fake_get16(&r[W5500_Sn_DPORT]);
            p->sport = fake_get16(&r[W5500_Sn_PORT]);
            p->len = n;
            p->arp_ok = ok;
            p->rtr = fake_get16(&f->common[W5500_RTR]);
            p->rcr = f->common[W5500_RCR];
            for (unsigned i = 0; i < n && i < FAKE_PKT_MAX; i++) p->data[i] = f->tx[s][(rd + i) & (size - 1u)];
        }
    } else if (r[W5500_Sn_SR] == W5500_SOCK_ESTABLISHED || r[W5500_Sn_SR] == W5500_SOCK_CLOSE_WAIT) {
        for (unsigned i = 0; i < n && f->tcp_out_n < sizeof(f->tcp_out); i++)
            f->tcp_out[f->tcp_out_n++] = f->tx[s][(rd + i) & (size - 1u)];
    } else {
        bits = W5500_SIR_TIMEOUT;
    }
    fake_put16(&r[W5500_Sn_TX_RD], wr);
    if (!(f->hold_send_ok && bits == W5500_SIR_SEND_OK)) r[W5500_Sn_IR] |= bits;
}

static inline void fake_command(w5500_fake_t* f, uint8_t s, uint8_t cmd)
{
    uint8_t* r = f->sreg[s];
    switch (cmd) {
        case W5500_CMD_OPEN: {
            const uint8_t m = r[W5500_Sn_MR] & 0x0Fu;
            r[W5500_Sn_SR] = m == W5500_MODE_TCP ? W5500_SOCK_INIT : m == W5500_MODE_UDP ? W5500_SOCK_UDP
                           : m == W5500_MODE_MACRAW ? W5500_SOCK_MACRAW : W5500_SOCK_CLOSED;
            fake_put16(&r[W5500_Sn_TX_RD], 0); fake_put16(&r[W5500_Sn_TX_WR], 0);
            fake_put16(&r[W5500_Sn_RX_RD], 0); f->rx_wr[s] = 0;
            break;
        }
        case W5500_CMD_LISTEN:
            if (r[W5500_Sn_SR] == W5500_SOCK_INIT) r[W5500_Sn_SR] = W5500_SOCK_LISTEN;
            break;
        case W5500_CMD_DISCON:
            if (r[W5500_Sn_SR] == W5500_SOCK_ESTABLISHED || r[W5500_Sn_SR] == W5500_SOCK_CLOSE_WAIT) {
                r[W5500_Sn_SR] = W5500_SOCK_CLOSED;
                r[W5500_Sn_IR] |= W5500_SIR_DISCON;
            }
            break;
        case W5500_CMD_CLOSE:
            r[W5500_Sn_SR] = W5500_SOCK_CLOSED;
            break;
        case W5500_CMD_SEND:
            fake_send(f, s);
            break;
        default:
            break;
    }
}

static inline uint8_t fake_reg_read(w5500_fake_t* f, uint8_t block, uint16_t a)
{
    if (block == W5500_COMMON) {
        if (a == W5500_MR) return f->reset_stuck ? W5500_MR_RST : f->common[a];
        if (a == W5500_PHYCFGR) return f->phy;
        if (a == W5500_SIR) {
            uint8_t v = 0;
            for (unsigned s = 0; s < 8u; s++) if (f->sreg[s][W5500_Sn_IR]) v |= (uint8_t)(1u << s);
            return v;
        }
        return a < sizeof(f->common) ? f->common[a] : 0u;
    }
    const uint8_t s = (uint8_t)(block >> 2), kind = block & 3u;
    if (s > 7u) return 0;
    if (kind == 1u) {
        uint8_t* r = f->sreg[s];
        if (a == W5500_Sn_CR) return f->cr_stuck ? 0x01u : 0u;
        if (a == W5500_Sn_TX_FSR || a == W5500_Sn_TX_FSR + 1u) {
            const uint16_t used = (uint16_t)(fake_get16(&r[W5500_Sn_TX_WR]) - fake_get16(&r[W5500_Sn_TX_RD]));
            uint8_t b[2];
            fake_put16(b, (uint16_t)(fake_size(f, s, true) - used));
            return b[a - W5500_Sn_TX_FSR];
        }
        if (a == W5500_Sn_RX_RSR || a == W5500_Sn_RX_RSR + 1u) {
            uint8_t b[2];
            fake_put16(b, (uint16_t)(f->rx_wr[s] - fake_get16(&r[W5500_Sn_RX_RD])));
            return b[a - W5500_Sn_RX_RSR];
        }
        if (a == W5500_Sn_RX_WR || a == W5500_Sn_RX_WR + 1u) {
            uint8_t b[2];
            fake_put16(b, f->rx_wr[s]);
            return b[a - W5500_Sn_RX_WR];
        }
        return a < sizeof(f->sreg[s]) ? r[a] : 0u;
    }
    if (kind == 2u) {
        const unsigned size = fake_size(f, s, true);
        return size ? f->tx[s][a & (size - 1u)] : 0u;
    }
    if (kind == 3u) {
        const unsigned size = fake_size(f, s, false);
        return size ? f->rx[s][a & (size - 1u)] : 0u;
    }
    return 0;
}

static inline void fake_reg_write(w5500_fake_t* f, uint8_t block, uint16_t a, uint8_t v)
{
    if (block == W5500_COMMON) {
        if (a == W5500_MR && (v & W5500_MR_RST)) {
            fake_chip_reset(f);
            return;
        }
        if (a == W5500_IR) { f->common[a] &= (uint8_t)~v; return; }
        if (a == W5500_VERSIONR || a == W5500_PHYCFGR) return;
        if (f->shar_broken && a >= W5500_SHAR && a < W5500_SHAR + 6u) return;
        if (a < sizeof(f->common)) f->common[a] = v;
        return;
    }
    const uint8_t s = (uint8_t)(block >> 2), kind = block & 3u;
    if (s > 7u) return;
    if (kind == 1u) {
        uint8_t* r = f->sreg[s];
        if (a == W5500_Sn_CR) { fake_command(f, s, v); return; }
        if (a == W5500_Sn_IR) { r[a] &= (uint8_t)~v; return; }
        if (a == W5500_Sn_SR || a == W5500_Sn_TX_FSR || a == W5500_Sn_TX_FSR + 1u ||
            a == W5500_Sn_TX_RD || a == W5500_Sn_TX_RD + 1u || a == W5500_Sn_RX_RSR ||
            a == W5500_Sn_RX_RSR + 1u) return;     /* read-only */
        if (a < sizeof(f->sreg[s])) r[a] = v;
        return;
    }
    if (kind == 2u) {
        const unsigned size = fake_size(f, s, true);
        if (size) f->tx[s][a & (size - 1u)] = v;
    }
    /* RX buffer is read-only for the host */
}

/* w5500_t callbacks */
static inline void fake_select(void* ctx, bool on)
{
    w5500_fake_t* f = (w5500_fake_t*)ctx;
    if (on && !f->cs) { f->hdr_n = 0; f->frames++; }
    f->cs = on;
}

static inline void fake_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, uint16_t n)
{
    w5500_fake_t* f = (w5500_fake_t*)ctx;
    for (uint16_t i = 0; i < n; i++) {
        const uint8_t b = tx ? tx[i] : 0u;
        if (!f->cs) { if (rx) rx[i] = 0xFFu; continue; }
        if (f->hdr_n < 3u) {
            f->hdr[f->hdr_n++] = b;
            if (rx) rx[i] = 0;
            if (f->hdr_n == 3u) {
                f->addr = (uint16_t)((f->hdr[0] << 8) | f->hdr[1]);
                f->block = (uint8_t)(f->hdr[2] >> 3);
                f->write = (f->hdr[2] & 0x04u) != 0;
            }
            continue;
        }
        if (f->write) {
            fake_reg_write(f, f->block, f->addr, b);
            if (rx) rx[i] = 0;
        } else if (rx) {
            rx[i] = fake_reg_read(f, f->block, f->addr);
        }
        f->addr++;
    }
}

static inline w5500_t fake_dev(w5500_fake_t* f)
{
    w5500_t d = {f, fake_select, fake_xfer};
    return d;
}

/* ---- injection ---------------------------------------------------------------- */
static inline void fake_rx_bytes(w5500_fake_t* f, uint8_t s, const uint8_t* p, unsigned n)
{
    const unsigned size = fake_size(f, s, false);
    for (unsigned i = 0; i < n; i++) f->rx[s][(f->rx_wr[s] + i) & (size - 1u)] = p[i];
    f->rx_wr[s] = (uint16_t)(f->rx_wr[s] + n);
    f->sreg[s][W5500_Sn_IR] |= W5500_SIR_RECV;
}

static inline void fake_udp_rx(w5500_fake_t* f, uint8_t s, const uint8_t ip[4], uint16_t port,
                        const uint8_t* data, uint16_t n)
{
    const uint16_t used = (uint16_t)(f->rx_wr[s] - fake_get16(&f->sreg[s][W5500_Sn_RX_RD]));
    if (used + 8u + n > fake_size(f, s, false)) return;     /* the chip drops what does not fit */
    uint8_t h[8];
    memcpy(h, ip, 4);
    fake_put16(&h[4], port);
    fake_put16(&h[6], n);
    fake_rx_bytes(f, s, h, 8u);
    fake_rx_bytes(f, s, data, n);
}

static inline bool fake_tcp_accept(w5500_fake_t* f, uint8_t s, const uint8_t ip[4], uint16_t port)
{
    uint8_t* r = f->sreg[s];
    if (r[W5500_Sn_SR] != W5500_SOCK_LISTEN) return false;
    r[W5500_Sn_SR] = W5500_SOCK_ESTABLISHED;
    memcpy(&r[W5500_Sn_DIPR], ip, 4);
    fake_put16(&r[W5500_Sn_DPORT], port);
    r[W5500_Sn_IR] |= W5500_SIR_CON;
    return true;
}

static inline void fake_tcp_rx(w5500_fake_t* f, uint8_t s, const uint8_t* data, unsigned n)
{
    fake_rx_bytes(f, s, data, n);
}

static inline void fake_tcp_peer_close(w5500_fake_t* f, uint8_t s)
{
    f->sreg[s][W5500_Sn_SR] = W5500_SOCK_CLOSE_WAIT;
    f->sreg[s][W5500_Sn_IR] |= W5500_SIR_DISCON;
}

static inline void fake_tcp_lost(w5500_fake_t* f, uint8_t s)
{
    f->sreg[s][W5500_Sn_SR] = W5500_SOCK_CLOSED;
    f->sreg[s][W5500_Sn_IR] |= W5500_SIR_TIMEOUT;
}

static inline void fake_release_send(w5500_fake_t* f, uint8_t s)
{
    f->sreg[s][W5500_Sn_IR] |= W5500_SIR_SEND_OK;
}

static inline void fake_arp_add(w5500_fake_t* f, const uint8_t ip[4])
{
    if (f->arp_n < 8u) memcpy(f->arp[f->arp_n++], ip, 4);
}
