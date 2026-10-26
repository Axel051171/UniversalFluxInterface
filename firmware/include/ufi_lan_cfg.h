#pragma once
/* LAN.CFG on the SD NAND U15 (stage L2): same style as UFI.CFG - key=value lines, '#'
 * comments, CRLF, written with the defaults when missing.  HAL-free: the board glue
 * (ufi_lan.c) reads and writes the file, this module only turns text into settings. */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define LAN_CFG_NAME        "LAN.CFG"
#define LAN_CFG_MAX_TEXT    1023u       /* bytes read, as for UFI.CFG */
#define LAN_HOST_MAX        32u
#define LAN_KEY_MAX         64u
#define LAN_PORT_DEFAULT    55100u
#define LAN_DISCOVER_PORT   55100u      /* UDP, fixed: the host broadcasts before it knows `port` */

typedef struct {
    bool enabled;                       /* lan=on (default) / off */
    bool dhcp;                          /* ip=dhcp (default) or a fixed address */
    uint8_t ip[4], mask[4], gw[4], dns[4];
    bool mac_set;                       /* mac= given: overrides the UID-derived MAC */
    uint8_t mac[6];
    char hostname[LAN_HOST_MAX + 1];    /* empty: ufi-XXXXXX from the MAC */
    uint16_t port;                      /* TCP port of the v2 session */
    bool write;                         /* lan_write=1 */
    char key[LAN_KEY_MAX + 1];          /* lan_key, empty = no authentication */
    uint8_t key_len;
    uint8_t errors;                     /* values that were ignored (bad address, MAC, ...) */
} lan_cfg_t;

extern const char lan_cfg_default_text[];
size_t lan_cfg_default_len(void);
void lan_cfg_defaults(lan_cfg_t* c);
/* Defaults first, then every recognised key of the text (n bytes, stops at a NUL). */
void lan_cfg_parse(lan_cfg_t* c, const char* text, size_t n);
/* hostname= or ufi-XXXXXX from the last three MAC bytes (lower-case hex) */
void lan_cfg_hostname(const lan_cfg_t* c, const uint8_t mac[6], char out[LAN_HOST_MAX + 1]);
/* dotted quad: four decimal numbers 0-255; true only for the whole string */
bool lan_parse_ip(const char* s, size_t n, uint8_t ip[4]);
/* "a.b.c.d" into out (at least 16 bytes), returns the length */
size_t lan_fmt_ip(char* out, const uint8_t ip[4]);
/* "xx:xx:xx:xx:xx:xx" into out (at least 18 bytes), returns the length */
size_t lan_fmt_mac(char* out, const uint8_t mac[6]);
