# UFI Headless v1.0 – was die Platine kann (Stand 24.10.2026, Schaltplanstufen A1–A4)

Lesehilfe für die Bestückung und den Gehäusebau: jede Zeile ist eine Funktion, daneben die
Teile, die sie tragen, und wie sie bedient wird. Vorgabe für v1.0: **komplette Bestückung**
(diesmal keine nachzubestückenden Netzwerke), geschlossenes Gehäuse, von außen nur USB und
LAN erreichbar, nichts ist nach dem Einbau steckbar. Quellen: Schaltplan
`kicad/UFI_Headless` (ERC 0/0), `kicad/UFI_Headless/README.md`, `docs/USB_Protokoll.md`,
`software/ufi_host/README.md`. Was noch nicht im Schaltplan ist, steht in Abschnitt 8.

## 1 Rechner und Speicher

| Funktion | Teile | Hinweis |
|---|---|---|
| Steuerung | U5 STM32H723ZGT6, LQFP-144, 550 MHz Cortex-M7, 25-MHz-Quarz Y1 | Firmware 1.18-dev; 108 von 144 Pins belegt, nach A4 und W5500 bleiben 24 frei |
| Flussspeicher 8 MB | U11 APS6404L QSPI-PSRAM an OCTOSPI1 | bis 20 Umdrehungen je Spur im Speicher; ohne PSRAM Rückfall auf 224 KB internes SRAM |
| Massenspeicher 4 GB | U15 CSNP32GCR01 SD-NAND an SDMMC2 | FAT32 „UFI“: Dumps `DUMPnnnn.SCP/.IMG/.ADF/.HFE`, Logs, `UFI.CFG`, `SELFTEST.LOG`, gespeicherte Laufwerksprofile |
| Debug | J4 SWD 1,27 mm (Cortex-Debug 10-polig), J5 UART (USART3 PD8/PD9), SWO PB3 | intern, nur bei offenem Deckel |
| Reset / Boot | SW1 RESET, SW2 BOOT0 | im Gehäuse unerreichbar, aber nicht nötig: Taste A an der Front beim Einschalten startet den ROM-DFU-Bootloader, `ufi bootloader` ebenso |
| Platinenkennung | R17/R18 an PA4 | `ufi caps` zeigt die Revision (v0.7 = 10k/2,2k); v1.0 bekommt einen eigenen Teilerwert (offen, siehe 8) |
| I²C-Reserve | TP10 SCL, TP11 SDA, TP12 +3V3, TP13 GND, R19/R20 2,2k | für EEPROM oder Sensor, keine Firmware |

## 2 Versorgung

| Funktion | Teile | Bedienung / Firmware |
|---|---|---|
| USB-C 5 V und Daten | J1 (HRO TYPE-C-31-M-12), R1/R2 Rd, U1 ESD | `ufi usb-power`: beworbener Quellenstrom 0,5/1,5/3 A aus CC1/CC2 |
| USB intern (Alternative) | J16 USB-Header | für ein Gehäuse-Panelkabel oder wenn J1 nicht erreichbar ist |
| 12 V Eingang | J2 Hohlstecker 5,5/2,1, SS54 Verpolschutz, SMAJ15CA | speist den Buck U2 TPS54202 (12 → 5 V) und die 12-V-Laufwerksschiene |
| 5 V extern | J17 PSU_IN (Klemme: 5 V, GND, GND, 12 V), F4 | für ein internes PC-Netzteil |
| 5-V-Quellenwahl | U3 TPS2116 (USB gegen Platinen-5 V), **neu U19 TPS2116** (externe 5 V von J17 mit Vorrang vor dem Buck), TP9 | automatisch, kein Jumper mehr (J18 entfallen); `PWR_SRC` PG4 meldet die Hauptquelle |
| 3,3 V | U4 AP7361C-33 LDO | TP1–TP4: 3V3, 5V, 12V, GND |
| Laufwerksversorgung geschaltet | Q1/Q3 AO3401A High-Side, Q2/Q4 2N7002, F2/F3 Polyfuses 1,1 A, D13/D14 TVS | aus, solange die MCU im Reset ist; Firmware schaltet 5 V, dann 12 V (`ufi power`, `POWER` 0x40) |
| Strommessung | R26/R29 0,1 Ω, U12/U13 INA180A1 → PC0/PC1 | über 1,5 A für 50 ms → Schiene aus, ERR-LED, Ereignis 0x83; `ufi selftest` Posten `rail_5v`/`rail_12v` |
| Laufwerksstrom-Ausgang | J3 4-polig (FDD_5V, GND, GND, FDD_12V) | Stufe A5: codierter Floppy-Stromstecker statt offener Stiftleiste |
| C64 PSU Guard | **neu J20 GUARD 1×4** (GND, OFF, ERR5, ERR12), R107/R108 | Anschluss der kleinen Spannungsplatine (`kicad/C64_PSU_Guard`, dort J3): OFF schaltet beide Guard-Kanäle ab, ERR5/ERR12 melden eine ausgelöste eFuse; Firmware-Nutzung folgt |

## 3 Laufwerksschnittstellen

| Schnittstelle | Teile | Firmware / Host |
|---|---|---|
| 34-polig PC/Shugart | J6 2×17, LS07-Treiber U6/U7 (Open-Collector), HC14-Empfänger U8, 1-kΩ-Abschluss R75–R91 (**neu einzeln**), ESD D9–D11, Lötbrücken JP1 (Pin 6 DRATE für 3-Mode) und JP2 (Pin 3 GND/5 V) | Laufwerk A/B (Twist) oder Shugart DS0–DS3, 300/360 U/min (`ufi rpm`, `SET_RPM`), DENSITY/DRATE/TG43 (`ufi timing`, `ufi lines`), 8-Zoll über Adapter |
| Amiga extern | J7 2×12 (Pin n = DB23 Pin n), R97–R99 | `drive=amiga|amiga2`, Amiga-ID (`AMIGA_ID`); Stufe A5: zusätzlich DB23-Buchse an der Laufwerkskante |
| Commodore IEC | J8 1×6 (DIN-6-Belegung), LS07 U9, HC14 U10, R92–R96, ESD D12 | 1541/1571/1581 roh und über DOS: `ufi iec-*`, D64/D71/D81 lesen und schreiben (`iec-read`/`iec-write`), Nibbles (`IEC_NIB`), Speicher/DOS-Befehle (`IEC_MEM`), Blockzugriff (`IEC_BLOCK`); Stufe A5: DIN-6-Buchse auf der Platine |
| Apple Disk II | J14 2×10, U16 AHCT244, U17 LVC2G17, U18 ICL7660 (−12 V), J15 zweites Laufwerk, J19 Sync-Sensor | 5,25-Zoll-GCR mit Viertelspuren, `apple-gcr`, `apple-table`, `read-dsk`; Sync-Sensor über `apple_sync=1` |
| Atari SIO (optional) | nur Kabel DIN-6 → SIO-13 an J8 | Firmware-Option, keine Platinenänderung |
| Schreibschutz in Hardware | **JP4 Lötbrücke** (statt Jumper J11), U14 LVC1G32, R33 | geschlossen = WGATE elektrisch gesperrt; zusätzlich Sitzungs-Schreibsperre per Software (`ufi write-lock`, Status 22) |

## 4 Flux-Engine (Kernfunktion)

| Funktion | Firmware / Host |
|---|---|
| Lesen | RDATA/INDEX auf TIM2 (275 MHz, 32 bit), DMA in den PSRAM; `ufi read`, `ufi read-disk` → SCP, HFE, IMG, ADF, DSK; Umdrehungen 1–20, ohne Index, hartsektoriert 10/16/32, Kopfschrittpläne (Spiradisc), Halb- und Viertelspuren |
| Schreiben | WDATA TIM3_CH1 per DMA, Präkompensation, `WRITE_LIMITS`, Prüflesen, Spurrest löschen (`--erase-rest`), schwache Zellen (`--weak-jitter`), Roh-Fingerabdruck-Rücklesen (`--raw-check`), `ERASE`, `PATTERN`, `FORMAT` (PC, Amiga, 1581) |
| Analyse auf dem Gerät | `ANALYZE` und `ANALYSIS_JOB` (Ops 0–56): Formaterkennung (IBM MFM/FM, Amiga, Commodore GCR, Apple GCR, Macintosh GCR, HP MMFM), Sektorbefunde, Timing/Lücken/Glitches, Schutzklassifikation, Spurvermessung (op 46), Qualitätsbericht `QUALITY`, Toleranzflags je Decoder |
| Laufwerksdiagnose | `ufi rpm`, `ufi monitor`, `ufi scan`, `DIAG_RPM`, `DRIVE_SCAN`, `PROBE_TRACKS`, `SEEK_TEST`, Laufwerksprofile (`ufi profile`, `ufi extra`) |
| Indexsimulation | **neu PE5 = TIM15 Kanal 1 → U7F → INDEX-Bus** (Hardware-Impuls, ohne Draht); `ufi index-sim 300|360 [--internal]`, `UFI.CFG index_sim=`; 3,3-V-Impuls auch auf J21 Pin 10 |
| Greaseweazle-kompatibel | `UFI.CFG protocol=gw`: `gw info/read/write/erase/seek/delays` mit den Greaseweazle-Werkzeugen (PID 0x4F57) |
| USB-Betriebsarten | Flux (Host-Tool), SD-Laufwerk (Massenspeicher), USB-Floppy (Diskette als USB-Laufwerk, PC-Formate), Greaseweazle, Composite; Wechsel per Modusschalter an der Front, Taste B oder `ufi mode` |
| Betrieb ohne PC | Taste A ≥ 1 s: ganze Diskette dumpen oder kopieren (`button_a=dump|copy`), Taste B: Abbruch / USB-Laufwerk; Blinkcodes auf ACT/ERR; alles über `UFI.CFG` einstellbar |

## 5 Bedienung und Anzeige (Frontplatte)

| Funktion | Teile | Hinweis |
|---|---|---|
| Front-Header | **J12 2×8 Wannenstecker mit Kodierung** | Flachbandkabel zur Frontplatte, einmal bei der Montage gesteckt |
| LEDs ACT, ERR, USB, FDD, PWR | je eigener Vorwiderstand R34–R37/R100 330 Ω auf der Platine, Anoden an J12 Pin 3/5/7/9/11, Kathoden an GND (gerade Pins) | D22–D26 ESD-Schutz; auf der Platine bleibt nur D4 ACT |
| Taster A und B | J12 Pin 13/14, R101/R102 470 Ω, D18/D19 ESD | SW3/SW4 auf der Platine bleiben als Footprint unbestückt (einzige bewusste Nichtbestückung) |
| Modusschalter EIN-AUS-EIN | J12 Pin 15/16 (MODE1/MODE2), Mittelkontakt +3V3 über R105 1 kΩ an Pin 1, R103/R104, D20/D21 | Mitte = Flux, MODE1 = USB-Floppy, MODE2 = SD-Laufwerk; ACT blinkt 1×/2×/3× |
| RESET / BOOT | nur auf der Platine | an der Front bewusst nicht vorgesehen (Fehlbedienung), DFU über Taste A beim Einschalten |

## 6 Test und Diagnose auf der Platine

| Funktion | Teile | Firmware / Host |
|---|---|---|
| Selbsttest | – | `ufi selftest` / `python -m ufi_host.bringup`: Takt, USB-Quarz, PSRAM, SD, Versorgungen, Board-ID, USB-C CC, Laufwerksleitungen, IEC-Rückschleife, Index-Schleife, Absturzprotokoll, Stack, Log |
| Index-Schleife | U7F auf den INDEX-Bus | Posten `index_loop` läuft ab v1.0 ohne Draht |
| Schreib-/Lese-Rückschleife | **neu U22 TS5A3166** (WDATA↔RDATA auf dem 5-V-Bus), **Q5 2N7002** (TRK0 → GND), R106 100k, C58; **U10F** liest WGATE zurück | LOOP_EN PG14, WGATE_SENSE PG15; im Reset offen; Selbsttest-Posten folgen in der Firmware |
| Logikanalysator | **neu J21 2×10 LA**, alle Signale 3,3-V-Seite | 1 RDATA, 3 WDATA, 5 INDEX, 7 WGATE, 9 STEP, 11 DIR, 13 SIDE, 15 DRVSEL_A, 17 MOTOR_A, 19 TRK0, 2 WPROT, 4 DSKCHG, 6 READY, 8 DENSITY, 10 INDEX_SIM, 12 LOOP_EN, 14 WGATE_SENSE, 16/18/20 GND |
| Messpunkte | TP1–TP4 Spannungen, TP5 RDATA, TP6 INDEX, TP7 WDATA, TP8 WGATE nach dem Gatter, TP9 5-V-Quelle, TP10–TP13 I²C | – |
| Ereignisse und Zähler | – | `ufi events`, `DIAGNOSTICS`, `FEATURES`, `DESCRIBE`, Grund der letzten Abweisung, Absturzaufzeichnung |

## 7 Bestückung v1.0

- Alles bestücken, einschließlich J7 (Amiga), J16 (USB-Header), C46/C50, R75–R99, J20, J21 und der neuen Teile U19, U22, Q5, JP4-Pads, D18–D26.
- Bewusst unbestückt: **SW3, SW4** (Taster A/B auf der Platine; sie sitzen an der Front). Kupfer-Lötbrücken JP1 (offen), JP2 (1-2 gebrückt), JP3 (offen), JP4 (offen = schreiben erlaubt) sind keine Bauteile; ihre Stellung wird bei der Montage festgelegt.
- Entfallen gegenüber v0.7: J9, J11, J13, J18, D5–D8, R12–R15, RN1–RN8.
- Nach dem Bestücken: `python -m ufi_host.bringup --port COMx --report bringup.json` (docs/Bringup_2026-10-24.md), bei v1.0 mit allen Posten OK statt der RN-Warnungen.

## 8 Noch nicht im Schaltplan (Reihenfolge wie geplant)

1. **A5 Laufwerkskante:** J8 → DIN-6-Buchse auf der Platine, DB23-Buchse für externe Amiga-Laufwerke neben J7, J3 als codierter Floppy-Stromstecker.
2. **A6 LAN:** W5500 mit RJ45-Magjack an der Eingangskante, SPI3 (PC10/PC11/PC12), CS PA15, INT PD5, RST PD10; Steuerung über LAN wie über USB als spätere Firmwarestufe.
3. **Board-ID v1.0:** neuer Teilerwert R18, damit die Firmware v0.7 (J9-Pinbelegung) von v1.0 (PE5-Indexsimulation, LOOP_EN) unterscheiden kann.
4. **Firmware-Nutzung der neuen Leitungen:** Selbsttest-Posten für die Rückschleife (WDATA→RDATA, TRK0, WGATE_SENSE), Guard-Status in `BOARD_STATUS`, Guard-OFF beim Abschalten der Laufwerksversorgung.
5. **Layout:** Eingangskante (USB-C in Kantenaussparung, J16, J2, J17, J4, J5) gegenüber der Laufwerkskante (J6, J14, DB23/J7, DIN-6, J3), Front-Header an einer Seitenkante, Gehäuse noch offen (Umriss bleibt bis dahin).
