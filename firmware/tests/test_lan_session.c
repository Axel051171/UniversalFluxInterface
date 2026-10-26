/* UFI v2 over TCP (ufi_lan_session.c) against the W5500 fake: listen/accept/close, frames
 * in pieces and with garbage, frame errors like USB, reply-room gating, one SEND at a
 * time, the lan_key challenge (HMAC-SHA256, one answer per challenge, three strikes). */
#include "check.h"
#include "w5500_fake.h"
#include "ufi_lan_session.h"
#include "ufi_sha256.h"
#include <string.h>

static w5500_fake_t f;
static w5500_t d;
static lan_session_t s;
static uint8_t frame_buf[LAN_V2_FRAME_MAX];
static const uint8_t PEER[4] = {192, 168, 1, 20};

/* callback doubles */
static bool room = true;
static unsigned got, connects, closes;
static uint8_t got_type, got_cmd, got_seq, got_payload[LAN_V2_MAX_PAYLOAD];
static uint16_t got_len;
static bool can(void* ctx) { (void)ctx; return room; }
static void on_frame(void* ctx, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t* p, uint16_t len)
{
    (void)ctx;
    got++; got_type = type; got_cmd = cmd; got_seq = seq; got_len = len;
    memcpy(got_payload, p, len);
}
static void nonce(void* ctx, uint8_t out[LAN_NONCE_SIZE])
{
    static uint8_t n;
    (void)ctx;
    for (unsigned i = 0; i < LAN_NONCE_SIZE; i++) out[i] = (uint8_t)(n * 31u + i);
    n++;
}
static void on_connect(void* ctx) { (void)ctx; connects++; }
static void on_close(void* ctx) { (void)ctx; closes++; }
static const lan_session_cb_t cb = {NULL, can, on_frame, nonce, on_connect, on_close};

static uint32_t out_pos;                /* replies read so far from f.tcp_out */
typedef struct { uint8_t type, cmd, seq, status; uint16_t len; uint8_t p[64]; } frame_t;

static bool next_reply(frame_t* r)
{
    if (f.tcp_out_n - out_pos < 10u) return false;
    const uint8_t* b = &f.tcp_out[out_pos];
    if (b[0] != 0x55 || b[1] != 0xAA) return false;
    r->type = b[2]; r->cmd = b[3]; r->seq = b[4]; r->status = b[5];
    r->len = (uint16_t)(b[6] | b[7] << 8);
    const uint16_t crc = (uint16_t)(b[8 + r->len] | b[9 + r->len] << 8);
    if (crc != lan_crc16(&b[2], 6u + r->len)) return false;
    memcpy(r->p, &b[8], r->len < 64 ? r->len : 64);
    out_pos += 10u + r->len;
    return true;
}

static uint16_t req(uint8_t* out, uint8_t type, uint8_t cmd, uint8_t seq, const void* p, uint16_t n)
{
    return lan_frame(out, type, cmd, seq, 0, (const uint8_t*)p, n);
}

static void service(void) { lan_session_service(&s, &d, 1000, &cb); }

static void connect(const char* key)
{
    fake_init(&f);
    d = fake_dev(&f);
    (void)w5500_reset(&d);
    const uint8_t mac[6] = {2, 1, 2, 3, 4, 5};
    (void)w5500_setup(&d, mac);
    lan_session_init(&s, 55100, key, (uint8_t)strlen(key), frame_buf);
    out_pos = 0; got = connects = closes = 0; room = true;
    service();
    (void)fake_tcp_accept(&f, 0, PEER, 50123);
    service();
}

int main(void)
{
    uint8_t m[LAN_V2_FRAME_MAX + 16];
    frame_t r;

    /* listen and accept */
    fake_init(&f);
    d = fake_dev(&f);
    (void)w5500_reset(&d);
    lan_session_init(&s, 55100, "", 0, frame_buf);
    service();
    CHECK(s.state == LAN_SESS_LISTEN && f.sreg[0][W5500_Sn_SR] == W5500_SOCK_LISTEN && f.sreg[0][W5500_Sn_PORT] == 0xD7 &&
          f.sreg[0][W5500_Sn_PORT + 1] == 0x3C && f.sreg[0][W5500_Sn_KPALVTR] == 2 && (f.sreg[0][W5500_Sn_MR] & 0x0F) == 1,
          "TCP listen on 55100 with keep-alive");
    connect("");
    CHECK(lan_session_connected(&s) && connects == 1 && !memcmp(s.peer, PEER, 4) && s.peer_port == 50123 && s.authed,
          "connected, open device authenticated");
    CHECK(lan_session_ready(&s, &d, LAN_V2_FRAME_MAX), "room for a full frame");

    /* a request in pieces */
    uint16_t n = req(m, LAN_V2_REQUEST, 0x00, 7, "UFI2", 4);
    fake_tcp_rx(&f, 0, m, 5);
    service();
    CHECK(!got, "partial header waits");
    fake_tcp_rx(&f, 0, m + 5, 6);
    service();
    CHECK(!got, "partial payload waits");
    fake_tcp_rx(&f, 0, m + 11, (unsigned)(n - 11));
    service();
    CHECK(got == 1 && got_type == 1 && got_cmd == 0 && got_seq == 7 && got_len == 4 && !memcmp(got_payload, "UFI2", 4),
          "PING dispatched");
    CHECK(w5500_rx_size(&d, 0) == 0, "consumed");

    /* garbage, a lone 55, then a frame */
    const uint8_t junk[] = {1, 2, 0x55, 3, 0xAA, 0x55};
    fake_tcp_rx(&f, 0, junk, sizeof(junk));
    n = req(m, LAN_V2_REQUEST, 0x01, 8, NULL, 0);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(got == 2 && got_cmd == 1 && got_seq == 8 && !next_reply(&r), "resync, no error for garbage");

    /* bad CRC: frame error like USB, scanning goes on */
    n = req(m, LAN_V2_REQUEST, 0x02, 9, "x", 1);
    m[n - 1] ^= 0x40;
    fake_tcp_rx(&f, 0, m, n);
    n = req(m, LAN_V2_REQUEST, 0x02, 10, NULL, 0);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.type == 2 && r.cmd == 0xFF && r.seq == 0 && r.status == 14 && s.frame_errors == 1, "CRC -> frame error");
    service();
    CHECK(got == 3 && got_seq == 10, "following frame taken");
    /* length above 4096 */
    m[0] = 0x55; m[1] = 0xAA; m[2] = 1; m[3] = 0; m[4] = 1; m[5] = 0; m[6] = 0x01; m[7] = 0x10;
    fake_tcp_rx(&f, 0, m, 8);
    service();
    CHECK(next_reply(&r) && r.cmd == 0xFF && r.status == 14 && w5500_rx_size(&d, 0) == 0, "length 4097 refused");

    /* no reply room: the request stays in the chip until there is */
    room = false;
    n = req(m, LAN_V2_REQUEST, 0x03, 11, NULL, 0);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(got == 3 && w5500_rx_size(&d, 0) == n, "held back");
    room = true;
    service();
    CHECK(got == 4 && got_seq == 11, "taken once there is room");

    /* DATA frames pass through after authentication too */
    n = req(m, LAN_V2_DATA, 0x22, 11, "\xf2\x48", 2);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(got == 5 && got_type == 4 && got_cmd == 0x22 && got_len == 2, "DATA dispatched");

    /* one SEND at a time */
    f.hold_send_ok = true;
    n = lan_frame(m, 2, 0, 1, 0, (const uint8_t*)"abc", 3);
    const uint32_t before = f.tcp_out_n;
    CHECK(lan_session_send(&s, &d, m, n) == LAN_TX_OK && f.tcp_out_n == before + n, "first SEND");
    CHECK(lan_session_send(&s, &d, m, n) == LAN_TX_BUSY && !lan_session_ready(&s, &d, 1), "second waits for SEND_OK");
    fake_release_send(&f, 0);
    CHECK(lan_session_send(&s, &d, m, n) == LAN_TX_OK, "after SEND_OK");
    f.hold_send_ok = false;
    fake_release_send(&f, 0);
    static uint8_t big[LAN_V2_FRAME_MAX];
    CHECK(lan_session_ready(&s, &d, LAN_V2_FRAME_MAX) && lan_session_send(&s, &d, big, LAN_V2_FRAME_MAX) == LAN_TX_OK, "full frame");

    /* peer closes: callback, then listening again */
    fake_tcp_peer_close(&f, 0);
    service();
    CHECK(closes == 1 && s.state == LAN_SESS_DOWN && f.sreg[0][W5500_Sn_SR] == W5500_SOCK_CLOSED, "peer close");
    CHECK(lan_session_send(&s, &d, m, n) == LAN_TX_CLOSED, "no sending without a connection");
    service();
    CHECK(s.state == LAN_SESS_LISTEN, "listening again");
    (void)fake_tcp_accept(&f, 0, PEER, 50124);
    service();
    CHECK(connects == 2 && s.peer_port == 50124 && s.sessions == 2, "second session");
    fake_tcp_lost(&f, 0);
    service();
    CHECK(closes == 2 && s.state == LAN_SESS_DOWN, "lost connection (keep-alive / retransmission timeout)");

    /* open device: AUTH reports no key */
    connect("");
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 1, NULL, 0);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.cmd == LAN_V2_AUTH && !r.status && r.len == 34 && r.p[0] == 1 && r.p[1] == 2 && !got,
          "challenge on an open device: flags 'authenticated'");

    /* keyed device */
    const char* key = "s3cret";
    connect(key);
    CHECK(!s.authed, "key: not authenticated yet");
    n = req(m, LAN_V2_REQUEST, 0x00, 1, "UFI2", 4);
    fake_tcp_rx(&f, 0, m, n);
    n = req(m, LAN_V2_DATA, 0x22, 1, "zz", 2);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.cmd == 0 && r.seq == 1 && r.status == 25 && !got && !next_reply(&r), "PING refused, DATA dropped");
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 2, NULL, 0);
    fake_tcp_rx(&f, 0, m, n);
    service();
    uint8_t chal[32];
    CHECK(next_reply(&r) && r.cmd == LAN_V2_AUTH && r.seq == 2 && !r.status && r.len == 34 && r.p[0] == 1 && r.p[1] == 1,
          "challenge");
    memcpy(chal, &r.p[2], 32);
    uint8_t answer[32];
    hmac_sha256((const uint8_t*)key, strlen(key), chal, 32, answer);
    answer[0] ^= 1;                                         /* wrong */
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 3, answer, 32);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.status == 26 && !s.authed && s.fails == 1, "wrong answer");
    answer[0] ^= 1;                                         /* right, but the challenge is spent */
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 4, answer, 32);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.status == 26 && !s.authed, "a challenge answers once");
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 5, "short", 5);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.status == 16, "bad length");
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 6, NULL, 0);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && !r.status && memcmp(&r.p[2], chal, 32), "fresh challenge");
    hmac_sha256((const uint8_t*)key, strlen(key), &r.p[2], 32, answer);
    n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, 7, answer, 32);
    fake_tcp_rx(&f, 0, m, n);
    n = req(m, LAN_V2_REQUEST, 0x00, 8, "UFI2", 4);
    fake_tcp_rx(&f, 0, m, n);
    service();
    CHECK(next_reply(&r) && r.seq == 7 && !r.status && s.authed && got == 1 && got_seq == 8, "authenticated, PING passes");

    /* three wrong answers close the connection */
    connect(key);
    for (uint8_t i = 0; i < 3; i++) {
        n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, (uint8_t)(10 + 2 * i), NULL, 0);
        fake_tcp_rx(&f, 0, m, n);
        memset(answer, i, 32);
        n = req(m, LAN_V2_REQUEST, LAN_V2_AUTH, (uint8_t)(11 + 2 * i), answer, 32);
        fake_tcp_rx(&f, 0, m, n);
        service();
    }
    service();
    CHECK(closes == 1 && s.state == LAN_SESS_DOWN, "dropped after three failures");
    CHECK_SUMMARY("lan_session");
}
