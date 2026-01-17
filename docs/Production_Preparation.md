# UFI PCB Final-Check & Produktions-Vorbereitung

## Übersicht

Checkliste und Vorbereitung für die PCB-Fertigung bei JLCPCB oder ähnlichen Herstellern.

---

## 1. Design Rule Check (DRC)

### 1.1 Generelle Regeln (JLCPCB 4-Layer)

| Parameter | Min. Wert | UFI Design | Status |
|-----------|-----------|------------|--------|
| Min. Track Width | 0.09mm | 0.15mm | ✓ |
| Min. Spacing | 0.09mm | 0.15mm | ✓ |
| Min. Via Drill | 0.2mm | 0.3mm | ✓ |
| Min. Via Diameter | 0.4mm | 0.6mm | ✓ |
| Min. Annular Ring | 0.075mm | 0.15mm | ✓ |
| Min. PTH Drill | 0.2mm | 0.8mm | ✓ |
| Min. NPTH Drill | 0.5mm | 3.2mm | ✓ |
| Board Thickness | 1.6mm | 1.6mm | ✓ |

### 1.2 Impedanz-kontrollierte Leitungen

| Signal | Impedanz | Track Width | Spacing | Layer |
|--------|----------|-------------|---------|-------|
| USB HS D+/D- | 90Ω diff | 0.2mm | 0.15mm | F.Cu |
| Ethernet MDI | 100Ω diff | 0.15mm | 0.18mm | F.Cu |
| PCIe TX/RX | 85Ω diff | 0.18mm | 0.15mm | B.Cu |

### 1.3 Copper Pour Check

- [ ] GND Plane (In1.Cu) vollständig gefüllt
- [ ] +5V Region (In2.Cu links) korrekt definiert
- [ ] +3V3 Region (In2.Cu rechts) korrekt definiert
- [ ] Top/Bottom GND Pours vorhanden
- [ ] Keine isolierten Kupfer-Inseln
- [ ] Thermal Relief bei Power Pins

---

## 2. Schematic Review

### 2.1 Power

| Rail | Quelle | Verbraucher | Kapazität |
|------|--------|-------------|-----------|
| +12V | DC Jack / USB-PD | Buck Regler | 100µF Elko |
| +5V | TPS54331 | CM5, Laufwerke, Level-Shifter | 220µF + 10µF×4 |
| +3V3 | TPS54331 | STM32, Flash, Logik | 220µF + 10µF×4 |
| +1V8 | LDO | STM32 Core | 10µF×2 |

### 2.2 Decoupling

| IC | 100nF | 10µF | 4.7µF | Status |
|----|-------|------|-------|--------|
| STM32H723 | 12× | 2× | - | ✓ |
| W25Q128 | 1× | - | - | ✓ |
| 74LVC245 (×4) | 4× | - | - | ✓ |
| CM5 Connector | - | 4× | - | ✓ |

### 2.3 Kritische Verbindungen

- [ ] STM32 VBAT an 3V3 (mit 100nF)
- [ ] STM32 VCAP1/VCAP2 an GND (mit 2.2µF)
- [ ] HSE Crystal Load Caps korrekt (12-20pF)
- [ ] USB Pull-up Widerstände (1.5kΩ)
- [ ] Ethernet Termination (49.9Ω)
- [ ] Reset RC-Filter (10kΩ + 100nF)
- [ ] BOOT0 Pull-down (10kΩ)

---

## 3. Footprint Verification

### 3.1 Custom Footprints

| Footprint | Datenblatt | Pin 1 | Courtyard | 3D |
|-----------|------------|-------|-----------|-----|
| Hirose DF40C-100DP | ✓ | ✓ | ✓ | - |
| LQFP-144 STM32 | ✓ | ✓ | ✓ | ✓ |
| FDD 34-pin | ✓ | ✓ | ✓ | - |
| Apple 19-pin | ✓ | ✓ | ✓ | - |
| Amiga 2×12 Header | ✓ | ✓ | ✓ | - |
| DIN-6 IEC | ✓ | ✓ | ✓ | - |
| RJ45 Magjack | ✓ | ✓ | ✓ | ✓ |
| USB-C HRO | ✓ | ✓ | ✓ | ✓ |
| M.2 2242 | ✓ | ✓ | ✓ | - |
| Module Port 2×5 | ✓ | ✓ | ✓ | - |

### 3.2 Polarity Markings

- [ ] Elkos: Plus-Markierung auf Silkscreen
- [ ] Dioden: Kathoden-Markierung
- [ ] LEDs: Kathoden-Markierung
- [ ] ICs: Pin 1 Markierung
- [ ] Stecker: Pin 1 / Key Position

---

## 4. Manufacturing Files

### 4.1 Gerber-Export (KiCad 8)

```
Einstellungen:
- Format: Gerber X2
- Koordinaten: 4.6 Format
- Einheit: mm
- Plot Reference Designators: Ja
- Plot Footprint Values: Ja
```

**Benötigte Layer:**

| Datei | Layer | Inhalt |
|-------|-------|--------|
| UFI-F_Cu.gbr | F.Cu | Top Copper |
| UFI-In1_Cu.gbr | In1.Cu | GND Plane |
| UFI-In2_Cu.gbr | In2.Cu | Power Split |
| UFI-B_Cu.gbr | B.Cu | Bottom Copper |
| UFI-F_Paste.gbr | F.Paste | SMD Paste Top |
| UFI-B_Paste.gbr | B.Paste | SMD Paste Bottom |
| UFI-F_Silkscreen.gbr | F.SilkS | Silkscreen Top |
| UFI-B_Silkscreen.gbr | B.SilkS | Silkscreen Bottom |
| UFI-F_Mask.gbr | F.Mask | Solder Mask Top |
| UFI-B_Mask.gbr | B.Mask | Solder Mask Bottom |
| UFI-Edge_Cuts.gbr | Edge.Cuts | Board Outline |
| UFI-PTH.drl | - | Plated Holes |
| UFI-NPTH.drl | - | Non-Plated Holes |

### 4.2 Drill Files

```
Format: Excellon
Einheit: mm
Zeros: Suppress Leading
Koordinaten: Absolut
PTH und NPTH getrennt
```

### 4.3 Pick & Place (CPL)

```csv
Designator,Mid X,Mid Y,Layer,Rotation
U1,72,42,top,0
U2,86,35,top,0
U10,18,65,top,0
...
```

**Format für JLCPCB:**
- Spalten: Designator, Mid X, Mid Y, Layer, Rotation
- Koordinaten in mm
- Rotation in Grad (0-359)
- Layer: "top" oder "bottom"

### 4.4 BOM (Stückliste)

```csv
Comment,Designator,Footprint,LCSC Part
STM32H723ZGT6,U1,LQFP-144,C2062638
W25Q128JVSIQ,U2,SOIC-8,C97521
TPS54331DR,U10;U11,SOIC-8,C9865
74LVC245APW,U20;U21;U22;U23,TSSOP-20,C6396
...
```

---

## 5. JLCPCB Bestelloptionen

### 5.1 PCB Spezifikation

| Option | Auswahl |
|--------|---------|
| Base Material | FR-4 |
| Layers | 4 |
| Dimensions | 110mm × 85mm |
| PCB Qty | 5 (Minimum) |
| PCB Thickness | 1.6mm |
| Impedance Control | Yes |
| Layer Stackup | JLC04161H-7628 |
| PCB Color | Green (oder Schwarz) |
| Silkscreen | White |
| Surface Finish | ENIG |
| Via Covering | Tented |
| Min Track/Spacing | 0.15/0.15mm |
| Min Hole Size | 0.3mm |
| Board Outline Tolerance | ±0.2mm |

### 5.2 PCBA Optionen

| Option | Auswahl |
|--------|---------|
| Assembly Side | Top |
| Tooling Holes | Added by JLCPCB |
| Confirm Parts Placement | Yes |
| Economic/Standard | Standard (für Fine-Pitch) |

### 5.3 Stackup (JLC04161H-7628)

```
┌─────────────────────────────────────┐
│ F.Cu (Signal)          35µm        │
├─────────────────────────────────────┤
│ Prepreg 7628           0.2mm       │
├─────────────────────────────────────┤
│ In1.Cu (GND)           35µm        │
├─────────────────────────────────────┤
│ Core                   1.065mm      │
├─────────────────────────────────────┤
│ In2.Cu (Power)         35µm        │
├─────────────────────────────────────┤
│ Prepreg 7628           0.2mm       │
├─────────────────────────────────────┤
│ B.Cu (Signal)          35µm        │
└─────────────────────────────────────┘
Total: 1.6mm
```

---

## 6. Vor-Produktions-Checkliste

### 6.1 Design Freeze

- [ ] Alle Schaltplan-Änderungen eingefroren
- [ ] DRC in KiCad ohne Fehler
- [ ] ERC in KiCad ohne kritische Fehler
- [ ] Alle Netze verbunden (keine offenen)
- [ ] Alle Footprints zugewiesen

### 6.2 Review

- [ ] Schaltplan von zweiter Person geprüft
- [ ] PCB Layout von zweiter Person geprüft
- [ ] Kritische Signale (USB, ETH) auf Integrität geprüft
- [ ] Thermal Management geprüft

### 6.3 Dateien

- [ ] Gerber-Export durchgeführt
- [ ] Drill-Files exportiert
- [ ] BOM erstellt und LCSC-Nummern verifiziert
- [ ] CPL erstellt und Rotationen geprüft
- [ ] Alle Dateien in ZIP gepackt

### 6.4 Bestellung

- [ ] Gerber bei JLCPCB hochgeladen
- [ ] Stackup als "Impedance Control" ausgewählt
- [ ] BOM/CPL für PCBA hochgeladen
- [ ] Teil-Verfügbarkeit geprüft
- [ ] Lieferzeit notiert

---

## 7. Kosten-Kalkulation (5 Stück)

### 7.1 PCB

| Position | Preis |
|----------|-------|
| PCB 4-Layer 110×85mm ENIG | $33.00 |
| Impedance Control | +$0.00 (inkl.) |
| Shipping (DHL) | $25.00 |
| **PCB Total** | **$58.00** |
| **Pro Stück** | **$11.60** |

### 7.2 PCBA (SMD)

| Position | Preis |
|----------|-------|
| Assembly Fee | $8.00 |
| Stencil | $1.50 |
| Components (SMD) | ~$60.00 |
| Extended Parts Fee | ~$3.00 |
| **PCBA Total** | **$72.50** |
| **Pro Stück** | **$14.50** |

### 7.3 THT Komponenten (selbst löten)

| Position | Preis |
|----------|-------|
| FDD Connector ×2 | $4.00 |
| Apple Connector | $3.00 |
| 2×12 Header (Amiga) | $0.50 |
| DIN-6 IEC | $2.00 |
| RJ45 Magjack | $2.50 |
| USB-C ×2 | $1.20 |
| DC Jack | $0.50 |
| Pin Headers | $1.00 |
| **THT Total** | **$14.70** |

### 7.4 Gesamt

| Position | 5 Stück | Pro Stück |
|----------|---------|-----------|
| PCB | $58.00 | $11.60 |
| PCBA | $72.50 | $14.50 |
| THT | $73.50 | $14.70 |
| **Ohne CM5** | **$204.00** | **~$41** |
| + CM5 4GB | +$350.00 | +$70.00 |
| **Mit CM5** | **$554.00** | **~$111** |

---

## 8. Produktions-Dateien ZIP

### Struktur:

```
UFI_Production_V2.1.zip
├── Gerber/
│   ├── UFI-F_Cu.gbr
│   ├── UFI-In1_Cu.gbr
│   ├── UFI-In2_Cu.gbr
│   ├── UFI-B_Cu.gbr
│   ├── UFI-F_Paste.gbr
│   ├── UFI-B_Paste.gbr
│   ├── UFI-F_Silkscreen.gbr
│   ├── UFI-B_Silkscreen.gbr
│   ├── UFI-F_Mask.gbr
│   ├── UFI-B_Mask.gbr
│   ├── UFI-Edge_Cuts.gbr
│   ├── UFI-PTH.drl
│   └── UFI-NPTH.drl
├── Assembly/
│   ├── UFI_BOM.csv
│   └── UFI_CPL.csv
├── Documentation/
│   ├── UFI_Schematic.pdf
│   ├── UFI_Assembly.pdf
│   └── UFI_3D_Top.png
└── README.txt
```

---

## 9. Nach Erhalt

### 9.1 Eingangsprüfung

- [ ] Richtige Anzahl PCBs
- [ ] Abmessungen korrekt (110×85mm ±0.2mm)
- [ ] Bohrungen vorhanden und korrekt
- [ ] Silkscreen lesbar
- [ ] ENIG-Finish gleichmäßig
- [ ] Keine sichtbaren Defekte

### 9.2 Elektrische Prüfung

- [ ] Durchgangsprüfung kritischer Netze
- [ ] Kurzschluss-Prüfung Power Rails
- [ ] Via-Durchgang stichprobenartig prüfen

### 9.3 THT Bestückung

Reihenfolge:
1. Niedrige Bauteile zuerst (Stiftleisten)
2. Dann Buchsen (USB-C, RJ45)
3. Zuletzt große Stecker (FDD, DC Jack)

---

## 10. Versionierung

| Version | Datum | Änderungen |
|---------|-------|------------|
| 1.0 | - | Original 160×100mm |
| 2.0 | - | Headless 110×85mm |
| 2.1 | 2026-01-17 | Modular (Pin-Headers, Module Port) |

**Aktuelle Produktion: v2.1 Modular**
