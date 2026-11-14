# Native Firmware-Tests

Die Programme hier übersetzen echte Firmware-Quellen ohne HAL-Abhängigkeit (den
Schreib-Codec `src/ufi_write_codec.c`, später den MFM-Codec) zusammen mit einem kleinen
Testprogramm und führen sie auf dem Host aus. Geprüft wird damit der Code, der auch auf
dem STM32 läuft, nicht ein Modell davon.

```
python firmware/tests/run_tests.py          # alle
python firmware/tests/run_tests.py codec    # nur Tests, deren Name 'codec' enthält
```

Benötigt clang oder gcc im PATH. Ausgaben liegen in `firmware/tests/build/` (ignoriert).

| Programm | Firmware-Quelle | Inhalt |
|---|---|---|
| `test_write_verify.c` | `ufi_verify.c`, `ufi_mfm.c` | Verify-Stufen: gleiche Spur → Inhalt geprüft, gleiche Übergangszahl mit anderen Daten → Fehler, 3 % Jitter → geprüft, nicht dekodierbare Spur → nur plausibel; 1.44M, 720K, Amiga; 1024- und 256-Byte-Sektoren (Änderung an Byte 900 fällt durch, 1.17) |
| `test_write_stream.c` | `ufi_write_stream.c` | DMA-Streaming: Ring aus zwei Hälften, Nachladen bei HT/TC, exakter Stopp nach dem letzten Intervall für Längen 1…200 000 auch mit späten Interrupts, Unterlauf-Erkennung |
| `test_image.c` | `ufi_image.c` | Abbilddateien für das Schreiben von SD (1.16): Art und Geometrie aus Name und Größe, 1541/1571/1581-Zonen, SCP-Kopf und Spurkopf, Zellen → Takte mit der Rundung des Hosts; HFE (1.17): Kopf, Spurtabelle, Zellenlänge mit Anpassung an die Umdrehung, Bits ↔ Flux über die PLL mit Jitter und Drift |
| `test_write_codec.c` | `ufi_write_codec.c` | Kompaktparser (alle Startbytes, jede Aufteilung, reservierte Codes, Null-Intervall, Überzahl), Packen (Bereich 1,6–238 µs, Kappen nur per Flag, Zeitskala mit Restmitführung, Präkompensation gegen Referenz, in-place) |
| `test_sha256.c` | `ufi_sha256.c` | SHA-256 gegen die FIPS-180-4-Beispiele (leer, „abc“, zwei Blöcke, eine Million „a“ in ungeraden Stücken), HMAC-SHA256 gegen RFC 4231 Fälle 1, 2 und 6 (Schlüssel länger als ein Block), Vergleich in konstanter Zeit (LAN, Stufe L1) |
| `test_w5500.c` | `ufi_w5500.c`, `ufi_sha256.c` | W5500-Treiber gegen die Registernachbildung `w5500_fake.h` (SPI-Rahmen wie im Chip): Reset und VERSIONR 0x04 (0x00/0xFF/hängendes MR.RST abgewiesen), MAC schreiben mit Rücklesen, Pufferaufteilung 8/2/2/1 KB, PHY-Link, Socketbefehle, TX/RX-Zeiger über das Pufferende, ARP-Zeitüberschreitung, MAC aus der UID (02 + SHA-256) |
| `test_lan_cfg.c` | `ufi_lan_cfg.c` | LAN.CFG (L2): Vorgaben, die geschriebene Vorgabedatei (CRLF) ergibt genau die Vorgaben, feste Adresse mit Maske/Gateway/DNS, abgewiesene Adressen, MAC-Adressen (Multicast/Null abgewiesen), Hostnamen, Port, `lan_write`, `lan_key` (Ende an `#`, nach 64 Zeichen abgeschnitten), Kommentare, unbekannte Schlüssel, NUL und Längengrenze, Adress-/MAC-Formatierung; Fuzzziel `lan` (`fuzz_lan.c`) |
| `test_net.c` | `ufi_net.c`, `ufi_w5500.c`, `ufi_lan_cfg.c` | IP-Adresse (L3) gegen die W5500-Nachbildung mit gescriptetem DHCP-Server: DISCOVER/OFFER/REQUEST/ACK, ARP-Prüfung des Angebots mit SIPR 0.0.0.0, DECLINE bei belegter Adresse, NAK, RENEW per Unicast an T1, REBIND an T2, Ablauf, unendliche und sehr lange Leases; ohne Server Link-Local nach 20 s (Kandidat aus 169.254.1–254, gleiche MAC = gleicher erster Kandidat, Probe-Timing 1,5 s × 3), Konflikt beim Probieren und nachträglich (IR.CONFLICT), Wechsel auf DHCP nur ohne Sitzung, feste Adresse; DHCP-Nachrichten einzeln (Optionen, Grenzen, Parser-Abweisungen). Fuzzziel `lan` auch für DHCP und den Zustandsautomaten |
| `test_lan_session.c` | `ufi_lan_session.c`, `ufi_w5500.c`, `ufi_sha256.c` | UFI v2 über TCP (L4) gegen die W5500-Nachbildung: Listen/Accept auf 55100 mit Keep-Alive, Rahmen in Stücken und hinter Müll, CRC- und Längenfehler wie über USB, Annahme erst mit Platz für die Antwort, ein SEND zur Zeit, Trennen durch die Gegenstelle und durch Zeitüberschreitung, `lan_key`-Challenge (HMAC-SHA256, eine Antwort je Challenge, Trennen nach drei Fehlversuchen, Status 25 vorher, DATA verworfen); dazu `test_lan_runtime.py` (LAN-Regeln und Laufwerksbesitz aus `ufi_v2.c`) |
| `test_lan_status.c` | `ufi_lan_status.c`, `ufi_w5500.c`, `ufi_lan_cfg.c` | LAN-Status (L5): NET-0x48-Aufbau, NET.TXT-Text (auch längster Hostname), Discovery-Anfrage/-Antwort, Responder auf UDP 55100 gegen die Nachbildung: Unicast an den Fragenden, Broadcast an ein fremdes Subnetz, ein SEND zur Zeit, fremde und kurze Datagramme verworfen |

The table above describes 10 of the 34 C programs that `run_tests.py` builds; the full list is the
`TESTS` table in `run_tests.py`.

## Groups of the offline gate

`python firmware/tests/check_all.py` runs the groups below (`--jobs 1..8`, `--require-corpora`); the
result goes to `firmware/tests/build/offline_gate.json`. One line per file, taken from its docstring.

Independent groups, run in parallel (30):

| File | Covers |
|---|---|
| `run_tests.py` | builds and runs the native C test programs (table above): real firmware sources without HAL dependency |
| `test_regressions.py` | selected production control functions, extracted verbatim, against register and transport doubles; control flow, not a cycle-accurate simulation |
| `test_write_safety.py` | destructive diagnostic control flow with timed safety faults; software ordering and cleanup, not the physical gate latency |
| `test_gw.py` | official host encoder against the production C decoder; raw read deadline tests |
| `test_protocol_faults.py` | production frame parser fuzzing and generated composite descriptor checks |
| `test_native_requests.py` | native request rejection and compact preparation before side effects |
| `test_sd_runtime.py` | production SD loading and source-preserving write helper with storage and drive doubles |
| `test_sd_export.py` | production HFEv3 SD writer and capture publication with bounded FatFs doubles |
| `test_memory.py` | heap bounds, PSRAM failure selection and deliberately broken ELF budgets |
| `test_runtime.py` | production USB queue and IEC reset state machine with controlled completion and time |
| `test_quality_runtime.py` | firmware quality runner and WRITE parser with capture and clock doubles |
| `test_analysis_job.py` | production cooperative analyzer and job protocol with capture doubles |
| `test_precision_runtime.py` | production precision verifier with independent capture and U11 storage doubles |
| `test_capture_archive.py` | capture finalization and USB reset preserve only stable captures |
| `benchmark_quality.py` | builds deterministic quality fixtures and keeps their machine-readable metrics |
| `test_apple_gcr.py` | Apple II 5.25-inch 6-and-2 GCR decoder (format 7) against a synthetic track |
| `test_tolerance.py` | decoder tolerance flags (op 34) for IBM MFM, Amiga, FM, HP MMFM, Commodore and Macintosh, one fault at a time |
| `test_evidence_runtime.py` | production capture context/trace runtime and IRQ filtering with hardware doubles |
| `test_media_runtime.py` | production seek/recalibration and media profiles with simulated drive signals |
| `test_drive_profiles.py` | production line guards and operating profiles with GPIO doubles |
| `test_stream_runtime.py` | production streaming scheduler: bounded cache work and ring-wrap ordering |
| `test_motor_runtime.py` | production motor control: no repeated spin-up, cancellation turns the motor off |
| `test_capture_integrity.py` | production capture guards with register doubles; no physical timing claim |
| `test_lan_runtime.py` | LAN rules of the v2 dispatcher (L4): what the LAN may not do, LAN.CFG protection, drive ownership between USB and LAN, transport routing |
| `test_mac_write_guard.py` | Mac drive (board v1.0, J23) write guards: write protection from the register status, whole tracks only, never the SYNC sensor as index |
| `test_psram_init.py` | PSRAM start-up of `ufi_psram.c` (whole file) against a simulated controller and APS6404L chip |
| `test_board_safety_runtime.py` | board safety service (USB-loss reaction) and pin start-up (`board_gpio_init()`) against register doubles |
| `test_crash_boot.py` | reset cause and fault record of `ufi_crash.c` (whole file) against stub registers and a stub file system |
| `test_event_poll.py` | polled media events of `ufi_v2.c`: disk change 0x80 and write protect 0x86 |
| `test_weak_reference.py` | host reference `ufi_host/weak.py` against the production weak-window code of `ufi_analyze.c` |

Run one after the other afterwards:

| File | Covers |
|---|---|
| `test_flux_corpus.py` | pinned external capture corpus, offline replay; skipped without local fixtures unless `--require-corpora` |
| `test_gcr_corpus.py` | reproducible independent GCR encoder corpus; same skip rule |
| `test_mac_corpus.py` | local user-supplied MOOF images through the firmware decoder (format 6) and the host decoder; same skip rule |
| `test_hfe_corpus.py` | optional local HxC fixtures; same skip rule |
| `test_hfe_v3_reference.py` | pinned independent HFE cells; same skip rule |
| `pytest software/ufi_host/tests` | host package tests |
| `firmware/tools/asm6502.py` + `firmware/tools/test_iec_nib.py` | assembles `tools/iec_nib.s`, compares the result with `firmware/include/iec_nib.h`, and runs the 1541 drive code on py65 with a VIA2 / disk model |

Not part of the gate: `run_fuzz.py` (libFuzzer targets), `coverage.py` (llvm-cov of the natively built
modules), `benchmark_mfm_fastpaths.py` (host comparison of old and new primitives), `fuzz_stubs.py`
(stub generator used by the fuzz targets).
