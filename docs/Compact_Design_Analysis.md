# UFI Mainboard - Kompaktdesign Analyse

## Aktuelle Größe: 160mm x 100mm (160 cm²)

## Komponenten-Mindestgrößen

| Komponente | Breite | Höhe | Fläche | Fest? |
|------------|--------|------|--------|-------|
| CM5 Module | 55mm | 40mm | 22 cm² | ✅ Fix |
| STM32H723 LQFP-144 | 22mm | 22mm | 4.8 cm² | ✅ Fix |
| 2x 34-pin FDD (vertikal) | 12mm | 45mm | 5.4 cm² | Orientierung |
| Apple 19-pin | 10mm | 25mm | 2.5 cm² | Orientierung |
| DB-23 Amiga | 42mm | 15mm | 6.3 cm² | ✅ Fix |
| DIN-6 IEC | 22mm | 22mm | 4.8 cm² | ✅ Fix |
| RJ45 MagJack | 16mm | 22mm | 3.5 cm² | ✅ Fix |
| HDMI Type-A | 15mm | 12mm | 1.8 cm² | ✅ Fix |
| USB-C x2 | 10mm | 8mm | 0.8 cm² | ✅ Fix |
| M.2 2242 | 22mm | 45mm | 10 cm² | Optional |
| 40-pin Header | 52mm | 6mm | 3.1 cm² | ✅ Fix |
| DC Jack | 14mm | 10mm | 1.4 cm² | Optional |
| Power Section | 25mm | 20mm | 5 cm² | ~Fix |
| Level Shifters | 20mm | 15mm | 3 cm² | ~Fix |

**Summe Komponenten:** ~74 cm²
**Routing/Clearance Faktor:** 1.5x
**Minimum theoretisch:** ~111 cm²

---

## Optimierungs-Optionen

### Option A: Alles behalten, nur kompakter
```
Reduzierungen:
- M.2: 2280 → 2242 (spart 38mm Länge)
- FDD Connectors: vertikal statt horizontal
- Tighter placement (6-Layer für besseres Routing)

Ergebnis: ~130mm x 85mm (110 cm²)
```

### Option B: M.2 weglassen (USB Storage stattdessen)
```
Reduzierungen:
- Kein M.2 Slot (CM5 hat USB 3.0 für externe SSD)
- Kompaktere Anordnung

Ergebnis: ~120mm x 80mm (96 cm²)
```

### Option C: Minimal (1x FDD, kein M.2, kein Apple II)
```
Reduzierungen:
- 1x FDD statt 2x
- Kein Apple II Connector
- Kein M.2

Ergebnis: ~100mm x 80mm (80 cm²)
```

---

## Empfehlung: Kompakt-Version

### 120mm x 85mm (4-Layer) oder 100mm x 85mm (6-Layer)

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                     UFI COMPACT - 120mm x 85mm                                       │
│                                                                                      │
│   ┌──────────────────────────────────────────────────────────────────────────────┐  │
│   │ [FDD1] [FDD2] [Apple]     [Amiga DB-23]        [IEC]                         │  │
│   │  ║║║    ║║║    ║║║         ═══════════          ◯                            │  │
│   ├──────────────────────────────────────────────────────────────────────────────┤  │
│   │                                                                              │  │
│   │   ┌────────────────────┐  ┌──────────────┐  ┌───────────────────┐           │  │
│   │   │                    │  │   40-pin     │  │                   │  [RJ45]   │  │
│   │   │    CM5 MODULE      │  │   ════════   │  │    STM32H723      │  ┌────┐   │  │
│   │   │    55mm x 40mm     │  │   Expansion  │  │                   │  │    │   │  │
│   │   │                    │  │              │  │   [Flash] [Xtal]  │  └────┘   │  │
│   │   │   [J6]      [J7]   │  │              │  │                   │           │  │
│   │   │                    │  └──────────────┘  │   [Level Shift]   │  [HDMI]   │  │
│   │   └────────────────────┘                    └───────────────────┘  ┌────┐   │  │
│   │                                                                    │    │   │  │
│   │   ┌─────────────────────────────────────┐                         └────┘   │  │
│   │   │  [DC] [Buck] [Buck]    [M.2 2242]   │                                   │  │
│   │   │  12V   5V    3.3V     ═══════════   │                         [USB-C]   │  │
│   │   │                                     │                         [USB-C]   │  │
│   │   │  [USB-C PWR]                        │                                   │  │
│   │   └─────────────────────────────────────┘                         [LEDs]    │  │
│   │                                                                   [BTN]     │  │
│   └──────────────────────────────────────────────────────────────────────────────┘  │
│                                                                                      │
│   Fläche: 102 cm² (vs. 160 cm² Original) = 36% kleiner                              │
│                                                                                      │
└─────────────────────────────────────────────────────────────────────────────────────┘
```

---

## Ultra-Kompakt: 100mm x 80mm (6-Layer)

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    UFI MINI - 100mm x 80mm                                   │
│                                                                              │
│   ┌──────────────────────────────────────────────────────────────────────┐  │
│   │ [FDD1][FDD2]    [Amiga]         [IEC]    [RJ45][HDMI]                │  │
│   ├──────────────────────────────────────────────────────────────────────┤  │
│   │                                                                      │  │
│   │  ┌──────────────────┐  ┌─────────────────────┐                      │  │
│   │  │                  │  │                     │                      │  │
│   │  │   CM5 MODULE     │  │    STM32H723        │         [USB-C]      │  │
│   │  │                  │  │    + Flash          │         [USB-C]      │  │
│   │  │                  │  │    + Xtal           │                      │  │
│   │  │                  │  │                     │         [40-pin]     │  │
│   │  └──────────────────┘  └─────────────────────┘         ════════     │  │
│   │                                                        Expansion    │  │
│   │  ┌────────────────────────────────────────────────────┐             │  │
│   │  │  [DC/USB-C] [Power] [M.2 2242 optional]            │   [LED]    │  │
│   │  └────────────────────────────────────────────────────┘   [BTN]    │  │
│   │                                                                      │  │
│   └──────────────────────────────────────────────────────────────────────┘  │
│                                                                              │
│   Fläche: 80 cm² = 50% kleiner als Original!                                │
│   Aber: 6-Layer nötig (~$15 mehr pro PCB)                                   │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Größenvergleich

```
┌───────────────────────────────────────────────────────────────────────────────┐
│                         GRÖßENVERGLEICH                                        │
│                                                                                │
│   Original (160x100mm)                                                         │
│   ┌────────────────────────────────────────────────────────────────────────┐  │
│   │                                                                        │  │
│   │                                                                        │  │
│   │                           160 cm²                                      │  │
│   │                                                                        │  │
│   │                                                                        │  │
│   └────────────────────────────────────────────────────────────────────────┘  │
│                                                                                │
│   Kompakt (120x85mm)                        Ultra (100x80mm)                  │
│   ┌──────────────────────────────────┐      ┌────────────────────────┐       │
│   │                                  │      │                        │       │
│   │          102 cm²                 │      │       80 cm²           │       │
│   │          -36%                    │      │       -50%             │       │
│   │                                  │      │                        │       │
│   └──────────────────────────────────┘      └────────────────────────┘       │
│                                                                                │
│   Referenz: Raspberry Pi 4 = 85x56mm (48 cm²)                                 │
│   Referenz: Arduino Mega = 102x53mm (54 cm²)                                  │
│                                                                                │
└───────────────────────────────────────────────────────────────────────────────┘
```

---

## Kosten-Einfluss

| Version | Größe | Layer | PCB-Preis | Assembly | Gesamt |
|---------|-------|-------|-----------|----------|--------|
| Original | 160x100mm | 4 | €9 | €8 | €17 |
| Kompakt | 120x85mm | 4 | €7 | €7 | €14 |
| Ultra | 100x80mm | 6 | €12 | €7 | €19 |
| Mini* | 100x80mm | 4 | €6 | €7 | €13 |

*Mini = ohne M.2, nur 1x FDD

---

## Empfehlung

### Beste Balance: 120mm x 85mm (4-Layer)

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   ✅ Alle Features behalten                                     │
│   ✅ 36% kleiner als Original                                   │
│   ✅ 4-Layer (günstiger als 6-Layer)                           │
│   ✅ M.2 2242 statt 2280 (reicht für 256GB-1TB)                │
│   ✅ Passt in Standard-Gehäuse (125x80mm gibt es)              │
│   ✅ ~€3 günstiger pro Board                                    │
│                                                                 │
│   Einzelpreis: ~€51 (statt €54)                                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Für maximale Kompaktheit: 100mm x 80mm (6-Layer)

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   ✅ 50% kleiner                                                │
│   ✅ Alle wichtigen Features                                    │
│   ⚠️  6-Layer nötig (+€5 pro Board)                            │
│   ⚠️  Engeres Routing (anspruchsvoller)                        │
│   ⚠️  Weniger Platz für Modifikationen                         │
│                                                                 │
│   Einzelpreis: ~€56 (wegen 6-Layer)                            │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```
