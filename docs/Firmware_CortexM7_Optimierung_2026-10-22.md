# Gezielte Cortex-M7-Optimierung, 2026-10-22

Die Firmware wird an den tatsächlich verwendeten STM32H723 angepasst. Ziel sind
weniger unnötige Operationen und zuverlässige Verarbeitung unter Last. Ohne
Platine sind keine belastbaren Aussagen über erreichte MCU-Datenraten,
Interruptlatenzen oder die vollständige Ausnutzung der Recheneinheiten möglich.

## Übernommene Änderungen

- **CRC16 ohne Bitschleife und ohne Tabelle:** Die acht Schritte des Polynoms
  `0x1021` werden algebraisch durch XOR und Schieben zusammengefasst. Anfangswert,
  Bitreihenfolge, leere Blöcke und Verkettung bleiben gleich. Im ARM-O2-Objekt
  benötigt `mfm_crc16` jetzt36 statt50 Bytes Code; kein zusätzlicher Tabellen-RAM.
- **MFM-Bits parallel entpacken:** Acht Datenbits werden mit Masken und Schieben
  gesammelt. Taktbits und die obere Worthälfte bleiben wie bisher unberücksichtigt.
- **PLL mit abgesichertem 32-Bit-Pfad:** Drei Divisionen verwenden bei passendem
  Wertebereich die native32-Bit-Division. Große Zähler oder Nenner behalten exakt
  den64-Bit-Pfad. Negative Quotienten runden unverändert gegen0; lange Lücken,
  Phasenkorrektur und Decoder-Neusynchronisierung bleiben erhalten. Die erzeugte
  ARM-Ausgabe enthält drei `SDIV`-Pfade mit vorgeschalteter Bereichsprüfung.
  Es gibt keine ungesicherte Kürzung von Zeitstempeln oder Restfehlern.
- **Explizite Compilerprofile:** `UFI_RELEASE_OPTIMIZATION=2|3|s` wählt O2, O3 oder
  Os. Unbekannte Werte werden abgewiesen. Ein frischer Single-Config-Build ohne
  Buildtyp verwendet jetzt ausdrücklich Release/O2, auch in der vorhandenen CI.
  C11 ist explizit konfiguriert. Der Standard bleibt O2; keine automatische
  Aktivierung von LTO oder numerisch verändernden Fast-Math-Optionen.

Die Änderungen betreffen Decoder und Build. USB-Befehle, Flagbedeutungen und
Greaseweazle-Drahtformat ändern sich nicht.

## Referenzprüfungen

Neue native Suite `test_mfm_fastpaths`: **504972/504972** Prüfungen bestanden.

- Unabhängige bisherige bitweise CRC gegen alle65536 Anfangszustände,
  Standardvektor `123456789` mit Ergebnis `0x29B1`, Blocklängen0–4096 und
  inkrementelle Verarbeitung.
- Alle65536 MFM-Wörter und nochmals65536 Wörter mit zufälliger oberer Hälfte.
- Divisionsgrenzen einschließlich INT64_MIN/MAX, INT32_MIN/MAX und UINT32_MAX
  sowie200000 deterministische Quotientenvergleiche mit der64-Bit-Referenz.
- Nach100000 Intervallen identischer vollständiger Decoderzustand gegenüber der
  ursprünglichen PLL-Rechnung, mit verschiedenen Formaten, Raten, Profilen,
  großen Lücken, Nullintervallen und extremen Phasenwerten.

Die Referenz-PLL verwendet denselben Zellverbraucher, aber die ursprüngliche
Arithmetik; CRC und Bitentpackung werden separat unabhängig geprüft. Die übrigen
Decoder-/Korpusprüfungen bleiben zusätzliche Kontrollen.

## Tatsächlich gebaute ARM-Profile

| Profil | BIN/FLASH | Freier AXI-RAM | Größter einzelner Projekt-Stackrahmen |
|---|---:|---:|---:|
| O2, Standard | 225260B | 36816B | 848B |
| O3, Vergleichskandidat | 284848B | 36816B | 872B |
| Os, Vergleichskandidat | 186832B | 36912B | 856B |

Alle drei Varianten bestehen Linker-Speicher- und Stackrahmengrenzen. Die Tabelle
misst Speicher, keine Laufzeit. Der Standardbuild wächst gegenüber dem letzten
Flag-Fix um152B Flash; seine RAM-Belegung und sein maximaler Rahmen bleiben gleich.
Die zusätzlichen Bereichsprüfungen und Rückfallpfade der PLL benötigen Code.
O3 kostet gegenüber O2 weitere59588B; ein Geschwindigkeitsvorteil auf dem Chip ist
nicht gemessen. Os spart Platz, ist damit aber ebenfalls kein bewiesener
Geschwindigkeitsgewinner. Eine maximale kumulative Stacktiefe wird nicht behauptet.

Quellen für die Einordnung:
[ST-Gerätebeschreibung](https://www.st.com/en/microcontrollers-microprocessors/stm32h723vg),
[GCC-Optimierungsoptionen](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html).
Der Gerätehersteller beschreibt den Cortex-M7 mit bis550MHz und I-/D-Cache;
die tatsächlich erreichbare Anwendungsgeschwindigkeit hängt vom Programm und
den Speicher-/Peripheriezugriffen ab. Ein höheres Optimierungslevel allein
qualifiziert keine maximale Anwendungsleistung.

## Reproduzierbarkeit und PC-Mikrobenchmark

```powershell
./build/venv/Scripts/python.exe firmware/tests/run_tests.py fastpaths
./build/venv/Scripts/python.exe firmware/tests/benchmark_mfm_fastpaths.py
cmake -S firmware -B firmware/build -DUFI_RELEASE_OPTIMIZATION=2
cmake --build firmware/build
```

Zum Vergleichen eigene Buildverzeichnisse und Werte3 beziehungsweise s verwenden;
den SDK-Pfad bei frischen Verzeichnissen wie im vorhandenen Build setzen.
Der Mikrobenchmark verifiziert die Ergebnisse auch unter den drei Hostprofilen.
Er nimmt je fünf Zeitmessungen, wechselt die Reihenfolge und berichtet den Median.
Er verarbeitet je Messung8MiB CRC-Eingabe und4194304 MFM-Wörter. Funktionsaufrufe
und Host-Timerauflösung sind Teil dieser Messung.

Auf diesem Windows-/Clang-Rechner benötigt CRC im O2-Lauf etwa14ms mit der
Referenz und12ms mit der XOR-Fassung; Bitentpacken etwa5ms in beiden Fällen.
Diese kurzen Hostzeiten sind keine MCU-Zyklusmessung und erlauben keinen Faktor
für die gesamte Firmware. Sie zeigen insbesondere keinen automatischen Vorteil
von O3 gegenüber O2. JSON: `firmware/tests/build/cpu_benchmark/mfm_fastpaths.json`.

## Abschlussprüfung und offene Grenzen

Die native Suite besteht unter Windows Clang und Linux GCC ASan/UBSan.
Statische ARM-Analyse aller59 Projektmodule und die drei Cross-Builds bestanden.
Der aktuelle vollständige Offline-Gate besteht31/31 Gruppen ohne übersprungene
Prüfung, einschließlich24 nativer Suites, fünf externer Korpora,197 Hosttests und
23/23 IEC-Prüfungen. Linux GCC ASan/UBSan besteht alle24 nativen Suites.
Der gezielte Decoder-/Kopierschutzfuzzer absolviert438082 Ausführungen in31 Sekunden
ohne erkannten Fehler; Initialisierung nach1434 Ausführungen, also zusätzliche
Mutationen im Lauf. Die anderen fünf Fuzz-Ziele wurden für diesen Nachtrag nicht
erneut ausgeführt. Echte SDK-Header sind keine Simulation von U11, DMA, USB oder
Laufwerksmechanik. Die Compilerprofil-Varianten wurden gebaut, nicht auf einem
emulierten oder realen MCU ausgeführt.

Lokale Prüfartefakte unter `firmware/tests/build/`: `cpu_offline_gate.log`,
`cpu_offline_gate.json`, `cpu_asan.log`, `cpu_static.log`, `cpu_fuzz.log`,
`cpu_arm_profiles.json`, `cpu_optimized_build.log`, `cpu_O3_build.log` und
`cpu_Os_build.log`. Vorheriges Decoderobjekt zur Instruktionsgegenüberstellung:
`mfm_before_cpu.obj`; `mfm_before_cpu.asm` sowie die aktuelle Disassemblierung
`mfm_after_cpu.asm`. Frische Release/O2-Voreinstellung und die Ablehnung eines
unbekannten Profilwerts wurden zusätzlich durch CMake-Konfiguration geprüft.

Weitere Entscheidungen über ITCM-Platzierung, DMA-/Cache-Größen, LTO,
Funktions-Inlining und Interruptbudgets benötigen belastbare MCU-Messungen.
Hohe CPU-Auslastung ist kein Selbstzweck: Echtzeitpfade brauchen Reserve.

Standard-BIN SHA256:
`a7967aac676c5b244c6077cf147d4613055e8b74823774f7c1690c4542419a8f`.
