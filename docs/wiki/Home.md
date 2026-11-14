# Universal Flux Interface

**Sprache / Language:** [Deutsch](Home) | [English](Home-EN)

UFI liest und schreibt Disketten auf der Ebene ihrer magnetischen Flusswechsel. Die Headless-Platine arbeitet mit einem STM32H723 und benötigt keinen Raspberry Pi. [UnifiedFloppyTool](UnifiedFloppyTool) bietet eine grafische PC-Oberfläche mit eigener UFI-Anbindung; das Python-Werkzeug `ufi` übernimmt Befehle, Skripte, Analyse und Abbilddateien. Weitere Betriebsarten ermöglichen Greaseweazle-Kompatibilität, USB-Massenspeicher und Aufnahmen auf den internen Speicher.

**Dokumentationsstand: 14. November 2026, Platine v1.0, Firmware 1.18-dev.** Eine v1.0-Platine hat den USB-Bring-up bestanden. Lesen und Schreiben mit echten Laufwerken sowie LAN mit Kabel sind nach dem dokumentierten Prüfstand noch offen. Die [Statusseite](Projektstatus) erklärt die Nachweise.

## Einstieg

| Dein Vorhaben | Anleitung |
|---|---|
| Ein vorhandenes Board zum ersten Mal verbinden | [Schnellstart](Schnellstart) |
| Das PC-Werkzeug installieren | [Installation](Installation) |
| Die grafische Oberfläche verwenden | [UnifiedFloppyTool mit UFI](UnifiedFloppyTool) |
| Ein eigenes Hostprogramm entwickeln | [USB-Protokoll für Entwickler](USB-Protokoll) und [vollständige Referenz](USB-Protokoll-Referenz) |
| Platine und Anschlüsse kennenlernen | [Hardware](Hardware) und [Anschlüsse](Anschluesse) |
| Bauteile auf den Platinenbildern finden | [Beschriftete Platinenübersicht](Platinenuebersicht) |
| Eine neue Platine prüfen | [Inbetriebnahme](Inbetriebnahme) |
| Eine Diskette sichern | [Lesen und Archivieren](Lesen-und-Archivieren) |
| Ein Abbild zurückschreiben | [Schreiben und Prüflesen](Schreiben-und-Prueflesen) |
| Einen Fehler eingrenzen | [Fehlerhilfe](Fehlerhilfe) |
| UFI mit anderen Flux-Systemen vergleichen | [Vorteile und Vergleich](Vorteile-und-Vergleich) |

## Was UFI auszeichnet

UFI vereint Anschlüsse für mehrere Laufwerksfamilien, internen Aufnahmespeicher, Tasterbedienung, LAN und überwachte Laufwerksversorgung. Die native 275-MHz-Zeitbasis und die offenen Hardware-/Softwarequellen ergänzen diesen Aufbau für Analyse und eigene Erweiterungen. Welche Vorteile das gegenüber KryoFlux, Greaseweazle, SuperCard Pro und Applesauce+ bietet, erläutert [Vorteile und Vergleich](Vorteile-und-Vergleich) mit Quellen und dem jeweiligen Prüfstand.

## Funktionen nach Arbeitsgebiet

- **Aufnahme und Auswertung:** [Dateiformate](Dateiformate), [Analyse und Datenqualität](Analyse-und-Datenqualitaet), [Laufwerke](Laufwerke).
- **Betrieb:** [Betriebsarten](Betriebsarten), [SD und Standalone](SD-und-Standalone), [LAN](LAN), [Greaseweazle](Greaseweazle).
- **Rechnerfamilien:** [Amiga](Amiga), [Apple und Macintosh](Apple-und-Macintosh), [Commodore IEC](Commodore-IEC).
- **Entwicklung:** [Firmware bauen und flashen](Firmware), [Entwicklung und Tests](Entwicklung-und-Tests), [Wiki pflegen](Wiki-pflegen).
- **Nachschlagen:** [FAQ](FAQ), [Glossar](Glossar), [Projektstatus](Projektstatus).

## Technische Eckdaten

| Merkmal | Ausführung |
|---|---|
| Prozessor | STM32H723ZGT6, Cortex-M7, 550 MHz |
| Flux-Zeitbasis | 275 MHz, nominell etwa 3,64 ns je Timerschritt |
| Flux-Puffer | 8 MB QSPI-PSRAM; interner Ersatzpuffer bei Ausfall |
| USB | USB-C mit Full-Speed-PHY |
| Netzwerk auf v1.0 | W5500, UFI-v2-Sitzung über TCP |
| Interner Speicher | SD-NAND für Abbilder, Konfiguration und Berichte |
| Laufwerksfamilien | PC/Shugart, Amiga, Apple Disk II, Macintosh sowie Commodore über IEC |

Timerauflösung ist keine gemessene Gesamtgenauigkeit. Die Eignung eines konkreten Laufwerks und Mediums ergibt sich aus Anschluss, Einstellungen und den zugehörigen Prüfergebnissen.

## Projekt und Referenzen

[Quellcode](https://github.com/Axel051171/UniversalFluxInterface) · [Fehler melden](https://github.com/Axel051171/UniversalFluxInterface/issues) · [Verbindliche Protokollreferenz](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md) · [MIT-Lizenz](https://github.com/Axel051171/UniversalFluxInterface/blob/main/LICENSE)
