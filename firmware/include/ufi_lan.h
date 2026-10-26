#pragma once
/* LAN on board v1.0: W5500 U23 on SPI3 (ufi_lan.c, board glue). */
#include <stdbool.h>
#include <stdint.h>
#include "ufi_lan_cfg.h"

/* chip / link state (ufi net, NET.TXT) */
enum {
    LAN_STATE_NO_HW = 0,        /* board before v1.0: no W5500 fitted */
    LAN_STATE_NO_CHIP = 1,      /* v1.0, but VERSIONR != 0x04 or SHAR read-back failed */
    LAN_STATE_OFF = 2,          /* LAN.CFG lan=off: chip held in reset */
    LAN_STATE_LINK_DOWN = 3,
    LAN_STATE_LINK_UP = 4,      /* PHY link, no address yet */
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
