/**
 * GB/GBA USB Dumper - RP2040 Firmware
 * 
 * Eigenständiges USB-Gerät zum Dumpen von Game Boy und Game Boy Advance Cartridges
 * 
 * Hardware: RP2040 + Level-Shifter + GB/GBA Cartridge Slots
 * USB: CDC für Kommandos, Bulk für Daten
 */

#include <stdio.h>
#include <string.h>
#include "pico/stdlib.h"
#include "pico/binary_info.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/clocks.h"
#include "tusb.h"

/* ============================================================================
 * PIN DEFINITIONEN
 * ============================================================================ */

// GB Cartridge Pins (active accent accent accent accent LOW accent accent accent accent)
#define PIN_GB_A0       0   // Address Bus A0-A15
#define PIN_GB_A15      15
#define PIN_GB_D0       16  // Data Bus D0-D7
#define PIN_GB_D7       23
#define PIN_GB_RD       24  // /RD - Read
#define PIN_GB_WR       25  // /WR - Write
#define PIN_GB_CS       26  // /CS - ROM Chip Select
#define PIN_GB_RESET    27  // /RESET
#define PIN_GB_CLK      28  // Clock (optional)

// GBA zusätzliche Pins (24-bit Adresse, 16-bit Daten)
#define PIN_GBA_A16     0   // GBA verwendet A0-A23
#define PIN_GBA_A23     7
#define PIN_GBA_D8      8   // GBA hat 16-bit Daten
#define PIN_GBA_D15     15
#define PIN_GBA_CS2     29  // /CS2 für SRAM

// Modus-Auswahl
#define PIN_MODE_GB     26  // Active = GB Mode
#define PIN_MODE_GBA    27  // Active = GBA Mode

// Status LED
#define PIN_LED_STATUS  25

/* ============================================================================
 * CARTRIDGE TYPEN
 * ============================================================================ */

typedef enum {
    CART_NONE = 0,
    CART_GB,        // Original Game Boy (32KB ROM)
    CART_GBC,       // Game Boy Color (bis 8MB ROM)
    CART_GBA        // Game Boy Advance (bis 32MB ROM)
} cart_type_t;

typedef enum {
    MBC_NONE = 0,   // No MBC (32KB)
    MBC_MBC1,       // MBC1 (bis 2MB ROM, 32KB RAM)
    MBC_MBC2,       // MBC2 (256KB ROM, 512x4bit RAM)
    MBC_MBC3,       // MBC3 (bis 2MB ROM, RTC)
    MBC_MBC5,       // MBC5 (bis 8MB ROM, 128KB RAM)
    MBC_MBC6,       // MBC6 (selten)
    MBC_MBC7,       // MBC7 (Accelerometer)
    MBC_MMM01,      // MMM01 (Multi-Cart)
    MBC_HUC1,       // Hudson HuC1
    MBC_HUC3,       // Hudson HuC3
    MBC_CAMERA,     // Pocket Camera
    MBC_TAMA5       // Bandai TAMA5
} mbc_type_t;

typedef struct {
    cart_type_t type;
    mbc_type_t mbc;
    char title[17];
    uint32_t rom_size;      // Bytes
    uint32_t ram_size;      // Bytes
    uint8_t cgb_flag;       // GBC Support
    uint8_t sgb_flag;       // SGB Support
    uint16_t checksum;
    uint8_t header_checksum;
} cart_info_t;

static cart_info_t g_cart_info;

/* ============================================================================
 * USB KOMMANDOS
 * ============================================================================ */

typedef enum {
    CMD_GET_INFO        = 0x01, // Cartridge Info lesen
    CMD_READ_ROM        = 0x10, // ROM lesen
    CMD_READ_RAM        = 0x11, // RAM/SRAM lesen
    CMD_WRITE_RAM       = 0x12, // RAM/SRAM schreiben
    CMD_ERASE_FLASH     = 0x20, // Flash löschen (für Repro)
    CMD_WRITE_FLASH     = 0x21, // Flash schreiben
    CMD_SET_MODE        = 0x30, // GB/GBA Modus setzen
    CMD_RESET           = 0xF0, // Reset
    CMD_BOOTLOADER      = 0xFF  // In Bootloader
} usb_command_t;

/* ============================================================================
 * LOW-LEVEL BUS FUNKTIONEN
 * ============================================================================ */

static inline void bus_set_address(uint32_t addr) {
    // A0-A15 auf GPIO 0-15
    gpio_put_masked(0xFFFF, addr & 0xFFFF);
    
    // GBA: A16-A23 auf GPIO (je nach Verdrahtung)
    if (g_cart_info.type == CART_GBA) {
        // Zusätzliche Adressleitungen für GBA
        gpio_put_masked(0xFF << 16, (addr >> 16) << 16);
    }
}

static inline void bus_set_data_output(void) {
    // D0-D7 als Output
    for (int i = PIN_GB_D0; i <= PIN_GB_D7; i++) {
        gpio_set_dir(i, GPIO_OUT);
    }
}

static inline void bus_set_data_input(void) {
    // D0-D7 als Input
    for (int i = PIN_GB_D0; i <= PIN_GB_D7; i++) {
        gpio_set_dir(i, GPIO_IN);
    }
}

static inline uint8_t bus_read_data(void) {
    uint32_t all = gpio_get_all();
    return (all >> PIN_GB_D0) & 0xFF;
}

static inline void bus_write_data(uint8_t data) {
    gpio_put_masked(0xFF << PIN_GB_D0, (uint32_t)data << PIN_GB_D0);
}

/* ============================================================================
 * ROM LESEN
 * ============================================================================ */

static uint8_t gb_read_byte(uint32_t addr) {
    bus_set_data_input();
    bus_set_address(addr);
    
    gpio_put(PIN_GB_CS, 0);     // CS aktiv
    gpio_put(PIN_GB_RD, 0);     // RD aktiv
    
    sleep_us(1);                // Warten auf Daten
    
    uint8_t data = bus_read_data();
    
    gpio_put(PIN_GB_RD, 1);     // RD inaktiv
    gpio_put(PIN_GB_CS, 1);     // CS inaktiv
    
    return data;
}

static void gb_write_byte(uint32_t addr, uint8_t data) {
    bus_set_data_output();
    bus_set_address(addr);
    bus_write_data(data);
    
    gpio_put(PIN_GB_CS, 0);     // CS aktiv
    gpio_put(PIN_GB_WR, 0);     // WR aktiv
    
    sleep_us(1);
    
    gpio_put(PIN_GB_WR, 1);     // WR inaktiv
    gpio_put(PIN_GB_CS, 1);     // CS inaktiv
    
    bus_set_data_input();
}

/* ============================================================================
 * MBC BANK SWITCHING
 * ============================================================================ */

static void mbc_select_rom_bank(uint16_t bank) {
    switch (g_cart_info.mbc) {
        case MBC_NONE:
            // Kein Bank-Switching
            break;
            
        case MBC_MBC1:
            // MBC1: Bank in 0x2000-0x3FFF
            gb_write_byte(0x2000, bank & 0x1F);
            if (bank > 0x1F) {
                gb_write_byte(0x4000, (bank >> 5) & 0x03);
            }
            break;
            
        case MBC_MBC2:
            // MBC2: Bank in 0x2100
            gb_write_byte(0x2100, bank & 0x0F);
            break;
            
        case MBC_MBC3:
            // MBC3: Bank in 0x2000-0x3FFF
            gb_write_byte(0x2000, bank & 0x7F);
            break;
            
        case MBC_MBC5:
            // MBC5: Bank Low in 0x2000, High in 0x3000
            gb_write_byte(0x2000, bank & 0xFF);
            gb_write_byte(0x3000, (bank >> 8) & 0x01);
            break;
            
        default:
            // Fallback
            gb_write_byte(0x2000, bank & 0xFF);
            break;
    }
}

static void mbc_select_ram_bank(uint8_t bank) {
    switch (g_cart_info.mbc) {
        case MBC_MBC1:
        case MBC_MBC3:
        case MBC_MBC5:
            gb_write_byte(0x4000, bank & 0x0F);
            break;
        default:
            break;
    }
}

static void mbc_enable_ram(bool enable) {
    // RAM Enable: 0x0A in 0x0000-0x1FFF
    gb_write_byte(0x0000, enable ? 0x0A : 0x00);
}

/* ============================================================================
 * HEADER LESEN & PARSING
 * ============================================================================ */

static void read_gb_header(void) {
    uint8_t header[0x50];
    
    // Header bei 0x0100-0x014F
    for (int i = 0; i < 0x50; i++) {
        header[i] = gb_read_byte(0x0100 + i);
    }
    
    // Titel (0x0134-0x0143)
    memcpy(g_cart_info.title, &header[0x34], 16);
    g_cart_info.title[16] = '\0';
    
    // CGB Flag (0x0143)
    g_cart_info.cgb_flag = header[0x43];
    
    // SGB Flag (0x0146)
    g_cart_info.sgb_flag = header[0x46];
    
    // Cartridge Type -> MBC (0x0147)
    uint8_t cart_type = header[0x47];
    switch (cart_type) {
        case 0x00: g_cart_info.mbc = MBC_NONE; break;
        case 0x01:
        case 0x02:
        case 0x03: g_cart_info.mbc = MBC_MBC1; break;
        case 0x05:
        case 0x06: g_cart_info.mbc = MBC_MBC2; break;
        case 0x0F:
        case 0x10:
        case 0x11:
        case 0x12:
        case 0x13: g_cart_info.mbc = MBC_MBC3; break;
        case 0x19:
        case 0x1A:
        case 0x1B:
        case 0x1C:
        case 0x1D:
        case 0x1E: g_cart_info.mbc = MBC_MBC5; break;
        case 0xFC: g_cart_info.mbc = MBC_CAMERA; break;
        case 0xFD: g_cart_info.mbc = MBC_TAMA5; break;
        case 0xFE: g_cart_info.mbc = MBC_HUC3; break;
        case 0xFF: g_cart_info.mbc = MBC_HUC1; break;
        default:   g_cart_info.mbc = MBC_NONE; break;
    }
    
    // ROM Size (0x0148)
    uint8_t rom_code = header[0x48];
    g_cart_info.rom_size = 32768 << rom_code;  // 32KB * 2^code
    
    // RAM Size (0x0149)
    uint8_t ram_code = header[0x49];
    switch (ram_code) {
        case 0x00: g_cart_info.ram_size = 0; break;
        case 0x01: g_cart_info.ram_size = 2048; break;    // 2KB (unused)
        case 0x02: g_cart_info.ram_size = 8192; break;    // 8KB
        case 0x03: g_cart_info.ram_size = 32768; break;   // 32KB
        case 0x04: g_cart_info.ram_size = 131072; break;  // 128KB
        case 0x05: g_cart_info.ram_size = 65536; break;   // 64KB
        default:   g_cart_info.ram_size = 0; break;
    }
    
    // Checksums
    g_cart_info.header_checksum = header[0x4D];
    g_cart_info.checksum = (header[0x4E] << 8) | header[0x4F];
    
    // Typ bestimmen
    if (g_cart_info.cgb_flag == 0xC0) {
        g_cart_info.type = CART_GBC;  // GBC Only
    } else if (g_cart_info.cgb_flag == 0x80) {
        g_cart_info.type = CART_GBC;  // GBC Enhanced
    } else {
        g_cart_info.type = CART_GB;   // Original GB
    }
}

/* ============================================================================
 * ROM DUMP
 * ============================================================================ */

static void dump_rom(uint8_t* buffer, uint32_t start_addr, uint32_t length) {
    uint32_t addr = start_addr;
    uint32_t remaining = length;
    
    while (remaining > 0) {
        // Bank berechnen
        uint16_t bank = addr / 0x4000;
        uint16_t offset = addr % 0x4000;
        
        // Bank 0 ist immer bei 0x0000-0x3FFF
        // Bank 1+ bei 0x4000-0x7FFF (mit Bank-Switching)
        
        if (bank > 0) {
            mbc_select_rom_bank(bank);
            offset += 0x4000;  // Bank 1+ startet bei 0x4000
        }
        
        // Lesen
        uint32_t chunk = (remaining > 0x4000) ? 0x4000 : remaining;
        if (offset + chunk > 0x8000) {
            chunk = 0x8000 - offset;
        }
        
        for (uint32_t i = 0; i < chunk; i++) {
            buffer[addr - start_addr + i] = gb_read_byte(offset + i);
        }
        
        addr += chunk;
        remaining -= chunk;
    }
}

/* ============================================================================
 * RAM DUMP / WRITE
 * ============================================================================ */

static void dump_ram(uint8_t* buffer, uint32_t length) {
    if (g_cart_info.ram_size == 0) return;
    
    mbc_enable_ram(true);
    
    uint32_t addr = 0;
    while (addr < length && addr < g_cart_info.ram_size) {
        uint8_t bank = addr / 0x2000;
        uint16_t offset = addr % 0x2000;
        
        mbc_select_ram_bank(bank);
        
        uint32_t chunk = ((length - addr) > 0x2000) ? 0x2000 : (length - addr);
        
        for (uint32_t i = 0; i < chunk; i++) {
            buffer[addr + i] = gb_read_byte(0xA000 + offset + i);
        }
        
        addr += chunk;
    }
    
    mbc_enable_ram(false);
}

static void write_ram(const uint8_t* buffer, uint32_t length) {
    if (g_cart_info.ram_size == 0) return;
    
    mbc_enable_ram(true);
    
    uint32_t addr = 0;
    while (addr < length && addr < g_cart_info.ram_size) {
        uint8_t bank = addr / 0x2000;
        uint16_t offset = addr % 0x2000;
        
        mbc_select_ram_bank(bank);
        
        uint32_t chunk = ((length - addr) > 0x2000) ? 0x2000 : (length - addr);
        
        for (uint32_t i = 0; i < chunk; i++) {
            gb_write_byte(0xA000 + offset + i, buffer[addr + i]);
        }
        
        addr += chunk;
    }
    
    mbc_enable_ram(false);
}

/* ============================================================================
 * USB INTERFACE
 * ============================================================================ */

#define USB_BUF_SIZE 4096
static uint8_t usb_rx_buf[USB_BUF_SIZE];
static uint8_t usb_tx_buf[USB_BUF_SIZE];

void tud_cdc_rx_cb(uint8_t itf) {
    (void)itf;
    
    uint32_t count = tud_cdc_read(usb_rx_buf, sizeof(usb_rx_buf));
    if (count == 0) return;
    
    uint8_t cmd = usb_rx_buf[0];
    
    switch (cmd) {
        case CMD_GET_INFO: {
            // Cartridge Info zurückgeben
            read_gb_header();
            
            usb_tx_buf[0] = CMD_GET_INFO;
            usb_tx_buf[1] = 0;  // Status OK
            usb_tx_buf[2] = g_cart_info.type;
            usb_tx_buf[3] = g_cart_info.mbc;
            memcpy(&usb_tx_buf[4], g_cart_info.title, 16);
            usb_tx_buf[20] = (g_cart_info.rom_size >> 0) & 0xFF;
            usb_tx_buf[21] = (g_cart_info.rom_size >> 8) & 0xFF;
            usb_tx_buf[22] = (g_cart_info.rom_size >> 16) & 0xFF;
            usb_tx_buf[23] = (g_cart_info.rom_size >> 24) & 0xFF;
            usb_tx_buf[24] = (g_cart_info.ram_size >> 0) & 0xFF;
            usb_tx_buf[25] = (g_cart_info.ram_size >> 8) & 0xFF;
            usb_tx_buf[26] = (g_cart_info.ram_size >> 16) & 0xFF;
            usb_tx_buf[27] = (g_cart_info.ram_size >> 24) & 0xFF;
            
            tud_cdc_write(usb_tx_buf, 28);
            tud_cdc_write_flush();
            break;
        }
        
        case CMD_READ_ROM: {
            uint32_t addr = usb_rx_buf[1] | (usb_rx_buf[2] << 8) |
                           (usb_rx_buf[3] << 16) | (usb_rx_buf[4] << 24);
            uint32_t len = usb_rx_buf[5] | (usb_rx_buf[6] << 8);
            
            if (len > USB_BUF_SIZE - 4) len = USB_BUF_SIZE - 4;
            
            dump_rom(&usb_tx_buf[4], addr, len);
            
            usb_tx_buf[0] = CMD_READ_ROM;
            usb_tx_buf[1] = 0;
            usb_tx_buf[2] = len & 0xFF;
            usb_tx_buf[3] = (len >> 8) & 0xFF;
            
            tud_cdc_write(usb_tx_buf, 4 + len);
            tud_cdc_write_flush();
            break;
        }
        
        case CMD_READ_RAM: {
            uint32_t len = g_cart_info.ram_size;
            if (len > USB_BUF_SIZE - 4) len = USB_BUF_SIZE - 4;
            
            dump_ram(&usb_tx_buf[4], len);
            
            usb_tx_buf[0] = CMD_READ_RAM;
            usb_tx_buf[1] = 0;
            usb_tx_buf[2] = len & 0xFF;
            usb_tx_buf[3] = (len >> 8) & 0xFF;
            
            tud_cdc_write(usb_tx_buf, 4 + len);
            tud_cdc_write_flush();
            break;
        }
        
        case CMD_WRITE_RAM: {
            uint32_t len = usb_rx_buf[1] | (usb_rx_buf[2] << 8);
            write_ram(&usb_rx_buf[3], len);
            
            usb_tx_buf[0] = CMD_WRITE_RAM;
            usb_tx_buf[1] = 0;
            tud_cdc_write(usb_tx_buf, 2);
            tud_cdc_write_flush();
            break;
        }
        
        case CMD_RESET:
            // Software Reset
            watchdog_reboot(0, 0, 0);
            break;
            
        case CMD_BOOTLOADER:
            // In BOOTSEL Mode
            reset_usb_boot(0, 0);
            break;
    }
}

/* ============================================================================
 * GPIO INITIALISIERUNG
 * ============================================================================ */

static void gpio_init_all(void) {
    // Address Bus A0-A15 als Output
    for (int i = PIN_GB_A0; i <= PIN_GB_A15; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_OUT);
        gpio_put(i, 0);
    }
    
    // Data Bus D0-D7 als Input (default)
    for (int i = PIN_GB_D0; i <= PIN_GB_D7; i++) {
        gpio_init(i);
        gpio_set_dir(i, GPIO_IN);
        gpio_pull_up(i);
    }
    
    // Control Signale
    gpio_init(PIN_GB_RD);
    gpio_set_dir(PIN_GB_RD, GPIO_OUT);
    gpio_put(PIN_GB_RD, 1);  // Inaktiv
    
    gpio_init(PIN_GB_WR);
    gpio_set_dir(PIN_GB_WR, GPIO_OUT);
    gpio_put(PIN_GB_WR, 1);  // Inaktiv
    
    gpio_init(PIN_GB_CS);
    gpio_set_dir(PIN_GB_CS, GPIO_OUT);
    gpio_put(PIN_GB_CS, 1);  // Inaktiv
    
    gpio_init(PIN_GB_RESET);
    gpio_set_dir(PIN_GB_RESET, GPIO_OUT);
    gpio_put(PIN_GB_RESET, 1);  // Nicht Reset
    
    // Status LED
    gpio_init(PIN_LED_STATUS);
    gpio_set_dir(PIN_LED_STATUS, GPIO_OUT);
}

/* ============================================================================
 * MAIN
 * ============================================================================ */

int main(void) {
    // System Init
    stdio_init_all();
    
    // GPIO Init
    gpio_init_all();
    
    // USB Init
    tusb_init();
    
    // Cartridge Info initialisieren
    memset(&g_cart_info, 0, sizeof(g_cart_info));
    
    // LED blinken zum Start
    for (int i = 0; i < 3; i++) {
        gpio_put(PIN_LED_STATUS, 1);
        sleep_ms(100);
        gpio_put(PIN_LED_STATUS, 0);
        sleep_ms(100);
    }
    
    // Main Loop
    while (1) {
        tud_task();
        
        // LED Toggle wenn Aktivität
        static uint32_t last_toggle = 0;
        if (time_us_32() - last_toggle > 500000) {
            gpio_put(PIN_LED_STATUS, !gpio_get(PIN_LED_STATUS));
            last_toggle = time_us_32();
        }
    }
    
    return 0;
}
