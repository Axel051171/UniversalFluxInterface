/**
 * UFI Flux Engine - UFI v2 session over TCP (stage L4)
 *
 * W5500 socket 0 in TCP server mode on LAN.CFG `port`: LISTEN accepts one connection; while
 * it is ESTABLISHED there is no listening socket, so a second client gets a reset - one
 * session at a time.  Keep-alive (Sn_KPALVTR, 10 s) and the chip's retransmission timeout
 * end a connection whose peer vanished; the socket then reads CLOSED and the firmware
 * callback puts the drive into the safe state.
 *
 * Frames are read from the TCP stream without consuming it first (peek), so a frame that
 * arrives in pieces simply waits: the header is checked (55 AA, length <= 4096), the
 * whole frame is peeked once present, its CRC checked, and only then consumed.  Garbage
 * before a frame start is skipped; a bad length or CRC answers the USB frame error (cmd
 * 0xFF, seq 0, status 14) and the scan resumes one byte later, exactly like the USB parser.
 * A request is taken only while the TX side has room for its reply.
 *
 * lan_key (op 0x49 AUTH): empty request -> 32-byte challenge; 32-byte request -> must be
 * HMAC-SHA256(lan_key, challenge).  Until then every other request is answered with
 * status 25 and DATA frames are dropped.  A challenge answers once; three wrong answers
 * close the connection.  Without a key the session is authenticated from the start.
 *
 * SEND: one SEND at a time (the datasheet wants SEND_OK before the next SEND); a frame is
 * written into the TX buffer only when it fits completely, so frames never interleave.
 */

#include "ufi_lan_session.h"
#include "ufi_sha256.h"
#include <string.h>

#define FRAME_ERROR_CMD     0xFFu
#define SCAN_CHUNK          64u
#define FRAMES_PER_SERVICE  8u

uint16_t lan_crc16(const uint8_t* p, uint32_t n)
{
    uint16_t crc = 0xFFFFu;             /* CRC-16/CCITT-FALSE, as ufi_v2.c */
    while (n--) {
        uint8_t x = (uint8_t)((crc >> 8) ^ *p++);
        x ^= (uint8_t)(x >> 4);
        crc = (uint16_t)((crc << 8) ^ ((uint16_t)x << 12) ^ ((uint16_t)x << 5) ^ x);
    }
    return crc;
}

uint16_t lan_frame(uint8_t* out, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t status,
                   const uint8_t* payload, uint16_t len)
{
    out[0] = 0x55u;
    out[1] = 0xAAu;
    out[2] = type;
    out[3] = cmd;
    out[4] = seq;
    out[5] = status;
    out[6] = (uint8_t)len;
    out[7] = (uint8_t)(len >> 8);
    if (len) {
        memmove(&out[LAN_V2_HDR], payload, len);
    }
    const uint16_t crc = lan_crc16(&out[2], 6u + len);
    out[LAN_V2_HDR + len] = (uint8_t)crc;
    out[LAN_V2_HDR + len + 1u] = (uint8_t)(crc >> 8);
    return (uint16_t)(LAN_V2_HDR + len + 2u);
}

void lan_session_init(lan_session_t* s, uint16_t port, const char* key, uint8_t key_len, uint8_t* frame)
{
    memset(s, 0, sizeof(*s));
    s->port = port;
    s->key = key;
    s->key_len = key_len;
    s->frame = frame;
}

bool lan_session_connected(const lan_session_t* s)
{
    return s->state == LAN_SESS_CONNECTED;
}

/* SEND_OK of the last SEND seen (or the connection ended): another SEND may follow */
static void tx_settle(lan_session_t* s, const w5500_t* d)
{
    if (!s->inflight) {
        return;
    }
    const uint8_t ir = w5500_sock_ir(d, W5500_SOCK_SESSION);
    if (ir & (W5500_SIR_SEND_OK | W5500_SIR_TIMEOUT)) {
        w5500_sock_ir_clear(d, W5500_SOCK_SESSION, W5500_SIR_SEND_OK);
        s->inflight = false;
    }
}

bool lan_session_ready(lan_session_t* s, const w5500_t* d, uint16_t n)
{
    if (s->state != LAN_SESS_CONNECTED || s->drop) {
        return false;
    }
    tx_settle(s, d);
    return !s->inflight && w5500_tx_free(d, W5500_SOCK_SESSION) >= n;
}

int lan_session_send(lan_session_t* s, const w5500_t* d, const uint8_t* p, uint16_t n)
{
    if (s->state != LAN_SESS_CONNECTED || s->drop ||
        w5500_sock_status(d, W5500_SOCK_SESSION) != W5500_SOCK_ESTABLISHED) {
        return LAN_TX_CLOSED;
    }
    tx_settle(s, d);
    if (s->inflight || w5500_tx_free(d, W5500_SOCK_SESSION) < n) {
        return LAN_TX_BUSY;
    }
    w5500_tx_put(d, W5500_SOCK_SESSION, p, n);
    if (!w5500_sock_cmd(d, W5500_SOCK_SESSION, W5500_CMD_SEND)) {
        return LAN_TX_CLOSED;
    }
    s->inflight = true;
    return LAN_TX_OK;
}

static void reply(lan_session_t* s, const w5500_t* d, uint8_t cmd, uint8_t seq, uint8_t status,
                  const uint8_t* payload, uint16_t len)
{
    uint8_t f[LAN_V2_HDR + 2u + LAN_NONCE_SIZE + 2u];
    if (len > LAN_NONCE_SIZE + 2u) {
        return;
    }
    (void)lan_session_send(s, d, f, lan_frame(f, LAN_V2_REPLY, cmd, seq, status, payload, len));
}

static void auth(lan_session_t* s, const w5500_t* d, const lan_session_cb_t* cb, uint8_t seq,
                 const uint8_t* p, uint16_t len)
{
    if (len == 0u) {
        uint8_t out[2u + LAN_NONCE_SIZE];
        cb->nonce(cb->ctx, s->nonce);
        s->nonce_valid = true;
        out[0] = 1;                                     /* schema */
        out[1] = (uint8_t)((s->key_len ? 1u : 0u) | (s->authed ? 2u : 0u));
        memcpy(&out[2], s->nonce, LAN_NONCE_SIZE);
        reply(s, d, LAN_V2_AUTH, seq, 0, out, sizeof(out));
        return;
    }
    if (len != LAN_NONCE_SIZE) {
        reply(s, d, LAN_V2_AUTH, seq, LAN_ST_BAD_ARGS, NULL, 0);
        return;
    }
    if (!s->key_len) {
        reply(s, d, LAN_V2_AUTH, seq, 0, NULL, 0);     /* open device: nothing to prove */
        return;
    }
    bool ok = false;
    if (s->nonce_valid) {
        uint8_t mac[SHA256_SIZE];
        hmac_sha256((const uint8_t*)s->key, s->key_len, s->nonce, LAN_NONCE_SIZE, mac);
        ok = sha256_equal(mac, p, SHA256_SIZE);
    }
    s->nonce_valid = false;                             /* one answer per challenge */
    if (ok) {
        s->authed = true;
        s->fails = 0;
        reply(s, d, LAN_V2_AUTH, seq, 0, NULL, 0);
        return;
    }
    reply(s, d, LAN_V2_AUTH, seq, LAN_ST_AUTH_FAILED, NULL, 0);
    if (++s->fails >= LAN_AUTH_TRIES) {
        s->drop = true;
    }
}

static void frame_error(lan_session_t* s, const w5500_t* d)
{
    s->frame_errors++;
    reply(s, d, FRAME_ERROR_CMD, 0, LAN_ST_FRAME, NULL, 0);
}

/* One frame (or one resync step) from the RX buffer; false: nothing more to do now. */
static bool next_frame(lan_session_t* s, const w5500_t* d, const lan_session_cb_t* cb)
{
    const uint16_t avail = w5500_rx_size(d, W5500_SOCK_SESSION);
    if (avail < 2u) {
        return false;
    }
    uint8_t b[SCAN_CHUNK];
    const uint16_t look = avail < SCAN_CHUNK ? avail : (uint16_t)SCAN_CHUNK;
    w5500_rx_peek(d, W5500_SOCK_SESSION, 0, b, look);
    uint16_t k = 0;
    while (k < look && !(b[k] == 0x55u && (k + 1u == look || b[k + 1u] == 0xAAu))) {
        k++;
    }
    if (k) {
        w5500_rx_skip(d, W5500_SOCK_SESSION, k);       /* bytes before a frame start */
        return true;
    }
    if (avail < LAN_V2_HDR) {
        return false;
    }
    if (look < LAN_V2_HDR) {
        w5500_rx_peek(d, W5500_SOCK_SESSION, 0, b, LAN_V2_HDR);
    }
    const uint16_t len = (uint16_t)(b[6] | (b[7] << 8));
    if (len > LAN_V2_MAX_PAYLOAD) {
        if (!cb->can_dispatch(cb->ctx) || !lan_session_ready(s, d, 16u)) {
            return false;
        }
        frame_error(s, d);
        w5500_rx_skip(d, W5500_SOCK_SESSION, 1u);
        return true;
    }
    const uint16_t total = (uint16_t)(LAN_V2_HDR + len + 2u);
    if (avail < total) {
        return false;                                   /* rest still on the way */
    }
    if (!cb->can_dispatch(cb->ctx)) {
        return false;                                   /* reply room first: TCP holds the rest */
    }
    uint8_t* f = s->frame;
    w5500_rx_peek(d, W5500_SOCK_SESSION, 0, f, total);
    const uint16_t crc = (uint16_t)(f[LAN_V2_HDR + len] | (f[LAN_V2_HDR + len + 1u] << 8));
    if (crc != lan_crc16(&f[2], 6u + len)) {
        frame_error(s, d);
        w5500_rx_skip(d, W5500_SOCK_SESSION, 1u);
        return true;
    }
    w5500_rx_skip(d, W5500_SOCK_SESSION, total);
    s->frames++;
    const uint8_t type = f[2], cmd = f[3], seq = f[4];
    if (type == LAN_V2_REQUEST && cmd == LAN_V2_AUTH) {
        auth(s, d, cb, seq, &f[LAN_V2_HDR], len);
    } else if (!s->authed) {
        if (type == LAN_V2_REQUEST) {
            reply(s, d, cmd, seq, LAN_ST_AUTH_REQUIRED, NULL, 0);
        }                                               /* DATA without a session: dropped */
    } else {
        cb->frame(cb->ctx, type, cmd, seq, &f[LAN_V2_HDR], len);
    }
    return true;
}

static void closed(lan_session_t* s, const w5500_t* d, const lan_session_cb_t* cb)
{
    const bool was = s->state == LAN_SESS_CONNECTED;
    w5500_sock_close(d, W5500_SOCK_SESSION);
    s->state = LAN_SESS_DOWN;
    s->authed = s->nonce_valid = s->inflight = s->drop = false;
    memset(s->nonce, 0, sizeof(s->nonce));
    if (was && cb->closed) {
        cb->closed(cb->ctx);
    }
}

void lan_session_close(lan_session_t* s, const w5500_t* d, const lan_session_cb_t* cb)
{
    if (s->state == LAN_SESS_CONNECTED) {
        (void)w5500_sock_cmd(d, W5500_SOCK_SESSION, W5500_CMD_DISCON);
    }
    closed(s, d, cb);
}

void lan_session_service(lan_session_t* s, const w5500_t* d, uint32_t now, const lan_session_cb_t* cb)
{
    const uint8_t sr = w5500_sock_status(d, W5500_SOCK_SESSION);
    w5500_sock_ir_clear(d, W5500_SOCK_SESSION, W5500_SIR_CON | W5500_SIR_DISCON | W5500_SIR_RECV);
    switch (s->state) {
        case LAN_SESS_DOWN:
            if (w5500_sock_open(d, W5500_SOCK_SESSION, W5500_MODE_TCP, s->port)) {
                w5500_write8(d, W5500_SREG(W5500_SOCK_SESSION), W5500_Sn_KPALVTR, LAN_KEEPALIVE_5S);
                if (w5500_sock_cmd(d, W5500_SOCK_SESSION, W5500_CMD_LISTEN) &&
                    w5500_sock_status(d, W5500_SOCK_SESSION) == W5500_SOCK_LISTEN) {
                    s->state = LAN_SESS_LISTEN;
                }
            }
            break;
        case LAN_SESS_LISTEN:
            if (sr == W5500_SOCK_ESTABLISHED) {
                uint8_t port[2];
                w5500_read(d, W5500_SREG(W5500_SOCK_SESSION), W5500_Sn_DIPR, s->peer, 4u);
                w5500_read(d, W5500_SREG(W5500_SOCK_SESSION), W5500_Sn_DPORT, port, 2u);
                s->peer_port = (uint16_t)((port[0] << 8) | port[1]);
                s->state = LAN_SESS_CONNECTED;
                s->authed = s->key_len == 0u;
                s->nonce_valid = s->inflight = s->drop = false;
                s->fails = 0;
                s->connected_ms = now;
                s->sessions++;
                if (cb->connected) {
                    cb->connected(cb->ctx);
                }
            } else if (sr != W5500_SOCK_LISTEN && sr != W5500_SOCK_SYNRECV) {
                closed(s, d, cb);                       /* half-open attempt failed: listen again */
            }
            break;
        case LAN_SESS_CONNECTED:
            if (s->drop || sr != W5500_SOCK_ESTABLISHED) {
                lan_session_close(s, d, cb);            /* peer closed, timed out or refused */
                break;
            }
            tx_settle(s, d);
            for (unsigned i = 0; i < FRAMES_PER_SERVICE && s->state == LAN_SESS_CONNECTED && !s->drop; i++) {
                if (!next_frame(s, d, cb)) {
                    break;
                }
            }
            break;
        default:
            s->state = LAN_SESS_DOWN;
            break;
    }
}
