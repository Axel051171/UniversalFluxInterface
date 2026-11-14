# Lesen und Archivieren

**Sprache / Language:** [Deutsch](Lesen-und-Archivieren) | [English](Reading-and-Archiving)

Eine Flux-Aufnahme bewahrt Zeitpunkte magnetischer Übergänge. Ein dekodiertes Sektorabbild enthält dagegen bereits interpretierte Nutzdaten. Für problematische oder geschützte Medien die Rohaufnahme zusätzlich zum dekodierten Ergebnis behalten.

Der Laufwerksbetrieb ist im dokumentierten Hardware-Prüfstand noch offen. Die Befehle unten beschreiben die implementierten Arbeitsabläufe für die Gerätequalifikation und spätere Nutzung.

## Einzelne Spur

Nach Auswahl, Motorstart und Referenzfahrt:

```console
ufi read 0 0 -r 5 --read-info --provenance -o track00.scp
```

`-r` bestimmt die Anzahl Umdrehungen. `--read-info` liefert Kontext wie Position, Indexquelle und Verlustfenster; `--provenance` meldet Herkunftsangaben. Die normale SCP-Datei enthält diese zusätzlichen Protokollblöcke nicht. Für deren Anzeige bzw. separate Aufbewahrung die Terminalausgabe sichern.

## Ganze Diskette

```console
ufi read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.scp
```

Die Zahlen an das Medium anpassen. Disketten mit 40 Spuren, einseitige Medien und Laufwerke mit abweichender Schrittweite brauchen andere Parameter; siehe [Laufwerke](Laufwerke).

Der SCP-/HFE-Gesamtexport entsteht erst nach dem Einlesen der angeforderten Spuren. Für unterbrechbare Sicherungen ist das native Archiv vorzuziehen.

## Natives Archiv

```console
ufi --integrity read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.ufi
ufi archive-check disk.ufi
```

`.ufi` ist ein SQLite-Archiv. Es speichert CRC-geprüfte Antwort-/Datenrahmen während des Empfangs und nach Abschluss die Umdrehungen in nativen Ticks. Abgebrochene Versuche bleiben als unvollständig oder fehlgeschlagen erkennbar. Durch den Protokollparser verworfene fehlerhafte Rahmen gehören nicht zum gespeicherten Datenbestand.

`--integrity` ergänzt Jobkennung, Paketfolge, Offsets und Abschlussprüfung. Die Prüfsummen bestätigen Datenkonsistenz; sie belegen weder die Echtheit eines Mediums noch, dass jeder physische Fluxwechsel erfasst wurde.

## Instabile Bereiche

```console
ufi read 0 0 -r 5 --read-info --weak --weak-threshold 2 -o weak.scp
```

Die Host-Auswertung vergleicht die Umdrehungen genau dieser Aufnahme. Sie liest das Medium nicht erneut. Unterschiede können von schwachen Stellen, Schmutz, Drehzahländerungen oder Kopierschutz stammen. Bei synthetischem Index bzw. Zeitscheiben sind die Umdrehungen nicht physisch ausgerichtet.

## Vorlauf am Index

```console
ufi read 0 0 -r 3 --lead-in --read-info
```

Der Vorlauf erfasst bis zu 20 ms vor dem ersten Index. Er benötigt passende Indeximpulse und PSRAM; er zählt nicht als zusätzliche Umdrehung. `read -o` schreibt den Vorlauf derzeit nicht in SCP/HFE. Wer ihn erhalten möchte, muss die native Python-Schnittstelle bzw. die Protokollrahmen gezielt speichern.

## Aufnahme beurteilen

Erst den Abschlussstatus und Verlustmeldungen prüfen, danach Decoderergebnisse. Mehr Umdrehungen helfen beim Vergleich, gleichen aber einen Überlauf oder eine falsche Geometrie nicht aus. Aufbewahren: Rohdatei, Hash, Laufwerksmodell, Versorgung, Firmware-Revision, Aufnahmeparameter und Qualitätsbericht.

Weiter: [Dateiformate](Dateiformate) · [Analyse und Datenqualität](Analyse-und-Datenqualitaet) · [Fehlerhilfe](Fehlerhilfe).

Quellen: [Archivformat](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/archive.py), [READ-Protokoll](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).
