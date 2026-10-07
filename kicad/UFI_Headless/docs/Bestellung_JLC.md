# Bestellung bei JLCPCB – UFI Headless v0.7

Dateien: `kicad/UFI_Headless/fertigung/` bzw. Anhang des GitHub-Release **v0.7** (dort mit `_v0.7` im Namen: `UFI_Headless_v0.7_gerber.zip`, `…_bom-jlc.csv`, `…_cpl-jlc.csv`).

## 1 Leiterplatte

Auf jlcpcb.com → *Order now* → `UFI_Headless_gerber.zip` hochladen. Größe (110 × 97 mm) und Lagen erkennt JLC selbst.

| Option | Einstellung | Warum |
|---|---|---|
| Layers | **4** | GND-Fläche In1, Signale/+3V3 In2 |
| Dimensions | 110 × 97 mm | aus Gerber |
| PCB Qty | 5 (Minimum) | |
| PCB Thickness | **1.6 mm** | Board-Aufbau in KiCad |
| Impedance Control | **No** / Standard-Lagenaufbau | USB Full Speed, keine kontrollierte Impedanz nötig |
| Via Covering | **Tented** | Vias liegen u. a. dicht an Bauteilen; Board ist so ausgelegt |
| Surface Finish | **ENIG** (empfohlen) | ebene Pads für QFP-144 (0,5 mm) und SOT-353; HASL geht, ist aber uneben |
| Min via hole / Track | Standard (0,3 mm / 0,2 mm genügen) | Board nutzt 0,3-mm-Bohrung, 0,2-mm-Bahnen, 0,15 mm Abstand |
| Mark on PCB / Order number | „Remove“ (Aufpreis) oder egal | Bestückungsdruck ist voll, Nummer landet sonst irgendwo |
| Gold fingers, Castellated | No | |

## 2 Bestückung (PCB Assembly)

| Option | Einstellung |
|---|---|
| PCBA Type | **Standard** (Economic geht nur, wenn für 4 Lagen angeboten; Standard ist sicher) |
| Assembly Side | **Top Side** (alle Bauteile oben) |
| PCBA Qty | 2 (reicht für Inbetriebnahme + Ersatz) |
| Tooling holes | Added by JLCPCB |
| Confirm Parts Placement | **Yes** – Platzierung im Viewer selbst prüfen |

Upload: **BOM** `UFI_Headless-bom-jlc.csv`, **CPL** `UFI_Headless-cpl-jlc.csv`.

### Teileprüfung (BOM-Schritt)
- Alle LCSC-Nummern sind gesetzt und wurden am 05.–07.10.2026 auf Lager geprüft. Bei „out of stock“: gleiches Bauteil mit anderer LCSC-Nummer wählen (Wert/Gehäuse identisch).
- **Extended Parts** kosten je ~3 $ Rüstgebühr (STM32H723, PSRAM, TPS54202, TPS2116, INA180, 74LVC1G32, SD-NAND u. a.) – normal.
- **U15 SD-NAND** ist das teuerste Einzelteil: CSNP32GCR01-AOW (4 GB, C2841139) ca. 45 $/Stück (Stand 07.10.2026). Günstiger auf demselben Footprint: **MKDV8GIL-AST** (1 GB, C26159627, ca. 29 $) – reicht für ~14 DD- bzw. ~7 HD-Images. Tausch im BOM-Schritt per „Change Part“.
- **Durchsteckteile (THT)** – günstiger von Hand löten, bei der Teileauswahl abwählen („do not place“):
  J3 (FDD-Power), J4 (SWD), J5 (Debug-UART), J6 (34-pol), J8 (IEC), ab v0.7 auch J14 (Disk II 2×10), J17 (Netzteil), J18 (5-V-Jumper), J19 (Sync-Sensor). Wer sie bestücken lassen will: THT-Assembly zuwählen (Aufpreis).
- **Ohne LCSC-Nummer – selbst besorgen und einlöten:**

| Ref | Teil | Hinweis |
|---|---|---|
| J2 | Hohlstecker 5,5/2,1 mm, liegend | Footprint `BarrelJack_Horizontal` |
| J7 | Wannenstecker 2×12, 2,54 mm | Amiga-Header (Adapterkabel: `docs/Amiga_DB23_Adapter_Cable.md` im Repo-Hauptordner) |
| J9 | Stiftleiste 2×5, 2,54 mm | Erweiterung, optional |
| J11 | Stiftleiste 1×2 + Jumper | WRITE LOCK |
| J12 | Stiftleiste 2×4, 2,54 mm | Front-LEDs im Gehäuse (je LED eine Reihe: ungerade Pin = +, gerade = −) |
| J13 | Stiftleiste 1×3, 2,54 mm | Betriebsart-Schalter im Gehäuse (Kippschalter EIN-AUS-EIN, Kabel 1:1, Mitte = Pin 2) |
| J15 | Stiftleiste 1×2, 2,54 mm | /ENABLE zweites Apple-Laufwerk, optional |
| J16 | Stiftleiste 2×5, 2,54 mm | interner USB-Header, **Stift 9 vor dem Einlöten herausziehen** (im Druck „X“) |

- Testpunkte TP1–TP8 und Lötbrücken JP1/JP2 sind blankes Kupfer – nichts zu bestücken.

### Platzierung prüfen (Viewer-Schritt)
Die Liste `JLC_Rotation_Check.md` nennt alle Teile, deren Drehung im JLC-Viewer kontrolliert werden muss (Pin-1-Punkt am Footprint-Pin-1). Besonders: U5 (MCU), U6–U10, U11 (PSRAM), U12–U14, Q1–Q4, D-Arrays D9–D12, U15 (SD-NAND, LGA-8: Pin-1-Punkt oben links), ab v0.7 U16 (AHCT244), U17, U18 (ICL7662) und D15/D16.
Falsch gedrehte Teile im Viewer per Klick drehen; nichts in KiCad ändern.

## 3 Danach

- Lieferung: [Inbetriebnahme.md](Inbetriebnahme.md) Schritt für Schritt.
- Für die ersten Tests: Schrott-Laufwerk und Schrott-Disketten bereithalten, Labornetzteil mit Strombegrenzung.
