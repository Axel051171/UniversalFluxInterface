# Bestellung bei JLCPCB – UFI Headless v0.5

Dateien: `kicad/UFI_Headless/fertigung/` bzw. Anhang des GitHub-Release **v0.5**.

## 1 Leiterplatte

Auf jlcpcb.com → *Order now* → `UFI_Headless_gerber.zip` hochladen. Größe (110 × 85 mm) und Lagen erkennt JLC selbst.

| Option | Einstellung | Warum |
|---|---|---|
| Layers | **4** | GND-Fläche In1, Signale/+3V3 In2 |
| Dimensions | 110 × 85 mm | aus Gerber |
| PCB Qty | 5 (Minimum) | |
| PCB Thickness | **1.6 mm** | Board-Aufbau in KiCad |
| Impedance Control | **No** / Standard-Lagenaufbau | USB Full Speed, keine kontrollierte Impedanz nötig |
| Via Covering | **Tented** | Vias liegen u. a. unter dem microSD-Slot; Board ist so ausgelegt |
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
- **Extended Parts** kosten je ~3 $ Rüstgebühr (STM32H723, PSRAM, TPS54202, TPS2116, INA180, 74LVC1G32, microSD-Slot u. a.) – normal.
- **Durchsteckteile (THT)** – günstiger von Hand löten, bei der Teileauswahl abwählen („do not place“):
  J3 (FDD-Power), J4 (SWD), J5 (Debug-UART), J6 (34-pol), J8 (IEC). Wer sie bestücken lassen will: THT-Assembly zuwählen (Aufpreis).
- **Ohne LCSC-Nummer – selbst besorgen und einlöten:**

| Ref | Teil | Hinweis |
|---|---|---|
| J2 | Hohlstecker 5,5/2,1 mm, liegend | Footprint `BarrelJack_Horizontal` |
| J7 | Wannenstecker 2×12, 2,54 mm | Amiga-Header (Adapterkabel: `docs/Amiga_DB23_Adapter_Cable.md` im Repo-Hauptordner) |
| J9 | Stiftleiste 2×5, 2,54 mm | Erweiterung, optional |
| J11 | Stiftleiste 1×2 + Jumper | WRITE LOCK |

- Testpunkte TP1–TP8 und Lötbrücken JP1/JP2 sind blankes Kupfer – nichts zu bestücken.

### Platzierung prüfen (Viewer-Schritt)
Die Liste `JLC_Rotation_Check.md` nennt alle Teile, deren Drehung im JLC-Viewer kontrolliert werden muss (Pin-1-Punkt am Footprint-Pin-1). Besonders: U5 (MCU), U6–U10, U11 (PSRAM), U12–U14, Q1–Q4, D-Arrays D9–D12, J10 (microSD – Kartenöffnung zeigt zur Oberkante).
Falsch gedrehte Teile im Viewer per Klick drehen; nichts in KiCad ändern.

## 3 Danach

- Lieferung: [Inbetriebnahme.md](Inbetriebnahme.md) Schritt für Schritt.
- Für die ersten Tests: Schrott-Laufwerk und Schrott-Disketten bereithalten, Labornetzteil mit Strombegrenzung.
