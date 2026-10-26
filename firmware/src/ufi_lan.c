/**
 * UFI Flux Engine - LAN board glue (board v1.0, stages L1-L3)
 *
 * W5500 U23 on SPI3: PC10 SCK, PC11 MISO, PC12 MOSI (AF6), PA15 SCSn (GPIO, 10k pull-up
 * R119), PD10 RSTn (GPIO, R121), PD5 INTn (input, R120).  Kernel clock PLL1Q 100 MHz,
 * prescaler 4 -> 25 MHz SCK, SPI mode 0, polled HAL transfers with a 10 ms timeout (one
 * register access is 3 header bytes plus data, a whole 4 KB frame about 1.4 ms).
 *
 * Start-up: board_gpio_init() parks the chip in reset; here RSTn is pulsed again (>= 500 us
 * low, then 2 ms for the PLL lock, datasheet reset timing), MR.RST and VERSIONR = 0x04
 * identify the chip, SHAR gets the MAC derived from the STM32 unique ID (SHAR resets to
 * 00:00:00:00:00:00, so it must be written after every reset) and is read back.  Without a
 * chip the line goes back into reset and the state says why.
 *
 * LAN.CFG (L2) is read from U15 before the chip starts: lan=off keeps it in reset, mac=
 * replaces the UID MAC.  A missing file is written with the defaults; without a file
 * system the defaults apply without writing.  CFG_RELOAD (0x54) reads it again after its
 * reply went out and restarts the chip when anything changed.
 *
 * Address (L3, ufi_net.c): started when the PHY reports a link, stopped when it goes away
 * (DHCP again on the next link); serviced every 10 ms or at once when INTn is low.
 *
 * State lives in SRAM2 (.sram2 is NOLOAD, so ufi_lan_init() clears it first): the AXI SRAM
 * .bss/.data budget is frozen at the gate value and DTCM keeps its stack reserve.
 */

#include "ufi_firmware.h"
#include "ufi_lan.h"
#include "ufi_lan_cfg.h"
#include "ufi_net.h"
#include "ufi_w5500.h"
#include <string.h>

#define LAN_SPI_TIMEOUT_MS  10u
#define LAN_PHY_POLL_MS     500u
#define LAN_NET_POLL_MS     10u         /* DHCP / probe timers are seconds; INTn shortens it */

typedef struct {
    SPI_HandleTypeDef spi;
    uint8_t state;
    uint8_t version, phy;
    uint8_t mac[6], uid_mac[6];
    uint8_t cfg_source;
    bool reload;                        /* CFG_RELOAD: read LAN.CFG again at the next service */
    lan_cfg_t cfg;
    char host[LAN_HOST_MAX + 1];
    uint32_t phy_ms, net_ms;
    uint32_t spi_errors;
    net_t net;                          /* address: DHCP / link-local / fixed (L3) */
    uint8_t pkt[NET_PKT_MAX];           /* DHCP messages */
    uint8_t buf[UFI_V2_FRAME_MAX];      /* LAN.CFG text, later the session frame */
} lan_t;

__attribute__((section(".sram2"), aligned(4))) static lan_t lan;

/* ============================================================================
 * SPI TRANSPORT (w5500_t callbacks)
 * ============================================================================ */

static void lan_select(void* ctx, bool on)
{
    (void)ctx;
    HAL_GPIO_WritePin(PIN_LAN_CS.port, PIN_LAN_CS.pin, on ? GPIO_PIN_RESET : GPIO_PIN_SET);
}

static void lan_xfer(void* ctx, const uint8_t* tx, uint8_t* rx, uint16_t n)
{
    (void)ctx;
    HAL_StatusTypeDef r;
    if (tx && rx) {
        r = HAL_SPI_TransmitReceive(&lan.spi, (uint8_t*)tx, rx, n, LAN_SPI_TIMEOUT_MS);
    } else if (tx) {
        r = HAL_SPI_Transmit(&lan.spi, (uint8_t*)tx, n, LAN_SPI_TIMEOUT_MS);
    } else if (rx) {
        memset(rx, 0, n);               /* dummy bytes out; the RX pointer trails the TX pointer */
        r = HAL_SPI_TransmitReceive(&lan.spi, rx, rx, n, LAN_SPI_TIMEOUT_MS);
    } else {
        uint8_t zero[16] = {0};         /* not used by the driver; kept total for the callback contract */
        r = HAL_OK;
        for (uint32_t left = n; left && r == HAL_OK; ) {
            const uint16_t k = (uint16_t)(left < 16u ? left : 16u);
            r = HAL_SPI_Transmit(&lan.spi, zero, k, LAN_SPI_TIMEOUT_MS);
            left -= k;
        }
    }
    if (r != HAL_OK) {
        lan.spi_errors++;
    }
}

static const w5500_t dev = {NULL, lan_select, lan_xfer};

/* ============================================================================
 * START-UP
 * ============================================================================ */

static void spi_init(void)
{
    RCC_PeriphCLKInitTypeDef clk = {0};
    clk.PeriphClockSelection = RCC_PERIPHCLK_SPI123;
    clk.Spi123ClockSelection = RCC_SPI123CLKSOURCE_PLL;    /* PLL1Q = 100 MHz */
    (void)HAL_RCCEx_PeriphCLKConfig(&clk);
    __HAL_RCC_SPI3_CLK_ENABLE();
    __HAL_RCC_GPIOC_CLK_ENABLE();
    GPIO_InitTypeDef g = {0};
    g.Pin = PIN_LAN_SCK.pin | PIN_LAN_MISO.pin | PIN_LAN_MOSI.pin;
    g.Mode = GPIO_MODE_AF_PP;
    g.Pull = GPIO_NOPULL;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    g.Alternate = LAN_SPI_AF;
    HAL_GPIO_Init(PIN_LAN_SCK.port, &g);

    SPI_HandleTypeDef* h = &lan.spi;
    h->Instance = SPI3;
    h->Init.Mode = SPI_MODE_MASTER;
    h->Init.Direction = SPI_DIRECTION_2LINES;
    h->Init.DataSize = SPI_DATASIZE_8BIT;
    h->Init.CLKPolarity = SPI_POLARITY_LOW;             /* W5500: mode 0 or 3 */
    h->Init.CLKPhase = SPI_PHASE_1EDGE;
    h->Init.NSS = SPI_NSS_SOFT;                         /* SCSn is a GPIO: one frame per access */
    h->Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;
    h->Init.FirstBit = SPI_FIRSTBIT_MSB;
    h->Init.TIMode = SPI_TIMODE_DISABLE;
    h->Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
    h->Init.CRCPolynomial = 7;
    h->Init.NSSPMode = SPI_NSS_PULSE_DISABLE;
    h->Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
    h->Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
    h->Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
    h->Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
    h->Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
    h->Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_ENABLE;  /* SCK stays low between frames */
    h->Init.IOSwap = SPI_IO_SWAP_DISABLE;
    if (HAL_SPI_Init(h) != HAL_OK) {
        lan.spi_errors++;
    }
}

static void chip_reset_hold(void)
{
    HAL_GPIO_WritePin(PIN_LAN_RST.port, PIN_LAN_RST.pin, GPIO_PIN_RESET);
}

/* LAN.CFG from U15; missing -> written with the defaults; no file system -> defaults */
static void load_cfg(void)
{
    uint16_t got = 0;
    const int ret = ufi_file_read(LAN_CFG_NAME, 0, lan.buf, LAN_CFG_MAX_TEXT, &got);
    if (ret == UFI_OK) {
        lan_cfg_parse(&lan.cfg, (const char*)lan.buf, got);
        lan.cfg_source = LAN_CFG_FILE;
        return;
    }
    lan_cfg_defaults(&lan.cfg);
    lan.cfg_source = LAN_CFG_DEFAULTS;
    if (ret == UFI_ERR_NOT_FOUND) {
        uint16_t written = 0;
        if (ufi_file_write(LAN_CFG_NAME, 0, true, (const uint8_t*)lan_cfg_default_text,
                           (uint16_t)lan_cfg_default_len(), &written) == UFI_OK) {
            lan.cfg_source = LAN_CFG_WRITTEN;
        }
    }
}

/* Chip out of reset with the configured MAC, or held in reset (lan=off, no chip). */
static void lan_start(void)
{
    memcpy(lan.mac, lan.cfg.mac_set ? lan.cfg.mac : lan.uid_mac, 6u);
    lan_cfg_hostname(&lan.cfg, lan.mac, lan.host);
    if (lan.net.mode != NET_MODE_NONE) {
        lan.net.changes++;              /* the chip reset drops the address */
    }
    lan.net.mode = NET_MODE_NONE;
    lan.net.state = NET_IDLE;
    chip_reset_hold();
    if (!lan.cfg.enabled) {
        lan.state = LAN_STATE_OFF;
        return;
    }
    HAL_Delay(1);                       /* >= 500 us low */
    HAL_GPIO_WritePin(PIN_LAN_RST.port, PIN_LAN_RST.pin, GPIO_PIN_SET);
    HAL_Delay(2);                       /* internal PLL lock, 1 ms max */
    const bool reset_ok = w5500_reset(&dev);
    lan.version = w5500_version(&dev);
    if (!reset_ok || !w5500_setup(&dev, lan.mac)) {
        lan.state = LAN_STATE_NO_CHIP;
        chip_reset_hold();
        return;
    }
    lan.phy = w5500_phy(&dev);
    lan.phy_ms = HAL_GetTick();
    lan.state = (lan.phy & W5500_PHY_LNK) ? LAN_STATE_LINK_UP : LAN_STATE_LINK_DOWN;
}

void ufi_lan_init(void)
{
    memset(&lan, 0, sizeof(lan));
    if (ufi_board_rev() < 10u) {
        lan.state = LAN_STATE_NO_HW;    /* v0.x: pins unconnected, nothing to drive */
        lan_cfg_defaults(&lan.cfg);
        return;
    }
    spi_init();
    lan_select(NULL, false);
    uint8_t uid[12];
    for (unsigned i = 0; i < 3u; i++) {
        const uint32_t w = *(volatile uint32_t*)(UID_BASE + 4u * i);
        for (unsigned k = 0; k < 4u; k++) {
            uid[4u * i + k] = (uint8_t)(w >> (8u * k));
        }
    }
    w5500_mac_from_id(uid, sizeof(uid), lan.uid_mac);
    load_cfg();
    lan_start();
}

void ufi_lan_reload(void)
{
    lan.reload = lan.state != LAN_STATE_NO_HW;  /* applied by the next service, after the reply */
}

/* ============================================================================
 * MAIN LOOP
 * ============================================================================ */

void ufi_lan_service(void)
{
    if (lan.reload) {
        lan.reload = false;
        const lan_cfg_t old = lan.cfg;
        load_cfg();
        if (memcmp(&old, &lan.cfg, sizeof(old)) != 0) {
            lan_start();                /* any change: chip restarted with the new settings */
        }
    }
    if (lan.state < LAN_STATE_LINK_DOWN) {
        return;
    }
    const uint32_t now = HAL_GetTick();
    if (now - lan.phy_ms >= LAN_PHY_POLL_MS) {
        lan.phy_ms = now;
        lan.phy = w5500_phy(&dev);
        const bool up = (lan.phy & W5500_PHY_LNK) != 0;
        if (up && lan.net.state == NET_IDLE) {
            net_start(&lan.net, &dev, &lan.cfg, lan.mac, lan.host, lan.pkt, now);
        } else if (!up && lan.net.state != NET_IDLE) {
            net_stop(&lan.net, &dev);   /* cable out: address gone, DHCP again on the next link */
        }
        lan.state = up ? LAN_STATE_LINK_UP : LAN_STATE_LINK_DOWN;
    }
    if (lan.state < LAN_STATE_LINK_UP) {
        return;
    }
    const bool irq = HAL_GPIO_ReadPin(PIN_LAN_INT.port, PIN_LAN_INT.pin) == GPIO_PIN_RESET;
    if (irq || now - lan.net_ms >= LAN_NET_POLL_MS) {
        lan.net_ms = now;
        net_service(&lan.net, &dev, now, false);
    }
    if (net_has_address(&lan.net)) {
        lan.state = LAN_STATE_BOUND;
    }
}

const net_t* ufi_lan_net(void) { return &lan.net; }

uint8_t ufi_lan_state(void) { return lan.state; }
uint8_t ufi_lan_version(void) { return lan.version; }
uint8_t ufi_lan_phy(void) { return lan.phy; }
void ufi_lan_mac(uint8_t mac[6]) { memcpy(mac, lan.mac, 6u); }
const lan_cfg_t* ufi_lan_cfg(void) { return &lan.cfg; }
uint8_t ufi_lan_cfg_source(void) { return lan.cfg_source; }
const char* ufi_lan_hostname(void) { return lan.host; }
