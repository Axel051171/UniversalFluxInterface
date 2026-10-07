# ufi – PC-Tool für die UFI Flux Engine (UFI Headless)

Kommandozeilen-Tool über den USB-CDC-Port (unter Windows ohne Treiber, COM-Port wird per VID/PID `1209:4F54` gefunden).
Liest Spuren als Flux und speichert sie als **SCP** (SuperCard Pro) – Dekodierung/Konvertierung mit vorhandenen Werkzeugen (HxC, Greaseweazle `gw convert`, FluxEngine). Schreibt SCP-Images zurück.

```bash
pip install -e software/ufi_host[dev]
ufi info
ufi select a && ufi motor on && ufi recal
ufi read 0 0 -r 3                         # Zusammenfassung (Flux-Anzahl, RPM je Umdrehung)
ufi read-disk -o disk.scp --tracks 80 --sides 2 -r 3 --drive a
ufi write-disk disk.scp --verify --drive amiga
ufi iec-reset && ufi iec-send 0x28 && ufi iec-recv
ufi bootloader                            # danach: dfu-util -a 0 -s 0x08000000:leave -D ufi_firmware.bin
```

## Protokoll (Firmware ≥ ec98588; Stream-Lesen braucht eine Firmware mit `ufi_stream.c`)

- Host → Gerät: ein Paket ≤ 64 Byte je Befehl, `[cmd, args...]` (Codes in `ufi_host/protocol.py` = `ufi_command_t`).
- Gerät → Host: jede Nachricht beginnt mit `{u8 command, u8 status, u16 length}` + Payload. Status = Fehlercode aus `ufi_fixes.h` (Klartext in `protocol.ERRORS`).
- Lesen (`READ_TRACK` 0x20, Standard): Antwort, dann **während die Diskette dreht** `{0x2C, 0, len}`-Nachrichten mit einem fortlaufenden Bytestrom (verlustfrei, 275 MHz, ~2 Byte je Flusswechsel): `01–EF` = Delta 1 Byte, `F0–FC b` = Delta 240 + ((x−F0)<<8) + b, `FD u32` = Indexpuls (Ticks nach dem letzten Wechsel), `FE u32` = langes Delta. Zeit 0 = erster Index; Format in `firmware/src/ufi_stream.c`, Decoder `protocol.decode_stream`. Abschluss `{0x2D, status, 1}` + `[Umdrehungen]`.
- Lesen roh (`READ_TRACK_RAW` 0x21, `Device.read_track_raw`): erst nach der Aufnahme je Umdrehung `{0x2E, 0, 12}` + `{track, side, rev, flags, index_ticks, count}` + `count × u32` Zeitstempel (relativ zum Index), gleicher Abschluss.
- Lesen-Optionen (beide Lesebefehle): `[cmd, track, side, revs, flags, period_ms u16]`. `flags` Bit0 = **ohne Indexpuls** (Laufwerk/Diskette ohne Index, Flippy-Seite, hart sektoriert): Umdrehungsgrenzen alle `period_ms` (0 = 200 ms). `READ_TRACK` erlaubt bis 200 Umdrehungen (Ringpuffer), `READ_TRACK_RAW` bis 20.
- Schreiben: Antwort abwarten, dann `count × u32` Deltas als Rohdaten, Fertig-Meldung mit demselben Befehlscode.
- Schreiben kompakt (`WRITE_TRACK_C` 0x33): `[cmd, track, side, count u32, bytes u32, verify]`, dann `bytes` Bytes im Stream-Code von oben (ohne `FD`-Indexmarken), Fertig-Meldung `0x33`. Etwa halbe Upload-Menge.
- `DRIVE_TIMING` (0x17): 10 × u16 – `step_pulse_us, step_rate_us, settle_us, dir_change_us, side_settle_us, spinup_ms, select_settle_us, motor_off_s` (Motor aus nach Leerlauf, 0 = nie, Standard 30), `double_step` (1 = 40-Spur-Diskette im 80-Spur-Laufwerk), `precomp_ns` (0xFFFF = nach Datenrate, 0 = aus). Kürzere Nutzdaten überschreiben nur die vorderen Felder.
- Board v0.5: `BOARD_STATUS` (0x1B, optional `[maske]` Bit0 FDD_5V, Bit1 FDD_12V) → `{power u8, flags u8, i5_mA u16, i12_mA u16, board_id_mV u16}`, Flags: Bit0 WRITE-LOCK-Jumper, Bit1/2 Überstrom-Abschaltung 5 V/12 V, Bit3 SD-Karte, Bit4/5 Taster A/B. `PROBE_TRACKS` (0x1C) → höchste erreichbare Spur (fährt an den Endanschlag). `SD_INFO` (0x1D) → `{present, status, card_type, bus_width, capacity_MB u32}`.
- `GET_INFO`: Firmware-Version mit Git-Revision und Build-Datum, Board inkl. Revision (BOARD_ID), MCU, PSRAM-Selbsttest.
- Sicherheit in der Firmware: USB weg > 300 ms → Transfers abbrechen, Motor aus, Laufwerk abwählen, WGATE frei; Laufwerksversorgung > 1,5 A für 50 ms → Schiene aus.

Tests ohne Hardware (simuliertes Gerät auf Byte-Ebene): `python -m pytest software/ufi_host/tests`.
