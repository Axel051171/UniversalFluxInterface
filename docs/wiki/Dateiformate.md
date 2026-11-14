# Dateiformate

**Sprache / Language:** [Deutsch](Dateiformate) | [English](File-Formats)

Die Dateiendung allein sagt nicht, wie viel von einer Diskette erhalten bleibt. UFI unterscheidet native Zeitdaten, konvertierte Fluxdaten, Bitzellen und dekodierte Sektoren.

| Format | Inhalt | Verwendung und Grenze |
|---|---|---|
| `.ufi` | Native Ticks, gespeicherte v2-Rahmen, Metadaten und Versuchszustände in SQLite | Originalaufnahme und Fehlernachweis; kein allgemeines Austauschformat und kein direkter `write`-Eingang |
| `.scp` | Fluxintervalle und Indexzeiten, mehrere Umdrehungen | Austausch und Rückschreiben; Host-Export quantisiert auf 25 ns |
| `.hfe` | Bitzellen; HFEv3 kann zusätzliche Zeit-/Steuerinformationen enthalten | Emulatoren und geeignete Schreibpfade; Export nicht mit nativer Fluxaufnahme gleichsetzen |
| `.img`, `.adf`, `.d81` | Dekodierte Sektoren für den jeweiligen Arbeitsablauf | Gut lesbare Nutzdaten; normale Sektorabbilder bewahren keine vollständige Fluxstruktur |
| `.d64`, `.d71` | Commodore-Sektorabbilder | IEC-Workflows für passende Laufwerksfamilien |
| Apple `.dsk`, `.po` | Dekodierte Apple-II-Sektoren | Sektorreihenfolge beachten |
| Mac `.dc42`, `.image` | DiskCopy-4.2-Struktur mit Daten und Tags | 400K-/800K-Workflows; Formatprüfung des jeweiligen Befehls beachten |

## SCP und native Ticks

UFI misst mit einer nominellen Zeitbasis von 275 MHz. Der Host-SCP-Writer rechnet auf 40 MHz bzw. 25 ns um. Er rundet absolute Zeitpunkte, damit sich die Rundung nicht bei jedem Intervall aufsummiert. Die ursprüngliche 275-MHz-Auflösung ist in SCP trotzdem nicht vollständig erhalten.

## HFE

`read-disk -o disk.hfe` bildet im normalen Host-Export die erste Umdrehung als Bitzellen ab. Bitrate und Kodierung können angegeben werden. Weitere Umdrehungen und rohe Timingunterschiede werden damit nicht als vollständige Rohaufnahme gespeichert.

HFEv3 besitzt zusätzliche Operationen für variable Bitrate, Indexpositionen und schwache Bereiche. Nicht jede solche Information ist durch beliebige Rückschreibpfade darstellbar. Ein abgelehntes Abbild nicht durch Umbenennen der Endung umgehen.

## Sektorabbilder

Ein dekodiertes Abbild kann trotz lesbarer Dateistruktur fehlende Sektoren enthalten. Standalone-Sektordumps können fehlende Sektoren mit Nullen auffüllen; die zugehörigen Berichte sind daher Teil des Ergebnisses. Eine erfolgreiche Dateierzeugung allein bestätigt keine vollständige Diskette.

## Format auswählen

- Für Erhaltung und Analyse: `.ufi` bzw. eine zusätzliche Roh-Flux-Aufnahme.
- Für Austausch mit Flux-Werkzeugen: `.scp`.
- Für ein bestimmtes Emulatorziel: dessen unterstütztes Bitzellen-/Sektorformat, zusätzlich zum Roharchiv.
- Für IEC-Sicherung: den zum Laufwerk passenden D64-/D71-/D81-Workflow mit Journal und Fehlerbericht.

Quellen: [SCP-Implementierung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/scp.py), [HFE-Implementierung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/hfe.py), [natives Archiv](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/archive.py).
