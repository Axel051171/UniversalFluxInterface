# Vorteile und Vergleich

**Sprache / Language:** [Deutsch](Vorteile-und-Vergleich) | [English](Benefits-and-Comparison)

**UFI bündelt Flux-Archivierung, mehrere Laufwerksfamilien, Standalone-Betrieb und Diagnose auf einer Platine.** Der praktische Vorteil liegt besonders an einem Arbeitsplatz, an dem PC-, Amiga-, Apple-, Macintosh- und Commodore-Medien bearbeitet werden sollen: Anschlüsse, Versorgungsüberwachung, Speicher und Bedienung sind Teil desselben Aufbaus.

Vergleichsstand: **14. November 2026**, UFI Headless v1.0 mit Firmware 1.18-dev. Verglichen werden dokumentierte Ausstattung und Funktionen. UFI hat den beschriebenen USB-Bring-up mit PSRAM bestanden; Laufwerksbetrieb und LAN mit Kabel sind nach dem [Projektstatus](Projektstatus) noch zu qualifizieren. Ein direkter Laborvergleich der Geräte liegt nicht vor.

## Welche Vorteile UFI bietet

| UFI-Ausstattung oder Funktion | Nutzen im Arbeitsablauf | Nachweis und Bedingung |
|---|---|---|
| Eigene Anschlüsse für PC/Shugart, Amiga, Disk II, Mac und IEC | Unterschiedliche Rechnerfamilien lassen sich mit derselben Grundplatine bearbeiten; weniger zusätzliche aktive Interfaceplatinen können nötig sein | Anschlüsse vorhanden, Treiber implementiert; Kabel und Versorgung müssen zum Laufwerk passen |
| Standalone-Dump auf internen SD-NAND, Taster und Statusanzeigen | Aufnahmen können ohne laufendes PC-Programm durchgeführt und später abgeholt werden | Firmware vorhanden; Ablauf mit echten Laufwerken noch offen |
| W5500-LAN neben USB | Ein eingebautes oder räumlich entferntes Gerät lässt sich über das lokale Netz bedienen | Hardware und TCP-/Discovery-Firmware vorhanden; LAN-Gerätetest offen |
| eFuses, Strommessung und Spannungsrückmeldung für Laufwerke | Fehler bei Versorgung und Last lassen sich gezielter erkennen; Versorgungsschienen sind steuerbar | Baugruppen vorhanden; Abschaltschwellen und Verhalten unter Laufwerkslast noch messen |
| Frontschalter plus Hardware-Gatter und auftragsbezogene Firmware-Freigabe | Sichtbare Schreibfreigabe am Gehäuse und zusätzliche Sperre außerhalb des Schreibauftrags | Auf v1.0 vorhanden; physische Schreibpfad-Abnahme steht aus |
| 8 MB PSRAM und Streaming-Ring | Pufferreserve für zeitweise verzögerte Abnahme und längere Aufnahmen | PSRAM im USB-Bring-up genutzt; Dauerlast mit realen Fluxdaten noch prüfen |
| Native 275-MHz-Zeitbasis und Archiv in Original-Ticks | Feine Quantisierung für Timinganalysen; native Aufnahme kann vor Exportumrechnung erhalten bleiben | Implementierte Timer-/Archivdarstellung; reale Messgenauigkeit separat bestimmen |
| UFI, Greaseweazle-kompatibler Modus, Composite und USB-Massenspeicher | Unterschiedliche Arbeitsweisen können dieselbe Hardware nutzen | Protokolle und Modi implementiert; Funktionsumfang hängt von Modus und Laufwerk ab |
| Offene KiCad-, Firmware- und Host-Quellen | Verhalten ist prüfbar und für eigene Anforderungen anpassbar | Quellen im Repository, MIT-Lizenz; auch andere offene Projekte bieten diesen Vorteil |

Beispiele und Details: [Anschlüsse](Anschluesse), [SD und Standalone](SD-und-Standalone), [LAN](LAN), [Schreibfreigabe](Schreiben-und-Prueflesen), [Dateiformate](Dateiformate). UFI-Referenzen: [Board-Pins](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/board.h), [Versorgungssteuerung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_board.c), [Firmware-Zeitbasis](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_firmware.h), [Lizenz](https://github.com/Axel051171/UniversalFluxInterface/blob/main/LICENSE).

## Gegenüber KryoFlux

KryoFlux bietet Fluxaufnahme, STREAM-Rohdaten, viele dekodierte Exportformate und Rückschreiben einschließlich ADF, G64 und IPF. Die Herstellerdokumentation beschreibt außerdem parallele Exporte und eine grafische Oberfläche. [KryoFlux-Funktionen](https://www.kryoflux.com/?page=kf_features).

Der dokumentierte Controller verbindet Laufwerke über den 34-poligen Shugart-Anschluss mit dem PC. Zwei Laufwerke können am Kabel hängen; der Zugriff erfolgt nacheinander. USB ist High Speed, die Hostsoftware wird für Windows, macOS und Linux angeboten. [Technische Übersicht](https://www.kryoflux.com/?page=kf_tech).

**UFIs zusätzlicher Nutzen:** separate native Anschlüsse für weitere Laufwerksfamilien, integrierter IEC-Zugang, Aufnahme auf internen Speicher ohne PC-Programm, LAN und ausführliche Boarddiagnose. Diese Kombination kann einen gemischten Archivierungsplatz vereinfachen. Sie belegt noch keine höhere Auslesequote oder bessere Kopierschutzreproduktion als KryoFlux. Eine direkte UFI-Unterstützung aller KryoFlux-/IPF-Arbeitsabläufe wird damit nicht zugesichert.

## Gegenüber Greaseweazle V4.1

Greaseweazle ist ebenfalls offen und besitzt bereits einen physischen Schreibfreigabe-Jumper. Die Modellübersicht nennt Mehrlaufwerksbetrieb, gepufferte Ausgänge und Disk-Change-Erkennung. High-Speed-USB und direkte 12-V-Versorgung sind bei bestimmten F7-Modellen vorhanden; sie dürfen nicht pauschal für alle Greaseweazle-Modelle ausgeschlossen werden. [Modellvergleich](https://github.com/keirf/greaseweazle/wiki/Greaseweazle-Models).

V4.1 verwendet USB-C und einen 34-poligen Laufwerksanschluss. Es unterstützt auch passend ausgewählte Amiga-/Atari-Laufwerke über diesen Bus. Laut Projektdokumentation kann die Eignung von PC-Laufwerken für GCR-Medien unterschiedlich ausfallen. [V4.1-Anschlussanleitung](https://github.com/keirf/greaseweazle/wiki/V4.1-Setup).

**UFIs zusätzlicher Nutzen:** eigene Disk-II-, Mac-, Amiga- und IEC-Anschlüsse, interner Aufnahmespeicher, Tasterbetrieb, LAN und überwachte Laufwerksversorgung. Der Nutzen liegt in der breiteren Integration des Aufbaus. Offenheit, USB-C, Mehrlaufwerksanschluss und physischer Schreibschutz sind keine UFI-Alleinstellungsmerkmale.

UFI enthält zusätzlich einen Greaseweazle-kompatiblen Modus. Damit soll vorhandene Hostsoftware weiter nutzbar sein; die jeweilige Kombination aus Werkzeugversion, Laufwerk und Funktion muss geprüft werden. Siehe [Greaseweazle](Greaseweazle).

## Gegenüber SuperCard Pro

SuperCard Pro bietet Fluxlesen/-schreiben, das verbreitete SCP-Format und eine veröffentlichte Hardware-SDK-/Formatspezifikation. Der Hersteller nennt 512 KB SRAM, einen 34-poligen Anschluss, USB Full Speed, serielle Schnittstellen und einen microSD-Steckplatz. Auch ein Betrieb über serielle Steuerung statt USB wird beschrieben. Eine portable Kopierstation wird auf der Produktseite als Erweiterung behandelt. [SuperCard-Pro-Produktbeschreibung](https://www.cbmstuff.com/index.php?route=product/product&product_id=52).

**UFIs zusätzlicher Nutzen:** ein größerer nomineller Fluxspeicher, direkt integrierte Taster-/SD-Dump-Firmware, LAN, weitere Anschlussfamilien und gemeinsame Board-/Versorgungsdiagnose. Gerade im Gehäuseaufbau sind diese Funktionen zusammen vorgesehen. Ein Kartenslot oder eine serielle Steuerungsmöglichkeit allein ist dagegen kein Nachweis eines mit UFI identischen autonomen Arbeitsablaufs.

SCP bleibt auch für UFI ein Austauschformat. Für die volle native Zeitauflösung das UFI-Archiv behalten; der normale UFI-SCP-Export rechnet auf 25-ns-Einheiten um.

## Gegenüber Applesauce und Applesauce+

Beim aktuellen Vergleich muss **Applesauce+** berücksichtigt werden. Es besitzt bereits mehrere PC-/Apple-Anschlüsse, zwei Sync-Sensor-Eingänge, gepufferte Aufnahme und High-Speed-USB. Der Hersteller nennt bis zu acht angeschlossene Laufwerke, eine native Abtastung mit 576 MHz und auswählbare Fluxausgabe bis 96 MHz. Die ältere Applesauce-Hardware wird mit Client 2.0 bis 24 MHz Ausgabe beschrieben. [Applesauce und Applesauce+](https://applesaucefdc.com/).

**UFIs eigener Schwerpunkt:** offener gemeinsamer Firmware-/Hardwarestand, IEC-Anbindung, interne Standalone-Aufnahmen und ein LAN-Zugang für denselben Geräteaufbau. Mehrere Ports oder Sync-Sensoren sind gegenüber Applesauce+ keine Alleinstellung. Aus dem UFI-Timertakt lässt sich auch keine pauschal höhere Messleistung als Applesauce+ ableiten.

## Einordnung von FluxEngine

FluxEngine bezeichnet sowohl eine eigene Hardware als auch eine plattformübergreifende Software zur Verarbeitung von Flux- und Sektorabbildern. Der Client unterstützt zusätzlich Greaseweazle und teilweise Applesauce. [FluxEngine-Projekt](https://github.com/davidgiven/fluxengine/blob/master/README.md).

**UFIs Vorteil gegenüber einem einfachen USB-Fluxaufbau** liegt in integrierten Schnittstellen, Standalone-Speicher, LAN und Gerätediagnose. Der Vergleich der Decoder- und Dateisystemfunktionen muss getrennt von der Hardware erfolgen. UFI wird durch seinen GW-Modus nicht automatisch zu einer in jeder Version getesteten FluxEngine-Plattform; fremde VID/PID und unterstützte Firmwarestände können eine Rolle spielen. [FluxEngine mit Greaseweazle-kompatibler Hardware](https://github.com/davidgiven/fluxengine/blob/master/doc/greaseweazle.md).

## Zeitauflösung richtig vergleichen

Die Zeiten in dieser Tabelle sind aus den dokumentierten Takten berechnete **nominelle Zeitschritte**, keine gemessenen Genauigkeiten oder Erfolgsquoten.

| Gerät bzw. Datenpfad | Dokumentierter Takt / Raster | Nomineller Zeitschritt | Einordnung |
|---|---|---|---|
| UFI nativer Capture-/Archivpfad | 275 MHz | etwa 3,64 ns | Timerzeitbasis im aktuellen UFI-Code |
| KryoFlux STREAM-Standardwerte | etwa 24,027 MHz Sample Clock | etwa 41,62 ns | Standardwert der Protokolldokumentation; tatsächlich gemeldeten Gerätetakt verwenden |
| Greaseweazle-Firmware | 72 MHz | etwa 13,89 ns | `SAMPLE_MHZ` im untersuchten Produktionscode |
| SuperCard Pro | 25-ns-Capture | 25 ns | Herstellerangabe zur Aufnahme-/Formatspezifikation |
| Applesauce+ Fluxausgabe | bis 96 MHz | etwa 10,42 ns | Ausgabeauflösung; native Abtastung laut Hersteller 576 MHz, etwa 1,74 ns |
| UFI im GW-Protokoll | 68,75 MHz | etwa 14,55 ns | Protokollseitige Umrechnung aus dem 275-MHz-Timer |
| Normaler UFI-SCP-Export | 25-ns-Raster | 25 ns | Dateiumrechnung; ursprüngliche Tickauflösung bleibt nur im nativen Pfad erhalten |

Quellen: [UFI-Konstanten](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_firmware.h), [KryoFlux STREAM Rev. 1.1, Seite 4](https://www.kryoflux.com/download/kryoflux_stream_protocol_rev1.1.pdf), [Greaseweazle `floppy.c`](https://github.com/keirf/greaseweazle-firmware/blob/master/src/floppy.c), [SCP-Spezifikation](https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt), [Applesauce+-Ankündigung](https://applesaucefdc.com/2025/06/10/introducing-the-applesauce/), [UFI-SCP-Writer](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/scp.py).

Das feinere UFI-Timerraster kann für Timinganalysen nützlich sein. Welche Unterschiede sich praktisch auswerten lassen, hängt zusätzlich von Eingangsschaltung, Filterung, Taktstabilität, Laufwerk und Medium ab. „Feinere Quantisierung“ darf deshalb nicht durch „entsprechend genauer“, „mehr gerettete Daten“ oder „schnellere Aufnahme“ ersetzt werden.

## Für welche Aufgaben UFI besonders interessant ist

Mit [UnifiedFloppyTool](UnifiedFloppyTool) ist außerdem eine eigene grafische PC-Anbindung im Entwicklungsstand vorhanden: native UFI-Verbindung, Diagnose, Aufnahmen, Schreibprüfung und IEC-Werkzeuge ergänzen die Platine. Das ist ein Vorteil des gemeinsamen Arbeitsablaufs. Die UFT-Geräteabnahme steht noch aus; eine grafische Oberfläche allein ist gegenüber den anderen Systemen keine Alleinstellung.

- **Gemischte Sammlung:** mehrere Laufwerksfamilien mit derselben Grundplatine und dokumentierter Anschlusszuordnung bearbeiten.
- **Gehäuse und Kopierstation:** Frontbedienung, lokale Speicherung und spätere Abholung der Abbilder kombinieren.
- **Versuchs- und Diagnoseplatz:** Strom, Spannung, Pufferzustand, Medienwechsel und Aufnahmebefunde gemeinsam auswerten.
- **Eigene Weiterentwicklung:** Schaltplan, Firmware und Host prüfen und auf besondere Laufwerke oder Abläufe anpassen.

Vor einer Entscheidung für den produktiven Archivbetrieb die tatsächlich benötigten Geräte-/Formatkombinationen abnehmen. Aussagen über Preisvorteil, höhere Geschwindigkeit oder bessere Wiederherstellung benötigen einen gesonderten Vergleich und werden hier nicht aus der Ausstattung abgeleitet.
