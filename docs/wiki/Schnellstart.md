# Schnellstart

**Sprache / Language:** [Deutsch](Schnellstart) | [English](Quickstart)

Dieser Ablauf gilt für eine bereits geprüfte und mit Firmware versehene Platine. Für ein frisch bestücktes Board zuerst die [Inbetriebnahme](Inbetriebnahme) durchführen.

## 1 Verbindung ohne Laufwerk prüfen

Den Frontschalter **SCHREIBEN ausschalten** und das Board zunächst nur über USB-C mit dem PC verbinden. Das [Host-Werkzeug installieren](Installation), dann ausführen:

```console
ufi info
ufi status
ufi describe
ufi psram
```

`info` nennt Firmware und Board. Platine v1.0 erscheint als Revision 10. Ein erfolgreicher PSRAM-Start wird als `PSRAM 8 MB ok` gemeldet. Bei einem Ersatzpuffer die Ursache auf der [Fehlerhilfeseite](Fehlerhilfe) prüfen.

Wenn die automatische Erkennung scheitert, den Port ausdrücklich wählen; `COM5` ist ein Beispiel:

```console
ufi --port COM5 info
```

## 2 Ersten Lesetest vorbereiten

Strom abschalten, das passende Laufwerk und dessen Versorgung nach [Anschlüsse](Anschluesse) verbinden. Ein Standard-PC-Laufwerk an J6 verwendet im folgenden Beispiel die Auswahl `a`. Für andere Familien die [Laufwerksseite](Laufwerke) beachten. Ein USB-Kabel allein ersetzt die benötigte Laufwerksversorgung nicht.

Nach dem Einschalten bei weiterhin gesperrtem Schreiben:

```console
ufi select a
ufi motor on
ufi recal
ufi rpm
ufi read 0 0 -r 3 --read-info -o probe.scp
ufi motor off
```

Der Kopf wird bewegt. Eine bekannte, entbehrliche Diskette erleichtert die Beurteilung. Dieser Ablauf ist ein Prüfablauf für den noch offenen Laufwerksnachweis, keine bereits erfolgte Hardware-Abnahme.

## 3 Ganze Diskette sichern

Für eine 80-Spur-Diskette mit zwei Seiten:

```console
ufi read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.scp
```

Die Geometrie muss zum Medium passen. Für ein natives Archiv mit Original-Ticks und gespeicherten Protokollrahmen:

```console
ufi --integrity read-disk --drive a --tracks 80 --sides 2 -r 3 -o disk.ufi
ufi archive-check disk.ufi
```

`archive-check` prüft die Datei offline. Ein strukturell gültiges Archiv kann unvollständige Versuche enthalten; deren Status ebenfalls auswerten.

Weiter: [Lesen und Archivieren](Lesen-und-Archivieren) · [Dateiformate](Dateiformate) · [Fehlerhilfe](Fehlerhilfe).
