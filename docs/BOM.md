# UFI - Universal Flux Interface
## Bill of Materials (BOM)

### Version 0.1 - Preliminary

---

## 1. Main ICs

| Ref | Part Number | Description | Package | Qty | Source | Est. Price |
|-----|-------------|-------------|---------|-----|--------|------------|
| U1 | STM32H723VGT6 | ARM Cortex-M7 MCU 550MHz 1MB Flash | LQFP-100 | 1 | Mouser/DigiKey | €8.50 |
| U2 | AMS1117-3.3 | 3.3V 1A LDO Regulator | SOT-223 | 1 | LCSC | €0.15 |
| U3 | LM2596S-5.0 | 5V 3A Step-Down Regulator | TO-263-5 | 1 | LCSC | €0.80 |
| U4-U7 | 74LVC245APW | 8-bit Level Shifter | TSSOP-20 | 4 | LCSC | €0.25 ea |
| U8 | SN7406DR | Hex Inverter Open-Collector | SOIC-14 | 1 | Mouser | €0.60 |
| U9 | SN7407DR | Hex Buffer Open-Collector | SOIC-14 | 1 | Mouser | €0.60 |
| U10 | USBLC6-2SC6 | USB ESD Protection | SOT-23-6 | 2 | LCSC | €0.20 ea |

---

## 2. Connectors

| Ref | Part Number | Description | Qty | Source | Est. Price |
|-----|-------------|-------------|-----|--------|------------|
| J1 | DF40C-100DP-0.4V | CM5 Connector Top (100-pin) | 1 | Mouser | €3.50 |
| J2 | DF40C-100DP-0.4V | CM5 Connector Bottom (100-pin) | 1 | Mouser | €3.50 |
| J3, J4 | IDC-34 | 34-pin Shugart FDD Connector | 2 | LCSC | €0.40 ea |
| J5 | IDC-20 | 20-pin Apple Disk II Connector | 1 | LCSC | €0.30 |
| J6 | DSUB-23 Female | Amiga External FDD Connector | 1 | Mouser | €2.50 |
| J7 | DIN-6 Female | IEC Bus (C64) Connector | 1 | Mouser | €1.80 |
| J8, J9 | JST-PH 3-pin | Sync Sensor Connectors | 2 | LCSC | €0.10 ea |
| J10 | USB-C Receptacle | USB-C Power/Data | 1 | LCSC | €0.50 |
| J11 | Barrel Jack 5.5x2.1 | 12V DC Input | 1 | LCSC | €0.30 |
| J12 | RJ45 MagJack | Gigabit Ethernet w/ LEDs | 1 | LCSC | €2.00 |
| J13 | HDMI-A Female | HDMI 2.0 Output | 1 | LCSC | €1.20 |
| J14 | M.2 Key M Socket | NVMe SSD Slot | 1 | LCSC | €1.50 |
| J15 | 2x5 Header 1.27mm | SWD Debug Connector | 1 | LCSC | €0.20 |

---

## 3. Crystals & Oscillators

| Ref | Part Number | Description | Package | Qty | Source | Est. Price |
|-----|-------------|-------------|---------|-----|--------|------------|
| Y1 | ABM8G-25.000MHZ | 25MHz Crystal HSE | 3.2x2.5mm | 1 | Mouser | €0.50 |
| Y2 | ABM8G-32.768KHZ | 32.768kHz Crystal LSE | 3.2x2.5mm | 1 | Mouser | €0.40 |

---

## 4. Passive Components

### Capacitors

| Value | Package | Qty | Description | Est. Price |
|-------|---------|-----|-------------|------------|
| 100nF | 0402 | 30 | Decoupling | €0.50 total |
| 1µF | 0402 | 10 | Decoupling | €0.30 total |
| 10µF | 0805 | 8 | Bulk capacitors | €0.40 total |
| 22µF | 1206 | 4 | Power supply | €0.40 total |
| 100µF/16V | Electrolytic | 2 | Input filter | €0.30 total |
| 22pF | 0402 | 4 | Crystal load | €0.10 total |
| 4.7µF | 0603 | 2 | USB decoupling | €0.10 total |
| 2.2µF | 0603 | 2 | LDO output | €0.10 total |

### Resistors

| Value | Package | Qty | Description | Est. Price |
|-------|---------|-----|-------------|------------|
| 10kΩ | 0402 | 20 | Pull-ups/downs | €0.20 total |
| 1kΩ | 0402 | 10 | IEC pull-ups | €0.10 total |
| 330Ω | 0402 | 4 | LED current limit | €0.05 total |
| 22Ω | 0402 | 4 | USB series resistors | €0.05 total |
| 5.1kΩ | 0402 | 2 | USB-C CC resistors | €0.05 total |
| 100Ω | 0402 | 8 | Termination | €0.10 total |
| 0Ω | 0402 | 4 | Jumpers | €0.05 total |

### Inductors

| Value | Package | Qty | Description | Est. Price |
|-------|---------|-----|-------------|------------|
| 33µH | 8x8mm | 1 | LM2596 inductor | €0.50 |
| Ferrite Bead | 0805 | 4 | EMI suppression | €0.20 total |

---

## 5. Diodes & LEDs

| Ref | Part Number | Description | Package | Qty | Source | Est. Price |
|-----|-------------|-------------|---------|-----|--------|------------|
| D1 | SS34 | 3A Schottky (LM2596) | SMA | 1 | LCSC | €0.10 |
| D2 | SMBJ12CA | 12V TVS Diode | SMB | 1 | LCSC | €0.20 |
| D3 | BAT54S | Dual Schottky | SOT-23 | 2 | LCSC | €0.10 ea |
| LED1 | Green 0805 | LINK LED | 0805 | 1 | LCSC | €0.02 |
| LED2 | Green 0805 | READ LED | 0805 | 1 | LCSC | €0.02 |
| LED3 | Yellow 0805 | WRITE LED | 0805 | 1 | LCSC | €0.02 |
| LED4 | Red 0805 | ERROR LED | 0805 | 1 | LCSC | €0.02 |

---

## 6. Transistors & MOSFETs

| Ref | Part Number | Description | Package | Qty | Source | Est. Price |
|-----|-------------|-------------|---------|-----|--------|------------|
| Q1 | SI2301CDS | P-Channel MOSFET (Reverse polarity) | SOT-23 | 1 | LCSC | €0.10 |
| Q2 | 2N7002 | N-Channel MOSFET | SOT-23 | 4 | LCSC | €0.05 ea |

---

## 7. Mechanical

| Item | Description | Qty | Source | Est. Price |
|------|-------------|-----|--------|------------|
| PCB | 4-Layer, 160x100mm, 1.6mm, ENIG | 1 | JLCPCB | €15-25 |
| Standoffs | M2.5x11mm Brass | 4 | Amazon | €2.00 |
| Screws | M2.5x5mm | 8 | Amazon | €1.00 |
| Heatsink | 14x14x6mm Aluminum | 1 | Amazon | €0.50 |
| Thermal Pad | 15x15x1mm | 1 | Amazon | €0.30 |

---

## 8. Optional Modules

| Item | Description | Qty | Source | Est. Price |
|------|-------------|-----|--------|------------|
| Raspberry Pi CM5 | CM5104032 (4GB/32GB) | 1 | RPi | €70 |
| NVMe SSD | 256GB M.2 2280 | 1 | Amazon | €25 |
| 12V PSU | 12V/3A Barrel Jack | 1 | Amazon | €10 |
| FDD Cable | 34-pin Ribbon | 2 | Amazon | €5 ea |

---

## Cost Summary

| Category | Estimated Cost |
|----------|----------------|
| Main ICs | €15.00 |
| Connectors | €20.00 |
| Passives | €5.00 |
| Diodes/LEDs | €1.00 |
| Mechanical | €20.00 |
| **PCB + Assembly** | €25.00 |
| **Subtotal (Board only)** | **~€86** |
| CM5 + NVMe + PSU | €105 |
| **Total (Complete System)** | **~€191** |

---

## Notes

1. Prices are estimates based on LCSC/Mouser 2024 pricing
2. Quantities assume single unit build; volume pricing significantly lower
3. PCB price varies by manufacturer and options (ENIG vs HASL)
4. CM5 price depends on RAM/eMMC configuration
5. Some components may require substitution based on availability

---

## Recommended Suppliers

- **LCSC** - Passives, common ICs, connectors (best for China-manufactured PCBs)
- **Mouser/DigiKey** - Specialty parts, guaranteed authenticity
- **JLCPCB** - PCB manufacturing, SMT assembly
- **Raspberry Pi** - Official CM5 modules
- **Amazon/AliExpress** - Mechanical parts, cables, power supplies
