# JLC-Bestückungsviewer: Orientierung prüfen

Im JLC-Viewer (Schritt *Component Placement*) muss Pin 1 des Bauteilmodells dort liegen, wo er auf der Platine liegt (Draufsicht). Falls nicht: Rotation im Viewer korrigieren und den Offset unten in `scripts/jlc_cpl.py` nachtragen.

| Ref | Wert | Gehäuse | KiCad ° | Offset ° | JLC ° | Pin 1 auf der Platine |
|---|---|---|---|---|---|---|
| BT1 | CR2032 | BatteryHolder_MYOUNG_BS-07-A1BJ001_CR2032 | 0 | +0 | 0 | links |
| C1 | 100uF/25V | CP_Elec_8x10.5 | 0 | +180 | 180 | links |
| C13 | 100uF/25V | CP_Elec_8x10.5 | 0 | +180 | 180 | links |
| D1 | SMF5.0CA | D_SMF | 90 | +0 | 90 | unten |
| D2 | SS54 | D_SMC | 0 | +0 | 0 | links |
| D3 | SMAJ13A | D_SMA | 0 | +0 | 0 | links |
| D4 | green | LED_0603_1608Metric | 0 | +0 | 0 | links |
| D9 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D10 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D11 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D12 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D13 | SMF5.0CA | D_SMF | 90 | +0 | 90 | unten |
| D14 | SMAJ15CA | D_SMA | 90 | +0 | 90 | unten |
| D15 | ESDA6V1-5SC6 | SOT-23-6 | 90 | +180 | 270 | unten links |
| D16 | ESDA6V1-5SC6 | SOT-23-6 | 90 | +180 | 270 | unten links |
| D17 | SMF5.0CA | D_SMF | 0 | +0 | 0 | links |
| D18 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D19 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D20 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D21 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D22 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D23 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D24 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D25 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D26 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D27 | SS54 | D_SMC | 0 | +0 | 0 | links |
| D28 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D29 | PESD3V3L1BA | D_SOD-323 | 0 | +0 | 0 | links |
| D31 | ESDA6V1-5SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| D32 | PESD3V3L1BA | D_SOD-323 | 90 | +0 | 90 | unten |
| D33 | PESD3V3L1BA | D_SOD-323 | 90 | +0 | 90 | unten |
| D34 | PESD3V3L1BA | D_SOD-323 | 90 | +0 | 90 | unten |
| J1 | USB-C | USB_C_Receptacle_HRO_TYPE-C-31-M-12 | 180 | +0 | 180 | unten rechts |
| J2 | 12V DC 5.5/2.1 | BarrelJack_Horizontal | -90 | +0 | 270 | unten rechts |
| J3 | FDD_PWR | JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical | -90 | +0 | 270 | oben rechts |
| J4 | SWD | PinHeader_2x05_P1.27mm_Vertical | 0 | +0 | 0 | oben links |
| J5 | DBG_UART | PinHeader_1x03_P2.54mm_Vertical | 0 | +0 | 0 | oben |
| J6 | FDD_34PIN | IDC-Header_2x17_P2.54mm_Vertical | 90 | +0 | 90 | unten links |
| J7 | AMIGA_FDD | IDC-Header_2x12_P2.54mm_Vertical | 90 | +0 | 90 | unten links |
| J8 | IEC_HDR | PinHeader_1x06_P2.54mm_Vertical | 0 | +0 | 0 | oben |
| J12 | FRONT | IDC-Header_2x08_P2.54mm_Vertical | 0 | +0 | 0 | oben links |
| J14 | DISK_II | IDC-Header_2x10_P2.54mm_Vertical_UFI | 90 | +0 | 90 | unten links |
| J16 | USB_HDR | PinHeader_2x05_P2.54mm_Vertical | 90 | +0 | 90 | unten links |
| J17 | PSU_IN | JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical | -90 | +0 | 270 | oben rechts |
| J19 | SYNC_SENSOR | JST_XH_B3B-XH-A_1x03_P2.50mm_Vertical | -90 | +0 | 270 | oben rechts |
| J20 | GUARD | JST_XH_B4B-XH-A_1x04_P2.50mm_Vertical | -90 | +0 | 270 | oben rechts |
| J21 | LA | PinHeader_2x10_P2.54mm_Vertical | 0 | +0 | 0 | oben links |
| J22 | HR911105A | RJ45_Hanrun_HR911105A_Horizontal | 180 | +0 | 180 | unten rechts |
| J23 | MAC_3.5_PORT | IDC-Header_2x10_P2.54mm_Vertical_UFI | 90 | +0 | 90 | unten links |
| Q5 | 2N7002 | SOT-23 | 0 | +180 | 180 | oben links |
| Q6 | BSS138DW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| Q7 | BSS138DW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| Q8 | BSS138DW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| Q9 | BSS138DW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| Q10 | BSS138DW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| Q11 | BSS138DW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| Q12 | BSS138DW | SOT-363_SC-70-6 | 180 | +0 | 180 | unten rechts |
| U1 | USBLC6-2SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| U2 | TPS54202DDC | SOT-23-6 | 180 | +180 | 0 | unten rechts |
| U3 | TPS2116DRL | SOT-583-8 | 0 | +0 | 0 | oben links |
| U4 | AP7361C-33E | SOT-223-3_TabPin2 | 0 | +180 | 180 | oben links |
| U5 | STM32H723ZGT6 | LQFP-144_20x20mm_P0.5mm | 0 | +270 | 270 | oben links |
| U8 | 74LVC14AD | SOIC-14_3.9x8.7mm_P1.27mm | 90 | +270 | 0 | unten links |
| U9 | SN74LS07D | SOIC-14_3.9x8.7mm_P1.27mm | 0 | +270 | 270 | oben links |
| U10 | 74LVC14AD | SOIC-14_3.9x8.7mm_P1.27mm | 0 | +270 | 270 | oben links |
| U11 | APS6404L-3SQR-SN | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |
| U12 | INA180A1 | SOT-23-5 | 0 | +180 | 180 | oben links |
| U13 | INA180A1 | SOT-23-5 | 0 | +180 | 180 | oben links |
| U14 | 74LVC1G32 | SOT-353_SC-70-5 | 90 | +180 | 270 | unten links |
| U15 | CSNP32GCR01-AOW | SD_NAND_LGA-8_6x8mm_P1.27mm | 0 | +0 | 0 | oben links |
| U16 | SN74AHCT244PWR | TSSOP-20_4.4x6.5mm_P0.65mm | 0 | +270 | 270 | oben links |
| U17 | 74LVC2G17GW | SOT-363_SC-70-6 | 0 | +0 | 0 | oben links |
| U18 | ICL7662EBA+T | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |
| U19 | TPS2116DRL | SOT-583-8 | 0 | +0 | 0 | oben links |
| U22 | TS5A3166DBVR | SOT-23-5 | 0 | +180 | 180 | oben links |
| U23 | W5500 | LQFP-48_7x7mm_P0.5mm | -90 | +270 | 180 | oben rechts |
| U24 | USBLC6-2SC6 | SOT-23-6 | 0 | +180 | 180 | oben links |
| U25 | SHT40-AD1B | Sensirion_DFN-4_1.5x1.5mm_P0.8mm_SHT4x_NoCentralPad | 180 | +0 | 180 | unten rechts |
| U26 | 74LVC14AD | SOIC-14_3.9x8.7mm_P1.27mm | 90 | +270 | 0 | unten links |
| U27 | SN74AHCT244PWR | TSSOP-20_4.4x6.5mm_P0.65mm | 0 | +270 | 270 | oben links |
| U28 | TPS25921A | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |
| U29 | TPS25921A | SOIC-8_3.9x4.9mm_P1.27mm | 0 | +270 | 270 | oben links |
| U30 | 74LVC2G32 | TSSOP-8_3x3mm_P0.65mm | 0 | +270 | 270 | oben links |
| U31 | 74LVC1G02 | SOT-353_SC-70-5 | 0 | +180 | 180 | oben links |
| X1 | 25MHz TCXO HCMOS | Oscillator_SMD_Abracon_ASE-4Pin_3.2x2.5mm | 0 | +0 | 0 | unten links |
| Y2 | 25MHz CL=12pF | Crystal_SMD_3225-4Pin_3.2x2.5mm | 0 | +0 | 0 | unten links |
| Y3 | 32.768kHz CL=7pF | Crystal_SMD_3215-2Pin_3.2x1.5mm | 90 | +0 | 90 | oben |

Dioden/LEDs: Pin 1 = Kathode. Elkos C1/C13: Pin 1 = Plus. USB-C J1 und RJ45 J22: Öffnung zur Platinenkante. BT1: Pad 1 = Plus (Halterkontakt). Q6–Q12 BSS138DW SOT-363: Pin 1 = S2 (Punkt im Druck), kein Offset hinterlegt. JST-XH J3/J17/J19/J20: Pin 1 an der Markierung; IDC J6/J7/J12/J14/J23: Kerbe der Wanne zum Pin-1-Dreieck.
