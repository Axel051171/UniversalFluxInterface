# Apple and Macintosh

**Sprache / Language:** [Deutsch](Apple-und-Macintosh) | [English](Apple-and-Macintosh)

Apple Disk II and Macintosh use different drive controls. On v1.0, J14 is the Disk II port and J23 is the Macintosh port. Identify the correct drive, power supply, adapter and optional solder bridges before connecting anything.

## Apple Disk II

```console
ufi select apple
ufi motor on
ufi recal
ufi read 17 0 -r 3 --quarter 2 --read-info -o apple-track.scp
ufi motor off
```

`--quarter 2` means track 17 plus two quarter steps, or 17.5. This is encoded separately from side selection.

Without a sync sensor, a Disk II drive provides no conventional index signal. UFI then divides captures into time slices whose boundaries do not identify a fixed angular position on the medium. With a suitable sensor on J19 and the appropriate `apple_sync` setup, firmware can use physical revolution boundaries.

For a decoded Apple II image:

```console
ufi read-dsk -o apple.dsk --tracks 35 -r 2
```

`.dsk` defaults to DOS 3.3 sector order; `.po` uses ProDOS order. Options for other prologues, symbol tables and tolerant decoding are specialist cases. Do not reinterpret error findings as valid sectors.

## Macintosh on v1.0

```console
ufi select mac
ufi mac status
ufi read-mac -o mac.dc42 --speed --retries 2
```

Firmware controls Sony registers and zone-dependent rotational speeds. 400K, 800K and FDHD drives differ. In particular, enable optional PWM and −12 V bridges only after checking the specific drive. The name `apple2` in older v0.7 documents does not identify a second Disk II socket on v1.0.

The FDHD MFM path has separate options, including a mode command not confirmed on hardware. Detecting a drive does not establish complete format support.

## Writing

`write-mac` generates GCR tracks from suitable 400K/800K images and can read them back. The implementation has offline tests; operation on hardware remains to be measured. First follow [writing and verification](Writing-and-Verification) and the drive-specific qualification plan.

Sources: [Mac driver](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_mac.c), [host Apple decoder](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/apple.py), [host Mac decoder](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/mac.py).
