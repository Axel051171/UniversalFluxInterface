# UFI PCB Final-Check & Produktions-Vorbereitung

## Übersicht

Dieses Dokument enthält die finale Checkliste vor der PCB-Produktion.

---

## 1. Schematic Review

### 1.1 Stromversorgung

```
☐ 12V Eingang mit Verpolungsschutz (Schottky-Diode)
☐ Sicherung oder PTC vorhanden
☐ TPS54331 Buck-Regler korrekt beschaltet
  ☐ Feedback-Widerstandsteiler für 5V
  ☐ Bootstrap-Kondensator
  ☐ Soft-Start Kondensator
☐ 3.3V LDO für STM32 (LM1117-3.3)
☐ 1.8V LDO für STM32 Core (optional, internen Regler nutzen)
☐ Bulk-Kondensatoren an Eingängen (100µF+)
☐ Entkopplung an jedem IC (100nF + 10µF)
```

### 1.2 STM32H723

```
☐ HSE Quarz (25MHz) mit Lastkapazitäten
☐ LSE Quarz (32.768kHz) mit Lastkapazitäten
☐ Reset-Beschaltung (RC + Taster)
☐ Boot0 auf GND (mit Jumper-Option)
☐ VDD/VSS alle verbunden
☐ VDDA mit Ferrit/Filter
☐ VREF+ korrekt (3.3V oder separate Referenz)
☐ USB HS Pins (PA11/PA12 oder PB14/PB15)
☐ SWDIO/SWCLK für Debugging
```

### 1.3 USB

```
☐ USB-C CC-Widerstände (5.1k für Device)
☐ ESD-Schutz auf D+/D-
☐ 90Ω Differenz-Impedanz
☐ Ferrit auf VBUS
```

### 1.4 Ethernet

```
☐ Magjack mit integrierten Transformern
☐ 49.9Ω Terminierung
☐ 100Ω Differenz-Impedanz
☐ RGMII oder RMII Timing korrekt
```

### 1.5 FDD Interface

```
☐ 74LVC245 Level-Shifter (3.3V ↔ 5V)
☐ DIR-Pin korrekt (Richtungssteuerung)
☐ OE-Pin auf GND oder gesteuert
☐ Pull-ups auf Input-Signalen
☐ Serienwiderstand auf Outputs (22-33Ω)
```

### 1.6 IEC Bus

```
☐ Open-Drain Ausgänge
☐ Pull-up Widerstände (1kΩ auf 5V)
☐ Level-Shifting (optional, 1541 ist 5V tolerant)
```

---

## 2. PCB Layout Review

### 2.1 Layer-Stackup

```
┌──────────────────────────────────────────┐
│ F.Cu   - Signal + USB/ETH Differenzpaare │  35µm
├──────────────────────────────────────────┤
│ Prepreg                                  │  0.2mm
├──────────────────────────────────────────┤
│ In1.Cu - GND Plane (ununterbrochen!)     │  35µm
├──────────────────────────────────────────┤
│ Core                                     │  1.0mm
├──────────────────────────────────────────┤
│ In2.Cu - Power Plane (+5V / +3V3)        │  35µm
├──────────────────────────────────────────┤
│ Prepreg                                  │  0.2mm
├──────────────────────────────────────────┤
│ B.Cu   - Signal                          │  35µm
└──────────────────────────────────────────┘

Gesamt: ~1.6mm
```

### 2.2 Design Rules

| Parameter | Min | Typ | Einheit |
|-----------|-----|-----|---------|
| Track Width | 0.15 | 0.2 | mm |
| Track Spacing | 0.15 | 0.2 | mm |
| Via Drill | 0.3 | 0.3 | mm |
| Via Diameter | 0.6 | 0.6 | mm |
| Annular Ring | 0.15 | 0.15 | mm |
| Silk Line | 0.12 | 0.15 | mm |
| Silk Text | 0.8 | 1.0 | mm |

### 2.3 Impedanz-Kontrolle

```
☐ USB Differenzpaar: 90Ω ±10%
  - Track Width: 0.2mm
  - Gap: 0.15mm
  - Referenz: GND Plane

☐ Ethernet Differenzpaar: 100Ω ±10%
  - Track Width: 0.15mm
  - Gap: 0.18mm
  - Referenz: GND Plane

☐ Single-Ended: 50Ω
  - Track Width: 0.25mm
  - Referenz: GND Plane
```

### 2.4 Kritische Signale

```
☐ USB D+/D- 
  - Längenausgleich <2mm
  - Keine Vias im Differenzpaar
  - Keine Referenz-Unterbrechung

☐ Ethernet MDI
  - Längenausgleich <5mm
  - Symmetrisches Routing

☐ Flux Input (RDATA)
  - Kurz halten (<30mm)
  - Weg von Störquellen
  - Guard-Ring optional

☐ HSE Quarz
  - Kurze Leitungen (<10mm)
  - GND-Guard
  - Keine Signale in der Nähe
```

### 2.5 Power Integrity

```
☐ GND Plane durchgehend (keine Schlitze!)
☐ Power Plane Split sauber
☐ Decoupling nah an IC-Pins
☐ Bulk-Caps an Eingang
☐ Sternförmige Masseführung für Analog
☐ Via-Stitching an Plane-Kanten
```

### 2.6 Thermal

```
☐ Buck-Regler mit Thermal Pad
☐ Via-Array unter STM32 (Thermal Vias)
☐ Kupferflächen für Wärmeableitung
☐ Kein Heißer IC neben Temperatursensitiven
```

---

## 3. Footprint-Check

### 3.1 Kritische Footprints

| Bauteil | Footprint | Geprüft |
|---------|-----------|---------|
| STM32H723ZGT6 | LQFP-144_20x20mm_P0.5mm | ☐ |
| CM5 Connector | Hirose_DF40C-100DP | ☐ |
| USB-C | HRO_TYPE-C-31-M-12 | ☐ |
| M.2 2242 | Custom | ☐ |
| RJ45 | Tab-Down Magjack | ☐ |
| FDD 34-pin | IDC Vertical | ☐ |
| DIN-6 | Female PCB | ☐ |

### 3.2 Pin-1 Markierung

```
☐ Alle ICs mit Pin-1 Marker
☐ Polarisierte Bauteile markiert
☐ Silkscreen lesbar (nicht unter Bauteil)
```

### 3.3 Courtyard

```
☐ Keine Courtyard-Überlappung
☐ Mindestabstand für Bestückung
☐ Platz für Nacharbeit
```

---

## 4. DRC (Design Rule Check)

```
☐ Alle DRC-Fehler behoben
☐ Keine unverbundenen Netze
☐ Keine Clearance-Verletzungen
☐ Alle Footprints mit Netz verbunden
☐ Thermal Relief an Planes
```

---

## 5. Gerber-Export

### 5.1 Erforderliche Dateien

```
☐ F.Cu (GTL)
☐ In1.Cu (G2L)
☐ In2.Cu (G3L)
☐ B.Cu (GBL)
☐ F.Mask (GTS)
☐ B.Mask (GBS)
☐ F.SilkS (GTO)
☐ B.SilkS (GBO)
☐ F.Paste (GTP)
☐ B.Paste (GBP)
☐ Edge.Cuts (GKO)
☐ Drill (DRL/XLN)
```

### 5.2 Gerber-Einstellungen

```
Format: RS-274X
Koordinaten: 4.6 (mm) oder 2.5 (inch)
Nullen: Leading Zero Suppression
Drill: Excellon mit Plated/Non-Plated
```

### 5.3 Gerber-Prüfung

```
☐ In Gerber-Viewer öffnen (gerbv, KiCad)
☐ Alle Layer korrekt
☐ Board-Outline geschlossen
☐ Drill-Positionen stimmen
☐ Keine fehlenden Features
```

---

## 6. BOM & CPL

### 6.1 BOM (Bill of Materials)

```csv
Designator,Package,Value,MPN,Qty,Supplier
U1,LQFP-144,STM32H723ZGT6,STM32H723ZGT6,1,LCSC
U10,SOIC-8,TPS54331,TPS54331DR,1,LCSC
...
```

**Prüfpunkte:**
```
☐ Alle Bauteile gelistet
☐ MPN (Manufacturer Part Number) korrekt
☐ Gehäuse/Package stimmt
☐ Menge korrekt
☐ Bei LCSC/JLCPCB verfügbar
```

### 6.2 CPL (Component Placement List)

```csv
Designator,Mid X,Mid Y,Rotation,Layer
U1,72.00,42.00,0,top
R1,10.00,15.00,90,top
...
```

**Prüfpunkte:**
```
☐ Koordinaten in mm
☐ Mittelpunkt-Koordinaten (nicht Ecke!)
☐ Rotation korrekt (0/90/180/270)
☐ Layer stimmt (top/bottom)
```

---

## 7. Produktionsparameter

### 7.1 JLCPCB Einstellungen

```
PCB Thickness: 1.6mm
Layers: 4
Material: FR-4
Surface Finish: ENIG
Copper Weight: 1oz outer, 0.5oz inner
Min Track/Space: 0.15mm/0.15mm
Min Hole: 0.3mm
Via Process: Tenting
Impedance Control: Yes
  - Diff 90Ω für USB
  - Diff 100Ω für Ethernet
```

### 7.2 SMT Assembly

```
☐ SMT Assembly: Yes
☐ Tooling Holes: Added by JLCPCB
☐ Confirm Parts Placement: Yes
☐ Bake Components: No (nur bei Feuchtigkeit)
```

### 7.3 Kosten-Schätzung (5 Stück)

```
PCB (4-Layer, 110x85mm):     ~$35
SMT Assembly:                 ~$50
SMD Components:               ~$75
THT Components (selbst):      ~$60
Shipping:                     ~$25
────────────────────────────────
GESAMT:                      ~$245 (~€230)
Pro Board:                   ~$49 (~€46)
```

---

## 8. Finale Checkliste

```
╔═══════════════════════════════════════════════════════════════════════════════╗
║                    UFI PCB PRODUKTIONS-FREIGABE                                ║
╠═══════════════════════════════════════════════════════════════════════════════╣
║                                                                                ║
║   SCHEMATIC REVIEW                                                             ║
║   ☐ Stromversorgung vollständig                                               ║
║   ☐ STM32 Beschaltung korrekt                                                 ║
║   ☐ USB/Ethernet korrekt                                                      ║
║   ☐ Level-Shifter korrekt                                                     ║
║                                                                                ║
║   LAYOUT REVIEW                                                                ║
║   ☐ Layer-Stackup definiert                                                   ║
║   ☐ Impedanz-Kontrolle spezifiziert                                           ║
║   ☐ Kritische Signale geprüft                                                 ║
║   ☐ Power Integrity OK                                                        ║
║                                                                                ║
║   FOOTPRINTS                                                                   ║
║   ☐ Alle Footprints verifiziert                                               ║
║   ☐ Pin-1 Markierungen                                                        ║
║   ☐ Courtyard-Check                                                           ║
║                                                                                ║
║   DRC                                                                          ║
║   ☐ Keine Fehler                                                              ║
║   ☐ Keine Warnungen (oder begründet ignoriert)                                ║
║                                                                                ║
║   PRODUKTION                                                                   ║
║   ☐ Gerber exportiert & geprüft                                               ║
║   ☐ BOM vollständig                                                           ║
║   ☐ CPL korrekt                                                               ║
║   ☐ Kosten kalkuliert                                                         ║
║                                                                                ║
╠═══════════════════════════════════════════════════════════════════════════════╣
║                                                                                ║
║   FREIGABE FÜR PRODUKTION:  ☐ JA   ☐ NEIN                                     ║
║                                                                                ║
║   Datum: ____________    Unterschrift: _______________                         ║
║                                                                                ║
╚═══════════════════════════════════════════════════════════════════════════════╝
```

---

## 9. Bestellvorgang

### 9.1 Gerber hochladen

```
1. https://jlcpcb.com öffnen
2. "Quote Now" → Gerber ZIP hochladen
3. Parameter prüfen/anpassen
4. "SMT Assembly" aktivieren
5. BOM & CPL hochladen
6. Bauteile-Platzierung prüfen
7. Bestellen
```

### 9.2 THT-Bauteile separat bestellen

Bei LCSC/Mouser/DigiKey:

```
- FDD 34-pin Stiftleiste (2x)
- Apple 19-pin Stiftleiste
- 2x12 Stiftleiste (Amiga)
- DIN-6 Buchse
- DC-Jack
- Taster (2x)
- M.2 Connector
- USB-A Buchse
```

---

## 10. Nach Erhalt

```
1. ☐ Visuelle Inspektion
2. ☐ Kurzschluss-Test (ohne Spannung!)
3. ☐ THT-Bauteile einlöten
4. ☐ CM5 einsetzen
5. ☐ Erste Inbetriebnahme
6. ☐ Test-Prozeduren durchführen
```
