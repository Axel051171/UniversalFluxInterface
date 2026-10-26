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
