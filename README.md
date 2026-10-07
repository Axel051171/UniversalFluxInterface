# UFI – Universal Flux Interface (Headless)

Flux-Lese-/Schreibgerät für Disketten: STM32H723 (550 MHz) misst jede Flusswechsel-Zeit mit
275 MHz (3,6 ns) und streamt sie per USB an den PC. Kein Raspberry Pi, kein Betriebssystem auf
dem Gerät – das frühere CM5-/Modul-Konzept ist aufgegeben (steht in der Git-Historie).

| | |
|---|---|
| MCU | STM32H723ZGT6, 550 MHz |
| Auflösung | 275 MHz Timer-Capture (3,6 ns), Index auf derselben Zeitbasis |
| Puffer | 8 MB QSPI-PSRAM (Ringpuffer beim Streamen, bis 200 Umdrehungen) |
| USB | USB-C, Full Speed, Datenstrom ~2 Byte je Flusswechsel, sendet während die Diskette dreht |
| Laufwerke | 34-pol PC/Shugart (DENSITY, DRATE, bis 4 Laufwerke), Amiga DB23 (Header, 2 Laufwerke), Apple Disk II (20-pol, 2 Laufwerke), Commodore IEC (1541) |
| Schreiben | Timer + DMA, Precompensation, Verify, WRITE-LOCK-Jumper |
| Platine v0.7 | 110 × 97 mm, 4 Lagen, geschaltete und gemessene Laufwerksversorgung, 4 GB SD-NAND fest verbaut, Erweiterungsheader, Apple-Disk-II-Port, zweites Amiga-Laufwerk (JP3) |
| Ohne PC | Taster: ganze Diskette als SCP auf den internen Speicher (mit Qualitätsbericht) oder Disk-zu-Disk-Kopie, danach als USB-Laufwerk am PC abholen |
| Greaseweazle-kompatibel | `UFI.CFG protocol=gw`: die Greaseweazle-Werkzeuge (`gw read`, `gw write`, …) erkennen das Board direkt |
| USB-Floppy | Betriebsart-Schalter: PC-Disketten (360K–2,88 MB, DMF) erscheinen am PC als normales USB-Diskettenlaufwerk, ohne Treiber; Atari ST, Commodore 1581 und Amiga 880K als Block-Laufwerk (Sektorabbild wie ST/D81/ADF) |

## Aufbau des Repos

| Ordner | Inhalt |
|---|---|
| [`kicad/UFI_Headless/`](kicad/UFI_Headless/README.md) | Schaltplan, Layout, Fertigungsdaten (`fertigung/`), Build- und ECO-Skripte |
| [`firmware/`](firmware/) | STM32H723-Firmware (CMake, STM32Cube HAL) |
| [`software/ufi_host/`](software/ufi_host/README.md) | Python-Host (`ufi`): Lesen/Schreiben, SCP-Export, Protokollbeschreibung |
| [`docs/`](docs/) | Amiga-DB23-Adapterkabel |

## Firmware bauen

```bash
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build        # -> ufi_firmware.bin
```

Erwartet arm-none-eabi-gcc und STM32Cube H7 V1.11.0 unter `~/STM32Cube/Repository/STM32Cube_FW_H7_V1.11.0`
(Pfad in `firmware/CMakeLists.txt`).

Flashen per SWD oder USB-DFU (Taster A beim Einschalten halten bzw. `ufi bootloader`).

## Status

Platine v0.6 ist fertig für die Bestellung (ERC/DRC sauber, JLC-Daten in `kicad/UFI_Headless/fertigung/`) – [Bestell-Anleitung JLC](kicad/UFI_Headless/docs/Bestellung_JLC.md).
Hardware und Firmware sind noch nicht am echten Gerät getestet – Ablauf für den Prototyp: [Inbetriebnahme-Checkliste](kicad/UFI_Headless/docs/Inbetriebnahme.md).

Lizenz: siehe [LICENSE](LICENSE).
