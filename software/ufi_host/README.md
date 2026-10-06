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

## Protokoll (Firmware ≥ ec98588)

- Host → Gerät: ein Paket ≤ 64 Byte je Befehl, `[cmd, args...]` (Codes in `ufi_host/protocol.py` = `ufi_command_t`).
- Gerät → Host: jede Nachricht beginnt mit `{u8 command, u8 status, u16 length}` + Payload. Status = Fehlercode aus `ufi_fixes.h` (Klartext in `protocol.ERRORS`).
- Lesen: Antwort, dann je Umdrehung `{0x2E, 0, 12}` + `{track, side, rev, flags, index_ticks, count}` + `count × u32` Zeitstempel (275 MHz, relativ zum Index), Abschluss `{0x2D, status, 1}` + `[Umdrehungen]`.
- Schreiben: Antwort abwarten, dann `count × u32` Deltas als Rohdaten, Fertig-Meldung mit demselben Befehlscode.

Tests ohne Hardware (simuliertes Gerät auf Byte-Ebene): `python -m pytest software/ufi_host/tests`.
