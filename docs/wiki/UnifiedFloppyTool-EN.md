# UnifiedFloppyTool with UFI

**Sprache / Language:** [Deutsch](UnifiedFloppyTool) | [English](UnifiedFloppyTool-EN)

**UnifiedFloppyTool (UFT) is the graphical PC application for the UFI board.** The reviewed development version includes a dedicated “UFI Flux Engine - Beta” controller, native USB connectivity, flux capture and writing, and additional UFI tools. The Python `ufi` command line remains a separate interface for diagnostics and scripts.

This page is based on the local development sources reviewed on 14 November 2026, Git revision `7e7dd6701c3a`. The folder name `UnifiedFloppyTool-4.1.0` is not a reliable version identifier: `VERSION.txt` specifies **4.1.6**. A published package with the same version number need not contain every development change described here. Public project: [UnifiedFloppyTool](https://github.com/Axel051171/UnifiedFloppyTool).

## Choosing an interface

| Interface | Use |
|---|---|
| UFT: “UFI Flux Engine - Beta” | Graphical access to native UFI v2 over USB CDC |
| Python host `ufi` | Commands, scripts, native `.ufi` archives and the implemented LAN interface |
| UFT: Greaseweazle controller | UFI in Greaseweazle-compatible mode, with that mode's capabilities |
| UFT: USB-Floppy controller | Separate interface for ordinary USB floppy drives; choose “UFI Flux Engine” for the native UFI board |

The reviewed native UFT connection uses `QSerialPort`. This does not establish support for UFI LAN transport or the native composite bulk interface in UFT. For those interfaces, follow the [LAN](LAN-EN) and [installation](Installation-EN) guides.

## Setting up the graphical connection

1. Use a UFT build with **Qt SerialPort** and native UFI support. A build without this module explicitly reports that a connection is unavailable.
2. Put the board into native Flux/UFI mode, with `protocol=ufi` in `UFI.CFG`. Its CDC port has VID/PID **`1209:4F54`**.
3. Close other applications holding the same COM port. UFT opens it exclusively.
4. In UFT, open the **Hardware** tab and select **“UFI Flux Engine - Beta”**, the actual port and the appropriate drive.
5. For qualification, enable **“Mitschnitt (.ufec) für die Bank-Abnahme”** (bench qualification recording) before connecting, and select a writable destination folder.
6. Click **Verbinden** (Connect), then check the reported firmware and board information. Expand **“UFI Flux Engine – Werkzeuge (Beta)”**. Tools remain disabled without a board session.

In GW mode the board has PID `4F57`. The native transport rejects that port with an explanation. Either use the Greaseweazle controller or return to native UFI mode. Passing the board's USB bring-up alone does not establish a successful UFT capture run.

## Available UFI tools

| Area | Functions present in the reviewed code |
|---|---|
| Diagnose | Self-test, drive discovery, track analysis, seek test and drive timing |
| Analyse | A separate track reading, quick scan and targeted flux re-reading of marked tracks |
| Apple II | Prologues/epilogues, decoding options, quarter-track selection and nibble statistics |
| IEC (Commodore) | Status, bus reset and images for 1541 D64 with 35/40 tracks, 1571 D71 and 1581 D81 |
| Board | Firmware/capability information, power status, SD file listing and USB mode changes |

Analysis that reads a track again is a separate capture. Its result must not automatically be attributed to the previous flux file. Drive timing and Apple decoding options change settings, so diagnostic tools are not collectively state-neutral.

A USB mode change ends UFT's native connection. The offered bootloader command does not establish working DFU enumeration: this remains unresolved according to [project status](Project-Status).

## Captures, files and write verification

Start with known test media, correct geometry and a small revolution count. The native UFT provider limits jobs to **20 revolutions** because of its 32-bit index times in nanoseconds. This is a limit of this host path, not a general firmware limit. Double stepping for flux reads is used only when the firmware announces the relevant command capability; it does not establish equivalent write-side double stepping.

| File or representation | Meaning |
|---|---|
| UFT `.ufec` | Connection traffic recording for diagnostics and qualification; not an ordinary disk image |
| UFT flux capture | The provider converts native ticks into integer nanoseconds using absolute timestamps |
| SCP output | Interchange format with a 25 ns grid; retain its metadata and capture findings as well |
| Python host `.ufi` | Separate native SQLite archive with ticks and validated frames; `.ufec` does not replace it |

UFT queries board capabilities and requires session consent for read/write functions not yet qualified on hardware. Consent is not technical qualification. For writing, also follow [writing and verification](Writing-and-Verification): hardware enable, write protection, permitted intervals and evidence level must agree. The native provider reports `verified` only at **level 3**, not merely after completed output or plausible read-back. Extended firmware verification requires support from the board and firmware.

## Language and qualification status

The wiki offers German and English versions. The reviewed UFT application explicitly loads German translations at startup; an equivalent language selector is not currently configured there. This English wiki page therefore retains the actual German control labels to help locate them.

The native integration and its tests exist in the source tree. Protocol doubles, transport recordings and the FakeV2 bridge do not replace measurements with real drives. Parts of the UFT bench guide still refer to board v0.7: do not transfer its older connector numbers to v1.0. For v1.0, follow the [connection overview](Connections), with IEC on **J8**.

Sources in the reviewed UFT tree: `src/hardwaretab.cpp`, `src/gui/ufi_werkzeuge.cpp`, `forms/ufi_werkzeuge.ui`, `src/hardware_providers/qserial_ufi_flux_engine_transport.cpp`, `src/hardware_providers/ufi_flux_engine_provider_v2.h`, `src/fluxcapturejob.cpp`, `src/main.cpp` and `docs/UFI_FLUX_ENGINE_BANK.md`. The local source reference is recorded in `docs/wiki-tools/companion-sources.json`.
