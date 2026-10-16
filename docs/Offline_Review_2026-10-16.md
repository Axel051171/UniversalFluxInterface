> Historischer Zwischenstand. Die nachfolgenden Firmwarekorrekturen und aktuellen Prüfergebnisse stehen in [Firmware_Abschluss_2026-10-16.md](Firmware_Abschluss_2026-10-16.md).

# Offline-Verbesserungen und verbleibende Arbeiten

## Geprüfte Originalquellen

- [Greaseweazle-Protokoll](https://github.com/keirf/greaseweazle-firmware/blob/master/inc/cdc_acm_protocol.h):
  getrennte Kommando-/Datenphasen, Abschlussstatus, optionale Felder und Reset.
- [Greaseweazle-Firmware](https://github.com/keirf/greaseweazle-firmware/blob/master/src/floppy.c):
  Längenprüfung vor READ-/WRITE-Vorbereitung. Vergleich mit den nativen UFI-
  Übergängen; keine fremden Hardware-Register oder Timingwerte übernommen.
- [Greaseweazle-HFE](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/image/hfe.py):
  v3 bewahrt variable Raten, schwache Bereiche und Indexmarken. Bereits vorhandener
  UFI-Hostimport berücksichtigt diese Trennung; physische Weak-Ausgabe bleibt offen.
- [Write Precompensation](https://github.com/keirf/greaseweazle/wiki/Write-Precompensation):
  format- und spurabhängige Kompensation. Aus Beispielen lassen sich keine
  kalibrierten Werte für unsere noch nicht getestete Schreibkette ableiten.
- [Clang Static Analyzer](https://clang.llvm.org/docs/ClangStaticAnalyzer.html):
  pfadsensitive symbolische Analyse als zusätzliche Offline-Prüfung.

Quellen am16.10.2026 online gelesen; Links auf die Entwicklungszweige sind
beweglich. Keine fremden Programme ausgeführt, kein Quellcode importiert.

## In diesem Durchgang umgesetzt

1. Nativer Schreibstart0x27 mit erwarteter Konfigurations- und Mediengeneration;
   erneute Medienprüfung nach Vorbereitung. DATA/END bleiben0x22.
2. Analyse-Op26 mit beiden Generationen; bestehendes Op24 bleibt kompatibel.
3. Native READ-Parameter vollständig vor Konfigurationsänderung prüfen;
   Hartsektor-Einstellung bei fehlgeschlagenem Start wiederherstellen.
4. WRITE-Hartsektor-Einstellung erst nach erfolgreicher Vorbereitung setzen.
5. Fehlerhafte Compact-Längen vor dem Löschen alter Prüfevidenz abweisen.
6. Fähigkeitsmatrix berücksichtigt die Greaseweazle-Ressourcenreservierung.
7. Hostunterstützung, verständlicher Status21, Parameterprüfung vor Joböffnung
   und Freigabe eines eigenen Integritätsjobs bei negativer Schreibannahme.
8. Exhaustive Tests der256 Flagwerte über relevante READ-/WRITE-Längen,
   stale-token-/Medienwechseltests und Host-Wiretests.
9. Wiederholbarer Clang-Prüfschritt für zehn hardwareunabhängige Produktionsmodule
   in `firmware/tools/check_static.py`, außerdem als CI-Schritt eingebunden.

Keine neuen RAM-Puffer und keine weiteren Auftragsflags. Vorhandene Fehlercodes
und versionierte Discovery reichen. Die neue Generationsprüfung ist keine
Mehrspurtransaktion und kann einen physisch nicht erkannten Medienwechsel nicht
erkennen. Eine erschöpfende Fehlerfreiheit ist durch diese Tests nicht bewiesen.

## Validierung

Vollständige Offline-Suite bestanden:161 Hosttests, native/runtime/GW-Prüfungen,
SCP/GCR/MOOF/HFE-Korpora und23/23 6502-Prüfungen. Abschließende Änderungen an
mediengebundenen Starts separat mit Produktionscode- und Hosttests geprüft.
Clang-Analyse der zehn Module ohne Befund. CI-Konfiguration ergänzt; die entfernte
GitHub-Actions-Ausführung wurde nicht gestartet.

ARM-Build ohne Warnungen: FLASH184968 Bytes, AXI293080 belegt /34600 frei,
SRAM1 6560, Heap8192, Stackadressraum24576. SHA-256:
`19ad64901d2fe82c474cd7125d68fdeb82b4cfb1ac3157fdb67004e36a7750f7`.

## Weiter offline möglich, noch nicht implementiert

- HFEv3-Export und HFEv3-Lesen direkt vom SD-Speicher der Firmware; derzeit
  unterstützt der Host v3-Import, die SD-Firmware HFEv1. Dafür sind eigene
  Format-/Speichergrenzen und Roundtrip-Tests nötig.
- Weitere unabhängige Referenzkorpora, insbesondere reale beschädigte/geschützte
  Spuren mit bekannter Herkunft und erwarteten Befunden. Zufallsdaten allein
  ersetzen keine solche Referenz.
- Breitere Analyse der HAL-/ISR-Pfade und länger laufendes, coveragegesteuertes
  Fuzzing. Der neue Clang-Schritt umfasst zehn Module, nicht die gesamte Firmware.
- Mehrspur-Auftragsplanung mit Wiederaufnahme. Dafür müssen Abbruch, Medium,
  Ressourcenbesitz und gespeicherte Fortschritte über jeden Spurwechsel hinweg
  zusammenpassen; einzelne Starttoken sind dafür noch kein Ersatz.

## Erst mit aufgebauter Hardware belastbar prüfbar

U11 unter echter DMA-/USB-Last, Cache-Kohärenz im realen Betrieb, Pulsbreiten,
Index-/WGATE-Ränder, Laufwerks-/Medienkompatibilität, Schreibqualität und optimale
Vorkompensation, ED/Perpendicular-Kombinationen, zuverlässige Weak-Bit-Erzeugung,
elektrische IEC-Timings sowie Betriebssystem-/USB-Qualifikation.
Simulierte oder synthetische Erfolge setzen keine Hardwarequalifikationsbits.
