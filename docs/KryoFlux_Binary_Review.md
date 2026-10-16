# Untersuchung der KryoFlux-Binärdatei

Stand: 14.10.2026. Statische Untersuchung der vom Nutzer genannten Datei,
Abgleich mit öffentlicher Dokumentation und dem vorhandenen UFI-Code.
Keine Ausführung, kein Flashen, keine Übernahme fremden Maschinencodes.

## Identifikation und belastbare Befunde

- Datei: `C:\Users\Axel\Downloads\firmware_kf_usb_rosalie_x64.bin`
- Größe: 25288 Bytes.
- SHA-256: `7033398dabd6c2a1a8d67443844a5545e8a60d82c44a1fdb972c657c22fa3b88`.
- Rohes Firmwareabbild, kein PE-/ELF-Container. Der Einstieg lässt sich als
  klassischer ARM-Code disassemblieren: `ldr`, `mov sp`, `msr CPSR_c`, ARM-Branch.
  Damit kein x86-64-Programm trotz Dateinamensuffix. Der Grund für das Suffix
  ist aus dieser Einzeldatei nicht belegt.
- Klassische ARM-Exceptionbehandlung und Registeradressen wie `0xFFFFF000`,
  `0xFFFFF400`, `0xFFFA0000` passen zur AT91/SAM7-Familie. Das genaue Chipmodell
  ist durch diese Untersuchung nicht eindeutig bestimmt. Kein direkt auf dem
  STM32H723/Cortex-M7 ausführbares Firmwareabbild.
- Die folgenden Versions- und Datumsangaben sind eingebettete Strings, keine
  kryptografisch bestätigte Herkunft oder Aussage über das Dateierstellungsdatum.

Alle Offsets beziehen sich auf den Anfang der Datei, nicht auf Laufzeitadressen.

| Dateioffset | Befund |
|---|---|
| `0x5CF8` | Formatstring mit Produktname `KryoFlux DiskSystem` und Version/Datum/Zeit |
| `0x5D34` | Versionsstring `3.00` |
| `0x5D3C` | `Mar 27 2018` |
| `0x5D48` | `18:25:55` |
| `0x5D60` | Hardwareinfofelder `hwid`, `hwrv`, `hs` |
| `0x5D7C` | Weitere Felder `wb`, `wa`, `wq`; Semantik hier nicht rekonstruiert |
| `0x5E20`–`0x5F2C` | Spurgrenzen sowie getrennte Auswahl-, Motor-, Step-, Kopfberuhigungs-, Write-Gate- und Bypass-Timingnamen |
| `0x6098` | Takthinweise `sck=24027428.5714285`, `ick=3003428.5714285625` |
| `0x61B4` | USB-Gerätedeskriptor: VID `03EB`, PID `6124`, bcdUSB `0200`, EP0-Paketgröße 8 |
| `0x622C` | USB-Konfiguration mit zwei Interfaces, Bulk-Endpoints OUT `01`, IN `82`, jeweils 64-Byte-Maximalpaket |

Der Beginn der USB-Konfiguration wurde anhand `09 02 3C 00 02 01 00 C0 FA` verifiziert.
`hs` darf nicht als Nachweis für USB High Speed gelesen werden: Das
[KryoFlux-Handbuch, Abschnitt Hard-sectored disks](https://www.kryoflux.com/download/kryoflux_manual.pdf)
beschreibt `hs=1` als Kennzeichnung für Hard-Sector-fähige Streams ab Firmware 3.00.
Es beschreibt außerdem automatische Hard-Sector-Erkennung. Die interne
Implementierung dieser Erkennung wurde hier nicht vollständig rekonstruiert.

Die eingebetteten Takthinweise passen zur veröffentlichten
[Stream-Dokumentation](https://www.kryoflux.com/download/kryoflux_stream_protocol_rev1.1.pdf).
Diese beschreibt zusätzliche Index- und Transportinformationen neben Fluxdaten.
Die auf der Herstellerseite bereitgestellte Dokumentation ist eine Untersuchung
von Jean Louis-Guérin; sie ersetzt keinen vollständigen Firmwarequelltext.

## Konkrete Verbesserungsmöglichkeiten für UFI

### 1. Alle Index-/Sektorlochimpulse als zusätzliche Messdaten erhalten

Istzustand: `ufi_flux_index_event()` in `firmware/src/ufi_flux.c` wendet zuerst
die Indexmaske an. Im Hard-Sector-Modus verwirft es danach die von `hs_is_index()`
als Sektorloch eingestuften Impulse. Der Klassifikator verwendet das Verhältnis
benachbarter Abstände; die konfigurierte Sektoranzahl wird dabei nicht zur
Validierung einer vollständigen Lochfolge verwendet.

Verbesserung: Rohereignisse vor der softwareseitigen Unterdrückung in einem
begrenzten separaten Puffer aufzeichnen. Pro Ereignis Zeitstempel, Fluxposition,
physische/simulierte Quelle und Klassifikationsgrund festhalten. Damit bleiben
die normale Umdrehungssteuerung und die Daten für eine spätere Neubewertung
getrennt verfügbar. Pufferverlust explizit zählen. Die Firmware kann aus mehreren
Umdrehungen Lochzahl, Periodizität und Unsicherheit vorbestimmen; das Tool bewertet
die Befunde weiter. Hardwareseitig weggefilterte Impulse sind nicht rekonstruierbar.

Vorgeschlagenes natives Flag: `RAW_INDEX_EVENTS`. Optional später
`CLASSIFY_HARD_SECTORS`; automatische Klassifikation ist kein Schreibfreigabesignal.
Umsetzungsstand siehe Nachtrag unten; RAW_INDEX hat inzwischen den Wert 0x0008.

### 2. Laufwerks-Timings vervollständigen und einheitlich anwenden

Binärbefund: Namen wie `t_density_select`, `t_step_after_motor`,
`t_track0_signal`, `t_write_gate_on` und `t_write_gate_off` sind getrennt vorhanden.
Das belegt benannte Parameter, nicht deren genaue Einheiten, Defaults oder
Wirkung in allen Kontrollpfaden.

UFI hat bereits elf Timing-/Optionsfelder, darunter Step, Richtungswechsel,
Seitenwechsel, Motorstart und Laufwerksauswahl. Zusätzlich existieren
Greaseweazle-Pre-/Post-Write-Werte. Eine vollständige Neuentwicklung ist unnötig.
Prüfenswert sind zusätzliche explizite Parameter für Density-Umschaltung,
Motor-bis-erster-Step und Write-Gate-Randzeiten. Im Write-Start steht außerdem
noch ein fester `HAL_Delay(20)` nach der bereits parametrisierten Seitenwahl.
Dies ist ein Ansatz zur Vereinheitlichung, kein Nachweis eines Hardwarefehlers.

Vorschlag: versioniertes `DRIVE_TIMING_V2` mit geprüften Grenzen und Rückmeldung
der tatsächlich wirksamen Werte. Zeitwerte sind Parameter, keine Bool-Flags.
Die vorhandene Greaseweazle-Parameterübersetzung muss erhalten bleiben.
Fremde Defaultwerte nicht ungeprüft auf unser Board übertragen.

### 3. Aufnahmebedingungen fest an jeden Bericht binden

UFI-Qualitätsberichte enthalten bereits Generation, Spur/Seite und Nominaltakt.
Board-/Pufferinformationen sind über separate Abfragen zugänglich; der
Hostarchivcode kann allgemeine Metadaten speichern. Es fehlt im untersuchten
Qualitätsbericht ein vollständiger, beim Start eingefrorener Firmwaredatensatz
der wirksamen Aufnahmebedingungen.

Vorschlag: kleiner `CAPTURE_CONTEXT`-Abschnitt mit Firmware-Build-ID,
Boardkennung, Laufwerkswahl, wirksamem Timingprofil, Density-/RPM-Einstellung,
Indexquelle und -maske, Hard-Sector-Konfiguration, Takt und U11/Fallback-Auswahl.
Ein Profilhash plus die lesbaren Parameter ermöglicht reproduzierbare Vergleiche.
Gemessene Taktkorrektur nur dann ausgeben, wenn sie tatsächlich gemessen wurde.
Dieser kleine Kontext sollte automatisch entstehen, ohne neues Analyseflag.

### 4. Transportprobleme zeitlich einer Aufnahme zuordnen

Die Stream-Dokumentation beschreibt Fortschrittsmeldungen mit Byteposition und
verstrichener Transferzeit sowie einen abschließenden Übertragungsstatus.
UFI besitzt bereits Rahmen-CRC, Integritätsjobs, Endstatus sowie globale Zähler
für Pufferbelegung, DMA-Fehler, Underruns und USB-Pausen. Diese Mechanismen bleiben.

Ergänzung: begrenzte, optionale Transporthistorie pro Aufnahme mit Zeit,
produzierten/übertragenen Daten, Pufferfüllstand und auftretenden Pausen. So kann
das Tool unterscheiden, ob eine problematische Aufnahme mit USB-Rückstau oder
anderen Transportfehlern zusammenfällt. Das beweist für sich keine Ursache eines
Magnetfehlers. Keine USB-Ausgabe oder umfangreiche Auswertung im Capture-ISR.

Vorgeschlagenes Flag: `TRANSFER_TRACE`, mit Limit und Verlustzähler.
Discovery/Report-Paging verwenden; keine neuen Greaseweazle-Streamtokens.

## Priorität und Grenzen

Empfohlene Reihenfolge: Rohindexereignisse, Aufnahme-Kontext, Timing-Vereinheitlichung,
danach optionale Transporthistorie. Offline prüfbar sind Zustandsmaschinen,
Lochfolgen mit Jitter/fehlenden Impulsen, Grenzwerte, Pufferüberlauf, Protokoll und
Berichtskonsistenz. Reale Signal- und Schreibzeiten benötigen später das Board.

Aus Strings und Deskriptoren lässt sich kein überlegenes PLL-Verfahren, keine
vollständige Schreibstrategie und keine vollständige Decoderliste nachweisen.
Das Abbild bietet daher konkrete Anregungen und Vergleichspunkte, aber keinen
direkt einsetzbaren STM32-Treiber. Vorhandene UFI-Funktionen werden nicht als neu
verkauft. Die ursprüngliche Untersuchung änderte Firmware und USB-Protokoll nicht.

## Umgesetzt am 2026-10-14

Die vier Anregungen wurden als eigene UFI-Implementierung umgesetzt, ohne Code
aus dem Binärabbild zu übernehmen. Native INFO-Capability Bit25 meldet:

- RAW_INDEX=0x0008: bis zu 240 Indexereignisse mit 64-Bit-Zeit, DMA-Cursor und
  Filtergrund; auch maskierte Impulse und Hard-Sector-Warmup werden protokolliert.
- TRANSFER_TRACE=0x0010: bis zu 48 Aufnahme-/USB-Proben mit Verlustzähler.
- Automatischer 160-Byte-Aufnahmekontext mit Startkonfiguration, Buildkennung,
  Nominaltakt, Speicherwahl und Ausgangszählern; kein zusätzliches Flag nötig.
- Fünf zusätzliche Zeitparameter über versioniertes Op 10. Vor-/Nachlauf am
  WGATE über stille Segmente im nativen erweiterten 34-Pin-Schreibpfad.

Details, Defaults und Grenzen: [USB-Protokoll, Abschnitt 4.11](USB_Protokoll.md#411-aufnahmebelege-und-zusätzliche-laufwerkszeiten-info-bit25).
Die beiden Flags sind formatunabhängig und nutzen freie Jobbits. Standardaufnahmen
konfigurieren sie über Op 7; Op 8 liest die Belege. Keine neuen Greaseweazle-Tokens.
Eine automatische Erkennung der Sektorlochzahl ist weiterhin nicht implementiert.

Offline bestanden: vollständiges Softwaregate, Grenz-/Verlusttests, Snapshot-
Konsistenz, Zeitüberlauf, Indexfilterpfad, USB-Rückstau und Produktions-Wellenform
mit Vor-/Nachlauf. Der ARM-Build belegt 169960 Byte FLASH und 4992 Byte SRAM1
für Diagnoseeinträge; AXI frei 37592 Byte. Reale Signale und Schreibqualität
bleiben bis zur Hardwareinbetriebnahme ungeprüft.
