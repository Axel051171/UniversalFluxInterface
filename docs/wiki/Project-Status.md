# Project status

**Sprache / Language:** [Deutsch](Projektstatus) | [English](Project-Status)

**Overview updated: 14 November 2026.** This documentation covers UFI Headless v1.0 with firmware 1.18-dev. Development can continue independently; always record the specific revision used for a test.

## Meaning of status terms

| Term | Meaning |
|---|---|
| Implemented | Code and interface exist |
| Tested offline | The specified software test ran on a host |
| Tested on hardware | The documented procedure ran on a specific board |
| Outstanding | The relevant evidence is still missing |

These statements apply to individual functions and setups. They do not qualify every device function collectively.

## Hardware and bring-up

| Area | Documented status |
|---|---|
| v1.0 manufacturing files | Released, tag `hw-v1.0`; ERC/DRC and manufacturing evidence in the report |
| USB bring-up on one v1.0 board | Passed on 11/12 November 2026: 49 OK, 3 WARN, 0 FAIL; USB-C only, PSRAM in use |
| Second board | Repeat with final firmware revision outstanding |
| Reading with a real drive | Outstanding |
| Writing and reading back with a real drive | Outstanding |
| Drive power at 12 V and manual measurements | Outstanding |
| LAN with cable connected | Outstanding |
| USB DFU | Enumeration failed in the documented setup; cause unresolved |

Sources: [README](https://github.com/Axel051171/UniversalFluxInterface/blob/main/README.md), [bring-up report, especially section 8](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Bringup_2026-10-24.md) (German report).

## Firmware and host

The implementation includes flux streaming, classic and extended write paths, several decoder families, analysis jobs, standalone dumps, USB modes and LAN. Native tests, control-flow checks, fuzzing and the ARM build complement one another.

Test counts change during development. For a particular release, consult its CI/offline report and the corpora actually executed. Skipped checks remain visible; a passing basic gate does not establish a complete corpus run.

## Changes of 14 November 2026

Everything below is covered by offline tests and has not run on a board yet.

- **Read progress:** The per-revolution progress message had never been sent and carried wrong values. It now arrives, including the one for the last revolution, with the index-to-index length and the time since the start of the read.
- **Write protect:** A new event reports a change of the selected drive's write protect state (not for Disk II and IEC).
- **Dropped events:** The device counts the events it could not deliver, by cause. Events remain hints; the completion of each request is authoritative.
- **Image files:** SCP, HFE, G64 and the Apple/Mac formats are written and checked completely before they take their place. An interruption leaves no partial file and does not overwrite an existing image.
- **Guarded requests:** After changing decoder tolerance, speed check or precompensation, fetch the configuration token again; an older one is refused.
- **USB and LAN together:** Settings can no longer be changed from the connection that does not own the drive.
- **Scheduled head steps:** Entries later than 15.6 s are executed; a failed step ends the request with an error instead of reading on at the wrong position.
- **Quarter tracks:** The PC tool keeps track and quarter step apart.
- **LAN:** `ufi net` shows a counter of failed transfers to the network chip once it is above zero.

Details: [protocol, sections 4.26 and 4.27](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md) (German).

## Next measurements

1. Repeat the USB test on the second board.
2. Check power and signals with a test drive.
3. Read known flux sequences or media and compare independently.
4. Qualify write signals on an analyzer first, without media.
5. Write test media and read back independently.
6. Test LAN link, address assignment, data transfer and error handling on hardware.

Plans: [write-path qualification](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Abnahme_Schreibpfad.md), [hardware qualification](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Firmware_Hardwarequalifikation_2026-10-22.md) (German documents).

Historical reports remain useful for understanding development. Older statements such as “LAN firmware missing” or “no assembled hardware yet” are superseded by newer, specifically documented results.
