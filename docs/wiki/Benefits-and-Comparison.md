# Benefits and comparison

**Sprache / Language:** [Deutsch](Vorteile-und-Vergleich) | [English](Benefits-and-Comparison)

**UFI combines flux archiving, several drive families, standalone operation and diagnostics on one board.** This is particularly useful at a workstation handling PC, Amiga, Apple, Macintosh and Commodore media: connectors, power monitoring, storage and controls belong to the same setup.

Comparison date: **14 November 2026**, UFI Headless v1.0 with firmware 1.18-dev. This comparison covers documented equipment and functions. UFI passed the described USB bring-up with PSRAM; drive operation and wired LAN still need qualification according to [project status](Project-Status). No direct laboratory comparison between these devices is available.

## What UFI offers

| UFI equipment or function | Workflow benefit | Evidence and conditions |
|---|---|---|
| Dedicated connectors for PC/Shugart, Amiga, Disk II, Mac and IEC | Different computer families can share one base board; fewer additional active interface boards may be needed | Connectors exist and drivers are implemented; cables and power must match each drive |
| Standalone dumps to internal SD NAND, buttons and indicators | Captures can run without an active PC application and be retrieved later | Firmware exists; real-drive workflow still needs testing |
| W5500 LAN alongside USB | An enclosed or remote device can be operated across the local network | Hardware, TCP and discovery firmware exist; LAN hardware test outstanding |
| eFuses, current measurement and drive voltage feedback | More targeted diagnosis of power and load faults; controllable supply rails | Circuits exist; trip thresholds and behavior under drive load still need measurement |
| Front switch, hardware gating and per-job firmware permission | Visible write enable at the enclosure and an additional lock outside write jobs | Present on v1.0; physical write-path qualification outstanding |
| 8 MB PSRAM and streaming ring | Buffer reserve for temporary host delays and longer captures | PSRAM used during USB bring-up; sustained real-flux load still needs testing |
| Native 275 MHz timebase and original-tick archive | Fine quantization for timing analysis; captures can be retained before export conversion | Timer/archive representation implemented; actual measurement accuracy needs separate assessment |
| UFI, Greaseweazle-compatible mode, composite and USB mass storage | Several workflows can use the same hardware | Protocols and modes implemented; capabilities depend on mode and drive |
| Open KiCad, firmware and host sources | Behavior can be inspected and adapted | Repository sources under the MIT license; other open projects also offer this benefit |

Examples and details: [connections](Connections), [SD and standalone](SD-and-Standalone), [LAN](LAN-EN), [write permissions](Writing-and-Verification), [file formats](File-Formats). UFI references: [board pins](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/board.h), [power control](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_board.c), [firmware timebase](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_firmware.h), [license](https://github.com/Axel051171/UniversalFluxInterface/blob/main/LICENSE).

## Compared with KryoFlux

KryoFlux offers flux capture, STREAM raw data, many decoded export formats and writing, including ADF, G64 and IPF workflows. Its documentation also describes parallel exports and a graphical interface. [KryoFlux features](https://www.kryoflux.com/?page=kf_features).

Its documented controller connects drives to a PC through a 34-pin Shugart connector. Two drives can share the cable, with access to one at a time. USB is High Speed, and host software is offered for Windows, macOS and Linux. [Technical overview](https://www.kryoflux.com/?page=kf_tech).

**UFI's additional benefit:** separate native connectors for further drive families, integrated IEC access, internal capture storage without an active PC application, LAN and detailed board diagnostics. This combination can simplify a mixed-media workstation. It does not yet demonstrate higher recovery rates or better copy-protection reproduction than KryoFlux, nor does it promise direct UFI support for every KryoFlux/IPF workflow.

## Compared with Greaseweazle V4.1

Greaseweazle is also open and already provides a physical write-enable jumper. Its model overview lists multiple-drive operation, buffered outputs and disk-change detection. Certain F7 models offer High-Speed USB and direct 12 V power; these capabilities must not be ruled out for all Greaseweazle models. [Model comparison](https://github.com/keirf/greaseweazle/wiki/Greaseweazle-Models).

V4.1 uses USB-C and a 34-pin drive connector. Suitable Amiga/Atari drives can also use this bus. Project documentation explains that the suitability of PC drives for GCR media varies. [V4.1 setup](https://github.com/keirf/greaseweazle/wiki/V4.1-Setup).

**UFI's additional benefit:** dedicated Disk II, Mac, Amiga and IEC connectors, internal capture storage, button operation, LAN and monitored drive power. The benefit is broader integration of the setup. Openness, USB-C, multiple-drive connections and physical write protection are not unique to UFI.

UFI additionally includes a Greaseweazle-compatible mode, intended to retain access to existing host software. Each combination of tool version, drive and function needs testing. See [Greaseweazle](Greaseweazle-EN).

## Compared with SuperCard Pro

SuperCard Pro offers flux reading/writing, the widely used SCP format and a published hardware SDK/format specification. The manufacturer lists 512 KB SRAM, a 34-pin connector, Full-Speed USB, serial interfaces and a microSD slot. Serial control instead of USB is also described. The product page treats a portable copying station as an extension. [SuperCard Pro product description](https://www.cbmstuff.com/index.php?route=product/product&product_id=52).

**UFI's additional benefit:** larger nominal flux memory, directly integrated button/SD dump firmware, LAN, additional connector families and combined board/power diagnostics. These functions are intended to work together in an enclosure. A card slot or serial control alone does not establish an autonomous workflow identical to UFI's.

SCP remains an interchange format for UFI as well. Retain the UFI archive for full native time resolution; normal UFI SCP export converts to 25 ns units.

## Compared with Applesauce and Applesauce+

A current comparison must include **Applesauce+**. It already has multiple PC/Apple connectors, two sync-sensor inputs, buffered capture and High-Speed USB. Its manufacturer specifies up to eight connected drives, native 576 MHz sampling and selectable flux output up to 96 MHz. Older Applesauce hardware is described as supporting output up to 24 MHz with Client 2.0. [Applesauce and Applesauce+](https://applesaucefdc.com/).

**UFI's focus:** a shared open firmware/hardware design, IEC connectivity, internal standalone captures and LAN access in the same device. Multiple ports and sync sensors are not unique advantages over Applesauce+. UFI's timer clock also does not establish generally better measurement performance than Applesauce+.

## Where FluxEngine fits

FluxEngine names both its own hardware and cross-platform software for processing flux and sector images. Its client additionally supports Greaseweazle and some Applesauce functionality. [FluxEngine project](https://github.com/davidgiven/fluxengine/blob/master/README.md).

**UFI's benefit over a basic USB flux setup** is its integrated interfaces, standalone storage, LAN and device diagnostics. Decoder and filesystem features require a separate comparison from hardware. GW mode does not automatically make UFI a fully tested FluxEngine platform for every version; VID/PID handling and supported firmware revisions can matter. [FluxEngine with Greaseweazle-compatible hardware](https://github.com/davidgiven/fluxengine/blob/master/doc/greaseweazle.md).

## Comparing time resolution correctly

The values below are **nominal time steps** calculated from documented clocks, not measured accuracy or recovery rates.

| Device or data path | Documented clock / grid | Nominal time step | Interpretation |
|---|---|---|---|
| UFI native capture/archive path | 275 MHz | about 3.64 ns | Timer timebase in current UFI code |
| KryoFlux STREAM defaults | about 24.027 MHz sample clock | about 41.62 ns | Protocol documentation default; use the actual clock reported by the device |
| Greaseweazle firmware | 72 MHz | about 13.89 ns | `SAMPLE_MHZ` in the examined production code |
| SuperCard Pro | 25 ns capture | 25 ns | Manufacturer's capture/format specification |
| Applesauce+ flux output | up to 96 MHz | about 10.42 ns | Output resolution; manufacturer specifies native 576 MHz sampling, about 1.74 ns |
| UFI using the GW protocol | 68.75 MHz | about 14.55 ns | Protocol conversion from the 275 MHz timer |
| Normal UFI SCP export | 25 ns grid | 25 ns | File conversion; original tick resolution is retained only in the native path |

Sources: [UFI constants](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_firmware.h), [KryoFlux STREAM Rev. 1.1, page 4](https://www.kryoflux.com/download/kryoflux_stream_protocol_rev1.1.pdf), [Greaseweazle `floppy.c`](https://github.com/keirf/greaseweazle-firmware/blob/master/src/floppy.c), [SCP specification](https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt), [Applesauce+ announcement](https://applesaucefdc.com/2025/06/10/introducing-the-applesauce/), [UFI SCP writer](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/scp.py).

UFI's finer timer grid can help with timing analysis. The differences that can actually be evaluated also depend on input circuitry, filtering, clock stability, the drive and the medium. Finer quantization must therefore not be presented as proportionally better accuracy, more recovered data or faster capture.

## Particularly useful applications

[UnifiedFloppyTool](UnifiedFloppyTool-EN) additionally provides a graphical PC integration in the development sources: native UFI connectivity, diagnostics, capture, write verification and IEC tools complement the board. This benefits the combined workflow. UFT hardware qualification remains outstanding; a graphical interface alone is not unique among these systems.

- **Mixed collections:** handle several drive families using the same base board and documented connector assignments.
- **Enclosures and copying stations:** combine front controls, local storage and later image retrieval.
- **Experiments and diagnostics:** evaluate current, voltage, buffer status, media changes and capture findings together.
- **Custom development:** inspect and adapt schematics, firmware and host tools for specific drives or workflows.

Before production archiving, qualify the actual device/format combinations required. Claims about lower cost, higher speed or better recovery need a separate comparison and are not inferred here from equipment alone.
