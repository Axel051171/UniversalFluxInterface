# UFI Modular - Design Dokumentation

## Übersicht

UFI Modular ist die modulare Version des Universal Flux Interface mit:
- **Pin-Header für Amiga** statt fester DB-23 Buchse
- **Module Port** für externe MIG/GB/GBA Dumper
- **USB-Only Betrieb** möglich (ohne CM5)

---

## Board-Spezifikationen

| Eigenschaft | Wert |
|-------------|------|
| Größe | 110mm × 85mm |
| Fläche | 93.5 cm² |
| Layer | 4 (Signal/GND/PWR/Signal) |
| Finish | ENIG |
| Min. Track | 0.15mm |
| Min. Space | 0.15mm |

---

## System-Architektur

```
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│   [PC - Hauptprogramm]                                      │
│     │  GUI, finale Image-Erstellung, Archiv                │
│     │                                                       │
│     └── GbE ──► [CM5 - Verarbeitung]                       │
│                   │  Puffern, Routinen, Algorithmen,       │
│                   │  Verbessern, Qualitätsanalyse          │
│                   │                                         │
│                   └── USB HS ──► [STM32 - Flux Engine]     │
│                                    │  Timing-Erfassung,    │
│                                    │  Laufwerk-Steuerung   │
│                                    │                        │
│                                    └──► FDD/Amiga/IEC      │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

### Aufgabenverteilung

| Komponente | Aufgabe |
|------------|---------|
| **STM32** | Flux-Timing erfassen (25ns), Laufwerk steuern |
| **CM5** | Puffern, Routinen, Algorithmen, Daten verbessern |
| **PC** | Hauptprogramm - GUI, finale Erstellung, Archiv |

---

## Connector-Übersicht

### J1, J2: FDD 34-pin Shugart
Standard PC-Floppy Anschluss für 3.5" und 5.25" Laufwerke.

### J3: Apple Disk II 19-pin
Für Apple II Disk II Laufwerke.

### J4: Amiga FDD Header (2×12 Pin)
**NEU: Pin-Header statt DB-23!**

| Pin | Signal | Pin | Signal |
|-----|--------|-----|--------|
| 1 | RDY | 2 | GND |
| 3 | DKCH | 4 | GND |
| 5 | GND | 6 | GND |
| 7 | GND | 8 | GND |
| 9 | GND | 10 | GND |
| 11 | GND | 12 | GND |
| 13 | /MTRX | 14 | /SEL0 |
| 15 | /SEL1 | 16 | /SIDE |
| 17 | RDATA | 18 | /STEP |
| 19 | /DIR | 20 | /WDATA |
| 21 | /WGATE | 22 | /TRK0 |
| 23 | /WPROT | 24 | INDEX |

**Adapter-Kabel**: 2×12 Header → DB-23 Male für Amiga-Laufwerke.

### J5: IEC DIN-6
C64/C128 Serial Bus für 1541/1571/1581 Laufwerke.

### J14: Module Port (2×5 Pin)
**NEU: Universeller Anschluss für externe Module!**

| Pin | Signal | Beschreibung |
|-----|--------|--------------|
| 1 | +5V | Stromversorgung 5V |
| 2 | +3V3 | Stromversorgung 3.3V |
| 3 | USB_D+ | USB Data+ |
| 4 | USB_D- | USB Data- |
| 5 | SDA | I2C Data |
| 6 | SCL | I2C Clock |
| 7 | MOSI | SPI Data Out |
| 8 | SCK | SPI Clock |
| 9 | GND | Ground |
| 10 | GND | Ground |

Unterstützte Module:
- MIG Dumper (Nintendo Switch)
- GB/GBA USB Dumper
- Weitere über Expansion

### J15: USB-A (Module)
Alternative zum Pin-Header - direkter USB-Anschluss für Module.

### J9: USB-C (Debug/Firmware)
**Nur für Entwicklung & Firmware-Update!**
- STM32 DFU Mode (Firmware-Update)
- UART über USB (Debug-Console)
- Nicht für normalen Betrieb

---

## Pinout-Diagramme

### Amiga Header (J4)
```
        Pin 1                    Pin 23
          ▼                        ▼
    ┌─────────────────────────────────┐
    │ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ │ Row 1 (odd)
    │ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ ○ │ Row 2 (even)
    └─────────────────────────────────┘
          ▲                        ▲
        Pin 2                    Pin 24
```

### Module Port (J14)
```
      Pin 1  Pin 3  Pin 5  Pin 7  Pin 9
        ▼      ▼      ▼      ▼      ▼
    ┌─────────────────────────────────┐
    │  ■     ○     ○     ○     ○     │ 5V  D+  SDA MOSI GND
    │  ○     ○     ○     ○     ○     │ 3V3 D-  SCL SCK  GND
    └─────────────────────────────────┘
        ▲      ▲      ▲      ▲      ▲
      Pin 2  Pin 4  Pin 6  Pin 8  Pin 10
```

---

## Adapter-Kabel

### Amiga DB-23 Adapter
```
UFI Header (2×12)          DB-23 Male
      ┌─────┐              ┌───────────────┐
      │ 1   │──────────────│ 1  RDY        │
      │ 3   │──────────────│ 2  DKCH       │
      │ 13  │──────────────│ 3  /MTRX      │
      │ ... │              │ ...           │
      └─────┘              └───────────────┘
```

### MIG/GB/GBA Modul-Kabel
```
Module Port (2×5)          Modul-Board
      ┌─────┐              ┌─────────────┐
      │ 1,2 │──── +5V/3V3 ─│ Power       │
      │ 3,4 │──── USB ─────│ USB Port    │
      │ 5,6 │──── I2C ─────│ EEPROM      │
      │ 7,8 │──── SPI ─────│ Flash       │
      │9,10 │──── GND ─────│ Ground      │
      └─────┘              └─────────────┘
```

---

## Vorteile der modularen Architektur

| Aspekt | Vorteil |
|--------|---------|
| **Kosten** | Kein teurer DB-23 Connector (~€2.50 gespart) |
| **Flexibilität** | Eigenes Kabel möglich, verschiedene Stecker |
| **USB-Only** | Funktioniert ohne CM5 wie Greaseweazle |
| **Module** | Externe Dumper einfach anschließbar |
| **Platz** | Pin-Header kleiner als D-Sub Buchsen |

---

## Kompatible Module

### Integriert über Module Port:
1. **MIG Dumper** - Nintendo Switch Cartridges
2. **GB/GBA Dumper** - Game Boy / Game Boy Advance
3. **Custom Module** - Eigene Entwicklungen

### Über 40-pin Expansion (J13):
1. **NES/Famicom Dumper**
2. **SNES Dumper**
3. **Mega Drive/Genesis Dumper**
4. **Weitere Systeme**

---

## Stückliste (Änderungen)

| Alt (Headless) | Neu (Modular) | Diff |
|----------------|---------------|------|
| DB-23 Buchse | 2×12 Header | −€2.50 |
| - | 2×5 Header | +€0.30 |
| - | USB-A Buchse | +€0.50 |
| 1× USB-C | 2× USB-C | +€0.40 |
| **Gesamt** | | **−€1.30** |

---

## Einzelpreis

```
┌─────────────────────────────────────────────────────────────┐
│           UFI MODULAR - EINZELPREIS (5er Serie)             │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   PCB (4-Layer, ENIG)               €6.60                   │
│   SMT Assembly                       €8.50                   │
│   SMD Bauteile                      €15.00                   │
│   THT Connectors                    €12.00  (−€2.50 DB-23)  │
│   Shipping                           €5.00                   │
│   ─────────────────────────────────────────                 │
│                                                             │
│   BOARD (ohne CM5):                ~€47                     │
│                                                             │
│   + CM5 4GB:                       +€70                     │
│   ─────────────────────────────────────────                 │
│                                                             │
│   ══════════════════════════════════════════                │
│   KOMPLETTSYSTEM:                 ~€117                     │
│   ══════════════════════════════════════════                │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```
