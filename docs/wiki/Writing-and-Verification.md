# Writing and verification

**Sprache / Language:** [Deutsch](Schreiben-und-Prueflesen) | [English](Writing-and-Verification)

Write commands modify the inserted disk. Use expendable media for setup and acceptance. The physical write path has not yet been accepted with real drives in the documented test record.

## Write enable

On v1.0, both the front SCHREIBEN / WRITE switch and firmware enable must permit the hardware write path. Drive write protection and any session write lock also apply.

```console
ufi write-lock
ufi status
ufi write-limits
```

`ufi write-lock engage` locks disk writing until an MCU reset. No USB command disengages it. It does not replace physical write protection.

## Write an SCP image back

One track from an existing SCP image:

```console
ufi write disk.scp 0 0 --verify
```

Select the appropriate drive and start its motor first. For a whole image:

```console
ufi write-disk disk.scp --drive a --verify
```

Drive and geometry must match the image. By default, the first revolution of each SCP track is used. HFE images are converted from bit cells into flux intervals.

## Verification levels

| Level | Meaning |
|---|---|
| 0 | No corresponding confirmation |
| 1 | Write output completed |
| 2 | Readback plausible; content identity not confirmed |
| 3 | Content of captured reference sectors confirmed |
| 4 | Not verifiable using this method |

These levels are evidence classes. Level 4 is not stronger than level 3. A raw fingerprint compares timing/density distributions and does not establish identical sector contents.

## Extended write path

```console
ufi --extended write disk.scp 0 0 --verify
```

The extended path accepts host-specified timings and long gaps. Supported combinations depend on backend, format and firmware. Extended `--verify` may perform host-side readback. Check `ufi write --help`, `ufi describe`, `ufi write-limits` and the protocol reference for special options.

## Options that deliberately alter the signal

- `--clamp` modifies unrepresentable intervals and reports their count; it is not a lossless substitute for a suitable write backend.
- `--scale` scales flux timings; use only for a justified source/target speed adjustment.
- `--erase-rest` keeps the write gate open after the data until the next index, erasing the track remainder.
- `--start-offset` shifts the start relative to index, in 275 MHz ticks.

Do not combine these indiscriminately. Establish backend limits and error behavior before using valuable media.

## Acceptance

The [write-path measurement plan, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Abnahme_Schreibpfad.md) covers pulse count, DMA boundaries, index termination, locks and readback. Start with loopback/logic-analyzer measurements, then test media and preferably independent-controller verification.

Source: [Verification classes](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_verify.h).
