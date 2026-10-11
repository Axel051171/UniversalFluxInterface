# USB-Protokoll der UFI Flux Engine (UFI Headless)

Stand: Firmware 1.16, Board v0.7. Alle Mehrbyte-Werte **little endian**.

## 1 USB-Personalitäten

| Modus | VID:PID | Produktname | Klasse | Auswahl |
|---|---|---|---|---|
| UFI-Flux | 1209:4F54 | UFI Flux Engine | CDC (virtueller COM-Port) | Schalter J13 Mitte, `UFI.CFG protocol=ufi` (Standard) |
| Greaseweazle | 1209:4F57 | UFI Flux Engine (gw-compat) | CDC | Schalter Mitte, `UFI.CFG protocol=gw` |
| SD-Laufwerk | 1209:4F55 | UFI Flux Storage | Mass Storage | Schalter Pin 3, Taster B ≥ 2 s, Befehl `USB_MODE 1` |
| USB-Floppy | 1209:4F56 | UFI USB Floppy | Mass Storage | Schalter Pin 1, Befehl `USB_MODE 2` |

Nach jedem Wechsel meldet sich das Gerät neu an (ca. 200 ms getrennt). ACT blinkt die Betriebsart: 1× Flux, 2× SD, 3× Floppy, 4× Greaseweazle. Die Baudrate des COM-Ports ist bedeutungslos, außer **10000 Baud = Kanal zurücksetzen** (Greaseweazle-Konvention, gilt auch für UFI v2).

Im UFI-Flux-Modus versteht das Gerät zwei Protokolle: das alte **UFI v1** (ein Befehl je USB-Paket, siehe `software/ufi_host/README.md`) und **UFI v2** (Abschnitt 3). Ein USB-Paket, das mit `0x55 0xAA` beginnt (also der erste v2-Rahmen in einem Schreibaufruf; nicht innerhalb laufender v1-Schreibdaten), schaltet die Sitzung auf v2 um; sie bleibt v2 bis zum Kanal-Reset (10000 Baud) oder zur Neuanmeldung. Neue Software sollte nur v2 benutzen.

---

## 2 Greaseweazle-Modus (`protocol=gw`)

Unabhängige Umsetzung des Greaseweazle-Protokolls (Keir Fraser, `cdc_acm_protocol.h`, public domain). Die Greaseweazle-Werkzeuge (`gw info`, `gw read`, `gw write`, `gw erase`, `gw seek`, `gw delays`, `gw reset`) erkennen das Gerät am Produktnamen „gw-compat“ auch ohne `--device`.

### 2.1 Rahmen
- Befehl: `[cmd, len, args…]`, `len` = Gesamtlänge inkl. der zwei Kopfbytes. Befehle dürfen beliebig über USB-Pakete verteilt sein; nicht pipelinen.
- Antwort: `[cmd, ack]` + befehlsabhängige Daten. `ack` 0 = OK.
- Kanal-Reset: SET_LINE_CODING mit 10000 Baud verwirft Teilbefehle und bricht einen laufenden Lesevorgang ab.

| ack | Bedeutung |
|---|---|
| 0 | OK |
| 1 | unbekannter/ungültiger Befehl |
| 2 | kein Indeximpuls |
| 3 | Spur 0 nicht gefunden |
| 4 | Flux-Überlauf (Lesen) |
| 5 | Flux-Unterlauf (Schreiben) |
| 6 | schreibgeschützt (Diskette oder WRITE-LOCK-Jumper) |
| 7 | kein Laufwerk angewählt |
| 8 | kein Bus-Typ gesetzt |
| 9 | ungültige Laufwerksnummer |
| 10 | ungültiger Pin |
| 11 | ungültiger Zylinder |
| 12 | Speicher voll |

### 2.2 Befehle

| Code | Name | Argumente | Antwortdaten |
|---|---|---|---|
| 0 | GET_INFO | `idx` u8 | 32 Byte (siehe unten) |
| 2 | SEEK | `cyl` s8 oder s16 | – (Zylinder 0…83, negativ = ungültig) |
| 3 | HEAD | `head` u8 (0/1) | – |
| 4 | SET_PARAMS | `idx`=0, bis 8 × u16 | – |
| 5 | GET_PARAMS | `idx`=0, `n` u8 | `n` Byte |
| 6 | MOTOR | `unit` u8, `on` u8 | – |
| 7 | READ_FLUX | `ticks` u32, `max_index` u16, [`linger` u32] | Flussdaten bis Byte 0 |
| 8 | WRITE_FLUX | `cue_at_index` u8, `terminate_at_index` u8, [`hard_sector_ticks` u32 = 0] | nach den Daten: 1 Statusbyte |
| 9 | GET_FLUX_STATUS | – | (Status steht im ack) |
| 12 | SELECT | `unit` u8 | – |
| 13 | DESELECT | – | – |
| 14 | SET_BUS_TYPE | 0 keiner, 1 IBM-PC, 2 Shugart | – |
| 15 | SET_PIN | `pin` u8 (nur 2 = DENSITY), `level` u8 | – |
| 16 | RESET | – | – (Einschaltzustand) |
| 17 | ERASE_FLUX | `ticks` u32 (Löschdauer, ab 1.15 ausgewertet: 1…5000 ms) | nach dem Löschen: 1 Statusbyte |
| 18 | SOURCE_BYTES (1.14) | `len` u32, `seed` u32 | `len` Bytes aus dem Generator von `gw bandwidth` (`r&255`, dann `r = r>>1 ^ 0x80000062` bei gesetztem Bit 0, sonst `r>>1`) |
| 19 | SINK_BYTES (1.14) | `len` u32, `seed` u32, danach `len` Datenbytes | nach den Daten 1 Statusbyte: 0 = alle Bytes passten zum Generator, 1 = verfälscht |
| 20 | GET_PIN | `pin` u8 (8 INDEX, 26 TRK0, 28 WRPROT, 34 DSKCHG/READY) | `level` u8 (0 = low = aktiv) |
| 22 | NOCLICK_STEP (1.14) | – | – (ein Schritt nach außen auf Zylinder 0 für Flippy-Umbauten; ack 11 auf anderen Zylindern) |

Nicht unterstützt (ack 1): 1 UPDATE, 11 SWITCH_FW_MODE, 21 TEST_MODE.

**GET_INFO idx 0:** `fw_major, fw_minor` (= UFI-Firmware, z. B. 1.14), `is_main=1`, `max_cmd=22`, `sample_freq` u32 = **68 750 000**, `hw_model=0x55`, `hw_submodel=1` (gw zeigt „Unknown (0x5501)“), `usb_speed=0` (Full Speed), `mcu_id=0`, `mcu_mhz` u16 = 550, `sram_kb` u16 = 564, `usb_buf_kb` u16 = 8, Rest 0. **idx 1:** Bandbreite: `min_bytes, min_us, max_bytes, max_us` (je u32) der letzten SOURCE/SINK-Übertragung (eine Messung, min = max; vor der ersten ein Nennwert von 8 KB je 8 ms). **idx 7 / 8+n:** Laufwerk aktuell / Einheit n: `flags` u32 (Bit0 Zylinder gültig, Bit1 Motor an), `cyl` s32.

**Bus/Einheiten:** IBM-PC: 0 = Laufwerk A, 1 = B (Kabel mit Twist). Shugart: 0–3 = DS0–DS3 (gerades Kabel, DS3 nur mit JP1). Amiga-Port, IEC und Apple-Port sind im Greaseweazle-Modus nicht erreichbar.

**Parameter (idx 0, je u16):** select_delay µs, step_delay µs, seek_settle ms, motor_delay ms, watchdog ms (Standard 10000: ohne Befehl Motoren aus, abwählen), pre_write µs (ab 1.15: erster Flusswechsel so viel später als das Schreibtor am Index, höchstens 238 µs), post_write µs (ab 1.15: Schreibtor bleibt nach dem letzten Flusswechsel eine impulsfreie Periode dieser Länge an, höchstens 238 µs, ± eine Intervalllänge), index_mask µs (gespeichert, nicht ausgewertet: die Indexentprellung sitzt im Erfassungspfad).

### 2.3 Flusscode (Abtastrate 68,75 MHz = 275 MHz / 4)
| Bytes | Bedeutung |
|---|---|
| `1…249` | Flusswechsel, Abstand = Wert |
| `250…254, b` | Abstand = 250 + (Wert − 250) × 255 + b − 1 |
| `0xFF 1 N28` | Indeximpuls, N = Ticks ab dem letzten Wechsel (Cursor unverändert) |
| `0xFF 2 N28` | Lücke: N Ticks zum nächsten Wechsel addieren |
| `0xFF 3 N28` | (nur Schreiben) Lücke vorher mit Wechseln im Abstand N füllen |
| `0` | Ende des Datenstroms |

N28 = 4 Byte: `1|(N<<1)`, `1|(N>>6)`, `1|(N>>13)`, `1|(N>>20)`.

Lesen liefert `max_index − 1` volle Umdrehungen ab einem Indeximpuls (der erste Impuls kommt als Index mit N = 0). Schreiben beginnt immer am Index und endet am nächsten; die Vorkompensation macht `gw` selbst.

---

## 3 UFI v2

### 3.1 Rahmen (beide Richtungen)

```
Offset 0  0x55
       1  0xAA
       2  type     1 = Anfrage (Host)   2 = Antwort   3 = Ereignis   4 = Daten   5 = Ende
       3  cmd      Befehlscode (Antwort/Daten/Ende tragen den Code der Anfrage)
       4  seq      Laufnummer: Antwort/Daten/Ende wiederholen die seq der Anfrage; Ereignisse zählen selbst
       5  status   Anfrage: 0; sonst Statuscode (3.3)
       6  len      u16, Länge der Nutzdaten (0…4096)
       8  payload  len Byte
   8+len  crc      u16, CRC-16/CCITT-FALSE (Polynom 0x1021, Start 0xFFFF) über Byte 2 … 7+len
```

- Rahmen sind unabhängig von USB-Paketgrenzen. Der Empfänger sucht `55 AA`, liest den Kopf und prüft die CRC; bei Fehler verwirft er bis zum nächsten `55 AA`.
- Fehlerhafte Anfrage (CRC, Länge): Antwort `type=2, cmd=0xFF, seq=0, status=14`.
- Pro Anfrage genau **eine Antwort**. Lang laufende Befehle (Lesen, Schreiben) antworten sofort mit dem Annahmestatus und schließen mit einem **Ende-Rahmen** (type 5) ab; dazwischen kommen **Daten-Rahmen** (type 4).
- Flusskontrolle: Das Gerät hält den USB-Endpunkt an, wenn sein Empfangspuffer voll ist (NAK) – der Host schreibt einfach weiter.
- Ereignisse (type 3) kommen nur nach `EVENTS` und nie mitten in einem Rahmen.

### 3.2 Befehle

Laufwerksnummern (`drive`): 0 keins, 1 A, 2 B (PC-Kabel mit Twist), 3 apple (J14), 4 amiga (J7 DF1), 5 iec, 6–9 ds0–ds3 (Shugart), 10 amiga2 (J7 DF2, JP3), 11 apple2 (J15). Spuren immer physisch.

| Code | Name | Anfrage | Antwort-Nutzdaten |
|---|---|---|---|
| **System** | | | |
| 0x00 | PING | beliebig | dieselben Bytes |
| 0x01 | INFO | – | `proto` u8 = 2, `fw_major` u8, `fw_minor` u8, `board_rev` u8 (5/6/7, 0 = unbekannt), `caps` u32, `sample_hz` u32 = 275 000 000, `store_bytes` u32, `max_payload` u16 = 4096, dann ASCII-Text (Firmware, Git-Rev., Datum, PSRAM-Test) |
| 0x02 | STATUS | – | `drive, motor, wprot, track0, disk_changed, ready, track, side` (je u8), dann `board_status` (8 Byte, wie v1 0x1B) |
| 0x03 | RESET | – | Antwort, danach Neustart |
| 0x04 | BOOTLOADER | – | Antwort, danach ROM-DFU |
| 0x05 | USB_MODE | `mode` u8 (0 Flux, 1 SD-Laufwerk, 2 USB-Floppy, 3 Greaseweazle) | Antwort, danach Neuanmeldung |
| 0x06 | EVENTS | `mask` u32 (Bits = Ereigniscodes − 0x80) | – |
| 0x08 | WRITE_LIMITS (1.15) | – | `schema` u8 = 1, `flags` u8 (Bit0 Inhaltsprüfung, Bit1 lange Spuren per Streaming, Bit2 Kappen per Flag), `sample_hz` u32, `max_flux` u32 (Wörter des Flussspeichers), `min_ticks` u32 (440), `max_ticks` u32 (65 536) |
| 0x07 | SELFTEST (1.14) | – | `n` u8, dann n × (`id` u8, `status` u8: 0 ok, 1 Fehler, 2 nicht prüfbar, 3 Warnung, `value` u32) |
| **Laufwerk** | | | |
| 0x10 | SELECT | `drive` u8 | – |
| 0x11 | MOTOR | `on` u8 | – (wartet die Anlaufzeit ab) |
| 0x12 | SEEK | `track` u8 [, `quarter` u8 0–3 (Apple-Viertelspur, 1.14)] | – |
| 0x13 | RECAL | – | – |
| 0x14 | SIDE | `side` u8 | – |
| 0x15 | TIMING | – oder bis 11 × u16 (wie v1 0x17; nur die gesendeten Felder werden gesetzt) | aktuelle 11 × u16 (ab 1.16; zuvor 10) |
| 0x16 | LINES | `density` u8, `drate` u8 | – |
| 0x17 | CHECK_DISK | – | `changed` u8, `present` u8 |
| 0x18 | PROBE_TRACKS | – | höchste erreichbare Spur u8 |
| 0x19 | SEEK_TEST | `a` u8, `b` u8, `cycles` u8 | – |
| 0x1A | AMIGA_ID | – | ID u32 |
| 0x1B | DIAG_RPM (1.13) | `revs` u8 (0 = 5, max 50) | `revs` u8, `period_min` u32, `period_avg` u32, `period_max` u32, `pulse` u32 (Ticks 275 MHz), `rpm_x100` u16 |
| 0x1C | DRIVE_SCAN (1.13) | `flags` u8 (Bit0 = Shugart-Bus DS0–DS3 statt a/b/amiga/amiga2) | 4 × (`drive` u8, `found` u8: Bit0 TRK0 gefunden, Bit1 Indeximpulse, Bit2 schreibgeschützt) |
| 0x1D | INDEX_SIM (1.13) | – (Abfrage) oder `rpm` u16 (0 = aus, 200–400), `pulse_us` u16 (0 = 2000), `mode` u8 (1.14: Bit0 Ausgang J9 Pin 6, Bit1 intern; 0 = Bit0) | `rpm` u16, `mode` u8 (1.14) aktueller Zustand |
| 0x1E | SET_RPM (1.14) | – (Abfrage) oder `rpm` u16 (300/360, 0 = Abfrage) | `rpm` u16 (0 = nicht gesetzt), `line` u8 (0 keine, 1 DENSITY, 2 DRATE) |
| 0x26 | FORMAT (1.14) | `track, side` u8, `flags` u8 (Bit0 Prüflesen, Bit1 Amiga, Bit2 1581-Seitentausch), `rate_kbps` u16 (250/300/500/1000), `spt` u8 (Amiga 11, HD 22 ab 1.16), `gap3` u8 (0 = nach Layout), `fill` u8 | `flux_count` u32, `good` u8 (Sektoren mit gültiger CRC beim Prüflesen, 0xFF ohne) |
| 0x25 | ANALYZE (1.14) | `track, side` u8, `revs` u8 (0 = 3, max 8) | `revs` u8, `encoding` u8 (0 ?, 1 FM, 2 MFM, 3 GCR), `bitcell_ns` u16, `rpm_x100` u16, `peak_ns` 4 × u16, `index_ticks` u32, `transitions` u32, `bitcells` u32, `sectors` u8, `weak_count` u8, `first_sector_us` u32, `weak_mask` u64 (40 Byte) |
| 0x1F | STEP_SCHEDULE (1.14) | `flags` u8 (Bit0 Positionen in Viertelspuren), `n` u8 (≤ 32), n × (`at_ms` u16, `pos` u8) | – |
| **Fluss** | | | |
| 0x20 | READ | `track, side, revs` u8, `flags` u8 (Bit0 ohne Index, Bit1 hartsektoriert, Bit2 `track` in Viertelspuren), `period_ms` u16, bei Bit1 `sectors` u8 (1.14) | Annahme; dann Daten-Rahmen, Ende-Rahmen mit `revs` u8 |
| 0x21 | ABORT | – | – (bricht Lesen/Schreiben ab; der laufende Vorgang endet mit Ende-Rahmen Status 4) |
| 0x22 | WRITE | `track, side` u8, `flags` u8 (Bit0 Prüflesen, Bit1 hartsektoriert, Bit2 `track` in Viertelspuren, Bit3 Zeitskala, Bit4 Kappen erlaubt), `flux_count` u32, `byte_count` u32, dann in dieser Reihenfolge: bei Bit1 `sectors` u8, bei Bit3 `scale` u16 (1/1000, 500–2000) (1.14) | Annahme; dann sendet der Host Daten-Rahmen (type 4, cmd 0x22) mit zusammen `byte_count` Byte; Ende-Rahmen nach dem Schreiben mit `verify_level` u8, `clamped` u32 (1.15) |
| 0x23 | ERASE | `track, side` u8 | – (eine Umdrehung) |
| 0x24 | PATTERN | `track, side` u8, `interval_ns` u16, `duration_ms` u16 | – |
| **Commodore IEC** | | | |
| 0x30 | IEC_RESET | – | – |
| 0x31 | IEC_SEND | `byte` u8, `eoi` u8 | – |
| 0x32 | IEC_RECV | – | `byte` u8, `eoi` u8 |
| 0x33 | IEC_MEM (1.14) | `op` u8 (0 M-W, 1 M-R, 2 M-E, 3 DOS-Befehl), `addr` u16, dann Daten (M-W ≤ 32 Byte, DOS-Befehl: Text) bzw. `n` u8 (M-R) | M-R: `n` Byte |
| 0x34 | IEC_NIB (1.14) | `halftrack` u8 (2–84), `sync` u8, `mode` u8 (0 Stück ab Sync, 1 roh, 2 nur positionieren), `flags` u8 (Bit0 Laufwerkscode neu laden, Bit1 Stepper-Richtung umgekehrt), `device` u8 (0 = `iec_device`) | `status` u8 (0 ok, 1 kein Sync/keine Bytes, 2 Ursprung nicht gefunden, 3 roh), `synclen` u8 (≈ 11 µs), `maxlen` u16, `halftrack` u8, 512 Rohbytes |
| **Board** | | | |
| 0x40 | POWER | – oder `mask` u8 (Bit0 FDD_5V, Bit1 FDD_12V) | `board_status` 8 Byte |
| 0x41 | USB_POWER | – | `cc1_mv` u16, `cc2_mv` u16, `current_ma` u16 |
| 0x42 | SD_INFO | – | `present, status, card_type, bus_width` u8, `capacity_mb` u32 |
| **Dateien (SD-NAND, FAT32)** | | | |
| 0x50 | DIR | `start` u16, Pfad (ASCII, leer = Wurzel) | `count` u8, `more` u8, dann je Eintrag `size` u32, `attr` u8 (0x10 = Ordner), `nlen` u8, Name |
| 0x51 | READ_FILE | `offset` u32, `len` u16 (≤ 4096), Name | Dateiinhalt (kürzer am Dateiende) |
| 0x52 | WRITE_FILE | `offset` u32, `flags` u8 (Bit0 neu anlegen/kürzen), `nlen` u8, Name, Daten | geschriebene Bytes u16 |
| 0x53 | DELETE | Name | – |
| 0x54 | CFG_RELOAD | – | – (UFI.CFG neu einlesen) |
| **Ohne PC** | | | |
| 0x60 | DUMP_START | – oder `drive, tracks, sides, revs` u8 | – |
| 0x61 | DUMP_STATUS | – | `state, error, track, side` u8, `file_no` u16, `fs_result` u8, `storage_ready` u8 |
| 0x62 | DUMP_ABORT | – | – |
| 0x63 | COPY_START | – | – |

**caps (INFO):** Bit0 PSRAM ok, Bit1 SD-NAND bereit, Bit2 IEC, Bit3 Amiga, Bit4 Amiga2/JP3-fähig (v0.7), Bit5 Shugart-Bus, Bit6 Apple-Port (v0.7), Bit7 Sync-Sensor aktiviert (`apple_sync=1`), Bit8 Greaseweazle-Modus verfügbar, Bit9 Dateizugriff, Bit10 Ereignisse, Bit11 Laufwerksdiagnose und Indexsimulation (DIAG_RPM, DRIVE_SCAN, INDEX_SIM; Firmware 1.13), Bit12 FORMAT (1.14), Bit13 SELFTEST (1.14), Bit14 WRITE_LIMITS (1.15).

**Beschäftigt (Status 1):** Während eines Dumps/einer Kopie werden nur PING, INFO, STATUS, EVENTS, POWER, USB_POWER, SD_INFO, DUMP_STATUS und DUMP_ABORT ausgeführt; während READ/WRITE (bis zum Ende-Rahmen) werden Laufwerks- und Flussbefehle (0x10–0x1C, 0x1E, 0x20, 0x22–0x24, DUMP_START, COPY_START) abgewiesen; INDEX_SIM (0x1D) ist jederzeit erlaubt. WRITE wartet höchstens 1 s auf den Indeximpuls (sonst Ende-Status 4); schlägt das Positionieren vor dem Schreiben fehl, endet WRITE mit Status 3.

### 3.3 Statuscodes

| Status | Bedeutung |
|---|---|
| 0 | OK |
| 1 | beschäftigt (Dump läuft, Vorgang aktiv) |
| 2 | kein Laufwerk angewählt / Laufwerk fehlt |
| 3 | Positionieren fehlgeschlagen (Spur 0) |
| 4 | kein Indeximpuls / abgebrochen |
| 5 | Zeitüberschreitung |
| 6 | DMA-/Prüflese-Fehler |
| 7 | USB-Fehler |
| 8 | IEC: kein Gerät |
| 9 | IEC: keine Quittung |
| 10 | Puffer voll / Überlauf |
| 11 | nicht unterstützt (auf diesem Board) |
| 12 | schreibgeschützt |
| 13 | Speicher (SD-NAND/Dateisystem) |
| 14 | Rahmenfehler (CRC/Länge) |
| 15 | unbekannter Befehl |
| 16 | ungültige Argumente |
| 17 | Datei nicht gefunden |
| 18 | Flussabstand nicht darstellbar (1,6–238 µs; WRITE ohne Bit4), nichts geschrieben (1.15) |
| 19 | Spur länger als eine Umdrehung: der Index hat das Schreiben beendet, die Spur ist abgeschnitten (1.15) |

### 3.4 Flusscode (READ-Daten, WRITE-Daten), 275 MHz
Gleich dem v1-Stream (`ufi_stream.c`): Zeit 0 = erster Indeximpuls (bzw. Start ohne Index).

| Bytes | Bedeutung |
|---|---|
| `01…EF` | Flusswechsel, Abstand = Wert (Ticks à 3,64 ns) |
| `F0…FC b` | Abstand = 240 + ((Wert − 0xF0) << 8) + b |
| `FD u32` | Indeximpuls, u32 = Ticks nach dem letzten Wechsel (nur Lesen) |
| `FE u32` | Abstand = u32 |
| `00`, `FF` | reserviert |

Lesen endet nach `revs` Indeximpulsen (bzw. Zeitabschnitten); der Ende-Rahmen meldet die Zahl der gesendeten vollen Umdrehungen. Ohne Index (Apple ohne Sync-Sensor, `flags` Bit0) markieren `FD`-Einträge die Abschnittsgrenzen (`period_ms`, 0 = 200 ms).

**Apple mit Sync-Sensor** (J19, Hall-Sensor nach Applesauce-Art, `UFI.CFG apple_sync=1`, caps Bit7): Der Sensor liegt auf der INDEX-Leitung; READ liefert dann echte Umdrehungen ab dem Sensorimpuls (`FD` = Sensorimpuls), WRITE beginnt am Sensorimpuls – Spuren bleiben zueinander ausgerichtet. Ohne `apple_sync=1` arbeitet das Apple-Laufwerk mit Zeitabschnitten, auch wenn ein Sensor steckt.

### 3.5 Ereignisse (type 3, nach EVENTS)

| cmd | Ereignis | Nutzdaten |
|---|---|---|
| 0x80 | Diskette gewechselt | `drive` u8 |
| 0x81 | Taster | `button` u8 (0 A, 1 B), `long` u8 |
| 0x82 | Dump/Kopie-Fortschritt | wie DUMP_STATUS |
| 0x83 | Laufwerksversorgung abgeschaltet (Überstrom) | `board_status` |
| 0x84 | Betriebsart-Schalter bewegt | neuer `mode` u8 (vor der Neuanmeldung) |

### 3.6 Festlegungen im Detail

- **seq:** Der Host nummeriert Anfragen 1…255 (danach wieder 1); 0 ist der Rahmenfehler-Antwort vorbehalten. Höchstens **eine Anfrage gleichzeitig** offen. Ereignisse zählen ihre eigene seq ab 0 (Überlauf 255 → 0).
- **Zeitüberschreitung:** Kommt eine Antwort nicht (z. B. wegen CRC-Fehler verworfen), wartet der Host 1 s (Lesen/Schreiben: bis zum Ende-Rahmen, Zeitlimit großzügig) und setzt dann den Kanal zurück (10000 Baud). Eine Antwort mit `cmd=0xFF, seq=0, status=14` gehört zur gerade offenen Anfrage.
- **INFO:** `store_bytes` = Größe des Flussspeichers (8 MB PSRAM, sonst interner Ersatzpuffer), nicht der SD-NAND (dafür SD_INFO). Der Text danach ist ASCII, Zeilen durch `\n` getrennt, ohne abschließendes Nullbyte: Name/Version, Git-Stand, Build-Datum, PSRAM-Ergebnis, ab 1.14 die Reset-Ursache (`reset: …`).
- **board_status (8 Byte):** `power` u8 (Bit0 FDD_5V an, Bit1 FDD_12V an), `flags` u8 (Bit0 WRITE-LOCK gesteckt, Bit1/2 Überstrom 5 V/12 V ausgelöst, Bit3 SD-NAND bereit, Bit4/5 Taster A/B gedrückt, Bit6 Sicherheitsmodus), `i5_ma` u16, `i12_ma` u16, `board_id_mv` u16.
- **READ:** `revs` 1…200. Der Ende-Rahmen ist maßgeblich: Nur so viele Umdrehungen gelten, wie er meldet (bei Überlauf können `FD`-Marken fehlen oder überzählig sein). Ende-Status 4 = kein Index **oder** per ABORT abgebrochen; 10 = Überlauf (Host las zu langsam).
- **WRITE_LIMITS (1.15):** reine Abfrage (auch während eines Dumps), Nutzdaten müssen leer sein (sonst 16). Der Host (`ufi write`, `write-disk`) holt die Grenzen einmal je Verbindung und weist Aufträge lokal ab, die die Firmware mit Status 10 oder 18 ablehnen würde – mit Spur, Index und Wert; mit Kappen-Flag entfällt die Intervallprüfung. Ältere Firmware ohne Bit 14: keine Vorprüfung.
- **WRITE Index während des Schreibens (1.15):** Kommt der nächste Indeximpuls, bevor die letzte Periode ausgegeben ist, fällt das Schreibtor sofort und der Auftrag endet mit Status 19 (zuvor galt dieser Sicherheitsstopp als Erfolg). Die Spur war länger als eine Umdrehung; Abhilfe: Zeitskala < 1 oder weniger Flusswechsel.
- **WRITE Bereichsprüfung (1.15):** Nach Zeitskala und Präkompensation muss jede Periode 440…65 536 Takte (1,6…238 µs) betragen. Liegt eine außerhalb, wird der Auftrag **vor** Kopfbewegung und Schreibtor mit Ende-Status 18 abgewiesen; nichts auf der Diskette ändert sich. Mit `flags` Bit4 werden solche Perioden stattdessen auf die Grenze gesetzt, und der Ende-Rahmen meldet ihre Zahl in `clamped`. Firmware ≤ 1.14 kappte still. Der Greaseweazle-Modus kappt weiterhin (er hat keinen Rückkanal für eine Abweisung).
- **WRITE Ende-Nutzdaten (1.15):** `verify_level` u8: 0 keine Aussage, 1 Schreiben abgeschlossen (Timer/DMA ohne Fehler), 2 Rücklesen plausibel (nur Zahl der Flusswechsel ±5 %), 3 Inhalt geprüft, 4 nicht prüfbar; `clamped` u32. Stufe 3: Die Firmware dekodiert vor dem Rücklesen die gepackte Spur selbst (IBM-MFM, sonst Amiga; Datenrate 250/300/500/1000 kbit/s aus dem kürzesten häufigen Abstand) und merkt sich Sektorkennung und CRC-32 der 512 Datenbytes (bis 64 Sektoren). Dann liest sie zwei Umdrehungen zurück: Alle Referenzsektoren wieder mit gültiger CRC und gleichen Daten gefunden → 3 (Jitter stört nicht, die Zahl der Flusswechsel ist dann unerheblich); ein fehlender oder veränderter Sektor → Ende-Status 6. Spuren ohne dekodierbare Sektoren (GCR, Schutzspuren, nur Flux) erhalten höchstens Stufe 2 (Zahl der Flusswechsel ±5 %, mindestens 100), sonst Status 6 – „verifiziert“ heißt das nicht. Der Ende-Rahmen trägt die Nutzdaten auch bei Fehlerstatus; Firmware ≤ 1.14 sendet keine.
- **WRITE Zeitskala (Bit3, 1.14):** Die Firmware multipliziert jeden Flussabstand mit `scale`/1000 (Rest wird mitgeführt, kein Drift), bevor Präkompensation und Grenzen greifen. Damit lassen sich Abbilder mit anderer Drehzahl auf einem Laufwerk mit fester Drehzahl schreiben (Faktor = Drehzahl der Quelle / Drehzahl des Ziels, z. B. 360-U/min-Abbild auf 300-U/min-Laufwerk: 1,2) und die Zonen von Macintosh-400K/800K-Disketten (variable Drehzahl) auf einem PC-Laufwerk nachbilden (Faktor je Spurzone). Außerhalb 0,5…2,0 Status 16.
- **WRITE-Daten (1.15):** Zulässig sind nur die Codes `01…EF`, `F0…FC b` und `FE u32` (u32 ≠ 0); `00`, `FD` (Indexmarke) und `FF` sowie mehr Übergänge als `flux_count` beenden den Auftrag **sofort** mit Ende-Status 10, weitere Daten-Rahmen dieser seq werden verworfen (Firmware ≤ 1.14 sammelte danach weiter und konnte dabei Speicher überschreiben). `flux_count` ist nur durch den Flussspeicher begrenzt (Status 10 in der Annahme-Antwort): Die Firmware spielt die Perioden aus einem Ring von zwei 2048er-Hälften in SRAM2, den die DMA-Halb-/Vollinterrupts aus der gepackten Spur nachladen (ein DMA-Transfer kann höchstens 65 535 Perioden; Firmware ≤ 1.14 übergab längere Spuren trotzdem einem einzigen Transfer und schnitt sie damit ab). Der Stopp nach der letzten Periode wird gegen den DMA-Zähler ausgezählt; kommt ein Nachladen zu spät (Unterlauf), endet der Auftrag mit Status 6 und das Schreibtor fällt sofort ab.
- **WRITE:** Der Host wartet die Annahme-Antwort ab und sendet erst dann die Daten-Rahmen (je ≤ 4096 Byte Nutzdaten, beliebig aufgeteilt, zusammen genau `byte_count`). Ende-Status: 0 OK, 12 schreibgeschützt, 4 kein Index (Laufwerk mit Index), 6 Prüflesen fehlgeschlagen (`flags` Bit0), 10 Daten fehlerhaft/zu viele Wechsel.
- **Dateinamen:** FAT 8.3 (keine langen Namen), Großbuchstaben, Pfadtrenner `/` erlaubt (z. B. `DUMPS/DUMP0001.SCP`), höchstens 64 Zeichen.
- **READ_FILE:** Weniger Bytes als angefordert (auch 0) = Dateiende.
- **WRITE_FILE:** `flags` Bit0 legt die Datei neu an bzw. kürzt sie auf 0 (auch mit 0 Datenbytes). Weniger geschriebene als gesendete Bytes = Speicher voll; die Antwort trägt dann Status 13.
- **EVENTS:** Unbekannte Bits in `mask` werden ignoriert; `mask = 0` schaltet alle Ereignisse ab.
- **DUMP_START:** Nutzdaten leer (Werte aus UFI.CFG) oder genau 4 Byte.
- **UFI.CFG:** Textdatei, Zeilen `schlüssel=wert` (Schlüssel klein), `#` beginnt einen Kommentar, CRLF; unbekannte Schlüssel werden ignoriert. Schlüssel: `drive, tracks, sides, revs, button_a, copy_from, copy_to, protocol, apple_sync, index_sim, index_sim_mode` (`index_sim=300|360` schaltet die Indexsimulation beim Start ein, 1.13; `index_sim_mode=pin|internal|both`, 1.14), `rpm_line, rpm_360, rpm` (3-Mode-Laufwerke, 1.14), `iec_device`, `track_search`, `iec_type`, `double_step`, `auto_dump` (1.14), `tg43` (1.16); `tracks=0` = automatisch.
- **IEC_NIB (1.14, 1541-Rohspuren):** Die Firmware lädt beim ersten Aufruf (oder mit Bit0) ein 6502-Programm (`firmware/tools/iec_nib.s`, 6 + 32 Byte je M-W) nach $0310 im Laufwerk, schreibt die Parameter nach $0300 (Halbspur, Sync-Index, Modus, Phasenrichtung), startet es mit M-E und wartet bis zu 6 s, bis das Laufwerk wieder auf ATN antwortet (es läuft mit gesperrten Interrupts). Das Programm fährt den Kopf in Halbschritten (eigener Positionszähler $0308, den der Host nach der Kalibrierung setzt; 0 = nicht bewegen), setzt die Dichte-Bits der Spur, sucht den Spurursprung (Sync nach der längsten Strecke ohne Sync, also der Header-Sync von Sektor 0), überspringt `sync` Syncs, misst die Sync-Länge und liest 512 Bytes; während eines Syncs liefert es je Bytezeit ein $FF, Syncs erscheinen also als $FF-Folgen mit ungefährer Länge. Modus 1 liest 512 Bytes ohne Bezug (unformatierte Spuren), Modus 2 positioniert nur. Die Antwort kommt aus $0304 (Status, Sync-Länge, längste Strecke, Zähler) und $0600–$07FF; $0700 ist der BAM-Puffer des DOS, der Host sendet zum Schluss `I0`. Das Zusammensetzen der Stücke (alle 2 Syncs eines, Ausrichtung an den $FF-Folgen, Ende beim Wiederauftauchen des Ursprungs) und die G64-Ausgabe macht `ufi iec-nib`; Dauer etwa 10–20 s je Spur. Kein Bitzellen-Timing, schwache Bits nur über Mehrfachlesen.
- **IEC-Dump (1.14):** DUMP_START mit `drive` = 5 (iec), per UFI.CFG `drive=iec` oder Taster A, liest eine 1541/1571/1581-Diskette über den Laufwerkscontroller (Kanal 2 „#“, `U1`-Blocklesen, Gerät `iec_device`, Standard 8). Laufwerkstyp aus `UFI.CFG iec_type` (1541/1571/1581) oder bei `auto` aus der Einschaltmeldung nach einem Bus-Reset („73,CBM DOS V2.6 1541“, „V3.0 1571“, „V10 1581“; andere Meldungen → 1541): 1541 → `DUMPnnnn.D64` mit `tracks` 35 (683 Blöcke, 174 848 Byte) oder 40 (768 Blöcke; andere Werte gelten als 35); 1571 → `.D71` (70 Spuren, 1366 Blöcke, nach `U0>M1`; `tracks=35|40` liest nur Seite 1 in D64-Geometrie); 1581 → `.D81` (80 Spuren × 40 Sektoren, 819 200 Byte). `sides`/`revs` werden ignoriert. Nicht lesbare Blöcke stehen als Nullen in der Datei und mit Spur/Sektor in `DUMPnnnn.LOG`. DUMP_STATUS meldet in `track` die 1541-Spur (1–40), `side` 0. Dauer einige Minuten (seriell, etwa 0,3–0,6 s je Block); Abbruch wie gewohnt (DUMP_ABORT, Taster B). Kein Flux: Kopierschutz und schwache Bits sind so nicht erfassbar.
- **FORMAT (1.14):** Die Firmware erzeugt die Spur selbst (Encoder des USB-Floppy-Modus): IBM-MFM mit 512-Byte-Sektoren 1…`spt` (IAM, Standard-Gaps; Gap 3 wird gekürzt, wenn die Spur nicht reicht) oder Amiga-Trackdisk (Bit1; 11 Sektoren bei 250 kbit/s, ab 1.16 auch HD mit 22 Sektoren bei 500 kbit/s), Datenbytes = `fill`. Die Umdrehungszeit wird je Motorlauf einmal gemessen (zwei Indexabstände, 60 s gültig) und die Spur zu 98 % gefüllt. Geschrieben wird ab Index mit Präkompensation; mit Bit0 folgt ein Prüflesen über zwei Umdrehungen (`good` zählt nur Sektoren mit gültiger CRC **und** Datenbytes = `fill`). Blockierend (bis etwa 2 s je Spur). Status 2 ohne Laufwerk/IEC, 16 Apple oder ungültige Werte (Rate, `spt` 1–36, Amiga ≠ 11/22), 12 schreibgeschützt, 10 Sektoren passen nicht in die Umdrehung, 4 kein Index. Nützliche Layouts: 1.44M 500/18, 720K 250/9, 1.2M 500/15, 360K 300/9 (mit `double_step`), DMF 500/21, Amiga 250/11, Amiga HD 500/22, 1581 250/10 + Bit2.
- **ANALYZE (1.14):** liest `revs` Umdrehungen blockierend (kein Datenstrom, bis 4 s) und wertet aus: Umdrehungszeit/Drehzahl, Flusswechsel, Histogramm der Abstände in 100-ns-Klassen mit bis zu vier Spitzen (lokale Maxima über 2 % der Wechsel), daraus Kodierung und Bitzelle: Verhältnis 1:1,5 (4/6/8 µs) = MFM bzw. an Apple-Laufwerken GCR, Bitzelle = halbe erste Spitze; 1:2 mit dritter Spitze ≈ 3 = C64-GCR, Bitzelle = erste Spitze; 1:2 ohne = FM, Bitzelle = doppelte erste Spitze. `bitcells` = Umdrehungszeit / Bitzelle (Long-Track-Erkennung). Schwache Bits: die Umdrehung wird in 64 Zeitfenster geteilt; weicht die Zahl der Flusswechsel eines Fensters zwischen den Umdrehungen um ≥ 3 ab, gilt es als schwach (`weak_mask` Bit w). Für MFM werden die Sektoren von Umdrehung 0 dekodiert (`sectors` = gültige CRC) und die Zeit vom Index bis zur ersten gültigen Sektor-ID gemeldet (Spurausrichtung, radialer Versatz des Laufwerks). Status 2 ohne Laufwerk, 4 ohne Index, 5 bei Zeitüberschreitung. `ufi analyze 0 0 -r 5`.
- **Dump-Metadaten (1.16):** Zu jedem Dump entsteht neben Abbild und `.LOG` die Datei `DUMPnnnn.JSN` (JSON; 8.3-Name, daher `.JSN`): `file`, `format` (scp/d64/d71/d81, später img/adf), `firmware`, `git`, `board` (5/6/7), `drive`, `sample_hz`; bei Flux-Dumps `tracks`, `sides`, `revs`, `double_step`, `rpm` (Mittel der Spuren), `encoding` (mfm/amiga/unknown aus der Qualitätsprobe), `rate_bps`, `spt`, `bad_tracks`; bei IEC-Dumps `dos` (Einschaltmeldung), `tracks`, `bad_blocks`; immer `checksum` (Bytesumme des Abbilds hinter dem SCP-Kopf, identisch mit dem Prüfsummenfeld im SCP-Kopf; bei IEC über alle Bytes). Per READ_FILE abholbar.
- **Dump (1.14):** `tracks=0` ermittelt vor dem Dump die höchste erreichbare Spur (PROBE_TRACKS) und liest bis dahin (max. 84); `UFI.CFG track_search=1` liest bei `double_step` (40-Spur-Diskette im 80-Spur-Laufwerk) eine schwache Spur zusätzlich eine physische Spur weiter außen und innen und behält die beste Position.
- **Dump `double_step` (1.14):** `UFI.CFG double_step=0|1|auto`. Bei `auto` liest die Firmware nach Spur 0 (Format aus der Qualitätsprobe bekannt) einmal die physische Spur 2: Melden die Sektorköpfe dort Zylinder 1, liegt eine 40-Spur-Diskette (48 tpi) im 80-Spur-Laufwerk; dann werden zwei Schritte je Spur geschaltet, der Kopf rekalibriert, `tracks` > 42 auf 40 (`tracks=0`) bzw. 42 gekürzt und `double step: on` ins LOG geschrieben. Ohne erkanntes Format (kein MFM/Amiga) bleibt es bei einfachen Schritten. Nicht für Apple und IEC.
- **Auto-Dump (1.14):** `UFI.CFG auto_dump=1` startet einen Dump von selbst, sobald eine Diskette eingelegt wird (PC-A/B- und Amiga-Laufwerke mit DSKCHG-Leitung; nicht Shugart-Bus, Apple, IEC). Im Leerlauf wird das Laufwerk alle 500 ms kurz angewählt und die DSKCHG-Leitung gelesen (ohne Diskette gesetzt); dann alle 2 s ein Schrittpaar (hörbar), das die Verriegelung bei eingelegter Diskette löscht; 1 s später beginnt der Dump wie mit Taster A. Nach dem Dump muss die Diskette erst entnommen werden. Ausgesetzt, solange der Host ein anderes Laufwerk gewählt hat, eine Übertragung läuft, der SD-/USB-Floppy-Modus aktiv ist oder `protocol=gw` gilt.
- **STEP_SCHEDULE (1.14, Spiradisc-artige Spuren):** Kopfbewegungen während des nächsten READ oder WRITE: `at_ms` zählt ab dem Startereignis der Operation (Indeximpuls, der Umdrehung 0 bzw. das Schreiben beginnt; ohne Index der Beginn der Aufnahme), `pos` ist die Zielposition in Spuren oder, mit Bit0, in Viertelspuren (Spur × 4 + Viertel, Apple). Die Einträge müssen zeitlich geordnet sein (sonst Status 16), die Hauptschleife führt sie bei Fälligkeit mit dem normalen Positionieren aus (Shugart: Schritt + Einschwingzeit, Apple: Phasen; Genauigkeit etwa 1–2 ms, die Aufnahme läuft dabei weiter). Der Plan gilt einmalig für die nächste Operation und verfällt, wenn sie vorher endet. `ufi read 17 0 --steps 50:17.1,100:17.2,150:17.3`.
- **SET_RPM (1.14, 3-Mode-Laufwerke mit 300/360 U/min):** Die Firmware schaltet die in `UFI.CFG rpm_line=density|drate` gewählte Busleitung (DENSITY = Pin 2, DRATE = Pin 6 über JP1 – dort auch DS3 des Shugart-Busses) auf den Pegel, der laut `rpm_360=low|high` 360 U/min bedeutet (Laufwerksjumper beachten), und wartet bei laufendem Motor die Hochlaufzeit (`spinup_ms`) ab. Ohne konfigurierte Leitung Status 11, andere Werte als 300/360 Status 16; die Antwort enthält immer den aktuellen Zustand. `rpm=300|360` setzt die Drehzahl beim Start. Mit `rpm_line=density` überschreibt LINES (0x16) die Wahl. Kontrolle mit DIAG_RPM.
- **Hartsektoriert (READ/WRITE Bit1, 1.14):** `sectors` = Zahl der Sektorlöcher (10, 16, 32; ≥ 2, sonst Status 16, ebenso ohne das Byte). Die Firmware zählt alle Löcher auf INDEX und nimmt nur das Indexloch als Index: den Impuls, der nach weniger als ¾ des vorigen Abstands kommt (das Indexloch liegt in der Mitte zwischen zwei Sektorlöchern; das folgende Sektorloch hat denselben kurzen Abstand, davor war der Abstand aber bereits kurz). Umdrehungen und `FD`-Marken zählen also von Indexloch zu Indexloch, WRITE beginnt und endet am Indexloch. Bis zur Erkennung vergehen bis zu einer Umdrehung plus zwei Löcher; die Einstellung gilt nur für den jeweiligen Befehl (Dump, Kopie und Greaseweazle-Modus arbeiten weichsektoriert).
- **Viertelspuren (Apple Disk II, 1.14):** SEEK mit `quarter` 1–3 bzw. READ/WRITE mit Bit2 (`track` = Spur × 4 + Viertel, 0–156) setzen den Kopf zwischen die Halbspuren: gerade Viertel = eine Phase, ungerade Viertel = beide Nachbarphasen gleichzeitig bestromt (bleiben bis zum nächsten Positionieren an). Bei anderen Laufwerken antwortet die Firmware mit Status 16; Spur × 4 + Viertel > 156 mit Status 3. Nach Abwahl des Laufwerks (Phasen stromlos) gilt eine Viertelposition als unbekannt, der nächste Seek rekalibriert. STATUS meldet weiterhin die ganze Spur. Kopierschutz-Spuren (z. B. Spur 17,5) liest man mit `ufi read 17 0 --quarter 2`.
- **DIAG_RPM (1.13):** misst am gewählten Laufwerk `revs` Umdrehungen über die INDEX-Leitung (Motor wird gestartet und läuft weiter); Status 2 ohne Laufwerk oder bei IEC, Status 4 ohne Indeximpuls (auch Apple ohne `apple_sync=1`), Status 5 bei Abbruch der Impulsfolge. `rpm_x100` = 6000 × 275 000 000 / `period_avg`, gerundet; `pulse` ist die mittlere Impulsbreite (Low-Zeit auf dem Kabel).
- **DRIVE_SCAN (1.13):** wählt nacheinander jedes der vier Laufwerke, startet den Motor, rekalibriert (TRK0 = Laufwerk antwortet), wartet bis 600 ms auf zwei Indeximpulse (= Diskette eingelegt und dreht) und liest den Schreibschutz; anschließend ist wieder das zuvor gewählte Laufwerk aktiv. Dauert bis zu einigen Sekunden je Laufwerk, bewegt die Köpfe.
- **INDEX_SIM (1.13):** Open-Drain-Ausgang auf J9 Pin 6 (PE1) mit `rpm` Impulsen je Minute, Impuls = Low für `pulse_us`. Für Flippy-Disketten: Draht an den Indexsensor-Ausgang eines PC-Laufwerks, das ohne Index nicht schreibt. Derselbe Pin ist die SD-Stellung des Betriebsart-Schalters an J13: steht der Schalter auf SD, antwortet INDEX_SIM mit Status 1; solange die Simulation läuft, wird der Schalter dort als „nicht SD“ gelesen. Die Antwort enthält immer den aktuellen Zustand (auch bei Status 16 für `rpm` außerhalb 200–400, `pulse_us` ≥ halbe Umdrehung oder unbekannte `mode`-Bits). **`mode` Bit1 intern (1.14):** Der Impulsbeginn gilt im Lese- und Schreibpfad als Indeximpuls (READ-Umdrehungen, WRITE-Start und -Ende, ERASE, DIAG_RPM); die INDEX-Leitung des Laufwerks wird solange ignoriert, damit ein über den Draht zurückgespiegelter Impuls nicht doppelt zählt. Ohne Bit0 bleibt J9 Pin 6 Eingang (Betriebsart-Schalter weiter nutzbar). `UFI.CFG index_sim_mode=pin|internal|both`.
- **SELFTEST (1.14):** blockierend (etwa 0,5 s), nicht während READ/WRITE/Dump. Posten: 1 `timer` TIM2-Takte je ms gegen SysTick (275 000 ± 2 %); 2 `usb_sof` Quarzabweichung in ppm gegen 128 USB-Rahmen des Hosts (2 = nicht enumeriert, Warnung ab ±500 ppm); 3 `psram` Größe des Flussspeichers, frischer Mustertest über die obersten 64 KB (Fehler: `value` = Offset); 4 `sd` Kapazität in MB; 5/6 `rail_5v`/`rail_12v` Strom in mA (1 = Überstrom ausgelöst, 2 = abgeschaltet/Sicherheitsmodus); 7 `board_id` Teiler in mV (3 = keine bekannte Revision); 8 `usbc_cc` beworbener Strom in mA (3 = kein CC, USB-A-Kabel); 9 `fdd_lines` Laufwerkseingänge ohne gewähltes Laufwerk, Bitmaske der Leitungen auf Low (Bit0 INDEX, 1 TRK0, 2 WPROT, 3 DSKCHG, 4 READY, 5 RDATA; 3 = etwas zieht, z. B. ein angeschlossenes Laufwerk; 2 = Laufwerk gewählt); 10 `iec_loop` Rückschleife durch die IEC-Treiber/Empfänger (Bit0–3 ATN/CLK/DATA/SRQ nicht als gezogen gelesen = Fehler, Bit4–7 nicht als losgelassen gelesen = Warnung, RESET bleibt unberührt); 11 `index_loop` Indexsimulation 300 U/min auf J9 Pin 6, Impuls muss binnen 250 ms auf INDEX erscheinen (Draht J9-6 → J6-8; 2 = kein Draht oder Schalter auf SD); 12 `last_reset` Absturzaufzeichnung beim Start (`value` 1 HardFault, 2 MemManage, 3 BusFault, 4 UsageFault; 3 = vorhanden); 13 `stack_free` seit dem Start nie benutzte Stack-Bytes (beim Start wird das freie DTCM unter dem Stackzeiger mit einem Muster gefüllt; 3 unter 2 KB, 1 unter 512 Byte; der Linker prüft, dass die DTCM-Daten 8 KB Stack frei lassen); 14 `log` `SELFTEST.LOG` geschrieben (`value` = Bytes). Dieselbe Tabelle steht als Text in `SELFTEST.LOG`.
- **Absturzaufzeichnung (1.14):** Ein Cortex-M-Fault legt Register (r0–r3, r12, lr, pc, xpsr), CFSR/HFSR/MMFAR/BFAR und die Laufzeit im DTCM ab und löst sofort einen Reset aus (statt bis zum Watchdog zu hängen). Beim nächsten Start schreibt die Firmware `CRASH.LOG` (nur der letzte Fault, per READ_FILE abholbar) und INFO meldet in der fünften Textzeile `reset: HardFault pc=… lr=… cfsr=… (CRASH.LOG)`; sonst steht dort die Reset-Ursache (`power-on`, `pin`, `software`, `watchdog`, `brown-out`).
- **TIMING / TG43 (1.16):** Die Zeitwerte sind 11 × u16: `step_pulse_us, step_rate_us, settle_us, dir_change_us, side_settle_us, spinup_ms, select_settle_us, motor_off_s, double_step, precomp_ns, tg43`. Mit `tg43` = 1 folgt die DENSITY-Leitung (J6 Pin 2) nach jedem Positionieren und Rekalibrieren der physischen Spur: ab Spur 43 gezogen (low), darunter losgelassen. Auf dem 50-poligen Bus eines 8-Zoll-Laufwerks ist Pin 2 des Adapters TG43 („track greater than 43“), das den Schreibstrom auf den inneren Spuren senkt; ohne das Signal sind Schreibvorgänge dort unzuverlässig. Nicht zusammen mit `rpm_line=density` (dann hat die Drehzahlwahl Vorrang) und nicht am Apple-Port. `UFI.CFG tg43=1` setzt das Feld beim Start, LINES (0x16) überschreibt den Pegel bis zum nächsten Positionieren. Ältere Hosts senden 10 Felder und lassen `tg43` unberührt.
- **Nicht in v2:** Timer-/GPIO-Diagnose gibt es nur in v1; die Drehzahl liefern DIAG_RPM und ANALYZE.

### 3.7 Beispiel: Spur lesen

```
Host:  55 AA 01 10 01 00 01 00  01               crc   SELECT drive=1 (A)
Gerät: 55 AA 02 10 01 00 00 00                   crc   OK
Host:  55 AA 01 11 02 00 01 00  01               crc   MOTOR on
Gerät: 55 AA 02 11 02 00 00 00                   crc
Host:  55 AA 01 20 03 00 06 00  00 00 03 00 00 00 crc  READ track 0, side 0, 3 revs
Gerät: 55 AA 02 20 03 00 00 00                   crc   angenommen
Gerät: 55 AA 04 20 03 00 00 10  <4096 Byte Fluss> crc  Daten …
Gerät: 55 AA 05 20 03 00 01 00  03               crc   Ende, 3 Umdrehungen
```
