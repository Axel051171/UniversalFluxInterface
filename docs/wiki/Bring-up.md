# Bring-up

**Sprache / Language:** [Deutsch](Inbetriebnahme) | [English](Bring-up)

Test a new board in stages: visual inspection, power, firmware, USB, and then a drive. Keep writing disabled until the intended write tests.

## Before first power-up

1. Compare board revision and fitted parts with the manufacturing files.
2. Check pin-1 markings, polarity and solder bridges.
3. Turn the SCHREIBEN / WRITE switch off or disconnect the front-panel cable.
4. Check the power rails for shorts.
5. Leave drives disconnected initially.

Use the [v1.0 bring-up plan, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Bringup_2026-10-24.md) for power and test points. The older checklist contains earlier-revision sections and must be interpreted for v1.0.

## Firmware and USB

The documented bench uses SWD programming. USB DFU enumeration remained unresolved; see [Firmware](Firmware-EN).

After successful flashing, initially without a drive:

```console
ufi info
ufi status
ufi psram
ufi selftest
ufi diagnostics
```

`selftest` actively tests hardware. Its complete test list is in the [protocol reference, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md). Distinguish `skip` and `warn` from successful tests.

## Generate a report

For a v1.0 board powered through USB only, without a connected 12 V source:

```console
python -m ufi_host.bringup --port COM5 --expect-rev 10 --no-12v --report bringup-usb.json
```

Replace the port. The procedure checks board functions, can switch power rails and can set the RTC. Without `--drive`, it does not move a head; without `--write-test`, it does not perform a disk write test. The report contains OK/WARN/FAIL/SKIP and an overall verdict.

For an initial drive read test, after checking its supply and enabling write protection:

```console
python -m ufi_host.bringup --port COM5 --expect-rev 10 --drive a --read-revs 50 --report bringup-drive.json
```

This requires the appropriate external drive supply. Add `--write-test` only for the planned acceptance step with an expendable disk.

## Record results

Keep board ID, firmware revision or binary hash, power configuration, drive model, cable, disk, measurements and JSON report. A USB test without a drive does not establish successful disk capture.

Recorded starting point: one v1.0 board passed the USB procedure on 11/12 November 2026 with PSRAM in use: 49 OK, 3 WARN, 0 FAIL. See [Project status](Project-Status) for the second board and outstanding drive, 12 V and LAN measurements.
