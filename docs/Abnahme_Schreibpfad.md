# Abnahme des Schreibpfads auf der UFI Headless v0.7

Stand: Firmware 1.18-dev (Audit F01–F07 behoben, DMA-Streaming, Verify-Stufen; 1.16: TG43, Dump dekodiert, Abbild von SD, IEC-Schreiben, Amiga HD; 1.18: Spurrest löschen, Sitzungs-Schreibsperre, Generationsblock, mediengebundener READ-Start). Noch **nicht ausgeführt** –
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
| TG43 (1.16) | `ufi timing tg43=1`, Seeks 42 → 43 → 42 | DENSITY (J6 Pin 2) fällt bei Spur ≥ 43 auf Low und geht darunter wieder hoch; mit `rpm_line=density` bleibt die Drehzahlwahl unberührt |
| **Spurrest löschen (1.18)** | `ufi write … --erase-rest` mit einer Spur von etwa 100 ms Länge, Index-Simulation 200 ms; dieselbe Spur ohne `--erase-rest` als Gegenprobe | mit Option: nach dem letzten Impuls **kein** weiterer Flankenwechsel auf WDATA, WGATE bleibt aktiv und fällt erst mit der nächsten Indexflanke (Abstand zum letzten Impuls = `erased_ticks` ± 1 Takt, hier ≈ 100 ms); ohne Option fällt WGATE direkt nach der letzten Periode und `erased_ticks` = 0. Zusätzlich: Spur länger als die Index-Periode mit Option → Status 19, `erased_ticks` 0, kein Nachlauf |
| Spurrest löschen, Abbruch (1.18) | wie oben, ABORT während des Nachlaufs | WGATE fällt sofort, Ende-Status 4, kein Impuls; danach normaler WRITE möglich |
| Spurrest löschen, Apple (1.18) | `flags2` Bit0 bei angewähltem Disk-II-Laufwerk | Status 11 bei der Annahme, /WRREQ bleibt inaktiv |
| **Startversatz (1.18)** | `ufi write … --start-offset 2750000` (10 ms), Index-Simulation 200 ms; Gegenprobe ohne Option | WGATE steigt 10 ms ± 0,5 µs nach der Indexflanke, der erste Impuls folgt mit dem ersten Intervall ab dort; `start_offset_applied` im Ende-Rahmen = gemessener Abstand (erwartet 2 750 000 plus wenige Takte Latenz); ohne Option steigt WGATE an der Indexflanke und das Feld ist 0. Mit `--erase-rest` zusätzlich: Tor bleibt bis zum nächsten Index offen; Spur länger als Rest der Umdrehung → Status 19 |
| Startversatz, Wartezeit (1.18) | Versatz 250 ms bei 200 ms Index-Simulation; ABORT während der Wartezeit | der zweite Index wird ignoriert, Tor öffnet 50 ms nach dem zweiten Index; nach ABORT öffnet das Tor nie, Ende-Status 4 |

## 2 Fehlerfälle und Schutz

| Test | Erfolg |
|---|---|
| Ungültiges Präfix (`00`, `FD`, `FF`), Null-Intervall, Überzahl, zu wenige Daten | Ende-Status 10 sofort, WGATE nie an, nächster Auftrag sauber |
| USB-Abzug während Empfang / während Ausgabe | definierter Zustand: WGATE spätestens am nächsten Index aus, Motor aus nach Timeout, Neuverbindung ohne Reset |
| WRITE-LOCK-Jumper, WPROT des Laufwerks | Status 12, keine Schreibaktivität |
| Watchdog / Fault während des Schreibens | WGATE aus (Reset), `CRASH.LOG`, INFO meldet Ursache |
| Schreibschutz während des Schreibens gesetzt | Laufwerk ignoriert WGATE; Verify meldet Fehler statt Erfolg |
| Sitzungs-Schreibsperre (1.18) | `ufi write-lock engage`, danach WRITE, ERASE, PATTERN, FORMAT, `sd-write`, `iec-write`, `gw write`, USB-Floppy-Modus schreiben, Taster-Schreiben | native Befehle: Status 22 bei der Annahme, WGATE nie an; GW-Modus: GW-Quittung „schreibgeschützt“; Massenspeicher: Schreibfehler am PC; Taster: keine Schreibaktivität. STATUS-Flagbyte Bit 7 gesetzt (`BoardStatus.session_locked`), `ufi write-lock` zeigt die Sperre getrennt vom Jumper |
| Sperre überlebt (1.18) | nach `engage`: `ufi mode gw` und zurück, USB abziehen und neu verbinden, ABORT | Sperre bleibt gesetzt (`[37]` → Bit 0); erst nach Reset-Taster/Spannung aus ist sie weg; Watchdog-Reset löscht sie ebenfalls (`CRASH.LOG` prüfen) |
| Sperre nach Annahme (1.18) | langen WRITE starten, während die Daten laufen `[37, 1]` setzen | Ende-Rahmen Status 22, WGATE ging nie an |
| Generationsblock (1.18) | `ufi read` und `ufi write` auf einer Spur, dann Diskette wechseln (oder `[16]`) und wiederholen | Start- und Endgeneration je Ende-Rahmen gleich; nach dem Wechsel andere Mediengeneration, `capture_id` steigt je READ; GUARDED_READ/GUARDED_WRITE mit der alten Generation → Status 21 vor jedem Daten-Rahmen |
| Wechsel während GUARDED_READ (1.18) | Diskettenwechselsignal während einer laufenden Aufnahme auslösen (Klappe öffnen) | Aufnahme endet mit Status 21, keine weiteren Daten-Rahmen, nächster Start mit neuer Generation läuft |

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
| Dump dekodiert (1.16) | `ufi dump --format img` von einer 1.44M-Diskette | `DUMPnnnn.IMG` = 1 474 560 Byte, identisch mit `ufi read-disk` + Host-Dekodierung; `.JSN` nennt Format, Drehzahl, `bad_tracks` 0 |
| Abbild von SD (1.16) | `ufi put GAME.ADF` → `ufi sd-write GAME.ADF`; dann `DUMP0001.SCP` zurückschreiben | `WRITE.LOG` ohne `BAD`; ADF am Amiga bootfähig; SCP-Rückschreiben: Rücklesen dekodiert gleich (Verify Stufe 3 per `ufi write --verify` als Gegenprobe) |
| IEC schreiben (1.16) | `ufi iec-write test.d64 --verify` auf eine frisch formatierte Diskette, dann `ufi iec-read` | Rücklesen byteidentisch, `iec-status` `00, OK`; mit Schreibschutz: Abbruch mit DOS 26 |
| Amiga HD (1.16) | `ufi format amigahd --verify` auf einer HD-Diskette im PC-HD-Laufwerk | 22/22 Sektoren je Spur; USB-Floppy-Modus zeigt 1,76 MB |
| HFE-Dump (1.17) | `ufi dump --format hfe` von einer 720K-Diskette, Datei per `ufi get`, dann `ufi read-disk -o x.hfe` derselben Diskette | `DUMPnnnn.HFE` startet in einem HxC-/Gotek-Emulator (FlashFloppy) und wird vom PC gelesen; `.JSN` nennt `rate_bps` 250000; `hxcfe`/HxC-Software zeigt 9 Sektoren je Spur ohne Fehler |
| HFE schreiben (1.17) | `ufi put x.hfe` → `ufi sd-write X.HFE` und `ufi write-disk x.hfe --verify` auf eine zweite Diskette | `WRITE.LOG` je Spur `ok (scale …)` zwischen 0,970 und 1,030 ohne `BAD`; Diskette im PC lesbar; Verify Stufe 3 bei `write-disk --verify` |
| Verify 1024-Byte-Sektoren (1.17) | 8 × 1024-Byte-Spur per `ufi write --verify`, danach dieselbe Spur mit einem geänderten Byte > 512 | zweiter Durchlauf endet mit Status 6, nicht mit Stufe 3 |

## 4 Offene Punkte aus der Recherche (Backlog, keine Zusage)

- Präkompensation je Kodierung (FM/MFM/GCR/M2FM), Kopf und Radius messen statt pauschaler Heuristik; Kalibrierung mit Rücklesen.
- Laufwerksprofile mit Herkunft und Datum (Schrittzeiten, Settle, Spin-up, höchste Spur) statt Blindwerte.
- Hard-Sector-Schreibfenster (Loch gegen Index, fehlende Löcher), Apple-Sync-Ausrichtung real prüfen.
- Lange fluxfreie Lücken (> 238 µs) ohne künstliche Impulse: Timer-Burst-DMA (ARR + CCR) als eigene Erweiterung.
- Archivmetadaten: Firmware-Revision, Board, Laufwerk, gemessene Drehzahl und Taktbasis zusätzlich zur Rohdatei.
- UFT-Anbindung über einen eigenen UFI-v2-Adapter (nicht den USB-Floppy-SCSI-Pfad).
