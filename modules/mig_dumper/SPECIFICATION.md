# MIG (Mask ROM / IGEL) Dumper Module - Vollständige Spezifikation

## Übersicht

Das MIG Dumper Modul ermöglicht das Dumpen von Mask ROMs und programmierbaren EPROMs, wie sie in älteren Arcade-Platinen, Synthesizern und Industriesteuerungen verwendet werden.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   MIG DUMPER MODULE                                                                     │
│                                                                                          │
│   ┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐                  │
│   │                 │     │                 │     │                 │                  │
│   │  STM32G0B1      │────►│  Level Shifter  │────►│   ZIF Socket    │                  │
│   │  (oder RP2040)  │     │  74LVC245       │     │   40-Pin        │                  │
│   │                 │     │                 │     │                 │                  │
│   └─────────────────┘     └─────────────────┘     └─────────────────┘                  │
│            │                                                                            │
│            ▼                                                                            │
│   ┌─────────────────┐                                                                   │
│   │    USB-C        │                                                                   │
│   │    (CDC)        │                                                                   │
│   └─────────────────┘                                                                   │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Unterstützte ROM-Typen

### Parallel ROMs

| Typ | Größe | Pins | Adresse | Daten |
|-----|-------|------|---------|-------|
| 2716 | 2KB | 24 | A0-A10 | D0-D7 |
| 2732 | 4KB | 24 | A0-A11 | D0-D7 |
| 2764 | 8KB | 28 | A0-A12 | D0-D7 |
| 27128 | 16KB | 28 | A0-A13 | D0-D7 |
| 27256 | 32KB | 28 | A0-A14 | D0-D7 |
| 27512 | 64KB | 28 | A0-A15 | D0-D7 |
| 27C010 | 128KB | 32 | A0-A16 | D0-D7 |
| 27C020 | 256KB | 32 | A0-A17 | D0-D7 |
| 27C040 | 512KB | 32 | A0-A18 | D0-D7 |
| 27C080 | 1MB | 32 | A0-A19 | D0-D7 |
| 27C160 | 2MB | 42 | A0-A20 | D0-D15 |
| 27C322 | 4MB | 42 | A0-A21 | D0-D15 |

### Mask ROMs (Arcade/Konsolen)

| Typ | Verwendung | Pins |
|-----|------------|------|
| OKI M27C160 | Neo Geo | 42 |
| Fujitsu MB834000 | CPS2 | 40 |
| Sharp LH534xxx | verschiedene | 40 |
| NEC µPD27C4096 | Sega | 40 |

---

## Hardware Design

### MCU-Auswahl

**Option 1: STM32G0B1** (empfohlen)
- 64 MHz Cortex-M0+
- Genug GPIO für 32-Pin ROMs
- USB Full-Speed
- Preis: ~3€

**Option 2: RP2040**
- 133 MHz Dual-Core
- Mehr GPIO (30 verfügbar)
- USB Full-Speed
- Preis: ~1€

### Pin-Mapping (STM32G0B1)

```
                   STM32G0B1CBT6 (LQFP-48)
                   
    A0-A7   ◄────►  PA0-PA7    (8 Pins)
    A8-A15  ◄────►  PB0-PB7    (8 Pins)
    A16-A19 ◄────►  PC0-PC3    (4 Pins)
    
    D0-D7   ◄────►  PA8-PA15   (8 Pins)
    D8-D15  ◄────►  PB8-PB15   (8 Pins, für 16-bit ROMs)
    
    /CE     ◄────►  PC4
    /OE     ◄────►  PC5
    VPP     ◄────►  PC6  (via DAC oder PWM)
    
    USB_DM  ◄────►  PA11
    USB_DP  ◄────►  PA12
```

### ZIF Socket Pinout (40-Pin Universal)

```
                ┌─────────────────────┐
        VPP   1 │ ●                 ● │ 40  VCC
        A16   2 │ ●                 ● │ 39  A17
        A15   3 │ ●                 ● │ 38  A18
        A12   4 │ ●                 ● │ 37  A19
        A7    5 │ ●                 ● │ 36  A20 (42-pin)
        A6    6 │ ●                 ● │ 35  /CE
        A5    7 │ ●                 ● │ 34  D15 (16-bit)
        A4    8 │ ●                 ● │ 33  D14
        A3    9 │ ●                 ● │ 32  D13
        A2   10 │ ●                 ● │ 31  D12
        A1   11 │ ●                 ● │ 30  D11
        A0   12 │ ●                 ● │ 29  D10
        D0   13 │ ●                 ● │ 28  D9
        D1   14 │ ●                 ● │ 27  D8
        D2   15 │ ●                 ● │ 26  /OE
        GND  16 │ ●                 ● │ 25  A10
        D3   17 │ ●                 ● │ 24  A11
        D4   18 │ ●                 ● │ 23  A9
        D5   19 │ ●                 ● │ 22  A8
        D6   20 │ ●                 ● │ 21  D7
                └─────────────────────┘
```

### Schaltplan

```
    ┌─────────────────────────────────────────────────────────────────────────────────────┐
    │                                                                                      │
    │    ┌────────────┐        ┌────────────┐        ┌────────────────────────────┐       │
    │    │            │        │            │        │                            │       │
    │    │  USB-C     │───────►│  STM32G0B1 │───────►│  74LVC245 x3              │       │
    │    │            │        │            │        │  (Level Shifter)          │       │
    │    └────────────┘        └────────────┘        └─────────────┬──────────────┘       │
    │                                │                              │                      │
    │                                │ VPP Control                  │                      │
    │                                ▼                              ▼                      │
    │                          ┌────────────┐              ┌────────────────────┐         │
    │                          │            │              │                    │         │
    │                          │  VPP Gen   │              │   ZIF-40 Socket    │         │
    │                          │  (12V/25V) │──────────────►│   + Adapter        │         │
    │                          │            │              │                    │         │
    │                          └────────────┘              └────────────────────┘         │
    │                                                                                      │
    └─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Firmware

### ROM-Lese-Algorithmus

```c
/**
 * Universal ROM Read
 * 
 * @param addr      Start-Adresse
 * @param buffer    Ziel-Buffer
 * @param size      Anzahl Bytes
 * @param data_width 8 oder 16 bit
 */
void rom_read(uint32_t addr, uint8_t* buffer, uint32_t size, uint8_t data_width) {
    // CE und OE aktivieren
    gpio_put(PIN_CE, 0);
    gpio_put(PIN_OE, 0);
    
    for (uint32_t i = 0; i < size; ) {
        // Adresse setzen
        set_address(addr);
        
        // Warten auf Daten (tACC = 70-200ns typisch)
        delay_ns(200);
        
        // Daten lesen
        if (data_width == 16) {
            uint16_t data = read_data_16bit();
            buffer[i++] = data & 0xFF;
            buffer[i++] = (data >> 8) & 0xFF;
            addr++;
        } else {
            buffer[i++] = read_data_8bit();
            addr++;
        }
    }
    
    // CE und OE deaktivieren
    gpio_put(PIN_CE, 1);
    gpio_put(PIN_OE, 1);
}
```

### USB Kommandos

| CMD | Name | Parameter | Beschreibung |
|-----|------|-----------|--------------|
| 0x01 | DETECT | - | ROM-Typ erkennen |
| 0x02 | SET_TYPE | type, pins, width | ROM-Typ manuell setzen |
| 0x10 | READ | addr[4], len[4] | ROM lesen |
| 0x11 | READ_ALL | - | Komplettes ROM dumpen |
| 0x20 | VERIFY | crc32[4] | CRC prüfen |
| 0x30 | SET_VPP | voltage | VPP Spannung setzen (für EPROM) |
| 0xF0 | RESET | - | Reset |

### ROM-Typ Erkennung

```c
typedef struct {
    const char* name;
    uint32_t size;          // Bytes
    uint8_t pins;           // 24, 28, 32, 40, 42
    uint8_t addr_bits;      // 11-21
    uint8_t data_bits;      // 8 oder 16
    bool has_a_minus_1;     // A-1 Pin (manche 16-bit ROMs)
} rom_type_t;

static const rom_type_t rom_types[] = {
    {"2716",    2048,    24, 11, 8,  false},
    {"2732",    4096,    24, 12, 8,  false},
    {"2764",    8192,    28, 13, 8,  false},
    {"27128",   16384,   28, 14, 8,  false},
    {"27256",   32768,   28, 15, 8,  false},
    {"27512",   65536,   28, 16, 8,  false},
    {"27C010",  131072,  32, 17, 8,  false},
    {"27C020",  262144,  32, 18, 8,  false},
    {"27C040",  524288,  32, 19, 8,  false},
    {"27C080",  1048576, 32, 20, 8,  false},
    {"27C160",  2097152, 42, 20, 16, true},
    {"27C322",  4194304, 42, 21, 16, true},
    {NULL, 0, 0, 0, 0, false}
};

rom_type_t* detect_rom_type(void) {
    // Verschiedene Adress-Bits testen
    // Wenn Daten sich wiederholen, ist Adresse kleiner
    
    for (int i = 0; rom_types[i].name != NULL; i++) {
        // Test...
    }
    return NULL;
}
```

---

## PC Software (Python)

```python
#!/usr/bin/env python3
"""
MIG Dumper - PC Tool
"""

import serial
import struct
import time
from tqdm import tqdm

class MIGDumper:
    CMD_DETECT = 0x01
    CMD_SET_TYPE = 0x02
    CMD_READ = 0x10
    CMD_READ_ALL = 0x11
    CMD_VERIFY = 0x20
    
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=1)
        
    def detect(self):
        """ROM-Typ erkennen"""
        self.ser.write(bytes([self.CMD_DETECT]))
        resp = self.ser.read(32)
        if len(resp) < 2:
            return None
        
        return {
            'type': resp[0],
            'pins': resp[1],
            'size': struct.unpack('<I', resp[2:6])[0],
            'name': resp[6:].decode('utf-8').rstrip('\x00')
        }
    
    def read_all(self, filename, progress=True):
        """Komplettes ROM dumpen"""
        info = self.detect()
        if not info:
            raise Exception("Kein ROM erkannt")
        
        print(f"ROM: {info['name']} ({info['size']} bytes)")
        
        self.ser.write(bytes([self.CMD_READ_ALL]))
        
        data = bytearray()
        chunk_size = 4096
        
        with tqdm(total=info['size'], disable=not progress) as pbar:
            while len(data) < info['size']:
                chunk = self.ser.read(min(chunk_size, info['size'] - len(data)))
                if not chunk:
                    raise Exception("Timeout beim Lesen")
                data.extend(chunk)
                pbar.update(len(chunk))
        
        with open(filename, 'wb') as f:
            f.write(data)
        
        print(f"Gespeichert: {filename}")
        return data
    
    def verify(self, data):
        """CRC32 prüfen"""
        import binascii
        crc = binascii.crc32(data) & 0xFFFFFFFF
        
        self.ser.write(bytes([self.CMD_VERIFY]) + struct.pack('<I', crc))
        resp = self.ser.read(1)
        
        return resp[0] == 0x00

if __name__ == '__main__':
    import sys
    
    if len(sys.argv) < 3:
        print("Usage: mig_dumper.py <port> <output.bin>")
        sys.exit(1)
    
    dumper = MIGDumper(sys.argv[1])
    dumper.read_all(sys.argv[2])
```

---

## Bill of Materials

| Ref | Komponente | Wert | Package | Preis |
|-----|------------|------|---------|-------|
| U1 | STM32G0B1CBT6 | - | LQFP-48 | ~3€ |
| U2-U4 | 74LVC245 | - | TSSOP-20 | 3x 0.20€ |
| U5 | LDO 3.3V | AP2112K-3.3 | SOT-23-5 | ~0.30€ |
| U6 | VPP Boost | MT3608 | SOT-23-6 | ~0.50€ |
| J1 | USB-C | TYPE-C-31-M-12 | SMD | ~0.50€ |
| J2 | ZIF-40 | - | TH | ~3€ |
| Y1 | Crystal | 8 MHz | 3215 | ~0.20€ |
| - | PCB (2-Layer) | - | 80x50mm | ~3€ |

**Geschätzte Kosten**: ~12€ pro Modul

---

## Adapter für verschiedene ROM-Größen

Da ZIF-40 nicht alle ROMs direkt aufnehmen kann, werden Adapter benötigt:

### 24-Pin Adapter (2716/2732)

```
ZIF-40        24-Pin ROM
──────────────────────────
Pin 8    ──►  Pin 1  (A7)
Pin 9    ──►  Pin 2  (A6)
...
Pin 16   ──►  Pin 12 (GND)
Pin 20   ──►  Pin 24 (VCC)
```

### 28-Pin Adapter (2764-27512)

```
ZIF-40        28-Pin ROM
──────────────────────────
Pin 7    ──►  Pin 1  (VPP)
Pin 8    ──►  Pin 2  (A12)
...
```

### 42-Pin Adapter (27C160/27C322)

Benötigt speziellen 42-Pin Adapter oder zweites ZIF-Socket.

---

## Integration mit UFI

Das MIG Modul kann als eigenständiges USB-Gerät oder über den UFI Expansion Port betrieben werden:

### Standalone (USB)
- Direkter Anschluss an PC
- Eigene Stromversorgung über USB

### Via UFI Expansion
- I2C/SPI Kommunikation
- Stromversorgung über UFI
- Integration in UFI Web-UI

---

## Entwicklungsschritte

- [x] Spezifikation
- [ ] Schaltplan
- [ ] PCB Layout
- [ ] Prototyp
- [ ] Firmware
- [ ] PC Tool
- [ ] Adapter-Design
- [ ] Dokumentation
