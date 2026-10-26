#pragma once
/* LAN on board v1.0: W5500 U23 on SPI3 (ufi_lan.c, board glue). */
#include <stdbool.h>
#include <stdint.h>
#include "ufi_lan_cfg.h"
#include "ufi_net.h"
#include "ufi_lan_status.h"

/* chip / link state (ufi net, NET.TXT) */
enum {
    LAN_STATE_NO_HW = 0,        /* board before v1.0: no W5500 fitted */
    LAN_STATE_NO_CHIP = 1,      /* v1.0, but VERSIONR != 0x04 or SHAR read-back failed */
    LAN_STATE_OFF = 2,          /* LAN.CFG lan=off: chip held in reset */
    LAN_STATE_LINK_DOWN = 3,
    LAN_STATE_LINK_UP = 4,      /* PHY link, no address yet */
    LAN_STATE_BOUND = 5,        /* address in use (DHCP, link-local or fixed: ufi_net.h) */
};

/* where the settings came from */
enum { LAN_CFG_DEFAULTS = 0, LAN_CFG_FILE = 1, LAN_CFG_WRITTEN = 2 };

void ufi_lan_init(void);        /* after ufi_sd_init / ufi_config_load: LAN.CFG, detect, reset, MAC */
void ufi_lan_service(void);     /* main loop */
void ufi_lan_reload(void);      /* CFG_RELOAD: LAN.CFG again at the next service */
uint8_t ufi_lan_state(void);
uint8_t ufi_lan_version(void);  /* VERSIONR as read at start-up (0 without chip) */
uint8_t ufi_lan_phy(void);      /* last PHYCFGR value */
void ufi_lan_mac(uint8_t mac[6]);
const lan_cfg_t* ufi_lan_cfg(void);
uint8_t ufi_lan_cfg_source(void);
const char* ufi_lan_hostname(void);
const net_t* ufi_lan_net(void);
void ufi_lan_status(lan_status_t* s);     /* NET 0x48, NET.TXT, discovery (L5) */

/* LAN session as a transport of native frames (L4) */
bool ufi_lan_tx_idle(void);
bool ufi_lan_tx_room(void);
int ufi_lan_tx_start(const uint8_t* p, uint32_t n);
int ufi_lan_tx_blocking(const uint8_t* p, uint32_t n);
bool ufi_lan_connected(void);
bool ufi_lan_write_allowed(void);     /* lan_write=1 and the hardware write lock open */

/* Transports of UFI v2 frames and drive ownership (ufi_v2.c, L4) */
enum { UFI_ROUTE_NONE = 0, UFI_ROUTE_USB = 1, UFI_ROUTE_LAN = 2 };
void ufi_v2_init(void);
uint8_t ufi_v2_route(void);           /* where the next native frame goes */
uint8_t ufi_v2_owner(void);           /* UFI_ROUTE_* that owns the drive, NONE = free */
bool ufi_v2_claim(uint8_t origin);    /* take the drive for a transport; false = the other owns it */
void ufi_v2_lan_frame(uint8_t type, uint8_t cmd, uint8_t seq, uint8_t* p, uint16_t len);
void ufi_v2_lan_closed(void);         /* LAN session ended: safe state if LAN owned the drive */
