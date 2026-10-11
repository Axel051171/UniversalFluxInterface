# C64 PSU Guard – geschützte 5 V / 12 V für ein 1541-II / 1581 aus einem PC-Netzteil

Status: **Schaltplan + Layout v0.1**, ERC 0, DRC 0 (2 Lagen, 58 × 44 mm, JLCPCB-Bestückung), **noch nicht gebaut**.

Eigenständige Platine, unabhängig vom UFI-Board. Sie sitzt zwischen dem internen PC-Netzteil des Geräts und dem 4-poligen DIN-Stromanschluss des Commodore-Laufwerks (Original-Netzteil DV-5128UP: 5 V 700 mA, 12 V 500 mA) und ersetzt dessen Schutzfunktion. Ein PC-Netzteil liefert auf beiden Schienen viele Ampere, ohne Begrenzung würde ein Kurzschluss im Laufwerk das dünne DIN-Kabel und die Laufwerksplatine beschädigen.

![Oberseite](docs/top.png)

## Was die Platine erkennt und tut

Zwei gleiche Kanäle (5 V und 12 V), je:

| Block | Teile | Verhalten |
|---|---|---|
| Verpolschutz | AO3401A als ideale Diode (Q1/Q3), Gate über 100k an GND, BZT52C10 klemmt Vgs | negative Eingangsspannung: Kanal bleibt stromlos, LED **REV POL** (D9) leuchtet (über 1N4148W D10/D11 von der negativen Schiene gespeist) |
| Eingangsschutz | SMF5.0CA (5 V) / SMAJ15CA (12 V), 10 µF + 100 nF | Spannungsspitzen vom Netzteil |
| eFuse | TPS25921ADR (U1/U2), SOIC-8 | Strombegrenzung, Schnellabschaltung in 3 µs, Über-/Unterspannung über Teiler, Sanftanlauf 22 nF, thermischer Schutz, **Auto-Retry** |
| Anzeige | grün am Ausgang (**5V OK** D3, **12V OK** D7), rot am FLT-Pin (**5V ERR** D4, **12V ERR** D8) | rot = Kanal abgeschaltet; erlischt, sobald der Fehler weg ist (Kurzschluss: blinkt im Takt der thermischen Abschaltung, Retry nach 150 ms) |
| Reserve | J3 für das UFI-Board, 2N7002 (Q2/Q4) ziehen ENUV nach GND | Pin 2 OFF high = beide Kanäle aus; FLT-Ausgänge open drain (Pull-up auf der UFI-Seite) |

Dimensionierung (TPS25921A: I_LIMIT ≈ 0,0105 A/kΩ, Schnellabschaltung 0,0142 A/kΩ + 0,36 A, Schwellen ENUV/OVP 1,39 V steigend / 1,34 V fallend):

| | 5-V-Kanal | 12-V-Kanal |
|---|---|---|
| Strombegrenzung (R_ILIM) | 1,21 A (R6 115k), Schnellabschaltung 2,0 A | 1,50 A (R14 143k), Schnellabschaltung 2,4 A |
| Überspannung aus / wieder ein | 6,0 V / 5,8 V (R4 100k, R5 30k) | 14,0 V / 13,5 V (R12 100k, R13 11k) |
| Unterspannung ein / aus | 4,18 V / 4,03 V (R2 100k, R3 49,9k) | 10,4 V / 10,0 V (R10 100k, R11 15,4k) |
| Anlaufzeit (C_SS 22 nF) | ≈ 23 ms | ≈ 54 ms |
| LED-Vorwiderstände | 1k (≈ 3 mA) | 3,3k (≈ 3 mA) |

Die Anlaufzeit hält den Einschaltstrom in die Elkos des Laufwerks unter der Begrenzung (2200 µF an 5 V ergeben ≈ 0,5 A). Der Motoranlauf des 1541-II liegt unter 1 A und wird nicht begrenzt.

**Was die Platine nicht sehen kann:** einen falsch belegten DIN-Stecker am Laufwerksende. Die Kabelbelegung einmal mit dem Multimeter prüfen (unten).

## Anschlüsse

| Stecker | Belegung |
|---|---|
| J1 PSU IN (Stiftleiste 1×4, 2,54 mm) | 1 +5 V (rot), 2 und 3 GND (schwarz), 4 +12 V (gelb) = Belegung des kleinen Floppy-Stromsteckers (Berg) eines PC-Netzteils; ein Molex-auf-Berg-Adapter passt direkt. Pin 1 = eckiges Pad, oben |
| J2 DRIVE (Schraubklemme 3-polig, 5 mm) | 1 +5 V → DIN Pin 1, 2 GND → DIN Pin 2, 3 +12 V → DIN Pin 4; DIN Pin 3 bleibt frei |
| J3 UFI (Stiftleiste 1×4) | 1 GND, 2 OFF, 3 FLT5, 4 FLT12; unbeschaltet lassen = normaler Betrieb (100k Pull-down an OFF) |
| H1–H4 | M3, mit GND verbunden |

DIN-Stecker am Laufwerkskabel (Blick auf die Stiftseite des Steckers, Nase unten, wie auf dem Netzteil-Label): oben links 3 (frei), oben rechts 2 GND, unten links 4 +12 V, unten rechts 1 +5 V. Vor dem ersten Einschalten mit dem Multimeter zwischen Schraubklemme und Steckerstiften durchklingeln.

## Inbetriebnahme

1. Platine ohne Laufwerk ans Netzteil: beide grünen LEDs an, keine rote. Ausgänge messen: 5,0 V und 12,0 V.
2. J1 um 180° verdreht aufstecken (5 V und 12 V vertauscht): 5-V-Kanal meldet Überspannung (**5V ERR**), 12-V-Kanal Unterspannung (**12V ERR**), Ausgänge bleiben aus. Richtig herum stecken, beide LEDs erlöschen.
3. Ausgang 5 V kurz mit einem 2,2-Ω-Widerstand belasten (≈ 2,3 A): **5V ERR** blinkt, grüne LED aus; Widerstand weg, Kanal kommt von selbst zurück.
4. Laufwerk anschließen, Diskette lesen, dabei die 12 V am Ausgang beobachten (Motoranlauf darf die rote LED nicht auslösen).

## Fertigung (JLCPCB)

`fertigung/`: Gerber-ZIP, `C64_PSU_Guard-bom-jlc.csv`, `C64_PSU_Guard-cpl-jlc.csv`, `JLC_Rotation_Check.md` (Pin-1-Kontrolle im Bestückungsviewer). 2 Lagen, 1,6 mm, Leiterbahnen ≥ 0,25 mm, Versorgung 1,0 mm. Alle LCSC-Nummern stehen im Schaltplan (`LCSC`-Feld) und sind am 2026-10-11 auf Lager geprüft; Bauteile ≈ 3,10 USD je Platine bei 5 Stück, 12 Extended-Positionen (Einrichtgebühr), TPS25921ADR C189199 nur rund 1000 Stück am Lager.

## Werkzeugweg

Wie beim UFI-Board: Schaltplan und Layout entstehen aus Skripten, die Dateien in KiCad sind das Ergebnis.

```bash
# Schaltplan -> ERC -> Netzliste -> Platzierung -> Freerouting -> GND-Flächen -> DRC
TOOLS=/pfad/zu/freerouting-2.4.1.jar+jdk-25-jre bash scripts/make_pcb.sh
# Gerber, BOM, CPL, Vorschaubilder
bash scripts/make_fab.sh
```

`scripts/build_schematic.py` (Bauteile, Netze, LCSC), `scripts/build_pcb.py` (Platzierung, Umriss, Bestückungsdruck), `scripts/finish_pcb.py` (GND-Flächen beidseitig, Via neben jedem SMD-GND-Pad). Freerouting 2.4.1 braucht Java 25. Projektbibliothek `lib/C64_PSU_Guard.kicad_sym` enthält das TPS25921-Symbol.
