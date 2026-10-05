# UFI Headless – STM32H723 Flux Engine (ohne CM5)

Status: **Schaltplan + Layout v0.1** – ERC 0 Verstöße, alle 89 Netze per Netlist-Export geprüft; PCB-DRC 0 Fehler (Details unten).

| Sheet | Inhalt | Status |
|---|---|---|
| Power | USB-C (5V + Daten), 12V-Hohlstecker, TPS54202 Buck 12→5V, TPS2116 Power-Mux, AP7361C 3V3-LDO, FDD-Power-Ausgang | ✅ |
| MCU_Core | STM32H723ZGT6 (LQFP144), Entkopplung, VCAP, VDDA-Filter, HSE 25 MHz, Reset/Boot, SWD, Debug-UART, LEDs | ✅ |
| Flux_Interface | 2× SN74LS07 Open-Collector (40 mA) für 10 Ausgänge, 74LVC14A Schmitt-Inverter für 6 Eingänge, 1k Pull-ups auf 5V | ✅ |
| FDD_Connectors | 34-pol IBM-PC/Shugart, Amiga 2×12-Header (Pin n = DB23 Pin n) | ✅ (Amiga-Pinout prüfen, s.u.) |
| IEC_Bus | SN74LS07 + 74LVC14A, 1k Pull-ups, 1×6-Stiftleiste J8 (Pin n = DIN-6 Pin n: 1 SRQ, 2 GND, 3 ATN, 4 CLK, 5 DATA, 6 RESET), DIN-6-Buchse extern per Kabel | ✅ |

## Signalpolarität (wichtig für Firmware)

- **Ausgänge** (FDD_*, IEC_*_OUT): MCU **low** = Busleitung aktiv (low). MCU high bzw. Reset/High-Z = losgelassen (TTL-Eingang des LS07 floatet high).
- **Eingänge** (FDD_INDEX/TRK0/WPROT/RDATA/DSKCHG/READY, IEC_*_IN): durch den 74LVC14A **invertiert** – MCU **high** = Busleitung aktiv (low). RDATA/INDEX-Capture daher auf **steigende** Flanke.

## 34-pol Stecker (J6)

Ungerade Pins GND. 2 DENSITY · 8 INDEX · 10 MOTOR_A · 12 DRVSEL_B · 14 DRVSEL_A · 16 MOTOR_B · 18 DIR · 20 STEP · 22 WDATA · 24 WGATE · 26 TRK0 · 28 WPROT · 30 RDATA · 32 SIDE · 34 DSKCHG/READY · 4, 6 frei.
Shugart-Laufwerke: Pin 10/12/14 = DS0/DS1/DS2, Pin 16 = MOTOR ON – die Firmware wählt den Bustyp (wie Greaseweazle).

## Amiga-Header (J7) – gegen Amiga HRM verifiziert

Belegung nach Amiga Hardware Reference Manual, Header-Pin n = DB23-Pin n:
1 /RDY · 2 /DKRD · 3–7 GND · 8 /MTRXD · 9 /SEL2B · 10 /DRESB · 11 /CHNG · 12 +5V · 13 /SIDEB · 14 /WPRO · 15 /TK0 · 16 /DKWEB · 17 /DKWDB · 18 /STEPB · 19 /DIRB · 20 /SEL3B · 21 /SEL1B · 22 /INDEX · 23 +12V · 24 GND.
SEL1B teilt sich DRVSEL_B, MTRXD teilt sich MOTOR_B mit dem 34-pol Bus → nur ein Laufwerk gleichzeitig betreiben. SEL2B/SEL3B/DRESB per 1k auf +5V inaktiv.

Belegung bestätigt gegen Amiga Hardware Reference Manual, Appendix E „External Disk Interface Specification“ (alle 23 Pins). `docs/Amiga_DB23_Adapter_Cable.md` wurde entsprechend korrigiert (vorher GND auf DB23 12/15/20/23 und falsches Steckergeschlecht).

## Stromaufnahme +5V (Abschätzung)

3× LS07 ≈ 0,12 A · Pull-ups worst case ≈ 0,1 A · MCU über LDO ≈ 0,35 A · 3,5"-Laufwerk ≈ 0,5–1 A Spitze → ≈ 1,5 A. TPS2116 (2,5 A) und Buck (2 A) reichen; an USB-only über dem 500-mA-Default.

## Erzeugen / Prüfen

Die Schaltpläne werden per Skript generiert – **nicht** von Hand in Eeschema bearbeiten, solange das Design noch im Fluss ist (Änderungen gehen beim nächsten Lauf verloren).

```bash
PY="C:/Program Files/KiCad/10.0/bin/python.exe"
CLI="C:/Program Files/KiCad/10.0/bin/kicad-cli.exe"
cd kicad/UFI_Headless
"$PY" scripts/build_schematic.py                       # schreibt *.kicad_sch + netlist_summary.txt
"$CLI" sch erc --severity-all -o erc.rpt UFI_Headless.kicad_sch
"$CLI" sch export netlist -o headless.net UFI_Headless.kicad_sch
"$PY" scripts/verify_netlist.py headless.net           # Soll/Ist-Vergleich aller Netze
```

## Layout (v0.1, automatisch erzeugt)

![top](docs/top.png)

- 110 × 85 mm, 4 Lagen, 100 Bauteile, 4× M3
- DRC: **0 Fehler, 0 unverbundene Elemente, Schaltplan-Parität ok**; 4 Silkscreen-Warnungen (IDC-Kontur am Rand – Fertiger clippt)
- Fertigungsdaten in `fertigung/`: `UFI_Headless_gerber.zip` (Gerber + Excellon), `UFI_Headless-bom.csv`, `UFI_Headless-pos.csv` (KiCad) und `UFI_Headless-cpl-jlc.csv` (JLC-Spaltennamen)

### Layout-Review (gemessen mit `scripts/layout_metrics.py`)

| Bereich | Ergebnis |
|---|---|
| Quarz Y1 | HSE_IN/HSE_OUT je 9,0 mm inkl. Lastkondensatoren, **keine Vias**, symmetrisch (vorgeroutet) |
| Buck TPS54202 | Eingangs-C 100 nF 1,8 mm an VIN, SW-Knoten 2,8 mm gerade zu L1 (0,6 mm), FB-Teiler direkt an Pin 4 (vorgeroutet) |
| Versorgung | Supply-Pours entlang der Routen (`power_pours.py`): +5V/+12V/VBUS ~0,85–1,8 mm effektiv, VBUS_F über In2 ~1,5 mm |
| MCU | 100 nF ~2,8 mm vor jedem VDD-Pin, alle VSS-Pins per Stich + Via direkt auf die GND-Lage |
| USB | D+/D− 43/44 mm (USB FS: Länge/Impedanz unkritisch), ESD U1 zwischen Buchse und MCU |

Handkorrekturen nach dem Autorouting (einmalig auf diesem Board, nicht Teil von `make_pcb.sh`): IEC_RESET-Sackgasse an RN8 ersetzt, PWR_SRC auf B.Cu nach y = 48 mm verlegt, GND-Vias an C33 (VDDA) und am Buck-GND (U2.1/C3.2); nach dem Tausch J8 DIN-6 → Stiftleiste (`scripts/swap_footprint.py` + inkrementelles Freerouting) IEC_SRQ U9↔U10 auf In2 bei x = 87,07 mm. Ein neuer Pipeline-Lauf routet anders und kann andere Nacharbeit brauchen – **dieses Board ist der geprüfte Stand**.

### Vor einer Bestellung noch offen

1. **Bestelloptionen**: VBUS-Vias liegen im USB-C-Pad → „Via Covering: Epoxy filled & capped“ (alternativ plugged); kleine Vias 0,48/0,25 mm – Aufpreis für < 0,3 mm Bohrung möglich
2. **CPL-Rotationen** im JLC-Bestückungsviewer kontrollieren, besonders ICs (U2 SOT-23-6 um 180° gedreht, U3–U10), Y1, D1–D8, Elko-Polarität C1/C13, J1
3. **Handbestückung**: J2 (Hohlbuchse 5,5/2,1) und J7 (2×12 Wannenstecker) haben keine LCSC-Nummer
4. **IEC-Kabel**: J8 (1×6, 2,54 mm) auf DIN-6-Buchse, 1:1 nach Pinnummer (Lötseite der Buchse beachten)
5. Netzklassen (Power 0,8 mm nachträglich verbreitert) stehen nur im Board, nicht in `.kicad_pro`

## Layout-Pipeline (`scripts/make_pcb.sh`)

```bash
TOOLS=<dir mit jdk-25*-jre + freerouting-2.4.1.jar> bash scripts/make_pcb.sh all   # place | route | all
```

1. `build_pcb.py` – Footprints aus der Schaltplan-Netlist (mit KIID-Pfaden → Schaltplan-Parität), Platzierung, 110×85 mm Outline, M3-Löcher, In1-GND-Plane, vorgeroutete USB-C-Auffächerung (gesperrt)
2. Freerouting 2.4.1 (Java 25) – F.Cu / In2.Cu / B.Cu als Signallagen, In1.Cu GND-Plane
3. `drop_violations.py` – Freerouting-Leiterbahnen mit DRC-Verstoß löschen und neu routen (bis sauber)
4. `widen_power.py` – Versorgungsnetze auf 0,8 mm (GND/3V3 0,5 mm) verbreitern, wo DRC es erlaubt
5. `finish_pcb.py` – GND-Pours F/B, +3V3-Pour In2, GND-Stitching-Vias, Inseln anbinden, Zonen füllen

Lagenaufbau: F.Cu Signal + GND-Pour · In1.Cu GND-Plane · In2.Cu Signal + 3V3-Pour · B.Cu Signal + GND-Pour.
Hinweis Fertigung: VBUS-Vias liegen im USB-C-Pad (Via-in-Pad) → bei JLC „Via Covering: Plugged/Epoxy filled“ wählen oder vor dem Bestellen manuell versetzen. D−-Vias an J1 sind 0,48/0,25 mm.

## Stromversorgung

```
USB-C VBUS ──Polyfuse 1.5A──► VBUS_F ──────────────┐ VIN2 (Fallback)
12V Jack ──SS54──► +12V ──TPS54202──► +5V_DRV ─────┤ VIN1 (Priorität, Schwelle ≈4.3V)
                     │                             TPS2116 ──► +5V ──► AP7361C ──► +3V3
                     └──────────► FDD_PWR Pin 4    (2.5A)      └──► FDD_PWR Pin 1
```

- Nur USB: Logik + 3,5"-Laufwerk aus USB-C. Ohne CC-Auswertung ist offiziell nur der USB-Default-Strom (500 mA) zugesichert; 3,5"-Laufwerke laufen damit in der Praxis meist (wie bei Greaseweazle), für Spec-Konformität später CC-Spannung per ADC auswerten.
- 12V gesteckt: Laufwerks-5V aus dem Buck, 12V für 5,25"-Laufwerke.
- `PWR_SRC` (TPS2116 ST, open-drain, Pull-up auf 3V3) → PG4, Firmware kann die aktive Quelle lesen.

## GPIO-Belegung (Referenz für Firmware)

| Signal | Pin | Funktion |
|---|---|---|
| FDD_RDATA | PA5 | TIM2_CH1 (AF1) Input Capture, 32 bit |
| FDD_INDEX | PA1 | TIM2_CH2 (AF1) Input Capture, gleiche Zeitbasis wie RDATA |
| FDD_WDATA | PA6 | TIM3_CH1 (AF2) Output |
| FDD_MOTOR_A / _B | PE7 / PE8 | GPIO out |
| FDD_DRVSEL_A / _B | PE9 / PE10 | GPIO out |
| FDD_STEP / DIR / SIDE | PE11 / PE12 / PE13 | GPIO out |
| FDD_WGATE / DENSITY | PE14 / PE15 | GPIO out |
| FDD_TRK0 / WPROT / DSKCHG / READY | PF0 / PF1 / PF2 / PF3 | GPIO in |
| IEC_{ATN,CLK,DATA,SRQ,RESET}_OUT | PD0–PD4 | GPIO out → OC-Treiber |
| IEC_{ATN,CLK,DATA,SRQ,RESET}_IN | PF4–PF8 | GPIO in |
| USB D− / D+ | PA11 / PA12 | OTG_HS mit internem FS-PHY |
| VBUS_SENSE | PA9 | Teiler 22k/33k (5V → 3,0V) |
| SWDIO / SWCLK / SWO | PA13 / PA14 / PB3 | Debug |
| DBG_TX / DBG_RX | PD8 / PD9 | USART3 (AF7) |
| LED_ACT / FDD / USB / ERR | PG0–PG3 | High = an |
| PWR_SRC | PG4 | TPS2116 Status |
| HSE | PH0 / PH1 | 25 MHz |

### Nötige Firmware-Anpassungen (`firmware/src/ufi_main.c`)

Die alte Pintabelle ist mit dem H723 nicht umsetzbar (RDATA auf PC3 hat kein TIM2_CH1, PA0 war doppelt geplant). Zu ändern:

- `PIN_FDD_RDATA` PC3 → **PA5** (AF1 TIM2_CH1), `PIN_FDD_INDEX` PC0 → **PA1** (TIM2_CH2)
- `PIN_FDD_WDATA` PB4 → **PA6** (AF2 TIM3_CH1)
- FDD-Ausgänge von GPIOA/GPIOB → **GPIOE 7–15**, FDD-Eingänge GPIOC → **GPIOF 0–3**
- IEC: getrennte OUT (PD0–4) / IN (PF4–8) statt bidirektionaler Pins
- LEDs GPIOE 0–3 → **GPIOG 0–3**; Power-LED ist fest an 3V3
- Linker-Skript `STM32H723ZGTX_FLASH.ld` passt bereits (LQFP144)

## Sourcing-Notizen

- LCSC-Nummern: per JLCPCB-API (05.10.2026, Lagerbestand geprüft) zugeordnet, Tabelle `LCSC` in `scripts/build_schematic.py`. Extended-Teile kosten bei JLC je ~3 $ Rüstgebühr; Spule SRN6045TA-150M (C1330797) hatte nur 189 Stück Lager
- Quarz: YXC X322525MOB4SI (C9006, Basic), 25 MHz, CL 12 pF → Lastkondensatoren 2× 18 pF C0G (C1647); gm_crit ≈ 0,97 mA/V < 1,5 mA/V (H723 HSE)
- IEC: Stiftleiste 1×6 (C37208) statt DIN-6 auf der Platine; DIN-6-Buchse sitzt extern (Gehäuse), Firmware unverändert (gleiche MCU-Pins)
- Firmware ist noch nicht an Pintabelle + Polarität angepasst (siehe oben)
