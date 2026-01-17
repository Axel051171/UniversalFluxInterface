# Nintendo Switch Cartridge Module - Vollständige Spezifikation

## Übersicht

Das Switch Cartridge Modul ermöglicht das Dumpen von Nintendo Switch Game Cards.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   ┌────────────────────────────────────────────────────────────────────────────┐        │
│   │                     SWITCH CARTRIDGE MODULE                                 │        │
│   │                                                                             │        │
│   │   ┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐      │        │
│   │   │                 │     │                 │     │                 │      │        │
│   │   │  STM32H723      │────►│    FPGA         │────►│   Cartridge     │      │        │
│   │   │  (oder RP2350)  │     │  (optional)     │     │   Slot          │      │        │
│   │   │                 │     │                 │     │                 │      │        │
│   │   └─────────────────┘     └─────────────────┘     └─────────────────┘      │        │
│   │            │                                                                │        │
│   │            ▼                                                                │        │
│   │   ┌─────────────────┐                                                       │        │
│   │   │    USB 3.0      │                                                       │        │
│   │   │    (oder 2.0)   │                                                       │        │
│   │   └─────────────────┘                                                       │        │
│   │                                                                             │        │
│   └────────────────────────────────────────────────────────────────────────────┘        │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Switch Game Card Spezifikationen

### Physische Eigenschaften

| Eigenschaft | Wert |
|-------------|------|
| Größe | 31.0 × 21.5 × 3.5 mm |
| Kontakte | 16 Pins (verdeckt) |
| Kapazität | 1GB, 2GB, 4GB, 8GB, 16GB, 32GB |
| Interface | Custom Serial Protocol |

### Pinout (inoffiziell, basierend auf Reverse Engineering)

```
            ┌───────────────────────────────────────┐
            │                                       │
    Pin 1   │ ●   ●   ●   ●   ●   ●   ●   ●       │   Pin 8
            │                                       │
    Pin 9   │ ●   ●   ●   ●   ●   ●   ●   ●       │   Pin 16
            │                                       │
            └───────────────────────────────────────┘

Pin   Signal        Beschreibung
──────────────────────────────────────────────────
 1    GND           Ground
 2    VCC           3.3V Power
 3    DAT0          Data Line 0
 4    DAT1          Data Line 1
 5    DAT2          Data Line 2
 6    DAT3          Data Line 3
 7    CLK           Clock (bis 200 MHz)
 8    CMD           Command Line
 9    GND           Ground
10    VCC           3.3V Power
11    RST           Reset
12    INT           Interrupt
13    WP            Write Protect
14    CD            Card Detect
15    GND           Ground
16    GND           Ground
```

### Kommunikationsprotokoll

Die Switch Game Card verwendet ein proprietäres Protokoll ähnlich zu eMMC:

1. **Initialisierung**
   - Card Detect prüfen
   - Reset-Sequenz
   - Card Info abfragen (CMD0, CMD1, CMD2)

2. **Authentifizierung**
   - Die Karte erwartet eine Authentifizierung
   - Ohne gültigen Schlüssel nur Header-Daten lesbar
   - **HINWEIS**: Vollständiges Dumpen erfordert Schlüssel

3. **Datenübertragung**
   - 4-bit oder 8-bit Modus
   - Clock bis 200 MHz
   - DMA-Transfer empfohlen

---

## Hardware-Optionen

### Option 1: STM32H723 (Einfach)

| Komponente | Beschreibung |
|------------|--------------|
| MCU | STM32H723 @ 550 MHz |
| Interface | SDMMC Peripheral |
| USB | USB 2.0 High-Speed |
| Geschwindigkeit | ~25 MB/s (USB 2.0 limitiert) |

**Vorteile**: Einfaches Design, keine FPGA-Kenntnisse nötig
**Nachteile**: USB 2.0 limitiert Geschwindigkeit

### Option 2: RP2350 + USB3 Chip

| Komponente | Beschreibung |
|------------|--------------|
| MCU | RP2350 @ 150 MHz |
| Interface | PIO State Machine |
| USB | USB3343 (USB 3.0 PHY) |
| Geschwindigkeit | ~100 MB/s möglich |

**Vorteile**: Günstiger, USB 3.0 möglich
**Nachteile**: Komplexere Software, USB 3.0 PHY nötig

### Option 3: FPGA + USB3 (Optimal)

| Komponente | Beschreibung |
|------------|--------------|
| FPGA | Lattice ECP5 / Gowin GW2A |
| Interface | Native 200 MHz Support |
| USB | FTDI FT601 (USB 3.0 FIFO) |
| Geschwindigkeit | ~300 MB/s möglich |

**Vorteile**: Maximale Geschwindigkeit, flexibel
**Nachteile**: Teuer, FPGA-Entwicklung nötig

---

## Empfohlenes Design: STM32H723

### Schaltplan-Überblick

```
                        ┌─────────────────────────────────────────┐
                        │           STM32H723VGT6                 │
                        │                                         │
    USB-C ◄────────────►│ USB_OTG_HS                              │
                        │                                         │
                        │ SDMMC1_D0  ◄──────────► DAT0           │
                        │ SDMMC1_D1  ◄──────────► DAT1           │◄─► Game Card
                        │ SDMMC1_D2  ◄──────────► DAT2           │    Slot
                        │ SDMMC1_D3  ◄──────────► DAT3           │
                        │ SDMMC1_CK  ◄──────────► CLK            │
                        │ SDMMC1_CMD ◄──────────► CMD            │
                        │                                         │
                        │ GPIO       ◄──────────► RST, INT, etc. │
                        │                                         │
                        └─────────────────────────────────────────┘
```

### Bill of Materials

| Ref | Komponente | Wert | Package | Preis |
|-----|------------|------|---------|-------|
| U1 | STM32H723VGT6 | - | LQFP-100 | ~8€ |
| U2 | LDO 3.3V | AP2112K-3.3 | SOT-23-5 | ~0.30€ |
| U3 | USB ESD | USBLC6-2 | SOT-23-6 | ~0.20€ |
| J1 | USB-C | TYPE-C-31-M-12 | SMD | ~0.50€ |
| J2 | Game Card Slot | Custom | - | ~5€ |
| Y1 | Crystal | 25 MHz | 3215 | ~0.30€ |
| C1-C10 | Capacitor | 100nF | 0402 | ~0.10€ |
| - | PCB (4-Layer) | - | 50x40mm | ~5€ |

**Geschätzte Kosten**: ~20€ pro Modul

---

## Firmware-Architektur

### Hauptmodule

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   ┌──────────────┐    ┌──────────────┐    ┌──────────────┐    ┌──────────────┐         │
│   │              │    │              │    │              │    │              │         │
│   │  USB CDC     │───►│  Command     │───►│  Card        │───►│  SDMMC       │         │
│   │  Interface   │    │  Handler     │    │  Protocol    │    │  Driver      │         │
│   │              │    │              │    │              │    │              │         │
│   └──────────────┘    └──────────────┘    └──────────────┘    └──────────────┘         │
│                                                                                          │
│   ┌──────────────┐    ┌──────────────┐                                                  │
│   │              │    │              │                                                  │
│   │  DMA         │    │  Error       │                                                  │
│   │  Manager     │    │  Handling    │                                                  │
│   │              │    │              │                                                  │
│   └──────────────┘    └──────────────┘                                                  │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

### USB Kommandos

| CMD | Name | Parameter | Beschreibung |
|-----|------|-----------|--------------|
| 0x01 | GET_INFO | - | Card Info lesen |
| 0x02 | GET_STATUS | - | Status abfragen |
| 0x10 | READ_SECTOR | addr, count | Sektoren lesen |
| 0x11 | READ_HEADER | - | Header lesen (ohne Auth) |
| 0x20 | SET_KEY | key[16] | Authentifizierungsschlüssel setzen |
| 0x21 | AUTHENTICATE | - | Authentifizierung durchführen |
| 0xF0 | RESET | - | Modul Reset |
| 0xFF | BOOTLOADER | - | DFU Mode |

### Datenfluss beim Dumpen

```
1. CMD GET_INFO
   ◄── Card Info (Title, Size, etc.)

2. CMD SET_KEY (optional)
   ──► key[16]
   ◄── OK/FAIL

3. CMD AUTHENTICATE
   ◄── OK/FAIL

4. CMD READ_SECTOR (repeat)
   ──► addr=0, count=1024
   ◄── data[512KB]
   ──► addr=1024, count=1024
   ◄── data[512KB]
   ... (bis Kapazität erreicht)
```

---

## Rechtliche Hinweise

⚠️ **WICHTIG**: 

1. Das Dumpen von Game Cards ist nur für Backup-Zwecke der eigenen Spiele legal
2. Die Verbreitung von Dumps ist illegal und verstößt gegen Urheberrecht
3. Die Authentifizierungsschlüssel sind urheberrechtlich geschützt
4. Dieses Modul ist NUR für Forschungs- und Backup-Zwecke gedacht

---

## Entwicklungsschritte

### Phase 1: Hardware (4 Wochen)
- [ ] Schaltplan fertigstellen
- [ ] PCB Layout (4-Layer)
- [ ] Prototyp bestellen
- [ ] Game Card Slot beschaffen

### Phase 2: Basis-Firmware (4 Wochen)
- [ ] USB CDC implementieren
- [ ] SDMMC Treiber
- [ ] Card Detect / Reset
- [ ] Header-Lesen (ohne Auth)

### Phase 3: Protokoll (8 Wochen)
- [ ] Protokoll-Analyse
- [ ] Authentifizierung verstehen
- [ ] Vollständiges Lesen
- [ ] Fehlerbehandlung

### Phase 4: Software (4 Wochen)
- [ ] PC Tool entwickeln
- [ ] GUI erstellen
- [ ] Integration mit UFI Hauptprogramm

---

## Ressourcen

- Switch Homebrew Wiki: https://switchbrew.org/
- Atmosphère (CFW): https://github.com/Atmosphere-NX/Atmosphere
- Lockpick (Key Dumper): https://github.com/shchmue/Lockpick
