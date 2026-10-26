#pragma once
/* LAN status for the host (stage L5): NET op 0x48 (USB and LAN), NET.TXT on U15 and the
 * UDP discovery responder for `ufi discover`.  HAL-free: the board glue fills
 * lan_status_t, this module packs and answers. */
#include "ufi_w5500.h"
#include "ufi_lan_cfg.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_NET_TXT         "NET.TXT"
#define LAN_STATUS_MAX      (43u + LAN_HOST_MAX)    /* NET reply */
#define LAN_DISC_REPLY_MAX  (22u + LAN_HOST_MAX)
#define LAN_DISC_REQ_SIZE   8u                      /* "UFI?", version 1, 3 reserved */
#define LAN_DISC_VERSION    1u

/* lan_status_t.flags */
#define LAN_F_LINK          0x01u
#define LAN_F_100M          0x02u
#define LAN_F_FULL_DUPLEX   0x04u
#define LAN_F_CONNECTED     0x08u
#define LAN_F_AUTHED        0x10u
#define LAN_F_WRITE         0x20u                   /* lan_write=1 */
#define LAN_F_KEY           0x40u                   /* lan_key set */
#define LAN_F_CONFLICT      0x80u                   /* address conflict seen */

typedef struct {
    uint8_t state;                  /* LAN_STATE_* (ufi_lan.h) */
    uint8_t mode;                   /* NET_MODE_* (ufi_net.h) */
    uint8_t flags;
    uint8_t owner;                  /* drive owner, UFI_ROUTE_* */
    uint8_t version;                /* VERSIONR, 0 without chip */
    uint8_t ip[4], mask[4], gw[4], dns[4], mac[6], peer[4];
    uint16_t port, peer_port;
    uint32_t lease_left;            /* s; 0xFFFFFFFF infinite or fixed, 0 none */
    uint8_t fw_major, fw_minor, board_rev;
    char host[LAN_HOST_MAX + 1];
} lan_status_t;

/* NET op reply, returns its length (<= LAN_STATUS_MAX) */
uint16_t lan_status_pack(const lan_status_t* s, uint8_t* out);
/* NET.TXT, CRLF lines; returns the length (out at least LAN_TEXT_MAX bytes) */
#define LAN_TEXT_MAX        400u
size_t lan_status_text(const lan_status_t* s, char* out);
/* discovery */
bool lan_disc_is_request(const uint8_t* p, uint16_t n);
uint16_t lan_disc_reply(const lan_status_t* s, uint8_t* out);
/* Socket 2 (UDP LAN_DISCOVER_PORT): opens it, answers every request with a unicast reply
 * to the asker, or a broadcast when the asker is not on our subnet (link-local device,
 * routed host); one reply per call, the next only after SEND_OK / TIMEOUT of the last
 * (*inflight); buf >= LAN_DISC_REPLY_MAX bytes of scratch.  Returns the replies sent. */
unsigned lan_disc_service(const w5500_t* d, const lan_status_t* s, uint8_t* buf, bool* inflight);
