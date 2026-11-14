# Eigenes Tool: USB-Protokoll

**Sprache / Language:** [Deutsch](USB-Protokoll) | [English](USB-Protocol)

Diese Seite ist der Einstieg für eigene Hostprogramme, GUIs und Skripte. Verwende das **native UFI-v2-Protokoll**. Die [vollständige Detailreferenz](USB-Protokoll-Referenz) enthält sämtliche Befehlstabellen und Spezialfunktionen. Stand: Firmware 1.18-dev, 14. November 2026. Implementierung und Messnachweis unterscheiden sich; siehe [Projektstatus](Projektstatus).

## Geeigneten Transport wählen

| Modus | VID:PID | Zugang für dein Programm |
|---|---|---|
| Native UFI | `1209:4F54` | USB-CDC, serieller Bytestrom; einfachster Einstieg |
| Composite | `1209:4F58` | Interface 2, Vendor-Klasse, Bulk OUT `0x03`, IN `0x83`; ausschließlich UFI v2 |
| Greaseweazle | `1209:4F57` | Eigenes Protokoll; keine nativen UFI-v2-Rahmen senden |
| SD / USB-Floppy | `1209:4F55` / `1209:4F56` | Mass Storage; kein normaler UFI-CDC-Port |

Die serielle Baudrate bestimmt nicht die USB-Übertragungsgeschwindigkeit. **10000 Baud setzt den CDC-Kanal zurück**; anschließend z. B. wieder 115200 einstellen. Ein Kanalreset kann die bestehende Arbeit unterbrechen: vorher andere Clients schließen. Composite verwendet eigene EP0-Anfragen zum Zurücksetzen und Abbrechen; keine globale USB-Neukonfiguration durchführen. Die Entwicklungs-PID `4F58` und die tatsächliche Betriebssystemunterstützung sind gesondert zu behandeln. Details: Referenz 1.1, 1.2 und 2.5.

Nach einem USB-Moduswechsel erscheint das Gerät neu. Die MCU-Seriennummer hilft beim Wiederfinden desselben Boards. Die Boardfunktionen des [UnifiedFloppyTool](UnifiedFloppyTool) verwenden im untersuchten Stand den nativen CDC-Weg. Für Netzwerkprogramme siehe [LAN](LAN).

## Sitzungsstart

1. Port explizit wählen und öffnen; bei mehreren Geräten nach Seriennummer unterscheiden.
2. Den eigenen Transportkanal zurücksetzen und alte Empfangsdaten verwerfen.
3. `PING` mit einer neuen Zufallskennung senden und die identische Antwort prüfen.
4. `INFO` lesen: Protokollversion, Boardrevision, Zeitbasis und Payloadgrenze auswerten.
5. Je nach angekündigter Fähigkeit `DESCRIBE`, `FEATURES`, `WRITE_LIMITS` und die `0x0F`-Discovery-/Maskenabfragen verwenden. Unbekannte Erweiterungen nicht voraussetzen.
6. Genau eine Anfrage offen halten; Ereignisse separat zustellen und Datenoperationen bis zum zugehörigen `END` verfolgen.

Ein erfolgreiches `PING` belegt die Kommunikation, keine Laufwerksabnahme. INFO-Bits, zulässige Portbelegung, unterstützte Flags und Hardwarequalifikation sind verschiedene Aussagen. Setzende Konfigurationsbefehle können Generationen ändern: Tokens für geschützte Aufträge erst nach dem Einstellen abfragen.

## Rahmenaufbau

Alle Mehrbytewerte sind **little endian**. Ein Rahmen ist `10 + payload_length` Byte lang, maximal 4106 Byte.

| Offset | Länge | Feld |
|---:|---:|---|
| 0 | 2 | Sync `55 AA` |
| 2 | 1 | Typ: 1 Anfrage, 2 Antwort, 3 Ereignis, 4 Daten, 5 Ende |
| 3 | 1 | Befehlscode `cmd` |
| 4 | 1 | Sequenz `seq` |
| 5 | 1 | Status; in Anfragen 0 |
| 6 | 2 | Payloadlänge, 0 bis 4096 |
| 8 | variabel | Payload |
| 8 + Länge | 2 | CRC-16/CCITT-FALSE über Offset 2 bis zum letzten Payloadbyte |

CRC: Polynom `0x1021`, Start `0xFFFF`, keine Spiegelung, kein finales XOR. Der CRC-Wert steht ebenfalls little endian im Rahmen. ASCII `123456789` ergibt `0x29B1`.

Der Host verwendet seq 1 bis 255, dann wieder 1. Antwort, DATA und END gehören zur Anfrage; Ereignisse besitzen eigene Sequenzen. Eine Rahmenfehlerantwort kann `cmd=0xFF, seq=0, status=14` tragen und ist der offenen Anfrage zuzuordnen.

### Geprüfte Hex-Vektoren

Die letzten beiden Bytes sind jeweils die tatsächlich berechnete CRC, keine Platzhalter.

```text
PING request, seq=1, ASCII UFI2:
55 AA 01 00 01 00 04 00 55 46 49 32 38 08
PING response, seq=1:
55 AA 02 00 01 00 04 00 55 46 49 32 F7 B9
SELECT A, seq=2:
55 AA 01 10 02 00 01 00 01 B9 30
READ track=0, side=0, revs=3, flags=0, period_ms=0, seq=3:
55 AA 01 20 03 00 06 00 00 00 03 00 00 00 2E 87
```

## Parser und Transport richtig implementieren

- `read()` kann ein Byte, einen halben Rahmen oder mehrere Rahmen liefern. USB-Paket- und API-Aufrufgrenzen sind keine Protokollgrenzen.
- Ein inkrementeller Parser behält ein abschließendes `55`, wenn `AA` erst später kommt. Er prüft Typ, Längenobergrenze und CRC, bevor er Payloads ausliefert.
- Innerhalb eines vollständig geprüften Rahmens ist `55 AA` gewöhnlicher Inhalt. Nur bei einer verworfenen Rahmenhypothese erneut nach Sync suchen.
- Auch `write()` kann nur einen Teil übernehmen. Bis zur vollständigen Ausgabe weiterschreiben; fehlenden Fortschritt und absolute Zeitlimits behandeln.
- Ereignisse aus dem Antwortpfad herausnehmen. Für Antwort/DATA/END Typ, Befehl und Sequenz abgleichen; alte Antworten dürfen einen neuen Auftrag nicht abschließen.
- Bei Transferverlusten den laufenden Fluxauftrag als unvollständig behandeln. Eine spätere gültige CRC macht verlorene Daten nicht wieder vollständig.
- Unbekannte Schemata ablehnen oder bewusst behandeln; Mindestlängen vor jedem Feldzugriff prüfen. Erweiterbare Antworten anhand der dokumentierten Schema-/Größenfelder lesen.

Der vorhandene [Python-Parser](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/protocol2.py) ist eine ausführbare Referenz für Rahmen und Zuordnung. Seine `FrameParser.feed()`-Methode nimmt beliebig geschnittene Byteblöcke an. Ein Host sollte Zeitlimits auf die erwartete Operation abstimmen, nicht bei jedem empfangenen Byte unbegrenzt verlängern.

## INFO und wichtige Befehle

Die feste INFO-Payload ist 18 Byte lang; danach folgt optionaler ASCII-Text. Binärfelder auswerten, keine Versionen oder Fähigkeiten aus dem Anzeige-Text erraten.

| Offset in INFO | Typ | Feld |
|---:|---|---|
| 0 | u8 | Protokollversion, erwartet 2 |
| 1, 2 | je u8 | Firmware major/minor |
| 3 | u8 | Boardrevision: 10 für v1.0, 5/6/7 für v0.x, 0 unbekannt |
| 4 | u32 | Capability-Bits |
| 8 | u32 | Native Zeitbasis `sample_hz`, derzeit 275000000 |
| 12 | u32 | Fluxspeicher in Bytes; nicht SD-Kapazität |
| 16 | u16 | Maximale Payloadgröße |
| 18 | Bytes | Optionaler ASCII-Anzeigetext |

| Code | Funktion | Einstieg |
|---|---|---|
| `00` / `01` / `02` | PING / INFO / STATUS | Verbindung und Zustand |
| `06` | EVENTS | Maske setzen; aktuelle Firmware kann auch nur den Zustand abfragen |
| `08` / `09` / `0C` | WRITE_LIMITS / DESCRIBE / FEATURES | Grenzen, Takte, Transport und Fähigkeiten |
| `0A` / `0B` | RESOURCE / JOB | Besitz bzw. optionale Transportintegrität |
| `0F` | ANALYSIS_JOB mit Unter-Op | Discovery, Masken, Generationen, Analyse und Berichte |
| `10` / `11` / `12` / `13` / `14` | SELECT / MOTOR / SEEK / RECAL / SIDE | Laufwerkssteuerung |
| `20` / `21` / `22` | READ / ABORT / WRITE | Fluxoperationen |
| `27` / `28` | GUARDED_WRITE / GUARDED_READ | Start mit Konfigurations- und Mediengeneration |
| `30` bis `37` | IEC-Familie | Commodore-Bus, Sektoren und DOS-Status |
| `40` bis `49` | Board-/Netzwerkfunktionen | Versorgung, Speicher, Sensoren, LAN und AUTH |
| `50` bis `54` / `60` bis `64` | Dateien / Standalone | SD-Dateien, Konfiguration, Dump/Kopie/Schreiben |

Codes in dieser Tabelle sind hexadezimal; Bereiche sind keine Zusage, dass jede Nummer belegt ist. Die vollständigen Nutzlasten stehen in der Referenz, Kapitel 3. Die `0x0F`-Unter-Ops werden dort überwiegend **dezimal** angegeben: z. B. Op 38 = Byte `0x26`, nicht `0x38`.

## Eine Spur lesen

Nach passender Auswahl, Motorstart und Referenzfahrt eine READ-Anfrage senden. Der übliche Grundkörper ist `<BBBBH>`: `track`, `side`, `revs`, `flags`, `period_ms` (6 Byte). Optionale Felder folgen abhängig von Flags. Keine Bits aus WRITE oder ANALYZE übernehmen.

| READ-Flag | Bedeutung |
|---|---|
| Bit 0 | Ohne Index, Zeitabschnitte verwenden |
| Bit 1 | Hartsektoroption; zusätzliches Sektorzahlbyte |
| Bit 2 | Spurbyte kodiert Viertel-/Halbspurauswahl gemäß Laufwerk |
| Bit 3 | Provenienzblock im END |
| Bit 4 | Fortschrittsereignisse je Umdrehung; zusätzlich EVENTS-Maskenbit 5 setzen |
| Bit 5 | Lesekontext im END |
| Bit 6 | Vorlauf vor Index 0; braucht geeigneten Index und PSRAM |
| Bit 7 | Zusätzliches `flags2`-Byte am Ende; dessen Bit 0 ist auf unterstützter Firmware Doppelschritt pro READ |

Vor Erweiterungen die Masken über `0x0F [38]` abfragen. Bei Erfolg kommt zuerst RESPONSE `cmd=0x20`, danach DATA und abschließend END mit derselben Sequenz. Annahme ist noch kein Aufnahmeerfolg. END liefert mindestens die Zahl vollständiger Umdrehungen. Aktuelle Firmware ergänzt den 17-Byte-Generationsblock; angeforderte Provenienz (96 Byte) und Lesekontext (132 Byte) folgen in dieser Reihenfolge. Bei aktivem Integritätsjob steht dessen Abschlussnachweis davor. Details: Referenz 2.6, 4.5, 4.12 und 4.24.

**GUARDED_READ:** Die Annahme antwortet auf `0x28`, DATA und END bleiben aber **`0x20`**, bei gleicher seq. GUARDED_WRITE antwortet entsprechend auf `0x27`, streamt und endet unter **`0x22`**. Ein Parser, der nur den ursprünglichen Befehl erwartet, bleibt sonst hängen.

### Fluxdaten dekodieren

| Code | Bedeutung |
|---|---|
| `01..EF` | Fluxabstand in nativen Ticks |
| `F0..FC b` | Abstand `240 + ((code - 0xF0) << 8) + b` |
| `FD u32` | Index bei `letzter_Fluxzeitpunkt + u32`; keine Fluxflanke |
| `FE u32` | Fluxabstand u32 |
| `FF 01 u32` / `FF 02 u32` | Angeforderter Vorlauf / Index 0; nur bei READ Bit 6 |

Ein Index verändert den Zeitpunkt der letzten Fluxflanke nicht. Der nächste Fluxabstand zählt weiterhin ab dieser Flanke, nicht ab der Indexmarke. Codewörter können DATA-Rahmen überschreiten: einen unvollständigen `FD`, `FE` oder `FF`-Code zwischen Payloads aufbewahren.

Ohne Vorlauf beginnt die Aufnahme am ersten Index ohne führende `FD`-Marke. Jede abgeschlossene Umdrehung endet mit einer `FD`-Marke; N erfolgreich gesendete Umdrehungen ergeben N Marken, nicht N+1. Zeitabschnitte ohne physische Indexquelle sind keine gemessene Winkelreferenz. Zeiten in mindestens 64 Bit akkumulieren und für Anzeige/Export vom gemeldeten `sample_hz` umrechnen.

## Schreiben und Wiederverbindung

Für Schreiben zuerst die Fähigkeiten, zulässigen Flagmasken, Backendgrenzen und Schreibsperren prüfen. Die WRITE-Anfrage enthält Anzahl der Fluxintervalle und Länge des **kodierten** Streams als unterschiedliche Felder. DATA enthält keine READ-Indexmarken. Beim Integritätsjob kommen die 16-Byte-DATA-Köpfe zusätzlich hinzu; sie zählen nicht zur Fluxbytezahl. Vor JOB-COMMIT beginnt dort kein physisches Schreiben.

Ein verlorenes ACK oder END erlaubt **keinen blinden Wiederholungsversuch**. Der Schreibauftrag könnte bereits ausgeführt worden sein. Wenn verfügbar JOB abfragen, Ergebnis sichern und den Zustand neu beurteilen. Nach Reset gibt es keine persistente Exactly-once-Garantie. Status 19 bedeutet, dass der Index eine bereits geschriebene, zu lange Spur abgeschnitten hat; er bedeutet nicht „nichts geschrieben“.

Verify-Level sind Nachweisklassen: 0 keine Aussage, 1 Ausgabe beendet, 2 Rücklesen plausibel, 3 Sektorinhalt geprüft, 4 nicht prüfbar. Ein höherer Zahlenwert ist nicht automatisch ein besserer Nachweis. Siehe [Schreiben und Prüflesen](Schreiben-und-Prueflesen).

## Ereignisse und Fehler

EVENTS-Maskenbit = Ereigniscode minus `0x80`. Ereignisse sind Hinweise und können verloren gehen. Aktuelle Firmware meldet per EVENTS die Kapazität (derzeit drei Ereignisse, maximal 12 Payloadbytes) sowie kumulative Verlustzähler. Vorher/nachher vergleichen; alte Firmware kann eine leere Antwort oder Status 16 auf die reine Abfrage liefern. Den maßgeblichen Status einer Operation aus RESPONSE/END auswerten.

Häufige Statuswerte: 1 BUSY, 3 SEEK_FAILED, 4 NO_INDEX/älterer Abbruchpfad, 6 DMA/Verify, 10 OVERFLOW, 11 UNSUPPORTED, 12 WRITE_PROTECTED, 14 FRAME_ERROR, 16 BAD_ARGS, 18 NOT_REPRESENTABLE, 19 TRACK_LONG, 20 CANCELLED, 21 MEDIA_CHANGED, 22 Sitzungsschreibsperre, 23 SPEED_MISMATCH. Über LAN kommen 24–26 hinzu. Das sind UFI-Statuscodes, keine Betriebssystemfehler.

## Kleines Python-Beispiel

Nach [Installation des Python-Hosts](Installation) das [PING-/INFO-Beispiel](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/wiki-tools/examples/ufi_v2_probe.py) verwenden. Es übernimmt den vorhandenen Parser und sendet keine Laufwerks- oder Medienbefehle. Der Kommandozeileneinstieg verlangt explizit den nativen CDC-Port und setzt vor dem Probe den Kanal zurück.

```console
python docs/wiki-tools/examples/ufi_v2_probe.py --port COM5
```

Das Skript steht im Hauptrepository; den tatsächlichen Port einsetzen. Kern für ein eigenes Programm:

```python
from ufi_host.protocol2 import Device2

device = Device2(prepared_byte_stream, timeout=2.0)
if not device.ping(b"my-tool-probe"):
    raise RuntimeError("PING echo mismatch")
info = device.device_info()
print(info.proto, info.sample_hz, info.max_payload)
```

`prepared_byte_stream` ist ein bereits geöffneter und vorbereiteter Transport mit `read(n)` und `write(data)`, kein direkt ausführbarer Platzhalter. Das vollständige Skript erledigt Portprüfung, Vorbereitung und Schließen. Andere Sprachen können Rahmen, Testvektoren und Zustandsfolge unabhängig davon implementieren.

## Eigenen Client prüfen

Vor einem Gerätetest mindestens Teilrahmen an jeder Byteposition, mehrere Rahmen in einem Read, Syncbytes in Payloads, defekte CRC/Länge, verschachtelte Ereignisse, fremde Sequenzen, Annahmefehler, END-Fehler, abgeschnittene Fluxcodes und verlorenes END prüfen. Danach Testmedium und Mitschnitt verwenden. Die vorhandenen [Hosttests](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/tests/test_protocol2.py) liefern ein Byte-Level-Double; die [Firmware-Simulationstests](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tests/test_sim_host.py) ergänzen Prüfungen gegen ausgeführten Firmwarecode. Beides ersetzt keine elektrische Messung.

Quellen: [Protokollquelle](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md), [Firmware-Dispatcher](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_v2.c), [Fluxencoder](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_stream.c), [Python-Host](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/protocol2.py).
