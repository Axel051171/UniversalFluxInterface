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
| `test_write_verify.c` | `ufi_verify.c`, `ufi_mfm.c` | Verify-Stufen: gleiche Spur → Inhalt geprüft, gleiche Übergangszahl mit anderen Daten → Fehler, 3 % Jitter → geprüft, nicht dekodierbare Spur → nur plausibel; 1.44M, 720K, Amiga |
| `test_write_codec.c` | `ufi_write_codec.c` | Kompaktparser (alle Startbytes, jede Aufteilung, reservierte Codes, Null-Intervall, Überzahl), Packen (Bereich 1,6–238 µs, Kappen nur per Flag, Zeitskala mit Restmitführung, Präkompensation gegen Referenz, in-place) |
