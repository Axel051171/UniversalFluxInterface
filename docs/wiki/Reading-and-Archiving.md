# Reading and archiving

**Sprache / Language:** [Deutsch](Lesen-und-Archivieren) | [English](Reading-and-Archiving)

A flux capture retains magnetic-transition times. A decoded sector image contains interpreted payload data. Keep raw capture alongside decoded results for damaged or protected disks.

Real-drive operation remains open in the documented hardware record. These commands describe implemented workflows for qualification and subsequent use.

## Capture one track

After selecting the drive, starting its motor and recalibrating:

```console
ufi read 0 0 -r 5 --read-info --provenance -o track00.scp
```

`-r` sets the number of revolutions. `--read-info` returns context such as position, index origin and loss windows; `--provenance` returns origin information. A normal SCP export does not contain these additional protocol blocks. Save terminal output if you need their displayed contents separately.

## Capture a whole disk

```console
ufi read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.scp
```

Adjust the geometry to the disk. Forty-track disks, single-sided disks and different step widths need appropriate parameters; see [Drives](Drives).

The complete SCP/HFE export is created after all requested tracks have been captured. Prefer a native archive when capture may be interrupted.

## Native archive

```console
ufi --integrity read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.ufi
ufi archive-check disk.ufi
```

`.ufi` is a SQLite archive. It commits CRC-validated reply/data frames during reception, then stores completed revolutions in native ticks. Interrupted attempts remain identifiable as incomplete or failed. Malformed frames rejected by the protocol parser are not stored.

`--integrity` adds job ID, packet sequence, offsets and completion checks. Checksums establish consistency, not media authenticity or capture of every physical transition.

## Unstable regions

```console
ufi read 0 0 -r 5 --read-info --weak --weak-threshold 2 -o weak.scp
```

The host compares revolutions from this exact capture without reading the disk again. Differences can result from weak areas, dirt, speed changes or copy protection. Synthetic indexes and time slices do not physically align the revolutions.

## Pre-index lead-in

```console
ufi read 0 0 -r 3 --lead-in --read-info
```

Lead-in captures up to 20 ms before index 0. It needs suitable index pulses and PSRAM, and is not an extra revolution. `read -o` does not currently store lead-in in SCP/HFE. Retaining it requires deliberately saving native Python data or protocol frames.

## Assess a capture

Check completion status and loss reports before decoder results. More revolutions help comparison but do not compensate for overflows or incorrect geometry. Keep raw files, hashes, drive model, supply details, firmware revision, parameters and quality reports.

Next: [File formats](File-Formats) · [Analysis and data quality](Analysis-and-Data-Quality) · [Troubleshooting](Troubleshooting).

Sources: [Archive implementation](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/archive.py), [READ protocol, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).
