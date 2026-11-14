# Analyse und Datenqualität

**Sprache / Language:** [Deutsch](Analyse-und-Datenqualitaet) | [English](Analysis-and-Data-Quality)

Ein belastbares Ergebnis verbindet drei Fragen: Wurde die Aufnahme vollständig übertragen? Lassen sich Sektoren konsistent dekodieren? Wiederholen sich deren Inhalte über mehrere Umdrehungen bzw. Aufnahmen?

## Zuerst die Aufnahme prüfen

```console
ufi read 0 0 -r 5 --read-info --provenance --weak
ufi diagnostics
```

Auf Abschlussstatus, Überlauf, Verlustfenster, Indexquelle und Kopfposition achten. Eine Aufnahme mit Verlusten bleibt unvollständig, auch wenn sich darin noch gültige Sektoren finden.

`--weak` vergleicht Übergangszahlen in Zeitfenstern zwischen den Umdrehungen dieser Aufnahme. Die Fenster sind keine einzelnen magnetischen Bits. Ohne physischen Index sind sie außerdem nicht zuverlässig an derselben Winkelposition ausgerichtet.

## Firmwareanalyse

```console
ufi analyze 0 0 -r 3
ufi weak --positions
```

`analyze` liest die Spur erneut und erzeugt eigene Ergebnisse. Diese beschreiben daher nicht automatisch die zuvor gespeicherte READ-Aufnahme. Besonders bei instabilen Stellen müssen beide Aufnahmen getrennt betrachtet werden.

Der aktuelle Entwicklungsstand enthält darüber hinaus kooperative Analysejobs, Vergleichsberichte, Sektordetails, Archiv-Replay und Pläne über mehrere Spuren. Die genauen Schemas und Bedingungen stehen in der [Analysejob-Referenz](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).

## Strenge und tolerante Dekodierung

Toleranzoptionen können beschädigte Prüfsummen, ungewöhnliche Prologe oder unvollständige Strukturen sichtbar machen. So gewonnene Daten müssen ihren Befund behalten. Ein toleriert dekodierter Sektor ist nicht automatisch fehlerfrei.

```console
ufi tolerance
ufi apple-gcr
```

Vor Änderungen die Standardauswertung aufbewahren. Bei systematischen Abweichungen auch Laufwerksjustage, Kabel und Versorgung prüfen, statt nur Decoder-Toleranzen zu erhöhen.

## Amiga offline auswerten

```console
ufi amiga-quality disk.scp --track 0 --side 0 --spt 11 -o quality.json
```

Das liest die vorhandene Datei und benötigt kein Board. Die Auswertung berücksichtigt unter anderem Sektordaten, Labels und widersprüchliche Duplikate. Sie ergänzt die Rohdatei; siehe [Amiga](Amiga).

## Nachvollziehbare Ergebnisse

Rohaufnahme, Bericht, Firmware-Revision und Einstellungen zusammen speichern. Hashes erlauben die spätere Prüfung derselben Datei. Für Medienidentität zusätzlich eigene Kennzeichnung und gegebenenfalls einen Sektorvergleich verwenden; eine Sitzungs-Generationsnummer ist keine dauerhafte Disketten-ID.

Weiter: [Lesen und Archivieren](Lesen-und-Archivieren) · [Projektstatus](Projektstatus).
