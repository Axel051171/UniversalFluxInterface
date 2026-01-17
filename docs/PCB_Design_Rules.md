# UFI Mainboard - PCB Design Rules

## Board Specifications

| Parameter | Value | Notes |
|-----------|-------|-------|
| Board Size | 160mm x 100mm | Standard Eurocard |
| Layer Count | 4 | Signal-GND-PWR-Signal |
| Board Thickness | 1.6mm | Standard FR4 |
| Copper Weight | 1oz (35µm) outer, 0.5oz (17.5µm) inner | |
| Surface Finish | ENIG | Required for CM5 connector |
| Solder Mask | Green (both sides) | |
| Silkscreen | White (both sides) | |

---

## Layer Stackup

```
┌─────────────────────────────────────────────────────────────────────┐
│                         4-LAYER STACKUP                              │
├─────────────────────────────────────────────────────────────────────┤
│                                                                      │
│  ═══════════════════════════════════════════════════════════════    │
│  │ F.Cu (Top)    │ 35µm │ Signal + Components                  │    │
│  ├───────────────┼──────┼──────────────────────────────────────┤    │
│  │ Prepreg       │ 0.2mm│ FR4, εr=4.5                          │    │
│  ├───────────────┼──────┼──────────────────────────────────────┤    │
│  │ In1.Cu (GND)  │ 35µm │ Solid GND plane (reference)          │    │
│  ├───────────────┼──────┼──────────────────────────────────────┤    │
│  │ Core          │1.065mm│ FR4, εr=4.5 (main structure)        │    │
│  ├───────────────┼──────┼──────────────────────────────────────┤    │
│  │ In2.Cu (PWR)  │ 35µm │ Split power plane (+3V3/+5V/+12V)   │    │
│  ├───────────────┼──────┼──────────────────────────────────────┤    │
│  │ Prepreg       │ 0.2mm│ FR4, εr=4.5                          │    │
│  ├───────────────┼──────┼──────────────────────────────────────┤    │
│  │ B.Cu (Bottom) │ 35µm │ Signal + Components                  │    │
│  ═══════════════════════════════════════════════════════════════    │
│                                                                      │
│  Total: 1.6mm (±10%)                                                │
│                                                                      │
└─────────────────────────────────────────────────────────────────────┘
```

---

## Design Rules

### General Rules

| Parameter | Min | Preferred | Max | Notes |
|-----------|-----|-----------|-----|-------|
| Track Width | 0.15mm | 0.2mm | - | Signal traces |
| Track Width (Power) | 0.5mm | 1.0mm | - | Power nets |
| Clearance | 0.15mm | 0.2mm | - | Track-to-track |
| Via Diameter | 0.4mm | 0.6mm | - | Pad size |
| Via Drill | 0.2mm | 0.3mm | - | Hole size |
| Annular Ring | 0.1mm | 0.15mm | - | |

### Impedance Control

| Signal Type | Target Impedance | Track Width | Notes |
|-------------|------------------|-------------|-------|
| Single-ended | 50Ω | 0.28mm | Over GND plane |
| USB 2.0 HS | 90Ω differential | 0.2mm/0.2mm gap | D+/D- |
| Ethernet | 100Ω differential | 0.15mm/0.15mm gap | TX/RX pairs |
| HDMI | 100Ω differential | 0.15mm/0.15mm gap | D0/D1/D2/CLK |
| PCIe | 100Ω differential | 0.15mm/0.15mm gap | TX/RX pairs |

### Length Matching

| Signal Pair | Max Mismatch | Notes |
|-------------|--------------|-------|
| USB D+/D- | 0.1mm | Within pair |
| Ethernet TX+/TX- | 0.5mm | Within pair |
| Ethernet RX+/RX- | 0.5mm | Within pair |
| HDMI Data pairs | 0.5mm | Within pair |
| HDMI All lanes | 2.0mm | Between lanes |
| PCIe TX/RX | 0.5mm | Within pair |

---

## Component Placement Guidelines

### CM5 Zone (Center-Left)

```
┌─────────────────────────────────────────────────────────────────┐
│                     CM5 MODULE PLACEMENT                         │
│                                                                  │
│    ┌─────────────────────────────────────────────────────┐      │
│    │                                                     │      │
│    │         KEEP-OUT ZONE (NO COMPONENTS)               │      │
│    │                                                     │      │
│    │    ┌───────────────────────────────────────┐       │      │
│    │    │                                       │       │      │
│    │    │          CM5 MODULE                   │       │      │
│    │    │          55mm x 40mm                  │       │      │
│    │    │                                       │       │      │
│    │    │   [J6]                       [J7]    │       │      │
│    │    │   DF40C                     DF40C    │       │      │
│    │    │                                       │       │      │
│    │    └───────────────────────────────────────┘       │      │
│    │                                                     │      │
│    │    • No tall components under CM5                   │      │
│    │    • Max component height: 2mm                      │      │
│    │    • Thermal vias under CM5 for heat transfer       │      │
│    │                                                     │      │
│    └─────────────────────────────────────────────────────┘      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### STM32H723 Zone (Center-Right)

```
┌─────────────────────────────────────────────────────────────────┐
│                    STM32H723 PLACEMENT                           │
│                                                                  │
│    ┌─────────────────────────────────────────────────────┐      │
│    │                                                     │      │
│    │                    ┌─────────┐                      │      │
│    │    [Y1 25MHz]──────│         │                      │      │
│    │                    │ STM32   │                      │      │
│    │    [Y2 32k]────────│ H723    │────[USB PHY]        │      │
│    │                    │ ZGT6    │                      │      │
│    │    [Flash]─────────│         │────[Level Shift]    │      │
│    │                    │ LQFP144 │                      │      │
│    │                    └─────────┘                      │      │
│    │                                                     │      │
│    │    Decoupling capacitors:                           │      │
│    │    • 100nF on each VDD (12 total)                   │      │
│    │    • 4.7µF bulk near power pins                     │      │
│    │    • Place within 3mm of pins                       │      │
│    │                                                     │      │
│    │    Crystal placement:                               │      │
│    │    • HSE crystal within 5mm of OSC pins             │      │
│    │    • Load capacitors adjacent to crystal            │      │
│    │    • Ground guard ring around crystal               │      │
│    │                                                     │      │
│    └─────────────────────────────────────────────────────┘      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

### Connector Zone (Top Edge)

```
┌─────────────────────────────────────────────────────────────────────────────────────────────┐
│                                    CONNECTOR PLACEMENT                                       │
│                                                                                              │
│   ┌────────────────────────────────────────────────────────────────────────────────────┐    │
│   │                                                                                    │    │
│   │  [J1: FDD1]  [J2: FDD2]  [J3: Apple]  [J4: Amiga DB-23]  [J5: IEC DIN-6]         │    │
│   │  34-pin      34-pin      19-pin       23-pin              6-pin                   │    │
│   │                                                                                    │    │
│   │  ═════════════════════════════════════════════════════════════════════════════   │    │
│   │                                     BOARD EDGE                                     │    │
│   │                                                                                    │    │
│   └────────────────────────────────────────────────────────────────────────────────────┘    │
│                                                                                              │
│   Notes:                                                                                     │
│   • All connectors on board edge for cable management                                        │
│   • Maintain 5mm clearance between connectors                                               │
│   • Strain relief considerations for through-hole connectors                                │
│                                                                                              │
└─────────────────────────────────────────────────────────────────────────────────────────────┘
```

### Power Zone (Bottom-Left)

```
┌─────────────────────────────────────────────────────────────────┐
│                      POWER SECTION                               │
│                                                                  │
│    ┌─────────────────────────────────────────────────────┐      │
│    │                                                     │      │
│    │  [J16: DC In]──►[F1]──►[D1]──►┌─────────────┐      │      │
│    │  12V/5A                        │ TPS54331    │      │      │
│    │                                │ 12V→5V/3A   │──►+5V│      │
│    │                                └─────────────┘      │      │
│    │                                       │             │      │
│    │                                       ▼             │      │
│    │                                ┌─────────────┐      │      │
│    │  [J8: USB-C]──►[D2]──►         │ TPS54331    │      │      │
│    │  5V/3A (alt)                   │ 5V→3.3V/3A  │──►+3V3     │
│    │                                └─────────────┘      │      │
│    │                                                     │      │
│    │  Layout rules:                                      │      │
│    │  • Short, wide traces for power                     │      │
│    │  • Inductor close to IC (within 5mm)               │      │
│    │  • Input/output caps adjacent to IC                 │      │
│    │  • Thermal vias under power ICs                     │      │
│    │  • Star grounding for analog/digital                │      │
│    │                                                     │      │
│    └─────────────────────────────────────────────────────┘      │
│                                                                  │
└─────────────────────────────────────────────────────────────────┘
```

---

## Critical Signal Routing

### USB High-Speed (480 Mbps)

```
USB D+/D- Routing Guidelines:
─────────────────────────────

1. Route as 90Ω differential pair
2. Track width: 0.2mm, gap: 0.2mm (for 1.6mm board)
3. Length match within 0.1mm
4. Keep traces on same layer (preferably F.Cu)
5. Avoid vias if possible; if needed, use matched via pairs
6. 33Ω series resistors near USB connector
7. ESD protection (USBLC6-2SC6) at connector

    [USB Connector]                    [STM32H723]
         │                                  │
    D+ ──┼──[33Ω]──────────────────────────┼── PA11 (USB_DP)
         │              └──┬──┘            │
    D- ──┼──[33Ω]──────────┘               ┼── PA12 (USB_DM)
         │                                  │
    GND ─┼──[ESD]──────────────────────────┼── GND
         │                                  │
```

### Ethernet RGMII

```
Ethernet Signal Routing:
────────────────────────

TX Signals (CM5 → PHY):
• TXD0-TXD3: Length match within 2mm
• TX_CLK: Separate routing, add 1-2ns delay if needed
• TX_EN: Can be routed with TXD group

RX Signals (PHY → CM5):
• RXD0-RXD3: Length match within 2mm
• RX_CLK: Provided by PHY
• RX_DV: Route with RXD group

MDI Pairs (PHY → MagJack):
• Route as 100Ω differential pairs
• Length match pairs within 0.5mm
• Keep pairs separated by at least 2x track width
```

---

## Power Plane Split

```
┌─────────────────────────────────────────────────────────────────────────────────────────┐
│                            IN2.Cu POWER PLANE LAYOUT                                     │
│                                                                                          │
│  ┌────────────────────────────────────────────────────────────────────────────────┐     │
│  │                                                                                │     │
│  │   ╔══════════════════════╗  ╔══════════════════════════════════════════════╗  │     │
│  │   ║                      ║  ║                                              ║  │     │
│  │   ║      +12V            ║  ║                   +5V                        ║  │     │
│  │   ║   (Drive Power)      ║  ║              (CM5, FDD Logic)                ║  │     │
│  │   ║                      ║  ║                                              ║  │     │
│  │   ╠══════════════════════╣  ╠══════════════════════════════════════════════╣  │     │
│  │   ║                      ║  ║                                              ║  │     │
│  │   ║      +3V3_ANALOG     ║  ║                  +3V3                        ║  │     │
│  │   ║    (STM32 ADC ref)   ║  ║             (STM32, Level Shifters)          ║  │     │
│  │   ║                      ║  ║                                              ║  │     │
│  │   ╚══════════════════════╝  ╚══════════════════════════════════════════════╝  │     │
│  │                                                                                │     │
│  └────────────────────────────────────────────────────────────────────────────────┘     │
│                                                                                          │
│  Notes:                                                                                  │
│  • Use 0.5mm gap between power domains                                                  │
│  • Connect domains with ferrite beads where needed                                      │
│  • In1.Cu (GND) should be solid, minimal cuts                                           │
│                                                                                          │
└─────────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Via Stitching

- Place GND vias every 5mm around board perimeter
- Place GND vias around high-speed signals (USB, Ethernet)
- Connect all GND pours on all layers
- Use 0.3mm vias for stitching

---

## Thermal Considerations

### Heat Sources
1. **STM32H723**: ~0.5W typical, 1W max
2. **Buck Regulators**: ~0.3W each at full load
3. **CM5 Module**: ~5W (handled by CM5 heatsink)

### Thermal Relief
- Use thermal vias under STM32 exposed pad (minimum 9 vias, 0.3mm)
- Thermal vias under power regulators
- Connect thermal pads to inner GND plane
- Consider copper pour on bottom layer under hot components

---

## DFM Checklist

- [ ] Minimum track width ≥ 0.15mm
- [ ] Minimum clearance ≥ 0.15mm
- [ ] Minimum drill ≥ 0.2mm
- [ ] Minimum annular ring ≥ 0.1mm
- [ ] No acute angles in traces (use 45° or curves)
- [ ] Solder mask between fine-pitch pads
- [ ] Silkscreen not over pads
- [ ] Fiducials for SMT assembly (3x, diagonal)
- [ ] Test points for critical signals
- [ ] Board outline closed polygon
- [ ] Mounting holes with proper clearance
