# Amiga-Prüflesen und Spurqualität

Implementiert am 2026-10-13; ausschließlich offline geprüft.

Nachfolgend ist der Python-Hostpfad beschrieben. Zusätzlich stehen Analyse und
erweitertes Prüflesen jetzt direkt in der Firmware bereit: siehe
[USB-Protokoll, Abschnitt 5.2](USB_Protokoll.md#52-firmware-detailanalyse-acht-flag-bits-118-dev).
Andere Tools können diese Funktionen ohne den Python-Decoder verwenden.
Eigene Implementierung, keine Übernahme von ADF-Drive-/GPL-Quellcode.

## Schreiben und Prüfen

`--verify` ist eine Option des Unterbefehls:

```text
ufi --port usb --extended write disk.scp 0 0 --verify
ufi --port usb --extended write-disk disk.hfe --verify
```

Der Host erkennt IBM MFM oder Amiga trackdisk. Bei Amiga muss die Quelle
einen vollständigen Satz von 11 beziehungsweise 22 Sektoren einer Spur
enthalten. Spurkennung und Schreibziel müssen passen. Fehlende oder
widersprüchliche prüfsummengültige Quellsektoren verhindern WRITE.

Nach erfolgreichem WRITE folgen zwei Umdrehungen READ. Header- und
Datenprüfsumme müssen stimmen; Nutzdaten (512 Bytes), Label (16 Bytes)
und das Headerfeld Sektoren-bis-Gap werden mit der Quelle verglichen.
Eine gültige abweichende Variante führt auch dann zum Fehler, wenn eine
andere Umdrehung passt. Fehlende Sektoren, unerwartete Kennungen,
Abbruch oder Capture-Overflow gelten nicht als erfolgreicher Vergleich.

SCP- und HFE-Schreibbefehle übergeben dem Hostprüfer die Quellspurlänge.
Damit lassen sich im Quellabbild über die Indexgrenze geteilte Sektoren
zusammensetzen. Bei direkten API-Aufrufen unterstützt
`Device2.write_track(..., index_ticks=..., extended=True, verify=True)`
dasselbe Verhalten. Ohne Spurlänge wird eine unvollständige Quelle abgewiesen.
Die Spurlänge ist Host-Metadatum; das USB-Wire-Layout bleibt unverändert.

`WriteResult.host_verified_format` beschreibt den Prüfer,
`host_verified_sectors` die Anzahl und `readback_quality` den Amiga-Bericht.
Die Firmware erhält beim erweiterten Schreiben kein VERIFY-Bit.
Der klassische Firmwareprüfer bezieht jetzt ebenfalls die Labelbytes in
seinen CRC32-Inhaltsvergleich ein und verwirft gültige abweichende Duplikate.
Er bleibt eine Prüfung der dekodierten Referenzsektoren, kein Vollspurbeweis.

## Analyse ohne angeschlossenes Board

```text
ufi amiga-quality disk.scp --track 0 --side 0 -o quality.json
ufi amiga-quality hd.scp --spt 22 --bitrate 500 -o quality-hd.json
```

Bei HD-Aufnahmen eines langsam laufenden Amiga-Laufwerks gegebenenfalls
`--spt 22 --bitrate 250` verwenden. Die Analyse unterstützt 250, 300 und
500 kbit/s; Bitrate und erwartete Geometrie sind ausdrücklich einstellbar.
Der Offline-Befehl öffnet keine USB-Verbindung. Exitcode 1 bedeutet fehlende,
widersprüchliche oder unerwartete Sektoren oder einen Eingabefehler.

Bei einer neuen Aufnahme Rohflux und Bericht getrennt aufbewahren:

```text
ufi --port usb read 0 0 -r 3 -o track.scp --amiga-quality quality.json
```

JSON-Berichte werden ausschließlich als neue Dateien angelegt. Die Analyse
ändert keine Rohdaten. SCP bewahrt Flux mit seiner Zeitquantisierung;
HFE und ADF sind kein Ersatz für mehrere Rohflux-Umdrehungen.

## Bedeutung des JSON-Berichts (Schema 1)

Pro Umdrehung werden maximal drei PLL-Profile ausgewertet: Standard,
schnellere Frequenzanpassung und geringere Phasenanpassung. Initialperiode,
Frequenz- und Phasenteiler stehen im Bericht. Es erfolgen keine zusätzlichen
mechanischen Leseversuche und keine Schreibversuche. `adaptive=False` in
`amiga.analyze_capture()` beschränkt die Analyse auf das Standardprofil.

| Zustand je Sektor | Aussage |
|---|---|
| `missing` | Keine prüfsummengültige Variante gefunden |
| `conflicting` | Mindestens zwei unterschiedliche gültige Varianten; keine Mehrheitsentscheidung |
| `recovered` | Auf mindestens einer Umdrehung nur mit alternativem PLL-Profil gefunden |
| `intermittent` | Prüfsummenfehler beobachtet oder nicht auf jeder Umdrehung gefunden |
| `observed_once` | Nur eine Umdrehung ausgewertet, daher keine Stabilitätsaussage |
| `stable` | Auf mindestens zwei Umdrehungen jeweils im Standardlauf gleich gefunden |

Zusätzlich: Fehlversuche je PLL-Profil, Header-/Datenprüfsummenfehler,
abgeschnittene Sektoren, abweichende Spurkennungen, Variantenanzahl und
Intervallhistogramm mit 125-ns-Bins (letzter Bin sammelt längere Intervalle).
Sektoren über den Index hinweg verwenden nur tatsächlich aufeinanderfolgende
Umdrehungen. Die letzte Aufnahme wird nicht künstlich mit der ersten verbunden.
Ein am Aufnahmeende abgeschnittener Sektor kann daher als intermittierend
erscheinen; die Trunkierungszähler liefern den Kontext.

`complete` bedeutet vollständiger, widerspruchsfreier Sektorsatz, nicht
fehlerfreie oder stabile Magnetschicht. Schwache Bits durch Kopierschutz,
Medienfehler und elektrisches Rauschen lassen sich daraus nicht sicher
unterscheiden. Auch bei vollständig geretteten Daten bleibt diese Evidenz
sichtbar. GCR und absichtlich ungültige Prüfsummen sind noch keine unterstützten
Formate dieses automatischen Host-Prüfers.

## Offline-Nachweise und Grenzen

- `software/ufi_host/tests/test_amiga.py`: unabhängiger synthetischer Encoder,
  DD/HD einschließlich HD bei 250 kbit/s, nichtleere Labels, XOR-kompensierte
  Datenänderungen, fehlerhafte Prüfsummen, fehlende/wechselnde Sektoren,
  Indexversatz, Jitter, PLL-Rettung, USB-Schreibablauf und Offline-SCP-Befehl.
- `firmware/tests/test_amiga.c`: unabhängig erzeugte Bitfolgen prüfen den
  echten Firmwaredecoder und Labelvergleich. Der echte Encoder wird für
  297/300/303 RPM, beide Geometrien und Startzellen-Grenzen geprüft.
- Der Firmwareencoder legt den freien Gap vor die Sektoren und lässt bei
  ausreichendem Platz zwei Nullbytes dahinter. Seine vier Startzellen werden
  jetzt bei der Mindestlänge berücksichtigt.

Elektrische Schreibnaht, Ausgangspolarität, reale Drehzahlschwankungen und
Medienverträglichkeit müssen später am Board gemessen werden.

Formatreferenz: [AmigaOS: Floppy Boot Process and Physical Layout](https://wiki.amigaos.net/wiki/Amiga_Floppy_Boot_Process_and_Physical_Layout).
