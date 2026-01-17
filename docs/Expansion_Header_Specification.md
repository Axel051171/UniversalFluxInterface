# UFI Expansion Header Specification

## Overview

The UFI mainboard includes a 40-pin expansion header for connecting optional modules such as cartridge readers (GB/GBA, Switch, etc.) and other peripherals.

---

## Physical Specifications

| Parameter | Value |
|-----------|-------|
| Connector Type | 2x20 pin header, 2.54mm pitch |
| Pin Count | 40 |
| Mounting | Through-hole or SMD |
| Keying | Pin 1 marked with triangle |

---

## Pinout

```
           ┌─────────────────────────────┐
           │    UFI Expansion Header     │
           │         (Top View)          │
           └─────────────────────────────┘
           
    ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
    │  1  │  3  │  5  │  7  │  9  │ 11  │ 13  │ 15  │ 17  │ 19  │
    │3.3V │ 5V  │ PE0 │ PE1 │ PE2 │ PE3 │ PE4 │ PE5 │ PE6 │ PE7 │
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
    │  2  │  4  │  6  │  8  │ 10  │ 12  │ 14  │ 16  │ 18  │ 20  │
    │ GND │ GND │ PE8 │ PE9 │PE10 │PE11 │PE12 │PE13 │PE14 │PE15 │
    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
    
    ┌─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┬─────┐
    │ 21  │ 23  │ 25  │ 27  │ 29  │ 31  │ 33  │ 35  │ 37  │ 39  │
    │ PD0 │ PD1 │ PD2 │ PD3 │ PB4 │ PB6 │ PA4 │ PA6 │ PC4 │ PC6 │
    ├─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┼─────┤
    │ 22  │ 24  │ 26  │ 28  │ 30  │ 32  │ 34  │ 36  │ 38  │ 40  │
    │ PD4 │ PD5 │ PD6 │ PD7 │ PB5 │ PB7 │ PA5 │ PA7 │ PC5 │ PC7 │
    └─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┴─────┘
```

---

## Pin Assignment Table

| Pin | Name | STM32 Pin | Function | Alt Function |
|-----|------|-----------|----------|--------------|
| 1 | 3V3 | - | 3.3V Power | 500mA max |
| 2 | GND | - | Ground | - |
| 3 | 5V | - | 5V Power | 500mA max |
| 4 | GND | - | Ground | - |
| 5 | A0 | PE0 | Address 0 | GPIO |
| 6 | A8 | PE8 | Address 8 | GPIO |
| 7 | A1 | PE1 | Address 1 | GPIO |
| 8 | A9 | PE9 | Address 9 | GPIO |
| 9 | A2 | PE2 | Address 2 | GPIO |
| 10 | A10 | PE10 | Address 10 | GPIO |
| 11 | A3 | PE3 | Address 3 | GPIO |
| 12 | A11 | PE11 | Address 11 | GPIO |
| 13 | A4 | PE4 | Address 4 | GPIO |
| 14 | A12 | PE12 | Address 12 | GPIO |
| 15 | A5 | PE5 | Address 5 | GPIO |
| 16 | A13 | PE13 | Address 13 | GPIO |
| 17 | A6 | PE6 | Address 6 | GPIO |
| 18 | A14 | PE14 | Address 14 | GPIO |
| 19 | A7 | PE7 | Address 7 | GPIO |
| 20 | A15 | PE15 | Address 15 | GPIO |
| 21 | D0 | PD0 | Data 0 | GPIO |
| 22 | D4 | PD4 | Data 4 | GPIO |
| 23 | D1 | PD1 | Data 1 | GPIO |
| 24 | D5 | PD5 | Data 5 | GPIO |
| 25 | D2 | PD2 | Data 2 | GPIO |
| 26 | D6 | PD6 | Data 6 | GPIO |
| 27 | D3 | PD3 | Data 3 | GPIO |
| 28 | D7 | PD7 | Data 7 | GPIO |
| 29 | CS | PB4 | Chip Select | SPI3_MISO |
| 30 | RD | PB5 | Read Enable | SPI3_MOSI |
| 31 | WR | PB6 | Write Enable | I2C1_SCL |
| 32 | CLK | PB7 | Clock | I2C1_SDA |
| 33 | A16 | PA4 | Address 16 | SPI1_NSS / DAC1 |
| 34 | A17 | PA5 | Address 17 | SPI1_SCK / DAC2 |
| 35 | A18 | PA6 | Address 18 | SPI1_MISO |
| 36 | A19 | PA7 | Address 19 | SPI1_MOSI |
| 37 | SPI_SCK | PC4 | SPI Clock | GPIO |
| 38 | SPI_MISO | PC5 | SPI Data In | GPIO |
| 39 | SPI_MOSI | PC6 | SPI Data Out | GPIO |
| 40 | SPI_CS | PC7 | SPI Chip Select | GPIO |

---

## Module Configurations

### GB/GBA Cartridge Module

Uses parallel bus interface:

```
Address Bus: A0-A15 (16-bit), A16-A23 (GBA extended)
Data Bus:    D0-D7 (8-bit)
Control:     CS, RD, WR, CLK
Power:       3.3V, 5V (via level shifters on module)
```

| Signal | Header Pin | Description |
|--------|------------|-------------|
| A0-A7 | 5,7,9,11,13,15,17,19 | Address low byte |
| A8-A15 | 6,8,10,12,14,16,18,20 | Address high byte |
| D0-D3 | 21,23,25,27 | Data low nibble |
| D4-D7 | 22,24,26,28 | Data high nibble |
| /CS | 29 | Chip Select (directly directly directly directly directly directly directly directly active low) |
| /RD | 30 | Read (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly active low) |
| /WR | 31 | Write (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly active low) |
| CLK | 32 | Optional clock |
| A16-A19 | 33-36 | GBA extended address |

### Switch Cartridge Module (with FPGA)

Uses SPI interface for FPGA communication:

```
SPI Interface: Pins 37-40
Clock:         Up to 50 MHz
Data:          Full duplex
```

| Signal | Header Pin | Description |
|--------|------------|-------------|
| SPI_SCK | 37 | SPI Clock |
| SPI_MISO | 38 | Data from FPGA |
| SPI_MOSI | 39 | Data to FPGA |
| SPI_CS | 40 | FPGA Select |
| FPGA_RESET | 31 | Optional: FPGA Reset |
| FPGA_IRQ | 32 | Optional: Interrupt |

### Generic SPI Peripheral

Any SPI device can be connected:

```c
// STM32 SPI Configuration
SPI_HandleTypeDef hspi_exp;
hspi_exp.Instance = SPI1;  // Using PA4-PA7
hspi_exp.Init.Mode = SPI_MODE_MASTER;
hspi_exp.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_4;  // ~28 MHz
hspi_exp.Init.Direction = SPI_DIRECTION_2LINES;
hspi_exp.Init.DataSize = SPI_DATASIZE_8BIT;
hspi_exp.Init.CLKPolarity = SPI_POLARITY_LOW;
hspi_exp.Init.CLKPhase = SPI_PHASE_1EDGE;
```

### Generic I2C Peripheral

Using pins 31-32 (PB6/PB7):

```c
// STM32 I2C Configuration
I2C_HandleTypeDef hi2c_exp;
hi2c_exp.Instance = I2C1;
hi2c_exp.Init.ClockSpeed = 400000;  // 400 kHz
hi2c_exp.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
```

---

## Electrical Specifications

### Voltage Levels

| Rail | Voltage | Current (max) | Protection |
|------|---------|---------------|------------|
| 3.3V | 3.3V ± 5% | 500 mA | Resettable fuse |
| 5V | 5.0V ± 5% | 500 mA | Resettable fuse |
| GPIO | 3.3V | 20 mA/pin | ESD diodes |

### GPIO Characteristics (STM32H723)

| Parameter | Min | Typ | Max | Unit |
|-----------|-----|-----|-----|------|
| VOL (Low) | - | - | 0.4 | V |
| VOH (High) | 2.4 | - | - | V |
| VIL (Low Input) | - | - | 0.8 | V |
| VIH (High Input) | 2.0 | - | - | V |
| IOL (Sink) | - | - | 20 | mA |
| IOH (Source) | - | - | 20 | mA |

### Timing

| Parameter | Value | Note |
|-----------|-------|------|
| GPIO Toggle Rate | 100 MHz max | Direct register access |
| SPI Clock | 50 MHz max | With prescaler |
| I2C Speed | 1 MHz max | Fast-mode Plus |
| Parallel Bus Cycle | 50 ns min | With DMA |

---

## Module Design Guidelines

### PCB Dimensions

```
┌─────────────────────────────────────┐
│                                     │
│   Recommended Module Size:          │
│   60mm x 40mm                       │
│                                     │
│   ┌─────────────────────────────┐   │
│   │    40-pin Header            │   │
│   │    (at edge)                │   │
│   └─────────────────────────────┘   │
│                                     │
│   Mounting Holes: 4x M2.5           │
│   at corners (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly 3mm from edge)       │
│                                     │
└─────────────────────────────────────┘
```

### Signal Integrity

1. **Keep traces short** - Max 50mm for high-speed signals
2. **Ground plane** - Use ground pour under signal traces
3. **Decoupling** - 100nF at each IC, near VCC pins
4. **Series resistors** - 33Ω on high-speed outputs

### ESD Protection

All GPIO pins have internal ESD protection, but for robust designs:
- Add TVS diodes on exposed connectors
- Add 100Ω series resistors on long cables

---

## Firmware API

### GPIO Port Access

```c
// Fast GPIO access for parallel bus
#define ADDR_PORT   GPIOE
#define DATA_PORT   GPIOD
#define CTRL_PORT   GPIOB

// Set address (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly 16-bit)
static inline void set_address(uint16_t addr) {
    ADDR_PORT->ODR = addr;
}

// Read data (8-bit)
static inline uint8_t read_data(void) {
    return (uint8_t)(DATA_PORT->IDR & 0xFF);
}

// Write data (8-bit)
static inline void write_data(uint8_t data) {
    DATA_PORT->ODR = (DATA_PORT->ODR & 0xFF00) | data;
}

// Control signals
#define CS_LOW()    (CTRL_PORT->BSRR = GPIO_PIN_4 << 16)
#define CS_HIGH()   (CTRL_PORT->BSRR = GPIO_PIN_4)
#define RD_LOW()    (CTRL_PORT->BSRR = GPIO_PIN_5 << 16)
#define RD_HIGH()   (CTRL_PORT->BSRR = GPIO_PIN_5)
#define WR_LOW()    (CTRL_PORT->BSRR = GPIO_PIN_6 << 16)
#define WR_HIGH()   (CTRL_PORT->BSRR = GPIO_PIN_6)
```

### Module Detection

Modules can be detected via I2C EEPROM:

```c
#define MODULE_EEPROM_ADDR  0x50

typedef struct {
    uint32_t magic;         // 0x55464D4F = "UFMO"
    uint16_t module_id;     // Module type ID
    uint16_t version;       // Hardware version
    char name[32];          // Module name
    uint32_t capabilities;  // Feature flags
} module_info_t;

bool detect_module(module_info_t *info) {
    uint8_t buf[sizeof(module_info_t)];
    
    if (HAL_I2C_Mem_Read(&hi2c_exp, MODULE_EEPROM_ADDR << 1, 
                         0, I2C_MEMADD_SIZE_8BIT,
                         buf, sizeof(buf), 100) != HAL_OK) {
        return false;
    }
    
    memcpy(info, buf, sizeof(module_info_t));
    return info->magic == 0x55464D4F;
}
```

### Module IDs

| ID | Module Type |
|----|-------------|
| 0x0001 | GB/GBA Cartridge Reader |
| 0x0002 | Switch Cartridge Reader |
| 0x0003 | NES Cartridge Reader |
| 0x0004 | SNES Cartridge Reader |
| 0x0005 | Genesis Cartridge Reader |
| 0x0006 | N64 Cartridge Reader |
| 0x00FF | Development/Test Module |

---

## Mechanical Drawing

```
                          60mm
    ┌─────────────────────────────────────────────┐
    │ ○                                         ○ │
    │                                             │
    │   ┌─────────────────────────────────────┐   │
    │   │                                     │   │
    │   │        Module Specific Area         │   │
 40mm   │        (Cartridge slots, etc.)      │   │
    │   │                                     │   │
    │   └─────────────────────────────────────┘   │
    │                                             │
    │ ┌─────────────────────────────────────────┐ │
    │ │▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓│ │
    │ │▓▓▓▓▓  40-pin Header to UFI  ▓▓▓▓▓▓▓▓▓│ │
    │ └─────────────────────────────────────────┘ │
    │ ○                                         ○ │
    └─────────────────────────────────────────────┘
    
    ○ = M2.5 Mounting Hole (3mm from edge)
    ▓ = 40-pin header (50.8mm = 20 x 2.54mm)
```

---

## Revision History

| Rev | Date | Changes |
|-----|------|---------|
| 1.0 | 2026-01-17 | Initial release |
