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
| A6 | W5500 + RJ45-Magjack HR911105A an SPI3 (PC10 SCK, PC11 MISO, PC12 MOSI, PA15 CS, PD5 INT, PD10 RST), Werte nach WIZnet-Referenz, 25-MHz-Quarz, Abblockung, Firmware-Pinmap (Treiber später) | neues Blatt LAN, MCU_Core, firmware | ERC 0/0, Gate grün | erledigt (Commit A6) |
| A7 | Board-ID: R18 22k (2,27 V) für v1.0, Firmware `BOARD_ID_V10_MV`, `board_rev` 10, Host-Anzeige v1.0 | MCU_Core, firmware, host | Gate grün | erledigt (Commit A7) |
| A8 | Schaltplan-Review nach Skill `kicad-schaltplan-review` (ESD, Leckströme, LED-Ströme, 5-V-Toleranz, Pinkonflikte, verwaiste Netze) | alle | Befunde abgearbeitet oder begründet offen | offen |
| B1 | Platzierungsentwurf: Netzliste in die Platine übernommen (94 neue Footprints, alte RN/J9/J11/J13/J18/LED-Teile entfernt), Stecker auf Eingangs- und Laufwerkskante, J22 neben J2, J12 rechts, J8 links, LAN-Block neben U15; Bild `docs/placement_v1.0_draft_2026-10-24.png` | .kicad_pcb | Bild für den Owner | erledigt (Commit B1), unverdrahtet, kein DRC |
| B2 | Layout: USB-C in Kantenaussparung, Feinplatzierung (Restüberlappungen der Passivteile), alte Leiterbahnen der geänderten Netze entfernen, neue Teile routen, Siebdruck/F.Fab mit Öffnungsseite, DRC 0/0 gegen JLC-Regeln | .kicad_pcb | DRC 0/0 | erledigt (Commit B2): DRC 0 Fehler / 0 Warnungen / 0 unverbunden mit Schaltplan-Parität; J1/J22 mit Öffnung zur Kante (bei Rot 0 zeigten beide nach innen – Fehler seit v0.1); Quarz-Cluster und 3V3A-Filter unter U23 neu gelegt; QFP-Reihen mit 0,5/0,25-Vias aufgefächert (`fanout.py`), Rest mit `batch_route.py`; SW3 (DNP) auf B.Cu unter J22 geparkt; Bild `docs/layout_v1.0_2026-10-24.png`. Freerouting lief in den 1800-s-Timeout und ließ 154 Verbindungen offen – deshalb die ECO-Werkzeuge. **Nicht erfüllt:** Lastpfad-Breiten nach Skill `pcb-routing` (Messung 24.10.: alle 16 Lastnetze 0,2–0,25 mm + Pours, Pfade bis 122 mm) → B3 |
| B3 | Lastpfade nach Skill `pcb-routing`: Netzklasse `Load` 1,0 mm (16 Netze inkl. Zwischenknoten FDD5_F/FDD5_S/FDD12_F/FDD12_S/+5V_PRI/+5V_PSU_F, `scripts/eco/set_netclasses.py`), Regeln in `routing_rules.json`; Versorgungskette kompakt umplatzieren (F2/R26/Q1/U12/D13 und F3/R29/Q3/U13/D14 an J3; D2/C1–C3/U2/L1/U19/U3 als Block), Lastpfade zuerst von Hand mit 1,0 mm und ≥ 2 Vias je Lagenwechsel, verdrängte Signale danach neu routen | .kicad_pcb, .kicad_pro | `check_routing.py` 0 FEHLER, DRC 0/0 | offen – Owner-Entscheidung: Umplatzierung der Power-Kette (Vorschlag) oder 0,5 mm + Pours als Kompromiss |
| C | Fertigungsfreigabe nach Skill `pcb-fertigungsfreigabe`: BOM mit Herstellernummern und Lager, CPL ohne DNP, Rotation geprüft, Gerber/Bohr/STEP, Tag `hw-v1.0` | fertigung/ | Freigabebericht | offen |

## Offene Fragen an den Owner
- Gehäuse (Typ, Innenmaße, Wandstärke) bestimmt die Kantenausschnitte für J1 (USB-C) und J22 (RJ45); der Umriss ist 110 × 97 mm.
- SW3/SW4 bestücken oder nicht (derzeit DNP, Taster sitzen an der Front); SW3 steht auf B.Cu unter J22, SW4 bleibt auf F.Cu.
- B3: Power-Kette umplatzieren (Lastpfade 1,0 mm nach `pcb-routing`) oder Kompromiss 0,5 mm + Pours.

## Nach Stufe A4 noch in der Firmware zu nutzen
LOOP_EN/WGATE_SENSE im Selbsttest (WDATA→RDATA, TRK0, WGATE), Guard-Status in BOARD_STATUS,
GUARD_OFF beim Abschalten der Laufwerksversorgung; W5500-Treiber nach A6.
