# Firmwareprüfung vom 22.10.2026

Geprüfte Basis: Commit `13b1c65`, Firmware `1.18-dev`. Vorhandene lokale Änderungen
an der KiCad-Projektdatei und `claude.txt` wurden nicht bearbeitet. Prüfung ohne Hardware.

Der erste Teil dokumentiert die anfängliche Bestandsprüfung. Der Nachtrag am Ende
beschreibt die anschließend umgesetzten Firmware-Erweiterungen und den aktuellen
Build; seine Speicher- und Prüfangaben ersetzen die älteren Zwischenstände.

## Ergebnis und Befunde

Der ARM-Build und der vollständige Offline-Testlauf bestehen. Ein Fehler in der
Testinfrastruktur wurde gefunden und korrigiert: Der Provenienzblock liest inzwischen
MCU-UID und DBGMCU-IDCODE, aber `tests/hal_stub/stm32h7xx_hal.h` stellte diese Symbole
nicht bereit. Dadurch scheiterten die statische Analyse und der v2-Fuzzer beim
Kompilieren. Die ergänzten Definitionen verweisen auf Host-Speicher statt auf physische
MCU-Adressen. Der produktive Firmwarecode wurde bei dieser Prüfung nicht geändert.

Die wichtigsten noch offenen Punkte:

1. **Kaum Spielraum oberhalb der AXI-Mindestreserve:** 33016 Bytes sind frei, also
   nur 248 Bytes mehr als die geforderten 32768 Bytes. Das Budget besteht; zusätzliche
   statische AXI-Daten erfordern eine bewusste Speicherentscheidung. Das bedeutet
   nicht, dass insgesamt nur 248 Bytes RAM frei sind.
2. **CI ist kein vollständiger Korpus-Freigabetest:** Der Workflow lädt drei externe
   Korpora und startet `check_all.py` ohne `--require-corpora`. Die lokalen MOOF-/HxC-
   Archive dürfen deshalb fehlen. Der hier ausgeführte lokale Lauf verlangt und
   prüft dagegen alle fünf Korpora.
3. **Hardwarequalifikation bleibt offen:** USB-/DMA-Echtzeit, U11, elektrische
   Laufwerksansteuerung, reales Prüflesen und die physische Wirkung der Weak-Marker
   sind durch Hosttests nicht nachgewiesen. Die statische Analyse deckt zudem
   15 hardwareabhängige Module nicht ab.
4. **Weiterhin zurückgestellte Funktionen:** Laut aktuellem Implementierungsprotokoll
   fehlen die v2-Ausgabe gespeicherter Umdrehungen und ein qualifiziertes Schreibfenster
   für hartsektorierte Medien. Der ältere Bericht vom 16.10. bildet die inzwischen
   hinzugekommenen Funktionen nicht vollständig ab.

## Aktueller Funktionsstand

Seit dem Bericht vom 16.10. wurden unter anderem implementiert: zusätzliche
Schreibschutz-/Diagnosefunktionen, Provenienzblock, sektorspezifischer Prüfbericht,
Drehzahlprüfung, Index-Startversatz, feinere Weak-Fenster, Fortschrittsereignisse,
Spurübersicht als Firmwarejob (Op46), Weak-Marker im erweiterten Schreibpfad und
separates Rohspur-Prüflesen über WRITE `flags2` Bit4. Die aktuellen Regressionen
prüfen auch diese Erweiterungen. Rohspurprofile bleiben verlustbehaftete Vergleiche,
kein vollständiger Inhaltsnachweis.

## Erneut ausgeführte Prüfungen

- `cmake -S firmware -B firmware/build` und `cmake --build firmware/build`: erfolgreich.
  Warnungen kamen aus den STM32Cube-USB-/FatFs-Quellen.
- `UFI_ANALYZE=1` mit `firmware/tests/check_all.py --require-corpora`:
  **25/25 Prüfgruppen erfolgreich, keine übersprungen**; darin 19 native Testsuiten,
  fünf Referenzkorpora, **197 Host-Kompatibilitätstests**, Greaseweazle-Regressionen
  und **23/23 IEC-6502-Prüfungen**.
- `firmware/tools/check_static.py` nach Korrektur: **12 HAL-unabhängige und
  23 Module mit HAL-Testdefinitionen erfolgreich**, 15 Module ausgenommen.
- `firmware/tests/run_fuzz.py --target all --seconds 15`: alle fünf Ziele unter
  Windows AddressSanitizer erfolgreich. Parser626803, v2-Dispatcher9863,
  Greaseweazle8237, SD3539 und Laufwerkssteuerung46023 Durchläufe.
  Bei v2 und SD verbrauchte schon das Einlesen des vorhandenen Korpus die
  Zeitvorgabe; dort belegt dieser Lauf einen erfolgreichen Korpus-Replay,
  keine zusätzliche Mutationskampagne. Protokoll:
  `firmware/tests/build/review_fuzz_2026-10-22.log`.

Testprotokoll: `firmware/tests/build/offline_gate.json`.

## Build und Speicher

| Bereich | Belegt / Reserve |
|---|---:|
| FLASH | 204604 Bytes belegt |
| AXI | 294664 Bytes belegt / 33016 Bytes frei |
| DTCM statisch | 109984 Bytes |
| DTCM Stackbereich | 21088 Bytes |
| Heap | 8192 Bytes |
| SRAM1 | 9920 Bytes belegt |
| SRAM2 | 8192 Bytes belegt |

Binärdatei: `firmware/build/ufi_firmware.bin`, eingebettete Revision `13b1c65+`.
Das Plus entsteht bereits durch die vorhandene Änderung an einer getrackten
KiCad-Datei; es bedeutet nicht allein eine geänderte Firmwarequelle.
SHA256: `0b38d782ddbb237d3ae0a001b66b71d33a52f3e186372a684a5e73cbfe8eaccb`.
Der Reproduzierbarkeitsnachweis vom 16.10. wurde in diesem Prüflauf nicht erneuert.

## Nachtrag: Umsetzung der acht Verbesserungsbereiche

Die Firmware enthält jetzt folgende Erweiterungen. Das Host-Tool wurde nicht geändert.

| Bereich | Umsetzung |
|---|---|
| Erhaltene Aufnahme | Op48: Manifest und adressierbare Timestamp-Blöcke; ID, Inhaltsfingerabdruck und CRC32 verhindern versehentliches Vermischen veralteter Aufnahmen. Op52: kooperative erneute Analyse mit ursprünglicher Position ohne Laufwerkszugriff. |
| Genaueres Roh-Prüflesen | Op50: optionale U11-Referenz für den tatsächlich ausgegebenen nativen u32-Flux. Beide Rückleseumdrehungen werden flankenweise verglichen. Op54: seitenweiser Differenzbericht; beobachtende oder strenge Policy. |
| Sektorvarianten | Op49: bis64 getrennte Inhaltsvarianten mit CHRN, CRC32/FNV-1a, Positionen, Befunden und Umdrehungs-/PLL-Masken; Überlauf wird gemeldet. |
| Informationsgewinn | Op11 Policy3 registriert neue gültige CHRN+CRC32-Identitäten; bei ausbleibendem Gewinn frühzeitiger Abschluss. Unvollständige Registrierung verhindert einen falschen Stillstandsbefund. |
| Mehrfachlese-Statistik | Op51: Zuordnungen, fehlende/zusätzliche Flanken, mittlere absolute/vorzeichenbehaftete und maximale Zeitabweichung in128 Fenstern. |
| Formatregeln | AmigaDOS OFFSET-Regel1 prüft Sektoren-bis-Lücke gegen1…11/22, mit Rohwert und Regelkennung. Bestehende Befunde anderer Formate bleiben nutzbar. Allgemeine Kopierschutzklassifikation bleibt ungeprüft. |
| USB-Wiederaufnahme | Fertige, finalisierte Daten bleiben über den USB-Reset im laufenden MCU erhalten. Abruf ist idempotent und an Manifest/ID/Fingerabdruck gebunden; aktive physische Vorgänge werden abgebrochen. |
| Qualitätsvergleich | `firmware/tests/benchmark_quality.py`: sieben deterministische Fälle mit Messwerten als JSON. Vier gezielte Fehler erkannt, drei Kontrollen ohne Fehlalarm. Der konfigurierte Histogrammvergleich akzeptiert diese vier subtilen Fehlerfälle. |

Das Benchmark-Ergebnis ist ein Nachweis für die ausgewählten Softwarefälle und kein
Nachweis, dass die Hardware KryoFlux übertrifft. Speicherung über Stromverlust oder
MCU-Neustart ist nicht garantiert; Fingerabdrücke sind nicht kryptographisch.
Roh-Livestreams werden nicht nachträglich als erhaltene Vollaufnahme angeboten.
Die präzise Schreibprüfung unterstützt den nativen indexgebundenen u32-Pfad mit
Quellperiode; Splice-Offset, Gate-Zugaben und u16-Pfad werden vor WGATE abgelehnt.
Generische PROTECTION-/DATA_ABSENT-Befunde bleiben reserviert. Neue Protokolldetails
und Grenzwerte stehen in [USB_Protokoll.md](USB_Protokoll.md), Abschnitte4.19–4.22.

Ausgeführte Prüfungen:

- Vollständiges Offline-Gate mit `--require-corpora`: **28/28 Prüfgruppen bestanden**,
  nichts übersprungen;20 native Suiten,197 Host-Kompatibilitätstests, alle fünf
  externen Korpora und23/23 IEC-Prüfungen.
- Neue Produktionscode-Tests: U11-Aufteilung inklusive DMA-Überlaufchunk,
  Referenzerhaltung, präzise Fehlererkennung, strenge/beobachtende Policy,
  malformed Timestamps, Abbruch, Speicheranalyse, wiederholbare USB-Seiten,
  Metadata-CRC, veraltete IDs/Fingerabdrücke, USB-Reset und Variantenüberlauf.
- Ergänzter Einzeltestlauf nach dem Gate: Sektorbefunde **41/41**, inklusive
  Variantenüberlauf; Analysejob-Harness inklusive Fingerabdruckabweichung und Op51.
- Linux GCC mit ASan/UBSan: Sektorbefunde41/41, Flankenvergleich, präziser
  Prüflese-Runtime, Capture-/USB-Reset, Analysejobs und Benchmark erfolgreich.
- Statische Clang-Analyse: **36 Module erfolgreich** (12 HAL-unabhängig,24 gegen
  HAL-Stubs);15 Module mit realen HAL-/USB-/FatFs-Abhängigkeiten bleiben außerhalb.
- ARM-Build einschließlich Linker-/Speicherbudgetprüfung erfolgreich;
  die neue leere Watchdog-Bedingung erzeugt nach der Korrektur keine Compilerwarnung.

Aktueller Build `firmware/build/ufi_firmware.bin`, Revision `13b1c65+`:

| Bereich | Aktueller Wert |
|---|---:|
| FLASH | 214304 Byte |
| AXI belegt / frei | 294736 / 32944 Byte |
| Abstand zur geforderten freien AXI-Reserve32 KiB | 176 Byte |
| DTCM statisch / Stackbereich | 109984 / 21088 Byte |
| Heap | 8192 Byte |
| SRAM1 / SRAM2 / SRAM4 belegt | 13808 / 8192 / 5668 Byte |

SHA256: `dcf2544ca6f4024b491124c1cfc7d5bedffe12df782ad11f0132a373a2d93787`.
Die optionale Referenz nutzt die obere Hälfte von U11; Capture inklusive DMA-Scratch
bleibt in der unteren Hälfte. Deaktivieren gibt den Speicher wieder frei. Keine
zusätzlichen großen AXI-Arrays. Auf der Platine bleiben U11-Durchsatz, elektrische
Signale, Indexjitter, DMA-Latenz und Schreib-/Prüfleseverhalten zu qualifizieren.
Es bestehen keine offenen Software-Prüffehler in diesem Änderungsumfang.

## Nachtrag: Flaggesteuerte Kopierschutz-Hinweisanalyse

Die sechs vorgeschlagenen Prüfgruppen sind über Op55 auswählbar; die Firmware
liefert über Op56 einen generationgebundenen, begrenzten Bericht. Hinweise,
Kandidaten, bekannte Signaturbestandteile und unzureichende Untersuchung bleiben
getrennt. Standardmäßig aus. Aktivierung mit Standardgrenzen:
`[55, 1, 0x3F, 0, 3, 2, 30, 0]` über `ANALYSIS_JOB 0x0F`.
Details, alle Felder und Regelkennungen: [USB_Protokoll.md](USB_Protokoll.md),4.23.

Implementiert sind Regeln für gültige/CRC-fehlerhafte Inhaltsvarianten, stabile
CRC-Fehler, Kodierungsfehler, doppelte Kennungen, Kopfpositionen, gemischte bzw.
nicht unterstützte Längen, lokale Zellzeitabweichungen, lange Fluxlücken,
Positionsversatz und veränderte zyklische Sektorreihenfolge. Defekte und legitime
Sonderformate bleiben mögliche Ursachen eines Kandidaten. Es gibt keine Aussage
„ungeschützt“ und keine automatische Reparatur oder Datenänderung.

Die erste konkrete Signaturregel untersucht modernen Copylock-Sektor6:
Sync, Sektor-ID, Kenntext, vollständige LFSR-Datenfolge, Clockbits und Abschluss
müssen passen und in der konfigurierten Zahl von Umdrehungen wiederkehren.
Sie bestätigt diesen Bestandteil, nicht den vollständigen Track/Spielschutz.
Alte/erweiterte Varianten und über den Aufnahmeindex laufende Signaturfelder
bleiben offen. `SF_PROTECTION` in Op47 bleibt reserviert; Op56 enthält die
regelgebundene Aussage. Erkenntnisse aus PLL-Alternativen werden nicht als
unabhängige Lesungen gewertet; doppelte CHRN werden nicht mit Weak-Verhalten
gleichgesetzt.

Prüfergebnis:

- Vollständiges Offline-Gate: **28/28 Gruppen**,21 native Suiten, fünf externe
  Korpora,197 Hosttests und23/23 IEC-Prüfungen; keine ausgelassenen Gruppen.
- Neue Schutzanalyse zunächst169/169 im Gate, anschließend erweitert auf
  **179/179** im gezielten Clang- und GCC-ASan/UBSan-Lauf. Gegenbeispiele umfassen
  falschen Sync/Index/Kenntext, beschädigte und degenerierte LFSR-Daten,
  abgeschnittenes Feld, falschen Abschluss und illegale Clockbits. Positive
  synthetische Signatur auch mit etwa5% abweichender Zellzeit erkannt.
- Negativkontrollen: gewöhnlicher IBM-Flux bei allen sechs Flags, sauberer
  Amiga-Track in der Runtime-Prüfung, Sektorbeobachtungen aller sieben
  Decoderfamilien, feste Interleave-Reihenfolge und zyklische Rotation.
- Runtime-/USB-Tests prüfen atomare Einstellungen, Abweisung unbekannter Flags,
  aktive/inaktive Zustände, alte Berichtssnapshots nach Umkonfiguration,
  generationgebundene Seiten, automatische Rohvergleichsanforderung und
  unzureichende Ein-Umdrehungs-Aufnahmen. Mit ASan/UBSan erfolgreich.
- Neuer ASan-Fuzzer `run_fuzz.py --target protection --seconds 15`:
  **56585 Ausführungen in16 Sekunden**, ohne Fund. Corpus/Log bleiben unter
  `firmware/tests/build/fuzz_protection_corpus` bzw. `protection_fuzz_result.log`.
- Statische Analyse **37 Module** erfolgreich (13 HAL-unabhängig,24 gegen Stubs;
  15 außerhalb der Analyse). ARM-Build und Speicherbudgetprüfung erfolgreich.

Aktuellster Build ersetzt die vorherigen Binär-/Speicherangaben:

| Bereich | Wert |
|---|---:|
| FLASH | 220768 Byte |
| AXI belegt / frei | 294736 / 32944 Byte |
| DTCM statisch / Stackbereich | 109984 / 21088 Byte |
| SRAM1 / SRAM2 / SRAM4 | 13808 / 8192 / 12396 Byte |
| Schutzanalysezustand in SRAM4 | 6724 Byte |

`firmware/build/ufi_firmware.bin`, SHA256:
`311019f603107689c3ae2c51210ec0197b9a3bda06bd4b1a7aab5eefdf1a1b56`.
Keine Host-Tool- oder Platinenänderung in diesem Schritt. Offen bleiben die
Hardwarequalifikation und die Validierung an einem beschrifteten Corpus realer
geschützter Disketten. Der vorhandene Decoder-Corpus und synthetische
Signaturtests belegen keine universelle Erkennungsquote.
