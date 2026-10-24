# UFI Headless v1.0 – Stufenplan (Branch `hw/v0.7-frontpanel`)

Vorgaben des Owners (24.10.2026): geschlossenes Gehäuse, von außen nur LAN und USB
erreichbar, nach dem Einbau nichts steckbar; eine Platinenkante Eingang (Host, Versorgung,
Debug), die gegenüberliegende Kante Laufwerke; Frontplatte für LEDs, Taster A/B und
Modusschalter über J12; komplette Bestückung (SW3/SW4 einzige DNP); externe Amiga-Laufwerke
über DB23; LAN über W5500. Entwurfsvorgabe mit Begründungen:
[docs/PCB_Revision_Empfehlungen_2026-10-24.md](../../docs/PCB_Revision_Empfehlungen_2026-10-24.md),
Funktionsübersicht: [docs/Funktionsuebersicht_UFI_Headless_v1.0.md](../../docs/Funktionsuebersicht_UFI_Headless_v1.0.md).

Tor je Stufe: ERC über das ganze Projekt 0/0, Firmware-Pinmap im selben Commit, wenn ein
MCU-Pin wandert; Commit pro Stufe mit Referenzen und offenen Punkten.

| Stufe | Ziel | Blätter | Abschluss | Stand |
|---|---|---|---|---|
| A1 | 5-V-Quelle automatisch: J18 raus, U19 TPS2116 (J17 vor Buck) | Power | ERC 0/0 | erledigt, 9d5b134 |
| A2 | RN1–RN8 → 25 × 1 kΩ 0603 (R75–R99) | Flux_Interface, IEC_Bus, FDD_Connectors | ERC 0/0 | erledigt, 9d5b134 |
| A3 | Front-Header J12 2×8, LEDs/Taster/Modusschalter nach außen, J9/J13/D5–D8 raus, SW3/SW4 DNP | MCU_Core | ERC 0/0 | erledigt, 9d5b134 |
| A3b | PESD3V3L1BA D18–D26, R105 1 kΩ vor FRONT_3V3, I²C-Testpads TP10–TP13 | MCU_Core | ERC 0/0 | erledigt, 5d81b31 |
| A4 | Rückschleife ohne Jumper (U22 TS5A3166, Q5, LOOP_EN), INDEX_SIM auf PE5/TIM15 über U7F, WGATE_SENSE über U10F, JP4 statt J11, J20 GUARD, J21 LA; Firmware-Pinmap | Flux_Interface, IEC_Bus, MCU_Core, firmware | ERC 0/0, Gate grün, Build 0 Warnungen | erledigt, 9e0313e |
| A5 | entfällt (Owner 24.10.: Stecker intern wie bisher, J8/J7/J3 bleiben Stiftleisten; DIN-6 und DB23 sind Gehäusebuchsen am Kabel, einmal bei der Montage gesteckt) | – | – | gestrichen |
| A6 | W5500 + RJ45-Magjack an SPI3 (PC10 SCK, PC11 MISO, PC12 MOSI, PA15 CS, PD5 INT, PD10 RST), 25-MHz-Quarz, Abblockung, Firmware-Pinmap (Treiber später) | neues Blatt LAN, MCU_Core, firmware | ERC 0/0 | offen |
| A7 | Board-ID: neuer Teilerwert R18 für v1.0, Firmware `BOARD_ID_V10_MV` | MCU_Core, firmware | Gate grün | offen |
| A8 | Schaltplan-Review nach Skill `kicad-schaltplan-review` (ESD, Leckströme, LED-Ströme, 5-V-Toleranz, Pinkonflikte, verwaiste Netze) | alle | Befunde abgearbeitet oder begründet offen | offen |
| B | Layout: USB-C in Kantenaussparung, Eingangskante gegen Laufwerkskante, J12 an einer Seitenkante, neue Teile platzieren und routen, Siebdruck/F.Fab mit Öffnungsseite, DRC 0/0 gegen JLC-Regeln | .kicad_pcb | DRC 0/0 | offen |
| C | Fertigungsfreigabe nach Skill `pcb-fertigungsfreigabe`: BOM mit Herstellernummern und Lager, CPL ohne DNP, Rotation geprüft, Gerber/Bohr/STEP, Tag `hw-v1.0` | fertigung/ | Freigabebericht | offen |

## Offene Fragen an den Owner
- Gehäuse (Typ, Innenmaße) bestimmt die Lage der Kanten und die USB-C-Höhe; bis dahin bleibt der Umriss 110 × 85 mm.
- SW3/SW4 bestücken oder nicht (derzeit DNP, Taster sitzen an der Front).

## Nach Stufe A4 noch in der Firmware zu nutzen
LOOP_EN/WGATE_SENSE im Selbsttest (WDATA→RDATA, TRK0, WGATE), Guard-Status in BOARD_STATUS,
GUARD_OFF beim Abschalten der Laufwerksversorgung; W5500-Treiber nach A6.
