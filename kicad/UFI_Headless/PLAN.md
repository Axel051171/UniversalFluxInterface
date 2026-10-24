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
| A9 | Rettungsfunktionen und Schutz vor B3 (Owner-Vorgabe 24.10., Details unten): Schreibsperre im Grundzustand, INDEX auf TIM2_CH4, Sanftanlauf Q1/Q3, RTC, CC-ESD, J20-Serienwiderstände, verpolsichere J17/J3, Dioden-ODER 12 V, 50-mΩ-Shunts + F2 1812, SW3/SW4 raus | MCU_Core, Power, firmware | ERC 0/0, Firmware-Pinmap und Build 0 Warnungen im selben Commit | A9a erledigt (Commit 233b54a: Stecker, D27, CC-ESD U24, R125–R127, R26/R29 50 mΩ + Firmware-Skalierung, F2 1812, SW3/SW4 raus, ERC 0/0); A9b erledigt (Commit A9b: R33 10k Pull-up, JP4 raus, U14 74LVC1G332, R131 100k auf WLOCK_FW, R130 470 Ω vor PE6, J12 Pin 10/12 = FRONT_WRLED/WLOCK_HW mit D28/D29 + R128 1k, INDEX zusätzlich auf PB11; Firmware PE4 Ausgang WLOCK_FW (Reset = gesperrt, nur während eines Schreibauftrags frei), PE6 WREN_SENSE, PB11 TIM2_CH4 AF1 – Capture nutzt weiter CH2); A9c erledigt (Commit A9c: X1 TCXO 25 MHz HCI TC32H4 C5119013 statt Y1/C38/C39 mit R132 10k am E/D-Pin und C75, PH1 offen; Y3 32,768 kHz Epson FC-135 C48615 + C76/C77 10 pF an PC14/PC15; VBAT von +3V3 getrennt mit BT1 CR2032-Halter MYOUNG BS-07-A1BJ001 C2979167 + C78; U25 SHT40-AD1B C2909890 + C79 an TP10–TP13; Firmware `ufi_clock.c`: HSE-Bypass zuerst, Quarz-Rückfall für v0.x, `ufi_clock_rtc_init()` nur auf board_rev 10 mit LSE medium-high, Kalender ab 2000-01-01; Sanftanlauf entfällt zugunsten A11 M2) |
| A11 | Erkenntnisse aus dem Gerätevergleich (Owner-Recherche 24.10., Details unten); M-Punkte Pflicht, S-Punkte Soll, K-Punkte nach Owner-Entscheid | Power, Flux_Interface, FDD_Connectors, Apple_Port, MCU_Core | ERC 0/0, Firmware-Pinmap + Build im selben Commit, Timer-Zuteilungstabelle in docs | offen – M1 ersetzt den 74LVC1G332 aus A9b, M2 ersetzt A9c Sanftanlauf und F2 aus A9a |
| B1 | Platzierungsentwurf: Netzliste in die Platine übernommen (94 neue Footprints, alte RN/J9/J11/J13/J18/LED-Teile entfernt), Stecker auf Eingangs- und Laufwerkskante, J22 neben J2, J12 rechts, J8 links, LAN-Block neben U15; Bild `docs/placement_v1.0_draft_2026-10-24.png` | .kicad_pcb | Bild für den Owner | erledigt (Commit B1), unverdrahtet, kein DRC |
| B2 | Layout: USB-C in Kantenaussparung, Feinplatzierung (Restüberlappungen der Passivteile), alte Leiterbahnen der geänderten Netze entfernen, neue Teile routen, Siebdruck/F.Fab mit Öffnungsseite, DRC 0/0 gegen JLC-Regeln | .kicad_pcb | DRC 0/0 | erledigt (Commit B2): DRC 0 Fehler / 0 Warnungen / 0 unverbunden mit Schaltplan-Parität; J1/J22 mit Öffnung zur Kante (bei Rot 0 zeigten beide nach innen – Fehler seit v0.1); Quarz-Cluster und 3V3A-Filter unter U23 neu gelegt; QFP-Reihen mit 0,5/0,25-Vias aufgefächert (`fanout.py`), Rest mit `batch_route.py`; SW3 (DNP) auf B.Cu unter J22 geparkt; Bild `docs/layout_v1.0_2026-10-24.png`. Freerouting lief in den 1800-s-Timeout und ließ 154 Verbindungen offen – deshalb die ECO-Werkzeuge. **Nicht erfüllt:** Lastpfad-Breiten nach Skill `pcb-routing` (Messung 24.10.: alle 16 Lastnetze 0,2–0,25 mm + Pours, Pfade bis 122 mm) → B3 |
| B3 | Lastpfade nach Skill `pcb-routing`: Netzklasse `Load` 1,0 mm (16 Netze inkl. Zwischenknoten FDD5_F/FDD5_S/FDD12_F/FDD12_S/+5V_PRI/+5V_PSU_F, `scripts/eco/set_netclasses.py`), Regeln in `routing_rules.json`; Versorgungskette kompakt umplatzieren (F2/R26/Q1/U12/D13 und F3/R29/Q3/U13/D14 an J3; D2/C1–C3/U2/L1/U19/U3 als Block), Lastpfade zuerst von Hand mit 1,0 mm und ≥ 2 Vias je Lagenwechsel, verdrängte Signale danach neu routen | .kicad_pcb, .kicad_pro | `check_routing.py` 0 FEHLER, DRC 0/0 | offen – Owner-Entscheidung: Umplatzierung der Power-Kette (Vorschlag) oder 0,5 mm + Pours als Kompromiss |
| C | Fertigungsfreigabe nach Skill `pcb-fertigungsfreigabe`: BOM mit Herstellernummern und Lager, CPL ohne DNP, Rotation geprüft, Gerber/Bohr/STEP, Tag `hw-v1.0` | fertigung/ | Freigabebericht | offen |

### A9 im Detail (Vorgabe des Owners, 24.10.2026)
1. **Schreibsperre im Grundzustand an (A9b):** R33 von GND auf +3V3, 10 kΩ statt 100 kΩ (WLOCK_HW high = gesperrt); JP4 entfällt; U14 → 74LVC1G332 (3-fach-ODER) mit FDD_WGATE, WLOCK_HW, WLOCK_FW → FDD_WGATE_G; WLOCK_FW an PE4 als Ausgang mit 100 kΩ Pull-up (Reset/Boot = gesperrt); WLOCK_HW zusätzlich an PE6 (WREN_SENSE, Eingang); J12 Pin 12 = FRONT_WREN (Schalter nach GND), Pin 10 = FRONT_WRLED (LED-Anode über 1 kΩ an +3V3, Kathode an FRONT_WREN), beide mit PESD3V3L1BA; GND bleibt auf 2/4/6/8. Geschrieben wird nur bei Schalter EIN, Firmware-Freigabe und aktivem WGATE; abgezogenes Frontkabel = gesperrt.
2. **INDEX auf TIM2_CH4 (A9b):** FDD_INDEX zusätzlich an PB11 (Pin 70); TIM2 misst Flux (CH1), Pulsbreite (CH2) und Index (CH4) auf einer Zeitbasis; AF gegen das Datenblatt prüfen.
3. **Sanftanlauf Q1/Q3 (A9c):** Q1: 10 kΩ zwischen Q2-Drain und Gate, 100 nF/16 V X7R Gate–FDD_5V (Rampe ≈ 1,7 ms); Q3: 220 nF/25 V Gate–FDD_12V, R32 bleibt Serienwiderstand (Rampe ≈ 3,3 ms); Abschalten Q1 ≈ 10 ms, Q3 ≈ 2 ms; bei der Inbetriebnahme mit 100 µF Last messen.
4. **RTC (A9c):** 32,768-kHz-Quarz an PC14/PC15 (Pin 8/9), CL 6–7 pF, Lastkondensatoren aus CL und Streukapazität; kurz am Pin mit GND-Schirmung; VBAT (Pin 6) von +3V3 trennen: CR2032-Halter + 100 nF, Umschalter ist im MCU.
5. **CC-ESD (A9a, erledigt):** U24 USBLC6-2SC6 auf CC1/CC2 (VRWM 5,25 V, gleiche Bauteilnummer wie U1) statt zweier Einzeldioden.
6. **J20 (A9a, erledigt):** R125–R127 220 Ω in Serie auf GUARD_OFF/ERR5/ERR12, Steckerseite GUARD_*_J.
7. **Stecker (A9a, erledigt):** J17 und J3 JST XH 4-polig B4B-XH-A (C144395), Belegung unverändert; Adapterkabel Berg ↔ XH in `docs/Inbetriebnahme.md`. D27 SS54 von J17 Pin 4 (+12V_PSU) auf +12V, J2 bleibt über D2.
8. **5-V-Kette (A9a, erledigt):** R26/R29 50 mΩ 1 % (RLM12FTCMR050 C912768, Alternative ERJ8CWFR050V C713437; Lager in Stufe C prüfen), F2 1812 wie F3 (C20999, 24 V, halber Widerstand des 1206-Typs); Firmware `ufi_board.c` skaliert nach Platinenrevision (v1.0 1 mV/mA).
9. **SW3/SW4 (A9a, erledigt):** aus Schaltplan gestrichen; Platine folgt in B3.
10. **Owner 24.10.: beide übernehmen (A9c):** SHT40 an TP10–TP13 (I²C, an der Lufteinlasskante); TCXO 25 MHz statt Y1 (≈ 1 ppm, Taktausgang an OSC_IN, C19/C18 entfallen).

Firmware zu A9b/A9c im selben Commit: Pinmap PE4 WLOCK_FW (Startwert high), PE6 WREN_SENSE, PB11 INDEX auf TIM2_CH4 (A9b, erledigt); LSE/RTC einschalten (A9c, erledigt: `ufi_clock_rtc_init()` aus `ufi_init()` nach der Platinenerkennung, Getter/Setter `ufi_clock_rtc_get/set()` ohne Protokoll-Op). Noch offen (A10): Zeitstempel aus RTC und Board-ID in die Metadaten jeder Aufnahme, Op zum Stellen/Lesen der Uhr, SHT40 auslesen (Protokollerweiterung → `docs/USB_Protokoll.md`, Abstimmung mit der UFT-Sitzung). A9c-Bauteilstand (JLC-Bestand 24.10.): TCXO HCI TC32H4-25.000-33GNDTNL C5119013 (1270; Alternative ECS-TXO-3225-250-TR C2451321, 61), Quarz Epson FC-135 Q13FC13500002 C48615 (2860; Abracon ABS07-32.768KHZ-7-T C276424, 5691), Halter BS-07-A1BJ001 C2979167 (1501, THT/Wellenlöten), SHT40-AD1B-R2 C2909890 (19 423; R3 C2848306). STM32H723-Datenblatttabelle für den HSE-Bypass-Pegel (0,7/0,3 VDD) konnte nicht abgerufen werden (ST-Download blockiert); der HCMOS-TCXO liefert laut Datenblatt ≥ 90 % VCC / ≤ 10 % VCC und erfüllt beide Werte mit Reserve.

### A11 im Detail (Vorgabe des Owners, 24.10.2026, Gerätevergleich KryoFlux/Greaseweazle/Applesauce/Pauline/DiscFerret/FluxEngine, SA400/SA850/TEAC-Handbücher)
- M1 Schreibsperre: LOCK = WLOCK_HW ∨ WLOCK_FW (74LVC1G32); ein 74LVC2G32 gatet FDD_WGATE→FDD_WGATE_G **und** APL_WRREQ→U16 (heute läuft WRREQ an der Sperre vorbei). Ersetzt den 74LVC1G332 aus A9b; ein späterer DB19-Port hängt an derselben Sperre.
- M2 Laufwerksversorgung: 2× TPS25921A statt F2/F3/Q1–Q4 (SS ≈ 2–5 ms, ILIM ≈ 1,5 A, OVP 6,0 V / 14,0 V, FLT an GPIO; ENUV vom GPIO über Teiler mit Pull-down = aus im Reset). R26/R29 + INA180 bleiben. D3 → SMAJ13A. Ersetzt A9c Sanftanlauf und F2 aus A9a.
- M3 Shugart/Amiga-Ausgänge: duale N-MOSFETs (2N7002DW-Klasse, 100 k Gate-Pull-down) statt U6/U7 (LS07 garantiert 0,4 V nur bei 16 mA, 150-Ω-Abschlüsse brauchen ~31 mA); Logik in der Firmware invertieren; eigener MOSFET für INDEX_SIM.
- M4 R75–R91 (Shugart/Amiga) von +5V auf FDD_5V (kein Rückspeisen ins stromlose Laufwerk); R92–R96 (IEC) bleiben an +5V.
- M5 J19 Pin 3 auf eigenes Netz SYNC: 10 k Pull-up, Schmitt-Gatter (S6), ESD, eigener Timerkanal (heute speist der Sync-Sensor auf INDEX).
- M6 JP2 streichen, J6 Pin 3 fest GND (SA800/TEAC).
- S1/S2 Pin 2 und Pin 4: je MOSFET-Ausgang (per Firmware abschaltbar) + Empfänger am dritten 74LVC14 (S6). S3 Pin 33 Flippy: Lötjumper GND/Signal, Signal = GPIO über 2,4 k, 2,4 k nach GND. S4 FDD_5V/FDD_12V über Teiler an PC4/PC5 (ADC), Grenzen 4,5 V / 10,8 V in der Firmware, Werte in die Metadaten. S5 Disk II J14.9 −12 V: Jumper + Sicherung, Siebdruck „kein Mac-Laufwerk“, ICL7662-Last nachrechnen. S7 USB-Schirm über 1 MΩ ‖ 4,7 nF.
- Timer-Zuteilung TIM2 (32 Bit) als Tabelle in docs: Flux CH1, Pulsbreite CH2, Index CH4 (A9b), SYNC, Apple-RD CH3 – je Betriebsart gegen das H723-Datenblatt.
- K1 Apple-3,5″/Mac-Port (DB19-Belegung, PWM Pin 10): **Owner 24.10.: keine D-Sub-Buchse auf der Platine** – wie J7/DB23 als interne Stiftleiste (2×10) mit Kabel zur Gehäusebuchse, die Buchsen hat der Owner. **K1 kommt (Owner 24.10.).** K2 XP1541-Header: **gestrichen** (Owner 24.10.: Flux über das PC-Laufwerk ist der bessere Rettungsweg). K3 34-Pin Rechnerseite, K4 RD/INDEX-Pull-up 470 Ω per Jumper, K5 34-Pin-Prüfstecker: nach Owner-Entscheid.
- Vor der Bestellung prüfen: Amiga-Adapterkabel mit Einzeldrähten (IDC-Quetschung auf DB23 legt J7.23 +12 V auf DB-Pin 12 = +5 V des Laufwerks); `docs/Inbetriebnahme.md` nennt noch „Jumper J11 (WRITE LOCK)“.
- Reihenfolge: A9 (A9a/A9b/A9c erledigt) → A11 → A8 Review → B3 → A10 (Firmware: Ringpuffer vor dem Index, ≥ 5 Umdrehungen, Index-/Sektorzeitstempel, Pin 34 READY/DSKCHG, Amiga-ID, Spur puffern vor WGATE, Schrittlimit) → C.

## Offene Fragen an den Owner
- A11: K1 kommt als interne Stiftleiste mit Kabel zur Gehäusebuchse (keine D-Sub-Buchse auf der Platine), K2 gestrichen, SHT40 + TCXO in A9c (alles Owner 24.10.). Noch offen: Amiga-Kabel mit Einzeldrähten bestätigt? K3–K5?
- Gehäuse (Typ, Innenmaße, Wandstärke) bestimmt die Kantenausschnitte für J1 (USB-C) und J22 (RJ45); der Umriss ist 110 × 97 mm.
- SW3/SW4 bestücken oder nicht (derzeit DNP, Taster sitzen an der Front); SW3 steht auf B.Cu unter J22, SW4 bleibt auf F.Cu.
- B3: Power-Kette umplatzieren (Lastpfade 1,0 mm nach `pcb-routing`) oder Kompromiss 0,5 mm + Pours.

## Nach Stufe A4 noch in der Firmware zu nutzen
LOOP_EN/WGATE_SENSE im Selbsttest (WDATA→RDATA, TRK0, WGATE), Guard-Status in BOARD_STATUS,
GUARD_OFF beim Abschalten der Laufwerksversorgung; W5500-Treiber nach A6.
