# Empfehlungen für die nächste Platinenrevision (nach dem Bring-up vom 24.10.2026)

Grundlage: Foto der bestückten v0.7 vom 22.10., Bauteilliste aus `UFI_Headless.kicad_pcb`,
die Firmware-Qualifikationsdoku und das, was die Firmware in 1.18 inzwischen an Messbarkeit
voraussetzt. Jede Empfehlung nennt den Anlass. Was ich aus den Projektdateien nicht
bestätigen konnte, steht als "prüfen".

## 1 Befunde dieser Bestückung und was sie verhindert hätte

| Befund | Ursache | Abhilfe in der nächsten Revision |
|---|---|---|
| J1 USB-C um 180° gedreht | HRO-TYPE-C-31-M-12 hat vier symmetrische Gehäusepins, das Gehäuse passt in beide Richtungen; auf der Bestückungszeichnung ist die Öffnung nicht erkennbar | Buchse in eine **Aussparung der Platinenkante** setzen (Ausschnitt im Umriss, Öffnung ragt in die Aussparung): gedreht kollidiert das Gehäuse mit der Platine. Zusätzlich auf F.Fab den Steckerumriss mit Öffnungsseite zeichnen und auf dem Bestückungsdruck einen Pfeil "USB" an der Kante. Rework ist bei dieser Bauform schwer, deshalb mechanisch ausschließen. |
| RN1–RN8 (1 kΩ-Arrays) nicht bestückt | Arrays (MNR14/1206-4) sind beim Bestücker oft nicht lagernd oder als "optional" gelesen | Entweder vier Einzelwiderstände 0603 je Gruppe (Standardteil, immer lagernd) oder ein Array aus dem Basisbestand des Bestückers. In der BOM als **required** markieren; die Spalte "DNP" nur für wirklich optionale Teile. |
| C46/C50 nicht bestückt | wie oben, kein DNP-Vermerk, aber ausgelassen | Vor Bestellung die Pick-and-Place-Liste gegen die BOM abgleichen (Skript: jede Referenz mit Wert muss in der Platzierungsliste vorkommen, außer DNP). |
| J11 WRITE-LOCK ohne Stifte | zweipolige Stiftleiste als "optional" behandelt | Schiebeschalter (z. B. SMD-Slide SPDT) statt Jumper, mit Druck "WRITE LOCK / ENABLE". Ein Schalter wird bestückt, ein Jumper vergessen. |
| J13 Modusschalter, J12, J9, J16, J7, J2 unbestückt | Durchsteck-Steckverbinder in einer SMD-Bestückung | Entscheiden, was die Platine wirklich braucht (siehe 3). Alles, was bleibt, als SMD-Stiftleiste oder als Teil der Standardbestückung; Rest aus dem Layout entfernen statt als leere Pads mitzuführen. |

## 2 Testbarkeit, die die Firmware heute schon nutzen kann

Die Firmware hat Selbsttests und Qualifikationsschritte, die an Hardware-Rückschleifen
hängen. Sie sollten auf der Platine steckbar sein, nicht als Fädeldraht:

1. **Loopback-Block (2 × n Stiftleiste, 2,54 mm) für die Flux-Schleife:** WDATA (hinter dem
   LS07, mit Pull-up) → RDATA (vor dem LVC14), WGATE → eine Statusleitung, INDEX_SIM (PE1)
   → INDEX. Mit drei Jumpern lässt sich der gesamte Schreib-/Lesepfad ohne Laufwerk
   messen: genau das, was `Abnahme_Schreibpfad.md` Abschnitt 1 und
   `Firmware_Hardwarequalifikation_2026-10-22.md` "Endliche gepufferte Erfassung" verlangen.
   Selbsttest `index_loop` nutzt heute J9 Pin 6; der Block macht den Draht überflüssig.
2. **Logikanalysator-Header** mit allen 34-Pin-Signalen auf 3,3-V-Pegel (hinter den LVC14
   bzw. vor den LS07) plus GND alle zwei Pins. Die Abnahme misst WDATA/WGATE/INDEX/RDATA
   direkt; heute müssen Tastköpfe an den Steckerpins hängen.
3. **Testpunkte als Lötpads mit Beschriftung:** 3,3 V, 5 V, 12 V, FDD_5V, FDD_12V, VBUS,
   USB D+/D−, TRK0, WPROT. TP1–TP8 sind vorhanden, TP1 (3,3 V) laut Layout vorn als 1,5-mm-Pad; prüfen, ob TP1 auf der
   Vorderseite liegt, auf dem Foto fehlt er.
4. **TRK0 für Tests ohne Laufwerk:** die Qualifikationsdoku verlangt TRK0 vom Adapter. Ein
   Jumper "TRK0 force" (TRK0 auf aktiv) erspart den Adapter für Loopback-Läufe.
5. **Strommessung bleibt** (INA180 auf 5 V und 12 V, Provenienzblock nutzt sie), zusätzlich
   eine LED je Laufwerksrail: sichtbar, ob `ufi power` die Rails geschaltet hat.

## 3 Versorgung und Stecker vereinfachen

- Heute drei Einspeisungen (USB-C, J17 PSU_IN 5/12 V, J2 12-V-Buchse) plus Jumper J18 für
  die 5-V-Quelle. Das ist die fehleranfälligste Stelle beim Einschalten. Vorschlag: **eine
  12-V-Einspeisung** (Hohlstecker J2 oder XT30, verpolungssicher) mit Buck auf 5 V und
  LDO auf 3,3 V, USB-C nur als Datenanschluss mit 5-V-Fallback über den vorhandenen
  Mux (TPS2116, automatische Priorität, kein Jumper). J18 entfällt. 5,25"- und
  8"-Laufwerke brauchen 12 V ohnehin.
- Verpolungs- und Überstromschutz je Laufwerksrail als eFuse (z. B. TPS2553/TPS259x)
  statt Polyfuse plus Software; die INA180-Messung bleibt für die Provenienz.
- USB-Header J16 entweder bestücken (als Reserve, hat sich gerade als nützlich erwiesen)
  oder streichen; ESD (USBLC6) und Serienwiderstände müssen für beide Wege gelten, prüfen.
- Laufwerksstecker als **geschirmte, codierte Box-Header** beibehalten: J6 (2×17), J7
  Amiga (2×12) und J14 Disk II (2×10) sind im Layout bereits IDC-Footprints. Für J8 IEC und
  J3 FDD_PWR ebenfalls codierte Gehäuse vorsehen; verdrehte Kabel sind die zweithäufigste
  Fehlerquelle nach gedrehten Steckern.

## 4 Signalpfade (bestätigt durch die Firmwarearbeit)

- Eingänge über 74LVC14 (Schmitt-Trigger, 5-V-tolerant) sind richtig; die 1-kΩ-Pull-ups
  nach 5 V gehören auf die Laufwerksseite der LVC14. Ausgänge über 74LS07 (offener
  Kollektor, 40 mA) tragen auch 8"-Abschlüsse von 150 Ω (Doku 4.4). Beibehalten.
- RDATA wird im INDEX-Interrupt scharf geschaltet (Qualifikationsdoku): die Latenz vom
  physischen Index kann erste Flanken verlieren. Hardwareseitig hilft, INDEX und RDATA auf
  Timer-Eingänge desselben Timers zu legen, damit die Aufnahme per Hardware-Trigger
  startet; prüfen, ob die H723-Pinbelegung das zulässt (TIM2 CH1 RDATA, CH2 INDEX sind
  schon auf TIM2, der Trigger-Start über TIM2 ETR/TI2 wäre dann eine Firmwareänderung ohne
  Layoutänderung).
- PSRAM APS6404L (8 MB, QSPI) ist die Durchsatzgrenze im Dauerstreaming; die Alternative
  wäre ein OCTOSPI-PSRAM (APS6408L-OB) am selben Controller. Nur umstellen, wenn die
  Durchsatzmessung aus der Qualifikation es verlangt.
- SD-NAND im LGA-8 ist für den Bestücker gut, für den Menschen unsichtbar: Lötbarkeit
  nur per Selbsttest `sd` prüfbar. Ein microSD-Sockel als Alternativ-Footprint würde das
  Tauschen und Auslesen am PC erlauben; Entscheidung nach dem ersten SD-Selbsttest.

## 5 Fertigungsunterlagen, die Bestückungsfehler abfangen

1. Bestückungszeichnung (F.Fab) mit Steckerumrissen samt Öffnungsseite, Pin-1-Marken und
   Polaritäten; sie ist das Dokument, das der Bestücker wirklich ansieht.
2. BOM mit Spalten "required/DNP", Herstellernummer, Bestücker-Teilenummer; Basisteile
   bevorzugen, damit nichts "nicht verfügbar" bleibt.
3. Prüfskript vor der Bestellung: Platzierungsliste gegen BOM, Rotationen gegen die
   Bauteilbibliothek (JLC/LCSC drehen Stecker und Dioden gern um 90/180°).
4. Nach Lieferung: `python -m ufi_host.bringup` (ohne Laufwerk) als Abnahme jeder Platine,
   Bericht mit Seriennummer ablegen (`docs/Bringup_2026-10-24.md`).

## 6 Festgelegt am 24.10.2026 (Entwurfsvorgabe für die Umsetzung in KiCad)

- **Versorgung:** wie bisher (USB-C, J17 PSU_IN, J2 12 V), aber ohne J18: die 5-V-Quelle
  wählt der TPS2116 automatisch. Laufwerksversorgung bleibt auf der Platine (J3) als
  Stiftleiste wie bisher (Owner 24.10.: Stecker intern unverändert, einmal gesteckt).
- **Gehäuseeinbau:** USB-C, 12-V-Buchse und IEC hinten, bündig mit der Platinenkante;
  Laufwerkskabel intern. Auf der Platine nur eine Status-LED (ACT); alle übrigen
  Anzeigen und die Bedienung an der Frontplatte. Umriss und sechs Bohrungen bleiben,
  bis das Gehäuse feststeht (Vorschlag Hammond 1455; Höhe bestimmt USB-C-Lage).
- **Frontplatten-Header J12 neu, 2×8 Wannenstecker mit Kodierung:** LEDs ACT, ERR, USB,
  FDD, PWR mit je eigenem Vorwiderstand auf der Platine (nur Anoden am Header, Kathoden
  an GND), Taster A, Taster B, Modusschalter (ersetzt J13) jeweils mit Serienwiderstand
  100 Ω–1 kΩ und TVS-Array nahe am Header; 3,3 V, GND und die sechs freien Pins GND.
  RESET und BOOT nur auf der Platine (BOOT an der Front wäre ein Fehlbedienungsrisiko).
  A und B bleiben als Footprint auf der Platine, unbestückt.
- **Steckverbinder:** J16 USB-Header und J7 Amiga bestücken; J9 Erweiterung entfällt
  (EXP_IO1/IO2 gehen auf den Front-Header, INDEX_SIM auf einen eigenen Timer-Pin); J4 SWD und
  J5 UART bleiben unverändert; J11 WRITE-LOCK wird die Lötbrücke JP4 (siehe Gehäuse-Vorgabe).
- **Gehäuse-Vorgabe (24.10., nach A3):** Nach dem Einbau ist nichts mehr steckbar, von außen
  bleiben nur LAN und USB erreichbar. Deshalb keine Jumper und keine Schiebeschalter für den
  Betrieb: WRITE-LOCK als Lötbrücke JP4 (einmal bei der Montage), die Selbsttest-Rückschleife
  elektronisch (unten), J15 (zweites Apple-Laufwerk) und J19 (Sync-Sensor) bleiben interne,
  bei der Montage einmal gesteckte Kabel. RESET/BOOT im Gehäuse sind unkritisch, weil Taste A
  an der Front beim Einschalten den ROM-DFU-Bootloader startet. Diesmal **komplette
  Bestückung** aller Bauteile; bewusst unbestückt bleiben nur SW3/SW4 (Taster A/B auf der
  Platine, an der Front vorhanden).
- **Selbsttest-Rückschleife ohne Jumper (A4, umgesetzt):** INDEX_SIM = PE5 (TIM15 Kanal 1,
  Hardware-Impuls) über den bisher freien Open-Collector-Treiber U7F fest auf den INDEX-Bus
  (Wired-OR wie der Sync-Sensor); LOOP_EN = PG14 (100 k Pull-down R106, im Reset offen)
  schließt den Analogschalter U22 TS5A3166 zwischen FD_WDATA und FD_RDATA (5 V, Steuereingang
  VIH 2,4 V bei 5 V laut TI-Datenblatt, 3,3-V-Pegel reicht) und zieht über Q5 2N7002 FD_TRK0
  nach GND; WGATE-Rücklesen über den freien Inverter U10F (IEC-Blatt) nach PG15.
  Logikanalysator-Header J21 2×10 bestückt (3,3-V-Seite, nur bei offenem Deckel genutzt).
- **C64 PSU Guard (kleine Spannungsplatine):** eigener Header J20 1×4 GUARD (GND, OFF = PG7,
  ERR5 = PG8, ERR12 = PG13, 10 k Pull-ups R107/R108) als Ersatz für den entfallenen J9;
  passt auf J3 der Guard-Platine (1 GND, 2 OFF, 3 ERR5, 4 ERR12). Firmware-Nutzung folgt.
- **LAN (A6, umgesetzt):** Blatt LAN mit U23 W5500 an SPI3 (PC10 SCK, PC11 MISO, PC12 MOSI,
  PA15 CS, PD5 INT, PD10 RST; Belegung gegen die ST-Pindatenbank geprüft) und J22 HanRun
  HR911105A (RJ45 mit integrierten Übertragern, Bob-Smith-Netz und LINK/ACT-LEDs; Pinbelegung
  aus dem HanRun-Datenblatt Rev. A/2: 1 TD+, 2 TD−, 3 RD+, 4 TX-Mittelanzapfung, 5 RX-Mittelanzapfung,
  6 RD−, 8 Gehäuse-GND, 9/10 grüne LED, 12/11 gelbe LED). Beschaltung nach der
  WIZnet-Referenzschaltung W5500-Ref-ExternalMag (2015) und dem W5500-Datenblatt v1.1.0:
  49,9 Ω Abschluss an TXP/TXN, 10 Ω + 22 nF an der TX-Mittelanzapfung, 6,8 nF in Reihe und
  82 Ω gegen die RX-Mittelanzapfung mit 10 nF, EXRES1 12,4 kΩ 1 %, 1V2O 10 nF, TOCAP 4,7 µF,
  VBG offen, RSVD 23 an GND, PMODE über 10 k auf 3,3 V (Auto-Negotiation), 25-MHz-Quarz wie Y1
  mit 18 pF und 1 M, 3V3A über Ferrit mit 10 µF + 5 × 100 nF. Firmware: Pinmap im selben Commit,
  der Chip bleibt bis zum Treiber im Reset. Danach bleiben 24 der 144 MCU-Pins frei.
- **FDD-Abschluss:** 32 Einzelwiderstände 0603, 1 kΩ, statt RN1–RN8; 8-Zoll-Abschluss
  (150–220 Ω) gehört in den Adapter.
- **Testbarkeit:** Loopback-Block (WDATA→RDATA, WGATE→Status, INDEX_SIM→INDEX, TRK0-Force),
  Logikanalysator-Header auf 3,3 V, Testpunkte wie in Abschnitt 2.
- **USB-C:** in Kantenaussparung, Öffnung bündig außen; F.Fab mit Öffnungsseite.
- **Amiga extern:** DB23 als Gehäusebuchse am Flachbandkabel von J7 (Owner 24.10.: keine Buchsen auf der Platine, J7, J8 und J3 bleiben Stiftleisten; DIN-6 ebenso am Kabel von J8). Offen bleibt das Gehäuse (bis dahin heutiger Umriss).
- **Eingang/Ausgang-Struktur (Vorgabe 24.10.):** eine Platinenkante trägt alles, was vom Host und von der Versorgung kommt (USB-C, J16, 12-V-Buchse, J17 PSU_IN, SWD J4, UART J5); die gegenüberliegende Kante alle Laufwerksanschlüsse als interne Stiftleisten (J6 34-pol, J14 Disk II, J7 Amiga, J8 IEC, J3 Laufwerksstrom; DIN-6 und DB23 sind Gehäusebuchsen am Kabel). Front-Header J12 an einer Seitenkante.

## 7 Nicht ändern

MCU STM32H723ZGT6 (LQFP-144, Flash 1 MB bei 228 KB belegt, Reserven ausreichend),
25-MHz-Quarz, SWD-Header J4 (Cortex-Debug 1,27 mm), Taster RESET/BOOT/A/B, Status-LEDs,
Schirm-Header J6, INA180-Messung, die Treiberwahl LS07/LVC14 für den 34-Pin-Bus.
- **LAN:** W5500 (SPI, TCP/IP in Hardware) mit RJ45-Magjack an der Eingangskante; USB bleibt Hauptweg, LAN als spätere Firmwarestufe (Entscheidung 24.10.).
