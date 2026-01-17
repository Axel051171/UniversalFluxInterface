# UFI Expansion Module: Game Boy / GBA Cartridge Reader

## Overview

This expansion module adds Game Boy (DMG/CGB) and Game Boy Advance (AGB) cartridge reading/writing capabilities to the UFI platform. Unlike Switch cartridges, these use simple parallel ROM interfaces that can be directly controlled by the STM32H723 without requiring an FPGA.

---

## Supported Cartridges

| System | Cartridge | ROM Size | RAM/SRAM | Mapper Support |
|--------|-----------|----------|----------|----------------|
| Game Boy (DMG) | 32-pin | 32KB - 8MB | 0-128KB | MBC1/2/3/5/6/7 |
| Game Boy Color | 32-pin | 32KB - 8MB | 0-128KB | MBC1/2/3/5/6/7 |
| Game Boy Advance | 32-pin | 1MB - 32MB | 0-128KB | None (linear) |

---

## Hardware Architecture

```
                    UFI Mainboard                    GB/GBA Module
                   ┌─────────────┐                  ┌─────────────────────┐
                   │             │                  │                     │
                   │  STM32H723  │                  │  ┌───────────────┐  │
                   │             │                  │  │  GB Cartridge │  │
                   │    PE0-15 ──┼──────────────────┼──│     Slot      │  │
                   │   (Address) │  40-pin Header   │  │   (32-pin)    │  │
                   │             │                  │  └───────┬───────┘  │
                   │    PD0-7  ──┼──────────────────┼──────────┤          │
                   │    (Data)   │                  │          │          │
                   │             │                  │  ┌───────┴───────┐  │
                   │    PB4-7  ──┼──────────────────┼──│  GBA Cartridge│  │
                   │  (Control)  │                  │  │     Slot      │  │
                   │             │                  │  │   (32-pin)    │  │
                   │    3.3V   ──┼──────────────────┼──│               │  │
                   │    5V     ──┼──────────────────┼──└───────────────┘  │
                   │    GND    ──┼──────────────────┼──                   │
                   │             │                  │  ┌───────────────┐  │
                   └─────────────┘                  │  │   74LVC245    │  │
                                                   │  │ Level Shifter │  │
                                                   │  │  (3.3V ↔ 5V)  │  │
                                                   │  └───────────────┘  │
                                                   └─────────────────────┘
```

---

## Game Boy Cartridge Pinout (32-pin)

```
                    ┌──────────────────────┐
                    │    Game Boy Cart     │
                    │      (Top View)      │
                    └──────────────────────┘
        
        Pin 1  ○ VCC (5V)          GND ○ Pin 32
        Pin 2  ○ CLK               /WR ○ Pin 31  
        Pin 3  ○ /WR               /RD ○ Pin 30
        Pin 4  ○ /RD               /CS ○ Pin 29
        Pin 5  ○ /CS              AUD  ○ Pin 28
        Pin 6  ○ A0                A15 ○ Pin 27
        Pin 7  ○ A1                A14 ○ Pin 26
        Pin 8  ○ A2                A13 ○ Pin 25
        Pin 9  ○ A3                A12 ○ Pin 24
        Pin 10 ○ A4                A11 ○ Pin 23
        Pin 11 ○ A5                A10 ○ Pin 22
        Pin 12 ○ A6                A9  ○ Pin 21
        Pin 13 ○ A7                A8  ○ Pin 20
        Pin 14 ○ D0                D7  ○ Pin 19
        Pin 15 ○ D1                D6  ○ Pin 18
        Pin 16 ○ D2                D5  ○ Pin 17
                    └──────────────────────┘
                           D3  D4
```

### Signal Description

| Pin | Name | Direction | Description |
|-----|------|-----------|-------------|
| 1 | VCC | Power | +5V Supply |
| 2 | CLK | Input | Clock (directly directly directly directly directly directly directly directly directly directly directly directly directly directly 1 MHz, directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly optional) |
| 3 | /WR | Input | Write Enable (active low) |
| 4 | /RD | Input | Read Enable (active low) |
| 5 | /CS | Input | Chip Select (directly directly directly directly directly directly directly directly directly directly directly active low) |
| 6-13 | A0-A7 | Input | Address Bus (low byte) |
| 14-16 | D0-D2 | Bidir | Data Bus |
| 17-19 | D5-D7 | Bidir | Data Bus |
| 20-27 | A8-A15 | Input | Address Bus (high byte) |
| 28 | AUD | Output | Audio Output (directly directly directly directly directly directly directly directly directly from cart) |
| 29 | /CS | Input | Chip Select (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly RAM) |
| 30 | /RD | Input | Read Enable |
| 31 | /WR | Input | Write Enable |
| 32 | GND | Power | Ground |

---

## Game Boy Advance Cartridge Pinout (32-pin)

```
                    ┌──────────────────────┐
                    │      GBA Cart        │
                    │      (Top View)      │
                    └──────────────────────┘

     Pin 1  ○ VDD (3.3V)              GND ○ Pin 32
     Pin 2  ○ PHI (Clock)            /WR ○ Pin 31
     Pin 3  ○ /WR                    /RD ○ Pin 30
     Pin 4  ○ /RD                   /CS2 ○ Pin 29
     Pin 5  ○ /CS (ROM)              IRQ ○ Pin 28
     Pin 6  ○ AD0                   AD15 ○ Pin 27
     Pin 7  ○ AD1                   AD14 ○ Pin 26
     Pin 8  ○ AD2                   AD13 ○ Pin 25
     Pin 9  ○ AD3                   AD12 ○ Pin 24
     Pin 10 ○ AD4                   AD11 ○ Pin 23
     Pin 11 ○ AD5                   AD10 ○ Pin 22
     Pin 12 ○ AD6                    AD9 ○ Pin 21
     Pin 13 ○ AD7                    AD8 ○ Pin 20
     Pin 14 ○ A16                    A23 ○ Pin 19
     Pin 15 ○ A17                    A22 ○ Pin 18
     Pin 16 ○ A18                    A21 ○ Pin 17
                    └──────────────────────┘
                          A19  A20
```

### Key Differences from GB

| Feature | Game Boy | GBA |
|---------|----------|-----|
| Voltage | 5V | 3.3V |
| Address | 16-bit (A0-A15) | 24-bit (AD0-15 + A16-23) |
| Data | 8-bit (D0-D7) | 16-bit (AD0-AD15 multiplexed) |
| Bus | Separate A/D | Multiplexed AD |

---

## Schematic

### Main Circuit

```
                                    UFI Expansion Header (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly 40-pin)
                                    ┌─────────────────────────────┐
                                    │ 1  3.3V        GND  2      │
                                    │ 3  5V          GND  4      │
                                    │ 5  PE0/A0      PE8/A8   6  │
                                    │ 7  PE1/A1      PE9/A9   8  │
                                    │ 9  PE2/A2      PE10/A10 10 │
                                    │ 11 PE3/A3      PE11/A11 12 │
                                    │ 13 PE4/A4      PE12/A12 14 │
                                    │ 15 PE5/A5      PE13/A13 16 │
                                    │ 17 PE6/A6      PE14/A14 18 │
                                    │ 19 PE7/A7      PE15/A15 20 │
                                    │ 21 PD0/D0      PD4/D4   22 │
                                    │ 23 PD1/D1      PD5/D5   24 │
                                    │ 25 PD2/D2      PD6/D6   26 │
                                    │ 27 PD3/D3      PD7/D7   28 │
                                    │ 29 PB4/CS      PB5/RD   30 │
                                    │ 31 PB6/WR      PB7/CLK  32 │
                                    │ 33 PA4/A16     PA5/A17  34 │
                                    │ 35 PA6/A18     PA7/A19  36 │
                                    │ 37 PC4/A20     PC5/A21  38 │
                                    │ 39 PC6/A22     PC7/A23  40 │
                                    └─────────────────────────────┘


    ┌─────────────────────────────────────────────────────────────────────────┐
    │                        GB/GBA Cartridge Module                           │
    │                                                                          │
    │   3.3V ─────┬──────────────────────────────────────────────────┐        │
    │             │                                                   │        │
    │            ─┴─ C1                                               │        │
    │            ───  100nF                                           │        │
    │             │                                                   │        │
    │            GND                                                  │        │
    │                                                                 │        │
    │   5V ───────┬──────────────────────────────────────────────┐   │        │
    │             │                                               │   │        │
    │            ─┴─ C2                                           │   │        │
    │            ───  100nF                                       │   │        │
    │             │                                               │   │        │
    │            GND                                              │   │        │
    │                                                             │   │        │
    │                                                             │   │        │
    │   ┌─────────────────────────────────────────────────────┐  │   │        │
    │   │                    74LVC245 (U1)                     │  │   │        │
    │   │                    (Address Low)                     │  │   │        │
    │   │                                                      │  │   │        │
    │   │  VCCA ─── 3.3V            VCCB ─── 5V               │  │   │        │
    │   │  DIR ──── HIGH (A→B)      OE ──── Active            │  │   │        │
    │   │                                                      │  │   │        │
    │   │  A1 ◄─── PE0              B1 ───► GB_A0             │  │   │        │
    │   │  A2 ◄─── PE1              B2 ───► GB_A1             │  │   │        │
    │   │  A3 ◄─── PE2              B3 ───► GB_A2             │  │   │        │
    │   │  A4 ◄─── PE3              B4 ───► GB_A3             │  │   │        │
    │   │  A5 ◄─── PE4              B5 ───► GB_A4             │  │   │        │
    │   │  A6 ◄─── PE5              B6 ───► GB_A5             │  │   │        │
    │   │  A7 ◄─── PE6              B7 ───► GB_A6             │  │   │        │
    │   │  A8 ◄─── PE7              B8 ───► GB_A7             │  │   │        │
    │   └─────────────────────────────────────────────────────┘  │   │        │
    │                                                             │   │        │
    │   ┌─────────────────────────────────────────────────────┐  │   │        │
    │   │                    74LVC245 (U2)                     │  │   │        │
    │   │                    (Address High)                    │  │   │        │
    │   │                                                      │  │   │        │
    │   │  A1 ◄─── PE8              B1 ───► GB_A8             │  │   │        │
    │   │  A2 ◄─── PE9              B2 ───► GB_A9             │  │   │        │
    │   │  A3 ◄─── PE10             B3 ───► GB_A10            │  │   │        │
    │   │  A4 ◄─── PE11             B4 ───► GB_A11            │  │   │        │
    │   │  A5 ◄─── PE12             B5 ───► GB_A12            │  │   │        │
    │   │  A6 ◄─── PE13             B6 ───► GB_A13            │  │   │        │
    │   │  A7 ◄─── PE14             B7 ───► GB_A14            │  │   │        │
    │   │  A8 ◄─── PE15             B8 ───► GB_A15            │  │   │        │
    │   └─────────────────────────────────────────────────────┘  │   │        │
    │                                                             │   │        │
    │   ┌─────────────────────────────────────────────────────┐  │   │        │
    │   │                    74LVC245 (U3)                     │  │   │        │
    │   │                    (Data Bus - Directly directly directly directly directly Bidirectional)           │  │   │        │
    │   │                                                      │  │   │        │
    │   │  DIR ◄─── PB4 (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly READ=Low, WRITE=High)          │  │   │        │
    │   │                                                      │  │   │        │
    │   │  A1 ◄──► PD0              B1 ◄──► GB_D0             │  │   │        │
    │   │  A2 ◄──► PD1              B2 ◄──► GB_D1             │  │   │        │
    │   │  A3 ◄──► PD2              B3 ◄──► GB_D2             │  │   │        │
    │   │  A4 ◄──► PD3              B4 ◄──► GB_D3             │  │   │        │
    │   │  A5 ◄──► PD4              B5 ◄──► GB_D4             │  │   │        │
    │   │  A6 ◄──► PD5              B6 ◄──► GB_D5             │  │   │        │
    │   │  A7 ◄──► PD6              B7 ◄──► GB_D6             │  │   │        │
    │   │  A8 ◄──► PD7              B8 ◄──► GB_D7             │  │   │        │
    │   └─────────────────────────────────────────────────────┘  │   │        │
    │                                                             │   │        │
    │   Control Signals:                                          │   │        │
    │                                                             │   │        │
    │   PB5 (RD) ────[74LVC1G04]──── GB_/RD                       │   │        │
    │   PB6 (WR) ────[74LVC1G04]──── GB_/WR                       │   │        │
    │   PB7 (CS) ────[74LVC1G04]──── GB_/CS                       │   │        │
    │                                                             │   │        │
    │                                                             │   │        │
    │   ┌─────────────────┐       ┌─────────────────┐            │   │        │
    │   │  GB Cartridge   │       │  GBA Cartridge  │            │   │        │
    │   │     Slot        │       │      Slot       │            │   │        │
    │   │    (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly 32-pin)     │       │     (directly directly directly directly directly directly directly directly directly directly directly 32-pin)      │            │   │        │
    │   │                 │       │                 │            │   │        │
    │   │  VCC=5V         │       │  VDD=3.3V       │            │   │        │
    │   │                 │       │                 │            │   │        │
    │   └────────┬────────┘       └────────┬────────┘            │   │        │
    │            │                         │                     │   │        │
    │            └────────┬────────────────┘                     │   │        │
    │                     │                                      │   │        │
    │            [Directly directly directly Directly directly directly directly Directly Directly Directly Directly Directly Directly Directly Directly Directly Directly Directly Directly Directly Slot Select Switch / Directly Directly Directly GPIO]              │   │        │
    │                                                             │   │        │
    └─────────────────────────────────────────────────────────────┘   │        │
                                                                      │        │
                                                                      │        │
```

---

## Bill of Materials

| Ref | Component | Value | Package | Qty | Price |
|-----|-----------|-------|---------|-----|-------|
| U1-U3 | 74LVC245APW | - | TSSOP-20 | 3 | €0.30 |
| U4-U6 | 74LVC1G04 | Inverter | SOT-23-5 | 3 | €0.10 |
| J1 | Expansion Header | 2x20 | 2.54mm | 1 | €0.50 |
| J2 | GB Cartridge Slot | 32-pin | - | 1 | €2.00 |
| J3 | GBA Cartridge Slot | 32-pin | - | 1 | €2.00 |
| C1-C4 | Capacitor | 100nF | 0402 | 4 | €0.02 |
| R1-R3 | Resistor | 10kΩ | 0402 | 3 | €0.01 |
| SW1 | Slide Switch | SPDT | - | 1 | €0.20 |

**Total: ~€6.50** (excluding PCB)

---

## Firmware Interface

### Memory Map

```c
// GB/GBA Cartridge Memory Regions
#define GB_ROM_BASE     0x0000      // 0x0000 - 0x7FFF (32KB window)
#define GB_VRAM_BASE    0x8000      // Not directly accessible
#define GB_SRAM_BASE    0xA000      // 0xA000 - 0xBFFF (8KB window)
#define GB_ROM_BANK     0x4000      // Banked ROM (0x4000 - 0x7FFF)

#define GBA_ROM_BASE    0x08000000  // 0 - 32MB
#define GBA_SRAM_BASE   0x0E000000  // Save RAM
#define GBA_FLASH_BASE  0x0E000000  // Flash save (same address)
#define GBA_EEPROM_BASE 0x0D000000  // EEPROM save
```

### Low-Level API

```c
/**
 * GB/GBA Cartridge Reader API
 */

#include <stdint.h>
#include <stdbool.h>

// Cartridge types
typedef enum {
    CART_TYPE_NONE = 0,
    CART_TYPE_GB,       // Original Game Boy
    CART_TYPE_GBC,      // Game Boy Color
    CART_TYPE_GBA,      // Game Boy Advance
} cart_type_t;

// MBC (Memory Bank Controller) types
typedef enum {
    MBC_NONE = 0,       // 32KB ROM only
    MBC_MBC1,           // Max 2MB ROM, 32KB RAM
    MBC_MBC2,           // Max 256KB ROM, 512x4 bits RAM
    MBC_MBC3,           // Max 2MB ROM, 32KB RAM, RTC
    MBC_MBC5,           // Max 8MB ROM, 128KB RAM
    MBC_MBC6,           // Max 8MB ROM, 128KB RAM, Flash
    MBC_MBC7,           // Max 8MB ROM, Accelerometer
    MBC_MMM01,          // Multi-cart
    MBC_CAMERA,         // Game Boy Camera
    MBC_TAMA5,          // Tamagotchi
    MBC_HUC1,           // Hudson
    MBC_HUC3,           // Hudson with RTC
} mbc_type_t;

// Cartridge info structure
typedef struct {
    cart_type_t type;
    mbc_type_t mbc;
    char title[17];         // Game title (null-terminated)
    uint8_t cgb_flag;       // 0x80 = CGB compatible, 0xC0 = CGB only
    uint8_t sgb_flag;       // 0x03 = SGB compatible
    uint8_t rom_size;       // ROM size code
    uint8_t ram_size;       // RAM size code
    uint32_t rom_bytes;     // Actual ROM size in bytes
    uint32_t ram_bytes;     // Actual RAM size in bytes
    uint8_t header_checksum;
    uint16_t global_checksum;
    bool has_battery;       // Battery-backed SRAM
    bool has_rtc;           // Real-Time Clock
    bool has_rumble;        // Rumble motor
} cart_info_t;


// ============================================================================
// Hardware Control
// ============================================================================

/**
 * Initialize the cartridge interface
 * @return true if successful
 */
bool cart_init(void);

/**
 * Detect inserted cartridge type
 * @return Cartridge type (NONE if no cart)
 */
cart_type_t cart_detect(void);

/**
 * Read cartridge header and populate info structure
 * @param info Pointer to info structure to fill
 * @return true if valid cartridge detected
 */
bool cart_read_header(cart_info_t *info);

/**
 * Set voltage (3.3V for GBA, 5V for GB/GBC)
 * @param voltage_3v3 true for 3.3V, false for 5V
 */
void cart_set_voltage(bool voltage_3v3);


// ============================================================================
// Low-Level Bus Operations
// ============================================================================

/**
 * Read a single byte from cartridge
 * @param address 16-bit address (GB) or 24-bit address (GBA)
 * @return Data byte
 */
uint8_t cart_read_byte(uint32_t address);

/**
 * Write a single byte to cartridge
 * @param address 16-bit address
 * @param data Data byte to write
 */
void cart_write_byte(uint32_t address, uint8_t data);

/**
 * Read multiple bytes (DMA-accelerated)
 * @param address Start address
 * @param buffer Destination buffer
 * @param length Number of bytes to read
 */
void cart_read_block(uint32_t address, uint8_t *buffer, uint32_t length);

/**
 * Write multiple bytes
 * @param address Start address
 * @param buffer Source buffer
 * @param length Number of bytes to write
 */
void cart_write_block(uint32_t address, const uint8_t *buffer, uint32_t length);


// ============================================================================
// Game Boy MBC Operations
// ============================================================================

/**
 * Select ROM bank (MBC1/2/3/5)
 * @param bank Bank number (1-511 depending on MBC)
 */
void gb_select_rom_bank(uint16_t bank);

/**
 * Select RAM bank (MBC1/3/5)
 * @param bank Bank number (0-15)
 */
void gb_select_ram_bank(uint8_t bank);

/**
 * Enable/disable RAM access
 * @param enable true to enable
 */
void gb_enable_ram(bool enable);

/**
 * Read RTC registers (MBC3)
 * @param rtc Pointer to 5-byte array [Seconds, Minutes, Hours, Days_Lo, Days_Hi_Flags]
 */
void gb_read_rtc(uint8_t *rtc);


// ============================================================================
// High-Level Operations
// ============================================================================

/**
 * Dump entire ROM to buffer
 * @param buffer Destination buffer (must be large enough)
 * @param info Cartridge info (for size calculation)
 * @param progress_cb Progress callback (NULL to disable)
 * @return Number of bytes read
 */
uint32_t cart_dump_rom(uint8_t *buffer, const cart_info_t *info,
                       void (*progress_cb)(uint32_t current, uint32_t total));

/**
 * Dump save RAM to buffer
 * @param buffer Destination buffer
 * @param info Cartridge info
 * @return Number of bytes read
 */
uint32_t cart_dump_sram(uint8_t *buffer, const cart_info_t *info);

/**
 * Write save RAM from buffer
 * @param buffer Source buffer
 * @param info Cartridge info
 * @return Number of bytes written
 */
uint32_t cart_write_sram(const uint8_t *buffer, const cart_info_t *info);

/**
 * Verify ROM contents against buffer
 * @param buffer Reference buffer
 * @param info Cartridge info
 * @return true if match
 */
bool cart_verify_rom(const uint8_t *buffer, const cart_info_t *info);


// ============================================================================
// GBA-Specific Operations
// ============================================================================

/**
 * Read GBA ROM (24-bit addressing, 16-bit data)
 * @param address 24-bit address
 * @return 16-bit data
 */
uint16_t gba_read_rom(uint32_t address);

/**
 * Detect GBA save type
 * @return Save type (SRAM, Flash, EEPROM)
 */
typedef enum {
    GBA_SAVE_NONE,
    GBA_SAVE_SRAM_32K,
    GBA_SAVE_FLASH_64K,
    GBA_SAVE_FLASH_128K,
    GBA_SAVE_EEPROM_512,
    GBA_SAVE_EEPROM_8K,
} gba_save_type_t;

gba_save_type_t gba_detect_save_type(void);

/**
 * Read GBA Flash save
 * @param buffer Destination buffer
 * @param size Flash size
 * @return Bytes read
 */
uint32_t gba_read_flash(uint8_t *buffer, uint32_t size);

/**
 * Write GBA Flash save
 * @param buffer Source buffer
 * @param size Flash size
 * @return Bytes written
 */
uint32_t gba_write_flash(const uint8_t *buffer, uint32_t size);
```

### Example Usage

```c
// Example: Dump a Game Boy cartridge

#include "cart_reader.h"
#include <stdio.h>

void progress(uint32_t current, uint32_t total) {
    printf("\rDumping: %lu / %lu bytes (%lu%%)", 
           current, total, (current * 100) / total);
}

int main(void) {
    cart_info_t info;
    
    // Initialize
    if (!cart_init()) {
        printf("Failed to initialize cartridge reader\n");
        return -1;
    }
    
    // Detect cartridge
    cart_type_t type = cart_detect();
    if (type == CART_TYPE_NONE) {
        printf("No cartridge detected\n");
        return -1;
    }
    
    // Read header
    if (!cart_read_header(&info)) {
        printf("Failed to read cartridge header\n");
        return -1;
    }
    
    printf("Game: %s\n", info.title);
    printf("Type: %s\n", type == CART_TYPE_GBA ? "GBA" : "GB/GBC");
    printf("ROM: %lu bytes\n", info.rom_bytes);
    printf("RAM: %lu bytes\n", info.ram_bytes);
    printf("MBC: %d\n", info.mbc);
    
    // Allocate buffer
    uint8_t *rom_buffer = malloc(info.rom_bytes);
    if (!rom_buffer) {
        printf("Out of memory\n");
        return -1;
    }
    
    // Dump ROM
    printf("Dumping ROM...\n");
    uint32_t bytes = cart_dump_rom(rom_buffer, &info, progress);
    printf("\nDumped %lu bytes\n", bytes);
    
    // Save to file
    FILE *f = fopen("game.gb", "wb");
    fwrite(rom_buffer, 1, bytes, f);
    fclose(f);
    
    // Dump save if present
    if (info.ram_bytes > 0 && info.has_battery) {
        uint8_t *sram_buffer = malloc(info.ram_bytes);
        printf("Dumping save RAM...\n");
        cart_dump_sram(sram_buffer, &info);
        
        FILE *sf = fopen("game.sav", "wb");
        fwrite(sram_buffer, 1, info.ram_bytes, sf);
        fclose(sf);
        free(sram_buffer);
    }
    
    free(rom_buffer);
    printf("Done!\n");
    
    return 0;
}
```

---

## Timing Specifications

### Game Boy Read Cycle

```
         ┌──────────────────────────────────────┐
Address  │  VALID ADDRESS                       │
         └──────────────────────────────────────┘
         
            ┌─────────────────────────────────┐
   /CS   ───┘                                 └───
         
              ┌───────────────────────────────┐
   /RD   ─────┘                               └───
         
                            ┌────────────────┐
  Data   ───────────────────│  VALID DATA    │───
                            └────────────────┘
         
         ├──────┤
         tAS=0ns (Address Setup)
         
                ├───────────┤
                tACC=150ns (Access Time)
         
         ├──────────────────────────────────┤
                     tCYC=250ns
```

### Timing Parameters

| Parameter | Symbol | Min | Typ | Max | Unit |
|-----------|--------|-----|-----|-----|------|
| Read Cycle | tCYC | 250 | - | - | ns |
| Address Setup | tAS | 0 | - | - | ns |
| Access Time | tACC | - | 100 | 150 | ns |
| Data Hold | tDH | 10 | - | - | ns |
| Write Pulse | tWP | 60 | - | - | ns |

At **4 MHz** (250ns cycle), we can read ~4 MB/s. A 32 MB GBA ROM takes ~8 seconds.

---

## PCB Design Notes

1. **Dimensions:** 60mm x 40mm (fits in small enclosure)
2. **Layers:** 2-layer sufficient
3. **Mounting:** 4x M2.5 mounting holes
4. **Connectors:**
   - 40-pin header to UFI mainboard
   - 32-pin edge connector (GB)
   - 32-pin edge connector (GBA)

### Layout Considerations

- Keep data bus traces short and matched length
- Add ground plane under cartridge slots
- Place decoupling caps close to 74LVC245 VCC pins
- Use 0.1" pitch for easy assembly

---

## Software Integration

### USB Commands (Extension to UFI Protocol)

| Code | Name | Description |
|------|------|-------------|
| 0x80 | CMD_CART_DETECT | Detect cartridge type |
| 0x81 | CMD_CART_INFO | Read cartridge info |
| 0x82 | CMD_CART_READ_ROM | Dump ROM |
| 0x83 | CMD_CART_READ_SRAM | Dump save |
| 0x84 | CMD_CART_WRITE_SRAM | Write save |
| 0x85 | CMD_CART_VERIFY | Verify ROM |

### CLI Usage

```bash
# Detect cartridge
ufi cart detect

# Show info
ufi cart info

# Dump ROM
ufi cart dump-rom game.gb

# Dump save
ufi cart dump-save game.sav

# Restore save
ufi cart write-save game.sav

# Full backup
ufi cart backup --rom game.gb --save game.sav
```

---

## References

- [GB Cartridge Pinout](https://gbdev.io/pandocs/The_Cartridge_Header.html)
- [GBA Cartridge Pinout](https://problemkaputt.de/gbatek.htm#gbacartridgeheader)
- [MBC Implementations](https://gbdev.io/pandocs/MBCs.html)
- [GBxCart RW](https://www.insidegadgets.com/projects/gbxcart-rw/)
- [Retrode](https://www.retrode.com/)
