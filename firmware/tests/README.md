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
