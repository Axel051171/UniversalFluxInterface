# HxC-Gotek-Archiv: Prüfung vom 16.10.2026

Eingang: `C:\Users\Axel\Downloads\HXCFEUSB_Gotek_HxC_firmware.zip`
(61.240.792 Bytes, 838 ZIP-Einträge).
SHA-256: `fab679db2536e257f274b0dd1dd44e5cc69005e64e6ea7433dd3b0d096320052`.

## Befund

Das äußere ZIP enthält Updates, Programme, Handbücher, Konfigurationen und
Diskettenabbilder; keine Dateien mit Endung .c/.h/.cpp. Verschachtelte Archive
wurden nicht vollständig untersucht. Release Notes nennen v5.3.1.1a vom
3. August 2026. Keine mitgelieferte EXE, DLL oder Firmware wurde ausgeführt
oder in UFI übernommen. Lizenzen einzelner Hilfsprogramme sind keine pauschale
Lizenz für die Gotek-Firmware.

HxC ersetzt ein Laufwerk gegenüber einem Floppy-Controller. UFI steuert reale
Laufwerke. Deshalb lassen sich Signalrichtungen und Emulationsfunktionen nicht
unverändert übertragen. QuickDisk und Apple II haben eigene Schnittstellen;
ein Formatdecoder allein belegt keine elektrische Kompatibilität.

Gelesen: README, Haupt-Release-Notes, Diagnose-README, WIP-README sowie die
mitgelieferten Handbuchseiten zu Einstellungen und Test-Firmware.
Zusätzlich geprüft: [Herstellerhandbuch](https://hxc2001.com/docs/gotek-floppy-emulator-hxc-firmware/)
und [HFE-Spezifikation](https://www.hxc2001.com/floppy_drive_emulator/HFE-file-format.html).
HFEv2 und v3 enthalten Steuerbefehle im Bitstrom. HFEv3 ermöglicht unter anderem
variable Datenraten und schwache Bereiche. Die öffentliche Opcode-Tabelle
beschreibt RAND missverständlich; eine spätere Implementierung braucht deshalb
zusätzlich überprüfte Referenzvektoren und einen Abgleich mit Quellcode.

## Direkt behobene Fehler

- Firmware-HFE-Headerparser ignorierte die Revision. HFEv2 verwendet dieselbe
  Signatur wie v1, enthält aber Opcodes. Revisionen ungleich 0 werden jetzt
  abgewiesen, damit Steuerbytes nicht als Fluxdaten geschrieben werden.
- Host-HFE-Parser prüft jetzt Headergröße, Revision, Datenrate, Tabellenbereich
  und gelesene Spurbereiche. Abgeschnittene Spurdaten konnten zuvor eine
  Endlosschleife verursachen. Ungerade Spurlängen und Überlappungen mit dem
  Header-/Tabellenbereich werden abgewiesen.
- Keine neue USB-Operation, kein neues Flag und keine Änderung am
  Greaseweazle-Protokoll. Zum Zeitpunkt dieser ersten Korrektur war HFEv3
  noch nicht unterstützt; die Folgeimplementierung steht unten.

Validierung: native Image-Tests 59/59; Host-HFE-Tests 8/8; ARM-Build erfolgreich.
AXI-RAM frei: 36.056 Bytes. Alle sieben direkt enthaltenen HFE-v1-Dateien
(536 Spurseiten) wurden mit dem korrigierten Host-Parser eingelesen. Darunter
Amiga mit 253 kbit/s, Atari ST und CPC mit 250 kbit/s. Das prüft den Container,
nicht Sektorprüfsummen, Kopierschutz oder Hardware-Schreibqualität. Die Abbilder
wurden nicht ins Repository aufgenommen. Keine vollständige Testsuite erneut
ausgeführt; die Prüfungen waren auf diese Änderung begrenzt.

## Ursprüngliche Umsetzungsempfehlung

1. Die HFE-Beispiele als optionalen, hashgebundenen Decoder-Korpus nutzen:
   Amiga, CPC und Atari ST durch echte Firmware-Decoder führen und Sektor-
   prüfsummen/Geometrie prüfen. Die 253-kbit/s-Datei ist ein guter Grenzfall
   für Nominalrate versus tatsächlich erzeugte Zellzeiten.
2. Lokale Datenratenanalyse: Fensterweise geschätzte Zellzeit, Streuung,
   Evidenzmenge und Konfidenz ausgeben. Bestehende TIMING/WINDOWS-Auswahl
   verwenden; ein Ergebnismerkmal `RATE_CHANGE_CANDIDATE` erst nach
   überprüfter Trennung von Datenmustern, PLL-Einschwingen und Drehzahlabweichung.
3. Schwachstellenbericht um die räumliche Beziehung von gültigen Inhaltswechseln,
   Decodierfehlern und fehlenden/zusätzlichen Flanken ergänzen. Bestehenden
   REV_COMPARE-Bericht nutzen. Keine pauschale Kopierschutzbehauptung und
   kein Flag, das eine physisch reproduzierbare Weak-Bit-Schreibfunktion verspricht.
4. Indexquelle im Auswertepfad konsequent berücksichtigen: realer Index,
   externer Sensor, simuliert oder Zeitabschnitt. Vorhandenen Aufnahmekontext
   und RAW_INDEX nutzen. `ALIGNMENT_UNCERTAIN` beibehalten, solange keine
   belastbare Phasenreferenz vorliegt.
5. HFEv3 zunächst im Tool als Container unterstützen; Firmware erhält
   explizite Fluxintervalle und prüft Zeit-/Puffergrenzen. Unterschied zwischen
   zufällig erzeugten Emulatorbits und magnetischen schwachen Bereichen
   erhalten. Vollständiger MCU-Containerparser ist eine separate Erweiterung.

## Folgeimplementierung am 16.10.2026

- Optionaler, SHA-256-gebundener HFE-Korpustest in `check_all.py`: 536 Spurseiten,
  5.000 Sektoren; Header-/Datenprüfsummen, normalisierte C/H/R/N und konsistente
  Inhalte über zwei Durchläufe. 16 zusätzliche Spurseiten ab Zylinder 80 sind
  ausdrücklich ohne decodierte Sektoren erwartet. Keine Referenz-Payloaddatei,
  daher keine Behauptung eines unabhängigen Bytevergleichs mit Originaldisketten.
- 32 lokale Zeitfenster in der Firmware, fixer Zusatzspeicher 1.280 Bytes,
  ohne Heap. Nominalzellen-Schätzer unabhängig vom PLL-Zustand, Datenmengen-
  und Streuungskriterien sowie Datenraten-Kandidaten relativ zum Fenstermedian.
- Decoderfehler und gültige Inhaltswechsel getrennt pro Fenster. Vergleichs-
  flanken aus vier bestehenden Vergleichsfenstern werden im Bericht zusammengeführt.
  Jede Auswertung bleibt generationsgebunden. Phasenausrichtung weiterhin
  unbestätigt; Indexquelle ist über den bestehenden Aufnahmekontext verfügbar.
- Neues INFO-Bit30, Discovery-Op20, Abschnitt 14 (1312 Bytes). Keine neuen
  Auftragsbits. Vollständige Ergebnisflags in USB-Protokoll 4.17.
- HFEv3-Hostimport mit NOP, INDEX, BITRATE, SKIP, RAND, Rate-Wrap und
  36-MHz-Zellzeiten. Deterministische Spuren nutzen Extended WRITE; Firmware-
  Prüflesen erhält die Quellperiode. RAND und mehrere Indexmarken werden
  erhalten, aber mangels physischer Umsetzung nicht automatisch geschrieben.
  HFEv3-Export und ein MCU-HFEv3-Containerparser sind nicht implementiert.

Opcode-Semantik zusätzlich anhand der primären Implementierungen geprüft:
[Greaseweazle HFE](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/image/hfe.py)
(Keir Fraser, Public Domain) und
[FlashFloppy HFE](https://github.com/keirf/flashfloppy/blob/master/src/image/hfe.c)
(nur Referenzvergleich; kein Quellcodeblock übernommen). Insbesondere RAND
belegt acht Zellen, SKIP kann dies verkürzen; der Ratenparameter ist eine
Zellperiode in 1/36 MHz und keine direkte kbit/s-Angabe.

Die Parser-Tests verwenden handprüfbare Opcode-Sequenzen. Das HxC-Archiv selbst
enthält direkt nur HFEv1-Beispiele, keine unabhängigen HFEv3-Kopierschutzdisketten.
