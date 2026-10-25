# Bestellung bei JLCPCB – UFI Headless v1.0

Dateien: `kicad/UFI_Headless/fertigung/` (erzeugt mit `scripts/make_fab.sh`): `UFI_Headless_gerber.zip`,
`UFI_Headless-bom-jlc.csv`, `UFI_Headless-cpl-jlc.csv`, `JLC_Rotation_Check.md`. Freigabebericht:
[Freigabe_v1.0.md](Freigabe_v1.0.md).

## 1 Leiterplatte

Auf jlcpcb.com → *Order now* → `UFI_Headless_gerber.zip` hochladen. Größe und Lagenzahl liest JLC aus den Gerbern
(Jobdatei: 6 Lagen, 110,1 × 97,1 mm, 1,6 mm).

| Option | Einstellung | Warum |
|---|---|---|
| Layers | **6** | S/G/S/S/G/S: F.Cu Signale, In1 GND, In2/In3 Signale, In4 GND, B.Cu Signale (README, Abschnitt Layout B3) |
| Dimensions | 110 × 97 mm | aus Gerber |
| PCB Qty | 5 (Minimum) | |
| PCB Thickness | **1.6 mm** | Lagenaufbau in KiCad |
| Layer Stackup | **JLC06161H-3313** | Werte im Board-Setup (3313-Prepreg 0,0994 mm außen, Kerne 0,55 mm, 2116 innen); die USB-Breite 0,15/0,18 mm ist dafür geschätzt |
| Impedance Control | **No** | USB Full Speed und 100-Mbit-Ethernet laufen ohne kontrollierte Impedanz; die Werte im README sind Schätzungen |
| Via Covering | **Tented** | |
| Surface Finish | **ENIG** | bei 6 Lagen ohne Aufpreis; ebene Pads für QFP-144 und SOT-353 |
| Min via / Track | Standard genügt | kleinste Bahn 0,13 mm (Ethernet), kleinster Abstand 0,127 mm, Vias 0,45/0,2 bis 0,6/0,35 mm; JLC kann bei 6 Lagen 0,09 mm und 0,15/0,25 mm |
| Via in Pad | **No** | das Board nutzt kein Via-in-Pad |
| Mark on PCB / Order number | „Remove“ (Aufpreis) oder egal | |
| Gold fingers, Castellated | No | |

## 2 Bestückung (PCB Assembly)

| Option | Einstellung |
|---|---|
| PCBA Type | **Standard** |
| Assembly Side | **Top Side** – einziges Teil unten ist BT1 (siehe unten) |
| PCBA Qty | 2 (Inbetriebnahme + Ersatz) |
| Tooling holes | Added by JLCPCB |
| Confirm Parts Placement | **Yes** – Platzierung im Viewer selbst prüfen |

Upload: **BOM** `UFI_Headless-bom-jlc.csv`, **CPL** `UFI_Headless-cpl-jlc.csv`.

### Teileprüfung (BOM-Schritt)
- Alle 99 BOM-Zeilen haben eine LCSC-Nummer (304 Bestückplätze, keine DNP-Teile). Lager und Basic/Extended wurden am
  25.10.2026 über die JLC-Teilesuche geprüft: 37 Basic, 1 Preferred, 61 Extended (je ~3 $ Rüstgebühr). Bauteilkosten
  bei der niedrigsten Staffel ca. 70,6 $ je Platine ohne Rüstgebühren.
- **U5 STM32H723ZGT6 (C730146): 23 Stück** am 25.10.2026 – vor allem anderen sichern, eine der beiden Wege:
  1. **Reservieren:** im JLC-Konto unter der Teileverwaltung (Parts Manager / „My Parts“) mindestens
     2 Stück + 1 Reserve von C730146 in die eigene Teilebibliothek kaufen. Diese Teile gehören dann dem
     Konto und werden bei der Bestückung verwendet, auch wenn der freie Lagerbestand inzwischen leer ist.
  2. **Beistellteil:** STM32H723ZGT6 selbst kaufen (z. B. Distributor, Gehäuse LQFP-144 20 × 20 mm, Endung
     „T6“) und als Kundenteil an JLC schicken bzw. über die Teileverwaltung als eigenes Teil anlegen; in der
     BOM bleibt die Zeile U5, im Bestückungsauftrag „vom Kunden beigestellt“ wählen. Lieferzeit einplanen.
  Ausweichteil mit gleicher Pinbelegung: STM32H733ZGT6 (C730162, mit Krypto-Einheit, 4 Stück, ca. 14 $) –
  dass die Firmware darauf unverändert läuft, ist nicht nachgewiesen, daher nur nach Rückfrage.
  Die Bedienelemente heißen auf jlcpcb.com je nach Version anders; hier nicht im Konto geprüft.
- J7 Wannenstecker 2×12 (C5144580) 81 Stück. U15 CSNP32GCR01-AOW (C2841139) 389 Stück, ca. 39,55 $ –
  das teuerste Teil, bleibt bestückt.
- Bei „out of stock“: gleiches Bauteil mit anderer LCSC-Nummer wählen (Wert, Gehäuse und Pinbelegung identisch).
- **Durchsteckteile (THT):** J2, J3, J4, J5, J6, J7, J8, J12, J14, J16, J17, J19, J20, J21, J22, J23 und BT1. Alle haben
  eine LCSC-Nummer. Für eine fertig bestückte Platine **THT-Assembly zuwählen** (Aufpreis je Lötstelle); sonst bei der
  Teileauswahl abwählen und selbst löten.
- **BT1** (CR2032-Halter BS-07-A1BJ001, C2979167) sitzt auf der **Unterseite**. Entweder Bestückung „Both Sides“
  wählen (zweite Seite kostet extra – im Angebot prüfen) oder BT1 abwählen und von Hand einlöten. Pad 1 = Plus.
- **J16** (interner USB-Header 2×5): Stift 9 ist die Kodierung. Bei Maschinenbestückung bleibt er stehen und wird
  nachher gezogen oder abgekniffen (im Druck „X“).
- Testpunkte TP1–TP13 und die Lötbrücken JP1, JP3, JP5–JP8 sind blankes Kupfer – nichts zu bestücken. Grundzustand
  ab Werk: JP1 (DRATE auf J6 Pin 6) offen, JP3 (Amiga DF2) offen, JP5/JP6 offen (Rückseitendruck an JP6:
  „nur 400K“ – −12 V an J23 Pin 9 nur für das 400K-Laufwerk), JP7 Pad 1–2 (J6 Pin 33 auf GND),
  JP8 (−12 V an Disk II) geschlossen.

### Platzierung prüfen (Viewer-Schritt)
`JLC_Rotation_Check.md` listet 87 Teile, deren Drehung im JLC-Viewer kontrolliert werden muss (Pin-1-Punkt des
Modells am Pin 1 des Footprints). Die Offsets nach kicad-jlcpcb-tools sind eingerechnet, aber nicht im Viewer geprüft.
Besonders genau: U5 (LQFP-144), U15 (LGA-8, Pin 1 oben links), U11, U23 W5500, die SOT-353/SOT-363-Teile
(U14, U30, U31, U17, Q6–Q12 ohne hinterlegten Offset), D9–D16/D31 (SOT-23-6), U3/U19 (SOT-583 ohne Offset), X1, Y2/Y3,
J1 USB-C und J22 RJ45 (Öffnung zur Platinenkante). Falsch gedrehte Teile im Viewer per Klick drehen und den Offset in
`scripts/jlc_cpl.py` nachtragen; nichts in KiCad ändern.

## 3 Danach

- Lieferung: [Inbetriebnahme.md](Inbetriebnahme.md) und die Messliste in `docs/Bringup_2026-10-24.md` Abschnitt 6.
- Frontplatte: Anschlussplan J12 (LEDs, Schalter SCHREIBEN, Taster, Modusschalter) im README.
- Für die ersten Tests: Schrott-Laufwerk und Schrott-Disketten, Labornetzteil mit Strombegrenzung.
