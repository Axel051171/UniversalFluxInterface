# Amiga DB-23 Adapter-Kabel Spezifikation

## Übersicht

Adapterkabel zum Anschluss von Amiga-externen Laufwerken (A1010, A1011, Cumana CAX354) an UFI.

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   UFI Board                              Amiga External Drive                            │
│   ┌─────────────┐                        ┌─────────────────────┐                        │
│   │             │                        │                     │                        │
│   │  J4         │     Adapter-Kabel      │        DB-23        │                        │
│   │  2x12 HDR   │◄────────────────────►│        Female       │                        │
│   │             │                        │                     │                        │
│   └─────────────┘                        └─────────────────────┘                        │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## UFI J4 Pinout (2x12 Header, 2.54mm)

```
                  ┌──────────────────┐
         GND   1  │ ●              ● │  2   +5V
       RDATA   3  │ ●              ● │  4   +5V
        SIDE   5  │ ●              ● │  6   DKCHG
         DIR   7  │ ●              ● │  8   WPROT
        STEP   9  │ ●              ● │  10  TRK0
        WDAT  11  │ ●              ● │  12  WGATE
       INDEX  13  │ ●              ● │  14  READY
         SEL  15  │ ●              ● │  16  DKRD
        MTRX  17  │ ●              ● │  18  /SEL1
         GND  19  │ ●              ● │  20  GND
         +5V  21  │ ●              ● │  22  +5V
        N.C.  23  │ ●              ● │  24  +12V
                  └──────────────────┘
```

---

## Amiga DB-23 Pinout (Female, Drive Side)

```
          ┌─────────────────────────────────────┐
          │  1   2   3   4   5   6   7   8   9  │
          │   10  11  12  13  14  15  16  17    │
          │     18  19  20  21  22  23          │
          └─────────────────────────────────────┘

Pin  Signal        Dir    Beschreibung
───────────────────────────────────────────────
 1   /RDY          ◄──    Drive Ready (active low)
 2   /DKRD         ◄──    Disk Read Data
 3   /WPR          ◄──    Write Protect
 4   /TK0          ◄──    Track 00
 5   /WGATE        ──►    Write Gate
 6   /WDATA        ──►    Write Data
 7   /STEP         ──►    Step Pulse
 8   /DIR          ──►    Direction (High=Out)
 9   /SEL2         ──►    Select Drive 2 (active low)
10   /SEL0         ──►    Select Internal (not used)
11   /SEL3         ──►    Select Drive 3 (active low)
12   GND           ───    Ground
13   /SEL1         ──►    Select Drive 1 (active low)
14   /MTRX         ──►    Motor Control
15   GND           ───    Ground
16   /SIDE         ──►    Side Select
17   /DKCHG        ◄──    Disk Change
18   /INDEX        ◄──    Index Pulse
19   +12V          ───    Power +12V
20   GND           ───    Ground
21   +5V           ───    Power +5V
22   +5V           ───    Power +5V
23   GND           ───    Ground
```

---

## Verdrahtung (UFI J4 ↔ DB-23)

| UFI J4 Pin | Signal | DB-23 Pin | Signal |
|------------|--------|-----------|--------|
| 1 | GND | 12, 15, 20, 23 | GND |
| 2 | +5V | 21, 22 | +5V |
| 3 | RDATA | 2 | /DKRD |
| 4 | +5V | 21, 22 | +5V |
| 5 | SIDE | 16 | /SIDE |
| 6 | DKCHG | 17 | /DKCHG |
| 7 | DIR | 8 | /DIR |
| 8 | WPROT | 3 | /WPR |
| 9 | STEP | 7 | /STEP |
| 10 | TRK0 | 4 | /TK0 |
| 11 | WDAT | 6 | /WDATA |
| 12 | WGATE | 5 | /WGATE |
| 13 | INDEX | 18 | /INDEX |
| 14 | READY | 1 | /RDY |
| 15 | SEL | 13 | /SEL1 |
| 16 | DKRD | 2 | /DKRD |
| 17 | MTRX | 14 | /MTRX |
| 18 | /SEL1 | 13 | /SEL1 |
| 19, 20 | GND | 12, 15, 20, 23 | GND |
| 21, 22 | +5V | 21, 22 | +5V |
| 24 | +12V | 19 | +12V |

---

## Kabel-Konstruktion

### Benötigte Teile

| Teil | Bezeichnung | Menge |
|------|-------------|-------|
| Stecker | 2x12 IDC Header Male | 1 |
| Buchse | DB-23 Male (für Drive Female) | 1 |
| Kabel | 26-adriges Flachbandkabel | ~30cm |
| Gehäuse | DB-23 Kunststoffgehäuse | 1 |

### Aufbau

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   IDC 2x12                    Flachbandkabel                    DB-23 Male              │
│   ┌─────┐                     ════════════════                  ┌─────────────┐         │
│   │█████│────────────────────────────────────────────────────►│             │         │
│   │█████│                     ~30cm                             │  ●●●●●●●●●  │         │
│   └─────┘                                                       │   ●●●●●●●●  │         │
│                                                                 │    ●●●●●●   │         │
│                                                                 └─────────────┘         │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

### Lötplan DB-23

```
          ┌─────────────────────────────────────┐
          │  1   2   3   4   5   6   7   8   9  │
          │   RDY RD WPR TK0 WG  WD STP DIR S2  │
          │                                     │
          │   10  11  12  13  14  15  16  17    │
          │   S0  S3 GND S1 MTR GND SID DCH     │
          │                                     │
          │     18  19  20  21  22  23          │
          │     IDX +12 GND +5V +5V GND         │
          └─────────────────────────────────────┘
```

---

## Signalpegel

Alle Signale sind Active-Low (0V = Aktiv, 5V = Inaktiv).

Der UFI Level-Shifter (74LVC245) konvertiert automatisch zwischen 3.3V (STM32) und 5V (Amiga).

---

## Kompatible Laufwerke

| Laufwerk | Typ | getestet |
|----------|-----|----------|
| A1010 | 3.5" 880K | ☐ |
| A1011 | 3.5" 880K | ☐ |
| Cumana CAX354 | 3.5" 880K | ☐ |
| Externe PC-Laufwerke (mit Adapter) | 3.5" | ☐ |

---

## Hinweise

1. **Stromversorgung:** Das Laufwerk wird über das Kabel mit +5V und +12V versorgt. Sicherstellen dass das UFI-Netzteil ausreichend Leistung liefert.

2. **Drive Select:** Standard ist /SEL1 (Pin 13). Bei mehreren Laufwerken /SEL2 oder /SEL3 verwenden.

3. **Kein Daisy-Chain:** Im Gegensatz zum Original-Amiga kein Daisy-Chain möglich. Ein Kabel pro Laufwerk.

4. **Motor Control:** /MTRX muss LOW sein damit der Motor dreht.
