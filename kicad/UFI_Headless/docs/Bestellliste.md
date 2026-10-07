# Bestellliste UFI Headless v0.7

Alles, was für ein fertiges Gerät bestellt werden muss. Preise und Lagerbestand: JLCPCB-Teileliste, abgefragt am **07.10.2026** für **2 bestückte Platinen** – vor der Bestellung im JLC-Konfigurator gegenprüfen.
Ablauf der JLC-Bestellung selbst: [Bestellung_JLC.md](Bestellung_JLC.md).

## 1 JLCPCB: Leiterplatte + SMD-Bestückung

Dateien aus dem Release **v0.7**: `UFI_Headless_gerber.zip`, `UFI_Headless-bom-jlc.csv`, `UFI_Headless-cpl-jlc.csv`.

| Posten | Menge | ca. Kosten |
|---|---|---|
| Leiterplatte 4 Lagen, 110 × 97 mm, 1,6 mm, ENIG | 5 (Minimum) | laut Konfigurator |
| SMD-Bestückung „Standard“, eine Seite (156 Bauteile je Platine) | 2 | Setup + Schablone + Lötstellen laut Konfigurator |
| SMD-Bauteile (57 Sorten, davon 32 „Extended“) | für 2 Platinen | **≈ 155 $** |
| Rüstgebühr Extended Parts (≈ 3 $ je Sorte) | 32 Sorten | **≈ 96 $** |

Die teuersten Bauteile (für 2 Platinen):

| Teil | Ref | LCSC | Stück | Einzelpreis | Summe | Lager |
|---|---|---|---|---|---|---|
| SD-NAND 4 GB CSNP32GCR01-AOW | U15 | C2841139 | 2 | 45,64 $ | 91,27 $ | 466 |
| STM32H723ZGT6 | U5 | C730146 | 2 | 11,03 $ | 22,05 $ | 353 |
| PSRAM APS6404L-3SQR-SN | U11 | C5333729 | 2 | 3,97 $ | 7,93 $ | 4573 |
| Taster TL3342F160QG | SW1–SW4 | C2886898 | 8 | 1,04 $ | 8,30 $ | 1786 |
| LDO AP7361C-33E | U4 | C500795 | 2 | 0,81 $ | 1,62 $ | 3492 |

**Sparoption:** U15 durch **MKDV8GIL-AST** (1 GB, C26159627, ≈ 29 $) ersetzen – gleicher Footprint, spart ≈ 33 $ für 2 Platinen, Platz für ≈ 14 DD- bzw. 7 HD-Images.

Knappe Lagerbestände (bei Bestellung prüfen): STM32H723ZGT6 (353), Spule SRN6045TA-150M L1 (189), SD-NAND (466), Elko C5337554 (791).

## 2 Durchsteckteile – bei LCSC mitbestellen, selbst einlöten

In der BOM vorhanden, beim JLC-BOM-Schritt aber **abwählen** („do not place“) und von Hand löten. Bei der Leiterplatte als LCSC-Bestellung mitordern (je 2–5 Stück Reserve).

| Ref | Teil | LCSC | je Platine | ca. Preis |
|---|---|---|---|---|
| J3 | Stiftleiste 1×4, 2,54 mm (Laufwerksstrom) | C32713270 | 1 | 0,03 $ |
| J4 | Stiftleiste 2×5, **1,27 mm** (SWD) | C22438120 | 1 | 0,08 $ |
| J5 | Stiftleiste 1×3, 2,54 mm (Debug-UART) | C49257 | 1 | 0,03 $ |
| J6 | Wannenstecker 2×17, 2,54 mm (34-pol Laufwerk) | C20920 | 1 | 0,28 $ |
| J8 | Stiftleiste 1×6, 2,54 mm (IEC) | C37208 | 1 | 0,04 $ |
| J14 | Wannenstecker 2×10, 2,54 mm (Apple Disk II, v0.7) | C2977593 | 1 | 0,12 $ |

## 3 Ohne LCSC-Nummer – selbst besorgen

| Ref | Teil | je Platine | Hinweis |
|---|---|---|---|
| J2 | Hohlbuchse 5,5/2,1 mm, liegend, Printmontage | 1 | Footprint `BarrelJack_Horizontal` (Bauform CUI PJ-102A) |
| J7 | Wannenstecker 2×12, 2,54 mm | 1 | Amiga-Anschluss |
| J9 | Stiftleiste 2×5, 2,54 mm | 1 | Erweiterung, optional |
| J11 | Stiftleiste 1×2, 2,54 mm + **Jumper** | 1 | WRITE LOCK – Jumper gesteckt = Schreibschutz |
| J12 | Stiftleiste 2×4, 2,54 mm | 1 | Front-LEDs |
| J13 | Stiftleiste 1×3, 2,54 mm | 1 | Betriebsart-Schalter |
| J15 | Stiftleiste 1×2, 2,54 mm | 1 | /ENABLE zweites Apple-Laufwerk, nur bei Bedarf |

Am einfachsten: eine Leiste 2×40 und eine 1×40 (2,54 mm) zum Ablängen, dazu ein paar Jumper.

## 4 Gehäuse und Bedienelemente

| Teil | Menge | Hinweis |
|---|---|---|
| Kippschalter **EIN-AUS-EIN** (1-polig, Mittelstellung aus) | 1 | Betriebsart; Kabel 3-adrig 1:1 auf J13 (Mittelkontakt = Pin 2) |
| Front-LEDs 1× grün, 3× gelb | 4 | vorhanden; 4 × 2-adriges Kabel mit Dupont-Buchse 2,54 mm auf J12 |
| Dupont-Buchsengehäuse 1×2 / 1×3 + Crimpkontakte | Satz | für J12, J13, J3 |
| Gehäuse | 1 | Platine 110 × 97 mm, 6 Löcher M3 (4 Ecken + je eines bei y = 71 mm links/rechts) |
| Abstandsbolzen M3 + Schrauben | 6 | |
| Taster für die Front (optional) | 2 | parallel zu SW3/SW4 über J9 Pin 7/8 (Taster gegen GND, Pin 9/10) |

## 5 Kabel und Stromversorgung

| Teil | Menge | Hinweis |
|---|---|---|
| Netzteil **12 V, ≥ 2 A**, Hohlstecker 5,5/2,1 mm | 1 | für 5,25"-Laufwerke nötig (12 V); ein 3,5"-Laufwerk geht auch nur an USB-C, wenn der Port ≥ 1,5 A liefert (`ufi`-Befehl `USB_POWER` zeigt es) |
| USB-C-Kabel (Daten) | 1 | |
| 34-pol Flachbandkabel **mit Twist** | 1 | PC-Laufwerke A/B |
| 34-pol Flachbandkabel **gerade** (mehrere Stecker) | 1 | nur für den Shugart-Bus mit bis zu 4 Laufwerken |
| Laufwerks-Stromkabel J3 → Laufwerk | 1 je Laufwerk | J3: Pin 1 = +5 V, 2/3 = GND, 4 = +12 V – **gleiche Reihenfolge wie der 3,5"-Berg-Stecker**, der große 5,25"-Molex-Stecker hat sie **umgekehrt** (Pin 1 = +12 V): Kabel entsprechend kreuzen! |
| Y-Stromkabel | bei Bedarf | für 2–4 Laufwerke an J3 |
| Amiga-Adapterkabel 24-pol → DB23 | 1 | nur für Amiga-Laufwerke, Aufbau: `docs/Amiga_DB23_Adapter_Cable.md` (Repo-Hauptordner) |
| DIN-Buchse 6-pol (IEC) + Kabel 6-adrig auf J8 | 1 | nur für 1541/1571; J8 Pin n = DIN-Pin n |
| Disk-II-Kabel 20-pol | 1 | das Originalkabel des Disk-II-Laufwerks passt direkt auf J14 (rote Ader = Pin 1) |
| Adapterkabel 20-pol → DB19-Buchse | bei Bedarf | für „Apple 5.25 Drive“/UniDisk 5.25 (DB19); Belegung README „Apple-Disk-II-Port“; DB19 Pin 9 (/DRIVE2) von J15 für ein zweites Laufwerk in Kette |

## 6 Werkzeug für die Inbetriebnahme

| Teil | Hinweis |
|---|---|
| Labornetzteil mit Strombegrenzung | erste Einschaltung 150 mA begrenzt |
| Multimeter, Oszilloskop ≥ 50 MHz | Messpunkte TP1–TP8 |
| ST-Link V2/V3 **mit 10-pol 1,27-mm-Kabel** (optional) | nur für SWD; Flashen geht auch per USB-DFU (Taster A beim Einschalten) |
| Schrott-Laufwerk + Schrott-Disketten | für alle Schreibtests |

Ablauf: [Inbetriebnahme.md](Inbetriebnahme.md).
