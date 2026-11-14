# Drives

**Sprache / Language:** [Deutsch](Laufwerke) | [English](Drives)

Drive selection identifies the electrical interface, not automatically the disk format. Match the connector, selected drive, supply, geometry and any double stepping before capture.

| Selection | Interface or family | Notes |
|---|---|---|
| `a`, `b` | PC drives on a 34-pin cable | Check position and cable twist |
| `ds0` to `ds3` | Straight Shugart bus | Drive-select jumpers; shared motor signal |
| `amiga`, `amiga2` | External Amiga drives through J7 | Second selection requires the correct JP3/cable setup |
| `apple` | Apple Disk II on J14 | Quarter tracks; no physical index boundaries without a sync sensor |
| `mac` | Macintosh on J23, v1.0 | Zoned speed and different register control |
| `iec` | Commodore on the IEC bus | Access through the drive controller or nibble helper |

## Selection and movement

```console
ufi select a
ufi motor on
ufi recal
ufi seek 20
ufi side 0
ufi status
ufi motor off
```

`recal` finds the reference position; `seek` moves the head. `scan` probes several selections and can also move heads. Verify wiring first.

## A 40-track disk in an 80-track drive

Adjacent logical tracks are two mechanical steps apart. The current development firmware supports this for one READ:

```console
ufi read 17 0 -r 3 --double-step --read-info -o track17.scp
```

For a persistent timing setting:

```console
ufi timing double_step=1
```

This also affects following operations; reset it when moving to an 80-track disk. `--double-step` applies only to that read request and is not intended for Apple/Mac drives.

On a double-stepping Shugart setup, `--quarter 2` is the half track at mechanical position `2n+1`. A normal Shugart stepper cannot position at quarter offsets 1 or 3.

## Speed and timing

```console
ufi rpm
ufi monitor --drive a --seconds 60
ufi timing
ufi profile
```

Profiles and extra delays depend on the drive. A conservative profile may help during setup. Successful seeking does not replace verification using known track IDs. Eight-inch drives also need an appropriate adapter and TG43/timing configuration.

Sources: [Drive control](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_drive.c), [host reference, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/README.md).
