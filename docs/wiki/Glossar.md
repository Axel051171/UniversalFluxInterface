# Glossar

**Sprache / Language:** [Deutsch](Glossar) | [English](Glossary)

| Begriff | Bedeutung bei UFI |
|---|---|
| Fluxwechsel | Magnetischer Übergang, der am Laufwerksausgang als Impuls erscheint |
| Tick | Einheit der Timerzeit; nominell 1/275.000.000 s für UFI-Fluxdaten |
| Index | Referenzimpuls pro Umdrehung bzw. die durch den gewählten Modus erzeugte Grenze |
| Physischer Index | Vom Laufwerk oder passenden Sensor gemessene Winkelreferenz |
| Zeitscheibe | Zeitliche Unterteilung ohne feste physische Winkelreferenz |
| Lead-in / Vorlauf | Aufgenommene Fluxdaten vor dem ersten als Start gewerteten Index |
| MFM, FM, GCR | Familien von magnetischen Datenkodierungen |
| PLL | Verfahren zur Rekonstruktion eines Zelltakts aus Fluxintervallen |
| Bitzelle | Zeitabschnitt im rekonstruierten Kodiertakt |
| DMA | Hardwareübertragung zwischen Peripherie und Speicher |
| PSRAM | Externer Arbeitsspeicher für den Flux-Puffer |
| Ringpuffer | Speicher, dessen abgearbeitete Abschnitte erneut verwendet werden |
| Overcapture | Ein Capture-Ereignis wurde nicht rechtzeitig abgeholt; mindestens ein Ereignis kann fehlen |
| Underrun | Schreibausgabe erhält nicht rechtzeitig neue Daten |
| WGATE | Schreibfreigabesignal zum Laufwerk |
| Präkompensation | Gezielte zeitliche Verschiebung von Schreibübergängen |
| Verify | Prüfung mit verschiedenen Nachweisklassen nach einer Schreiboperation |
| Fingerabdruck | Verdichtete Merkmale einer Aufnahme; je nach Verfahren keine Inhaltsgleichheit |
| Generation | Sitzungsbezogener Änderungszähler für Konfiguration, Medium oder Aufnahme |
| Provenienz | Herkunfts- und Kontextangaben einer Aufnahme |
| CRC | Prüfsumme zur Erkennung bestimmter Datenfehler |
| SHA-256 | Hash für den Vergleich derselben Datei bzw. desselben Datenbestands |
| CDC | USB-Klasse für die serielle Hostverbindung |
| Composite | USB-Gerät mit mehreren Schnittstellen |
| IEC | Serieller Commodore-Laufwerksbus |
| SWD | Debug- und Programmierschnittstelle des STM32 |
| DFU | USB-Verfahren zur Firmwareübertragung |

Weiter: [Dateiformate](Dateiformate) · [Analyse und Datenqualität](Analyse-und-Datenqualitaet).
