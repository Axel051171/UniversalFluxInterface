# Timer- und Pin-Zuteilung UFI Headless v1.0 (Stand 24.10.2026, Stufe A11e)

Verbindliche Tabelle für alle Stufen, die MCU-Pins belegen (A11a–A11d, K1). Jede neue
Belegung wird hier eingetragen, bevor sie in Schaltplan oder Firmware landet. Alternativ-
funktionen und ADC-Kanäle sind gegen die STM32H723ZG-Pin-Datenbank geprüft
(`stm32-data-generated/data/chips/STM32H723ZG.json`), die Timerbasis gegen `ufi_clock.c`
(SYSCLK 550 MHz, HCLK 275 MHz, APB1/APB2 137,5 MHz → Timertakt 275 MHz, `FLUX_TIMER_FREQ`).

## 1 Timer

| Timer | Breite / Takt | Kanal | Pin (AF) | Signal | Richtung | Stufe / Quelle |
|---|---|---|---|---|---|---|
| TIM2 | 32 bit, 275 MHz | CH1 | PA5 (AF1) | FDD_RDATA | Input Capture, DMA | v0.1, `ufi_flux.c` |
| TIM2 | | CH2 | PA1 (AF1) | FDD_INDEX | Input Capture (Umdrehungen, Schreibstart) | v0.1 |
| TIM2 | | CH3 | PA2 (AF1) | APL_RDDATA (Disk II / Mac RD) | Input Capture, gleiche Zeitbasis wie RDATA | v0.7 |
| TIM2 | | CH4 | PB11 (AF1) | FDD_INDEX (2) | Input Capture; macht CH2 für die RDATA-Pulsbreite frei | A9b, Capture nutzt weiter CH2 |
| TIM2 | | OC4 (intern) | – | Schreibstart-Versatz (`start_offset_ticks`) | Vergleich | 1.18 |
| TIM3 | 16 bit, 275 MHz | CH1 | PA6 (AF2) | FDD_WDATA | Output, DMA-Bitzellen | v0.1, `ufi_write.c` |
| TIM3 | | CH2 | PA7 (AF2) | APL_WRDATA (Disk II / Mac WR) | Output Toggle | v0.7 |
| TIM7 | Basistimer | – | – | Flux-ISR-Takt (Indexsimulation intern, `dma_pos()`) | – | 1.13, `ufi_flux.c` |
| TIM15 | 16 bit, 275 MHz | CH1 | PE5 (AF4) | INDEX_SIM | Output (A11b: über eigenen MOSFET auf den INDEX-Bus) | v1.0 |
| **TIM8** | 16 bit, 275 MHz | CH1 | PC6 (AF3) | SYNC (J19 Pin 3, Sync-Sensor) | Input Capture, eigener Zeitstempel | **A11d (M5), geplant** |
| **TIM1** | 16 bit, 275 MHz | CH1 | PA8 (AF1) | Mac-PWM (K1 Pin 20, nur 400K-Laufwerk, über Lötjumper) | PWM-Ausgang | **A11d (K1), geplant** |
| TIM4, TIM5, TIM12–14, TIM16, TIM17, TIM23, TIM24 | | | | frei (TIM5 teilt PA0–PA3 mit TIM2, daher nicht nutzbar) | | |

Betriebsarten: Lesen = TIM2 CH1 + CH2 (CH4 parallel); Schreiben = TIM3 CH1 + TIM2 CH2 (Index), optional OC4;
Apple/Mac = TIM2 CH3 + TIM3 CH2 + TIM2 CH2/SYNC; Selbsttest `index_loop` = TIM15 CH1 → INDEX-Bus → TIM2 CH2.
TIM2 ist damit voll belegt; SYNC bekommt deshalb TIM8 (gleicher 275-MHz-Takt, 16 bit, Zeitstempel
per Überlaufzähler), nicht einen TIM2-Kanal.

## 2 ADC

| Kanal | Pin | Signal | Stufe |
|---|---|---|---|
| ADC1 INP10 | PC0 | I_FDD5 (INA180, 50 mΩ → 1 mV/mA) | v0.5 / A9a |
| ADC1 INP11 | PC1 | I_FDD12 | v0.5 / A9a |
| ADC1 INP18 | PA4 | BOARD_ID (2,27 V = v1.0) | v0.5 |
| **ADC1 INP4** | PC4 | FDD_5V über 10 k/10 k (Grenze 4,5 V) | **A11d (S4), geplant** |
| **ADC1 INP8** | PC5 | FDD_12V über 33 k/10 k (Grenze 10,8 V) | **A11d (S4), geplant** |

## 3 GPIO-Reservierungen der Stufe A11 (freie Pins vor A11c: 20)

| Pin | Signal | Richtung | Stufe | Bemerkung |
|---|---|---|---|---|
| PF9 | FLT5 | in, Pull-up | A11c, **belegt** | eFuse U28 FLT |
| PF10 | FLT12 | in, Pull-up | A11c, **belegt** | eFuse U29 FLT |
| PC6 | SYNC | TIM8_CH1 (AF3) | A11d | Sync-Sensor über U26 (74LVC14) |
| PD14 | J6 Pin 2 Eingang | in | A11d (S1) | über U26, Pin 2 bleibt zugleich DENSITY-Ausgang (MOSFET) |
| PD15 | J6 Pin 4 Eingang | in | A11d (S2) | über U26 |
| PF14 | J6 Pin 4 Ausgang | out | A11d (S2) | 2N7002DW, Gate-Pull-down |
| PB5 | J6 Pin 33 (Flippy) | out | A11d (S3) | über 2,4 k, 2,4 k nach GND, Lötjumper GND/Signal |
| PC4 | FDD_5V-Messung | ADC1 INP4 | A11d (S4) | |
| PC5 | FDD_12V-Messung | ADC1 INP8 | A11d (S4) | |
| PB0 | Mac SEL (K1 Pin 12) | out | A11d (K1) | über U27 (AHCT244, FDD_5V) |
| PA8 | Mac PWM (K1 Pin 20) | TIM1_CH1 (AF1) | A11d (K1) | über U27 und Lötjumper (offen) |
| PB1 | Mac !CSTOUT (K1 Pin 20) | in | A11d (K1) | Schmitt-Eingang an U26, immer verbunden (Owner 24.10.) |

Frei nach A11: PC13 (nur RTC/Tamper, 3 mA), PC2_C, PC3_C (ADC3, Direktpins), PB4 (NJTRST), PC7, PC8,
PC9 (TIM3/TIM8 CH2–CH4, SDMMC1), PA10 (TIM1_CH3, USART1_RX) = 8 Pins.

## 4 Bereits belegte Pins (Referenz)

Die vollständige Belegung steht in `kicad/UFI_Headless/README.md`, Abschnitt „GPIO-Belegung“;
Firmware-Quelle ist `firmware/include/board.h` / `firmware/src/board_headless.c`. Änderungen an
dieser Tabelle und an `board.h` gehören in denselben Commit (Skill `pcb-revision-stufen`).
