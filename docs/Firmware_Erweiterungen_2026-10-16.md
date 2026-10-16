# Firmware-Erweiterungen: überprüfter Stand vom 16.10.2026

Umfang dieses Arbeitsschritts: Firmware, zugehörige Tests, Build-Prüfung und
Dokumentation. Vorhandene Änderungen an Hostsoftware und Platine wurden erhalten.
Es wurde nichts veröffentlicht und keine Hardwareprüfung durchgeführt.

## Umgesetzt

- Analysepläne lassen sich über Op30 auf SD sichern und nach Reset ausdrücklich
  wiederherstellen. Zwei wechselnde CRC-gesicherte Datensätze, Sequenznummer,
  Rücklesen nach dem Speichern und Bindung an Medien-ID sowie Betriebsprofile
  schützen vor unbemerkter Wiederaufnahme eines falschen Plans. Ein fehlgeschlagenes
  Speichern setzt den RAM-Cursor zurück. Wiederherstellung startet keine Operation.
- Op31/32 ergänzen das Prüflesen unbekannter Rohspuren um ein normalisiertes
  Zeit-/Dichteprofil mit 64 Fenstern und acht Intervallklassen. Das Tool erhält
  Abweichungen pro Fenster; optional gilt fehlende Übereinstimmung als Schreibfehler.
  Der bisherige Inhaltsprüflevel wird dadurch nicht angehoben.
- SD-Aufnahmen beginnen als `.PRT` und erhalten erst nach erfolgreichem Abschluss
  ihren endgültigen Dateinamen. Vorhandene unvollständige Aufnahmen werden nicht
  überschrieben. Ein nicht lesbares Dateisystem wird nicht mehr automatisch formatiert.
- SD-Dumpformat4 erzeugt HFEv3 direkt in der Firmware, einschließlich sicherer
  Kodierung von Datenbytes, die sonst Opcodes wären. Zwei Köpfe verwenden getrennte
  256-Byte-Seitenhälften. Der vorhandene Puffer genügt; kein zweiter Spurpuffer nötig.
- Ein fünfter externer Referenzkorpus prüft HFEv3-Kodierung und Dekodierung auf
  84 Spurseiten mit 8.661.040 Zellen. Download und SHA256 sind festgelegt.
- Fuzzing deckt zusätzlich Profilvergleich, HFEv3-Encoder sowie Job- und
  Ressourcenübergänge ab. Ein dabei gefundener Fehler an Fenstergrenzen bei
  gleichmäßiger Geschwindigkeitsänderung wurde behoben und als Regression getestet.
- `firmware/tools/repro_build.py` erstellt zwei frische ARM-Builds mit festgelegten
  Werkzeugversionen und Builddatum und vergleicht die Binärdateien. Das Manifest
  enthält Quell-, verwendete Herstellerdatei- und Binärhashes. CI-Actions sind auf
  Commit-Hashes festgelegt.

## Neue Flags

Die Bits gehören zu getrennten Operationsfeldern, nicht zu einer globalen Maske.

| Operation | Flag | Wert | Zweck |
|---|---|---|---|
| Op30 | PERSIST_CHECKPOINT | 0x0001 | Analysefortschritt auf SD sichern |
| Op30 | MEDIA_RECONFIRMED | 0x0002 | Medienzuordnung vor Wiederherstellung erneut bestätigen |
| Op31 | RAW_TIMING_PROFILE | 0x01 | Zeitprofil beim Prüflesen erfassen |
| Op31 | REQUIRE_TIMING_MATCH | 0x02 | Übereinstimmung verlangen; benötigt 0x01 |

Wire-Layouts, Kombinationen, Fehlerfälle und Reportfelder:
[USB_Protokoll.md, Abschnitt4.23](USB_Protokoll.md#423-dauerhafte-analysepläne-rohspurprüfung-und-sd-abschluss).
Bestehende Greaseweazle-Kommandos und das 32-Byte-Op28-Layout bleiben erhalten.

## Durchgeführte Prüfungen

- Gesamtlauf mit `UFI_ANALYZE=1` und `check_all.py --require-corpora`:
  **23 von 23 Prüfgruppen erfolgreich, keine übersprungen**.
- Darin: 19 native Testsuiten, Firmware-Regressionen einschließlich Greaseweazle,
  fünf externe Referenzkorpora, 166 Host-Regressionstests und 23/23 IEC-6502-Prüfungen.
  Hosttests dienen hier nur der Kompatibilitätsprüfung.
- Gezielte Linux-GCC-Prüfungen für HFEv3, Rohprofil, Analyseplan und SD-Laufzeit/Export
  ebenfalls erfolgreich.
- Erweiterter Fuzzer: **2.242.702 Durchläufe in 61 Sekunden** ohne weiteren Befund
  unter Windows AddressSanitizer. Das ist eine begrenzte Kampagne, kein Fehlerfreiheitsbeweis.
- ARM-Build erfolgreich. Normaler Build: FLASH191004 Bytes; AXI293528 Bytes belegt,
  34152 Bytes frei; statisches DTCM107520 Bytes, Stack23552 Bytes, Heap8192 Bytes.
- Zwei frische Windows-ARM-Builds sind byteidentisch. Werkzeuge: GCC14.2.1,
  CMake4.4.3, Ninja1.13.2, STM32CubeH7 FW.H7.1.11.0. Manifest erfasst 88 Projektdateien
  und 84 tatsächlich verwendete Herstellerdateien.

Normaler Build `firmware/build/ufi_firmware.bin`, SHA256:
`9301b6579cc2f168e6635df2483fb640eca13f193b14556847e464e568778b74`.

Reproduzierbarer Build, SHA256:
`770faeac72e8325360ecff36a5b7972c8f3f44e0c3dd6ac563c635fcc6da731f`.
Dieser verwendet bewusst eine aus den Quellen abgeleitete Versionskennung und ein
festes Datum statt der normalen Arbeitsbaumkennung; deshalb unterscheidet er sich
vom normalen Build. Lokales Manifest:
`firmware/tests/build/repro-pprmdjv7/manifest.json`.
Gesamttestprotokoll: `firmware/tests/build/offline_gate.json`.

## Grenzen und verbleibende Arbeit

- Elektrisches Verhalten, DMA-/USB-Echtzeit unter Last, U11-Durchsatz und reale
  Schreib-/Lesequalität bleiben bis zum Platinenaufbau unqualifiziert.
- Die Medien-ID ist eine vom Tool bestätigte Zuordnung, kein physischer Identitätsbeweis.
  Wiederherstellung verlangt passende Profile und erneute Bestätigung.
- Zwei Checkpoint-Dateien und `.PRT` verhindern viele unbemerkte Teilzustände,
  garantieren aber keine Stromausfall-Atomizität der SD-Karte oder FAT-Metadaten.
  Allgemeines Host-WRITE_FILE ist weiterhin nicht transaktional.
- Rohprofile sind verlustbehaftet und können gleiche Ergebnisse für unterschiedliche
  Spuren liefern. Sie ersetzen weder Sektor-Inhaltsprüfung noch vollständigen
  Fluxvergleich. Intervallwerte nahe Klassengrenzen können empfindlich reagieren.
- HFEv3-Export bewahrt rekonstruierte Zellen bei nominaler Zellzeit und Index;
  beliebige originale Fluxzeiten und physische Weak-Effekte werden nicht bewahrt.
  Für rohe Zeitdaten bleibt SCP vorgesehen. Der neue Korpus qualifiziert keine
  real beschädigten oder kopiergeschützten Datenträger.
- Reproduzierbarkeit ist für zwei Windows-Builds nachgewiesen, noch nicht zwischen
  Windows und Linux. Der normale CI-ARM-Build verwendet weiterhin die dort verfügbare
  Paket-Toolchain. Ein vollständig hermetischer Releaseprozess ist damit nicht belegt.

## Referenzgrundlagen

- Externes HFE-Referenzabbild: [FluxFox, festgelegter Commit](https://github.com/dbalsom/fluxfox/tree/1d72ff1b329cc072cec00fe42ae51bededf5374f/tests/images/transylvania).
  Abruf und Lizenzhinweise stehen in `firmware/tests/reference/hfe_v3_reference.json`;
  das Abbild wird nicht ins Repository aufgenommen.
- Festes Builddatum über [GCC SOURCE_DATE_EPOCH](https://gcc.gnu.org/onlinedocs/gcc/Environment-Variables.html).
