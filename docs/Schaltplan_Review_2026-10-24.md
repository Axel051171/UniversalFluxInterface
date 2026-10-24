# Schaltplan-Review UFI Headless v1.0 nach Stufe A4 (24.10.2026)

Nach Skill `kicad-schaltplan-review`, Stand Commit 9e0313e auf `hw/v0.7-frontpanel`.
Datenbasis: `kicad-cli sch erc` (0 Verstöße, Bericht `erc.rpt`), `kicad-cli sch export netlist`
(XML), `kicad-cli sch export bom` mit den Feldern LCSC und DNP, Firmware-Pintabelle
`firmware/src/board_headless.c`, Datenblätter TI TS5A3166 (SCDS186) und die Herstellerangaben
zur PESD3V3L1BA. Nichts wurde geändert; Fixes sind Vorschläge für die nächsten Stufen.

## Ergebnis

| Schwere | Anzahl |
|---|---|
| Fehler | 0 |
| Risiko | 3 |
| Hinweis | 6 |
| offen (nicht prüfbar) | 2 |

## Risiken

1. **Front-LED-Strom hängt von der LED-Farbe ab (R34–R37, R100 = 330 Ω an 3,3 V).**
   Rot/gelb (Vf ≈ 1,8–2,1 V) ergibt 3,6–4,5 mA, ausreichend. Grün-InGaN, blau oder weiß
   (Vf ≈ 2,9–3,2 V) ergibt unter 1 mA, die LED bleibt dunkel. Fix: Frontplatten-LEDs mit
   Vf ≤ 2,1 V wählen oder R34–R37/R100 auf 150 Ω setzen (dann 8 mA bei Rot, 2,7 mA bei Blau);
   PG0 trägt zusätzlich D4 über R11 1 kΩ (1,5 mA), zusammen unter der 20-mA-Pin-Grenze.
2. **PESD3V3L1BA ist ein Gattungsname.** Nexperia (Original) gibt IRM < 0,09 µA an, die
   Nachbauten (ElecSuper, Dowo, UMW, Goodwork) 0,5–1 µA bei 3,3 V und 46–100 pF. Gegen die
   internen Pull-ups (≈ 40 kΩ, ≈ 80 µA) und die 40-kΩ-Pull-downs der Modusleitungen reicht
   auch 1 µA, die Kapazität stört auf LED-/Tasterleitungen nicht. Fix: in der BOM den Hersteller
   festlegen (Nexperia PESD3V3L1BA bzw. PESD3V3L1BAF in SOD-323F) und die LCSC-Nummer eintragen.
3. **Indexsimulation liegt fest auf dem INDEX-Bus (U7F).** Elektrisch unkritisch (Open-Collector,
   Wired-OR wie J19), aber ein `index-sim`-Aufruf mit Bit 0 bei gewähltem Laufwerk mischt
   Simulations- und Laufwerksimpulse. Fix in der Firmware (nicht im Schaltplan): Bit 0 nur ohne
   gewähltes Laufwerk oder nur im Selbsttest zulassen, im Protokoll dokumentieren.

## Hinweise

1. **Firmware-Pinmap gegen Netzliste:** alle 55 Einträge der Pintabelle treffen das Netz am
   jeweiligen U5-Pin; keine Doppelbelegung; neue Pins PE5, PG7, PG8, PG13, PG14, PG15 wie
   vorgesehen. Namensabweichungen sind nur Schreibweisen (`PIN_FDD_TRACK0` ↔ `FDD_TRK0`,
   `PIN_FDD_DKCHG` ↔ `FDD_DSKCHG`).
2. **Verwaiste Netze:** keine Netze mit nur einem Pin; I²C liegt auf TP10/TP11, J9/J13/J18/J11
   hinterlassen nichts. U9F (IEC-LS07) bleibt als einziger freier Treiber (Eingang an +5V).
3. **J21 LA-Header** führt MCU-Pins ohne Serienwiderstand nach außen. Für den Bench-Betrieb bei
   offenem Deckel gewollt; im Gehäuse nicht erreichbar.
4. **Zwei Treiber je Leitung:** INDEX (Laufwerk, U7F, J19) alle Open-Collector; TRK0 (Laufwerk,
   Q5) Open-Collector; WDATA↔RDATA nur über U22 (im Reset offen, R106). Regel eingehalten.
5. **5-V-Verträglichkeit:** die 5-V-Busleitungen erreichen den MCU nur über HC14 an 3,3 V
   (U8, U10) bzw. LVC2G17 (U17). PE5 treibt einen LS07-TTL-Eingang (IIL ≤ 0,4 mA). U22-Steuereingang
   verträgt 5,5 V, V+ = +5V, Signalbereich 0–V+ passt zu den 1-kΩ-Pull-ups auf +5V. D9–D11 schützen
   FD_RDATA/FD_WDATA/FD_TRK0/FD_INDEX vor U22 und Q5.
6. **Versorgung der neuen Teile:** U22 mit C58 100 n, U19 mit C57 1 µ; R106 100 k hält LOOP_EN
   im Reset auf GND, GUARD_OFF hat seinen 100-k-Pull-down auf der Guard-Platine.

## Offen (nicht prüfbar in dieser Sitzung)

1. **Lagerbestand/Basic-Extended beim Fertiger:** die lokale JLCPCB-Datenbank ist nicht geladen;
   21 BOM-Zeilen ohne LCSC-Nummer (C58, D18–D26, J2, J7, J12, J15, J16, J20, J21, JP4, Q5, R74/R107/R108,
   R75–R99/R105, R101–R104, R106, TP9–TP13, U22). Teil der Stufe C.
2. **Nexperia-Datenblatt PESD3V3L1BA** war nicht direkt abrufbar (Server liefert HTML); der
   IRM-Wert stammt aus der Herstellerbeschreibung der Serie, die Nachbauwerte aus deren Datenblättern.

## Geprüft und ohne Befund

ESD auf allen Frontleitungen (D18–D26) einschließlich LEDs; Serienwiderstände R100–R104 zwischen J12
und MCU; FRONT_3V3 über R105; J12 als Wannenstecker; ein Vorwiderstand je LED; SWD J4 und UART J5
erhalten; BOOT/RESET nicht an der Front; INDEX_SIM auf TIM15_CH1; 1-kΩ-Abschlüsse auf +5V je Leitung;
Verpolschutz, Sicherungen und TVS am Eingang unverändert; DNP nur SW3/SW4, Footprints bleiben.
