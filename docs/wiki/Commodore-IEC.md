# Commodore IEC

**Sprache / Language:** [Deutsch](Commodore-IEC) | [English](Commodore-IEC-EN)

Über J8 erreicht UFI den seriellen IEC-Bus von Commodore-Laufwerken. Sektorzugriffe laufen dabei über den Controller im Laufwerk. Diese Verbindung ist kein direkter elektrischer Flux-Eingang des STM32.

## Erstkontakt

Laufwerk über den passenden Adapter anschließen und entsprechend seinem Modell versorgen. Busadresse, gemeinsame Masse und Kabelbelegung prüfen.

```console
ufi select iec
ufi iec-reset
ufi iec-status
```

Standardmäßig kann die Busadresse aus `UFI.CFG` kommen. Mit `--device` lässt sie sich für die dafür vorgesehenen Host-Befehle festlegen.

## Sektorabbild sichern

Beispiel für eine 1541 mit Adresse 8:

```console
ufi iec-read disk.d64 --type 1541 --device 8 --tracks 35 --retries 2
```

Für 1571 und 1581 die passende Typ-/Abbildkombination wählen. Bei Lesefehlern oder Unterbrechung bleiben im Journal-Workflow Teildatei und Checkpoint erhalten. Ein Fortsetzen ist möglich:

```console
ufi iec-read disk.d64 --type 1541 --device 8 --tracks 35 --retries 2 --resume
```

Dabei muss dieselbe physische Diskette eingelegt bleiben. Ein Checkpoint identifiziert eine Datei und den Arbeitsfortschritt; er beweist keine physische Medienidentität.

## Auf formatierte Diskette schreiben

Nach Freigabe des Schreibpfads und nur mit Testmedium:

```console
ufi iec-write disk.d64 --verify --device 8
```

Der U2-Sektorworkflow setzt ein passend formatiertes Ziel voraus. Fehlerstatus der Laufwerks-DOS-Meldung und Rücklesevergleich auswerten. Ein Sektorabbild stellt ungewöhnliche GCR-Spurstrukturen nicht vollständig wieder her.

## Nibble-Helfer

Das Projekt enthält zusätzlich 6502-Laufwerkscode für Nibble-Aufnahmen. Dieser wird separat assembliert und mit einem VIA-/Diskmodell geprüft. Ergebnisse dieses Pfads von direkten STM32-Fluxaufnahmen und normalen IEC-Sektorabbildern unterscheiden.

Quellen: [IEC-Treiber](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_iec.c), [Host-Journal](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/iec_job.py), [6502-Helfer](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tools/iec_nib.s).
