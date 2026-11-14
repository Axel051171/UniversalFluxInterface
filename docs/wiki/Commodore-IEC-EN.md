# Commodore IEC

**Sprache / Language:** [Deutsch](Commodore-IEC) | [English](Commodore-IEC-EN)

J8 connects UFI to the serial IEC bus of Commodore drives. Sector access passes through the controller inside the drive. This connection is not a direct electrical flux input to the STM32.

## Initial contact

Connect the drive with the correct adapter and power it according to its model. Check the bus address, common ground and cable pinout.

```console
ufi select iec
ufi iec-reset
ufi iec-status
```

The default bus address can come from `UFI.CFG`. Use `--device` for host commands that support an explicit address.

## Capture a sector image

Example for a 1541 at address 8:

```console
ufi iec-read disk.d64 --type 1541 --device 8 --tracks 35 --retries 2
```

For 1571 and 1581 drives, choose the matching type/image combination. On read errors or interruption, the journal workflow retains a partial file and checkpoint. A capture can be resumed:

```console
ufi iec-read disk.d64 --type 1541 --device 8 --tracks 35 --retries 2 --resume
```

Keep the same physical disk inserted. A checkpoint identifies a file and its progress; it does not prove physical media identity.

## Write to a formatted disk

After enabling the write path, and only on test media:

```console
ufi iec-write disk.d64 --verify --device 8
```

The U2 sector workflow requires a suitably formatted destination. Evaluate drive DOS error status and the read-back comparison. A sector image cannot fully reconstruct unusual GCR track structures.

## Nibble helper

The project also includes 6502 drive code for nibble captures. It is assembled separately and tested with a VIA/disk model. Distinguish this path from direct STM32 flux captures and ordinary IEC sector images.

Sources: [IEC driver](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_iec.c), [host journal](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/iec_job.py), [6502 helper](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tools/iec_nib.s).
