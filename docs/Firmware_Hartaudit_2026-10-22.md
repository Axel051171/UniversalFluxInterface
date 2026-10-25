# Firmware-Hartaudit vom 22.10.2026

## Ergebnis und Umfang

Zwölf konkrete Fehlergruppen beziehungsweise Härtungslücken wurden im aktuellen
Arbeitsstand behoben. Geprüft wurden insbesondere destruktive Schreibpfade,
Interrupt-Start und -Abschluss, Medienbindung, RAM-Initialisierung, Capture-Grenzen,
Mehrspuraufträge, Checkpoints und Kopierschutz-Evidenz. Zusätzlich wurden alle59
Projekt-C-Module mit echten ARM-/Cube-/USB-/FatFs-Headern statisch analysiert.

Dies ist eine Softwareprüfung ohne angeschlossene Platine. Sie ist weder ein Beweis,
dass sämtliche Fehler gefunden wurden, noch eine Hardwarefreigabe oder ein Nachweis
universeller Kopierschutzerkennung. Vorhandene Firmwareerweiterungen und fremde
Änderungen wurden erhalten; keine Host-Produktfunktionen oder KiCad-Dateien geändert.

## Behobene Befunde

| ID | Priorität | Befund und Auswirkung | Korrektur und Nachweis |
|---|---|---|---|
| H01 | Kritisch | Op40 vertraute dem Sektorzähler eines Arrays in `.dtcm`/NOLOAD vor dessen erster Initialisierung. Der Startup-Code löscht diesen Bereich absichtlich nicht. Ein Restwert255 ließ1281 Bytes aus einem326-Byte-Array kopieren; der kleine Testempfänger stürzte ab. | Ein von Startup gelöschtes BSS-Gültigkeitsflag schützt den Bericht. Vor dem ersten Prüfauftrag werden exakt sechs definierte Bytes geliefert. Der Test füllt DTCM gezielt mit `FF` und prüft Länge und Schreibgrenze. `test_quality_runtime.py`. |
| H02 | Hoch | ERASE/PATTERN ignorierten die Rückgabe der Seitenwahl. Ein ungültiger Kopf konnte dadurch zum Löschen oder Beschreiben der zuvor ausgewählten Seite führen; SEEK-Fehler wurden teilweise verfälscht. ERASE hatte zudem keine eigene Prüfung auf belegte Capture-/Schreib-/Streamingpfade oder stehenden Motor. | Beide Routinen geben Positionierungsfehler vor dem Schreibtor weiter und prüfen ihre Voraussetzungen selbst. Tests für ungültige Seite, nicht unterstützte Seite, abgebrochenen SEEK, Streaming und Motor aus. `test_write_safety.py`. |
| H03 | Hoch | ERASE wartete mit `HAL_Delay` bis zu5 Sekunden ohne Abbruchprüfung oder Watchdog-Service. PATTERN bediente den Watchdog, ignorierte aber Abbruch und Änderungen an Schutz-/Mediensignalen. | Gemeinsame, überwachte Wartefunktionen prüfen Abbruch, Sitzungssperre, physische Schreibsperre, Schreibschutz, Motor und Medientoken. Nach Schreibbeginn führt jeder Rückweg über das Schließen beider Schreibfreigaben und Timer-Abschaltung. Tests während Beruhigung, Indexwarten, Schreiben, Apple-Erase und Tick-Überlauf. `test_write_safety.py`. |
| H04 | Hoch | Zwischen Hauptschleifen-Prüfung und dem späteren Index-/Compare-Interrupt konnte ein Abbruch oder eine Schreibsperre eintreffen; `start_now` öffnete trotzdem das Schreibsignal. Beim Beenden wurde erst auf DMA gewartet und danach das Schreibtor geschlossen. | IRQ-sichere Flag-/GPIO-Prüfung unmittelbar im Startpfad, einschließlich des Apple-Starts ohne Index. Der konkrete Fehler bleibt für END/lokale Aufträge erhalten. WGATE/WRREQ werden vor dem DMA-Stopp geschlossen. Abbruch während der Beruhigung liefert CANCELLED statt TIMEOUT. Tests für Index, verzögerten Start und nachträglich gesetzte Sperren. `test_regressions.py`. |
| H05 | Mittel | Die ausdrücklich aktivierte Drehzahlprüfung ließ bei fehlgeschlagener Messung den WRITE weiterlaufen. Op41 konnte dabei den alten erfolgreichen Messwert zeigen. Dieses Verhalten war zuvor sogar als erlaubter Fall getestet und dokumentiert. | Eine fehlgeschlagene aktivierte Messung beendet den Auftrag vor Kopfbewegung und Schreibtor mit ihrem Fehler. Gemessene Dauer wird0, Quelldauer auf den aktuellen Auftrag gesetzt. Absichtliche Verhaltensverschärfung im Protokoll dokumentiert. `test_regressions.py`. |
| H06 | Mittel | Beim erweiterten WRITE mit normalem Prüflesen konnte die Rohreferenz vor der Weak-Marker-Verarbeitung entstehen und damit andere Intervalle beschreiben als die tatsächlich geschriebene Waveform. | Rohreferenz des erweiterten Pfads entsteht nach der Weak-Verarbeitung. Der klassische Pfad behält die Referenz vor seiner überschreibenden u16-Packung. Ein Kontrolltest ersetzt markierte Quelldaten während der Packphase und prüft die anschließende gültige Referenz. `test_regressions.py`; Weak-Verarbeitung selbst zusätzlich durch native Waveformtests abgedeckt. |
| H07 | Hoch | Nach abgeschlossener Aufnahme wurde das Medium geprüft, nach der potenziell längeren Inhaltsanalyse jedoch nicht erneut. Ein Medienwechsel oder Abbruch während dieser Analyse konnte noch einen erfolgreichen WRITE-END mit Prüflesestufe ergeben. | Gemeinsamer erfolgreicher Abschluss prüft Abbruch und Medienbindung nochmals, sonst WRITE_ERROR und Stufe0. Fehlende Revolution0 wird ebenfalls abgefangen. Tests ändern Medium beziehungsweise Abbruchflag direkt im Decoder-Double. `test_sd_runtime.py`. |
| H08 | Mittel | Synthetische Capture-Grenzen aktivierten Interrupts unabhängig vom vorherigen PRIMASK. Die Finalisierung zog außerdem von einem ungeprüften Indexzähler1 ab und vertraute der DMA-Endposition. Bei ungültigem internem Zustand konnte der Zähler unterlaufen beziehungsweise ein Bereich außerhalb des Stores verwendet werden. | PRIMASK wird gespeichert und wiederhergestellt. Fehlende vollständige Indexgrenzen und Endpositionen außerhalb des Stores ergeben CAPTURE_ERROR/DMA, kein Archiv. Der Grenztest ist defensive Härtung; ein regulärer USB-Auftrag, der diesen ungültigen Zustand erzeugt, wurde nicht nachgewiesen. `test_capture_archive.py`. |
| H09 | Mittel | Mehrspurpläne akzeptierten nur Jobflags0–5, obwohl derselbe START-Pfad bereits Bit6/JOB_RESEEK unterstützte. | Beide Masken akzeptieren0x7F. Der Test erstellt und startet einen Mehrspurplan mit JOB_RESEEK und kontrolliert die übernommenen Jobflags. `test_analysis_job.py`. |
| H10 | Mittel | Checkpoints prüften Recovery-/Laufwerksprofile, aber nicht die später ergänzten Decoder- und Kopierschutzeinstellungen. Ein Wiederanlauf konnte unter anderen Regeln fortfahren. Op33/34/36 konnten einen offenen, gerade wartenden Mehrspurplan außerdem verändern. | Dateischema2 bindet die effektiven Decoder-Toleranzen, Apple-Marken und -Tabelle, Evidenzflags, Zusatzzeiten und Kopierschutzoptionen per CRC32 ein. Diese Decoderänderungen sind bei offenem Plan BUSY. Tests für abweichende Toleranz, alte ungebundene Records, unveränderten Wiederanlauf und Slot-Fallback. Alte Schema1-Records werden beim Fortsetzen mit BAD_ARGS abgewiesen. Recordlänge128 und USB-Formate bleiben erhalten. `test_analysis_job.py`. |
| H11 | Mittel | Die Erkennung wiederholter fehlerhafter Nutzdaten verglich nur mit der ersten Variante. A–B–B übersah daher die Wiederholung von B. | Ein gemeinsames, auf64 Einträge begrenztes Register erfasst fehlerhafte Varianten unabhängig von ihrer Ankunftsreihenfolge. Erschöpfung meldet unvollständige Evidenz. Durch Entfernen redundanter Felder bleibt der Klassifikator exakt6724 Byte groß. Tests für A–B–B, fehlende Wiederholung und Registerüberlauf. `test_protection.c`. |
| H12 | Mittel | PATTERN übernahm Zählrichtung, Compare-Wert und Preload-Zustand des vorherigen Waveform-Schreibens. Beispielsweise konnte CCR1=0 zurückbleiben und die erwarteten Schreibimpulse verhindern. | Zählrichtung, Compare-Wert und Preload werden explizit gesetzt. Dauer wird auf1–5000 ms begrenzt. Diagnose-Schreiben besitzt einen eigenen internen Marker, damit normale Track-Ende-Indexbehandlung es nicht vorzeitig beendet. Tests setzen vorher absichtlich den Waveformzustand. `test_write_safety.py`, `test_regressions.py`. |

## Verbesserter Prüfprozess

- `check_static.py --compile-commands ...` verwendet die Definitionen und echten
  Header des Cross-Builds. Alle59 Projektmodule müssen erfasst werden; fehlende
  Module lassen die Prüfung scheitern. Der portable37-Modul-Pfad bleibt verfügbar.
  Die Prüfung analysiert keine Implementierungen der externen Bibliotheken.
- CMake erzeugt automatisch `compile_commands.json` und GCC-Stackdaten.
  `check_stack.py` prüft vor der BIN-/HEX-Erzeugung alle Projektmodule auf fehlende
  Messdaten, unbegrenzte dynamische Frames und Frames über2048 Byte. Die
  Budgetablehnung selbst wird mit positiven und negativen Beispielen getestet.
- Gemessene Stackdaten werden als `firmware/build/stack_usage.json` abgelegt und
  in CI gemeinsam mit dem vorhandenen ELF-Speicherbericht archiviert. Der größte
  Release-Frame ist1080 Byte in `ufi_write_verify`. Das ist kein addierter
  Worst-Case-Aufrufpfad und enthält keine Zusage über verschachtelte Interrupts.
- Die destruktiven Diagnosefälle sind als eigenständige Pflichtgruppe in
  `check_all.py` aufgenommen. Der CI-Build prüft zusätzlich alle Projektmodule
  mit den echten SDK-Headern.

## Ausgeführte Prüfungen

| Prüfung | Ergebnis |
|---|---|
| ARM Release-Build, Speicher- und neuer Stack-Gate | Bestanden; keine Projektwarnungen im inkrementellen Abschlussbuild. Beim vollständigen Neubau vorhandene Warnungen in unverändertem ST-/FatFs-Code. |
| `check_all.py --require-corpora` |29/29 Gruppen bestanden,0 übersprungen;21 native Testsuiten,197 Hosttests,23/23 IEC-Prüfungen und alle fünf externen Korpora. Log: `firmware/tests/build/hard_audit_gate.log`; maschinenlesbar `offline_gate.json`. |
| Statische ARM-Analyse |59/59 Projektmodule mit echten SDK-Headern bestanden. |
| Linux GCC + ASan/UBSan | Kopierschutz181/181; zusätzlich Steuerpfad-, Schreibsicherheits-, Qualitätsruntime-, Analysejob-, SD- und Capture-Archivprüfungen bestanden. |
| Fuzzer | Alle sechs Ziele mit ASan und neuen Mutationen geprüft, keine Fehler. Die unten angegebenen2.232.169 Ausführungen schließen die jeweiligen Korpusläufe ein. |
| Diff-Whitespace-Prüfung | `git -c core.safecrlf=false diff --check -- firmware docs .github/workflows/firmware.yml` bestanden. |

Die Windows-Fuzzer verwenden ASan; ASan **und** UBSan wurden getrennt unter Linux
mit GCC auf den genannten nativen Tests und Produktionsfunktions-Harnesses benutzt.
Stubs ersetzen Hardware und externe Ressourcen; ein Dispatcher-Fuzzer führt deshalb
nicht automatisch den wirklichen DMA-Schreibpfad aus. Diesen ergänzen die gezielten
Register-/Signal-Doubles und der ARM-Build.

| Fuzz-Ziel | Ausführungen gesamt | Davon beim Abschluss der Korpusinitialisierung | Laufzeit |
|---|---:|---:|---:|
| Parser |596091 |1114 |16 s |
| Kopierschutz |58948 |348 |16 s |
| USB-v2-Dispatcher |1380251 |9863 |61 s |
| Greaseweazle |133676 |4567 |16 s |
| SD/Image/Dateikonfiguration |7669 |3539 |61 s |
| Laufwerkssteuerung |55534 |4049 |16 s |

Der erste15-Sekunden-Lauf von v2 und SD endete bereits nach der Initialisierung;
deshalb wurden beide mit60 Sekunden neu ausgeführt. Obige Tabelle enthält die
letzten Läufe je Ziel. Logs: `firmware/tests/build/*_fuzz_result.log`, außerdem
`hard_audit_fuzz.log`, `hard_audit_fuzz_v2.log` und `hard_audit_fuzz_sd.log`.

## Speicher und Artefakt

| Bereich | Vor diesem Hartaudit | Nachher |
|---|---:|---:|
| FLASH |220768 B |221976 B |
| AXI belegt, einschließlich Heap |294736 B |294744 B |
| AXI frei |32944 B |32936 B |
| DTCM statisch |109984 B |109984 B |
| DTCM-Raum bis Stackoberkante |21088 B |21088 B |
| SRAM1 |13808 B |13808 B |
| SRAM2 |8192 B |8192 B |
| SRAM4 |12396 B |12396 B |

Die AXI-Reserve liegt nur168 Byte über dem verbindlichen32-KiB-Gate. Größere neue
statische AXI-Objekte sind damit ausdrücklich keine unproblematische Erweiterung;
der Build stoppt bei Überschreitung. U11 und der vorhandene interne Fallback wurden
nicht vergrößert. Kein zusätzliches Nutzdaten-Duplikat für die Kopierschutzanalyse.

Geprüfte BIN: `firmware/build/ufi_firmware.bin`,221976 Bytes,
SHA256 `77b825f8d8987e89c9099551b43833ca45467411b1a604f05314b1b53609ea3c`.
Version1.18-dev, Git-Kennung13b1c65+; die Plus-Kennung bezeichnet den bestehenden
nicht eingecheckten Arbeitsstand und ersetzt keinen Commit der Änderungen.

## Verbleibende Grenzen und Prüfbedarf

1. Die Platine fehlt: U11/OSPI-Timing, physische Schreibimpulse und Gate-Ränder,
   Interrupt-Latenzen, DMA-/Cache-Zusammenspiel, Stromversorgung und Buspegel müssen
   am Gerät geprüft werden. Diese Eigenschaften sind durch Hosttests nicht bewiesen.
2. Stack-Frames sind gemessen; Bibliotheksaufrufe, Rekursion und Interruptverschachtelung
   erfordern zusätzlich eine Aufrufpfadanalyse beziehungsweise spätere Stack-Messung.
3. Medientoken sind keine physische Diskettenkennung. Insbesondere Busvarianten ohne
   lesbares Disk-Change-Signal bleiben auf korrekte Zuordnung und Host-Mitteilung
   angewiesen. Ein nachträglich erkannter Wechsel kann ein bereits erfolgtes Schreiben
   nicht rückgängig machen.
4. Schutzheuristiken liefern begrenzte Evidenz. Schwache Stellen, absichtlich falsche
   CRCs und ungewöhnliche Layouts haben auch andere Ursachen; weiterhin kein Urteil
   „kopierschutzfrei“. Es fehlen beschriftete reale Schutzdisketten für eine Messung
   von Erkennungsrate und Fehlalarmen. Die vorhandenen normalen Formatkorpora ersetzen
   diese Prüfung nicht.
5. Checkpoint-CRC und Einstellungsfingerprint prüfen Integrität/Konsistenz, keine
   Authentizität oder Gleichheit zweier Firmwarebinärdateien. FAT-Stromausfallfestigkeit
   und U11-Inhalte über Power-Cycles bleiben ohne entsprechende Hardware unbelegt.
