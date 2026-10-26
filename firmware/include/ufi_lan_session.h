#pragma once
/* UFI v2 session over TCP (stage L4): W5500 socket 0 listens on LAN.CFG `port`, one
 * connection at a time, the frames are exactly the USB ones (55 AA type cmd seq status
 * len16 payload crc16).  This module owns the socket, reassembles frames from the TCP
 * stream, answers frame errors and the lan_key challenge itself and hands every other
 * frame to the firmware through a callback.  HAL-free. */
#include "ufi_w5500.h"
#include <stdbool.h>
#include <stdint.h>

#define LAN_V2_HDR          8u
#define LAN_V2_MAX_PAYLOAD  4096u
#define LAN_V2_FRAME_MAX    (LAN_V2_HDR + LAN_V2_MAX_PAYLOAD + 2u)
#define LAN_V2_REQUEST      1u
#define LAN_V2_REPLY        2u
#define LAN_V2_DATA         4u
#define LAN_V2_NET          0x48u       /* LAN status (stage L5), USB and LAN */
#define LAN_V2_AUTH         0x49u       /* lan_key challenge, LAN only */
#define LAN_NONCE_SIZE      32u
#define LAN_AUTH_TRIES      3u          /* wrong answers before the connection is dropped */
#define LAN_KEEPALIVE_5S    2u          /* Sn_KPALVTR: keep-alive every 10 s once data flowed */

/* status bytes the session sends itself (docs/USB_Protokoll.md 3.2) */
#define LAN_ST_FRAME        14u
#define LAN_ST_BAD_ARGS     16u
#define LAN_ST_AUTH_REQUIRED 25u
#define LAN_ST_AUTH_FAILED  26u

enum { LAN_SESS_DOWN = 0, LAN_SESS_LISTEN, LAN_SESS_CONNECTED };
enum { LAN_TX_OK = 0, LAN_TX_BUSY, LAN_TX_CLOSED };

typedef struct {
    uint8_t state;
    bool authed;                        /* no key, or the challenge was answered */
    bool nonce_valid;                   /* a challenge is outstanding */
    bool inflight;                      /* SEND issued, SEND_OK not seen yet */
    bool drop;                          /* close at the next service (auth failures, TX stall) */
    uint8_t fails;
    uint8_t nonce[LAN_NONCE_SIZE];
    uint8_t peer[4];
    uint16_t peer_port, port;
    const char* key;                    /* lan_key (not copied), key_len 0 = open */
    uint8_t key_len;
    uint32_t connected_ms;
    uint32_t frames, frame_errors, sessions;
    uint8_t* frame;                     /* LAN_V2_FRAME_MAX bytes, owned by the caller */
} lan_session_t;

typedef struct {
    void* ctx;
    bool (*can_dispatch)(void* ctx);    /* room for the reply a request may produce */
    void (*frame)(void* ctx, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t* payload, uint16_t len);
    void (*nonce)(void* ctx, uint8_t out[LAN_NONCE_SIZE]);
    void (*connected)(void* ctx);
    void (*closed)(void* ctx);
} lan_session_cb_t;

void lan_session_init(lan_session_t* s, uint16_t port, const char* key, uint8_t key_len, uint8_t* frame);
void lan_session_service(lan_session_t* s, const w5500_t* d, uint32_t now, const lan_session_cb_t* cb);
/* close the connection (address change, LAN off); calls cb->closed when one was open */
void lan_session_close(lan_session_t* s, const w5500_t* d, const lan_session_cb_t* cb);
int lan_session_send(lan_session_t* s, const w5500_t* d, const uint8_t* p, uint16_t n);
bool lan_session_ready(lan_session_t* s, const w5500_t* d, uint16_t n);  /* send of n would not wait */
bool lan_session_connected(const lan_session_t* s);
/* frame helpers (CRC-16/CCITT-FALSE over type..payload, little endian like USB) */
uint16_t lan_crc16(const uint8_t* p, uint32_t n);
uint16_t lan_frame(uint8_t* out, uint8_t type, uint8_t cmd, uint8_t seq, uint8_t status,
                   const uint8_t* payload, uint16_t len);
