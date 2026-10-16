# Firmwarekorrekturen zur Prüfungs- und Entwicklungsanweisung

> Historischer Zwischenstand. Die anschließenden Erweiterungen, aktuellen
> Prüfergebnisse und Binärhashes stehen in
> [Firmware-Erweiterungen vom 16.10.2026](Firmware_Erweiterungen_2026-10-16.md).

Stand: 16.10.2026. Arbeitsgrundlage: vom Nutzer bereitgestellte
`UFI_Pruefung_und_Entwicklungsanweisung.docx`. Abschließender Umfang ausdrücklich
Firmware, Tests und Dokumentation. Bestehende fremde Änderungen bleiben erhalten.
Keine Hardwarefreigabe, Veröffentlichung oder Bestellung.

## Behobene Firmwareprobleme

- HFEv3 ist tatsächlich mit dem SD-Leser verbunden. Verarbeitung in 256-Byte-
  Seitenblöcken, variable Zeiten, SKIPBITS, Indexrotation und vollständige
  Dateispurprüfung vor dem ersten Schreiben. RAND/Weak und Mehrfachindex werden
  abgewiesen. Der C-Decoder und der produktive SD-Lader sind separat getestet.
- HFEv1 kann einen vollen Ausgabepuffer nicht mehr als vollständige Spur melden.
- SD-Schreiben und direkte Laufwerkskopie verwenden eine vor dem Packing gesicherte
  Inhaltsreferenz. IMG/ADF verlangen alle erwarteten CHRN-IDs. Falsche gültige Daten
  werden trotz gleicher Geometrie abgelehnt. Rohspuren unterscheiden dekodierte
  Referenzsektoren von bloßer Impulszahlplausibilität; unerkannte Bereiche sind
  nicht inhaltlich verifiziert. CRC32-Fingerprints sind kein kryptografischer Beweis.
- Vor dem Rücklesen wird die Mediengeneration der Schreiboperation geprüft.
  Abbruch und erkannter Medienwechsel stoppen einen SD-Schreibauftrag.
- Lokales geprüftes Schreiben klemmt unzulässige Intervalle nicht mehr still.
- Mehrspur-Analyse mit RAM-Cursor und expliziter Ergebnisbestätigung: Op27–29.
  Keine automatische Wiederholung von Schreibaufträgen und keine Behauptung
  dauerhafter Wiederaufnahme nach Reset. Details in USB_Protokoll.md, 4.21.
- GCC-Warnungen in Mac-/GCR-Fixtures, inkonsistente Stub-Bindung und abweichende
  Arrayparameter der Testdoubles korrigiert.

## Nachweise und Grenzen

ARM-Build: FLASH188168 Bytes, AXI293184 belegt /34496 frei, SRAM1 6560 Bytes,
SRAM2 8192 Bytes, DTCM104KiB einschließlich reserviertem Heap/Stack.
Der AXI-Puffer liegt1728 Bytes oberhalb der geforderten32KiB-Freireserve.
Firmware-SHA256:
`82bb4b62e06989c90e0ed54112cf62269f3498af9bab01747302151118a0f597`.

GCC15.2.0 / Ubuntu, Python3.14.4: alle18 nativen Testprogramme sowie
`test_regressions.py`, `test_analysis_job.py`, `test_evidence_runtime.py`,
`test_quality_runtime.py` und die SD-Lader-/Vorlagenvergleichstests bestanden.
Clang unter Windows: elf reine Produktionsmodule ohne Analysebefund;
zusätzlich Analyse der erzeugten HAL-/ISR-Testhüllen. Diese enthalten echte
Produktionsfunktionen mit Testdoubles, keine vollständige STM32-Simulation.

LibFuzzer/AddressSanitizer unter Windows:2559702 Eingaben in31 Sekunden ohne
gefundenen Fehler. Ziel: HFEv3, HFE-Header und fragmentierter Compact-Schreibcodec.
Linux-CI ergänzt UndefinedBehaviorSanitizer; CI wurde lokal konfiguriert, nicht
remote gestartet. Ein kurzer Fuzzlauf beweist keine Fehlerfreiheit.
Referenzen: [LLVM LibFuzzer](https://llvm.org/docs/LibFuzzer.html) und
[Greaseweazle HFE-Parser](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/image/hfe.py).
Es wurde kein weiterer Dateiformatcode aus Fremdprojekten übernommen.

Der vollständige Gesamtlauf bestand: 21 Prüfschritte, 0 fehlgeschlagen, 0 übersprungen; darunter166 Hosttests, alle vier Korpora und23/23 6502-Prüfungen. Die abschließende zusätzliche Medienprüfung beim Rücklesestart wurde danach gezielt mit `test_sd_runtime.py` bestanden und der ARM-Build erneut erstellt.

Der Gesamtlauf verwendet:

```
UFI_ANALYZE=1 python firmware/tests/check_all.py --require-corpora
```

`--require-corpora` lässt fehlende oder veränderte Referenzdateien fehlschlagen.
Ohne diesen Schalter sind optionale fehlende Korpora ausdrücklich SKIPPED und
verhindern die Aussage einer vollständigen Korpusfreigabe. Ergebnisse stehen in
`firmware/tests/build/offline_gate.json`; dort sind tatsächliche Ausführungen,
Ausgaben und Laufzeiten getrennt erfasst. Die Korpora bleiben im ignorierten
Buildspeicher. Eine vorhandene externe Datei allein zählt nicht als Prüfung.

## Bewusst offen

- Reale schwache/geschützte Medien mit unabhängigen Sollbefunden; zusätzliche
  heruntergeladene, noch nicht ausgewertete Dateien werden nicht als bestanden gezählt.
- Dauerhafte Mehrspurfortsetzung nach Reset/Stromausfall samt gesicherter
  Medienzuordnung und persistenter Transaktion.
- HFEv3-SD-Export; SD-Dump bleibt HFEv1. Physische Weak-Ausgabe und Mehrfachindex-
  Schreiben bleiben nicht qualifiziert.
- Frischer Releaseaufbau auf beiden Betriebssystemen mit vollständig festgelegten
  Toolchains/Action-Commits. Die lokalen Gegenchecks ersetzen diese Reproduktion nicht.
- Hardwaretiming, U11 unter realer Last, USB-Abzüge am Gerät, elektrische
  Schreibsperren und physische Wiederlesbarkeit. Firmwaretests beheben keine
  fehlende elektrische Apple-Schreibsperre. Hardwareänderungen sind nach der
  letzten Nutzeranweisung nicht Teil dieses Auftrags.

Die dokumentierten Prüfergebnisse beziehen sich auf Softwareverhalten und die
aufgeführten Testdaten; keine pauschale Überlegenheit gegenüber anderen Geräten.
