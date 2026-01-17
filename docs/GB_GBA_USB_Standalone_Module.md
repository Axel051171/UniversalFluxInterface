# UFI GB/GBA Cartridge Reader - Standalone USB Module

## Overview

Standalone USB device for reading Game Boy and Game Boy Advance cartridges.
Can operate independently with any PC or connect to UFI mainboard.

```
┌─────────────────────────────────────────────────────────────────────┐
│                    GB/GBA CARTRIDGE READER                          │
│                      Standalone USB Module                          │
│                                                                     │
│   ┌─────────────────────────────────────────────────────────────┐   │
│   │                                                             │   │
│   │    ┌─────────────────┐      ┌─────────────────────────┐    │   │
│   │    │   GAME BOY      │      │   GAME BOY ADVANCE      │    │   │
│   │    │   CARTRIDGE     │      │   CARTRIDGE             │    │   │
│   │    │   SLOT          │      │   SLOT                  │    │   │
│   │    │   (DMG/GBC)     │      │   (GBA)                 │    │   │
│   │    └─────────────────┘      └─────────────────────────┘    │   │
│   │                                                             │   │
│   │    ┌───────────────────────────────────────────────────┐   │   │
│   │    │              RP2040 MICROCONTROLLER               │   │   │
│   │    │                                                   │   │   │
│   │    │  • Dual-Core ARM Cortex-M0+ @ 133 MHz            │   │   │
│   │    │  • Native USB 1.1 Full-Speed                      │   │   │
│   │    │  • 264 KB SRAM                                    │   │   │
│   │    │  • PIO for flexible I/O timing                    │   │   │
│   │    └───────────────────────────────────────────────────┘   │   │
│   │                                                             │   │
│   │    [USB-C]  [Status LED]  [GB/GBA Switch]  [UFI Header]    │   │
│   │                                                             │   │
│   └─────────────────────────────────────────────────────────────┘   │
│                                                                     │
│   Dimensions: 80mm x 50mm x 20mm (fits standard project box)       │
│   BOM Cost: ~€12                                                    │
│                                                                     │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Design Goals

1. **Standalone Operation** - Works with any PC via USB
2. **UFI Compatible** - Optional connection to UFI mainboard
3. **Enclosure Ready** - PCB fits standard Hammond 1551K case
4. **Low Cost** - Target BOM under €15
5. **Open Source** - Full hardware/software/firmware

---

## Hardware Architecture

### Block Diagram

```
                           ┌──────────────────────────────────────────┐
                           │         GB/GBA USB MODULE                │
                           │                                          │
    ┌──────────┐           │   ┌─────────────────────────────────┐   │
    │          │           │   │                                 │   │
    │   PC     │◄─── USB ──┼──►│         RP2040                  │   │
    │          │           │   │                                 │   │
    └──────────┘           │   │  ┌─────┐  ┌─────┐  ┌─────┐     │   │
                           │   │  │GPIO │  │GPIO │  │PIO  │     │   │
         OR                │   │  │0-15 │  │16-27│  │     │     │   │
                           │   │  └──┬──┘  └──┬──┘  └──┬──┘     │   │
    ┌──────────┐           │   └─────┼────────┼───────┼─────────┘   │
    │          │           │         │        │       │             │
    │   UFI    │◄── I2C ───┼─────────┼────────┼───────┘             │
    │Mainboard │   (opt)   │         │        │                     │
    │          │           │   ┌─────▼────────▼─────┐               │
    └──────────┘           │   │  74LVC4245 x2      │               │
                           │   │  Level Shifters    │               │
                           │   │  (3.3V ↔ 5V/3.3V)  │               │
                           │   └─────┬────────┬─────┘               │
                           │         │        │                     │
                           │   ┌─────▼──┐ ┌───▼────┐               │
                           │   │   GB   │ │  GBA   │               │
                           │   │  SLOT  │ │  SLOT  │               │
                           │   │  (5V)  │ │ (3.3V) │               │
                           │   └────────┘ └────────┘               │
                           │                                        │
                           └────────────────────────────────────────┘
```

### MCU Selection: RP2040

**Why RP2040 instead of STM32?**

| Feature | RP2040 | STM32F103 | STM32F4 |
|---------|--------|-----------|---------|
| Price | €0.70 | €2.50 | €4.00 |
| USB | Native FS | Native FS | Native HS |
| GPIO | 30 | 37 | 80+ |
| PIO | ✅ (flexible timing) | ❌ | ❌ |
| Availability | Excellent | Poor | Moderate |
| Dev Board | €4 (Pico) | €2 (BluePill) | €15 |

**PIO Advantage**: Programmable I/O can precisely emulate GB/GBA bus timing without CPU intervention.

---

## Pinout

### RP2040 Pin Assignment

```
┌────────────────────────────────────────────────────────────────────┐
│                        RP2040 PINOUT                               │
├────────────────────────────────────────────────────────────────────┤
│ Pin  │ Function     │ Direction │ Description                     │
├──────┼──────────────┼───────────┼─────────────────────────────────┤
│ GP0  │ D0           │ Bidir     │ Data bus bit 0                  │
│ GP1  │ D1           │ Bidir     │ Data bus bit 1                  │
│ GP2  │ D2           │ Bidir     │ Data bus bit 2                  │
│ GP3  │ D3           │ Bidir     │ Data bus bit 3                  │
│ GP4  │ D4           │ Bidir     │ Data bus bit 4                  │
│ GP5  │ D5           │ Bidir     │ Data bus bit 5                  │
│ GP6  │ D6           │ Bidir     │ Data bus bit 6                  │
│ GP7  │ D7           │ Bidir     │ Data bus bit 7                  │
├──────┼──────────────┼───────────┼─────────────────────────────────┤
│ GP8  │ A0           │ Output    │ Address bus bit 0               │
│ GP9  │ A1           │ Output    │ Address bus bit 1               │
│ GP10 │ A2           │ Output    │ Address bus bit 2               │
│ GP11 │ A3           │ Output    │ Address bus bit 3               │
│ GP12 │ A4           │ Output    │ Address bus bit 4               │
│ GP13 │ A5           │ Output    │ Address bus bit 5               │
│ GP14 │ A6           │ Output    │ Address bus bit 6               │
│ GP15 │ A7           │ Output    │ Address bus bit 7               │
├──────┼──────────────┼───────────┼─────────────────────────────────┤
│ GP16 │ A8           │ Output    │ Address bus bit 8               │
│ GP17 │ A9           │ Output    │ Address bus bit 9               │
│ GP18 │ A10          │ Output    │ Address bus bit 10              │
│ GP19 │ A11          │ Output    │ Address bus bit 11              │
│ GP20 │ A12          │ Output    │ Address bus bit 12              │
│ GP21 │ A13          │ Output    │ Address bus bit 13              │
│ GP22 │ A14          │ Output    │ Address bus bit 14              │
│ GP26 │ A15          │ Output    │ Address bus bit 15              │
├──────┼──────────────┼───────────┼─────────────────────────────────┤
│ GP23 │ ~RD          │ Output    │ Read strobe (directly active)   │
│ GP24 │ ~WR          │ Output    │ Write strobe                    │
│ GP25 │ ~CS          │ Output    │ Chip select                     │
│ GP27 │ ~RST         │ Output    │ Cartridge reset                 │
│ GP28 │ CLK          │ Output    │ PHI clock (directly active)     │
├──────┼──────────────┼───────────┼─────────────────────────────────┤
│ GP29 │ SLOT_SEL     │ Input     │ Slot select switch (0=GB, 1=GBA)│
└──────┴──────────────┴───────────┴─────────────────────────────────┘

USB: Native (GP not used)
I2C (optional UFI): I2C0 on dedicated pins
LED: WS2812B on remaining GPIO
```

### Connector Pinouts

#### USB Type-C (USB 2.0 only)

```
┌───────────────────────────────────────┐
│           USB-C (Top View)            │
├───────────────────────────────────────┤
│ A1  │ GND                             │
│ A4  │ VBUS (+5V)                      │
│ A5  │ CC1                             │
│ A6  │ D+                              │
│ A7  │ D-                              │
│ A8  │ SBU1 (NC)                       │
│ A9  │ VBUS (+5V)                      │
│ A12 │ GND                             │
│ B1  │ GND                             │
│ B4  │ VBUS (+5V)                      │
│ B5  │ CC2                             │
│ B6  │ D+                              │
│ B7  │ D-                              │
│ B8  │ SBU2 (NC)                       │
│ B9  │ VBUS (+5V)                      │
│ B12 │ GND                             │
└───────────────────────────────────────┘

Note: CC1/CC2 need 5.1kΩ pull-down for device mode
D+/D- directly to RP2040 USB pins
```

#### Game Boy Cartridge Slot (32-pin)

```
┌─────────────────────────────────────────────────────────────────┐
│                    GB CARTRIDGE SLOT                            │
│                  (Looking into slot)                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│    1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16│
│   ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══│
│   VCC PHI ~WR ~RD ~CS A0  A1  A2  A3  A4  A5  A6  A7  A8  A9 A10│
│                                                                 │
│   ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══│
│  A11 A12 A13 A14 A15  D0  D1  D2  D3  D4  D5  D6  D7 ~RST AUD GND│
│   17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32│
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

VCC = 5V (directly active)
PHI = 1MHz clock from RP2040
```

#### GBA Cartridge Slot (32-pin, directly active active active active active active active active active active active active)

```
┌─────────────────────────────────────────────────────────────────┐
│                    GBA CARTRIDGE SLOT                           │
│                  (Looking into slot)                            │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│    1   2   3   4   5   6   7   8   9  10  11  12  13  14  15  16│
│   ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══│
│   VCC PHI ~WR ~RD ~CS AD0 AD1 AD2 AD3 AD4 AD5 AD6 AD7 AD8 AD9AD10│
│                                                                 │
│   ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══ ═══│
│  AD11AD12AD13AD14AD15 A16 A17 A18 A19 A20 A21 A22 A23~RST~CS2 GND│
│   17  18  19  20  21  22  23  24  25  26  27  28  29  30  31  32│
│                                                                 │
└─────────────────────────────────────────────────────────────────┘

VCC = 3.3V (directly active)
AD0-15 = Multiplexed Address/Data
A16-23 = Upper address bits (directly active directly active directly active directly active directly active directly active directly active directly active directly active directly active directly active)
```

#### UFI Expansion Header (10-pin, optional)

```
┌─────────────────────────────────────┐
│    UFI EXPANSION (10-pin)           │
├─────────────────────────────────────┤
│  1 │ +3.3V                          │
│  2 │ +5V                            │
│  3 │ GND                            │
│  4 │ GND                            │
│  5 │ I2C_SDA                        │
│  6 │ I2C_SCL                        │
│  7 │ INT (directly active interrupt)                    │
│  8 │ ~RST                           │
│  9 │ GPIO0                          │
│ 10 │ GPIO1                          │
└─────────────────────────────────────┘

I2C Address: 0x42 (configurable)
INT: Active low when operation complete
```

---

## Schematic Blocks

### 1. RP2040 Core

```
                                    ┌─────────────────────────────────┐
                                    │          RP2040                 │
                                    │                                 │
    12MHz Crystal ─────────────────►│ XIN              GPIO0-7  ├────► D[0:7]
                                    │                  GPIO8-15 ├────► A[0:7]
    W25Q16 Flash ◄────── QSPI ─────►│ QSPI             GPIO16-22├────► A[8:15]
                                    │                  GPIO23-28├────► Control
    USB-C ◄──────────── USB ───────►│ USB              GPIO29   ├────► SLOT_SEL
                                    │                               │
    3.3V ──────────────────────────►│ DVDD, IOVDD                   │
    1.1V (internal LDO) ───────────►│ VREG                          │
    GND ───────────────────────────►│ GND                           │
                                    │                               │
    Boot Button ──────────────────►│ BOOTSEL                        │
    Reset Button ─────────────────►│ RUN                            │
                                    └─────────────────────────────────┘
```

### 2. Level Shifters

```
                        74LVC4245A (U1)                    74LVC4245A (U2)
                     Address Low + Control                  Address High + Data
                                    
    VCCA (3.3V) ──►┌─────────────────┐◄── VCCB (5V/3.3V)    VCCA ──►┌─────────────────┐◄── VCCB
                   │                 │                              │                 │
    A[0:7] ───────►│ A0-A7     B0-B7 │─────► GB/GBA A[0:7]         │ A0-A7     B0-B7 │─────► GB/GBA A[8:15]/D[0:7]
                   │                 │                              │                 │
    ~RD,~WR ──────►│ DIR       ~OE   │◄──── Enable          ~RD ───►│ DIR       ~OE   │◄──── Enable
                   │                 │                              │                 │
    GND ──────────►│ GND             │                      GND ───►│ GND             │
                   └─────────────────┘                              └─────────────────┘

    VCCB Switching:
    ┌──────────────────────────────────────────────────────────┐
    │                                                          │
    │   SLOT_SEL ──┬──► 0 (GB)  ──► VCCB = 5V (via MOSFET)    │
    │              │                                           │
    │              └──► 1 (GBA) ──► VCCB = 3.3V (direct)      │
    │                                                          │
    └──────────────────────────────────────────────────────────┘
```

### 3. Power Supply

```
    USB VBUS (5V) ──────┬─────────────────────────────────► +5V Rail (GB VCC)
                        │
                        │    ┌───────────────┐
                        └───►│  AP2112K-3.3  │───────────► +3.3V Rail
                             │  (LDO 600mA)  │              (RP2040, GBA VCC)
                             └───────────────┘
                                    │
                                   GND
```

---

## PCB Design

### Board Specifications

| Parameter | Value |
|-----------|-------|
| Dimensions | 80mm x 50mm |
| Layers | 2 |
| Thickness | 1.6mm |
| Copper | 1oz |
| Min Track | 0.2mm |
| Min Space | 0.2mm |
| Min Drill | 0.3mm |
| Surface Finish | HASL or ENIG |

### Enclosure

**Hammond 1551K** (or compatible):
- External: 80mm x 50mm x 20mm
- Internal: 76mm x 46mm x 17mm
- Material: ABS plastic
- Color: Black or translucent
- Price: ~€2.50

### Component Placement

```
┌────────────────────────────────────────────────────────────────────────────┐
│                              TOP VIEW                                       │
│                                                                             │
│   ┌─────────────────────────────────┐  ┌─────────────────────────────────┐ │
│   │                                 │  │                                 │ │
│   │      GB CARTRIDGE SLOT          │  │      GBA CARTRIDGE SLOT         │ │
│   │      (32-pin edge connector)    │  │      (32-pin edge connector)    │ │
│   │                                 │  │                                 │ │
│   └─────────────────────────────────┘  └─────────────────────────────────┘ │
│                                                                             │
│   ┌──────────────────────────────────────────────────────────────────────┐ │
│   │                                                                      │ │
│   │    [U1]        [U2]        ┌────────────┐        [U3]               │ │
│   │   74LVC4245   74LVC4245    │   RP2040   │       AP2112K             │ │
│   │                            │            │                            │ │
│   │    [C1-4]      [C5-8]      │  [Y1]      │       [C9-12]             │ │
│   │                            │  12MHz     │                            │ │
│   │                            └────────────┘                            │ │
│   │                                                                      │ │
│   │    [U4]                                           [SW1]              │ │
│   │   W25Q16                    [LED]                GB/GBA              │ │
│   │   Flash                     WS2812B              Switch              │ │
│   │                                                                      │ │
│   └──────────────────────────────────────────────────────────────────────┘ │
│                                                                             │
│   ┌────────┐  ┌────────┐  ┌──────────────────┐  ┌──────────────────────┐  │
│   │  USB-C │  │  BOOT  │  │   UFI HEADER     │  │    RESET BUTTON      │  │
│   │        │  │  BTN   │  │   (10-pin opt)   │  │                      │  │
│   └────────┘  └────────┘  └──────────────────┘  └──────────────────────┘  │
│                                                                             │
│   ════════════════════════════ EDGE ═══════════════════════════════════════│
└────────────────────────────────────────────────────────────────────────────┘
```

---

## Bill of Materials

| Ref | Description | Package | Qty | Unit Price | Total |
|-----|-------------|---------|-----|------------|-------|
| U1 | RP2040 | QFN-56 | 1 | €0.70 | €0.70 |
| U2 | W25Q16JVSSIQ (16Mbit Flash) | SOIC-8 | 1 | €0.25 | €0.25 |
| U3, U4 | 74LVC4245A (Octal Transceiver) | TSSOP-24 | 2 | €0.40 | €0.80 |
| U5 | AP2112K-3.3 (LDO) | SOT-23-5 | 1 | €0.15 | €0.15 |
| Y1 | 12MHz Crystal | 3215 | 1 | €0.10 | €0.10 |
| J1 | USB-C Receptacle | SMD | 1 | €0.30 | €0.30 |
| J2 | GB Cartridge Slot | Edge 32-pin | 1 | €1.50 | €1.50 |
| J3 | GBA Cartridge Slot | Edge 32-pin | 1 | €1.80 | €1.80 |
| J4 | 10-pin Header (UFI) | 2.54mm | 1 | €0.10 | €0.10 |
| SW1 | SPDT Slide Switch | SMD | 1 | €0.15 | €0.15 |
| SW2 | Tactile Button (BOOT) | SMD 6x6 | 1 | €0.05 | €0.05 |
| SW3 | Tactile Button (RESET) | SMD 6x6 | 1 | €0.05 | €0.05 |
| LED1 | WS2812B RGB LED | 5050 | 1 | €0.10 | €0.10 |
| Q1 | SI2301 P-MOSFET | SOT-23 | 1 | €0.05 | €0.05 |
| R1-R4 | 5.1kΩ (USB CC) | 0402 | 2 | €0.01 | €0.02 |
| R5-R6 | 27Ω (USB) | 0402 | 2 | €0.01 | €0.02 |
| R7-R10 | 10kΩ | 0402 | 4 | €0.01 | €0.04 |
| C1-C8 | 100nF | 0402 | 8 | €0.01 | €0.08 |
| C9-C10 | 1µF | 0402 | 2 | €0.02 | €0.04 |
| C11-C12 | 10µF | 0805 | 2 | €0.03 | €0.06 |
| C13-C14 | 15pF (Crystal) | 0402 | 2 | €0.01 | €0.02 |
| **PCB** | 2-layer, 80x50mm | - | 1 | €1.50 | €1.50 |
| **Enclosure** | Hammond 1551K | - | 1 | €2.50 | €2.50 |
| | | | | **TOTAL** | **~€12.38** |

---

## Firmware Architecture

### USB Protocol

```
Device Class: Vendor-specific (0xFF)
VID: 0x1209 (pid.codes)
PID: 0x4742 ("GB")

Endpoints:
  EP0: Control (commands)
  EP1 OUT: Bulk (write data to cartridge)
  EP1 IN: Bulk (read data from cartridge)

Commands (via EP0):
  0x01: GET_STATUS
  0x02: SET_MODE (GB/GBA)
  0x03: READ_ROM (address, length)
  0x04: WRITE_ROM (address, length)
  0x05: READ_RAM (address, length)
  0x06: WRITE_RAM (address, length)
  0x07: GET_HEADER
  0x08: DETECT_MBC
  0x09: SET_MBC_BANK
  0x0A: ERASE_FLASH
  0x0B: GET_FIRMWARE_VERSION
  0x0C: BOOTLOADER_MODE
```

### PIO Programs

```c
// GB Read Cycle (PIO)
.program gb_read
    set pins, 0b001    ; ~CS=0, ~RD=0, ~WR=1
    nop [7]            ; Wait for data valid (100ns @ 133MHz)
    in pins, 8         ; Read data bus
    set pins, 0b111    ; ~CS=1, ~RD=1, ~WR=1
    push               ; Send to FIFO

// GB Write Cycle (PIO)  
.program gb_write
    pull               ; Get data from FIFO
    out pins, 8        ; Set data bus
    set pins, 0b010    ; ~CS=0, ~RD=1, ~WR=0
    nop [7]            ; Hold time
    set pins, 0b111    ; ~CS=1, ~RD=1, ~WR=1
```

---

## Software Support

### Supported Formats

| Format | Extension | Description |
|--------|-----------|-------------|
| Raw ROM | .gb, .gbc, .gba | Unmodified ROM dump |
| Save | .sav | Battery-backed SRAM |
| RTC | .rtc | Real-time clock data |

### PC Software

- **CLI Tool**: `gbadump` (cross-platform, Rust)
- **GUI**: Web-based via WebUSB (Chrome)
- **Python Library**: `pygbadump`

### Example Usage

```bash
# Dump GB ROM
gbadump read rom game.gb

# Dump GB save
gbadump read save game.sav

# Write save back
gbadump write save game.sav

# Detect cartridge
gbadump info

# Flash homebrew
gbadump flash homebrew.gba
```

---

## Compatibility

### Game Boy (DMG/GBC)

| MBC Type | ROM Size | RAM Size | Status |
|----------|----------|----------|--------|
| ROM Only | 32KB | - | ✅ |
| MBC1 | 2MB | 32KB | ✅ |
| MBC2 | 256KB | 512B | ✅ |
| MBC3 | 2MB | 32KB+RTC | ✅ |
| MBC5 | 8MB | 128KB | ✅ |
| MBC6 | 1MB | 2KB | ⚠️ |
| MBC7 | 1MB | 256B | ⚠️ |
| HuC1 | 2MB | 32KB | ✅ |
| HuC3 | 2MB | 128KB | ✅ |
| MMM01 | 8MB | 32KB | ⚠️ |
| TAMA5 | 1MB | 32B | ❌ |
| Camera | 1MB | 128KB | ⚠️ |

### Game Boy Advance

| Save Type | Size | Status |
|-----------|------|--------|
| SRAM | 32KB | ✅ |
| EEPROM 512B | 512B | ✅ |
| EEPROM 8KB | 8KB | ✅ |
| Flash 64KB | 64KB | ✅ |
| Flash 128KB | 128KB | ✅ |

---

## Assembly Notes

1. **RP2040 Soldering**: Use hot air or reflow. QFN package requires careful alignment.
2. **Flash Programming**: Use SWD or UF2 bootloader via USB.
3. **Testing**: Check USB enumeration before installing in enclosure.
4. **Cartridge Slots**: Ensure proper alignment - test with sacrificial cartridge first.

---

## Revision History

| Version | Date | Changes |
|---------|------|---------|
| 1.0 | 2026-01-17 | Initial design |

---

## License

- Hardware: CERN-OHL-S-2.0
- Firmware: GPL-3.0
- Documentation: CC-BY-SA-4.0
