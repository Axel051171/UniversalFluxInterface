# UFI Module

Erweiterungsmodule für das UFI System.

## Übersicht

| Modul | MCU | Status | Beschreibung |
|-------|-----|--------|--------------|
| GB/GBA Dumper | RP2040 | ✅ Spec + Code | Game Boy / GBA Cartridges |
| MIG Dumper | STM32G0B1 | ✅ Spec + Code | EPROM / Mask ROM (2716-27C322) |
| Switch Cartridge | STM32H723 | ✅ Spec | Nintendo Switch Game Cards |

---

## GB/GBA USB Dumper

**Standalone USB-Gerät für Game Boy / GBA Cartridge Dumping**

### Features
- Original Game Boy (DMG)
- Game Boy Color
- Game Boy Advance
- Alle MBC-Typen (MBC1-MBC7, HuC1, HuC3, etc.)
- ROM Dump (bis 32 MB)
- RAM/Save Dump & Write
- RTC Backup (MBC3)

### Hardware
- RP2040 @ 133 MHz
- USB 2.0 (Full Speed)
- 74LVC245 Level-Shifter (3.3V ↔ 5V)
- GB + GBA Cartridge Slots

### Dateien
```
gb_gba_dumper/
├── SPECIFICATION.md    # Vollständige Spezifikation
├── CMakeLists.txt      # Build System
├── src/
│   └── main.c          # Firmware
└── include/
    └── tusb_config.h   # USB Config
```

### Kosten
~15€ pro Modul

---

## MIG Dumper

**Universal EPROM / Mask ROM Dumper**

### Features
- 2716 bis 27C322 (2 KB - 4 MB)
- 8-bit und 16-bit ROMs
- 24 bis 42 Pin ZIF Socket
- Automatische ROM-Erkennung
- CRC32 Verifikation

### Unterstützte ROMs
| Typ | Größe | Pins | Datenbreite |
|-----|-------|------|-------------|
| 2716 | 2 KB | 24 | 8-bit |
| 2732 | 4 KB | 24 | 8-bit |
| 2764 | 8 KB | 28 | 8-bit |
| 27128 | 16 KB | 28 | 8-bit |
| 27256 | 32 KB | 28 | 8-bit |
| 27512 | 64 KB | 28 | 8-bit |
| 27C010 | 128 KB | 32 | 8-bit |
| 27C020 | 256 KB | 32 | 8-bit |
| 27C040 | 512 KB | 32 | 8-bit |
| 27C080 | 1 MB | 32 | 8-bit |
| 27C160 | 2 MB | 42 | 16-bit |
| 27C322 | 4 MB | 42 | 16-bit |

### Hardware
- STM32G0B1 @ 64 MHz
- USB 2.0 (Full Speed)
- 74LVC245 Level-Shifter
- ZIF-40 Socket

### Dateien
```
mig_dumper/
├── SPECIFICATION.md    # Vollständige Spezifikation
├── CMakeLists.txt      # Build System
└── src/
    └── main.c          # Firmware
```

### Kosten
~12€ pro Modul

---

## Switch Cartridge Module

**Nintendo Switch Game Card Dumper**

### Features
- Switch Game Cards (1-32 GB)
- Header-Lesen ohne Authentifizierung
- Vollständiges Dumpen (mit Schlüssel)
- SDMMC Interface

### Hardware-Optionen
1. **STM32H723** - Einfach, USB 2.0
2. **RP2350 + USB3** - Schneller, komplexer
3. **FPGA + FT601** - Maximale Geschwindigkeit

### Dateien
```
switch_cartridge/
└── SPECIFICATION.md    # Vollständige Spezifikation
```

### Status
⚠️ **Nur Spezifikation** - Firmware nicht implementiert

### Rechtliche Hinweise
- Nur für Backup eigener Spiele
- Keine Verbreitung von Dumps
- Authentifizierungsschlüssel sind geschützt

---

## Integration mit UFI

Alle Module können:
1. **Standalone** - Direkt an PC via USB
2. **Via UFI Expansion** - Über UFI Mainboard

### Standalone Modus
- Eigene USB-Verbindung
- Eigene Stromversorgung
- Unabhängig von UFI

### UFI Integration
- Kommunikation über Expansion Header
- Stromversorgung durch UFI
- Integration in UFI Web-UI
- Zentrale Verwaltung

---

## Entwicklung

### Voraussetzungen

**GB/GBA Dumper (RP2040):**
```bash
# Pico SDK installieren
git clone https://github.com/raspberrypi/pico-sdk.git
export PICO_SDK_PATH=/path/to/pico-sdk

# Build
cd gb_gba_dumper
mkdir build && cd build
cmake ..
make
```

**MIG Dumper (STM32G0):**
```bash
# STM32CubeG0 installieren
# ARM GCC Toolchain installieren

cd mig_dumper
mkdir build && cd build
cmake ..
make
```

### Flash

**RP2040:**
```bash
# BOOTSEL gedrückt halten + Reset
# UF2 Datei auf RPI-RP2 kopieren
cp build/gb_gba_dumper.uf2 /media/RPI-RP2/
```

**STM32G0:**
```bash
# ST-Link
st-flash write build/mig_dumper.bin 0x08000000

# DFU (USB)
dfu-util -a 0 -s 0x08000000:leave -D build/mig_dumper.bin
```

---

## Roadmap

### Kurzfristig
- [ ] GB/GBA PCB Layout
- [ ] MIG PCB Layout
- [ ] Prototypen bestellen

### Mittelfristig
- [ ] Switch Firmware entwickeln
- [ ] PC Tools vereinheitlichen
- [ ] Web-UI Integration

### Langfristig
- [ ] Weitere Module (N64, SNES, etc.)
- [ ] Massenproduktion
- [ ] Dokumentation vervollständigen
