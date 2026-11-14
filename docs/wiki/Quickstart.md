# Quickstart

**Sprache / Language:** [Deutsch](Schnellstart) | [English](Quickstart)

Use this procedure for a board that has already been tested and flashed. For a newly assembled board, start with [Bring-up](Bring-up).

## 1 Check the connection without a drive

Turn the **SCHREIBEN / WRITE front-panel switch off**. Connect the board to the PC using USB-C only. [Install the host tool](Installation-EN), then run:

```console
ufi info
ufi status
ufi describe
ufi psram
```

`info` reports firmware and board details. Board v1.0 is reported as revision 10. Successful PSRAM initialization produces `PSRAM 8 MB ok`. If the fallback buffer is in use, follow [Troubleshooting](Troubleshooting).

If automatic detection fails, specify the actual port; `COM5` is an example:

```console
ufi --port COM5 info
```

## 2 Prepare an initial read test

Switch power off. Connect the appropriate drive and power supply following [Connections](Connections). The example below selects `a` for a standard PC drive on J6. For other families, see [Drives](Drives). USB alone does not replace the required drive supply.

After powering up, keep writing physically disabled:

```console
ufi select a
ufi motor on
ufi recal
ufi rpm
ufi read 0 0 -r 3 --read-info -o probe.scp
ufi motor off
```

The head will move. Use a known, expendable disk to assess the result. This is a procedure for the outstanding drive qualification, not a claim that hardware acceptance has already been completed.

## 3 Capture a whole disk

For an 80-cylinder disk with two sides:

```console
ufi read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.scp
```

Match the geometry to the disk. For a native archive retaining original ticks and protocol frames:

```console
ufi --integrity read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.ufi
ufi archive-check disk.ufi
```

`archive-check` works offline. A structurally valid archive can contain incomplete attempts; inspect their status too.

Next: [Reading and archiving](Reading-and-Archiving) · [File formats](File-Formats) · [Troubleshooting](Troubleshooting).
