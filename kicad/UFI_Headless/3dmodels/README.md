# 3D-Modelle (Projekt)

Für diese Bauteile liefert die KiCad-10-Bibliothek kein 3D-Modell (auch nicht das offizielle
kicad-packages3D-Repository). Die STEP-Dateien stammen aus der LCSC/EasyEDA-Bauteilbibliothek, geholt am
25.10.2026 mit `easyeda2kicad` 1.0.1 (`--3d --lcsc_id=…`), und sind in den Footprints der Platine als
`${KIPRJMOD}/3dmodels/<Datei>` eingetragen.

| Datei | Ref | LCSC | Teil |
|---|---|---|---|
| `USB-C_SMD-TYPE-C-31-M-12_1.step` | J1 | C165948 | HRO TYPE-C-31-M-12 |
| `RJ45-TH_HR911105A.step` | J22 | C12074 | HanRun HR911105A |
| `BAT-TH_BS-07-A1BJ001.step` | BT1 | C2979167 | MYOUNG BS-07-A1BJ001 (CR2032) |
| `F1812_L4.5-W3.2-H1.0.step` | F1, F4 | C32404 | Polyfuse 1812 |
| `DFN-4_L1.5-W1.5-P0.8.step` | U25 | C2909890 | SHT40-AD1B |
| `OSC-SMD_4P-L3.2-W2.5-H1.0.step` | X1 | C5119013 | TCXO 3,2 × 2,5 mm |
| `LGA-8_L8.0-W6.2-TL.step` | U15 | C2841139 | CSNP32GCR01-AOW (auch im Projekt-Footprint eingetragen) |

Lage: Drehung und Versatz wurden aus dem Vergleich der Pad-Lagen (gleiche Pad-Nummern) des EasyEDA-Footprints
mit dem KiCad-Footprint bestimmt; Abweichung ≤ 0,1 mm, bei F1/F4 0,29 mm (andere Pad-Abstände der beiden
Bibliotheken). BT1: Pad 1 (+) passt, Drehung 180°. Im 3D-Render von oben und unten geprüft; mechanische
Maße für die Gehäuseprüfung sind die des Herstellermodells, nicht nachgemessen.
