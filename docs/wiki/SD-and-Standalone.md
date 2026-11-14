# SD and standalone operation

**Sprache / Language:** [Deutsch](SD-und-Standalone) | [English](SD-and-Standalone)

The internal SD NAND stores captures and configuration. A standalone dump runs without an active PC application. Firmware file access and USB mass storage are mutually exclusive.

## Transfer files through the host

```console
ufi files
ufi get DUMP0001.SCP -o sicherung.scp
ufi put lokal.scp TEST.SCP
ufi cfg
```

Device files use FAT 8.3 names. `get` first writes a temporary file, synchronizes and checks it, and only then replaces the destination. This protects against an incomplete destination file but does not prevent replacing an existing file. Choose destination names deliberately. Implementation: [atomic file output](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/atomic.py).

## Capture configuration

A simple `UFI.CFG` for a PC drive:

```ini
drive=a
tracks=80
sides=2
revs=3
format=scp
button_a=dump
protocol=ufi
auto_dump=0
```

Adjust these values to the medium and drive. Individual settings can be changed and reloaded through the host:

```console
ufi cfg drive=a tracks=80 sides=2 revs=3 format=scp button_a=dump
```

`button_a=copy` and `button_a=write` perform writes. Keep `dump` for initial setup. The complete key list is in the [configuration reference](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).

## Run a dump

```console
ufi dump --drive a --tracks 80 --sides 2 -r 3 --format scp
ufi dump-status
```

With the appropriate configuration, holding button A also starts a dump. Button B cancels; the host provides `ufi dump-abort`. Keep the image and its LOG/JSN reports together. The dump workflow initially distinguishes unfinished output from completed images.

## Retrieve files as a USB drive

After completion, switch to SD mode:

```console
ufi mode sd
```

Copy the files, eject the USB drive on the PC, and only then switch back. The PC filesystem must not access an active dump concurrently.

## Write back from SD

```console
ufi sd-write TEST.SCP
```

This command **writes to the floppy disk**. Use it only after [write-path qualification](Writing-and-Verification), with the correct selection, power and permissions. Results are recorded in files including `WRITE.LOG`.

Source: [standalone implementation](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_dump.c).
