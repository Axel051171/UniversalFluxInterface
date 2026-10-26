/**
 * UFI Flux Engine - W5500 register-level driver (board v1.0, U23 on SPI3)
 *
 * Own implementation from the W5500 datasheet: SPI frame = address (16 bit, MSB first),
 * control byte BSB[4:0] << 3 | RWB << 2 | OM (00, variable length: chip select frames
 * the data), then the data phase with auto-incremented address.  TX/RX buffer blocks are
 * addressed with the 16-bit socket pointers directly; the chip maps them modulo the
 * socket buffer size, so a transfer across the buffer end needs no split.
 *
 * Pointer registers that the chip updates on its own (TX_FSR, RX_RSR) are read until two
 * reads agree, as the datasheet asks for 16-bit values that may change between bytes.
 */

#include "ufi_w5500.h"
#include "ufi_sha256.h"
#include <string.h>

#define POLL_LIMIT      2000u           /* MR.RST / Sn_CR clear within a few reads on the chip */

static void frame(const w5500_t* d, uint8_t block, uint16_t addr, bool write,
                  const uint8_t* tx, uint8_t* rx, uint16_t n)
{
    const uint8_t hdr[3] = {(uint8_t)(addr >> 8), (uint8_t)addr,
                            (uint8_t)((block << 3) | (write ? 0x04u : 0x00u))};
    d->select(d->ctx, true);
    d->xfer(d->ctx, hdr, NULL, 3u);
    if (n) {
        d->xfer(d->ctx, tx, rx, n);
    }
    d->select(d->ctx, false);
}

void w5500_read(const w5500_t* d, uint8_t block, uint16_t addr, uint8_t* buf, uint16_t n)
{
    frame(d, block, addr, false, NULL, buf, n);
}

void w5500_write(const w5500_t* d, uint8_t block, uint16_t addr, const uint8_t* buf, uint16_t n)
{
    frame(d, block, addr, true, buf, NULL, n);
}

uint8_t w5500_read8(const w5500_t* d, uint8_t block, uint16_t addr)
{
    uint8_t v = 0;
    w5500_read(d, block, addr, &v, 1u);
    return v;
}

void w5500_write8(const w5500_t* d, uint8_t block, uint16_t addr, uint8_t v)
{
    w5500_write(d, block, addr, &v, 1u);
}

uint16_t w5500_read16(const w5500_t* d, uint8_t block, uint16_t addr)
{
    uint8_t b[2] = {0, 0};
    w5500_read(d, block, addr, b, 2u);
    return (uint16_t)((b[0] << 8) | b[1]);
}

void w5500_write16(const w5500_t* d, uint8_t block, uint16_t addr, uint16_t v)
{
    const uint8_t b[2] = {(uint8_t)(v >> 8), (uint8_t)v};
    w5500_write(d, block, addr, b, 2u);
}

/* 16-bit value the chip may update between the two byte reads */
static uint16_t read16_stable(const w5500_t* d, uint8_t block, uint16_t addr)
{
    uint16_t a = w5500_read16(d, block, addr);
    for (unsigned i = 0; i < 8u; i++) {
        const uint16_t b = w5500_read16(d, block, addr);
        if (a == b) {
            break;
        }
        a = b;
    }
    return a;
}

bool w5500_reset(const w5500_t* d)
{
    w5500_write8(d, W5500_COMMON, W5500_MR, W5500_MR_RST);
    for (unsigned i = 0; i < POLL_LIMIT; i++) {
        if (!(w5500_read8(d, W5500_COMMON, W5500_MR) & W5500_MR_RST)) {
            return w5500_version(d) == W5500_VERSION;
        }
    }
    return false;
}

uint8_t w5500_version(const w5500_t* d)
{
    return w5500_read8(d, W5500_COMMON, W5500_VERSIONR);
}

bool w5500_setup(const w5500_t* d, const uint8_t mac[6])
{
    /* Buffer split (KB): session 8/8, DHCP 2/2, discovery 2/2, probe 1/1, the rest unused;
     * the chip refuses nothing here, but the sum must stay within 16 KB per direction. */
    static const uint8_t kb[8] = {8, 2, 2, 1, 0, 0, 0, 0};
    for (uint8_t s = 0; s < 8u; s++) {
        w5500_write8(d, W5500_SREG(s), W5500_Sn_RXBUF_SIZE, kb[s]);
        w5500_write8(d, W5500_SREG(s), W5500_Sn_TXBUF_SIZE, kb[s]);
        /* every socket event of the used sockets on INTn: the glue polls when it goes low */
        w5500_write8(d, W5500_SREG(s), W5500_Sn_IMR, kb[s] ? 0x1Fu : 0x00u);
    }
    w5500_write(d, W5500_COMMON, W5500_SHAR, mac, 6u);
    uint8_t back[6];
    w5500_read(d, W5500_COMMON, W5500_SHAR, back, 6u);
    w5500_write8(d, W5500_COMMON, W5500_IMR, W5500_IR_CONFLICT);
    w5500_write8(d, W5500_COMMON, W5500_SIMR, 0x0Fu);
    w5500_retry(d, 2000u, 8u);
    return memcmp(back, mac, 6u) == 0;
}

uint8_t w5500_phy(const w5500_t* d)
{
    return w5500_read8(d, W5500_COMMON, W5500_PHYCFGR);
}

void w5500_set_ip(const w5500_t* d, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4])
{
    w5500_write(d, W5500_COMMON, W5500_GAR, gw, 4u);
    w5500_write(d, W5500_COMMON, W5500_SUBR, mask, 4u);
    w5500_write(d, W5500_COMMON, W5500_SIPR, ip, 4u);
}

void w5500_retry(const w5500_t* d, uint16_t rtr_100us, uint8_t rcr)
{
    w5500_write16(d, W5500_COMMON, W5500_RTR, rtr_100us);
    w5500_write8(d, W5500_COMMON, W5500_RCR, rcr);
}

bool w5500_sock_cmd(const w5500_t* d, uint8_t s, uint8_t cmd)
{
    w5500_write8(d, W5500_SREG(s), W5500_Sn_CR, cmd);
    for (unsigned i = 0; i < POLL_LIMIT; i++) {
        if (!w5500_read8(d, W5500_SREG(s), W5500_Sn_CR)) {
            return true;
        }
    }
    return false;
}

bool w5500_sock_open(const w5500_t* d, uint8_t s, uint8_t mode, uint16_t port)
{
    w5500_sock_close(d, s);
    w5500_write8(d, W5500_SREG(s), W5500_Sn_MR, mode);
    w5500_write16(d, W5500_SREG(s), W5500_Sn_PORT, port);
    w5500_sock_ir_clear(d, s, 0x1Fu);
    if (!w5500_sock_cmd(d, s, W5500_CMD_OPEN)) {
        return false;
    }
    const uint8_t want = (mode & 0x0Fu) == W5500_MODE_TCP ? W5500_SOCK_INIT
                       : (mode & 0x0Fu) == W5500_MODE_UDP ? W5500_SOCK_UDP : W5500_SOCK_MACRAW;
    for (unsigned i = 0; i < POLL_LIMIT; i++) {
        if (w5500_sock_status(d, s) == want) {
            return true;
        }
    }
    return false;
}

void w5500_sock_close(const w5500_t* d, uint8_t s)
{
    (void)w5500_sock_cmd(d, s, W5500_CMD_CLOSE);
    w5500_sock_ir_clear(d, s, 0x1Fu);
}

uint8_t w5500_sock_status(const w5500_t* d, uint8_t s)
{
    return w5500_read8(d, W5500_SREG(s), W5500_Sn_SR);
}

uint8_t w5500_sock_ir(const w5500_t* d, uint8_t s)
{
    return w5500_read8(d, W5500_SREG(s), W5500_Sn_IR);
}

void w5500_sock_ir_clear(const w5500_t* d, uint8_t s, uint8_t bits)
{
    w5500_write8(d, W5500_SREG(s), W5500_Sn_IR, bits);     /* write 1 to clear */
}

void w5500_sock_dest(const w5500_t* d, uint8_t s, const uint8_t ip[4], uint16_t port)
{
    w5500_write(d, W5500_SREG(s), W5500_Sn_DIPR, ip, 4u);
    w5500_write16(d, W5500_SREG(s), W5500_Sn_DPORT, port);
}

uint16_t w5500_tx_free(const w5500_t* d, uint8_t s)
{
    return read16_stable(d, W5500_SREG(s), W5500_Sn_TX_FSR);
}

uint16_t w5500_rx_size(const w5500_t* d, uint8_t s)
{
    return read16_stable(d, W5500_SREG(s), W5500_Sn_RX_RSR);
}

void w5500_tx_put(const w5500_t* d, uint8_t s, const uint8_t* data, uint16_t n)
{
    const uint16_t wr = w5500_read16(d, W5500_SREG(s), W5500_Sn_TX_WR);
    w5500_write(d, W5500_STX(s), wr, data, n);
    w5500_write16(d, W5500_SREG(s), W5500_Sn_TX_WR, (uint16_t)(wr + n));
}

void w5500_rx_peek(const w5500_t* d, uint8_t s, uint16_t offset, uint8_t* buf, uint16_t n)
{
    const uint16_t rd = w5500_read16(d, W5500_SREG(s), W5500_Sn_RX_RD);
    w5500_read(d, W5500_SRX(s), (uint16_t)(rd + offset), buf, n);
}

void w5500_rx_skip(const w5500_t* d, uint8_t s, uint16_t n)
{
    const uint16_t rd = w5500_read16(d, W5500_SREG(s), W5500_Sn_RX_RD);
    w5500_write16(d, W5500_SREG(s), W5500_Sn_RX_RD, (uint16_t)(rd + n));
    (void)w5500_sock_cmd(d, s, W5500_CMD_RECV);
}

void w5500_mac_from_id(const uint8_t* id, size_t n, uint8_t mac[6])
{
    uint8_t h[32];
    sha256(id, n, h);
    mac[0] = 0x02u;                     /* locally administered, unicast */
    memcpy(&mac[1], h, 5u);
}
