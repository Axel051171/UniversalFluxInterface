# Entwicklung und Tests

**Sprache / Language:** [Deutsch](Entwicklung-und-Tests) | [English](Development-and-Tests)

Die Software wird ohne Laufwerk auf dem Host geprüft und zusätzlich für den STM32 gebaut. Hardware-Timing und reale elektrische Signale benötigen davon getrennte Messungen.

## Repository

| Pfad | Inhalt |
|---|---|
| `firmware/src`, `firmware/include` | STM32-Anwendung und Schnittstellen |
| `firmware/tests` | Native C-Tests, Kontrollflussprüfungen, Modelle, Korpora und Fuzz-Harnesses |
| `firmware/tools` | Statische Analyse, Speicher-/Stackprüfungen, 6502-Assembler und Hilfswerkzeuge |
| `software/ufi_host` | Python-Host und Tests |
| `kicad/UFI_Headless` | Schaltplan, Layout und Fertigungsunterlagen |
| `docs` | Protokoll, Nachweise und Messpläne |
| `docs/wiki` | Veröffentlichbare Wiki-Seiten |

## Offline-Gate

In einer geeigneten Python-Umgebung aus dem Projektstamm:

```console
python -m pip install -r requirements-test.txt
python firmware/tests/check_all.py
```

Das Gate baut native Programme aus Produktionscode, führt Firmware-Kontrollflussprüfungen und Host-Tests aus und prüft den 6502-Laufwerkscode. Der Bericht liegt im Test-Buildordner als `offline_gate.json`.

Einige Prüfungen extrahieren Produktionsfunktionen unverändert und setzen Treiber-Doubles ein. Sie testen den betreffenden Kontrollfluss, nicht eine taktgenaue STM32-Simulation. Host-Fakes sollten durch Tests an den Modulgrenzen ergänzt werden.

## Externe Korpora

```console
python firmware/tests/test_flux_corpus.py --download
python firmware/tests/test_gcr_corpus.py --download
python firmware/tests/test_hfe_v3_reference.py --download
```

Weitere Korpora benötigen passende lokal bereitgestellte Abbilder. `check_all.py --require-corpora` macht fehlende Korpora zu einem Fehler. Die Quellstände und Prüfsummen der Downloads sind in den jeweiligen Skripten festgelegt.

## Statische Analyse und Fuzzing

```console
python firmware/tools/check_static.py
python firmware/tests/run_fuzz.py --seconds 30
```

Die portable Analyse deckt HAL-freie Module und einen Teil der HAL-gebundenen Module mit Stub-Headern ab. Nach einem echten ARM-Build erweitert diese Variante den Umfang:

```console
python firmware/tools/check_static.py --compile-commands firmware/build/compile_commands.json
```

Fuzzing verwendet auf geeigneten Plattformen Sanitizer. Kurze Läufe sind Rauchtests; längere Läufe und gespeicherte Fehlerinputs verbessern die Abdeckung. Plattformabhängige Sanitizer-Verfügbarkeit im Ergebnis berücksichtigen.

## Speicher und Stack

Der Firmware-Build prüft Speicherbereiche, einzelne Stackframes sowie Aufrufketten und Interruptverschachtelung. Indirekte Funktionsaufrufe und angenommene Größen externer Funktionen begrenzen die Aussage des statischen Aufrufkettenmodells. Laufzeit-High-Water-Messung ergänzen.

## Änderungen prüfen

Bei Protokolländerungen Firmware, Host, Masken, Ereignispuffer, Besitzerprüfung und [Protokollbeschreibung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md) gemeinsam aktualisieren. Schreibpfadänderungen brauchen zusätzlich einen passenden Messfall im Abnahmeplan.

Quellen: [Testübersicht](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tests/README.md), [CI](https://github.com/Axel051171/UniversalFluxInterface/blob/main/.github/workflows/firmware.yml), [Callgraph-Prüfung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tools/check_callgraph.py).
