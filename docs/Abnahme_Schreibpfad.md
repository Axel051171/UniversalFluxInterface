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
| Halbspur Shugart (1.18) | 96-tpi-Laufwerk mit Doppelschritt-Profil (`ufi timing double_step=1`), 48-tpi-Diskette: `ufi read 17 0 --quarter 2` und `ufi seek 17 --quarter 2`; Gegenprobe ohne Doppelschritt und mit `--quarter 1` | Kopf steht auf physischer Spur 35 (Schrittzähler am Logikanalysator: 35 Impulse ab Spur 0), Aufnahme zeigt die Übersprechmischung der Nachbarspuren 17/18; ohne Doppelschritt oder mit Viertel 1 Status 16, `ufi refusal` Feld 3 Grund 2 |
| 8-Zoll-Laufwerk (1.18) | SA800/BASF 6102 über 50-auf-34-Adapter (HLD auf MOTOR oder SELECT), Terminierung des Laufwerks belassen; `ufi profile 8zoll`, dann `ufi seek 76`, `ufi recal`, `ufi read 40 0`; Gegenprobe `ufi profile standard` | Mit 8zoll 77 saubere Schrittimpulse von 8 ms Abstand, Rückkehr auf Spur 0 ohne Wiederholung, STEP/DIR am Laufwerk unter 0,5 V Low-Pegel (74LS07 gegen 220 Ω); mit Standard (3 ms) verliert das Laufwerk Schritte oder meldet Spur 0 zu früh, genau das Fehlerbild, das den Unterschied belegt |
| Drehzahlprüfung (1.18, Op 41) | `ufi speed-check 20`, dann erweiterter WRITE mit Quell-Indexdauer ohne Prüflesen (Flags `0x60`, HFEv3-Spur per `ufi write` ohne `--verify`) bei 300 U/min und bei 360 U/min (`ufi rpm-select 360`, Kontrolle `ufi rpm`); Gegenprobe `ufi speed-check off` | bei passender Drehzahl wird geschrieben; bei 360 U/min endet der Auftrag mit Status 23, WGATE bleibt aus, kein Schritt, `ufi speed-check` nennt beide Dauern; mit Toleranz 0 wird trotz Abweichung geschrieben |

## 2 Fehlerfälle und Schutz

| Test | Erfolg |
|---|---|
| Ungültiges Präfix (`00`, `FD`, `FF`), Null-Intervall, Überzahl, zu wenige Daten | Ende-Status 10 sofort, WGATE nie an, nächster Auftrag sauber |
| USB-Abzug während Empfang / während Ausgabe | definierter Zustand: WGATE spätestens am nächsten Index aus, Motor aus nach Timeout, Neuverbindung ohne Reset |
| USB-Moduswechsel, Seriennummer | `ufi mode sd`, `ufi mode floppy`, `ufi mode gw`, zurück zu `ufi mode flux`; je Modus USB-Deskriptor auslesen (Windows: Geräte-Manager, Linux: `lsusb -v`) | PID wechselt 4F54 → 4F55 → 4F56 → 4F57 → 4F54, iSerialNumber bleibt in jedem Modus dieselbe MCU-UID; ein Host, der an der Seriennummer zuordnet, findet nach jeder Neu-Enumeration dasselbe Board |
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
| Schwache Zellen (1.18, `flags2` Bit3) | `ufi --extended write x.scp 40 0 --weak-jitter 2000 --weak-window A:B --weak-drop-window C:D`, danach `ufi analyze 40 0 -r 5` und `ufi weak --positions` | die Op-44-Fenster mit Differenz ≥ 2 liegen in A:B und C:D, außerhalb keine; zweimal mit demselbem `--weak-seed` geschrieben: `weak_applied` gleich und Rücklesen gleich; ohne Startwert: zwei Schreibvorgänge unterscheiden sich im Fenster |
| Rohprüfung (1.18, `flags2` Bit4) | GCR- oder Schutzspur (nicht dekodierbar) mit `ufi --extended write x.scp 10 0 --raw-check`, danach dieselbe Spur mit einem Abbild anderer Dichte (z. B. HD statt DD) und `--raw-check` | erster Lauf: `verify_level` 2, Op 32 `plausibel`; zweiter Lauf: `verify_level` 1 und Op 32 `abweichend` mit Status 0, mit `ufi raw-check --require` Status 6; Fingerabdruck einer Spur mit Weak-Markern bleibt `plausibel` |

## 4 Hardware-Qualifikation: welche Messung setzt welches Bit

Die Fähigkeitsmatrix (`0x0F [22]`, USB_Protokoll 1.3) meldet je Zeile in Byte 4
`hardware_qualified` mit den Pfadbits Bit0 Capture, Bit1 Decodierung, Bit2
Raw-Flux-Schreiben, Bit3 Inhalts-Prüflesen; dazu die Kopfflags
`CAPABILITY_UNCONFIRMED` (1) und `WRITE_MODE_UNQUALIFIED` (2). Alle Bits stehen in
1.18-dev auf 0 beziehungsweise die Flags auf gesetzt, weil noch keine Messung am Gerät
abgenommen ist. Diese Tabelle legt fest, welche Abnahme welches Bit freigibt. Ein Bit
gilt je Zeile (Format beziehungsweise Rohfluss/IEC), je Board-Revision und je
Firmware-SHA256; ein Mitschnitt der Messung (UFT `.ufec` aus dem CapturingTransport
oder `ufi`-Protokoll mit Rohdatei) gehört als Beleg in `Erkenntnisse/Abnahme/`.
Ablage und Meldung der gesetzten Bits durch die Firmware (Eintrag in `UFI.CFG`,
Anzeige in Op 22) sind noch nicht gebaut; bis dahin dokumentiert diese Tabelle nur den
Weg.

| Zeile, Bit | Messung (Abschnitte oben) | Freigabekriterium |
|---|---|---|
| 0 Rohfluss, Bit0 Capture | Abschnitt 3: `ufi read-disk` einer Referenzdiskette, drei Umdrehungen, beide Köpfe, Spur 0 und innerste Spur; Vergleich mit einem zweiten Lesegerät (KryoFlux/Greaseweazle) derselben Diskette | Intervallhistogramme deckungsgleich (Spitzen ± 2 %), Indexdauer ± 0,2 %, kein `transport_dma`/`buffer_full` im Aufnahmekontext (Abschnitt 4 des Jobergebnisses) |
| 0 Rohfluss, Bit2 Raw-Flux-Schreiben | Abschnitt 1: Loopback N−1/N/N+1, Streaming 65 537/100 000/200 000, Unterlauf-Provokation; Abschnitt 3: Rohspur zurückschreiben und mit Bit0-Aufnahme vergleichen | Loopback impulsgenau (± 1 Takt), Unterlauf endet mit Status 10 ohne WGATE-Rest; zurückgeschriebene Spur liest sich mit identischem Histogramm und Sektorsatz |
| 1–7 Format, Bit0 Capture | wie Zeile 0 Bit0 mit einer Diskette dieses Formats (IBM-MFM DD/HD/ED, Amiga DD/HD, FM, HP-MMFM, CBM-GCR 1541, Mac-GCR 400K/800K, Apple II 5,25″) | Decoder meldet alle Sektoren mit gültigen Prüfsummen in zwei Umdrehungen; `GEOMETRY_UNCONFIRMED` bleibt (Einzelspur), aber keine `INCOMPLETE`-Flags |
| 1–7 Format, Bit1 Decodierung | ANALYZE DETAIL und Prüflese-Referenz auf derselben Aufnahme gegen Host-Dekodierung (`ufi analyze`, `ufi recognize`, Host-Decoder derselben Rohdatei) | Sektorsatz, CHRN und Daten-CRC-Befunde identisch mit dem Host-Decoder; Apple und Mac zusätzlich Prüfsummen je Sektor |
| 1–6 Format, Bit2 Raw-Flux-Schreiben | Abschnitt 3: `ufi write-disk --verify` eines Abbilds dieses Formats, Rücklesen auf einem zweiten Laufwerk | Rücklesen byteidentisch; bei Amiga Labels eingeschlossen; Spur 0 und innerste Spur, beide Köpfe |
| 1–5 Format, Bit3 Inhalts-Prüflesen | Abschnitt 3 „Verify 1024-Byte-Sektoren“ und „Verify mit geändertem Byte“ für das Format (Op 40 Bericht) | gleiche Impulszahl mit falschem Inhalt fällt durch (Stufe < 3, Sektor „different“), unveränderte Spur erreicht Stufe 3 |
| 7 Apple-II-GCR, Bit2 | Rohspur einer Apple-Diskette am Apple-Port zurückschreiben (klassisches Backend), Rücklesen am Apple II oder zweiten Gerät | 16 Sektoren mit gültigen 6-and-2-Prüfsummen, Prolog-/Epilogmarken erhalten |
| 8 IEC, Bit0/Bit2/Bit3 | Abschnitt 3 „IEC schreiben“ mit `--verify`, danach `ufi iec-read`; Fehlerkanal 15 je Block | Rücklesen byteidentisch, `iec-status` `00, OK`; ein absichtlich beschädigter Block wird im Rücklesen gemeldet |
| Kopf `WRITE_MODE_UNQUALIFIED` | erlischt, wenn Zeile 0 Bit2 und mindestens eine Formatzeile Bit2+Bit3 abgenommen sind | beide Backends (klassisch, Extended) mit Loopback und Medium |
| Kopf `CAPABILITY_UNCONFIRMED` | erlischt, wenn für das angeschlossene Laufwerksprofil (Op 18) jede Zeile mit `zulässig ≠ 0` ihr Bit0 trägt | gilt nur für diese Kombination aus Board, Firmware und Laufwerksprofil |

Was eine Messung nicht freigibt: Decoderraten sind keine Schreibraten (1 Mbit/s
dekodieren bestätigt keinen ED-Schreibpfad), ein Loopback bestätigt keinen magnetischen
Inhalt, ein Format bestätigt kein anderes, und ein Laufwerk bestätigt kein zweites.

## 5 Offene Punkte aus der Recherche (Backlog, keine Zusage)

- Präkompensation je Kodierung (FM/MFM/GCR/M2FM), Kopf und Radius messen statt pauschaler Heuristik; Kalibrierung mit Rücklesen.
- Laufwerksprofile mit Herkunft und Datum (Schrittzeiten, Settle, Spin-up, höchste Spur) statt Blindwerte.
- Hard-Sector-Schreibfenster (Loch gegen Index, fehlende Löcher), Apple-Sync-Ausrichtung real prüfen.
- Lange fluxfreie Lücken (> 238 µs) ohne künstliche Impulse: Timer-Burst-DMA (ARR + CCR) als eigene Erweiterung.
- Archivmetadaten: Firmware-Revision, Board, Laufwerk, gemessene Drehzahl und Taktbasis zusätzlich zur Rohdatei.
- UFT-Anbindung über einen eigenen UFI-v2-Adapter (nicht den USB-Floppy-SCSI-Pfad).
