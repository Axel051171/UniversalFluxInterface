# JLC-Bestückungsviewer: Orientierung prüfen

Im JLC-Viewer (Schritt *Component Placement*) muss Pin 1 des Bauteilmodells dort liegen, wo er auf der Platine liegt (Draufsicht). Falls nicht: Rotation im Viewer korrigieren und den Offset unten in `scripts/jlc_cpl.py` nachtragen.

| Ref | Wert | Gehäuse | KiCad ° | Offset ° | JLC ° | Pin 1 auf der Platine |
|---|---|---|---|---|---|---|
| C1 | 100uF/25V | CP_Elec_8x10.5 | 0 | +180 | 180 | links |
| C13 | 100uF/25V | CP_Elec_8x10.5 | 0 | +180 | 180 | links |
| D1 | SMF5.0CA | D_SMF | 90 | +0 | 90 | unten |
| D2 | SS54 | D_SMC | 0 | +0 | 0 | links |
| D3 | SMAJ15CA | D_SMA | 90 | +0 | 90 | unten |
| D4 | green | LED_0603_1608Metric | 0 | +0 | 0 | links |
| D5 | green | LED_0603_1608Metric | 0 | +0 | 0 | links |
| D6 | green | LED_0603_1608Metric | 0 | +0 | 0 | links |
| D7 | red | LED_0603_1608Metric | 0 | +0 | 0 | links |
| D8 | green PWR | LED_0603_1608Metric | 0 | +0 | 0 | links |
| D9 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D10 | ESDA6V1-5SC6 | SOT-23-6 | 90 | +180 | 270 | unten links |
| D11 | ESDA6V1-5SC6 | SOT-23-6 | 90 | +180 | 270 | unten links |
| D12 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D13 | SMF5.0CA | D_SMF | 90 | +0 | 90 | unten |
| D14 | SMAJ15CA | D_SMA | 0 | +0 | 0 | links |
| J1 | USB-C | USB_C_Receptacle_HRO_TYPE-C-31-M-12 | 0 | +0 | 0 | oben links |
| U1 | USBLC6-2SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| U2 | TPS54202DDC | SOT-23-6 | 180 | +180 | 0 | unten rechts |
| U3 | TPS2116DRL | SOT-583-8 | 0 | +0 | 0 | oben links |
| U4 | AP7361C-33E | SOT-223-3_TabPin2 | 0 | +180 | 180 | oben links |
| U5 | STM32H723ZGT6 | LQFP-144_20x20mm_P0.5mm | 0 | +270 | 270 | oben links |
| U6 | SN74LS07D | SOIC-14_3.9x8.7mm_P1.27mm | 90 | +270 | 0 | unten links |
| U7 | SN74LS07D | SOIC-14_3.9x8.7mm_P1.27mm | 90 | +270 | 0 | unten links |
| U8 | 74LVC14AD | SOIC-14_3.9x8.7mm_P1.27mm | 90 | +270 | 0 | unten links |
| U9 | SN74LS07D | SOIC-14_3.9x8.7mm_P1.27mm | 0 | +270 | 270 | oben links |
| U10 | 74LVC14AD | SOIC-14_3.9x8.7mm_P1.27mm | 0 | +270 | 270 | oben links |
| U11 | APS6404L-3SQR-SN | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |
| U12 | INA180A1 | SOT-23-5 | 0 | +180 | 180 | oben links |
| U13 | INA180A1 | SOT-23-5 | 0 | +180 | 180 | oben links |
| U14 | 74LVC1G32GW | SOT-353_SC-70-5 | 90 | +180 | 270 | unten links |
| U15 | CSNP32GCR01-AOW | SD_NAND_LGA-8_6x8mm_P1.27mm | 0 | +0 | 0 | oben links |
| Y1 | 25MHz CL=12pF | Crystal_SMD_3225-4Pin_3.2x2.5mm | -90 | +0 | 270 | oben links |

Dioden/LEDs: Pin 1 = Kathode. Elkos C1/C13: Pin 1 = Plus. USB-C J1: Kontakte zur Platinenkante.
