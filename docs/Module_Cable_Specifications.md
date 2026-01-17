# UFI Modul-Kabel Spezifikationen

## Übersicht

Kabel-Spezifikationen für alle UFI-Erweiterungsmodule.

---

## 1. GB/GBA USB Dumper Modul

### Verbindung: USB-C

Das GB/GBA Modul ist ein **eigenständiges USB-Gerät** und benötigt nur ein Standard USB-C Kabel.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   GB/GBA Modul                                    PC / UFI CM5                          │
│   ┌──────────────┐                               ┌──────────────┐                       │
│   │              │      USB-C Kabel              │              │                       │
│   │   USB-C      │◄────────────────────────────►│    USB-A     │                       │
│   │              │      (Standard)               │    USB-C     │                       │
│   └──────────────┘                               └──────────────┘                       │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

**Kabel-Anforderungen:**
- USB 2.0 High-Speed kompatibel
- Länge: max. 2m empfohlen
- USB-C auf USB-A oder USB-C auf USB-C

---

## 2. MIG Dumper Modul

### Option A: USB Direkt

Wenn als eigenständiges USB-Gerät:

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   MIG Modul                                       PC                                    │
│   ┌──────────────┐                               ┌──────────────┐                       │
│   │              │      USB-C Kabel              │              │                       │
│   │   USB-C      │◄────────────────────────────►│    USB-A     │                       │
│   │              │                               │              │                       │
│   └──────────────┘                               └──────────────┘                       │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

### Option B: UFI Expansion Port

Wenn über UFI Expansion Header angeschlossen:

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   UFI Board                                      MIG Modul                              │
│   ┌──────────────┐                              ┌──────────────┐                        │
│   │              │    10-pol Flachbandkabel     │              │                        │
│   │  J8 (2x5)    │◄────────────────────────────►│   2x5 HDR    │                        │
│   │  Expansion   │                              │              │                        │
│   └──────────────┘                              └──────────────┘                        │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

**J8 Expansion Header Pinout (2x5, 2.54mm):**

```
         ┌────────────┐
   +3V3  │ 1       2  │  +5V
    SDA  │ 3       4  │  SCL
   GPIO1 │ 5       6  │  GPIO2
   GPIO3 │ 7       8  │  GPIO4
    GND  │ 9      10  │  GND
         └────────────┘
```

**MIG Modul Pinout:**

| Pin | Signal | Funktion |
|-----|--------|----------|
| 1 | +3V3 | Stromversorgung 3.3V |
| 2 | +5V | Stromversorgung 5V |
| 3 | SDA | I2C Daten (für EEPROM) |
| 4 | SCL | I2C Clock |
| 5 | CS | Chip Select (Active Low) |
| 6 | CLK | SPI Clock |
| 7 | MOSI | SPI Master Out |
| 8 | MISO | SPI Master In |
| 9 | GND | Ground |
| 10 | GND | Ground |

**Kabel-Konstruktion:**

| Teil | Bezeichnung | Menge |
|------|-------------|-------|
| Stecker | 2x5 IDC Female | 2 |
| Kabel | 10-adriges Flachbandkabel | 15cm |

```
┌──────────┐     ═══════════════     ┌──────────┐
│ IDC 2x5  │────────────────────────│ IDC 2x5  │
│ (UFI)    │        15cm            │ (MIG)    │
└──────────┘     1:1 Verdrahtung     └──────────┘
```

---

## 3. Switch Cartridge Modul

### Verbindung: USB-C

Eigenständiges USB-Gerät wie GB/GBA Modul.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   Switch Modul                                    PC                                    │
│   ┌──────────────┐                               ┌──────────────┐                       │
│   │              │      USB 3.0 Kabel            │              │                       │
│   │   USB-C      │◄────────────────────────────►│   USB 3.0    │                       │
│   │              │    (für Geschwindigkeit)      │              │                       │
│   └──────────────┘                               └──────────────┘                       │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

**Hinweis:** USB 3.0 empfohlen wegen der großen Datenmengen (bis 32GB Cartridges).

---

## 4. Apple Disk II Adapter

### Verbindung: 19-Pin Header zu DB-19

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   UFI Board                                      Apple Disk II Drive                    │
│   ┌──────────────┐                              ┌──────────────┐                        │
│   │              │     Adapterkabel             │              │                        │
│   │  J3 (19-pin) │◄────────────────────────────►│   DB-19      │                        │
│   │              │                              │              │                        │
│   └──────────────┘                              └──────────────┘                        │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

**J3 Apple Header Pinout (19-pin, 2.54mm):**

```
          ┌─────────────────────────────────┐
    GND   │ 1   2   3   4   5   6   7   8   │
          │ 9  10  11  12  13  14  15  16   │  +12V
          │    17  18  19                    │
          └─────────────────────────────────┘

Pin  Signal          Pin  Signal
─────────────────────────────────────
 1   GND              2   PH0
 3   GND              4   PH1
 5   GND              6   PH2
 7   GND              8   PH3
 9   GND             10   /WRREQ
11   GND             12   /ENBL2
13   GND             14   /ENBL1
15   GND             16   RDDATA
17   +5V             18   WRDATA
19   +12V
```

**DB-19 Pinout (Apple II Drive):**

Standard Apple Disk II Pinout - direktes 1:1 Mapping.

---

## 5. IEC Bus Kabel (C64/1541)

### Verbindung: DIN-6 zu DIN-6

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   UFI Board                                      1541 Laufwerk                          │
│   ┌──────────────┐                              ┌──────────────┐                        │
│   │              │     Standard IEC Kabel       │              │                        │
│   │  J5 (DIN-6)  │◄────────────────────────────►│   DIN-6      │                        │
│   │              │     (C64 Serial)             │              │                        │
│   └──────────────┘                              └──────────────┘                        │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

**IEC DIN-6 Pinout:**

```
          ╭───────╮
        ╱    5    ╲
       │  3     4  │
       │     6     │
       │  1     2  │
        ╲─────────╱

Pin  Signal    Beschreibung
──────────────────────────────
 1   SRQ       Service Request (nicht verwendet)
 2   GND       Ground
 3   ATN       Attention
 4   CLK       Clock
 5   DATA      Data
 6   RESET     Reset (active low)
```

**Kabel:** Standard C64 Serial Kabel, 1:1 verdrahtet, max. 2m.

---

## 6. Shugart FDD Kabel

### Verbindung: 34-Pin IDC

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   UFI Board                                      3.5" FDD                               │
│   ┌──────────────┐                              ┌──────────────┐                        │
│   │              │     Standard FDD Kabel       │              │                        │
│   │  J1/J2       │◄────────────────────────────►│   34-pin     │                        │
│   │  (34-pin)    │                              │              │                        │
│   └──────────────┘                              └──────────────┘                        │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

**Standard PC FDD Kabel verwendbar!**

**Hinweis:** Twist zwischen Pin 10-16 für Drive Select NICHT erforderlich - UFI verwendet separate Anschlüsse für FDD1 und FDD2.

---

## Kabel-Übersicht

| Modul | Kabeltyp | Länge | Standard? |
|-------|----------|-------|-----------|
| GB/GBA | USB-C | 2m | ✅ Ja |
| MIG (USB) | USB-C | 2m | ✅ Ja |
| MIG (Expansion) | 10-pol Flachband | 15cm | ❌ Custom |
| Switch | USB-C 3.0 | 1m | ✅ Ja |
| Apple II | 19-pin zu DB-19 | 30cm | ❌ Custom |
| IEC/1541 | DIN-6 zu DIN-6 | 2m | ✅ Standard C64 |
| FDD Shugart | 34-pin IDC | 50cm | ✅ Standard PC |
| Amiga Extern | 2x12 zu DB-23 | 30cm | ❌ Custom |

---

## Bezugsquellen

### Standard-Kabel
- USB-C: Amazon, MediaMarkt, etc.
- FDD 34-pin: eBay (gebraucht), AliExpress
- IEC DIN-6: eBay "C64 Serial Cable"

### Custom-Kabel Teile
- IDC Stecker: Reichelt, Mouser
- DB-23/DB-19 Stecker: Mouser, DigiKey
- Flachbandkabel: Reichelt

### Fertige Adapter (falls verfügbar)
- Amiga DB-23: eBay "Amiga External Drive Cable"
- Apple DB-19: eBay "Apple Disk II Cable"
