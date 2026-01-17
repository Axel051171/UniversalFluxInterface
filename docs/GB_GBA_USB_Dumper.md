# UFI GB/GBA USB Dumper

## Standalone USB Game Boy / Game Boy Advance Cartridge Reader

A compact, USB-powered device for dumping Game Boy and Game Boy Advance cartridges.
Designed for use as a standalone device or as a UFI expansion module.

---

## Design Goals

```
┌─────────────────────────────────────────────────────────────────┐
│                    GB/GBA USB DUMPER                             │
│                                                                  │
│   • Standalone USB device (like MIG Dumper)                      │
│   • Compact form factor for desktop enclosure                    │
│   • Dual slot: Game Boy (DMG/GBC) + Game Boy Advance            │
│   • USB-C connection for power and data                          │
│   • Optional: UFI expansion header for direct integration        │
│   • Open source hardware and software                            │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Hardware Architecture

### Block Diagram

```
                           ┌─────────────────────────────────────┐
                           │       GB/GBA USB DUMPER              │
                           │                                      │
    ┌──────────┐           │   ┌────────────────────────────┐    │
    │          │  USB-C    │   │      RP2040                │    │
    │   PC     │◄─────────►│   │   (Dual-Core ARM M0+)      │    │
    │          │           │   │                            │    │
    └──────────┘           │   │   • 133 MHz               │    │
                           │   │   • 264 KB SRAM           │    │
                           │   │   • Native USB 1.1        │    │
                           │   │   • PIO for fast I/O      │    │
                           │   │                            │    │
                           │   └─────────┬──────────────────┘    │
                           │             │                        │
                           │   ┌─────────▼──────────────────┐    │
                           │   │     Level Shifters         │    │
                           │   │     (74LVC245 x3)          │    │
                           │   │                            │    │
                           │   │   3.3V ◄──► 5V (GB)       │    │
                           │   │   3.3V ◄──► 3.3V (GBA)    │    │
                           │   └─────────┬──────────────────┘    │
                           │             │                        │
                           │   ┌─────────▼────┐  ┌──────────┐    │
                           │   │  GB Slot     │  │ GBA Slot │    │
                           │   │  (32-pin)    │  │ (32-pin) │    │
                           │   │   5V Logic   │  │ 3.3V     │    │
                           │   └──────────────┘  └──────────┘    │
                           │                                      │
                           │   ┌────────────────────────────┐    │
                           │   │  Status LEDs + Button      │    │
                           │   │  • Power (green)           │    │
                           │   │  • Activity (blue)         │    │
                           │   │  • Slot Select Button      │    │
                           │   └────────────────────────────┘    │
                           │                                      │
                           └─────────────────────────────────────┘
```

### Why RP2040?

| Feature | RP2040 | STM32F103 | CH552 |
|---------|--------|-----------|-------|
| Price | €0.70 | €2.50 | €0.30 |
| USB | Native 1.1 | Native 2.0 FS | Native 1.1 |
| Speed | 133 MHz | 72 MHz | 24 MHz |
| GPIO | 30 | 37 | 18 |
| PIO | ✅ (8 SM) | ❌ | ❌ |
| Flash | External | 64-128KB | 16KB |
| Dev Tools | Excellent | Good | Limited |

**Decision: RP2040** - Best balance of cost, performance, and development ecosystem.
PIO state machines enable precise timing for cartridge protocols.

---

## Schematic Design

### Power Supply

```
USB-C VBUS (5V)
      │
      ├──────────────────────────────────► +5V_CART (GB cartridges)
      │
      │    ┌─────────────┐
      └───►│  AP2112K    │
           │  3.3V LDO   │──────────────► +3.3V (RP2040, GBA)
           │  600mA      │
           └─────────────┘

Power Budget:
  • RP2040: ~25mA typical
  • Level shifters: ~10mA
  • GB cartridge: ~50mA max
  • GBA cartridge: ~80mA max
  • Total: ~165mA max (well under 500mA USB limit)
```

### RP2040 Core

```
                        ┌────────────────────────────────┐
                        │           RP2040               │
                        │                                │
   USB D+  ─────────────┤ GPIO19 (USB_DP)                │
   USB D-  ─────────────┤ GPIO18 (USB_DM)                │
                        │                                │
   12MHz ──────────────►┤ XIN                            │
   Crystal              │                                │
                        │                                │
   Flash ◄─────────────►┤ GPIO0-3 (QSPI)   GPIO4-11 ────┼──► DATA[0:7]
   W25Q16               │                                │
                        │                   GPIO12-27 ───┼──► ADDR[0:15]
   BOOT ───────────────►┤ GPIO22 (BOOTSEL)               │
                        │                   GPIO28 ──────┼──► ~RD
   LED_PWR ◄────────────┤ GPIO25           GPIO29 ──────┼──► ~WR
   LED_ACT ◄────────────┤ GPIO24                         │
                        │                                │
   BTN ────────────────►┤ GPIO23           RUN ◄────────┼─── RESET
                        │                                │
                        └────────────────────────────────┘
```

### Cartridge Interface

```
                    ┌──────────────────────────────────────────────┐
                    │              Level Shifters                   │
                    │                                               │
 RP2040             │   U1: 74LVC245        U2: 74LVC245           │    Cartridge
 (3.3V)             │   (Address Low)       (Address High)         │    (5V/3.3V)
                    │                                               │
 A[0:7]  ──────────►│──►  A ──────► B  ──►──────────────────────────│──► ADDR[0:7]
                    │     DIR=1 (A→B)                               │
                    │     VccA=3.3V, VccB=VCC_CART                  │
                    │                                               │
 A[8:15] ──────────►│──►  A ──────► B  ──►──────────────────────────│──► ADDR[8:15]
                    │                                               │
                    │   U3: 74LVC245                                │
                    │   (Data - Bidirectional)                      │
                    │                                               │
 D[0:7]  ◄─────────►│◄─►  A ◄─────► B  ◄─►─────────────────────────│◄─► DATA[0:7]
                    │     DIR=~RD (0=read, 1=write)                 │
                    │                                               │
                    └──────────────────────────────────────────────┘

Voltage Selection (via analog switch or jumper):
  • SLOT_SEL = 0: VCC_CART = 5V  (Game Boy)
  • SLOT_SEL = 1: VCC_CART = 3.3V (Game Boy Advance)
```

---

## PCB Design

### Form Factor

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   Target Enclosure: Hammond 1593K or similar                     │
│   PCB Size: 65mm x 55mm (fits standard project box)              │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │                    65mm                                  │   │
│   │  ┌───────────────────────────────────────────────────┐  │   │
│   │  │                                                   │  │   │
│   │  │   ┌─────────────────────────────────────────┐    │  │55mm
│   │  │   │         GB CARTRIDGE SLOT               │    │  │   │
│   │  │   │         (32 pins, edge connector)       │    │  │   │
│   │  │   └─────────────────────────────────────────┘    │  │   │
│   │  │                                                   │  │   │
│   │  │   ┌─────────────────────────────────────────┐    │  │   │
│   │  │   │         GBA CARTRIDGE SLOT              │    │  │   │
│   │  │   │         (32 pins, edge connector)       │    │  │   │
│   │  │   └─────────────────────────────────────────┘    │  │   │
│   │  │                                                   │  │   │
│   │  │   ┌──────┐  ┌──────┐  ┌──────┐                   │  │   │
│   │  │   │RP2040│  │ U1   │  │ U2   │  [●] PWR         │  │   │
│   │  │   │      │  │245   │  │245   │  [●] ACT         │  │   │
│   │  │   └──────┘  └──────┘  └──────┘  [□] BTN         │  │   │
│   │  │                                                   │  │   │
│   │  │   ┌──────┐  ┌──────────┐                         │  │   │
│   │  │   │ U3   │  │  USB-C   │ ◄── Power & Data       │  │   │
│   │  │   │245   │  └──────────┘                         │  │   │
│   │  │   └──────┘                                        │  │   │
│   │  └───────────────────────────────────────────────────┘  │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Layer Stackup (2-Layer)

```
┌─────────────────────────────────────────┐
│  TOP: Components, Signals               │  1.6mm FR4
├─────────────────────────────────────────┤
│  BOTTOM: Ground Plane, Power            │
└─────────────────────────────────────────┘

Design Rules:
  • Track width: 0.2mm (signal), 0.5mm (power)
  • Clearance: 0.2mm
  • Via: 0.3mm drill, 0.6mm pad
  • All decoupling caps on TOP near ICs
```

---

## Bill of Materials

| Ref | Component | Package | Qty | Unit Price | Total |
|-----|-----------|---------|-----|------------|-------|
| U1 | RP2040 | QFN-56 | 1 | €0.70 | €0.70 |
| U2 | W25Q16JVSNIQ (2MB Flash) | SOIC-8 | 1 | €0.25 | €0.25 |
| U3 | AP2112K-3.3 (LDO) | SOT-23-5 | 1 | €0.15 | €0.15 |
| U4-U6 | 74LVC245APW | TSSOP-20 | 3 | €0.25 | €0.75 |
| U7 | TS5A3159 (Analog Switch) | SOT-23-6 | 1 | €0.20 | €0.20 |
| Y1 | 12MHz Crystal | 3215 | 1 | €0.15 | €0.15 |
| J1 | USB-C Receptacle | Mid-mount | 1 | €0.30 | €0.30 |
| J2 | GB Cartridge Slot | Edge Conn | 1 | €1.50 | €1.50 |
| J3 | GBA Cartridge Slot | Edge Conn | 1 | €1.80 | €1.80 |
| LED1-2 | 0603 LED (Green, Blue) | 0603 | 2 | €0.02 | €0.04 |
| SW1 | Tactile Button | 6x6mm | 1 | €0.05 | €0.05 |
| R1-R4 | 27Ω (USB) | 0402 | 2 | €0.01 | €0.02 |
| R5-R8 | 10kΩ | 0402 | 4 | €0.01 | €0.04 |
| R9-R10 | 330Ω (LED) | 0402 | 2 | €0.01 | €0.02 |
| C1-C10 | 100nF | 0402 | 10 | €0.01 | €0.10 |
| C11-C12 | 10µF | 0603 | 2 | €0.02 | €0.04 |
| C13-C14 | 15pF (Crystal) | 0402 | 2 | €0.01 | €0.02 |
| | **PCB (2-layer, 65x55mm)** | | 1 | €1.00 | €1.00 |
| | **Enclosure (Hammond 1593K)** | | 1 | €3.00 | €3.00 |
| | | | | **TOTAL:** | **~€10.13** |

---

## Firmware Architecture

### USB Interface

```c
// USB Device Descriptor
#define USB_VID  0x1209  // pid.codes VID
#define USB_PID  0x4742  // "GB" in hex

// USB Endpoints
EP0: Control (setup, status)
EP1: Bulk OUT (commands from host)
EP2: Bulk IN (data to host)

// Command Protocol
struct gb_command {
    uint8_t  cmd;        // Command type
    uint8_t  slot;       // 0=GB, 1=GBA
    uint32_t address;    // Start address
    uint32_t length;     // Bytes to transfer
    uint8_t  flags;      // Options
};

// Commands
CMD_GET_INFO      = 0x01  // Get device info
CMD_DETECT_CART   = 0x02  // Detect cartridge
CMD_READ_ROM      = 0x10  // Read ROM data
CMD_READ_RAM      = 0x11  // Read save RAM
CMD_WRITE_RAM     = 0x12  // Write save RAM
CMD_READ_HEADER   = 0x13  // Read cartridge header
CMD_SET_MBC       = 0x20  // Set MBC type (for bank switching)
```

### PIO-Based Fast Read

```c
// PIO program for parallel bus read (GB/GBA)
// Achieves ~4 MB/s transfer rate

.program cart_read
.wrap_target
    pull block          ; Get address from TX FIFO
    out pins, 16        ; Output address to A[0:15]
    set pins, 0         ; Assert ~RD (active low)
    nop [3]             ; Wait for data valid (~100ns)
    in pins, 8          ; Read D[0:7]
    push                ; Send to RX FIFO
    set pins, 1         ; Deassert ~RD
.wrap

// Transfer speeds:
// GB ROM:  ~2 MB in 0.5 seconds
// GBA ROM: ~32 MB in 8 seconds
```

### MBC Support

```c
// Supported Memory Bank Controllers

// Game Boy
typedef enum {
    MBC_NONE,      // 32KB ROM only
    MBC_MBC1,      // Up to 2MB ROM, 32KB RAM
    MBC_MBC2,      // Up to 256KB ROM, 512x4 bits RAM
    MBC_MBC3,      // Up to 2MB ROM, 32KB RAM, RTC
    MBC_MBC5,      // Up to 8MB ROM, 128KB RAM
    MBC_MBC6,      // Rare
    MBC_MBC7,      // Accelerometer
    MBC_MMM01,     // Multi-game
    MBC_HUC1,      // Hudson
    MBC_HUC3,      // Hudson with RTC
    MBC_CAMERA,    // Game Boy Camera
} mbc_type_t;

// Game Boy Advance
typedef enum {
    GBA_ROM_ONLY,      // No save
    GBA_SRAM,          // 32KB SRAM
    GBA_EEPROM_512,    // 512 bytes EEPROM
    GBA_EEPROM_8K,     // 8KB EEPROM
    GBA_FLASH_64K,     // 64KB Flash
    GBA_FLASH_128K,    // 128KB Flash
} gba_save_type_t;
```

---

## Software (Host Side)

### CLI Tool: `ufi-gbgba`

```bash
# Detect cartridge
ufi-gbgba detect
# Output: Game Boy Color cartridge detected
#         Title: POKEMON YELLOW
#         MBC: MBC5
#         ROM: 1MB (64 banks)
#         RAM: 32KB

# Dump ROM
ufi-gbgba dump-rom -o pokemon_yellow.gb
# Progress: [████████████████████] 100% (1.00 MB)
# SHA256: a9be...

# Dump save
ufi-gbgba dump-save -o pokemon_yellow.sav

# Restore save
ufi-gbgba write-save -i pokemon_yellow.sav

# GBA mode
ufi-gbgba detect --gba
ufi-gbgba dump-rom --gba -o game.gba
```

### Python Library

```python
import ufi_gbgba

# Connect to device
dev = ufi_gbgba.Device()

# Detect cartridge
cart = dev.detect()
print(f"Title: {cart.title}")
print(f"Type: {cart.type}")  # GB, GBC, GBA
print(f"ROM Size: {cart.rom_size}")
print(f"RAM Size: {cart.ram_size}")

# Dump ROM
with open("game.gb", "wb") as f:
    for chunk in dev.read_rom():
        f.write(chunk)

# Dump/restore save
save_data = dev.read_save()
dev.write_save(save_data)
```

---

## Enclosure Design

### Hammond 1593K Modification

```
┌─────────────────────────────────────────────────────────────────┐
│                        TOP VIEW                                  │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │   │
│   │  ▓▓▓▓▓▓▓▓▓▓▓▓  GB SLOT CUTOUT  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │   │
│   │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │   │
│   │                                                         │   │
│   │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │   │
│   │  ▓▓▓▓▓▓▓▓▓▓▓▓  GBA SLOT CUTOUT ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │   │
│   │  ▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓▓  │   │
│   │                                                         │   │
│   │                              [●] PWR   GB/GBA DUMPER   │   │
│   │                              [●] ACT                    │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
│                         FRONT VIEW                               │
│                                                                  │
│   ┌─────────────────────────────────────────────────────────┐   │
│   │                                                         │   │
│   │                    [═══USB-C═══]                        │   │
│   │                                                         │   │
│   └─────────────────────────────────────────────────────────┘   │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘

Cutouts needed:
  • Top: GB slot opening (56mm x 8mm)
  • Top: GBA slot opening (52mm x 5mm)  
  • Front: USB-C opening (9mm x 3.5mm)
  • Top: LED windows (2x 3mm holes)
```

### 3D Printed Enclosure (Alternative)

Custom enclosure can be designed in FreeCAD/OpenSCAD:
- Exact fit for PCB
- Integrated cartridge guides
- Light pipes for LEDs
- Snap-fit assembly

---

## Optional: UFI Expansion Mode

For direct connection to UFI mainboard (no USB):

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                  │
│   GB/GBA Dumper can operate in two modes:                        │
│                                                                  │
│   MODE 1: Standalone USB Device                                  │
│   ┌──────────┐     USB-C      ┌─────────────┐                   │
│   │   PC     │◄──────────────►│ GB/GBA      │                   │
│   │          │                │ Dumper      │                   │
│   └──────────┘                └─────────────┘                   │
│                                                                  │
│   MODE 2: UFI Expansion Module                                   │
│   ┌──────────────┐  40-pin    ┌─────────────┐                   │
│   │  UFI         │◄──────────►│ GB/GBA      │                   │
│   │  Mainboard   │  Header    │ Dumper      │                   │
│   └──────────────┘            └─────────────┘                   │
│                                                                  │
│   Detect mode via pin (e.g., ID pin on expansion header)         │
│   If USB VBUS present → USB mode                                 │
│   If expansion header connected → UFI mode                       │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Development Roadmap

### Phase 1: Hardware ✅
- [x] Component selection (RP2040)
- [x] Schematic design
- [ ] PCB layout
- [ ] Prototype fabrication

### Phase 2: Firmware
- [ ] USB bootloader (TinyUSB)
- [ ] Basic USB communication
- [ ] GB cartridge read
- [ ] GBA cartridge read
- [ ] MBC bank switching
- [ ] Save read/write

### Phase 3: Software
- [ ] CLI tool (Python)
- [ ] GUI application
- [ ] ROM database integration
- [ ] Save file management

### Phase 4: Production
- [ ] Enclosure design
- [ ] Documentation
- [ ] Community release

---

## References

- [RP2040 Datasheet](https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf)
- [Game Boy CPU Manual](https://gekkio.fi/files/gb-docs/gbctr.pdf)
- [GBA Technical Info](https://problemkaputt.de/gbatek.htm)
- [Insidegadgets GB Cart Reader](https://github.com/insidegadgets/GBCartRead)
- [GBxCart RW](https://github.com/lesserkuma/FlashGBX)
