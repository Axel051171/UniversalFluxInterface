# JLC-Bestückungsviewer: Orientierung prüfen

Im JLC-Viewer (Schritt *Component Placement*) muss Pin 1 des Bauteilmodells dort liegen, wo er auf der Platine liegt (Draufsicht). Falls nicht: Rotation im Viewer korrigieren und den Offset unten in `scripts/jlc_cpl.py` nachtragen.

| Ref | Wert | Gehäuse | KiCad ° | Offset ° | JLC ° | Pin 1 auf der Platine |
|---|---|---|---|---|---|---|
| D1 | BZT52C10 | D_SOD-123 | 0 | +0 | 0 | links |
| D2 | SMF5.0CA | D_SMF | 0 | +0 | 0 | links |
| D3 | green | LED_0603_1608Metric | 180 | +0 | 180 | rechts |
| D4 | red | LED_0603_1608Metric | 180 | +0 | 180 | rechts |
| D5 | BZT52C10 | D_SOD-123 | 0 | +0 | 0 | links |
| D6 | SMAJ15CA | D_SMA | 0 | +0 | 0 | links |
| D7 | green | LED_0603_1608Metric | 180 | +0 | 180 | rechts |
| D8 | red | LED_0603_1608Metric | 180 | +0 | 180 | rechts |
| D9 | red | LED_0603_1608Metric | 180 | +0 | 180 | rechts |
| D10 | 1N4148W | D_SOD-123 | 0 | +0 | 0 | links |
| D11 | 1N4148W | D_SOD-123 | 0 | +0 | 0 | links |
| J1 | PSU_IN | PinHeader_1x04_P2.54mm_Vertical | 0 | +0 | 0 | oben |
| J2 | DRIVE | TerminalBlock_MaiXu_MX126-5.0-03P_1x03_P5.00mm | 90 | +0 | 90 | unten |
| J3 | UFI_LINK | PinHeader_1x04_P2.54mm_Vertical | 90 | +0 | 90 | links |
| J4 | EXT_LED | PinHeader_2x05_P2.54mm_Vertical | 90 | +0 | 90 | unten links |
| Q1 | AO3401A | SOT-23 | 180 | +180 | 0 | unten rechts |
| Q2 | 2N7002 | SOT-23 | 0 | +180 | 180 | oben links |
| Q3 | AO3401A | SOT-23 | 180 | +180 | 0 | unten rechts |
| Q4 | 2N7002 | SOT-23 | 0 | +180 | 180 | oben links |
| Q5 | 2N7002 | SOT-23 | 0 | +180 | 180 | oben links |
| Q6 | 2N7002 | SOT-23 | 0 | +180 | 180 | oben links |
| U1 | TPS25921ADR | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |
| U2 | TPS25921ADR | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |

Dioden/LEDs: Pin 1 = Kathode. SOT-23: Pin 1 = Gate. SOIC-8 U1/U2: Pin 1 = GND (Markierung). Stiftleisten J1/J3: Pin 1 = eckiges Pad.
