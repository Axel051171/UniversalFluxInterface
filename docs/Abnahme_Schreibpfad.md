# Abnahme des Schreibpfads auf der UFI Headless v0.7

Stand: Firmware 1.15 (Audit F01–F07 behoben, DMA-Streaming, Verify-Stufen). Noch **nicht ausgeführt** –
alles unten ist am Gerät zu messen, bevor Schreibfunktionen als verlässlich gelten. Grundlage:
`Erkenntnisse/UFI_Firmware_2026-10-11` (Audit) und `Erkenntnisse/UFI_Weiterentwicklung_2026-10-11`
(Kandidat, Hardware-Testplan) im UnifiedFloppyTool-Repo, ergänzt um die Punkte der 1.15-Umsetzung.

Voraussetzungen: bestückte Platine mit bestandenem `ufi selftest`, Debugzugang, Logikanalysator an
WDATA (J6 Pin 22), WGATE (Pin 24), INDEX (Pin 8), zunächst **Loopback ohne beschreibbares Medium**
(WDATA/WGATE nur am Analysator), später Testmedien, die verloren gehen dürfen.

Je Messung festhalten: Boardrevision, Firmware-SHA-256 (`ufi info` Zeile 2 = Git-Revision),
Laufwerksmodell, Medium, Spur/Seite, Sollintervalle (Datei), gemessene Impulsfolge, Rücklesedaten,
Parameter (`ufi timing`, Skala, Kappen, pre/post_write), Ergebnis. Gegenprobe des Mediums auf einem
unabhängigen Controller (Greaseweazle, Kryoflux, Applesauce), wenn vorhanden.

## 1 Loopback am Logikanalysator (kein Medium)

| Test | Vorgehen | Erfolg |
|---|---|---|
| Grenzen | `ufi write-limits`; WRITE mit 439 / 440 / 65 536 / 65 537 Ticks | 439 und 65 537 → Status 18 vor Kopfbewegung, WGATE bleibt aus; 440 und 65 536 werden ausgegeben |
| Kappen | dieselben Daten mit `--clamp` | Ausgabe 440 bzw. 65 536, Ende-Rahmen `clamped` = 2 |
| Impulsfolge kurz | 3 / 100 / 4095 / 4096 / 4097 Intervalle | Impulszahl = `flux_count`, Abstände = Soll ± 1 Takt (3,6 ns), kein zusätzlicher Start- oder Endimpuls, WGATE fällt nach der letzten Periode |
| **Streaming** | 65 537 / 65 538 / 100 000 / 200 000 Intervalle (Index-Simulation intern, `--scale` ggf. < 1 damit es in eine „Umdrehung“ passt) | identische Folge wie gesendet, exakt n Impulse; Ende-Rahmen Status 0 |
| N−1/N/N+1 | lange Spur, letzte drei Intervalle unterschiedlich (z. B. 1000/2000/3000) | die letzten drei Abstände stimmen, danach kein Impuls (Pipeline „zwei Updates nach dem letzten DMA-Wert“) |
| Unterlauf | PSRAM-Zugriff künstlich bremsen (Debugger-Halt im HT-Interrupt) | WGATE fällt sofort, Ende-Status 6, kein Hängen |
| Index-Abbruch | Spur länger als die Index-Periode | WGATE fällt am Index, Status 19, keine weiteren Impulse |
| Gleichzeitige Ereignisse | Index-Simulation so stellen, dass der Index mit dem letzten DMA-Wert zusammenfällt | kein verlorener Impuls, kein falscher Erfolg, dokumentierte Reihenfolge |
| pre/post_write (GW-Modus) | `gw write` mit `--pre-write 200 --post-write 500` | erster Impuls 200 µs nach WGATE, WGATE 500 µs (± eine Periode) nach dem letzten Impuls aus |
| ERASE_FLUX | `gw erase --ticks …` | WGATE-Dauer = angeforderte Zeit (1…5000 ms) |
| Apple-Pfad | WRDATA (J14) im Toggle-Modus | Flankenabstände = Soll, /WRREQ nur während des Schreibens |

## 2 Fehlerfälle und Schutz

| Test | Erfolg |
|---|---|
| Ungültiges Präfix (`00`, `FD`, `FF`), Null-Intervall, Überzahl, zu wenige Daten | Ende-Status 10 sofort, WGATE nie an, nächster Auftrag sauber |
| USB-Abzug während Empfang / während Ausgabe | definierter Zustand: WGATE spätestens am nächsten Index aus, Motor aus nach Timeout, Neuverbindung ohne Reset |
| WRITE-LOCK-Jumper, WPROT des Laufwerks | Status 12, keine Schreibaktivität |
| Watchdog / Fault während des Schreibens | WGATE aus (Reset), `CRASH.LOG`, INFO meldet Ursache |
| Schreibschutz während des Schreibens gesetzt | Laufwerk ignoriert WGATE; Verify meldet Fehler statt Erfolg |

## 3 Mit Medium

| Test | Erfolg |
|---|---|
| 1.44M / 720K / Amiga formatieren (`ufi format … --verify`) | alle Sektoren CRC-gültig und `fill`-gleich; Gegenlesen am Fremdcontroller |
| WRITE mit `--verify` von echten Abbildern | Stufe 3 für MFM/Amiga; Stufe 2 nur bei nicht dekodierbaren Spuren |
| Verify-Negativprobe | Spur schreiben, Abbild mit anderen Daten gleicher Impulszahl schreiben lassen und gegen die erste Referenz prüfen → muss durchfallen |
| Jitter | Spur zehnmal lesen: Verify bleibt Stufe 3 |
| Beide Köpfe, außen/mitte/innen (0, 40, 79) | Nutzdaten, IDs, CRC gleich; Präkompensation an/aus vergleichen (innen) |
| Zeitskala | 360-U/min-Abbild mit `--scale 1.2` auf 300-U/min-Laufwerk; Rücklesen dekodierbar |
| Hartsektoriert, Apple-Viertelspuren, 3-Mode | jeweils mit Referenzmedium; Zähler, Phasen, Drehzahl (`ufi rpm`) gegen Soll |
| Dump/Kopie | SCP-Dump und `copy` A→B, Rücklesen beider Seiten identisch dekodierbar |

## 4 Offene Punkte aus der Recherche (Backlog, keine Zusage)

- Präkompensation je Kodierung (FM/MFM/GCR/M2FM), Kopf und Radius messen statt pauschaler Heuristik; Kalibrierung mit Rücklesen.
- Laufwerksprofile mit Herkunft und Datum (Schrittzeiten, Settle, Spin-up, höchste Spur) statt Blindwerte.
- Hard-Sector-Schreibfenster (Loch gegen Index, fehlende Löcher), Apple-Sync-Ausrichtung real prüfen.
- Lange fluxfreie Lücken (> 238 µs) ohne künstliche Impulse: Timer-Burst-DMA (ARR + CCR) als eigene Erweiterung.
- Archivmetadaten: Firmware-Revision, Board, Laufwerk, gemessene Drehzahl und Taktbasis zusätzlich zur Rohdatei.
- UFT-Anbindung über einen eigenen UFI-v2-Adapter (nicht den USB-Floppy-SCSI-Pfad).
