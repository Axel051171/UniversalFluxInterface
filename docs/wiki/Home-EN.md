# Universal Flux Interface

**Sprache / Language:** [Deutsch](Home) | [English](Home-EN)

UFI reads and writes floppy disks at the level of magnetic flux transitions. The Headless board uses an STM32H723 and does not need a Raspberry Pi. [UnifiedFloppyTool](UnifiedFloppyTool-EN) provides a graphical PC application with native UFI support; the Python `ufi` tool handles commands, scripts, analysis and disk images. Other modes provide Greaseweazle compatibility, USB mass storage and captures to internal storage.

**Documentation dated 14 November 2026, board v1.0, firmware 1.18-dev.** One v1.0 board has passed USB bring-up. Reading and writing real drives, and LAN operation with a cable, remain open in the documented test record. [Project status](Project-Status) explains the evidence.

## Get started

| Your task | Guide |
|---|---|
| Connect an existing board for the first time | [Quickstart](Quickstart) |
| Install the PC tool | [Installation](Installation-EN) |
| Use the graphical application | [UnifiedFloppyTool with UFI](UnifiedFloppyTool-EN) |
| Develop a custom host application | [USB protocol for developers](USB-Protocol) and [reference guide](USB-Protocol-Reference) |
| Identify the board and connectors | [Hardware](Hardware-EN) and [Connections](Connections) |
| Find components in the board images | [Annotated board overview](Board-Overview) |
| Test a newly assembled board | [Bring-up](Bring-up) |
| Preserve a disk | [Reading and archiving](Reading-and-Archiving) |
| Write an image back to disk | [Writing and verification](Writing-and-Verification) |
| Investigate a fault | [Troubleshooting](Troubleshooting) |
| Compare UFI with other flux systems | [Benefits and comparison](Benefits-and-Comparison) |

## What UFI offers

UFI combines several drive interfaces, internal capture storage, button control, LAN and monitored drive power. Its native 275 MHz timebase and open hardware/software sources support analysis and custom development. [Benefits and comparison](Benefits-and-Comparison) discusses KryoFlux, Greaseweazle, SuperCard Pro and Applesauce+, with sources and qualification status.

## Topics

- **Capture and analysis:** [File formats](File-Formats), [Analysis and data quality](Analysis-and-Data-Quality), [Drives](Drives).
- **Operation:** [Operating modes](Operating-Modes), [SD and standalone](SD-and-Standalone), [LAN](LAN-EN), [Greaseweazle](Greaseweazle-EN).
- **Computer families:** [Amiga](Amiga-EN), [Apple and Macintosh](Apple-and-Macintosh), [Commodore IEC](Commodore-IEC-EN).
- **Development:** [Firmware](Firmware-EN), [Development and tests](Development-and-Tests), [Wiki maintenance](Wiki-Maintenance).
- **Reference:** [FAQ](FAQ-EN), [Glossary](Glossary), [Project status](Project-Status).

## Specifications

| Feature | Implementation |
|---|---|
| Processor | STM32H723ZGT6, Cortex-M7, 550 MHz |
| Flux timebase | 275 MHz; nominal timer step approximately 3.64 ns |
| Flux buffer | 8 MB QSPI PSRAM; internal fallback buffer on failure |
| USB | USB-C with a Full-Speed PHY |
| Network on v1.0 | W5500; UFI v2 session over TCP |
| Internal storage | SD NAND for images, configuration and reports |
| Drive families | PC/Shugart, Amiga, Apple Disk II, Macintosh, and Commodore over IEC |

Timer resolution is not measured system accuracy. Suitability for a particular drive and disk depends on connections, settings and the corresponding test results.

## Project references

[Source code](https://github.com/Axel051171/UniversalFluxInterface) · [Report a problem](https://github.com/Axel051171/UniversalFluxInterface/issues) · [Authoritative protocol reference, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md) · [MIT license](https://github.com/Axel051171/UniversalFluxInterface/blob/main/LICENSE)
