# UFI Headless - Einzelpreis-Kalkulation

## Board-Größe: 110mm x 85mm (93.5 cm²)

**42% kleiner als Original (160x100mm)**
**Kein HDMI - Steuerung über Web/SSH**

---

## Szenario 1: Kleinserie (5 Stück bei JLCPCB)

### PCB Fertigung
| Posten | Gesamt (5 Stk) | Pro Stück |
|--------|----------------|-----------|
| 4-Layer PCB 110x85mm | $15 | $3.00 |
| ENIG Finish (Aufpreis) | $10 | $2.00 |
| Impedanzkontrolle | $8 | $1.60 |
| **PCB Subtotal** | **$33** | **$6.60** |

### SMT Assembly (JLCPCB)
| Posten | Gesamt (5 Stk) | Pro Stück |
|--------|----------------|-----------|
| Setup Fee | $8 | $1.60 |
| Assembly (pro Pad) | $30 | $6.00 |
| Stencil | $7 | $1.40 |
| **Assembly Subtotal** | **$45** | **$9.00** |

### SMD Bauteile (JLCPCB Lager)
| Komponente | Stückpreis | Pro Board |
|------------|------------|-----------|
| STM32H723ZGT6 (LCSC) | $8.50 | $8.50 |
| W25Q128 Flash | $1.20 | $1.20 |
| TPS54331 x2 | $0.80 | $1.60 |
| 74LVC245 x5 | $0.25 | $1.25 |
| 74HC4052 | $0.20 | $0.20 |
| AP7361C LDO | $0.15 | $0.15 |
| USBLC6-2SC6 x2 | $0.35 | $0.70 |
| Crystals (25MHz + 32k) | $0.50 | $0.50 |
| Inductors x2 | $0.30 | $0.60 |
| Schottky x2 | $0.10 | $0.20 |
| LEDs x4 | $0.03 | $0.12 |
| Buttons x2 | $0.05 | $0.10 |
| Capacitors (~80 Stk) | $0.01 | $0.80 |
| Resistors (~50 Stk) | $0.01 | $0.50 |
| **SMD Subtotal** | | **$16.42** |

### THT Connectors (Separat bestellen)
| Komponente | Stückpreis | Quelle |
|------------|------------|--------|
| Hirose DF40C-100DP x2 | $3.50 | Mouser |
| 34-pin IDC Header x2 | $0.80 | LCSC |
| Apple 19-pin Header | $0.60 | LCSC |
| DB-23 Female | $2.50 | DigiKey |
| DIN-6 Female | $1.20 | DigiKey |
| RJ45 MagJack | $2.20 | LCSC |
| HDMI Type-A | $1.50 | LCSC |
| USB-C x2 | $0.40 | LCSC |
| M.2 Key M | $1.50 | LCSC |
| 40-pin Header | $0.50 | LCSC |
| DC Jack | $0.30 | LCSC |
| SWD 10-pin | $0.20 | LCSC |
| Sync 3-pin x2 | $0.10 | LCSC |
| **Connectors Subtotal** | | **$16.60** |

### Shipping (5 Boards)
| Route | Preis | Pro Stück |
|-------|-------|-----------|
| JLCPCB → DE (DHL) | $18 | $3.60 |
| Mouser → DE | $12 | $2.40 |
| **Shipping Subtotal** | **$30** | **$6.00** |

---

## EINZELPREIS (5er Serie)

```
┌─────────────────────────────────────────────────────────────┐
│       UFI HEADLESS - EINZELPREIS (5er SERIE)                │
│              110mm x 85mm - OHNE HDMI                       │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   PCB (4-Layer, ENIG)               $6.60                   │
│   SMT Assembly                      $8.50                   │
│   SMD Bauteile                     $15.00  (−$1.42 HDMI)   │
│   THT Connectors                   $14.50  (−$2.10 HDMI)   │
│   Shipping (anteilig)               $5.00                   │
│   ─────────────────────────────────────────                 │
│   EINZELPREIS (ohne CM5)          $49.60  (~€46)           │
│                                                             │
│   + Raspberry Pi CM5 4GB           $75.00  (~€70)           │
│   ─────────────────────────────────────────                 │
│   KOMPLETTPREIS (mit CM5)        $124.60  (~€116)          │
│                                                             │
│   ════════════════════════════════════════                  │
│   ERSPARNIS vs. Original: ~€8 pro Board                    │
│   ════════════════════════════════════════                  │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Szenario 2: Einzelstück (Prototyp)

Bei nur 1 Stück sind die Fixkosten höher:

| Posten | Preis |
|--------|-------|
| PCB (min. 5 Stk, nur 1 nutzen) | $50.00 |
| Assembly Setup | $15.00 |
| Assembly | $12.00 |
| SMD Bauteile | $18.00 |
| THT Connectors | $20.00 |
| Shipping | $25.00 |
| **EINZELSTÜCK (ohne CM5)** | **$140.00 (~€130)** |
| + CM5 4GB | $75.00 |
| **KOMPLETT** | **$215.00 (~€200)** |

---

## Szenario 3: Größere Serie (20+ Stück)

| Menge | PCB+Assy | Bauteile | Connectors | Pro Stück |
|-------|----------|----------|------------|-----------|
| 5 | $19.00 | $16.42 | $16.60 | ~$58 |
| 10 | $14.00 | $14.50 | $14.00 | ~$48 |
| 20 | $11.00 | $12.80 | $12.00 | ~$40 |
| 50 | $8.50 | $11.00 | $10.00 | ~$34 |
| 100 | $7.00 | $9.50 | $8.50 | ~$29 |

---

## Vergleich mit kommerziellen Lösungen

| Gerät | Preis | Features |
|-------|-------|----------|
| **UFI Mainboard** | **~€54** | CM5 Host + STM32 Flux + Multi-Drive |
| Greaseweazle V4.1 | €35 | RP2040, 1x FDD |
| KryoFlux | €100 | USB, 1x FDD, proprietär |
| FluxEngine | €25 | Cypress FX2, 1x FDD |
| SuperCard Pro | €100 | USB, 2x FDD |

**UFI Vorteile:**
- Integrierter CM5 Linux Host
- 2x Shugart + Apple II + Amiga + IEC
- GbE, HDMI, NVMe
- Erweiterbar (40-pin Module)
- Open Source

---

## Empfehlung

```
┌─────────────────────────────────────────────────────────────┐
│                    EMPFOHLENE BESTELLUNG                     │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   Für Prototyp/Entwicklung:                                 │
│   → 5 Boards bestellen = €270 gesamt                        │
│   → Einzelpreis: ~€54 (ohne CM5)                           │
│   → 4 Boards als Reserve/Tausch                            │
│                                                             │
│   Für Community/Verkauf:                                    │
│   → 20 Boards bestellen = ~€800 gesamt                      │
│   → Einzelpreis: ~€40 (ohne CM5)                           │
│   → Verkaufspreis: €80-100 (Board only)                    │
│   → Verkaufspreis: €150-180 (mit CM5)                      │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

---

## Kostenaufstellung für 1x Komplettsystem

```
┌─────────────────────────────────────────────────────────────┐
│              1x UFI KOMPLETTSYSTEM                          │
├─────────────────────────────────────────────────────────────┤
│                                                             │
│   UFI Mainboard (bestückt)           €54                    │
│   Raspberry Pi CM5 4GB               €70                    │
│   CM5 Kühlkörper                      €8                    │
│   Gehäuse (optional)                 €15                    │
│   Netzteil 12V/3A                    €10                    │
│   ─────────────────────────────────────────                 │
│   TOTAL                             €157                    │
│                                                             │
│   Optional:                                                 │
│   + NVMe SSD 256GB                   €25                    │
│   + GB/GBA Modul                     €15                    │
│   + MicroSD 32GB                      €8                    │
│   ─────────────────────────────────────────                 │
│   VOLL AUSGESTATTET                 €205                    │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```
