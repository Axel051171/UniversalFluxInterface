# Amiga

**Sprache / Language:** [Deutsch](Amiga) | [English](Amiga-EN)

Externe Amiga-Laufwerke werden über J7 und einen passenden Adapter zum DB23-Anschluss verbunden. Sie verwenden andere Auswahl- und Motorbedingungen als ein gewöhnliches PC-Laufwerk. Die mechanische Ähnlichkeit eines Steckers bestätigt keine passende Verdrahtung.

## Laufwerk auswählen

```console
ufi select amiga
ufi motor on
ufi amiga-id
ufi recal
ufi read 0 0 -r 3 --read-info -o amiga-track.scp
ufi motor off
```

`amiga2` ist für die zweite Auswahl mit passender JP3-/Kabelkonfiguration vorgesehen. Gemeinsam verwendete PC-/Amiga-Leitungen beachten; siehe [Anschlüsse](Anschluesse).

## Ganze Diskette und Qualitätsbericht

```console
ufi read-disk --drive amiga --tracks 80 --sides 2 -r 3 -o amiga.scp
ufi amiga-quality amiga.scp --track 0 --side 0 --spt 11 -o amiga-quality.json
```

Amiga-DD verwendet im normalen Trackdisk-Format elf Sektoren pro Seite/Spur. Für HD ist der entsprechende 22-Sektor-Workflow zu wählen. Eine Rohaufnahme auch dann behalten, wenn anschließend ein ADF erzeugt wird.

Die Host-Prüfung berücksichtigt neben Daten auch Labels und Header-Metadaten. Ein normaler ADF-Datenstrom bewahrt nicht alle diese Angaben oder die rohe Fluxstruktur.

## Rückschreiben

Nach abgeschlossener Schreibpfad-Abnahme und ausschließlich auf einem entbehrlichen Medium:

```console
ufi write-disk amiga.scp --drive amiga --verify
```

Den gemeldeten Verify-Level auswerten. Ein bloß abgeschlossener Schreibvorgang bestätigt noch keine identischen Sektoren oder ein am Amiga bootfähiges Ergebnis.

## Kabelreferenz

Die [Adapterunterlage](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Amiga_DB23_Adapter_Cable.md) enthält historische Zeichnungen mit abweichenden Headerbezeichnungen. Für den Bau den v1.0-Schaltplan heranziehen; J7 ist der Amiga-Header, J4 ist SWD. Steckeransicht und Buchsenansicht nicht vertauschen.

Details: [Amiga-Prüflesen](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Amiga_Prueflesen.md).
