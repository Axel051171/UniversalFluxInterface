# UFI Mainboard - Bill of Materials (BOM)

## Summary

| Category | Items | Estimated Cost |
|----------|-------|----------------|
| MCU & Memory | 4 | €25.00 |
| Power Management | 12 | €18.00 |
| Connectors | 15 | €45.00 |
| Passives | ~150 | €15.00 |
| Level Shifters & Logic | 10 | €8.00 |
| Crystals & Oscillators | 3 | €5.00 |
| Protection & ESD | 8 | €6.00 |
| CM5 Module | 1 | €75.00 |
| PCB (4-layer) | 1 | €25.00 |
| **TOTAL** | | **~€222** |

---

## Detailed BOM

### MCU & Memory

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| U1 | STM32H723ZGT6 | LQFP-144 | 1 | €12.00 | €12.00 | Flux capture MCU |
| U2 | W25Q128JVSIQ | SOIC-8 | 1 | €2.50 | €2.50 | 16MB SPI Flash |
| U3 | IS42S16160J-6TLI | TSOP-II-54 | 1 | €4.00 | €4.00 | 32MB SDRAM (optional) |
| U4 | 24LC256 | SOIC-8 | 1 | €0.50 | €0.50 | 32KB I2C EEPROM |

### Power Management

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| U10 | TPS54331 | SOIC-8 | 1 | €1.50 | €1.50 | 5V/3A Buck (12V→5V) |
| U11 | TPS54331 | SOIC-8 | 1 | €1.50 | €1.50 | 3.3V/3A Buck (5V→3.3V) |
| U12 | AP7361C-33E | SOT-223 | 1 | €0.30 | €0.30 | 3.3V LDO (analog) |
| U13 | AP7361C-18E | SOT-223 | 1 | €0.30 | €0.30 | 1.8V LDO (CM5) |
| U14 | TPS2113A | SOIC-8 | 1 | €1.20 | €1.20 | Power MUX |
| U15 | INA219 | SOIC-8 | 1 | €1.50 | €1.50 | Current monitor |
| L1-L2 | 10µH/3A | 7x7mm | 2 | €0.40 | €0.80 | Buck inductors |
| D1-D2 | SS34 | SMA | 2 | €0.15 | €0.30 | Schottky diodes |
| C20-C25 | 100µF/25V | 8mm Electrolytic | 6 | €0.20 | €1.20 | Bulk capacitors |
| F1 | 3A Polyfuse | 1812 | 1 | €0.30 | €0.30 | Input protection |

### Connectors

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| J1-J2 | 34-pin IDC Header | 2.54mm shrouded | 2 | €0.80 | €1.60 | Shugart FDD |
| J3 | 20-pin IDC Header | 2.54mm shrouded | 1 | €0.60 | €0.60 | Apple Disk II |
| J4 | DB-23 Female | Horizontal | 1 | €2.50 | €2.50 | Amiga external |
| J5 | DIN-6 Female | PCB mount | 1 | €1.20 | €1.20 | C64 IEC bus |
| J6-J7 | Hirose DF40C-100DP-0.4V | SMD | 2 | €3.50 | €7.00 | CM5 connectors |
| J8 | USB-C Receptacle | Mid-mount | 1 | €0.40 | €0.40 | Power input |
| J9 | USB-C Receptacle | Mid-mount | 1 | €0.40 | €0.40 | Data (to CM5) |
| J10 | RJ45 MagJack | Tab-down | 1 | €2.50 | €2.50 | Gigabit Ethernet |
| J11 | HDMI Type-A | Horizontal | 1 | €1.50 | €1.50 | HDMI output |
| J12 | M.2 Key M | 2280 | 1 | €1.80 | €1.80 | NVMe SSD |
| J13 | 40-pin Header | 2x20, 2.54mm | 1 | €0.50 | €0.50 | Expansion module |
| J14-J15 | 3-pin JST-XH | 2.54mm | 2 | €0.15 | €0.30 | Sync sensors |
| J16 | DC Barrel Jack | 5.5/2.1mm | 1 | €0.40 | €0.40 | 12V input |
| J17 | 10-pin SWD | 1.27mm | 1 | €0.30 | €0.30 | Debug header |
| J18 | MicroSD Slot | Push-push | 1 | €0.80 | €0.80 | Optional storage |

### Level Shifters & Logic

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| U20-U23 | 74LVC245APW | TSSOP-20 | 4 | €0.30 | €1.20 | FDD level shift |
| U24-U25 | 74LVC245APW | TSSOP-20 | 2 | €0.30 | €0.60 | IEC bus drivers |
| U26 | 74LVC1G125 | SOT-23-5 | 2 | €0.15 | €0.30 | Index pulse buffer |
| U27 | SN74LVC2G14 | SOT-23-6 | 2 | €0.20 | €0.40 | Schmitt triggers |
| U28 | 74HC4052 | SOIC-16 | 1 | €0.25 | €0.25 | Drive select MUX |
| U29 | TXB0104 | TSSOP-14 | 1 | €0.50 | €0.50 | Bidirectional shifter |

### Crystals & Oscillators

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| Y1 | 25MHz Crystal | 3.2x2.5mm | 1 | €0.30 | €0.30 | STM32 HSE |
| Y2 | 32.768kHz Crystal | 2x1.2mm | 1 | €0.20 | €0.20 | STM32 LSE |
| Y3 | 25MHz TCXO | 3.2x2.5mm | 1 | €2.00 | €2.00 | Ethernet (optional) |

### Protection & ESD

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| D10-D11 | USBLC6-2SC6 | SOT-23-6 | 2 | €0.40 | €0.80 | USB ESD |
| D12-D13 | PESD5V0S1BL | SOD-323 | 4 | €0.15 | €0.60 | General ESD |
| D14 | SM712 | SOT-23 | 1 | €0.30 | €0.30 | IEC bus TVS |
| D15-D16 | SMF5.0A | SOD-123FL | 2 | €0.10 | €0.20 | Power TVS |
| U30 | MAX6818 | SOIC-16 | 1 | €1.50 | €1.50 | Button debounce |

### Passives (Approximate)

| Component | Package | Qty | Unit Price | Total | Notes |
|-----------|---------|-----|------------|-------|-------|
| 100nF MLCC | 0402 | 80 | €0.01 | €0.80 | Decoupling |
| 10µF MLCC | 0805 | 20 | €0.05 | €1.00 | Bulk decoupling |
| 4.7µF MLCC | 0603 | 15 | €0.03 | €0.45 | Bypass |
| 1µF MLCC | 0402 | 20 | €0.02 | €0.40 | Various |
| 10kΩ | 0402 | 30 | €0.01 | €0.30 | Pull-up/down |
| 1kΩ | 0402 | 20 | €0.01 | €0.20 | Current limit |
| 100Ω | 0402 | 10 | €0.01 | €0.10 | Series terminators |
| 33Ω | 0402 | 8 | €0.01 | €0.08 | USB series |
| Various | 0402/0603 | 50 | €0.01 | €0.50 | Other values |
| Ferrite 600Ω | 0603 | 10 | €0.05 | €0.50 | Power filtering |

### Indicators & Buttons

| Ref | Component | Package | Qty | Unit Price | Total | Notes |
|-----|-----------|---------|-----|------------|-------|-------|
| LED1-LED4 | 0603 LED | 0603 | 4 | €0.03 | €0.12 | Status LEDs |
| SW1 | Tactile Button | 6x6mm | 1 | €0.05 | €0.05 | Reset |
| SW2 | Tactile Button | 6x6mm | 1 | €0.05 | €0.05 | User/Boot |

### PCB & Assembly

| Item | Specification | Qty | Unit Price | Total | Notes |
|------|---------------|-----|------------|-------|-------|
| PCB | 4-layer, 160x100mm, ENIG | 1 | €15.00 | €15.00 | JLCPCB 5pcs |
| SMT Assembly | ~200 parts | 1 | €10.00 | €10.00 | JLCPCB assembly |

### CM5 Module (Not included in BOM, user provided)

| Item | Specification | Qty | Unit Price | Total | Notes |
|------|---------------|-----|------------|-------|-------|
| Raspberry Pi CM5 | 4GB RAM | 1 | €75.00 | €75.00 | User provided |

---

## Cost Breakdown by Subsystem

```
┌────────────────────────────────────────────────────────────────┐
│                    UFI MAINBOARD COST BREAKDOWN                │
├────────────────────────────────────────────────────────────────┤
│                                                                │
│  STM32H723 Core            ████████░░░░░░░░░░░░  €25.00 (11%)  │
│  Power Management          ██████████░░░░░░░░░░  €18.00 (8%)   │
│  Connectors                ████████████████████  €45.00 (20%)  │
│  Level Shifters & Logic    ████░░░░░░░░░░░░░░░░  €8.00  (4%)   │
│  Passives                  ██████░░░░░░░░░░░░░░  €15.00 (7%)   │
│  Protection                ███░░░░░░░░░░░░░░░░░  €6.00  (3%)   │
│  Crystals                  ██░░░░░░░░░░░░░░░░░░  €5.00  (2%)   │
│  PCB & Assembly            ███████████░░░░░░░░░  €25.00 (11%)  │
│  ─────────────────────────────────────────────────────────────│
│  SUBTOTAL (PCB + Parts)                         €147.00       │
│  CM5 Module (user)         ████████████████████  €75.00 (34%)  │
│  ─────────────────────────────────────────────────────────────│
│  TOTAL WITH CM5                                  €222.00       │
│                                                                │
└────────────────────────────────────────────────────────────────┘
```

---

## Ordering Notes

### PCB Fabrication (JLCPCB)
- **Layers**: 4
- **Dimensions**: 160mm x 100mm
- **Thickness**: 1.6mm
- **Surface Finish**: ENIG (required for CM5 connector)
- **Copper Weight**: 1oz outer, 0.5oz inner
- **Min Track/Space**: 0.15mm/0.15mm
- **Min Hole**: 0.2mm
- **Impedance Control**: Yes (USB, Ethernet, HDMI)

### Assembly Options
1. **Full JLCPCB Assembly** (~€35 extra for 5 boards)
   - Most SMD parts available in JLCPCB library
   - May need to supply CM5 connectors separately

2. **Partial Assembly + Hand Solder**
   - Get SMD parts assembled
   - Hand solder through-hole connectors

3. **Kit Form**
   - PCB only, source parts separately
   - Best for prototyping/modifications

---

## Revision History

| Rev | Date | Changes |
|-----|------|---------|
| 0.1 | 2026-01-16 | Initial BOM |
| 0.2 | 2026-01-17 | Added detailed part numbers, updated pricing |
