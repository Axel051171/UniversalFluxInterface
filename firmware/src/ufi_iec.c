/**
 * UFI Flux Engine - IEC Bus Module
 * 
 * Commodore IEC Serial Bus Protokoll für 1541/1571/1581 Laufwerke
 */

#include "ufi_firmware.h"
#include <stdio.h>
#include <string.h>

/* Board: each IEC line has a driver output (PD0-4 -> SN74LS07 open collector) and a
 * receiver input (PF4-8 <- 74LVC14A).  "pull" asserts the line via bus_out(), reads
 * use bus_in() (true = line low on the bus), see board.h. */

/* ============================================================================
 * IEC TIMING KONSTANTEN (in µs)
 * ============================================================================ */

#define IEC_T_AT    1000    // ATN Response (max 1000µs)
#define IEC_T_NE    40      // Non-EOI Response to RFD
#define IEC_T_S     70      // Bit Setup
#define IEC_T_V     20      // Data Valid
#define IEC_T_F     20      // Frame (after last bit)
#define IEC_T_R     20      // Frame to Release
#define IEC_T_BB    100     // Between Bytes
#define IEC_T_EI    200     // EOI Response
#define IEC_T_RY    60      // Talker Response Limit
#define IEC_T_PR    30      // Byte-Acknowledge

/* ============================================================================
 * DELAY FUNKTION
 * ============================================================================ */

static inline void iec_delay_us(uint32_t us) {
    uint32_t start = DWT->CYCCNT;
    uint32_t cycles = us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < cycles);
}

/* ============================================================================
 * GPIO FUNKTIONEN (Open-Drain Emulation)
 * ============================================================================ */

static inline void iec_release_clk(void)  { bus_out(&PIN_IEC_CLK_OUT, false); }
static inline void iec_pull_clk(void)     { bus_out(&PIN_IEC_CLK_OUT, true); }
static inline void iec_release_data(void) { bus_out(&PIN_IEC_DATA_OUT, false); }
static inline void iec_pull_data(void)    { bus_out(&PIN_IEC_DATA_OUT, true); }
static inline void iec_release_atn(void)  { bus_out(&PIN_IEC_ATN_OUT, false); }
static inline void iec_pull_atn(void)     { bus_out(&PIN_IEC_ATN_OUT, true); }

/* true = line held low by someone on the bus */
static inline bool iec_read_clk(void)     { return bus_in(&PIN_IEC_CLK_IN); }
static inline bool iec_read_data(void)    { return bus_in(&PIN_IEC_DATA_IN); }

/* ============================================================================
 * INITIALISIERUNG
 * ============================================================================ */

void ufi_iec_init(void) {
    /* GPIO modes are set by board_gpio_init(); release every line */
    bus_out(&PIN_IEC_ATN_OUT, false);
    bus_out(&PIN_IEC_CLK_OUT, false);
    bus_out(&PIN_IEC_DATA_OUT, false);
    bus_out(&PIN_IEC_SRQ_OUT, false);
    bus_out(&PIN_IEC_RESET_OUT, false);
}

/* ============================================================================
 * BUS RESET
 * ============================================================================ */

int ufi_iec_reset(void) {
    // Reset-Leitung Low für 20ms
    bus_out(&PIN_IEC_RESET_OUT, true);
    HAL_Delay(20);
    bus_out(&PIN_IEC_RESET_OUT, false);
    
    // Warten bis 1541 bereit (~500ms Boot-Zeit)
    HAL_Delay(500);
    
    // Bus releasen
    iec_release_clk();
    iec_release_data();
    iec_release_atn();
    
    return 0;
}

/* ============================================================================
 * ATN STEUERUNG
 * ============================================================================ */

int ufi_iec_atn(bool active) {
    if (active) {
        iec_pull_atn();
    } else {
        iec_release_atn();
    }
    iec_delay_us(100);
    return 0;
}

/* ============================================================================
 * BYTE SENDEN (als Controller)
 * ============================================================================ */

int ufi_iec_send_byte(uint8_t byte, bool eoi) {
    uint32_t timeout;
    
    // 1. CLK Low setzen (wir sind Talker)
    iec_pull_clk();
    iec_release_data();
    
    // 2. Warten bis Listener bereit (DATA Low)
    timeout = 1000;
    while (!iec_read_data() && timeout--) {
        iec_delay_us(10);
    }
    if (timeout == 0) {
        return -1;  // Kein Listener
    }
    
    // 3. EOI Handshake falls nötig
    if (eoi) {
        // EOI: CLK high lassen für >200µs
        iec_release_clk();
        iec_delay_us(IEC_T_EI);
        
        // Listener bestätigt EOI durch DATA High-Low-High
        timeout = 100;
        while (iec_read_data() && timeout--) {
            iec_delay_us(10);
        }
        timeout = 100;
        while (!iec_read_data() && timeout--) {
            iec_delay_us(10);
        }
    }
    
    // 4. CLK Release = "Ready to Send"
    iec_release_clk();
    iec_delay_us(IEC_T_NE);
    
    // 5. 8 Bits senden (LSB first)
    for (int i = 0; i < 8; i++) {
        // CLK Low
        iec_pull_clk();
        
        // Datenbit setzen (invertiert!)
        if (byte & (1 << i)) {
            iec_release_data();  // Bit = 1 -> DATA High
        } else {
            iec_pull_data();     // Bit = 0 -> DATA Low
        }
        
        iec_delay_us(IEC_T_S);
        
        // CLK High = Bit gültig
        iec_release_clk();
        iec_delay_us(IEC_T_V);
    }
    
    // 6. CLK Low, DATA Release
    iec_pull_clk();
    iec_release_data();
    iec_delay_us(IEC_T_F);
    
    // 7. Auf Acknowledge warten (DATA Low vom Listener)
    timeout = 1000;
    while (!iec_read_data() && timeout--) {
        iec_delay_us(10);
    }
    
    iec_delay_us(IEC_T_BB);
    
    return 0;
}

/* ============================================================================
 * BYTE EMPFANGEN (als Controller/Listener)
 * ============================================================================ */

int ufi_iec_receive_byte(uint8_t* byte, bool* eoi) {
    uint32_t timeout;
    *byte = 0;
    *eoi = false;
    
    // 1. DATA Release = "Ready to Receive"
    iec_release_data();
    
    // 2. Warten auf CLK Release vom Talker
    timeout = 1000;
    while (iec_read_clk() && timeout--) {
        iec_delay_us(10);
    }
    if (timeout == 0) {
        return -1;  // Timeout
    }
    
    // 3. EOI Check: Wenn CLK länger als 200µs High bleibt
    iec_delay_us(IEC_T_EI);
    if (!iec_read_clk()) {
        *eoi = true;
        // EOI Acknowledge: DATA Low-High
        iec_pull_data();
        iec_delay_us(60);
        iec_release_data();
    }
    
    // 4. 8 Bits empfangen (LSB first)
    for (int i = 0; i < 8; i++) {
        // Warten auf CLK Low (Bit-Start)
        timeout = 200;
        while (!iec_read_clk() && timeout--) {
            iec_delay_us(5);
        }
        
        // Warten auf CLK High (Bit gültig)
        timeout = 200;
        while (iec_read_clk() && timeout--) {
            iec_delay_us(5);
        }
        
        // Bit lesen (invertiert!)
        if (!iec_read_data()) {
            *byte |= (1 << i);
        }
    }
    
    // 5. Acknowledge senden (DATA Low)
    iec_pull_data();
    iec_delay_us(IEC_T_PR);
    
    return 0;
}

/* ============================================================================
 * LISTEN/TALK BEFEHLE
 * ============================================================================ */

int ufi_iec_listen(uint8_t device) {
    ufi_iec_atn(true);
    iec_delay_us(100);
    
    // LISTEN Befehl: 0x20 + Device
    int ret = ufi_iec_send_byte(0x20 | (device & 0x1F), false);
    
    return ret;
}

int ufi_iec_talk(uint8_t device) {
    ufi_iec_atn(true);
    iec_delay_us(100);
    
    // TALK Befehl: 0x40 + Device
    int ret = ufi_iec_send_byte(0x40 | (device & 0x1F), false);
    
    return ret;
}

int ufi_iec_secondary(uint8_t channel) {
    // Secondary Address: 0x60 + Channel
    return ufi_iec_send_byte(0x60 | (channel & 0x0F), false);
}

int ufi_iec_unlisten(void) {
    ufi_iec_atn(true);
    int ret = ufi_iec_send_byte(0x3F, false);
    ufi_iec_atn(false);
    return ret;
}

int ufi_iec_untalk(void) {
    ufi_iec_atn(true);
    int ret = ufi_iec_send_byte(0x5F, false);
    ufi_iec_atn(false);
    return ret;
}

/* ============================================================================
 * HIGH-LEVEL FUNKTIONEN
 * ============================================================================ */

// Befehl an Laufwerk senden (z.B. "I0" für Initialize)
int ufi_iec_command(uint8_t device, const char* cmd, uint8_t len) {
    int ret;
    
    // LISTEN Device, Secondary 15 (Command Channel)
    ret = ufi_iec_listen(device);
    if (ret != 0) return ret;
    
    ret = ufi_iec_secondary(15);
    if (ret != 0) return ret;
    
    ufi_iec_atn(false);
    
    // Befehl senden
    for (uint8_t i = 0; i < len; i++) {
        bool eoi = (i == len - 1);
        ret = ufi_iec_send_byte(cmd[i], eoi);
        if (ret != 0) return ret;
    }
    
    // UNLISTEN
    ret = ufi_iec_unlisten();
    
    return ret;
}

// Status vom Laufwerk lesen
int ufi_iec_read_status(uint8_t device, char* buffer, uint8_t max_len) {
    int ret;
    uint8_t idx = 0;
    
    // TALK Device, Secondary 15 (Status Channel)
    ret = ufi_iec_talk(device);
    if (ret != 0) return ret;
    
    ret = ufi_iec_secondary(15);
    if (ret != 0) return ret;
    
    ufi_iec_atn(false);
    
    // Turnaround: Wir werden Listener
    iec_release_clk();
    iec_delay_us(100);
    
    // Bytes lesen bis EOI
    bool eoi = false;
    while (!eoi && idx < max_len - 1) {
        uint8_t byte;
        ret = ufi_iec_receive_byte(&byte, &eoi);
        if (ret != 0) break;
        
        buffer[idx++] = byte;
    }
    buffer[idx] = '\0';
    
    // UNTALK
    ret = ufi_iec_untalk();
    
    return idx;
}

// Block lesen (z.B. für Raw Sector Access)
int ufi_iec_read_block(uint8_t device, uint8_t track, uint8_t sector,
                       uint8_t* buffer, uint16_t* len) {
    char cmd[20];
    int ret;
    
    // U1 Befehl: "U1:channel drive track sector"
    // Kanal 2 für Datenübertragung
    snprintf(cmd, sizeof(cmd), "U1:2 0 %d %d", track, sector);
    
    // Buffer-Pointer setzen
    ret = ufi_iec_command(device, "B-P:2 0", 7);
    if (ret != 0) return ret;
    
    // Block lesen Befehl
    ret = ufi_iec_command(device, cmd, strlen(cmd));
    if (ret != 0) return ret;
    
    // TALK Device, Kanal 2
    ret = ufi_iec_talk(device);
    if (ret != 0) return ret;
    
    ret = ufi_iec_secondary(2);
    if (ret != 0) return ret;
    
    ufi_iec_atn(false);
    iec_release_clk();
    iec_delay_us(100);
    
    // 256 Bytes lesen
    *len = 0;
    for (int i = 0; i < 256; i++) {
        bool eoi;
        ret = ufi_iec_receive_byte(&buffer[i], &eoi);
        if (ret != 0) break;
        (*len)++;
        if (eoi) break;
    }
    
    ret = ufi_iec_untalk();
    
    return (*len == 256) ? 0 : -1;
}

// Directory lesen
int ufi_iec_read_directory(uint8_t device, uint8_t* buffer, uint16_t max_len,
                           uint16_t* len) {
    int ret;
    
    // "$" öffnen
    ret = ufi_iec_listen(device);
    if (ret != 0) return ret;
    
    // Open mit Secondary 0
    ret = ufi_iec_secondary(0xF0);  // OPEN Kanal 0
    if (ret != 0) return ret;
    
    ufi_iec_atn(false);
    
    // "$" senden
    ret = ufi_iec_send_byte('$', true);
    if (ret != 0) return ret;
    
    ret = ufi_iec_unlisten();
    if (ret != 0) return ret;
    
    // TALK Kanal 0
    ret = ufi_iec_talk(device);
    if (ret != 0) return ret;
    
    ret = ufi_iec_secondary(0x60);
    if (ret != 0) return ret;
    
    ufi_iec_atn(false);
    iec_release_clk();
    iec_delay_us(100);
    
    // Directory-Daten lesen
    *len = 0;
    bool eoi = false;
    while (!eoi && *len < max_len) {
        ret = ufi_iec_receive_byte(&buffer[*len], &eoi);
        if (ret != 0) break;
        (*len)++;
    }
    
    ret = ufi_iec_untalk();
    
    // Close Kanal 0
    ret = ufi_iec_listen(device);
    ret = ufi_iec_secondary(0xE0);  // CLOSE Kanal 0
    ufi_iec_atn(false);
    ret = ufi_iec_unlisten();
    
    return 0;
}
