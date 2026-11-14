# Connections

**Sprache / Language:** [Deutsch](Anschluesse) | [English](Connections)

This overview applies to **UFI Headless v1.0**. Earlier revisions differ, especially in write protection, Apple/Mac ports and solder bridges. Disconnect power before changing connections. Check pin 1 and cable orientation on the actual board.

## v1.0 connector overview

| Connector | Use | Notes |
|---|---|---|
| J1 | USB-C | Host connection and board power |
| J22 | RJ45 | W5500 LAN |
| J6 | 34-pin PC/Shugart drive | A twisted PC cable and straight Shugart bus need different selections |
| J7 | Amiga drive | Header for an external DB23 adapter |
| J14 | 20-pin Apple Disk II | Do not confuse it with the Macintosh port |
| J23 | Macintosh 3.5-inch drive | v1.0; set JP5/JP6 only after checking the drive-specific requirements |
| J8 | Commodore IEC | Adapter to the serial drive bus |
| J19 | Sync sensor | Index reference for suitable Apple captures |
| J12 | Front panel | Buttons, indicators and write-enable switch |
| J4 | SWD | Programming and debugging |
| J3 | Drive power | Switched supply; verify polarity against the schematic |
| J17 | Power input | Its own input pinout; do not confuse it with J3 |

## Shared signals

PC/Shugart and Amiga share control lines. Multiple connectors do not imply independent simultaneous operation. DS0–DS3 on a straight Shugart bus share the motor signal.

JP1 can assign J6 pin 6 to DRATE or DS3; these uses are mutually exclusive. JP3 concerns the second Amiga selection. On v1.0, J6 pin 3 is permanently grounded. Instructions describing switchable JP2 belong to earlier revisions.

## Apple and Macintosh

On v0.7, J15 provided a second Disk II enable. On v1.0, drive ID 11 serves Mac port J23. The `apple2` name in older examples is therefore not a general recommendation for v1.0.

The v1.0 sync signal is captured separately through TIM8. Do not apply historical descriptions of direct connection to normal INDEX without checking the revision.

## Authoritative pin assignments

For custom cables, check the exact revision in KiCad and the firmware pin mapping. Older adapter drawings may use different connector names: J4 on v1.0 is SWD.

- [Schematic](https://github.com/Axel051171/UniversalFluxInterface/blob/main/kicad/UFI_Headless/UFI_Headless.kicad_sch)
- [Firmware pin definitions](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/board.h)
- [Board abstraction](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/board_headless.c)
- [Timer and peripheral assignments, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Timer_Zuteilung.md)

Next: [Drives](Drives) · [Amiga](Amiga-EN) · [Apple and Macintosh](Apple-and-Macintosh).
