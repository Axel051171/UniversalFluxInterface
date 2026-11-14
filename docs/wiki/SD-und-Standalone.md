# SD und Standalone

**Sprache / Language:** [Deutsch](SD-und-Standalone) | [English](SD-and-Standalone)

Der interne SD-NAND speichert Aufnahmen und Konfiguration. Ein Standalone-Dump arbeitet ohne laufendes PC-Programm. Dateizugriff durch Firmware und USB-Massenspeicher ist gegenseitig ausgeschlossen.

## Dateien über den Host übertragen

```console
ufi files
ufi get DUMP0001.SCP -o sicherung.scp
ufi put lokal.scp TEST.SCP
ufi cfg
```

Gerätedateien verwenden FAT-8.3-Namen. `get` schreibt zunächst eine temporäre Datei, synchronisiert und prüft sie und ersetzt erst danach das Ziel. Das schützt vor einer halbfertigen Zieldatei, verhindert aber nicht das Ersetzen einer vorhandenen Datei. Zieldateinamen deshalb bewusst wählen. Implementierung: [atomare Dateiausgabe](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/atomic.py).

## Aufnahmekonfiguration

Ein einfaches `UFI.CFG` für ein PC-Laufwerk:

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

Diese Werte an Medium und Laufwerk anpassen. Über den Host lassen sich einzelne Werte ändern und neu laden:

```console
ufi cfg drive=a tracks=80 sides=2 revs=3 format=scp button_a=dump
```

`button_a=copy` bzw. `button_a=write` sind schreibende Betriebsweisen. Für die erste Einrichtung `dump` belassen. Die vollständigen Schlüssel stehen in der [Konfigurationsreferenz](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).

## Dump ausführen

```console
ufi dump --drive a --tracks 80 --sides 2 -r 3 --format scp
ufi dump-status
```

Alternativ startet bei passender Konfiguration ein längerer Druck auf Taster A den Dump. Taster B bricht ab; der Host bietet `ufi dump-abort`. Bild und zugehörige LOG-/JSN-Berichte gemeinsam aufbewahren. Unfertige Ausgaben werden im Dump-Pfad zunächst getrennt von abgeschlossenen Abbildern behandelt.

## Als USB-Laufwerk abholen

Nach Abschluss in den SD-Modus schalten:

```console
ufi mode sd
```

Dateien kopieren, das USB-Laufwerk am PC auswerfen und erst dann zurückschalten. Ein aktiver Dump darf nicht gleichzeitig vom PC-Dateisystem bearbeitet werden.

## Von SD zurückschreiben

```console
ufi sd-write TEST.SCP
```

Dieser Befehl **schreibt auf die Diskette**. Erst nach [Schreibpfad-Abnahme](Schreiben-und-Prueflesen) und mit passender Auswahl, Versorgung und Freigabe verwenden. Das Ergebnis wird unter anderem in `WRITE.LOG` protokolliert.

Quelle: [Standalone-Implementierung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_dump.c).
