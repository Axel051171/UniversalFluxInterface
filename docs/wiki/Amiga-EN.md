# Amiga

**Sprache / Language:** [Deutsch](Amiga) | [English](Amiga-EN)

External Amiga drives connect through J7 and an appropriate DB23 adapter. Their drive-select and motor requirements differ from a normal PC drive. Mechanical connector similarity does not establish compatible wiring.

## Select the drive

```console
ufi select amiga
ufi motor on
ufi amiga-id
ufi recal
ufi read 0 0 -r 3 --read-info -o amiga-track.scp
ufi motor off
```

`amiga2` provides the second selection with the appropriate JP3/cable configuration. Observe the shared PC/Amiga signals; see [connections](Connections).

## Whole disk and quality report

```console
ufi read-disk --drive amiga --tracks 80 --sides 2 -r 3 -o amiga.scp
ufi amiga-quality amiga.scp --track 0 --side 0 --spt 11 -o amiga-quality.json
```

Normal Amiga DD Trackdisk format has eleven sectors per track and side. Use the corresponding 22-sector workflow for HD. Retain a raw capture even when subsequently generating an ADF.

Host verification checks labels and header metadata as well as data. A standard ADF data image does not preserve all of these fields or the raw flux structure.

## Writing back

After write-path qualification, and only on expendable media:

```console
ufi write-disk amiga.scp --drive amiga --verify
```

Evaluate the reported verification level. A completed write alone does not confirm identical sectors or a disk that boots on an Amiga.

## Cable reference

The [adapter document](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Amiga_DB23_Adapter_Cable.md) includes historical drawings with different header designators. Use the v1.0 schematic when building a cable: J7 is the Amiga header, while J4 is SWD. Distinguish plug and socket views.

Details: [Amiga read-back verification](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Amiga_Prueflesen.md) (German source document).
