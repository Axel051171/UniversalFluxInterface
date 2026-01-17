# MIG Switch/Dumper System - Analyse

## Was wir haben

### 1. MigDumpTool Source Code (v0.0.2)
**Typ:** Nintendo Switch Homebrew Application (NRO)
**Läuft auf:** Gehackte Nintendo Switch mit Custom Firmware
**Funktion:** Nutzt die offizielle Switch Gamecard API zum Dumpen

```
┌─────────────────────────────────────────────────────────────┐
│               Nintendo Switch (mit CFW)                      │
│                                                              │
│   ┌─────────────────┐       ┌─────────────────┐            │
│   │   MigDumpTool   │       │    Gamecard     │            │
│   │   (Homebrew)    │◄─────►│     Slot        │            │
│   └────────┬────────┘       └─────────────────┘            │
│            │                                                │
│            │ USB                                            │
│            ▼                                                │
│   ┌─────────────────┐                                      │
│   │    USB Host     │──────► PC (empfängt Dump)            │
│   └─────────────────┘                                      │
└─────────────────────────────────────────────────────────────┘
```

**Wichtige Dateien:**
- `gamecard.h` - Switch Gamecard Datenstrukturen (XCI Format)
- `dumper.c` - Dump-Logik für XCI, Certificate, Initial Data
- `usb.h` - USB-Transfer zum PC

### 2. MIG Firmware (update.s2 v1.2.3.1)
**Typ:** Verschlüsselte Binärdatei
**Größe:** 135.168 Bytes (132 KB)
**Ziel-Hardware:** MIG Dumper (ESP32-S2 + Lattice FPGA)

```
update.s2 (verschlüsselt)
├── ESP32-S2 Firmware (verschlüsselt)
├── Lattice FPGA Bitstream (verschlüsselt)  
└── Möglicherweise Konfig-Daten
```

**Analyse-Ergebnis:**
- ❌ Keine erkennbaren Header oder Magic Bytes
- ❌ Keine lesbaren Strings
- ❌ Hohe Entropie (komplett verschlüsselt)
- ❌ Keine Standard-Firmware-Formate erkennbar

---

## MIG Dumper Hardware-Architektur

Basierend auf öffentlichen Informationen:

```
┌─────────────────────────────────────────────────────────────┐
│                    MIG Dumper PCB                            │
│                                                              │
│   ┌─────────────────┐       ┌─────────────────┐            │
│   │   ESP32-S2      │◄─────►│  Lattice FPGA   │            │
│   │   (Controller)  │  SPI  │  (Card Protocol)│            │
│   └────────┬────────┘       └────────┬────────┘            │
│            │                         │                      │
│            │ USB                     │ 16-pin               │
│            ▼                         ▼                      │
│   ┌─────────────────┐       ┌─────────────────┐            │
│   │    USB-C        │       │  Switch Game    │            │
│   │    Port         │       │  Card Slot      │            │
│   └─────────────────┘       └─────────────────┘            │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

**Komponenten:**
- **ESP32-S2:** Espressif WiFi/BT MCU mit USB OTG
- **Lattice FPGA:** Vermutlich iCE40 oder ECP5 für Timing-kritisches Gamecard-Protokoll
- **Game Card Slot:** 16-pin Nintendo Switch Cartridge Connector

---

## Was gedumpt wird (MigDumpTool)

| Datei | Größe | Beschreibung |
|-------|-------|--------------|
| `{Game}.xci` | 1-32 GB | Komplettes Gamecard Image |
| `{Game} (Initial Data).bin` | 0x200 (512 B) | Verschlüsselte Titlekeys |
| `{Game} (Certificate).bin` | 0x200 (512 B) | Gamecard Zertifikat |
| `{Game} (Card ID Set).bin` | ~64 B | Hardware IDs |
| `{Game} (Card UID).bin` | 0x40 (64 B) | Unique Card ID |

**Wichtig:** Das Zertifikat wird im XCI mit 0xFF überschrieben (Zeile 224 in dumper.c):
```c
if (offset == 0) memset(buf + GAMECARD_CERTIFICATE_OFFSET, 0xFF, sizeof(FsGameCardCertificate));
```

---

## Möglichkeiten für UFI-Integration

### Option A: MIG Dumper Hardware nachbauen (SCHWIERIG)

**Voraussetzungen:**
1. ESP32-S2 DevKit oder Custom PCB
2. Lattice FPGA (iCE40UP5K oder ECP5)
3. Level Shifter (3.3V ↔ 1.8V)
4. Switch Game Card Connector

**Problem: Firmware**
- Die update.s2 ist verschlüsselt
- Kein bekannter Schlüssel
- Reverse Engineering wäre nötig
- Rechtlich sehr problematisch (Nintendo-IP)

### Option B: Existierenden MIG Dumper als USB-Gerät nutzen (MÖGLICH)

Der MIG Dumper erscheint als **USB Mass Storage**. Man könnte:

```
UFI ◄─── USB ───► MIG Dumper ◄─── Cartridge
         Host          Device
```

**Workflow:**
1. MIG Dumper an UFI (CM5) anschließen
2. Erscheint als `/dev/sdX` mit FAT-Filesystem
3. Game dumpen direkt in MIG's internen Speicher
4. Files auf UFI kopieren

**UFI Software könnte:**
- MIG Dumper erkennen (USB VID/PID)
- Automatisch mounten
- Dump-Workflow vereinfachen
- Mit Flux-Dumps kombinieren (für Retro-Disks)

### Option C: Clean-Room FPGA Implementation (SEHR SCHWIERIG)

Ohne Nintendo-IP ein eigenes Protokoll implementieren:

1. Logikanalysator an echte Switch + Cartridge
2. Protokoll dokumentieren
3. Verilog/VHDL für FPGA schreiben
4. Eigene ESP32 Firmware

**Geschätzter Aufwand:** 6-12 Monate, hohes Rechtsrisiko

---

## Empfehlung für UFI

### Kurzfristig: MIG Dumper als externes Gerät unterstützen

```c
// UFI kann MIG Dumper als USB-Gerät erkennen
#define MIG_DUMPER_VID  0x????  // Muss ermittelt werden
#define MIG_DUMPER_PID  0x????

bool ufi_detect_mig_dumper(void) {
    // USB-Enumeration prüfen
    // MIG erscheint als Mass Storage
}
```

### Langfristig: Modulares Cartridge-System

```
UFI Mainboard
     │
     ├── 40-pin Expansion Header
     │        │
     │        ├──► GB/GBA Module (JETZT - einfach, legal)
     │        │
     │        ├──► NES/SNES Module (SPÄTER - parallel ROM)
     │        │
     │        └──► Switch Module (VIELLEICHT - FPGA nötig)
     │                │
     │                └── Benötigt: Open-Source Firmware
     │                    oder legale Reverse-Engineering
```

---

## Technische Details aus dem Source Code

### GameCard Header Struktur (0x200 bytes)

```c
typedef struct {
    u8 signature[0x100];           // RSA-2048-PSS
    u32 magic;                     // "HEAD" = 0x48454144
    u32 rom_area_start_page;       // × 0x200
    u32 backup_area_start_page;    // 0xFFFFFFFF
    GameCardKeyIndex key_index;
    u8 rom_size;                   // GameCardRomSize enum
    u8 version;                    // 0x00
    u8 flags;                      // GameCardFlags
    u8 package_id[0x8];            // Challenge-Response
    u32 valid_data_end_page;
    u8 reserved[0x4];
    u8 card_info_iv[0x10];         // AES-128-CBC IV
    u64 partition_fs_header_addr;
    u64 partition_fs_header_size;
    u8 partition_fs_header_hash[0x20];
    u8 initial_data_hash[0x20];
    u32 sel_sec;
    u32 sel_t1_key;                // 0x02
    u32 sel_key;                   // 0x00
    u32 lim_area_page;
    GameCardInfo card_info;        // Verschlüsselt
} GameCardHeader;
```

### ROM Größen

```c
typedef enum {
    GameCardRomSize_1GiB  = 0xFA,
    GameCardRomSize_2GiB  = 0xF8,
    GameCardRomSize_4GiB  = 0xF0,
    GameCardRomSize_8GiB  = 0xE0,
    GameCardRomSize_16GiB = 0xE1,
    GameCardRomSize_32GiB = 0xE2
} GameCardRomSize;
```

---

## Fazit

| Aspekt | Status |
|--------|--------|
| MigDumpTool Source | ✅ Verfügbar, GPL-3.0 |
| MIG Firmware | ❌ Verschlüsselt, nicht nutzbar |
| Hardware-Protokoll | ❌ Undokumentiert |
| Legal für UFI | ⚠️ Risikoreich ohne Clean-Room |

**Praktischer Weg für UFI:**
1. MIG Dumper als externes USB-Gerät unterstützen
2. GB/GBA Modul als primäres Cartridge-Feature
3. Switch-Support nur mit legaler Open-Source Firmware

---

## Referenzen

- [MigDumpTool Source](https://migswitch.com) - GPL-3.0
- [SwitchBrew Gamecard](https://switchbrew.org/wiki/Gamecard)
- [MIG-Flash-PCBs](https://github.com/sabogalc/MIG-Flash-PCBs)
- [ESP32-S2 Documentation](https://docs.espressif.com/projects/esp-idf/en/latest/esp32s2/)
