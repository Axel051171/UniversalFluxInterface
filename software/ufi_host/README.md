# ufi – PC-Tool für die UFI Flux Engine (UFI Headless)

Kommandozeilen-Tool (Protokoll UFI v2, Rückfall auf v1) über den USB-CDC-Port (unter Windows ohne Treiber, COM-Port wird per VID/PID `1209:4F54` gefunden).
Alternativ (Firmware ≥ 1.10, `UFI.CFG` `protocol=gw`): das Gerät spricht das Greaseweazle-Protokoll (PID `1209:4F57`, „gw-compat“) und wird mit den Greaseweazle-Werkzeugen (`gw read`, `gw write`, …) benutzt – dann nicht mit `ufi`.
Liest Spuren als Flux und speichert sie als **SCP** (SuperCard Pro) – Dekodierung/Konvertierung mit vorhandenen Werkzeugen (HxC, Greaseweazle `gw convert`, FluxEngine). Schreibt SCP-Images zurück.

```bash
pip install -e software/ufi_host[dev]
ufi info
ufi select a && ufi motor on && ufi recal
ufi read 0 0 -r 3                         # Zusammenfassung (Flux-Anzahl, RPM je Umdrehung)
ufi read-disk -o disk.scp --tracks 80 --sides 2 -r 3 --drive a
ufi read-disk -o disk.hfe --bitrate 250        # HxC HFE v1 (Umdrehung 0 als MFM-Bitzellen, Bitrate sonst aus den kürzesten Flussabständen); auch bei read -o; write/write-disk nehmen .hfe (eine Umdrehung wird vorab gemessen, Zellen auf 98,5 % skaliert, ±3 %)
ufi write-disk disk.scp --verify --drive amiga
ufi iec-reset && ufi iec-send 0x28 && ufi iec-recv
ufi iec-write game.d64 --verify            # Firmware ≥ 1.16: D64/D71/D81 über den Laufwerkscontroller (U2) auf eine formatierte Diskette; iec-read holt sie zurück, iec-status zeigt die DOS-Meldung
ufi bootloader                            # danach: dfu-util -a 0 -s 0x08000000:leave -D ufi_firmware.bin
```

Nur mit UFI v2 (Firmware ≥ 1.12):

```bash
ufi caps                                  # Board-Revision, Firmware, caps-Bits im Klartext
ufi files [PFAD]                          # Verzeichnis auf dem SD-NAND
ufi get DUMP0001.SCP [-o d.scp]           # Datei herunterladen (READ_FILE in 4-KB-Stücken)
ufi put lokal.bin NAME.BIN                # Datei hochladen
ufi rm NAME.BIN
ufi cfg                                   # UFI.CFG anzeigen
ufi cfg drive=amiga revs=5                # Zeilen setzen/anhängen, zurückschreiben, CFG_RELOAD
ufi events                                # alle Ereignisse einschalten und ausgeben bis Strg+C
ufi mode flux|sd|floppy|gw|composite                # USB-Betriebsart (Gerät meldet sich neu an)
ufi dump [--drive a --tracks 80 --sides 2 -r 3] | dump-status | dump-abort | copy
ufi power [MASKE]                         # Board-Status; Maske Bit0 FDD_5V, Bit1 FDD_12V
ufi erase SPUR SEITE                      # eine Umdrehung löschen (ERASE 0x23), z. B. vor einem Schreibtest
```

Laufwerksdiagnose (Firmware ≥ 1.13, caps-Bit 11):

```bash
ufi rpm [UMDREHUNGEN]                     # Drehzahl aus den Indeximpulsen: min/mittel/max, Impulsbreite
ufi monitor --seconds 120                 # Drehzahl live über Minuten (verkettete READs, eine Zeile je Umdrehung mit Abweichung und Balken) zum Einstellen eines Laufwerks; Strg-C beendet
ufi scan [pc|ds]                          # welche Laufwerke antworten (a/b/amiga/amiga2 bzw. ds0-ds3): TRK0, Diskette dreht, Schreibschutz
ufi index-sim 300|360|off [--pulse 2000]  # Indeximpulse auf J9 Pin 6 (Flippy-Disketten), ohne Argument: Abfrage
ufi index-sim 300 --internal [--no-pin]   # Firmware ≥ 1.14: Impulse gelten intern als Index (Lesen/Schreiben ohne Draht)
ufi read-disk -o d.scp --hard-sectors 16  # Firmware ≥ 1.14: hartsektoriert (10/16/32 Löcher), auch bei read/write/write-disk
ufi read 17 0 --quarter 2                 # Firmware ≥ 1.14: Apple-Viertelspur (17,5); auch seek/write --quarter 1-3
ufi read 17 0 --steps 50:17.1,100:17.2    # Firmware ≥ 1.14: Kopfschritte nach Zeitplan während des Lesens (Spiradisc); auch write
ufi write-limits                          # Firmware ≥ 1.15: was der Schreibpfad annimmt (Takt, max. Intervalle, 1,6–238 µs); ufi write prüft damit vor dem Senden
ufi write IMG SPUR SEITE --erase-rest     # Firmware ≥ 1.18: Spurrest hinter dem letzten Intervall bis zum Index löschen (Tor offen, keine Flusswechsel); Ende-Rahmen meldet die gelöschte Länge
ufi timing tg43=1                         # Firmware ≥ 1.16: 8-Zoll-Laufwerke, Pin 2 = TG43 (reduzierter Schreibstrom ab Spur 43); auch UFI.CFG tg43=1
ufi apple-gcr addr_prologue=d4aa96 flags=keep_bad_addr_checksum+any_track   # Firmware ≥ 1.18: Apple-II-5,25-Zoll-GCR-Decoder (Format 7) für Kopierschutz-Varianten; 'apple-gcr reset'
ufi tolerance keep_bad_header+data_without_checksum   # Firmware ≥ 1.18: Toleranzflags der Analyse-Decoder für IBM-MFM/FM, HP, Amiga, Commodore, Macintosh (Op 34); Befunde je Sektor bleiben sichtbar; 'tolerance reset'
ufi apple-stats                           # Firmware ≥ 1.18: Nibble-Statistik der letzten Format-7-Analyse (Op 35): Nibbles, Sync-Nibbles, Kopf-/Datenfelder, verwaiste Köpfe je Umdrehung; je Sektor Volume, Sync-Läufe, Kopf-Daten-Abstand, Befunde
ufi write-lock [engage]                     # Firmware ≥ 1.18: Sitzungs-Schreibsperre (Op 37) anzeigen / einschalten; danach Status 22 für jedes Disketten- und Laufwerksschreiben bis zum Reset des Boards, kein Ausschalten über USB
ufi apple-table [standard|HEX128]           # Firmware ≥ 1.18: 6-and-2-Symboltabelle des Format-7-Decoders (Op 36) für Disketten mit vertauschtem Alphabet; apple-gcr flags=five_and_three addr_prologue=d5aab5 liest 13-Sektor-Disketten (DOS 3.2)
ufi read-dsk -o game.dsk [--order prodos] [--tracks 35] [-r 2] [--flags …]  # Apple II 5,25 Zoll: Fluss lesen, auf dem Host 6-and-2 decodieren, .dsk (DOS-3.3-Reihenfolge) oder .po (ProDOS) schreiben; fehlende/schwache Sektoren werden gelistet
ufi extra density_us=1500                 # Firmware ≥ 1.18: zusätzliche Laufwerkszeiten (Umschaltwartezeit, Motorstart→Schritt, Schreibberuhigung, WGATE-Vor-/Nachlauf)
ufi profile [standard|konservativ|save]   # Firmware ≥ 1.18: wirksame Einstellungen des gewählten Laufwerks; Zeitsatz anwenden (konservativ = KryoFlux-Werte: Schritt 8 ms, Beruhigung 40 ms, Umkehr 38 ms); save merkt sie sich je Laufwerk in UFIPROF.BIN auf der SD (ufi rm UFIPROF.BIN vergisst alle)
ufi selftest                              # Firmware ≥ 1.14: Platinen-Selbsttest (Takt, PSRAM, SD, Versorgungen, USB-C, Laufwerksleitungen, IEC-Rückschleife, Index-Schleife), schreibt SELFTEST.LOG
ufi format 1440 --verify                  # Firmware ≥ 1.14: Diskette in der Firmware formatieren (1440 720 1200 360 dmf amiga 1581 st720 st800, ab 1.16 amigahd; --track T für eine Spur)
ufi analyze 0 0 -r 5                      # Firmware ≥ 1.14: Drehzahl, Abstands-Spitzen, Kodierung/Bitzelle, Spurlänge, schwache Bits, Index→erster Sektor
ufi dump --tracks 0                       # Firmware ≥ 1.14: Spurzahl automatisch (höchste erreichbare Spur); UFI.CFG track_search=1 für 40-Spur-Disketten
ufi rpm-select 360                        # Firmware ≥ 1.14: 3-Mode-Laufwerk 300/360 U/min (UFI.CFG rpm_line, rpm_360); Kontrolle: ufi rpm
ufi sd-write GAME.ADF                     # Firmware ≥ 1.16: Abbild vom SD-NAND auf die Diskette (SCP/IMG/ADF/D81, D64 über IEC); ohne PC: UFI.CFG button_a=write, write_file=GAME.ADF; Bericht WRITE.LOG
ufi dump --format img                     # Firmware ≥ 1.16: dekodierte Sektoren statt Flux nach DUMPnnnn.IMG (adf: .ADF); UFI.CFG format=img|adf für Taster A
ufi dump --format hfe                     # Firmware ≥ 1.17: HxC HFE v1 nach DUMPnnnn.HFE (Bitrate aus der Spur-0-Probe); sd-write NAME.HFE schreibt HFE zurück
ufi dump --drive iec --tracks 35          # Firmware ≥ 1.14: 1541 an J8 über den Laufwerkscontroller nach DUMPnnnn.D64 (+ .LOG mit Fehlerblöcken)
ufi iec-nib -o disk.g64 [--halftracks] [--tracks 41]   # Firmware ≥ 1.14: Rohspuren (GCR) per Laufwerkscode in eine G64, Kopierschutz/Halbspuren
```

## Erweiterungen in 1.18-dev: Composite, Jobs und IEC-Wiederaufnahme

Die Firmware bietet `protocol=composite` beziehungsweise `ufi mode composite`:
Greaseweazle auf CDC, UFI v2 auf einem zusätzlichen Bulk-Interface am selben USB-
Gerät. Physische Laufwerksaktionen sind gegenseitig gesperrt. Das CLI gibt Native-
Besitz am Ende frei; zusätzlich verfällt Native-Besitz nach 30 s Leerlauf.
Aktive Jobs und ausstehende Antworten sind davon ausgenommen; eine eigene Anwendung verwendet `Device2.close()` oder
`resource(False)`. GW gibt bei DESELECT/Watchdog frei. Die USB-/Laufwerksfunktion
ist bisher offline getestet; die fertige Platine muss noch qualifiziert werden.

```bash
pip install -e 'software/ufi_host[usb]'
ufi mode composite
ufi --port usb features
ufi --port usb diagnostics
ufi --port usb describe
ufi --port usb --integrity read 0 0 -r 3
ufi --port usb job-status
ufi --port usb release
ufi read-disk -o disk.ufi --tracks 80 --sides 2 -r 3   # natives Archiv (SQLite, nur v2): jeder CRC-geprüfte Rahmen wird vor dem Dekodieren gespeichert, Zeitstempel in voller 275-MHz-Auflösung; abgebrochene Spuren bleiben als unvollständig markiert
ufi archive-check disk.ufi                             # SHA-256 der Aufnahmen offline prüfen, unvollständige Aufnahmen melden
ufi iec-identify --device 8 --fingerprint
ufi iec-read disk.d64 --type auto --device 8 --retries 2
ufi iec-write disk.d64 --checkpoint disk.write.json --retries 2
ufi iec-write disk.d64 --checkpoint disk.write.json --resume
```

`--port usb:SERIENNUMMER` wählt eines von mehreren Geräten. Der Bulk-Adapter
beansprucht ausschließlich Interface 2. Auf Linux können passende udev-Rechte
nötig sein. Die Entwicklungs-PID 1209:4F58 und WinUSB-Enumeration sind noch nicht
für eine Veröffentlichung qualifiziert. Ohne Composite bleiben die CDC-Modi nutzbar.

`--integrity` aktiviert Flux-Job-ID, Paketnummern, Byteoffsets und Gesamt-CRC32;
WRITE beginnt erst nach geprüftem COMMIT. Es bestätigt Transportintegrität,
keine magnetische Inhaltsgleichheit. `--extended` aktiviert für WRITE die langen
und kurzen Rohintervalle des neuen Schreibpfads. Dieser erwartet fertige Host-
Zeitabstände und lässt sich nicht mit `--clamp`, Skalierung oder
Apple-Viertelspuren kombinieren. `--extended --verify` automatisiert einen
anschließenden Leseauftrag für IBM-MFM und Amiga-DD/HD: Referenzsektoren müssen
mit gültigen Prüfsummen und identischen Daten zurückkommen, bei Amiga inklusive Labels. Andere Formate werden bei angeforderter Prüfung
vor dem Schreiben abgewiesen. Dies prüft Sektorinhalte, keine Flux-Identität oder
Kopierschutzstrukturen. WRITE_LIMITS gilt weiterhin für den klassischen Schreibpfad.

Analysejobs der Firmware (ANALYSIS_JOB, USB_Protokoll 6.1) sind als Referenzabruf
enthalten: `ufi recognize SPUR SEITE [--expect MASKE --chn C H N --attempts N --retry]`
startet einen Job mit DETAIL (Op 1), wartet auf sein Ende (Op 2), holt den Erkennungsblock
(Abschnitt 15 über Op 3, jede Seite mit Kopf- und CRC32-Prüfung) und nennt Format,
Erkennungsflags, Ursachen und erwartete/gesehene Sektormasken. In der Bibliothek:
`analysis_start`, `analysis_status`, `analysis_wait`, `analysis_abort`, `analysis_section`,
`recognition_discovery`, `recognition`. Ein gefundener Sektor bestätigt weder Geometrie noch
Vollständigkeit; `GEOMETRY_UNCONFIRMED` bleibt bei Einzelspuraufträgen immer gesetzt.

IEC-Schreiben mit `--checkpoint` prüft jeden Sektor bytegenau zurück. `--resume`
prüft zuerst die Quellidentität und alle bereits bestätigten Sektoren auf der
Diskette. Die Diskette muss dieselbe bleiben; ohne bestätigte Sektoren ist ein
Wechsel nicht erkennbar. Ein neues Formatieren (`--new`) ist bei Resume verboten.
ROM-Erkennung meldet unbekannte ROMs ausdrücklich; eine ROM-Familie bestimmt nicht
sicher die physische Laufwerksrevision. Das Journal bleibt bei Fehlern erhalten.

Verbindliche Wire-Layouts und Grenzen stehen in
[USB_Protokoll.md, Kapitel 1 bis 9](../../docs/USB_Protokoll.md).

## Protokoll UFI v2 (Standard)

`ufi` spricht **UFI v2** – Spezifikation: [docs/USB_Protokoll.md](../../docs/USB_Protokoll.md), Kapitel 2 und 3 (Rahmen `55 AA`, type/cmd/seq/status/len, CRC-16/CCITT-FALSE, Daten-/Ende-Rahmen für Lesen und Schreiben, Ereignisse, Dateien). Implementierung: `ufi_host/protocol2.py` (`Device2`, `FrameParser`, `connect`).

- Beim Öffnen: Kanal-Reset (10000 Baud), dann `PING` als v2-Rahmen. Antwortet das Gerät nicht in v2 (alte Firmware), nimmt `ufi` das v1-Protokoll (unten) und weist auf ein Firmware-Update hin; v2-Befehle (`caps`, `files`, `cfg`, …) melden dann einen Fehler.
- Lesen: `READ` → Annahme, Daten-Rahmen mit dem Flusscode (275 MHz, wie v1-Stream), Ende-Rahmen mit Umdrehungszahl. SCP-Ausgabe identisch zum v1-Weg.
- Schreiben: `WRITE` mit `flux_count`/`byte_count`, dann Daten-Rahmen im Flusscode (≤ 4096 Byte je Rahmen), Ende-Rahmen nach dem Schreiben (bzw. Prüflesen mit `--verify`).
- `selftest` gibt es in v2 nicht (PSRAM-Test steht in `ufi info`); `rpm` nutzt ab Firmware 1.13 `DIAG_RPM` (Indeximpulse, Motor wird gestartet), davor liest es eine Umdrehung der aktuellen Spur (Motor muss laufen).

## Protokoll UFI v1 (alt, Firmware < 1.12; Stream-Lesen braucht eine Firmware mit `ufi_stream.c`)

- Host → Gerät: ein Paket ≤ 64 Byte je Befehl, `[cmd, args...]` (Codes in `ufi_host/protocol.py` = `ufi_command_t`).
- Gerät → Host: jede Nachricht beginnt mit `{u8 command, u8 status, u16 length}` + Payload. Status = Fehlercode aus `ufi_fixes.h` (Klartext in `protocol.ERRORS`).
- Lesen (`READ_TRACK` 0x20, Standard): Antwort, dann **während die Diskette dreht** `{0x2C, 0, len}`-Nachrichten mit einem fortlaufenden Bytestrom (verlustfrei, 275 MHz, ~2 Byte je Flusswechsel): `01–EF` = Delta 1 Byte, `F0–FC b` = Delta 240 + ((x−F0)<<8) + b, `FD u32` = Indexpuls (Ticks nach dem letzten Wechsel), `FE u32` = langes Delta. Zeit 0 = erster Index; Format in `firmware/src/ufi_stream.c`, Decoder `protocol.decode_stream`. Abschluss `{0x2D, status, 1}` + `[Umdrehungen]`.
- Lesen roh (`READ_TRACK_RAW` 0x21, `Device.read_track_raw`): erst nach der Aufnahme je Umdrehung `{0x2E, 0, 12}` + `{track, side, rev, flags, index_ticks, count}` + `count × u32` Zeitstempel (relativ zum Index), gleicher Abschluss.
- Lesen-Optionen (beide Lesebefehle): `[cmd, track, side, revs, flags, period_ms u16]`. `flags` Bit0 = **ohne Indexpuls** (Laufwerk/Diskette ohne Index, Flippy-Seite, hart sektoriert): Umdrehungsgrenzen alle `period_ms` (0 = 200 ms). `READ_TRACK` erlaubt bis 200 Umdrehungen (Ringpuffer), `READ_TRACK_RAW` bis 20.
- Schreiben: Antwort abwarten, dann `count × u32` Deltas als Rohdaten, Fertig-Meldung mit demselben Befehlscode.
- Schreiben kompakt (`WRITE_TRACK_C` 0x33): `[cmd, track, side, count u32, bytes u32, verify]`, dann `bytes` Bytes im Stream-Code von oben (ohne `FD`-Indexmarken), Fertig-Meldung `0x33`. Etwa halbe Upload-Menge.
- `DRIVE_TIMING` (0x17): 10 × u16 – `step_pulse_us, step_rate_us, settle_us, dir_change_us, side_settle_us, spinup_ms, select_settle_us, motor_off_s` (Motor aus nach Leerlauf, 0 = nie, Standard 30), `double_step` (1 = 40-Spur-Diskette im 80-Spur-Laufwerk), `precomp_ns` (0xFFFF = nach Datenrate, 0 = aus). Kürzere Nutzdaten überschreiben nur die vorderen Felder.
- Board v0.5: `BOARD_STATUS` (0x1B, optional `[maske]` Bit0 FDD_5V, Bit1 FDD_12V) → `{power u8, flags u8, i5_mA u16, i12_mA u16, board_id_mV u16}`, Flags: Bit0 WRITE-LOCK-Jumper, Bit1/2 Überstrom-Abschaltung 5 V/12 V, Bit3 SD-Speicher bereit, Bit4/5 Taster A/B. `PROBE_TRACKS` (0x1C) → höchste erreichbare Spur (fährt an den Endanschlag). `SD_INFO` (0x1D) → `{ready, status, card_type, bus_width, capacity_MB u32}`.
- Laufwerke (`SELECT_DRIVE` 0x10, `ufi select`): 1 `a` / 2 `b` (PC-Kabel mit Twist), 4 `amiga` (J7), 5 `iec`, ab Firmware 1.5 Shugart-Bus 6–9 `ds0`–`ds3` (gerades Kabel, Pin 10/12/14/6 – `ds3` nur mit JP1; Motor Pin 16 gemeinsam), ab Firmware 1.9 10 `amiga2` (zweites Amiga-Laufwerk an J7 Pin 9, Board v0.7 mit JP3), ab Firmware 1.11 3 `apple` / 11 `apple2` (Disk-II-Port J14/J15, Board v0.7; kein Index: Umdrehungen = 200-ms-Abschnitte). Laufwerk für Dump/USB-Floppy: `UFI.CFG` auf dem SD-NAND (`drive=…`, `tracks=`, `sides=`, `revs=`).
- Board v0.6, SD-NAND (4 GB, FAT32): `DUMP_START` (0x50, optional `[drive, tracks, sides, revs]`, Standard Laufwerk A, 80, 2, 3) liest die ganze Diskette ohne PC nach `DUMPnnnn.SCP` – dasselbe Dateiformat wie `ufi read-disk`. `DUMP_STATUS` (0x51) → `{state, error, track, side, file_no u16, fs_result, storage_ready}` (state 6 = fertig, 7 = Fehler; error 1 Speicher, 2 Laufwerk, 3 voll, 4 abgebrochen). `DUMP_ABORT` (0x52). `COPY_START` (0x54): Disk-zu-Disk-Kopie laut `UFI.CFG` (`copy_from`, `copy_to`, `tracks`, `sides`), Status über `DUMP_STATUS`. Zu jedem Dump entsteht `DUMPnnnn.LOG` (Qualitätsbericht) und ab Firmware 1.16 `DUMPnnnn.JSN` (Metadaten: Firmware, Board, Laufwerk, Format, Drehzahl, Fehler, Prüfsumme). Während eines Dumps beantwortet das Gerät nur Status-Befehle, alles andere mit BUSY. `USB_MSC` (0x53, optional `[mode]`: 1 = SD-Laufwerk (Standard, PID 0x4F55), 2 = **USB-Floppy** (PID 0x4F56: Diskette in Laufwerk A als USB-Laufwerk, Formate: PC 360K/720K/1.2M/1.44M, DMF 1.68M, ED 2.88M (nur lesen), Atari ST 9/10 Sektoren (11: nur lesen), Commodore 1581, Amiga 880K und ab Firmware 1.16 Amiga HD 1.76M – Nicht-PC-Formate als Sektorabbild in ST-/D81-/ADF-Reihenfolge; Spurpuffer mit Rückschreiben nach 1 s und Prüflesen)): Antwort, dann meldet sich das Gerät neu – zurück per Betriebsart-Schalter, Taster B ≥ 2 s (SD) oder Neustart. Am Gerät: Taster A ≥ 1 s = Dump, Taster B kurz = Abbruch, B ≥ 2 s = SD-Laufwerk an/aus (nur Schalter in Mitte); Betriebsart-Schalter an J13 (Pin 1 = USB-Floppy, Pin 2 = Mittelkontakt 3V3, Pin 3 = SD-Laufwerk, Mitte = Flux).
- `GET_INFO`: Firmware-Version mit Git-Revision und Build-Datum, Board inkl. Revision (BOARD_ID), MCU, PSRAM-Selbsttest.
- Diagnose: `SEEK_TEST` (0x1E) `[spur_a, spur_b, zyklen ≤ 50]` pendelt zwischen zwei Spuren (Schrittmotor/Kopf); `WRITE_PATTERN` (0x34) `[track, side, interval_ns u16, dauer_ms u16 ≤ 5000]` schreibt ein konstantes Flussintervall (Schreibkette am Oszilloskop) – nur bei laufendem Motor, ohne WRITE-LOCK-Jumper und ohne Schreibschutz. Beide antworten erst am Ende.
- Startmodi (Taster beim Einschalten halten): A = ROM-DFU-Bootloader (Rettung ohne funktionierende Firmware), B = Sicherheitsmodus, Laufwerksversorgung bleibt aus (ERR-LED an, `BOARD_STATUS` Flag Bit6), einschalten per `BOARD_STATUS [maske]`.
- Sicherheit in der Firmware: USB weg > 300 ms → Transfers abbrechen, Motor aus, Laufwerk abwählen, WGATE frei; Laufwerksversorgung > 1,5 A für 50 ms → Schiene aus.

Tests ohne Hardware (simuliertes v1- und v2-Gerät auf Byte-Ebene, inkl. geteilter Rahmen, CRC-Fehler, Dateien, Rückfall auf v1): `python -m pytest software/ufi_host/tests`.

## Amiga-Prüflesen und schwache Spuren

`ufi --port usb --extended write disk.scp 0 0 --verify` prüft jetzt auch
vollständige Amiga-DD/HD-Spuren einschließlich der 16 Labelbytes zurück.
`ufi amiga-quality disk.scp -o quality.json` analysiert vorhandene Rohfluxdaten
ohne Board. Für HD `--spt 22 --bitrate 500` (gegebenenfalls 250) angeben.
`ufi --port usb read 0 0 -r 3 -o track.scp --amiga-quality quality.json`
erzeugt Fluxabbild und Bericht mit Fehlern, Varianten und PLL-Rettungsversuchen.
Gültige widersprüchliche Inhalte werden nicht per Mehrheit akzeptiert.
[Bedienung, JSON-Bedeutung und Grenzen](../../docs/Amiga_Prueflesen.md).

## HFEv3 und lokale Firmwareanalyse

Native Generationen: `Device2.capability_matrix()` und `configuration()` liefern
Fähigkeiten sowie getrennte Konfigurations-/Medientoken. Die Matrix kommt ab
1.18-dev als Schema 2 (neun Zeilen): `rows` enthält die Formatzeilen 1–7 (7 =
Apple-II-GCR), `raw_flux` die formatunabhängige Zeile für Rohfluss-Lesen und
-Schreiben, `iec` den IEC-Blockweg; Schema 1 älterer Builds wird weiter gelesen
(`raw_flux`/`iec` dann `None`). Nach Unterstützungserkennung
über Matrixbyte23 kann `write_track(..., expected_config=..., expected_media=...)`
den Schreibstart an beide Token binden. Kein automatischer Rückfall auf ungeprüftes
Schreiben. `ufi_host.capabilities.guarded_analysis(..., media_generation=...)`
erzeugt den entsprechenden Analyse-Start. Layouts: USB-Protokoll 1.3/4.5.

Zusätzliche Offline-Prüfung: `python firmware/tools/check_static.py` benötigt Clang
und prüft zehn hardwareunabhängige Firmwaremodule. Die vollständige Testsuite bleibt
`python firmware/tests/check_all.py`; Hardwarequalifikation ist davon getrennt.

HFEv3-Import erhält variable Zellzeiten, Indexmarken und RAND-Bereiche.
Deterministische Spuren schreibt `write`/`write-disk` über das native UFI-Protokoll
mit Extended WRITE; `--verify` nutzt dabei Firmware-Prüflesen. Kein automatisches
Skalieren oder Clamping. RAND-/Mehrfachindex-Spuren bleiben lesbare Metadaten,
werden aber nicht automatisch physisch geschrieben. Ein Export als HFEv1 wird
für HFEv3-Objekte abgewiesen. SD-HFE in der MCU bleibt v1.

Die Firmware liefert mit TIMING oder WINDOWS zusätzlich 32 lokale Zeitfenster;
REV_COMPARE ergänzt Flankenunterschiede. Der Host kann Abschnitt 14 mit
`ufi_host.regions.decode_regions()` auswerten. Wire-Layout und Flags:
[USB-Protokoll, Abschnitt 5.5](../../docs/USB_Protokoll.md#55-lokale-zeitfenster-hfev3-und-getrennte-schwachstellenbefunde).
