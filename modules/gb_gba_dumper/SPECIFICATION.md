# GB/GBA USB Dumper Module - Vollständige Spezifikation

## Übersicht

Eigenständiges USB-Gerät zum Dumpen von Game Boy und Game Boy Advance Cartridges.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   GB/GBA USB DUMPER                                                                     │
│                                                                                          │
│   ┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐                  │
│   │                 │     │                 │     │                 │                  │
│   │     RP2040      │────►│  74LVC245 x2    │────►│  GB Cartridge   │                  │
│   │   @ 133 MHz     │     │  Level Shifter  │     │  Slot           │                  │
│   │                 │     │                 │     │                 │                  │
│   └─────────────────┘     └─────────────────┘     └─────────────────┘                  │
│            │                      │                                                     │
│            │              ┌───────┴───────┐                                            │
│            │              │               │                                            │
│            ▼              ▼               ▼                                            │
│   ┌─────────────────┐   ┌─────────────────┐                                            │
│   │    USB-C        │   │  GBA Cartridge  │                                            │
│   │    (USB 2.0)    │   │  Slot           │                                            │
│   └─────────────────┘   └─────────────────┘                                            │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Unterstützte Cartridges

### Game Boy / Game Boy Color

| MBC Typ | ROM Max | RAM Max | Besonderheiten |
|---------|---------|---------|----------------|
| No MBC | 32 KB | - | Tetris, etc. |
| MBC1 | 2 MB | 32 KB | Häufigstes MBC |
| MBC2 | 256 KB | 512x4 bit | Integriertes RAM |
| MBC3 | 2 MB | 32 KB | + RTC |
| MBC5 | 8 MB | 128 KB | Pokemon G/S/C |
| MBC6 | 1 MB | 32 KB | Selten |
| MBC7 | 2 MB | 256 bytes | Accelerometer |
| HuC1 | 2 MB | 32 KB | Hudson |
| HuC3 | 2 MB | 128 KB | Hudson + RTC |
| Camera | 1 MB | 128 KB | Pocket Camera |

### Game Boy Advance

| Typ | ROM Max | Save | Besonderheiten |
|-----|---------|------|----------------|
| Standard | 32 MB | - | Kein Save |
| SRAM | 32 MB | 32 KB | Batterie-Backup |
| EEPROM | 32 MB | 512B/8KB | Flash Save |
| Flash | 32 MB | 64KB/128KB | Flash Save |

---

## Hardware Design

### Pinout - GB Cartridge (32-Pin)

```
              ┌─────────────────────────────────────┐
              │    GB/GBC CARTRIDGE CONNECTOR       │
              │                                     │
      VCC   1 │ ●                               ● │ 32  GND
      CLK   2 │ ●                               ● │ 31  /RESET
      /WR   3 │ ●                               ● │ 30  /RD
      /CS   4 │ ●                               ● │ 29  AIN (Audio)
      A0    5 │ ●                               ● │ 28  D0
      A1    6 │ ●                               ● │ 27  D1
      A2    7 │ ●                               ● │ 26  D2
      A3    8 │ ●                               ● │ 25  D3
      A4    9 │ ●                               ● │ 24  D4
      A5   10 │ ●                               ● │ 23  D5
      A6   11 │ ●                               ● │ 22  D6
      A7   12 │ ●                               ● │ 21  D7
      A8   13 │ ●                               ● │ 20  /CS2 (RAM)
      A9   14 │ ●                               ● │ 19  A15
      A10  15 │ ●                               ● │ 18  A14
      A11  16 │ ●                               ● │ 17  A13
              │                                     │
              └─────────────────────────────────────┘
```

### Pinout - GBA Cartridge (32-Pin Edge)

```
         ┌─────────────────────────────────────────────────┐
         │           GBA CARTRIDGE CONNECTOR               │
         │                                                 │
   VCC 1 │ ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ● │ 15 GND
         │ 2  3  4  5  6  7  8  9 10 11 12 13 14        │
         │                                                 │
  GND 17 │ ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ●  ● │ 31 VCC
         │18 19 20 21 22 23 24 25 26 27 28 29 30        │
         └─────────────────────────────────────────────────┘

Pin  Signal       Pin  Signal
───────────────────────────────
 1   VCC          17   GND
 2   PHI          18   AD0
 3   /WR          19   AD1
 4   /RD          20   AD2
 5   /CS          21   AD3
 6   AD8          22   AD4
 7   AD9          23   AD5
 8   AD10         24   AD6
 9   AD11         25   AD7
10   AD12         26   A16
11   AD13         27   A17
12   AD14         28   A18
13   AD15         29   A19
14   A20          30   A20
15   GND          31   VCC
16   A21          32   /CS2
```

### RP2040 Pin-Mapping

```
RP2040 GPIO     Signal          Funktion
──────────────────────────────────────────────────
GP0-GP7         A0-A7           Adresse Low
GP8-GP15        A8-A15          Adresse High
GP16-GP23       D0-D7           Daten
GP24            /RD             Read Enable
GP25            /WR             Write Enable
GP26            /CS             Chip Select (ROM)
GP27            /CS2            Chip Select (RAM)
GP28            /RESET          Reset
```

### Level Shifter (74LVC245)

```
               ┌──────────────────┐
         DIR 1 │                  │ 20 VCC (5V)
          A1 2 │                  │ 19 /OE
          A2 3 │                  │ 18 B1
          A3 4 │    74LVC245     │ 17 B2
          A4 5 │                  │ 16 B3
          A5 6 │                  │ 15 B4
          A6 7 │                  │ 14 B5
          A7 8 │                  │ 13 B6
          A8 9 │                  │ 12 B7
    GND(3.3V)10│                  │ 11 B8
               └──────────────────┘

A-Seite: 3.3V (RP2040)
B-Seite: 5V (Cartridge)
```

---

## Bill of Materials

| Ref | Komponente | Wert | Package | Preis |
|-----|------------|------|---------|-------|
| U1 | RP2040 | - | QFN-56 | ~1€ |
| U2 | W25Q16 | 2MB Flash | SOIC-8 | ~0.30€ |
| U3-U4 | 74LVC245 | - | TSSOP-20 | 2x 0.20€ |
| U5 | LDO 3.3V | AP2112K-3.3 | SOT-23-5 | ~0.30€ |
| J1 | USB-C | TYPE-C-31-M-12 | SMD | ~0.50€ |
| J2 | GB Slot | - | Custom | ~3€ |
| J3 | GBA Slot | - | Custom | ~3€ |
| Y1 | Crystal | 12 MHz | 3215 | ~0.20€ |
| - | PCB (2-Layer) | - | 60x40mm | ~3€ |

**Geschätzte Kosten**: ~12€ pro Modul

---

## USB Protokoll

### Kommandos

| CMD | Name | Parameter | Antwort |
|-----|------|-----------|---------|
| 0x01 | GET_INFO | - | cart_info_t |
| 0x10 | READ_ROM | addr[4], len[2] | data[] |
| 0x11 | READ_RAM | len[2] | data[] |
| 0x12 | WRITE_RAM | len[2], data[] | status |
| 0x20 | DETECT | - | type, mbc |
| 0x30 | SET_MODE | mode (GB/GBA) | status |
| 0xF0 | RESET | - | status |
| 0xFF | BOOTLOADER | - | (reboot) |

### Datenstrukturen

```c
typedef struct {
    uint8_t type;           // CART_GB, CART_GBC, CART_GBA
    uint8_t mbc;            // MBC_NONE, MBC_MBC1, ...
    char title[16];         // Spiel-Titel
    uint32_t rom_size;      // ROM Größe in Bytes
    uint32_t ram_size;      // RAM Größe in Bytes
    uint8_t cgb_flag;       // GBC Kompatibilität
    uint8_t sgb_flag;       // SGB Kompatibilität
    uint16_t checksum;      // Header Checksum
} cart_info_t;
```

---

## Dump-Geschwindigkeit

| Modus | ROM Größe | Zeit | Durchsatz |
|-------|-----------|------|-----------|
| GB | 32 KB | ~0.1s | 320 KB/s |
| GB | 2 MB | ~6s | 340 KB/s |
| GB | 8 MB | ~24s | 340 KB/s |
| GBA | 4 MB | ~8s | 500 KB/s |
| GBA | 32 MB | ~64s | 500 KB/s |

Limitiert durch USB Full-Speed (12 Mbit/s) und Bus-Timing.

---

## Schaltplan

```
                     ┌─────────────────────────────────────────────────────────────────┐
                     │                                                                  │
                     │                         RP2040                                   │
                     │                                                                  │
    USB-C ◄─────────►│ USB_DP/DM                                                       │
                     │                                                                  │
                     │ GP0-GP15  ◄───────► 74LVC245 #1 ◄───────► A0-A15 (Cartridge)    │
                     │                                                                  │
                     │ GP16-GP23 ◄───────► 74LVC245 #2 ◄───────► D0-D7 (Cartridge)     │
                     │                                                                  │
                     │ GP24-GP28 ◄─────────────────────────────► Control (RD,WR,CS)    │
                     │                                                                  │
                     │ XIN/XOUT ◄───────► 12 MHz Crystal                               │
                     │                                                                  │
                     └─────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
                              ┌───────────┐
                              │  W25Q16   │
                              │  (Flash)  │
                              └───────────┘
```

---

## Software (PC)

### Python Tool

```python
#!/usr/bin/env python3
"""GB/GBA Dumper - PC Tool"""

import serial
import struct
from tqdm import tqdm

class GBDumper:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=1)
    
    def get_info(self):
        self.ser.write(bytes([0x01]))
        resp = self.ser.read(28)
        return {
            'type': resp[0],
            'mbc': resp[1],
            'title': resp[2:18].decode().rstrip('\x00'),
            'rom_size': struct.unpack('<I', resp[18:22])[0],
            'ram_size': struct.unpack('<I', resp[22:26])[0],
        }
    
    def dump_rom(self, filename):
        info = self.get_info()
        print(f"Dumping: {info['title']} ({info['rom_size']} bytes)")
        
        data = bytearray()
        addr = 0
        chunk = 4096
        
        with tqdm(total=info['rom_size']) as pbar:
            while addr < info['rom_size']:
                self.ser.write(bytes([0x10]) + 
                              struct.pack('<IH', addr, chunk))
                resp = self.ser.read(chunk + 4)
                data.extend(resp[4:])
                addr += chunk
                pbar.update(chunk)
        
        with open(filename, 'wb') as f:
            f.write(data)
        
        print(f"Saved: {filename}")

if __name__ == '__main__':
    import sys
    dumper = GBDumper(sys.argv[1])
    dumper.dump_rom(sys.argv[2])
```

---

## Build-Anleitung

### Voraussetzungen

```bash
# Pico SDK installieren
git clone https://github.com/raspberrypi/pico-sdk
cd pico-sdk
git submodule update --init
export PICO_SDK_PATH=$(pwd)
```

### Kompilieren

```bash
cd modules/gb_gba_dumper
mkdir build && cd build
cmake ..
make
```

### Flashen

1. RP2040 BOOTSEL-Taste gedrückt halten
2. USB anschließen
3. `gb_gba_dumper.uf2` auf RPI-RP2 Laufwerk kopieren

---

## Integration mit UFI

Das Modul kann standalone oder über den UFI Expansion Port betrieben werden:

### Standalone
- Eigene USB-Verbindung zum PC
- Unabhängig von UFI Hauptplatine

### Via UFI Expansion (J8)
- I2C/SPI Kommunikation mit CM5
- Stromversorgung über UFI
- Integration in UFI Web-Interface

```
UFI J8 Header    GB/GBA Module
───────────────────────────────
Pin 1  (3.3V)    VCC
Pin 2  (GND)     GND
Pin 3  (SDA)     GP4 (I2C)
Pin 4  (SCL)     GP5 (I2C)
Pin 5  (MOSI)    GP19 (SPI)
Pin 6  (MISO)    GP16 (SPI)
Pin 7  (SCK)     GP18 (SPI)
Pin 8  (CS)      GP17 (SPI)
Pin 9  (INT)     GP22
Pin 10 (GND)     GND
```
