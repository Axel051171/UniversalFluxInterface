# USB-Protokoll der UFI Flux Engine (UFI Headless)

Stand: Firmware 1.12, Board v0.7. Alle Mehrbyte-Werte **little endian**.

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
| 17 | ERASE_FLUX | `ticks` u32 | nach dem Löschen: 1 Statusbyte |
| 20 | GET_PIN | `pin` u8 (8 INDEX, 26 TRK0, 28 WRPROT, 34 DSKCHG/READY) | `level` u8 (0 = low = aktiv) |

Nicht unterstützt (ack 1): 1 UPDATE, 11 SWITCH_FW_MODE, 18/19 SOURCE/SINK_BYTES (`gw bandwidth`), 21 TEST_MODE, 22 NOCLICK_STEP (Flippy).

**GET_INFO idx 0:** `fw_major, fw_minor` (= UFI-Firmware, z. B. 1.12), `is_main=1`, `max_cmd=22`, `sample_freq` u32 = **68 750 000**, `hw_model=0x55`, `hw_submodel=1` (gw zeigt „Unknown (0x5501)“), `usb_speed=0` (Full Speed), `mcu_id=0`, `mcu_mhz` u16 = 550, `sram_kb` u16 = 564, `usb_buf_kb` u16 = 8, Rest 0. **idx 1:** Bandbreite (fester Nennwert). **idx 7 / 8+n:** Laufwerk aktuell / Einheit n: `flags` u32 (Bit0 Zylinder gültig, Bit1 Motor an), `cyl` s32.

**Bus/Einheiten:** IBM-PC: 0 = Laufwerk A, 1 = B (Kabel mit Twist). Shugart: 0–3 = DS0–DS3 (gerades Kabel, DS3 nur mit JP1). Amiga-Port, IEC und Apple-Port sind im Greaseweazle-Modus nicht erreichbar.

**Parameter (idx 0, je u16):** select_delay µs, step_delay µs, seek_settle ms, motor_delay ms, watchdog ms (Standard 10000: ohne Befehl Motoren aus, abwählen), pre_write µs, post_write µs, index_mask µs (die letzten drei werden gespeichert, nicht ausgewertet).

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
| **Laufwerk** | | | |
| 0x10 | SELECT | `drive` u8 | – |
| 0x11 | MOTOR | `on` u8 | – (wartet die Anlaufzeit ab) |
| 0x12 | SEEK | `track` u8 | – |
| 0x13 | RECAL | – | – |
| 0x14 | SIDE | `side` u8 | – |
| 0x15 | TIMING | – oder 10 × u16 (wie v1 0x17) | aktuelle 10 × u16 |
| 0x16 | LINES | `density` u8, `drate` u8 | – |
| 0x17 | CHECK_DISK | – | `changed` u8, `present` u8 |
| 0x18 | PROBE_TRACKS | – | höchste erreichbare Spur u8 |
| 0x19 | SEEK_TEST | `a` u8, `b` u8, `cycles` u8 | – |
| 0x1A | AMIGA_ID | – | ID u32 |
| **Fluss** | | | |
| 0x20 | READ | `track, side, revs` u8, `flags` u8 (Bit0 ohne Index), `period_ms` u16 | Annahme; dann Daten-Rahmen, Ende-Rahmen mit `revs` u8 |
| 0x21 | ABORT | – | – (bricht Lesen/Schreiben ab; der laufende Vorgang endet mit Ende-Rahmen Status 4) |
| 0x22 | WRITE | `track, side` u8, `flags` u8 (Bit0 Prüflesen), `flux_count` u32, `byte_count` u32 | Annahme; dann sendet der Host Daten-Rahmen (type 4, cmd 0x22) mit zusammen `byte_count` Byte; Ende-Rahmen nach dem Schreiben |
| 0x23 | ERASE | `track, side` u8 | – (eine Umdrehung) |
| 0x24 | PATTERN | `track, side` u8, `interval_ns` u16, `duration_ms` u16 | – |
| **Commodore IEC** | | | |
| 0x30 | IEC_RESET | – | – |
| 0x31 | IEC_SEND | `byte` u8, `eoi` u8 | – |
| 0x32 | IEC_RECV | – | `byte` u8, `eoi` u8 |
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

**caps (INFO):** Bit0 PSRAM ok, Bit1 SD-NAND bereit, Bit2 IEC, Bit3 Amiga, Bit4 Amiga2/JP3-fähig (v0.7), Bit5 Shugart-Bus, Bit6 Apple-Port (v0.7), Bit7 Sync-Sensor aktiviert (`apple_sync=1`), Bit8 Greaseweazle-Modus verfügbar, Bit9 Dateizugriff, Bit10 Ereignisse.

**Beschäftigt (Status 1):** Während eines Dumps/einer Kopie werden nur PING, INFO, STATUS, EVENTS, POWER, USB_POWER, SD_INFO, DUMP_STATUS und DUMP_ABORT ausgeführt; während READ/WRITE (bis zum Ende-Rahmen) werden Laufwerks- und Flussbefehle (0x10–0x1A, 0x20, 0x22–0x24, DUMP_START, COPY_START) abgewiesen. WRITE wartet höchstens 1 s auf den Indeximpuls (sonst Ende-Status 4); schlägt das Positionieren vor dem Schreiben fehl, endet WRITE mit Status 3.

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
- **INFO:** `store_bytes` = Größe des Flussspeichers (8 MB PSRAM, sonst interner Ersatzpuffer), nicht der SD-NAND (dafür SD_INFO). Der Text danach ist ASCII, Zeilen durch `\n` getrennt, ohne abschließendes Nullbyte.
- **board_status (8 Byte):** `power` u8 (Bit0 FDD_5V an, Bit1 FDD_12V an), `flags` u8 (Bit0 WRITE-LOCK gesteckt, Bit1/2 Überstrom 5 V/12 V ausgelöst, Bit3 SD-NAND bereit, Bit4/5 Taster A/B gedrückt, Bit6 Sicherheitsmodus), `i5_ma` u16, `i12_ma` u16, `board_id_mv` u16.
- **READ:** `revs` 1…200. Der Ende-Rahmen ist maßgeblich: Nur so viele Umdrehungen gelten, wie er meldet (bei Überlauf können `FD`-Marken fehlen oder überzählig sein). Ende-Status 4 = kein Index **oder** per ABORT abgebrochen; 10 = Überlauf (Host las zu langsam).
- **WRITE:** Der Host wartet die Annahme-Antwort ab und sendet erst dann die Daten-Rahmen (je ≤ 4096 Byte Nutzdaten, beliebig aufgeteilt, zusammen genau `byte_count`). Ende-Status: 0 OK, 12 schreibgeschützt, 4 kein Index (Laufwerk mit Index), 6 Prüflesen fehlgeschlagen (`flags` Bit0), 10 Daten fehlerhaft/zu viele Wechsel.
- **Dateinamen:** FAT 8.3 (keine langen Namen), Großbuchstaben, Pfadtrenner `/` erlaubt (z. B. `DUMPS/DUMP0001.SCP`), höchstens 64 Zeichen.
- **READ_FILE:** Weniger Bytes als angefordert (auch 0) = Dateiende.
- **WRITE_FILE:** `flags` Bit0 legt die Datei neu an bzw. kürzt sie auf 0 (auch mit 0 Datenbytes). Weniger geschriebene als gesendete Bytes = Speicher voll; die Antwort trägt dann Status 13.
- **EVENTS:** Unbekannte Bits in `mask` werden ignoriert; `mask = 0` schaltet alle Ereignisse ab.
- **DUMP_START:** Nutzdaten leer (Werte aus UFI.CFG) oder genau 4 Byte.
- **UFI.CFG:** Textdatei, Zeilen `schlüssel=wert` (Schlüssel klein), `#` beginnt einen Kommentar, CRLF; unbekannte Schlüssel werden ignoriert. Schlüssel: `drive, tracks, sides, revs, button_a, copy_from, copy_to, protocol, apple_sync`.
- **Nicht in v2:** Selbsttest und Timer-/GPIO-Diagnose gibt es nur in v1; die Drehzahl berechnet der Host aus den Indexabständen eines READ.

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
