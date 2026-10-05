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
│   │  2x12 HDR   │◄────────────────────►│     DB-23 Male      │                        │
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

## Amiga DB-23 Pinout (Amiga Hardware Reference Manual, Appendix E)

Quelle: Amiga HRM, Appendix E „External Disk Interface Specification“, Pin Assignment (J7).
Am Amiga ist der Floppy-Port eine **DB-23-Buchse**; externe Laufwerke (A1010 …) haben einen **DB-23-Stecker** am Kabel.
Das Adapterkabel braucht daher eine **DB-23-Buchse** (wie der Amiga).

```
Pin  Signal        Dir*   Beschreibung
───────────────────────────────────────────────
 1   /RDY          ◄──    Ready (bzw. ID-Datenstrom im Identification Mode)
 2   /DKRD         ◄──    MFM Read Data
 3   GND           ───    Ground
 4   GND           ───    Ground
 5   GND           ───    Ground
 6   GND           ───    Ground
 7   GND           ───    Ground
 8   /MTRXD        ──►    Motor-on Data (wird mit /SELxB ins Laufwerk getaktet)
 9   /SEL2B        ──►    Select Drive 2
10   /DRESB        ──►    Reset (Laufwerk setzt Motor-FF zurück)
11   /CHNG         ◄──    Disk Change
12   +5V           ───    Power +5V (270 mA max, 410 mA Anlauf)
13   /SIDEB        ──►    Side Select
14   /WPRO         ◄──    Write Protect
15   /TK0          ◄──    Track 0
16   /DKWEB        ──►    Write Gate
17   /DKWDB        ──►    MFM Write Data
18   /STEPB        ──►    Step
19   DIRB          ──►    Direction (inaktiv = Richtung Mitte)
20   /SEL3B        ──►    Select Drive 3
21   /SEL1B        ──►    Select Drive 1 (erstes externes Laufwerk)
22   /INDEX        ◄──    Index
23   +12V          ───    Power +12V (160 mA max, 540 mA Anlauf)

* Richtung aus Sicht des Hosts (UFI): ──► Ausgang, ◄── Eingang
```

---

## Verdrahtung (UFI J4 ↔ DB-23)

| UFI J4 Pin | Signal | DB-23 Pin | Signal |
|------------|--------|-----------|--------|
| 1, 19, 20 | GND | 3, 4, 5, 6, 7 | GND |
| 2, 4, 21, 22 | +5V | 12 | +5V |
| 3 | RDATA | 2 | /DKRD |
| 5 | SIDE | 13 | /SIDEB |
| 6 | DKCHG | 11 | /CHNG |
| 7 | DIR | 19 | DIRB |
| 8 | WPROT | 14 | /WPRO |
| 9 | STEP | 18 | /STEPB |
| 10 | TRK0 | 15 | /TK0 |
| 11 | WDAT | 17 | /DKWDB |
| 12 | WGATE | 16 | /DKWEB |
| 13 | INDEX | 22 | /INDEX |
| 14 | READY | 1 | /RDY |
| 15 | SEL | 21 | /SEL1B |
| 17 | MTRX | 8 | /MTRXD |
| 24 | +12V | 23 | +12V |
| – | – | 9, 10, 20 | /SEL2B, /DRESB, /SEL3B: im Stecker je 1 kΩ nach +5V (inaktiv) oder offen lassen |

> ⚠ Die J4-Belegung der Modular-Platine führt RDATA doppelt (Pin 3 und 16 „DKRD“) und SEL doppelt (Pin 15 und 18 „/SEL1“).
> Pin 16 und 18 hier **nicht** verdrahten, bis die J4-Belegung im Modular-Schaltplan geklärt ist.
>
> **UFI Headless (J7):** Header-Pin n = DB-23-Pin n (Pin 24 = GND), SEL2B/SEL3B/DRESB sind auf der Platine bereits per 1 kΩ inaktiv – das Kabel ist dort 1:1.

---

## Kabel-Konstruktion

### Benötigte Teile

| Teil | Bezeichnung | Menge |
|------|-------------|-------|
| Stecker | 2x12 IDC Header Male | 1 |
| Buchse | DB-23 Female (wie am Amiga; Laufwerkskabel hat Stecker) | 1 |
| Kabel | 26-adriges Flachbandkabel | ~30cm |
| Gehäuse | DB-23 Kunststoffgehäuse | 1 |

### Aufbau

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                                                                                          │
│   IDC 2x12                    Flachbandkabel                    DB-23 Female            │
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
          │  RDY RD GND GND GND GND GND MTR S2  │
          │                                     │
          │   10  11  12  13  14  15  16  17    │
          │   RES CHG +5V SID WPR TK0 WG  WD    │
          │                                     │
          │     18  19  20  21  22  23          │
          │     STP DIR S3  S1  IDX +12         │
          └─────────────────────────────────────┘
```

---

## Signalpegel

Alle Signale sind Active-Low (0V = Aktiv, 5V = Inaktiv).

UFI Headless: Ausgänge über SN74LS07 (Open Collector, 40 mA), Eingänge über 74LVC14A mit 1 kΩ Pull-ups auf +5V (siehe `kicad/UFI_Headless/README.md`).

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
