# Analysis and data quality

**Sprache / Language:** [Deutsch](Analyse-und-Datenqualitaet) | [English](Analysis-and-Data-Quality)

A useful result answers three questions: was capture transferred completely, can sectors be decoded consistently, and do their contents agree across revolutions or captures?

## Check capture integrity first

```console
ufi read 0 0 -r 5 --read-info --provenance --weak
ufi diagnostics
```

Inspect completion status, overflow, loss windows, index source and head position. A capture with losses remains incomplete even if valid sectors can be decoded from it.

`--weak` compares transition counts in time windows across the capture's revolutions. Windows are not individual magnetic bits. Without physical index, they are not reliably aligned to the same angular position.

## Firmware analysis

```console
ufi analyze 0 0 -r 3
ufi weak --positions
```

`analyze` reads the track again and creates its own results. They do not automatically describe the earlier READ capture. Keep these separate, especially for unstable areas.

The development firmware also includes cooperative analysis jobs, comparison reports, sector details, archive replay and multitrack plans. Exact schemas and conditions are in the [analysis-job reference, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).

## Strict and tolerant decoding

Tolerance options can expose bad checksums, unusual prologues or incomplete structures. Preserve those findings with any recovered data. A sector accepted under tolerance is not automatically error-free.

```console
ufi tolerance
ufi apple-gcr
```

Retain the standard result before changing settings. For systematic discrepancies, also check alignment, cabling and power instead of only increasing tolerance.

## Offline Amiga analysis

```console
ufi amiga-quality disk.scp --track 0 --side 0 --spt 11 -o quality.json
```

This reads an existing file without a board. It checks sector data, labels and conflicting duplicates among other findings. Keep the raw file too; see [Amiga](Amiga-EN).

## Traceable results

Store raw capture, report, firmware revision and settings together. Hashes allow later verification of the same file. Physical media identity additionally needs your own labeling and, where appropriate, sector comparison. A session generation number is not a permanent disk ID.

Next: [Reading and archiving](Reading-and-Archiving) · [Project status](Project-Status).
