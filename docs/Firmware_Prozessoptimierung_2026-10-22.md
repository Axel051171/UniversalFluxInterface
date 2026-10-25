# Firmware-Prozessoptimierung, 2026-10-22

Dieser Nachtrag beschreibt die Umsetzung der acht Prozessverbesserungen nach
dem [Hartaudit](Firmware_Hartaudit_2026-10-22.md). Änderungen betreffen Firmware,
deren Prüfumgebung, Build/CI und Protokolldokumentation. Die Tool-Anwendung und
vorhandene Platinenänderungen wurden dafür nicht bearbeitet.

## Umgesetzte Änderungen

1. **Kooperatives natives Prüflesen.** Nach dem Schreiben laufen Aufnahme,
   Präzisionsvergleich, Rohprofil und Sektorprüfung als Zustandsmaschine. Die
   Vergleiche bearbeiten höchstens256 Einheiten pro Schritt. Zwischen den
   Schritten bleiben USB und Board-Service aktiv. END entsteht erst nach dem
   endgültigen Ergebnis. Abbruch, Medienwechsel, DMA-Fehler und Zeitüberschreitung
   räumen die eigenen Ressourcen auf. Die bisherigen synchronen APIs für interne
   Aufrufer bleiben erhalten. Wiederholtes Einschalten eines laufenden Motors
   überspringt die Anlaufpause; der erste Anlauf bedient Watchdog und Abbruch.
2. **Validierter Format-Hinweis.** Optionales QUALITY-Feature `0x0008`
   (`FAST_FORMAT_HINT`) probiert ein zuvor ermitteltes Format zuerst. Der Hinweis
   ist an Medium, Konfiguration, Decoderoptionen und Ratenzone gebunden. Mindestens
   zwei vertrauenswürdige Sektorbeobachtungen müssen ihn bestätigen; sonst folgt
   die vollständige Suche. Kopierschutzanalyse und Rohvergleich verwenden die
   vollständige Suche. Bei Nutzung bleibt `ambiguous=1`, weil konkurrierende
   Formate nicht vollständig ausgeschlossen wurden. Standardmäßig ausgeschaltet.
3. **Gemeinsame Fingerprints.** CRC32, FNV32 und ihre Präfixwerte entstehen bei
   Bedarf in einem Durchlauf und werden für Varianten, Inventar und
   Kopierschutzevidenz gemeinsam verwendet. Ohne diese Verbraucher bleibt der
   CRC-Pfad schlank. Eine konstante64-Byte-Tabelle beschleunigt CRC32; Polynom,
   Anfangswert, Verkettung und formatspezifische Schreibreferenzen bleiben erhalten.
4. **Begrenzte Streaming-Arbeit.** Pro Encoder-Aufruf höchstens1024 Flusswörter;
   Cache-Invalidierung umfasst nur die nächste ausgerichtete Teilstrecke. Ringende,
   DMA-Sicherheitsabstand und das noch nicht abgeschlossene jüngste Wort bleiben
   berücksichtigt. Native und Greaseweazle-Kodierung behalten ihre Drahtformate.
5. **SRAM-Verteilung.** Zwei Decoder liegen nun in SRAM1 beziehungsweise SRAM4;
   auch das beobachtete Rohprofil liegt statisch in SRAM4 statt auf dem Stack.
   Die NOLOAD-Zustände werden vor Verwendung initialisiert. Neue Zustände und
   Metadaten sind in den unten angegebenen Nettozahlen bereits enthalten.
6. **Evidenzgesteuerte Wiederholungen.** Policy3 vergleicht nun CHRN und beide
   Fingerprints. Neue ausdrücklich gewählte Policy4 `INVESTIGATE_VARIANTS`
   berücksichtigt außerdem nominale Varianten mit fehlerhafter Datenprüfsumme,
   den Übergang zu gültigen Daten und neue grobe Rohvergleichsmuster. Mindestens
   zwei Umdrehungen sind erforderlich. Registrierungsüberlauf oder fehlender
   Rohvergleich verhindern einen falschen Stagnationsschluss; Versuchslimit und
   Budget gelten weiter. Dies ist Leseplanung, keine automatische Rekonstruktion.
7. **Laufzeitmesswerte.** Native DIAGNOSTICS `0x0D` mit Nutzlast `01` liefert einen
   versionierten64-Byte-Bericht: Hauptschleifen-/Analyseschritte, maximale
   Millisekundenzeiten, Prüfschritte, Streaming-/Cachemengen, Formatkandidaten,
   Hinweisnutzung sowie Fingerprint-Aufwand. Zähler sättigen. Der leere bisherige
   Diagnoseaufruf behält sein Layout. Die Zeitbasis beträgt1000Hz, keine
   hochauflösende CPU-Zyklusmessung.
8. **Schnellere Softwareprüfung.** `check_all.py --jobs 1..8` führt22 unabhängige
   Gruppen mit getrennten Build-Verzeichnissen aus; Standard sind bis zu vier
   Prozesse. Korpus-, Host- und IEC-Abschlussprüfungen folgen anschließend.
   CMake nutzt vorhandenes ccache, ohne einen expliziten Compiler-Launcher zu
   überschreiben. CI installiert und speichert den Compiler-Cache. Ein lokaler
   ARM-Probelauf bestätigte einen Cache-Treffer samt wiederhergestellter
   `.su`-Stackdatei; die geänderte CI selbst wurde nicht auf GitHub ausgeführt.

Die neuen nativen Optionen und Berichtsfelder stehen in
[USB_Protokoll.md](USB_Protokoll.md), einschließlich Featuremaske `0x000F`,
Policymaske31, Validierungsgrenzen und Versuchsbewertung. Greaseweazle-Befehle und
ihre Flagbedeutungen bleiben unverändert.

## Gemessene Ressourcen

ARM-Release-Build mit den echten STM32CubeH7-/USB-/FatFs-Headern:

| Ressource | Vor diesem Nachtrag | Aktueller Stand |
|---|---:|---:|
| FLASH belegt | 221976B | 225076B |
| AXI belegt | 294744B | 290864B |
| AXI frei | 32936B | 36816B |
| DTCM statisch | 109984B | 109984B |
| Verbleibender DTCM-Stackraum | 21088B | 21088B |
| SRAM1 belegt | 13808B | 16040B |
| SRAM2 belegt | 8192B | 8192B |
| SRAM4 belegt | 12396B | 15668B |
| Größter gemessener Projekt-Stackrahmen | 1080B | 848B |

Netto3880B mehr freier AXI-RAM. Heap bleibt8192B. Die Stackprüfung erfasst723
Projektfunktionen und erzwingt2048B pro Rahmen. Sie belegt keine maximale
kumulative Aufrufketten-/Interrupttiefe.

BIN SHA256:
`669e8acf0e25db32b61d16dab434286a25de477fd4bde8ccb4ac76582a435327`.

## Verifikation

- ARM-Build und Linker-Speicher-/Stackgrenzen bestanden.
- Statische ARM-Analyse aller59 Projektmodule mit echten SDK-Headern bestanden.
- Serieller vollständiger Offline-Gate:31/31 Gruppen,23 native Suites,
  fünf externe Korpora,197 Hosttests und23/23 IEC-Prüfungen; nichts übersprungen.
- Derselbe vollständige Gate mit vier Prozessen ebenfalls31/31 bestanden.
  Die22 unabhängigen Gruppen benötigten seriell141,70s und parallel46,59s:
  Faktor3,04 auf diesem Rechner. Die nachfolgenden Korpus-/Host-/IEC-Gruppen
  sind in diesen beiden Zeiten nicht enthalten. Messungen am selben Quellstand,
  nacheinander; normale Systemlast und Cacheeffekte sind nicht isoliert.
- Linux GCC ASan/UBSan:23 native Suites sowie die aktuellen Laufzeitpfade für
  Quality, Analysejobs und USB-Protokoll bestanden. Ein zusätzlicher Lauf deckte
  SD-, Präzisions-, Streaming- und Motorpfade ab.
- Sechs ASan-Fuzz-Ziele jeweils60 Sekunden: insgesamt10318897 Ausführungen,
  alle mit neuen Mutationen nach Initialisierung und ohne erkannten Fehler.
- Gezielt geprüft: Fingerprints3078/3078 Referenzvergleiche, Metriken14/14;
  kooperative und synchrone Prüfberichte gleich, veränderte Sektoren abgelehnt;
  Format-Hinweisvalidierung und Rückfall; Varianten-/Rohmuster-Stagnation;
  begrenzte Cache-Arbeit und Ringumlauf; Motorstart/Abbruch aller Portfamilien.

Beim ersten Fuzz-Lauf lieferte ein generierter Ersatz für die neue Diagnosefunktion
eine beliebige Antwortlänge. Der echte Provider liefert ausschließlich64B; der
Befund ist daher kein Nachweis einer durch den Host erreichbaren Überlänge in
der Firmware. Der Fuzzer verwendet jetzt den echten Provider und einen neuen
Diagnose-Seed. Zusätzlich lehnt der zentrale Antwortpfad übergroße Längen und
fehlende Nutzdaten vor Kopie/CRC ab. Grenzwerttests mit Wächterbytes bestanden.
Der auslösende Input bleibt lokal unter
`firmware/tests/build/v2-crash-7389c68817e243d5357b64369f569c825b3f20f7` erhalten.

Messprotokolle und JSON-Berichte liegen lokal unter `firmware/tests/build/`:
`process_gate_serial.log`, `process_serial_gate.json`, `process_gate_parallel.log`,
`process_parallel_gate.json`, `process_asan_final.log`, `process_static.log` und
`process_fuzz.log`.

## Bewusst verbleibende Grenzen

Der erste Motoranlauf, Seek, einige Diagnose-/Lösch-/Musterroutinen und die
Vorverarbeitung der Schreibreferenz sind weiterhin synchron. Dieser Nachtrag
macht die Firmware nicht vollständig unterbrechungsfrei. Der einzelne
Decoder-Aufruf bleibt ebenfalls eine zusammenhängende Arbeitseinheit.

Ohne Platine lassen sich U11-Durchsatz, reale DMA-/Cache-Kohärenz, elektrische
Signale, Interruptlatenz, Laufwerksmechanik und erzielbare USB-/Flux-Datenraten
nicht qualifizieren. Native Tests und Fuzzer verwenden Hardware-Doubles.
Gemessene PC-Testzeiten sind keine Aussage zur Geschwindigkeit auf dem MCU.
Der Format-Hinweis ist eine optionale Suchabkürzung; Fingerprints und grobe
Rohmuster sind weder kryptographische Gleichheit noch universelle
Kopierschutzerkennung. Reale geschützte Medien und vollständige Stackketten
bleiben gesondert zu prüfen.

## Nachprüfung der Flag-Kombinationen

Bei der anschließenden Prüfung wurde eine semantische Wechselwirkung gefunden:
`FAST_FORMAT_HINT` setzt bei Verwendung vorsorglich `ambiguous=1`, während
`CONFIRM_CONTENT` (Policy1) Mehrdeutigkeit als Hindernis für eine Bestätigung
wertet. Eine gecachte Formaterkennung konnte dadurch die Bestätigungsfolge
unterbrechen. Policy1 fordert jetzt intern die vollständige Formatsuche an.
Die angeforderte Featuremaske bleibt sichtbar; nur das Bit für tatsächliche
Hinweisnutzung bleibt aus. Kein neues USB-Flag und keine Drahtformatänderung.
Tatsächliche Mehrdeutigkeit der vollständigen Suche bleibt ausdrücklich erhalten.
Das interne Merkmal nutzt vorhandenes Strukturpadding; RAM-Belegung unverändert.

Der Regressionstest wärmt den Cache nachweislich auf und vergleicht den
Bestätigungsjob mit und ohne angeforderten Hinweis. Ergebnis, Bestätigungszahl
und Stopgrund stimmen überein; der Bestätigungsjob verwendet keinen Cache-Kurzlauf.
Der bestehende Test für erfolgreiche Bestätigung mit explizitem Format bleibt
bestanden. Zusätzlich prüft die Quality-Suite alle4096 Kombinationen der256
Basisflags mit den16 bekannten Featuremasken, gültige übernommene Masken und
unbekannte Featurebits: insgesamt6328/6328 Quality-Assertions bestanden.

Weitere geprüfte Abhängigkeiten und beabsichtigte Ausschlüsse stehen gesammelt
in [USB-Protokoll3.6](USB_Protokoll.md#36-flag-abhängigkeiten-und-wechselwirkungen).
Insbesondere sind Weak-Schreiben, Inhaltsprüfung und Rohprüfung keine beliebig
veroderbare Gesamtmaske; Op31 und Op50 haben außerdem unterschiedliche Einheiten
und zusätzliche Voraussetzungen.

Nach dem Fix erneut bestanden: ARM-Build, vollständiger Offline-Gate31/31 Gruppen
ohne ausgelassene Korpora, statische ARM-Analyse59/59 sowie die betroffenen
Analysejob-/Quality-Laufzeittests unter Linux GCC ASan/UBSan. Die sechs Fuzz-Läufe
weiter oben gehören zum Stand vor diesem kleinen Nachtrag und wurden dafür
nicht wiederholt. Logs: `flag_audit_build.log`, `flag_audit_gate.log`,
`flag_audit_static.log`, `flag_audit_asan.log` unter `firmware/tests/build/`.

Aktuelle FLASH-Belegung225108B; RAM und maximaler Stackrahmen unverändert.
BIN SHA256 nach dem Flag-Fix:
`7b35c00e1b24a139c59f66b131ec56bce369f4902e2da5e071093bb75a08e161`.
