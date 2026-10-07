# Inbetriebnahme UFI Headless v0.6

Reihenfolge einhalten: jede Stufe erst, wenn die vorige passt. Ergebnisse in die Spalte ✔ eintragen.
Messpunkte: **TP1** 3V3 · **TP2** 5V · **TP3** 12V · **TP4** GND · **TP5** RDATA · **TP6** INDEX · **TP7** WDATA · **TP8** WGATE (hinter der Schreibsperre).
LEDs: PWR (grün, 3V3) · ACT · FDD · USB · ERR (rot).

Werkzeug: Labornetzteil mit Strombegrenzung, Multimeter, Oszilloskop (≥ 50 MHz), ST-Link (SWD) oder USB-DFU,
ein **Schrott-Laufwerk** und **Schrott-Disketten** für alle Schreibtests.

## 1 Sichtprüfung (ohne Strom)

| # | Prüfung | Soll | ✔ |
|---|---|---|---|
| 1.1 | Pin-1-Markierungen U5 (MCU), U6/U7/U9 (LS07), U8/U10 (LVC14), U11 (PSRAM), U12/U13 (INA180), U14, U15 (SD-NAND) | Punkt am Pin 1 | |
| 1.2 | Polarität D-TVS/Schottky (SS54), Elkos, LEDs | laut Bestückungsdruck | |
| 1.3 | Lötbrücken, v. a. QFP-144 (U5) und SOT-353 (U14) | keine Brücken (Lupe) | |
| 1.4 | JP1 offen, JP2 auf 1-2 (GND), Jumper J11 (WRITE LOCK) **gesteckt** | Auslieferungszustand + Schreibschutz | |
| 1.5 | Widerstand gegen GND (TP4): TP1 3V3, TP2 5V, TP3 12V, FDD_5V/12V an J3 | > 100 Ω, kein Kurzschluss | |

## 2 Versorgung (ohne Firmware, ohne Laufwerk)

| # | Prüfung | Soll | ✔ |
|---|---|---|---|
| 2.1 | 12 V an J2, Netzteil auf **150 mA** begrenzt | Strom < 100 mA, PWR-LED an | |
| 2.2 | TP3 / TP2 / TP1 | 11,5–12,5 V / 4,9–5,2 V / 3,25–3,35 V | |
| 2.3 | Verpolung kurz testen (12 V falsch herum, 150 mA) | kein Strom (SS54), nichts warm | |
| 2.4 | Nur USB-C am PC (ohne 12 V) | TP2 ≈ 5 V über den TPS2116, TP1 3,3 V | |
| 2.5 | FDD_5V / FDD_12V an J3 (Pins 1 / 4) | **0 V** – Schalter aus, solange keine Firmware läuft | |
| 2.6 | Temperatur nach 2 min (Finger/IR): Buck U2, LDO U4, MCU | handwarm | |

## 3 Firmware flashen

| # | Schritt | Soll | ✔ |
|---|---|---|---|
| 3.1 | `cmake -S firmware -B firmware/build -G Ninja && cmake --build firmware/build` | `ufi_firmware.bin`, keine Warnungen | |
| 3.2 | Flashen: ST-Link an J4 (SWD) **oder** BOOT-Taster (SW2) halten + RESET (SW1) → `dfu-util -a 0 -s 0x08000000:leave -D ufi_firmware.bin` | erfolgreich | |
| 3.3 | Nach dem Start (12 V an, Netzteil 500 mA) | 3 ERR-Blinker = PSRAM-Fehler; sonst keine ERR-LED | |
| 3.4 | `ufi info` | `UFI Flux Engine v1.4 <rev> <datum>` · `UFI Headless v0.6` · `STM32H723` · `PSRAM 8 MB ok` | |
| 3.5 | Bei „rev ?“: Spannung an PA4 (R17/R18) messen | 1,06 V | |
| 3.6 | `ufi selftest`, `ufi status` | keine Fehler | |

Neue v0.5-Befehle (Host-Tool hat dafür noch keine Kommandos) – in Python:

```python
import struct
from ufi_host import protocol as P
d = P.Device(P.open_serial())
st = lambda: struct.unpack("<BBHHH", d.command(0x1B))   # power, flags, i5_mA, i12_mA, board_id_mV
print(st())                       # Laufwerksversorgung ein/aus: d.command(0x1B, maske)
print(d.command(0x1D))            # SD_INFO: ready, status, type, bus, capacity_MB (u32)
```

## 4 Board-Funktionen v0.5

| # | Prüfung | Soll | ✔ |
|---|---|---|---|
| 4.1 | `st()` nach dem Start | power = 3 (beide an), FDD_5V/12V an J3 ≈ 5 / 12 V | |
| 4.2 | Ohne Laufwerk: i5_mA / i12_mA | < 20 mA (Offset INA180) | |
| 4.3 | Taster **B** beim Einschalten halten | ERR-LED an, power = 0, Flag Bit6; `d.command(0x1B, 3)` schaltet ein | |
| 4.4 | J11 gesteckt / gezogen | Flag Bit0 = 1 / 0 | |
| 4.5 | Taster A/B während des Betriebs gedrückt | Flag Bit4 / Bit5 | |
| 4.6 | Last 10 Ω/5 W an FDD_5V (≈ 0,5 A), dann 3,3 Ω (≈ 1,5 A) | i5_mA ≈ 500; bei > 1,5 A nach 50 ms aus, Flag Bit1, ERR-LED | |
| 4.6a | Front-LEDs an J12 (1+/2− PWR, 3+/4− ACT, 5+/6− FDD, 7+/8− ERR), LED-Test `d.command(0xD0, 2)` | grün dauerhaft, gelbe LEDs leuchten mit ACT/FDD/ERR mit; zu dunkel → R34–R37 kleiner (min. 150 Ω) | |
| 4.7 | `st()` Flag Bit3 | 1 = SD-NAND U15 initialisiert | |
| 4.8 | `SD_INFO` | ready 1, status 0, bus 4, capacity 3500–4000 MB (4 GB) bzw. 900–1000 MB (1 GB) | |
| 4.9 | Taster **A** beim Einschalten | DFU-Gerät erscheint (`dfu-util -l`) | |

## 5 Laufwerk lesen (nur lesen, J11 gesteckt lassen)

| # | Prüfung | Soll | ✔ |
|---|---|---|---|
| 5.1 | 3,5"-Laufwerk an J6 + Strom an J3, `ufi select a`, `ufi motor on` | Motor läuft, Strom i5/i12 plausibel (Anlauf kurz höher) | |
| 5.2 | `ufi recal`, `ufi seek 79`, `ufi seek 0` | Kopf fährt, TRK0 erkannt | |
| 5.3 | `ufi rpm` | 300 ± 3 (bzw. 360 bei HD-5,25") | |
| 5.4 | Oszi TP6 (INDEX) | ein Puls je Umdrehung, 200 ms, saubere Flanken | |
| 5.5 | Oszi TP5 (RDATA) während `ufi read 0 0 -r 3` | Abstände der Pulse 4/6/8 µs (DD) bzw. 2/3/4 µs (HD), keine Prellungen | |
| 5.6 | `ufi read 0 0 -r 3` mit bekannter Diskette | 3 Umdrehungen, Flusswechsel/Umdrehung plausibel (DD ≈ 35k, HD ≈ 70k) | |
| 5.7 | Ganze Diskette `ufi read-disk -o test.scp --tracks 80 --sides 2 -r 3`, Zeit stoppen | Image in HxC/Greaseweazle-Tools lesbar; **Dauer notieren** (USB-Durchsatz) | |
| 5.8 | Amiga-Laufwerk an J7: `ufi select amiga`, `ufi amiga-id` | ID wie im README | |
| 5.9 | USB-Kabel während `ufi motor on` ziehen | nach < 0,3 s Motor aus, Laufwerk abgewählt | |
| 5.10 | Motor an lassen, 30 s nichts tun | Motor geht selbst aus | |
| 5.11 | Dump ohne PC: USB ab, 12 V an, Diskette rein, Taster A ≥ 1 s | FDD-LED je Spur, ACT blinkt; danach ACT dauerhaft an. ERR-Blinkcode: 1 Speicher, 2 Laufwerk/Lesen, 3 voll/Schreibfehler, 4 abgebrochen | |
| 5.12 | Während eines Dumps Taster B kurz | Abbruch, ERR blinkt 4× | |
| 5.13 | Taster B ≥ 2 s (USB am PC) | USB-LED an, Laufwerk „UFI“ erscheint; `DUMP0001.SCP` kopieren und mit dem Image aus 5.7 vergleichen (gleiche Diskette: Spurdaten ähnlich, Format identisch) | |
| 5.14 | Taster B ≥ 2 s erneut | USB-LED aus, `ufi info` geht wieder | |
| 5.15 | Betriebsart-Schalter an J9 (Mitte → Pin 1, Seiten → Pin 5 / Pin 6), auf „USB-Floppy“ | ACT blinkt 2×, USB-LED an, am PC erscheint „UFI USB Floppy“ | |
| 5.16 | Formatierte 1,44-MB-Diskette rein, Explorer öffnen, Datei kopieren/lesen | Inhalt sichtbar, Datei lesbar; nach dem Schreiben läuft der Motor ~1 s nach (Rückschreiben + Prüflesen) | |
| 5.17 | Dasselbe mit 720K (und, falls 5,25"-Laufwerk, 1,2M / 360K) | Größe stimmt im Explorer | |
| 5.18 | Diskette wechseln, Explorer F5 | neuer Inhalt; ohne Diskette „kein Datenträger“ | |
| 5.19 | Schalter zurück in die Mitte | ACT blinkt 1×, `ufi info` geht wieder | |

## 6 Schreiben (Schrott-Diskette, J11 ziehen)

| # | Prüfung | Soll | ✔ |
|---|---|---|---|
| 6.1 | J11 gesteckt, Schreibversuch | Fehler „write protected“, TP8 bleibt high | |
| 6.2 | J11 gezogen, `WRITE_PATTERN` 2 µs, 1 s: `d.command(0x34, 0, 0, 0xD0, 0x07, 0xE8, 0x03)` (Motor an) | TP8 low für 1 s, TP7: 400-ns-Pulse alle 2 µs | |
| 6.3 | `ufi write` / `write-disk --verify` mit dem Image aus 5.7 | Verify ok, zurückgelesen identisch decodierbar | |
| 6.4 | Schreibgeschützte Diskette | Fehler „write protected“ | |

## 7 Sonstiges

| # | Prüfung | Soll | ✔ |
|---|---|---|---|
| 7.1 | 3-Mode-Laufwerk: JP1 brücken, `SET_LINES` DRATE an/aus, HD-Diskette lesen | **DRATE-Polarität festhalten** | |
| 7.2 | 1541 an J8 (DIN-6-Kabel): `ufi iec-reset`, `ufi iec-send 0x28`, `ufi iec-recv` | Laufwerk antwortet | |
| 7.3 | 40-Spur-Diskette im 80-Spur-Laufwerk: `ufi timing double_step=1` | Spuren lesbar | |
| 7.4 | `SEEK_TEST`: `d.command(0x1E, 0, 79, 5, timeout=60)` | Kopf pendelt 5×, kein Schrittverlust (danach `recal`) | |

## Ergebnisse, die zurück in Firmware/Doku müssen

- SD-NAND-Schreibrate (5.11: Dauer eines Dumps) → ggf. SDMMC-Takt 25 → 50 MHz in `firmware/src/ufi_sd.c`
- DRATE-Polarität (7.1) → Doku `SET_LINES`
- USB-Durchsatz (5.7) → entscheidet, ob USB High Speed (v0.7) nötig ist
- Strommessung: Offset/Steigung (4.2/4.6) → ggf. Kalibrierung in `ufi_board.c`
