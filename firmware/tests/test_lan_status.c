/* LAN status (ufi_lan_status.c): NET op layout, NET.TXT text, discovery request/reply and
 * the responder on socket 2 against the W5500 fake (unicast / broadcast reply, one SEND at
 * a time, foreign datagrams dropped). */
#include "check.h"
#include "w5500_fake.h"
#include "ufi_lan_status.h"
#include <string.h>

static w5500_fake_t f;

static lan_status_t sample(void)
{
    lan_status_t s;
    memset(&s, 0, sizeof(s));
    s.state = 5; s.mode = 1; s.flags = 0x01 | 0x02 | 0x04 | 0x08 | 0x40; s.owner = 2; s.version = 4;
    const uint8_t ip[4] = {192, 168, 1, 50}, mask[4] = {255, 255, 255, 0}, gw[4] = {192, 168, 1, 1},
                  dns[4] = {9, 9, 9, 9}, mac[6] = {2, 0x20, 0x64, 2, 0xca, 0xb3}, peer[4] = {192, 168, 1, 20};
    memcpy(s.ip, ip, 4); memcpy(s.mask, mask, 4); memcpy(s.gw, gw, 4); memcpy(s.dns, dns, 4);
    memcpy(s.mac, mac, 6); memcpy(s.peer, peer, 4);
    s.port = 55100; s.peer_port = 50123; s.lease_left = 3600;
    s.fw_major = 1; s.fw_minor = 18; s.board_rev = 10;
    strcpy(s.host, "ufi-02cab3");
    return s;
}

int main(void)
{
    lan_status_t s = sample();
    uint8_t o[LAN_STATUS_MAX];
    const uint16_t n = lan_status_pack(&s, o);
    CHECK(n == 53 && o[0] == 1 && o[1] == 5 && o[2] == 1 && o[3] == 0x4F && o[4] == 192 && o[7] == 50 &&
          o[8] == 255 && o[11] == 0 && o[12] == 192 && o[15] == 1 && o[16] == 9 && o[20] == 2 && o[25] == 0xb3,
          "NET head (%u)", n);
    CHECK(o[26] == 0x3C && o[27] == 0xD7 && o[28] == 0x3C && o[29] == 0xD7 && o[30] == 0x10 && o[31] == 0x0E &&
          !o[32] && o[34] == 192 && o[37] == 20 && o[38] == 0xCB && o[39] == 0xC3 && o[40] == 2 && o[41] == 4 &&
          o[42] == 10 && !memcmp(&o[43], "ufi-02cab3", 10), "NET tail");

    char t[LAN_TEXT_MAX];
    const size_t tn = lan_status_text(&s, t);
    CHECK(tn == strlen(t) && tn < LAN_TEXT_MAX && strstr(t, "\r\nstate=bound\r\n") && strstr(t, "\r\nmode=dhcp\r\n") &&
          strstr(t, "\r\nip=192.168.1.50\r\n") && strstr(t, "\r\nmac=02:20:64:02:ca:b3\r\n") &&
          strstr(t, "\r\nhostname=ufi-02cab3\r\n") && strstr(t, "\r\nport=55100\r\n") && t[0] == '#', "NET.TXT:\n%s", t);
    lan_status_t big = s;
    memset(big.host, 'h', LAN_HOST_MAX); big.host[LAN_HOST_MAX] = 0;
    big.state = 9; big.mode = 9;
    CHECK(lan_status_text(&big, t) < LAN_TEXT_MAX && strstr(t, "state=?"), "worst case fits");

    /* discovery packets */
    const uint8_t req[8] = {'U', 'F', 'I', '?', 1, 0, 0, 0};
    CHECK(lan_disc_is_request(req, 8) && !lan_disc_is_request(req, 7), "request length");
    uint8_t bad[8]; memcpy(bad, req, 8); bad[4] = 2;
    CHECK(!lan_disc_is_request(bad, 8), "other version");
    bad[4] = 1; bad[3] = '!';
    CHECK(!lan_disc_is_request(bad, 8), "a reply is no request");
    uint8_t r[LAN_DISC_REPLY_MAX];
    const uint16_t rn = lan_disc_reply(&s, r);
    CHECK(rn == 32 && !memcmp(r, "UFI!", 4) && r[4] == 1 && r[5] == (2 | 4 | 8) && r[6] == 0x3C && r[7] == 0xD7 &&
          r[8] == 192 && r[11] == 50 && r[12] == 2 && r[17] == 0xb3 && r[18] == 1 && r[19] == 18 && r[20] == 10 &&
          r[21] == 10 && !memcmp(&r[22], "ufi-02cab3", 10), "reply layout (%u)", rn);

    /* responder */
    fake_init(&f);
    w5500_t d = fake_dev(&f);
    (void)w5500_reset(&d);
    (void)w5500_setup(&d, s.mac);
    w5500_set_ip(&d, s.ip, s.mask, s.gw);
    uint8_t buf[LAN_DISC_REPLY_MAX];
    bool inflight = false;
    CHECK(!lan_disc_service(&d, &s, buf, &inflight) && f.sreg[2][W5500_Sn_SR] == W5500_SOCK_UDP &&
          f.sreg[2][W5500_Sn_PORT] == 0xD7 && f.sreg[2][W5500_Sn_PORT + 1] == 0x3C, "UDP 55100 open");
    const uint8_t host[4] = {192, 168, 1, 20}, far[4] = {10, 0, 0, 5};
    fake_arp_add(&f, host);
    fake_udp_rx(&f, 2, host, 40000, req, 8);
    CHECK(lan_disc_service(&d, &s, buf, &inflight) == 1 && f.out_n == 1 && f.out[0].sock == 2 &&
          !memcmp(f.out[0].ip, host, 4) && f.out[0].port == 40000 && f.out[0].len == 32 &&
          !memcmp(f.out[0].data, "UFI!", 4) && f.out[0].arp_ok, "unicast reply");
    fake_udp_rx(&f, 2, far, 40001, req, 8);
    CHECK(lan_disc_service(&d, &s, buf, &inflight) == 1 && f.out_n == 2 && f.out[1].ip[0] == 255 &&
          f.out[1].ip[3] == 255 && f.out[1].port == 40001, "other subnet: broadcast");
    f.hold_send_ok = true;
    fake_udp_rx(&f, 2, host, 40002, req, 8);
    fake_udp_rx(&f, 2, host, 40003, req, 8);
    CHECK(lan_disc_service(&d, &s, buf, &inflight) == 1 && inflight, "third reply in flight");
    CHECK(!lan_disc_service(&d, &s, buf, &inflight) && f.out_n == 3, "fourth waits for SEND_OK");
    fake_release_send(&f, 2);
    f.hold_send_ok = false;
    CHECK(lan_disc_service(&d, &s, buf, &inflight) == 1 && f.out_n == 4 && f.out[3].port == 40003, "then answered");
    const uint8_t junk[12] = "hello world";
    fake_udp_rx(&f, 2, host, 40004, junk, 12);
    fake_udp_rx(&f, 2, host, 40005, req, 5);
    (void)lan_disc_service(&d, &s, buf, &inflight);
    (void)lan_disc_service(&d, &s, buf, &inflight);
    CHECK(f.out_n == 4 && !w5500_rx_size(&d, 2), "foreign and short datagrams dropped");
    CHECK_SUMMARY("lan_status");
}
