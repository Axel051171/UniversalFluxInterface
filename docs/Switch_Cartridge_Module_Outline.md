# UFI Expansion Module: Switch Cartridge Reader (Hardware Outline)

## ⚠️ WICHTIGER HINWEIS

Dieses Dokument beschreibt **nur die Hardware**. Die Firmware ist:
- Proprietär (MIG/Lattice)
- Möglicherweise urheberrechtlich geschützt
- Nicht Teil dieses Projekts

**Rechtliche Risiken:**
- Nintendo verfolgt aktiv Flashcart-Hersteller
- Die MIG-Firmware könnte Nintendo-IP enthalten
- Verwendung könnte gegen Urheberrecht verstoßen

Dieses Dokument dient **nur zu Bildungszwecken**.

---

## Hardware-Architektur

Das MIG-Dumper Design verwendet:

```
┌─────────────────────────────────────────────────────────────────────┐
│                     Switch Cartridge Module                          │
│                                                                      │
│   ┌─────────────────┐      ┌─────────────────┐                      │
│   │ Switch Game     │      │                 │                      │
│   │ Cartridge       │◄────►│  Lattice FPGA   │                      │
│   │ (16-pin)        │      │  (ICE40/ECP5)   │                      │
│   └─────────────────┘      └────────┬────────┘                      │
│                                     │                               │
│                                     │ SPI/Parallel                  │
│                                     │                               │
│                            ┌────────▼────────┐                      │
│                            │    ESP32-S2     │                      │
│                            │  (or STM32H7)   │                      │
│                            └────────┬────────┘                      │
│                                     │                               │
│                                     │ USB                           │
│                                     ▼                               │
│                            ┌─────────────────┐                      │
│                            │   USB Type-C    │                      │
│                            └─────────────────┘                      │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Nintendo Switch Game Card Pinout

Die Switch Game Card verwendet einen **16-pin** Kontakt:

```
        ┌──────────────────────────────────────┐
        │         Nintendo Switch              │
        │           Game Card                  │
        │         (Bottom View)                │
        └──────────────────────────────────────┘
        
        ┌──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┬──┐
        │1 │2 │3 │4 │5 │6 │7 │8 │9 │10│11│12│13│14│15│16│
        └──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┴──┘
        
        Pin 1:  Card Detect 1 (directly directly directly directly directly directly directly directly directly GND when inserted)
        Pin 2:  Card Detect 2 (directly directly directly directly directly directly directly directly directly GND when inserted)
        Pin 3:  DATA0
        Pin 4:  DATA1
        Pin 5:  DATA2
        Pin 6:  DATA3
        Pin 7:  VCC (3.3V)
        Pin 8:  CLK (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly Clock)
        Pin 9:  GND
        Pin 10: CMD (directly directly directly directly directly directly directly directly directly directly directly directly Command)
        Pin 11: RST (directly directly directly directly directly directly directly directly directly directly directly directly directly directly Reset)
        Pin 12: DATA4
        Pin 13: DATA5
        Pin 14: DATA6
        Pin 15: DATA7
        Pin 16: VCC (directly directly directly 1.8V)
```

### Signal-Beschreibung

| Pin | Name | Richtung | Beschreibung |
|-----|------|----------|--------------|
| 1-2 | DETECT | Out | Card Detection (beide GND = eingesteckt) |
| 3-6 | DATA0-3 | Bidir | Datenbus (low nibble) |
| 7 | VCC_3V3 | Power | 3.3V Versorgung |
| 8 | CLK | In | Takt (bis 200 MHz) |
| 9 | GND | Power | Masse |
| 10 | CMD | Bidir | Kommando/Response |
| 11 | RST | In | Reset |
| 12-15 | DATA4-7 | Bidir | Datenbus (high nibble) |
| 16 | VCC_1V8 | Power | 1.8V Versorgung |

---

## FPGA-Anforderungen

### Warum ein FPGA nötig ist

1. **Timing-kritisch:** Das Protokoll erfordert präzises Timing im ns-Bereich
2. **Parallelität:** 8-bit Datenbus + CMD gleichzeitig
3. **Geschwindigkeit:** Bis zu 200 MHz Taktrate
4. **Protokoll-Komplexität:** Proprietäres Kommando-Set

### Geeignete FPGAs

| FPGA | IOs | Preis | Anmerkung |
|------|-----|-------|-----------|
| Lattice iCE40UP5K | 39 | ~€5 | Minimal, Open-Source Tools |
| Lattice iCE40HX8K | 206 | ~€10 | Mehr IOs |
| Lattice ECP5-25 | 197 | ~€15 | Schneller, mehr Logik |
| Gowin GW1NR-9 | 86 | ~€8 | Günstig, eigene Tools |

### Schnittstelle FPGA ↔ MCU

```
FPGA                                MCU (STM32/ESP32)
┌─────────────┐                    ┌─────────────┐
│             │                    │             │
│  SPI Slave ─┼────────────────────┼─ SPI Master │
│             │                    │             │
│  IRQ       ─┼────────────────────┼─ GPIO (INT) │
│             │                    │             │
│  RESET     ─┼────────────────────┼─ GPIO       │
│             │                    │             │
└─────────────┘                    └─────────────┘

SPI Commands (Beispiel):
  0x01 - Read Status
  0x02 - Set Address
  0x03 - Read Data
  0x04 - Write Data
  0x05 - Send CMD
  0x06 - Get Response
```

---

## Schaltplan-Outline

### Power Supply

```
                 3.3V (from UFI)
                      │
                      ├────────────────┬──────────────────┐
                      │                │                  │
                     ─┴─              ─┴─                ─┴─
                     ───              ───                ───
                    100µF            100nF              100nF
                      │                │                  │
                      │          ┌─────┴─────┐           │
                      │          │           │           │
                      │          │   FPGA    │           │
                      │          │           │           │
                      │          └─────┬─────┘           │
                      │                │                  │
                      └────────────────┴──────────────────┘
                                       │
                                      GND


                 1.8V für Game Card
                 ┌─────────────────┐
    3.3V ────────┤  AP2112K-1.8    ├──────── 1.8V
                 │  (LDO 600mA)    │
                 └────────┬────────┘
                          │
                         GND
```

### FPGA Minimal-Beschaltung (iCE40)

```
                                    ┌─────────────────────────────┐
                                    │       iCE40UP5K-SG48        │
                                    │                             │
    3.3V ──────────────────────────►│ VCC                         │
                                    │                             │
    GND ───────────────────────────►│ GND                         │
                                    │                             │
    12 MHz ────────────────────────►│ CLK                         │
    Oszillator                      │                             │
                                    │                             │
    SPI_SCK ───────────────────────►│ IOB_32a                     │
    SPI_MOSI ──────────────────────►│ IOB_34a                     │
    SPI_MISO ◄─────────────────────│ IOB_35b                     │
    SPI_CS ────────────────────────►│ IOB_33b                     │
                                    │                             │
    CRESET ────────────────────────►│ CRESET_B                    │
    CDONE ◄────────────────────────│ CDONE                       │
                                    │                             │
                                    │ IOT_*  ──────► Game Card    │
                                    │              (directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly directly 16 Signals)     │
                                    │                             │
                                    └─────────────────────────────┘
```

### Level Shifter (3.3V ↔ 1.8V)

Die Game Card arbeitet teilweise mit 1.8V Signalen:

```
    FPGA (3.3V)              TXS0108E              Game Card (1.8V)
   ┌─────────┐            ┌───────────┐            ┌─────────┐
   │         │            │           │            │         │
   │ DATA0  ─┼────────────┼─ A1   B1 ─┼────────────┼─ DATA0  │
   │ DATA1  ─┼────────────┼─ A2   B2 ─┼────────────┼─ DATA1  │
   │ DATA2  ─┼────────────┼─ A3   B3 ─┼────────────┼─ DATA2  │
   │ DATA3  ─┼────────────┼─ A4   B4 ─┼────────────┼─ DATA3  │
   │ DATA4  ─┼────────────┼─ A5   B5 ─┼────────────┼─ DATA4  │
   │ DATA5  ─┼────────────┼─ A6   B6 ─┼────────────┼─ DATA5  │
   │ DATA6  ─┼────────────┼─ A7   B7 ─┼────────────┼─ DATA6  │
   │ DATA7  ─┼────────────┼─ A8   B8 ─┼────────────┼─ DATA7  │
   │         │            │           │            │         │
   │ CMD    ─┼────────────┼─ A    B  ─┼────────────┼─ CMD    │
   │ CLK    ─┼────────────┼─ A    B  ─┼────────────┼─ CLK    │
   │ RST    ─┼────────────┼─ A    B  ─┼────────────┼─ RST    │
   │         │            │           │            │         │
   └─────────┘            │  VCCA=3.3V│            └─────────┘
                          │  VCCB=1.8V│
                          │  OE=HIGH  │
                          └───────────┘
```

---

## Bill of Materials (Hardware)

| Ref | Component | Value | Package | Qty | Preis |
|-----|-----------|-------|---------|-----|-------|
| U1 | Lattice iCE40UP5K | - | SG48 | 1 | €5.00 |
| U2 | AP2112K-1.8 | 1.8V LDO | SOT-23-5 | 1 | €0.30 |
| U3 | TXS0108E | Level Shifter | TSSOP-20 | 2 | €1.00 |
| U4 | W25Q32 | 32Mbit Flash | SOIC-8 | 1 | €0.50 |
| Y1 | Oszillator | 12 MHz | 3225 | 1 | €0.50 |
| J1 | Game Card Slot | 16-pin | - | 1 | €3.00 |
| J2 | Expansion Header | 40-pin | 2.54mm | 1 | €0.50 |
| C* | Capacitors | Various | 0402 | 20 | €0.50 |
| R* | Resistors | Various | 0402 | 10 | €0.20 |

**Hardware-Total: ~€12** (ohne PCB, ohne Firmware)

---

## Integration mit UFI

### Option A: Separates Modul

```
UFI Mainboard                    Switch Module
┌─────────────┐                 ┌─────────────────┐
│             │                 │                 │
│  Expansion ─┼─────────────────┼─ Header         │
│  Header     │   40-pin        │                 │
│             │                 │  ┌───────────┐  │
│             │                 │  │   FPGA    │  │
│             │                 │  └─────┬─────┘  │
│             │                 │        │        │
│             │                 │  ┌─────┴─────┐  │
│             │                 │  │ Card Slot │  │
│             │                 │  └───────────┘  │
└─────────────┘                 └─────────────────┘
```

### Option B: Onboard FPGA

Könnte direkt auf das UFI Mainboard:

```
UFI Mainboard (erweitert)
┌───────────────────────────────────────────────────────┐
│                                                        │
│  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐ │
│  │  CM5    │  │ STM32   │  │  FPGA   │  │  Card   │ │
│  │         │  │ H723    │  │ iCE40   │  │  Slot   │ │
│  └─────────┘  └─────────┘  └────┬────┘  └────┬────┘ │
│                                  │            │       │
│                                  └────────────┘       │
│                                                        │
└───────────────────────────────────────────────────────┘
```

**Vorteil:** Kompakter, weniger Kabel
**Nachteil:** Höhere Kosten auch wenn nicht genutzt

---

## Firmware-Situation

### Was in der MIG-Firmware steckt (vermutlich)

```
MIG Firmware
├── ESP32-S2 Application
│   ├── USB Mass Storage
│   ├── XCI File Handling
│   ├── Certificate Management
│   └── FPGA Communication
│
└── FPGA Bitstream
    ├── Game Card Protocol State Machine
    ├── Timing Generator
    ├── Data Buffer
    └── ??? (möglicherweise Nintendo-spezifisch)
```

### Mögliche Quellen

1. **MIG Firmware Dump** - Rechtlich problematisch
2. **Reverse Engineering** - Aufwendig, evtl. illegal
3. **Clean-Room Implementation** - Möglich aber sehr aufwendig
4. **Open-Source Alternativen** - Nicht bekannt

### Clean-Room Approach

Falls jemand das Protokoll dokumentiert hat (ohne Nintendo-Code):

```
1. Protokoll-Analyse (Logikanalysator)
2. State Machine dokumentieren
3. Eigene Verilog/VHDL Implementation
4. Eigene MCU-Firmware
```

**Geschätzter Aufwand:** 6-12 Monate für eine Person

---

## Rechtliche Überlegungen

| Aspekt | Status | Risiko |
|--------|--------|--------|
| Hardware-Design | Eigenes Design | Gering |
| Pinout-Dokumentation | Öffentlich bekannt | Gering |
| Protokoll | Undokumentiert | Mittel |
| Verschlüsselung | Nintendo-proprietär | Hoch |
| MIG-Firmware nutzen | Proprietär | Sehr hoch |

### Empfehlung

1. **Hardware:** Kann gebaut werden ✅
2. **FPGA:** Kann programmiert werden ✅
3. **Firmware:** Nicht kopieren ❌
4. **Protokoll:** Clean-Room oder warten ⚠️

---

## Fazit

Das Switch-Cartridge-Modul ist **technisch machbar**, aber:

- Die Firmware ist das Hauptproblem
- Nintendo ist sehr aggressiv bei der Rechtsverfolgung
- Ein Clean-Room Approach wäre der sichere Weg
- Ohne dokumentiertes Protokoll extrem aufwendig

**Meine Empfehlung:**
1. Erst das GB/GBA-Modul umsetzen (legal, einfacher)
2. Switch-Hardware vorbereiten (FPGA-Sockel auf dem Board)
3. Auf Open-Source Firmware warten oder Clean-Room starten

---

## Ressourcen

- [SwitchBrew - Game Card](https://switchbrew.org/wiki/Gamecard) - Technische Infos
- [Lattice iCE40 Documentation](https://www.latticesemi.com/ice40)
- [MIG-Flash-PCBs](https://github.com/sabogalc/MIG-Flash-PCBs) - Hardware Referenz
