/* W5500 driver (ufi_w5500.c) against the register-level fake (w5500_fake.h): reset and
 * VERSIONR detection, MAC write with read-back, buffer split, PHY link, socket commands,
 * buffer pointers with wrap-around at the socket buffer end, MAC from the unique ID. */
#include "check.h"
#include "w5500_fake.h"

static w5500_fake_t f;

int main(void)
{
    fake_init(&f);
    const w5500_t d = fake_dev(&f);

    /* reset and identity */
    f.common[W5500_SIPR] = 9;                                   /* something to wipe */
    CHECK(w5500_reset(&d), "reset with VERSIONR 0x04");
    CHECK(w5500_version(&d) == 0x04 && !f.common[W5500_SIPR], "version %u", w5500_version(&d));
    CHECK(w5500_read16(&d, W5500_COMMON, W5500_RTR) == 2000u && w5500_read8(&d, W5500_COMMON, W5500_RCR) == 8u,
          "reset values RTR/RCR");
    f.version = 0x00;                                           /* other chip / MISO stuck low */
    CHECK(!w5500_reset(&d), "VERSIONR 0x00 is not a W5500");
    f.version = 0xFF;
    CHECK(!w5500_reset(&d), "VERSIONR 0xFF (no chip, MISO high) is not a W5500");
    f.version = W5500_VERSION;
    f.reset_stuck = true;
    CHECK(!w5500_reset(&d), "MR.RST never clears");
    f.reset_stuck = false;
    CHECK(w5500_reset(&d), "reset again");

    /* every access is one chip-select frame */
    const unsigned frames = f.frames;
    (void)w5500_read8(&d, W5500_COMMON, W5500_VERSIONR);
    CHECK(f.frames == frames + 1u && !f.cs, "one frame per access, CS released");

    /* setup: MAC must be written (SHAR resets to 00) and read back */
    CHECK(!f.common[W5500_SHAR] && !f.common[W5500_SHAR + 5], "SHAR resets to zero");
    const uint8_t mac[6] = {0x02, 0x11, 0x22, 0x33, 0x44, 0x55};
    CHECK(w5500_setup(&d, mac), "setup");
    CHECK(!memcmp(&f.common[W5500_SHAR], mac, 6), "SHAR written");
    CHECK(f.sreg[0][W5500_Sn_TXBUF_SIZE] == 8 && f.sreg[0][W5500_Sn_RXBUF_SIZE] == 8 &&
          f.sreg[1][W5500_Sn_TXBUF_SIZE] == 2 && f.sreg[2][W5500_Sn_RXBUF_SIZE] == 2 &&
          f.sreg[3][W5500_Sn_TXBUF_SIZE] == 1 && !f.sreg[4][W5500_Sn_TXBUF_SIZE] && !f.sreg[7][W5500_Sn_RXBUF_SIZE],
          "buffer split 8/2/2/1");
    unsigned sum = 0;
    for (unsigned s = 0; s < 8; s++) sum += f.sreg[s][W5500_Sn_TXBUF_SIZE];
    CHECK(sum <= 16, "TX split within 16 KB (%u)", sum);
    CHECK(f.common[W5500_SIMR] == 0x0F && f.common[W5500_IMR] == W5500_IR_CONFLICT &&
          f.sreg[0][W5500_Sn_IMR] == 0x1F && !f.sreg[5][W5500_Sn_IMR], "interrupt masks");
    f.shar_broken = true;
    CHECK(w5500_reset(&d), "reset clears SHAR");
    CHECK(!w5500_setup(&d, mac), "SHAR read-back mismatch is reported");
    f.shar_broken = false;

    /* PHY */
    f.phy = 0xB8u | W5500_PHY_LNK | W5500_PHY_SPD;
    CHECK((w5500_phy(&d) & 7u) == 3u, "link up, 100 Mbit, half duplex");
    f.phy = 0xB8u;
    CHECK(!(w5500_phy(&d) & W5500_PHY_LNK), "link down");

    /* addresses */
    const uint8_t ip[4] = {192, 168, 1, 50}, mask[4] = {255, 255, 255, 0}, gw[4] = {192, 168, 1, 1};
    w5500_set_ip(&d, ip, mask, gw);
    CHECK(!memcmp(&f.common[W5500_SIPR], ip, 4) && !memcmp(&f.common[W5500_SUBR], mask, 4) &&
          !memcmp(&f.common[W5500_GAR], gw, 4), "SIPR/SUBR/GAR");
    w5500_retry(&d, 15000u, 2u);
    CHECK(f.common[W5500_RTR] == 0x3A && f.common[W5500_RTR + 1] == 0x98 && f.common[W5500_RCR] == 2,
          "RTR big endian, RCR");

    /* sockets */
    CHECK(w5500_sock_open(&d, 1, W5500_MODE_UDP, 68) && w5500_sock_status(&d, 1) == W5500_SOCK_UDP, "UDP open");
    CHECK(f.sreg[1][W5500_Sn_PORT] == 0 && f.sreg[1][W5500_Sn_PORT + 1] == 68, "port big endian");
    CHECK(w5500_sock_open(&d, 0, W5500_MODE_TCP, 55100) && w5500_sock_status(&d, 0) == W5500_SOCK_INIT, "TCP open");
    CHECK(w5500_sock_cmd(&d, 0, W5500_CMD_LISTEN) && w5500_sock_status(&d, 0) == W5500_SOCK_LISTEN, "listen");
    CHECK(w5500_tx_free(&d, 0) == 8192u && w5500_rx_size(&d, 0) == 0u, "empty session buffers");
    f.cr_stuck = true;
    CHECK(!w5500_sock_cmd(&d, 2, W5500_CMD_OPEN), "Sn_CR stuck is reported");
    f.cr_stuck = false;

    /* TX pointer wrap: fill to 2 bytes before the end, then 10 bytes across it */
    const uint8_t peer[4] = {192, 168, 1, 20};
    CHECK(fake_tcp_accept(&f, 0, peer, 50000) && w5500_sock_status(&d, 0) == W5500_SOCK_ESTABLISHED, "accepted");
    static uint8_t big[8190];
    for (unsigned i = 0; i < sizeof(big); i++) big[i] = (uint8_t)(i * 7u);
    w5500_tx_put(&d, 0, big, sizeof(big));
    CHECK(w5500_tx_free(&d, 0) == 2u, "free after 8190: %u", w5500_tx_free(&d, 0));
    CHECK(w5500_sock_cmd(&d, 0, W5500_CMD_SEND) && (w5500_sock_ir(&d, 0) & W5500_SIR_SEND_OK), "send 1");
    w5500_sock_ir_clear(&d, 0, W5500_SIR_SEND_OK);
    CHECK(!(w5500_sock_ir(&d, 0) & W5500_SIR_SEND_OK), "IR cleared by writing 1");
    const uint8_t tail[10] = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    w5500_tx_put(&d, 0, tail, sizeof(tail));
    CHECK(w5500_sock_cmd(&d, 0, W5500_CMD_SEND), "send 2");
    CHECK(f.tcp_out_n == 8200u && !memcmp(f.tcp_out, big, sizeof(big)) && !memcmp(&f.tcp_out[8190], tail, 10),
          "stream across the buffer end (%u)", (unsigned)f.tcp_out_n);

    /* RX peek / skip with wrap */
    static uint8_t in[8180];
    for (unsigned i = 0; i < sizeof(in); i++) in[i] = (uint8_t)(i ^ 0x5Au);
    fake_tcp_rx(&f, 0, in, sizeof(in));
    CHECK(w5500_rx_size(&d, 0) == sizeof(in), "rx size");
    uint8_t got[64];
    w5500_rx_peek(&d, 0, 100, got, 8);
    CHECK(!memcmp(got, &in[100], 8) && w5500_rx_size(&d, 0) == sizeof(in), "peek does not consume");
    w5500_rx_skip(&d, 0, sizeof(in));
    CHECK(w5500_rx_size(&d, 0) == 0, "skip consumes");
    uint8_t more[40];
    for (unsigned i = 0; i < sizeof(more); i++) more[i] = (uint8_t)(200u + i);
    fake_tcp_rx(&f, 0, more, sizeof(more));                     /* starts 12 bytes before the end */
    w5500_rx_peek(&d, 0, 0, got, sizeof(more));
    CHECK(!memcmp(got, more, sizeof(more)), "peek across the RX buffer end");

    /* UDP to a host that does not answer ARP: TIMEOUT */
    const uint8_t nobody[4] = {192, 168, 1, 99};
    w5500_sock_dest(&d, 1, nobody, 9);
    w5500_tx_put(&d, 1, tail, 1);
    (void)w5500_sock_cmd(&d, 1, W5500_CMD_SEND);
    CHECK(w5500_sock_ir(&d, 1) & W5500_SIR_TIMEOUT, "ARP timeout");
    w5500_sock_close(&d, 1);
    CHECK(w5500_sock_status(&d, 1) == W5500_SOCK_CLOSED && !w5500_sock_ir(&d, 1), "closed, IR cleared");

    /* MAC from the 96-bit unique ID: 02 + first 5 bytes of SHA-256 */
    uint8_t uid[12], m[6];
    for (unsigned i = 0; i < 12; i++) uid[i] = (uint8_t)(i + 1);
    w5500_mac_from_id(uid, sizeof(uid), m);
    const uint8_t want[6] = {0x02, 0x20, 0x64, 0x02, 0xca, 0xb3};
    CHECK(!memcmp(m, want, 6), "MAC %02x:%02x:%02x:%02x:%02x:%02x", m[0], m[1], m[2], m[3], m[4], m[5]);
    CHECK((m[0] & 3u) == 2u, "locally administered unicast");
    uid[11] ^= 1u;
    uint8_t m2[6];
    w5500_mac_from_id(uid, sizeof(uid), m2);
    CHECK(memcmp(m, m2, 6) != 0 && m2[0] == 2, "every UID bit counts");
    CHECK_SUMMARY("w5500");
}
