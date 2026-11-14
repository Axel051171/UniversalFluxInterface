# UnifiedFloppyTool mit UFI

**Sprache / Language:** [Deutsch](UnifiedFloppyTool) | [English](UnifiedFloppyTool-EN)

**UnifiedFloppyTool (UFT) ist die grafische PC-Anwendung zum UFI-Board.** Der untersuchte Entwicklungsstand enthält einen eigenen Controller „UFI Flux Engine - Beta“, eine native USB-Verbindung, Flux-Aufnahme und -Schreiben sowie zusätzliche UFI-Werkzeuge. Die Python-Kommandozeile `ufi` bleibt ein eigener Zugang für Diagnose und Skripte.

Grundlage dieser Seite: der am 14. November 2026 gelesene lokale Entwicklungsstand, Git-Revision `7e7dd6701c3a`. Der Ordnername `UnifiedFloppyTool-4.1.0` ist keine verlässliche Versionsangabe: `VERSION.txt` nennt **4.1.6**. Ein veröffentlichtes Paket mit derselben Versionsnummer muss nicht alle hier beschriebenen Entwicklungsänderungen enthalten. Öffentliche Projektseite: [UnifiedFloppyTool](https://github.com/Axel051171/UnifiedFloppyTool).

## Welchen Zugang verwenden

| Zugang | Verwendung |
|---|---|
| UFT: „UFI Flux Engine - Beta“ | Grafischer Zugriff auf das native UFI-v2-Protokoll über USB-CDC |
| Python-Host `ufi` | Befehle, Skripte, natives `.ufi`-Archiv und der implementierte LAN-Zugang |
| UFT: Greaseweazle-Controller | UFI im Greaseweazle-kompatiblen Modus mit dessen Funktionsumfang |
| UFT: USB-Floppy-Controller | Separater Zugang für gewöhnliche USB-Diskettenlaufwerke; für das native UFI-Board „UFI Flux Engine“ auswählen |

Die untersuchte native UFT-Verbindung verwendet `QSerialPort`; daraus ergibt sich keine UFT-Unterstützung des UFI-LAN-Transports oder der nativen Composite-Bulk-Schnittstelle. Für diese Wege die jeweiligen [LAN](LAN)- und [Installationshinweise](Installation) beachten.

## Grafische Verbindung einrichten

1. Einen UFT-Build mit **Qt SerialPort** und der nativen UFI-Anbindung verwenden. Ein Build ohne dieses Modul meldet ausdrücklich, dass keine Verbindung möglich ist.
2. Das Board in den nativen Flux-/UFI-Modus schalten, mit `protocol=ufi` in `UFI.CFG`. Der CDC-Port hat VID/PID **`1209:4F54`**.
3. Andere Programme schließen, die denselben COM-Port geöffnet halten. UFT öffnet den Port exklusiv.
4. In UFT den Reiter **Hardware**, dann **„UFI Flux Engine - Beta“**, den tatsächlichen Port und das passende Laufwerk wählen.
5. Für die Abnahme vor dem Verbinden **„Mitschnitt (.ufec) für die Bank-Abnahme“** aktivieren und einen beschreibbaren Zielordner auswählen.
6. **Verbinden** und die gemeldete Firmware sowie die Boardauskunft prüfen. Anschließend **„UFI Flux Engine – Werkzeuge (Beta)“** aufklappen. Ohne Boardsitzung bleiben die Werkzeuge gesperrt.

Im GW-Modus hat das Board PID `4F57`. Der native Transport weist diesen Port mit einem Hinweis zurück. Entweder den Greaseweazle-Controller verwenden oder zum nativen UFI-Modus zurückkehren. Der USB-Bring-up des Boards allein bestätigt noch keinen erfolgreichen UFT-Aufnahmelauf.

## Vorhandene UFI-Werkzeuge

| Bereich | Funktionen im untersuchten Code |
|---|---|
| Diagnose | Selbsttest, Laufwerke suchen, Spuranalyse, Seek-Test und Laufwerkszeiten |
| Analyse | Erneute Lesung einer Spur, Schnellscan und gezieltes Nachlesen markierter Spuren als Flux |
| Apple II | Prologe/Epiloge, Dekodieroptionen, Viertelspurauswahl und Nibble-Statistik |
| IEC (Commodore) | Status, Busreset und Abbilder für 1541 D64 mit 35/40 Spuren, 1571 D71 und 1581 D81 |
| Board | Firmware-/Fähigkeitsauskunft, Versorgungszustand, SD-Dateiliste und USB-Moduswechsel |

Eine Analyse mit erneuter Lesung ist eine eigene Aufnahme. Ihr Ergebnis darf nicht automatisch der vorherigen Fluxdatei zugeordnet werden. Laufwerkszeiten und Apple-Dekodieroptionen ändern Einstellungen; ein Diagnosebereich ist deshalb nicht insgesamt zustandsneutral.

Beim USB-Moduswechsel verliert UFT die native Verbindung. Der angebotene Bootloaderwechsel belegt keine funktionierende DFU-Enumeration: dieser Punkt ist laut [Projektstatus](Projektstatus) noch offen.

## Aufnahme, Dateien und Schreibprüfung

Für erste Aufnahmen ein bekanntes Testmedium, korrekte Geometrie und wenige Umdrehungen wählen. Der native UFT-Provider begrenzt Aufträge auf **höchstens 20 Umdrehungen** wegen seiner 32-Bit-Indexzeiten in Nanosekunden. Diese Grenze gehört zu diesem Hostpfad, nicht pauschal zur Firmware. Der Doppelschritt wird für Flux-Leseaufträge nur verwendet, wenn die Firmware das entsprechende Befehlsmerkmal meldet; daraus folgt keine gleiche Doppelschritt-Funktion beim Schreiben.

| Datei oder Darstellung | Bedeutung |
|---|---|
| UFT `.ufec` | Mitschnitt des Verbindungsverkehrs für Fehlersuche und Abnahme; kein gewöhnliches Diskettenabbild |
| UFT Flux-Aufnahme | Der Provider wandelt native Ticks über absolute Zeitpunkte in ganzzahlige Nanosekunden um |
| SCP-Ausgabe | Austauschformat mit 25-ns-Raster; zugehörige Metadaten und Aufnahmebefunde mit aufbewahren |
| Python-Host `.ufi` | Separates natives SQLite-Archiv mit Ticks und validierten Rahmen; `.ufec` ist kein Ersatz dafür |

UFT fragt die Boardfähigkeiten ab und verlangt für noch nicht am Gerät qualifizierte Lese-/Schreibfunktionen eine Sitzungsfreigabe. Das ist keine technische Abnahme. Für Schreiben zusätzlich [Schreiben und Prüflesen](Schreiben-und-Prueflesen) beachten: Hardwarefreigabe, Schreibschutz, zulässige Intervalle und Prüfklasse müssen zusammenpassen. Im nativen Provider bedeutet `verified` erst **Prüfstufe 3**, nicht bloß beendete Ausgabe oder plausible Rücklesedaten. Die erweiterte Firmware-Prüfung muss von Board und Firmware unterstützt werden.

## Sprache und Nachweisstand

Das Wiki ist auf Deutsch und Englisch wählbar. Die untersuchte UFT-Anwendung lädt dagegen beim Start ausdrücklich die deutsche Übersetzung; eine entsprechende Sprachumschaltung ist dort derzeit nicht eingerichtet. Auf der englischen Wikiseite bleiben deshalb die tatsächlich sichtbaren deutschen Bedienelemente zur Orientierung genannt.

Die native Anbindung und ihre Tests sind im Quellbaum vorhanden. Protokoll-Doubles, Transportmitschnitte und die FakeV2-Brücke ersetzen keine Messung mit echtem Laufwerk. Die UFT-Bankanleitung bezieht sich teilweise noch auf Board v0.7: insbesondere dort genannte alte Anschlussnummern nicht auf v1.0 übertragen. Für v1.0 gilt die [Anschlussübersicht](Anschluesse), mit IEC an **J8**.

Quellen im untersuchten UFT-Baum: `src/hardwaretab.cpp`, `src/gui/ufi_werkzeuge.cpp`, `forms/ufi_werkzeuge.ui`, `src/hardware_providers/qserial_ufi_flux_engine_transport.cpp`, `src/hardware_providers/ufi_flux_engine_provider_v2.h`, `src/fluxcapturejob.cpp`, `src/main.cpp` und `docs/UFI_FLUX_ENGINE_BANK.md`. Der lokale Quellenbezug ist unter `docs/wiki-tools/companion-sources.json` festgehalten.
