#pragma once
/* WIZnet W5500 hardwired TCP/IP controller (U23, board v1.0), register-level driver.
 *
 * Written from the W5500 datasheet register map (v1.0.x): every access is one SPI frame
 * of 16-bit address, control byte (block select BSB[4:0], RWB, OM = 00 variable length)
 * and the data phase, chip select low around it.  Multi-byte registers are big endian.
 * No HAL here: the board glue (ufi_lan.c) supplies chip select and byte transfer, the
 * native tests a register-level fake (tests/w5500_fake.h). */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    void* ctx;
    void (*select)(void* ctx, bool on);                                 /* SCSn low = true */
    void (*xfer)(void* ctx, const uint8_t* tx, uint8_t* rx, uint16_t n); /* tx NULL = zeros, rx NULL = discard */
} w5500_t;

/* block select */
#define W5500_COMMON            0x00u
#define W5500_SREG(s)           ((uint8_t)((s) * 4u + 1u))
#define W5500_STX(s)            ((uint8_t)((s) * 4u + 2u))
#define W5500_SRX(s)            ((uint8_t)((s) * 4u + 3u))

/* common registers */
#define W5500_MR                0x0000u
#define W5500_GAR               0x0001u     /* 4 */
#define W5500_SUBR              0x0005u     /* 4 */
#define W5500_SHAR              0x0009u     /* 6, resets to 00 */
#define W5500_SIPR              0x000Fu     /* 4 */
#define W5500_IR                0x0015u
#define W5500_IMR               0x0016u
#define W5500_SIR               0x0017u
#define W5500_SIMR              0x0018u
#define W5500_RTR               0x0019u     /* 2, 100 us units, reset 2000 = 200 ms */
#define W5500_RCR               0x001Bu     /* reset 8 */
#define W5500_PHYCFGR           0x002Eu
#define W5500_VERSIONR          0x0039u
#define W5500_MR_RST            0x80u
#define W5500_IR_CONFLICT       0x80u       /* ARP request with our source IP seen */
#define W5500_PHY_LNK           0x01u
#define W5500_PHY_SPD           0x02u       /* 1 = 100 Mbit/s */
#define W5500_PHY_DPX           0x04u       /* 1 = full duplex */
#define W5500_VERSION           0x04u

/* socket registers */
#define W5500_Sn_MR             0x0000u
#define W5500_Sn_CR             0x0001u
#define W5500_Sn_IR             0x0002u
#define W5500_Sn_SR             0x0003u
#define W5500_Sn_PORT           0x0004u     /* 2 */
#define W5500_Sn_DIPR           0x000Cu     /* 4 */
#define W5500_Sn_DPORT          0x0010u     /* 2 */
#define W5500_Sn_RXBUF_SIZE     0x001Eu     /* KB: 0, 1, 2, 4, 8, 16 */
#define W5500_Sn_TXBUF_SIZE     0x001Fu
#define W5500_Sn_TX_FSR         0x0020u     /* 2 */
#define W5500_Sn_TX_RD          0x0022u
#define W5500_Sn_TX_WR          0x0024u
#define W5500_Sn_RX_RSR         0x0026u
#define W5500_Sn_RX_RD          0x0028u
#define W5500_Sn_RX_WR          0x002Au
#define W5500_Sn_IMR            0x002Cu
#define W5500_Sn_KPALVTR        0x002Fu     /* TCP keep-alive, 5 s units */

#define W5500_MODE_TCP          0x01u
#define W5500_MODE_UDP          0x02u
#define W5500_MODE_MACRAW       0x04u
#define W5500_MODE_BCASTB       0x40u       /* UDP: block broadcast */

#define W5500_CMD_OPEN          0x01u
#define W5500_CMD_LISTEN        0x02u
#define W5500_CMD_CONNECT       0x04u
#define W5500_CMD_DISCON        0x08u
#define W5500_CMD_CLOSE         0x10u
#define W5500_CMD_SEND          0x20u
#define W5500_CMD_RECV          0x40u

#define W5500_SIR_CON           0x01u
#define W5500_SIR_DISCON        0x02u
#define W5500_SIR_RECV          0x04u
#define W5500_SIR_TIMEOUT       0x08u
#define W5500_SIR_SEND_OK       0x10u

#define W5500_SOCK_CLOSED       0x00u
#define W5500_SOCK_INIT         0x13u
#define W5500_SOCK_LISTEN       0x14u
#define W5500_SOCK_SYNSENT      0x15u
#define W5500_SOCK_SYNRECV      0x16u
#define W5500_SOCK_ESTABLISHED  0x17u
#define W5500_SOCK_FIN_WAIT     0x18u
#define W5500_SOCK_CLOSING      0x1Au
#define W5500_SOCK_TIME_WAIT    0x1Bu
#define W5500_SOCK_CLOSE_WAIT   0x1Cu
#define W5500_SOCK_LAST_ACK     0x1Du
#define W5500_SOCK_UDP          0x22u
#define W5500_SOCK_MACRAW       0x42u

#define W5500_UDP_HDR           8u          /* RX: peer IP, peer port, length */

/* Socket use of the UFI firmware and the buffer split (16 KB TX + 16 KB RX in the chip). */
#define W5500_SOCK_SESSION      0u          /* TCP UFI v2 session */
#define W5500_SOCK_DHCP         1u          /* UDP 68 */
#define W5500_SOCK_DISCOVER     2u          /* UDP discovery responder */
#define W5500_SOCK_PROBE        3u          /* UDP ARP probe (link-local, DHCP address check) */
#define W5500_SESSION_BUF       8192u

void w5500_read(const w5500_t* d, uint8_t block, uint16_t addr, uint8_t* buf, uint16_t n);
void w5500_write(const w5500_t* d, uint8_t block, uint16_t addr, const uint8_t* buf, uint16_t n);
uint8_t w5500_read8(const w5500_t* d, uint8_t block, uint16_t addr);
void w5500_write8(const w5500_t* d, uint8_t block, uint16_t addr, uint8_t v);
uint16_t w5500_read16(const w5500_t* d, uint8_t block, uint16_t addr);
void w5500_write16(const w5500_t* d, uint8_t block, uint16_t addr, uint16_t v);

/* Soft reset (MR.RST, self clearing) and identity: true when VERSIONR reads 0x04. */
bool w5500_reset(const w5500_t* d);
uint8_t w5500_version(const w5500_t* d);
/* Chip set-up after reset: MAC (written and read back; false on a mismatch), the socket
 * buffer split above, interrupts for the four used sockets, chip default retry timing. */
bool w5500_setup(const w5500_t* d, const uint8_t mac[6]);
uint8_t w5500_phy(const w5500_t* d);                        /* PHYCFGR */
void w5500_set_ip(const w5500_t* d, const uint8_t ip[4], const uint8_t mask[4], const uint8_t gw[4]);
void w5500_retry(const w5500_t* d, uint16_t rtr_100us, uint8_t rcr);

/* sockets */
bool w5500_sock_cmd(const w5500_t* d, uint8_t s, uint8_t cmd);          /* false: Sn_CR never cleared */
bool w5500_sock_open(const w5500_t* d, uint8_t s, uint8_t mode, uint16_t port);
void w5500_sock_close(const w5500_t* d, uint8_t s);
uint8_t w5500_sock_status(const w5500_t* d, uint8_t s);
uint8_t w5500_sock_ir(const w5500_t* d, uint8_t s);
void w5500_sock_ir_clear(const w5500_t* d, uint8_t s, uint8_t bits);
void w5500_sock_dest(const w5500_t* d, uint8_t s, const uint8_t ip[4], uint16_t port);
uint16_t w5500_tx_free(const w5500_t* d, uint8_t s);
uint16_t w5500_rx_size(const w5500_t* d, uint8_t s);
/* Copy into the TX buffer at Sn_TX_WR and advance it; SEND is separate. */
void w5500_tx_put(const w5500_t* d, uint8_t s, const uint8_t* data, uint16_t n);
/* RX buffer from Sn_RX_RD + offset without consuming; skip consumes n bytes (RX_RD + RECV). */
void w5500_rx_peek(const w5500_t* d, uint8_t s, uint16_t offset, uint8_t* buf, uint16_t n);
void w5500_rx_skip(const w5500_t* d, uint8_t s, uint16_t n);

/* Locally administered unicast MAC 02:xx:xx:xx:xx:xx from SHA-256 over the STM32 96-bit
 * unique ID (or any id bytes): stable per board, distinct between boards. */
void w5500_mac_from_id(const uint8_t* id, size_t n, uint8_t mac[6]);
