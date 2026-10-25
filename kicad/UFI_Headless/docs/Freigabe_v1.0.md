# Fertigungsfreigabe UFI Headless v1.0 (Stufe C, 25.10.2026)

Geprüft nach dem Skill `pcb-fertigungsfreigabe` auf dem Stand von Branch `hw/v0.7-frontpanel` nach den Commits
B3a (1c7b326), B3 (7d1729e) und c6cfae9. Jede Zeile ist gemessen oder exportiert; was nicht geprüft werden konnte,
steht als **nicht geprüft** da. Bestellung: [Bestellung_JLC.md](Bestellung_JLC.md).

## 1 Prüfpunkte

| Prüfpunkt | Ergebnis | Nachweis |
|---|---|---|
| ERC ganzes Projekt | **0 Fehler, 0 Warnungen** | `kicad-cli sch erc --severity-all --exit-code-violations`, Exit 0 |
| DRC mit Schaltplan-Parität | **0 Verstöße, 0 unverbunden, 0 Paritätsprobleme**, keine Ausnahmen | `kicad-cli pcb drc --severity-all --schematic-parity --exit-code-violations`, Exit 0, JLC-Regeln in `UFI_Headless.kicad_dru` |
| Berichte | `fertigung/erc.rpt` (2026-10-25T23:25:05) und `fertigung/drc.rpt` (2026-10-25T23:25:10), beide nach dem letzten Platinen-Commit 3cf7f11 (23:24:40) erzeugt, je 0 Verstöße | `kicad-cli … --severity-all [--schematic-parity] --exit-code-violations`, Exit 0 |
| Ignorierte Prüfungen (Projekteinstellung, keine Ausnahmen einzelner Fehler) | Auf einer Kopie mit allen ignorierten Regeln als Warnung nachgeprüft: **missing_courtyard 0** (kein Footprint ohne Courtyard, Kollisionsprüfung also vollständig); **single_global_label 1** – „VBUS“, das Netz ist auf dem Power-Blatt vollständig verbunden (J1, F1, J16, D1, U1, U24, R9), das Label braucht kein Gegenstück; **footprint_filter 19 / footprint_filters_mismatch 7** – Filter der Bibliothekssymbole (z. B. Polyfuse ↔ 1812, 74HC14-Symbol ↔ SOIC/TSSOP, eigene IDC-Footprints), die gewählten Footprints sind gewollt; **track_not_centered_on_via 66** – Bahn endet auf dem Via, aber nicht in der Mitte, elektrisch verbunden (0 unverbunden); footprint_type_mismatch, tuning_profile_track_geometries, four_way_junction, simulation_model_issue je 0 | Skript `unignore.py` (Scratchpad), ERC/DRC auf der Kopie |
| Leiterbahnbreiten Lastpfade | **32 von 32 Pfaden OK, 0 HALS, 0 FEHLER** | `check_routing.py --rules routing_rules.json`, Exit 0, neue Skill-Fassung vom 25.10.: liest gesperrte Segmente, Pfad = kürzester Weg über der Mindestbreite, Pad-Hälse ≤ 1,5 mm als HALS, Bahnenden auf dem Pad als ein Knoten. Gegenprobe mit allen Pfaden auf 1,0 mm: die 0,3-mm-Anschlüsse an U3/U19 (TPS2116, 0,5-mm-Raster), den Kelvin-Abgriffen und den Abblock-C sind 1,6–3,6 mm lang, also länger als 1,5 mm – deshalb stehen diese Pfade in `routing_rules.json` mit 0,3 mm |
| Lagen und Fertigungsgrenzen | 6 Lagen, 1,6 mm, kleinste Bahn 0,13 mm, kleinster Abstand 0,127 mm, Vias 0,45/0,2 (19), 0,5/0,25 (858), 0,6/0,3 (179), 0,6/0,35 (1) | Gerber-Jobdatei, Board-Auswertung; JLC 6 Lagen: 0,09 mm, Via 0,15/0,25 mm |
| JLC-Grenzwerte einzeln | Bauteil-PTH-Restring: 255 Pads, keines unter 0,15 mm (JLC Mehrlagen, absolutes Minimum). Bohrungsabstand Pad–Pad kleinster 0,55 mm (JLC 0,45), Via–Via/Pad kleinster 0,283 mm (JLC 0,2). Kupfer–Kante 0,3 mm (JLC 0,2). Via-Restring 0,125 mm: zulässig, JLC erlaubt bei 6 Lagen Vias 0,15/0,25 mm | Skripte `pth_ring.py`/`hole_gap.py` (Scratchpad) gegen [JLC Capabilities](https://jlcpcb.com/capabilities/pcb-capabilities) und [JLC 6-Lagen](https://jlcpcb.com/6-layer-pcb), 25.10.2026 |
| Umriss | geschlossen, 1 Kontur, 110,1 × 97,1 mm | pcbnew-Auswertung, Gerber-Jobdatei |
| Befestigung | 6 × M3 (3,2 mm) bei x 3,5 / 106,5 und y 3,5 / 71,0 / 93,5 | pcbnew-Auswertung; gegen das Gehäuse **nicht geprüft** (Gehäuse noch offen) |
| Stecker an der Kante | J1 USB-C 1,5 mm und J22 RJ45 4,7 mm über die Eingangskante (Öffnung nach außen, rot 180); J14/J23/J3 an der Laufwerkskante (0,5 mm); J12 und J7 rechts bündig, J8 und J17 links | pcbnew-Auswertung der Bauteilgrenzen |
| 3D / Bauhöhe | STEP exportiert (`fertigung/UFI_Headless.step`) | Kollisionen mit dem Gehäuse und Höhe unter dem Deckel **nicht geprüft** |
| BOM vollständig | 99 Zeilen, 304 Bestückplätze, **alle mit LCSC-Nummer**, keine DNP-Teile | `UFI_Headless-bom-jlc.csv`, Abgleich mit der CPL: jede BOM-Referenz hat eine CPL-Zeile, keine Doppelungen |
| Lager und Basic/Extended | 37 Basic, 1 Preferred, 61 Extended; Bauteile ca. 70,6 $ je Platine | JLC-Teilesuche 25.10.2026, Tabelle `fertigung/Lagerpruefung_2026-10-25.md` |
| CPL | 304 Zeilen, Rotationsoffsets eingerechnet, Seite je Teil | `UFI_Headless-cpl-jlc.csv` |
| Rotation gegen JLC-Vorschau | Checkliste mit 87 Teilen erzeugt | Abgleich im JLC-Viewer **nicht geprüft** (geht erst beim Bestellen) |
| Kennzeichnung | Revision und Datum im Siebdruck („UFI Headless v1.0  2026-10-25“, B.SilkS, in Stufe C ergänzt), Pin-1- und Polaritätsmarken, Pin-Labels an allen Steckern, Testpunkte TP1–TP13 | Silk-DRC 0 Warnungen; Lesbarkeit am Druck **nicht geprüft** |
| Debug-Zugang | J4 SWD 2×5 1,27 mm, J5 UART, J21 Logikanalysator 2×10, TP1–TP13 | Netzliste |
| Firmware-Pinbelegung | gegen die Netzliste geprüft (A8, 77 Pins) und nach B3a nachgezogen (PC7, PC9, PC13); ARM-Build 0 Warnungen, Offline-Gate grün | Commits 1c7b326, c6cfae9 |

## 2 Offene Risiken

1. **U5 STM32H723ZGT6:** nur 23 Stück bei JLC (25.10.). Vor der Bestellung prüfen und reservieren.
2. **BT1 auf der Unterseite:** einziges Teil unten, THT-Halter. Bestückung beider Seiten kostet extra; sonst von Hand löten.
3. **Impedanzen nur geschätzt:** USB 0,15/0,18 mm ≈ 91 Ω differenziell, Ethernet 0,13 mm ≈ 54 Ω je Leitung, ohne Kopplung ≈ 108 Ω statt 100 Ω (IPC-2141A). Für USB Full Speed und 100BASE-TX auf kurzen Strecken unkritisch, aber nicht nachgerechnet. Vor der Bestellung im JLC-Impedanzrechner prüfen.
4. **Flachbandkabel J6/J7 hinter J14/J23:** Die 34- und 26-poligen Stecker sitzen 11 mm hinter den Disk-II- und Mac-Steckern an der Kante. Die Kabel von J6/J7 laufen über die Stecker von J14/J23. Am Gehäuse mit echten Kabeln prüfen.
5. **Rotationen SOT-363/SOT-583 ohne hinterlegten Offset** (Q6–Q12, U17, U3, U19): im JLC-Viewer besonders genau prüfen.
6. **PC13 treibt FDD_MOTOR_B** (Q6 Gate, B3a): Backup-Domain-Pin, 3 mA, langsam. Für das statische MOSFET-Gate mit 100k reicht das laut Datenblatt-Familie; am Board messen.
7. **Aus dem A8-Review weiter offen:** BSS138DW R_DS(on) bei 3,3 V Gate (R4) und AHCT244 Ioff bei abgeschalteter FDD_5V (R5) – Messungen bei der Inbetriebnahme.

## 3 Nur an echter Hardware prüfbar

Ungetestet heißt ungetestet, nicht bestanden. Die Messliste steht in `docs/Bringup_2026-10-24.md` Abschnitt 6 und
[Inbetriebnahme.md](Inbetriebnahme.md):

- Versorgung: eFuse-Grenzen 1,43–1,7 A, Sanftanlauf ≈ 3 ms, OVP/UV, Schienenspannungen gegen das Multimeter.
- Schreibsperre: J6 Pin 24 bleibt high bei gesetzter Sperre während eines Lesevorgangs; Selbsttest `wgate_lock`.
- MOSFET-Ausgänge: V_OL mit 150-Ω-Abschluss des Laufwerks.
- TCXO/HSE-Bypass-Pegel, LSE-Anschwingen und CR2032-Pufferung, SHT40 am echten Pad.
- USB-Enumeration, Ethernet-Link (Firmware fehlt noch, siehe LAN-Stufe), SD-NAND-Formatierung.
- SYNC-Sensor-Wiederholgenauigkeit, Pin 2/4/33/34 echter Laufwerke, Mac-Port J23.
- Lesen und Schreiben mit echten Laufwerken an J6, J7, J14, J23, IEC an J8.
