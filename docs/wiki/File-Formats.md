# File formats

**Sprache / Language:** [Deutsch](Dateiformate) | [English](File-Formats)

A filename extension alone does not say how much disk information is retained. UFI distinguishes native timestamps, converted flux, bit cells and decoded sectors.

| Format | Contents | Use and limitations |
|---|---|---|
| `.ufi` | Native ticks, stored v2 frames, metadata and attempt states in SQLite | Original capture and failure evidence; not a general interchange format or direct `write` input |
| `.scp` | Flux intervals and index times, multiple revolutions | Interchange and writing; host export quantizes to 25 ns |
| `.hfe` | Bit cells; HFEv3 may carry additional timing/control information | Emulators and suitable write paths; not equivalent to native flux |
| `.img`, `.adf`, `.d81` | Decoded sectors for the relevant workflow | Payload data; ordinary sector images do not preserve complete flux structure |
| `.d64`, `.d71` | Commodore sector images | IEC workflows for the corresponding drives |
| Apple `.dsk`, `.po` | Decoded Apple II sectors | Sector order matters |
| Mac `.dc42`, `.image` | DiskCopy 4.2 data and tags | 400K/800K workflows; check each command's format requirements |

## SCP and native ticks

UFI's nominal flux timebase is 275 MHz. The host SCP writer converts to 40 MHz, or 25 ns units. Rounding absolute timestamps prevents cumulative per-interval rounding, but SCP still does not retain all original 275 MHz resolution.

## HFE

Normal host export with `read-disk -o disk.hfe` maps the first revolution into bit cells. Bit rate and encoding can be specified. Additional revolutions and raw timing variations are not retained as a full raw capture.

HFEv3 adds variable-rate, index-position and weak-region operations. Not every write backend can reproduce every operation. Do not bypass rejection by renaming the extension.

## Sector images

A decoded image can contain missing sectors despite a readable file structure. Standalone sector dumps may fill missing sectors with zeros, so their reports are part of the result. Successful file creation alone does not establish disk completeness.

## Choose a format

- Preservation and analysis: `.ufi` or an additional raw-flux capture.
- Interchange with flux tools: `.scp`.
- A particular emulator: its supported bit-cell/sector format alongside the raw archive.
- IEC backup: the drive-appropriate D64/D71/D81 workflow with journal and error report.

Sources: [SCP](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/scp.py), [HFE](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/hfe.py), [native archive](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/archive.py).
