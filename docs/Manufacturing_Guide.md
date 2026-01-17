# UFI Mainboard - Manufacturing Guide

## Gerber Export Settings (KiCad 8)

### File > Plot

| Setting | Value |
|---------|-------|
| Output directory | `gerber/` |
| Plot format | Gerber |
| Use Protel filename extensions | ✅ Yes |
| Gerber options: Use extended X2 format | ✅ Yes |
| Include Gerber job file | ✅ Yes |
| Subtract soldermask from silkscreen | ✅ Yes |

### Layers to Plot

| Layer | Extension | Description |
|-------|-----------|-------------|
| F.Cu | .GTL | Top copper |
| In1.Cu | .G2 | Inner layer 1 (GND) |
| In2.Cu | .G3 | Inner layer 2 (PWR) |
| B.Cu | .GBL | Bottom copper |
| F.Paste | .GTP | Top solder paste |
| B.Paste | .GBP | Bottom solder paste |
| F.SilkS | .GTO | Top silkscreen |
| B.SilkS | .GBO | Bottom silkscreen |
| F.Mask | .GTS | Top solder mask |
| B.Mask | .GBS | Bottom solder mask |
| Edge.Cuts | .GKO | Board outline |

### Drill Files

| Setting | Value |
|---------|-------|
| File > Fabrication Outputs > Drill Files | |
| Drill File Format | Excellon |
| PTH and NPTH in single file | No (separate) |
| Oval Holes Drill Mode | Use alternate drill mode |
| Map File Format | Gerber |
| Drill Origin | Absolute |
| Drill Units | Millimeters |
| Zeros Format | Decimal format |

---

## JLCPCB Order Specifications

### PCB Specifications

```
┌────────────────────────────────────────────────────────────┐
│              JLCPCB ORDER SETTINGS                          │
├────────────────────────────────────────────────────────────┤
│                                                             │
│  Base Material:        FR-4                                 │
│  Layers:               4                                    │
│  Dimensions:           160mm x 100mm                        │
│  PCB Qty:              5 (minimum)                          │
│  Product Type:         Industrial/Consumer electronics      │
│                                                             │
│  PCB Thickness:        1.6mm                                │
│  PCB Color:            Green (or Black)                     │
│  Silkscreen:           White                                │
│  Surface Finish:       ENIG (required for CM5 connector!)   │
│                                                             │
│  Outer Copper Weight:  1 oz                                 │
│  Inner Copper Weight:  0.5 oz                               │
│                                                             │
│  Via Covering:         Tented                               │
│  Min via hole size:    0.2mm ✓                              │
│  Min track/spacing:    0.15mm/0.15mm ✓                      │
│                                                             │
│  Gold Fingers:         No                                   │
│  Castellated Holes:    No                                   │
│  Confirm Production:   Yes (for 4-layer)                    │
│                                                             │
│  Impedance Control:    Yes - JLC04161H-7628                 │
│    • USB: 90Ω differential                                  │
│    • Ethernet: 100Ω differential                            │
│                                                             │
├────────────────────────────────────────────────────────────┤
│  Estimated Price (5 pcs):  ~$45-65 USD                      │
│  Production Time:          7-10 days                        │
│  Shipping:                 ~$15-25 USD (DHL)                │
└────────────────────────────────────────────────────────────┘
```

### Layer Stackup for JLCPCB JLC04161H-7628

```
┌─────────────────────────────────────────────────────────────┐
│               JLC04161H-7628 STACKUP                         │
├─────────────────────────────────────────────────────────────┤
│                                                              │
│  Layer         │ Thickness │ Material    │ Impedance        │
│  ──────────────┼───────────┼─────────────┼─────────────────  │
│  Top Solder    │ ~15µm     │ Solder Mask │                  │
│  F.Cu (L1)     │ 35µm      │ Copper      │ 50Ω SE           │
│  Prepreg       │ 0.2104mm  │ 7628*1      │                  │
│  In1.Cu (L2)   │ 17.5µm    │ Copper      │ GND Plane        │
│  Core          │ 1.065mm   │ FR4 Core    │                  │
│  In2.Cu (L3)   │ 17.5µm    │ Copper      │ PWR Plane        │
│  Prepreg       │ 0.2104mm  │ 7628*1      │                  │
│  B.Cu (L4)     │ 35µm      │ Copper      │ 50Ω SE           │
│  Bot Solder    │ ~15µm     │ Solder Mask │                  │
│  ──────────────┼───────────┼─────────────┼─────────────────  │
│  Total         │ ~1.6mm    │             │                  │
│                                                              │
│  Impedance Targets:                                          │
│  • Single-ended 50Ω: Track width ~0.35mm over GND           │
│  • Differential 90Ω: Track 0.2mm, gap 0.15mm (USB)          │
│  • Differential 100Ω: Track 0.15mm, gap 0.18mm (Ethernet)   │
│                                                              │
└─────────────────────────────────────────────────────────────┘
```

---

## SMT Assembly (JLCPCB)

### Assembly Service Selection

| Option | Setting |
|--------|---------|
| PCBA Type | Standard |
| Assembly Side | Top Side (or Both if needed) |
| Tooling holes | Added by JLCPCB |
| Confirm Parts Placement | Yes |

### Required Files for Assembly

1. **BOM (Bill of Materials)** - CSV format
2. **CPL (Component Placement List)** - CSV format

### BOM Format (JLCPCB)

```csv
Comment,Designator,Footprint,LCSC Part #
STM32H723ZGT6,U1,LQFP-144_20x20mm,C2677129
W25Q128JVSIQ,U2,SOIC-8,C97521
TPS54331DR,U10,SOIC-8,C9865
TPS54331DR,U11,SOIC-8,C9865
74LVC245APW,U20,TSSOP-20,C6448
74LVC245APW,U21,TSSOP-20,C6448
74LVC245APW,U22,TSSOP-20,C6448
74LVC245APW,U23,TSSOP-20,C6448
25MHz Crystal,Y1,3215-4Pin,C32346
32.768kHz Crystal,Y2,2012-2Pin,C32346
100nF 0402,C1-C50,0402,C1525
10uF 0805,C51-C60,0805,C15850
10kΩ 0402,R1-R30,0402,C25744
1kΩ 0402,R31-R40,0402,C11702
LED Green 0603,LED1,0603,C72043
LED Blue 0603,LED2-LED4,0603,C72041
```

### CPL Format (JLCPCB)

```csv
Designator,Mid X,Mid Y,Layer,Rotation
U1,105,50,top,0
U2,120,42,top,0
U10,25,80,top,0
U11,42,80,top,0
U20,75,30,top,0
U21,85,30,top,0
U22,95,30,top,0
U23,65,65,top,0
Y1,92,42,top,0
Y2,92,58,top,0
J6,25,40,top,0
J7,25,60,top,0
```

---

## Component Sourcing

### Critical Components (Order Separately)

| Component | Source | Part Number | Qty | Notes |
|-----------|--------|-------------|-----|-------|
| CM5 4GB | RPi Official | SC1112 | 1 | User provided |
| Hirose DF40C-100DP | Mouser | 798-DF40C100DP04V51 | 2 | CM5 connectors |
| RJ45 MagJack | LCSC | C708532 | 1 | With integrated magnetics |
| USB-C Receptacle | LCSC | C168688 | 2 | Mid-mount |
| 34-pin IDC Header | LCSC | C358713 | 2 | Shrouded |
| DB-23 Female | DigiKey | A32090-ND | 1 | Amiga connector |
| DIN-6 Female | DigiKey | CP-2360-ND | 1 | IEC bus |
| M.2 Connector | LCSC | C2765186 | 1 | Key M |

### Components Available at JLCPCB

Most SMD components (resistors, capacitors, ICs) are available in JLCPCB's parts library:
- STM32H723ZGT6: C2677129
- TPS54331: C9865
- 74LVC245: C6448
- W25Q128: C97521
- Standard passives: All available

---

## Pre-Manufacturing Checklist

### DRC (Design Rule Check)

```
□ Run DRC in KiCad (Inspect > Design Rules Checker)
□ No errors in DRC report
□ All nets connected (no unconnected pads)
□ No clearance violations
□ No copper pour islands
```

### ERC (Electrical Rule Check)

```
□ Run ERC in schematic editor
□ All power pins connected
□ No floating inputs
□ No conflicting outputs
```

### Visual Inspection

```
□ All components placed correctly
□ Silkscreen readable (not over pads)
□ Reference designators visible
□ Mounting holes correct size (M3 = 3.2mm)
□ Board outline closed
□ Fiducials present (if using assembly)
```

### Gerber Verification

```
□ Open Gerbers in GerbView or online viewer
□ All layers align correctly
□ Drill hits match pad locations
□ No missing copper
□ Edge cuts form closed polygon
```

---

## File Package for Manufacturing

Create a ZIP file with:

```
UFI_Mainboard_Gerber_v0.2.zip
├── UFI_Mainboard-F_Cu.gtl
├── UFI_Mainboard-In1_Cu.g2
├── UFI_Mainboard-In2_Cu.g3
├── UFI_Mainboard-B_Cu.gbl
├── UFI_Mainboard-F_Paste.gtp
├── UFI_Mainboard-B_Paste.gbp
├── UFI_Mainboard-F_SilkS.gto
├── UFI_Mainboard-B_SilkS.gbo
├── UFI_Mainboard-F_Mask.gts
├── UFI_Mainboard-B_Mask.gbs
├── UFI_Mainboard-Edge_Cuts.gko
├── UFI_Mainboard-PTH.drl
├── UFI_Mainboard-NPTH.drl
└── UFI_Mainboard-job.gbrjob
```

For assembly, add:
```
UFI_Mainboard_Assembly_v0.2.zip
├── UFI_Mainboard_BOM.csv
└── UFI_Mainboard_CPL.csv
```

---

## Estimated Costs

| Item | Quantity | Unit Cost | Total |
|------|----------|-----------|-------|
| PCB (4-layer, ENIG) | 5 | $10 | $50 |
| SMT Assembly | 5 | $15 | $75 |
| Components (JLCPCB) | 5 sets | $30 | $150 |
| Shipping (DHL) | 1 | $25 | $25 |
| **Subtotal** | | | **$300** |
| | | | |
| **Per Board** | | | **$60** |

### Components to Order Separately

| Item | Quantity | Unit Cost | Total |
|------|----------|-----------|-------|
| Hirose DF40C-100DP | 10 | $3.50 | $35 |
| RJ45 MagJack | 5 | $2.50 | $12.50 |
| DB-23 Female | 5 | $3.00 | $15 |
| DIN-6 Female | 5 | $1.50 | $7.50 |
| USB-C Receptacle | 10 | $0.50 | $5 |
| 34-pin IDC | 10 | $0.80 | $8 |
| M.2 Connector | 5 | $1.80 | $9 |
| **Subtotal** | | | **$92** |

### Total Project Cost (5 boards)

| Category | Cost |
|----------|------|
| PCB + Assembly | $300 |
| Separate Components | $92 |
| CM5 Module (1x) | $75 |
| **Total** | **$467** |
| **Per Complete Unit** | **~$155** |

---

## Revision History

| Rev | Date | Changes |
|-----|------|---------|
| 0.1 | 2026-01-16 | Initial design |
| 0.2 | 2026-01-17 | Component placement, routing prep |
