# HP 9895: Übertragene Erkenntnisse und Grenzen

Geprüft: brouhaha/hp9895fw, Commit
`45eaa2279bd92f47378f0b850cc27fd8d20a0fc1`.
Das Projekt enthält ROM, rekonstruiertes Z80-Listing und Controllerbeschreibung.
Keine allgemeine Projektlizenz gefunden; der GPL-Hinweis in der HTML-Datei gilt
für das eingebettete JavaScript. Kein ROM-, Disassembly- oder fremder Quellcode
wurde in die UFI-Firmware übernommen.

## Primärquellen und Gegenprüfung

- [Originales HP-Servicehandbuch, 09895-90030, Februar 1981](https://www.bitsavers.org/pdf/hp/disc/9895/09895-90030_9895A_Flexible_Disc_Memory_Service_Manual_198102.pdf),
  Abschnitte 2-11 bis 2-15: FM/MMFM-Regeln, Bitreihenfolge, Feldaufbau,
  Adressmarken und deren CRC-Einschluss. Abschnitt 2-4 beschreibt die analoge
  Margin-Erkennung, die wir nicht mit digitalen PLL-Abweichungen gleichsetzen.
- [Controllerbeschreibung im geprüften Repository](https://github.com/brouhaha/hp9895fw/blob/45eaa2279bd92f47378f0b850cc27fd8d20a0fc1/hp9895.html).
- [MAME HPI-Formatreferenz](https://github.com/mamedev/mame/blob/master/src/lib/formats/hpi_dsk.cpp)
  und [Controlleremulation](https://github.com/mamedev/mame/blob/master/src/devices/bus/ieee488/hp9895.cpp):
  Gegenprüfung der CRC-Bitreihenfolge, Adressmarken und HPI-Byteanordnung.
  MAME-Quellen wurden am 13.10.2026 gelesen; sie sind hier Referenz, keine
  eingebundene Bibliothek oder kopierte Implementierung.

## Eigenständige Umsetzung

Der gemeinsame begrenzte PLL-Eingang erhält eigene FM-/MMFM-Zustandsautomaten.
CRC und Clockregeln werden unabhängig ausgewertet. HP-ECC-Marken werden erkannt,
aber ohne implementierte ECC-Spezifikation nicht als dekodierte Daten ausgegeben.
Die native Analyse erhält Formatkennungen 3/4 und drei zusätzliche Diagnosebits
in einem versionierten Parameterblock. Das erweiterte Firmware-Prüflesen nutzt
dieselben neuen Decoder für Quelle und Rücklesen. Greaseweazle-Wireformat bleibt
unverändert. Details stehen in [USB_Protokoll.md](USB_Protokoll.md).

`legacy_fixture.h` erzeugt ausschließlich synthetische Testdaten anhand der
Formatregeln. Es verwendet weder den Produktionsdecoder noch dessen CRC-Routine.
Tests prüfen Payload, CRC-Fehler, reine Clockfehler, Größenbegrenzungen,
Sondermarken, Umdrehungsgrenzen und das Zurückweisen geänderter Daten.
Die synthetischen Spuren sind kein Ersatz für unabhängige reale Fluxaufnahmen.
Kein Hardwaretest, keine analoge Margin-Messung, keine Zusage elektrischer
Kompatibilität mit einem angeschlossenen 8-Zoll-Laufwerk.
