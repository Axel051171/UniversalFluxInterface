# USB-Protokoll der UFI Flux Engine (UFI Headless)

Stand: Firmware 1.18-dev, Board v0.7; ergänzt am 22.10.2026 (4.18–4.23) und am 26.10.2026 (2.2 Zählregel der Indexmarken, 4.12 Offset 90–95, 4.24).
Alle Mehrbyte-Werte **little endian**. Die Kapitel 1–9 beschreiben den implementierten
Softwarestand des UFI-v2-Protokolls, Kapitel 10 den Greaseweazle-Modus. Hardware-,
Betriebssystem- und Laufwerksqualifikation stehen aus. Die Abschnitte sind inhaltlich
aus der bisherigen, chronologisch gewachsenen Fassung übernommen und nur neu geordnet;
die Zuordnung der alten Nummern steht in Anhang A.

## Inhalt

- 1 Überblick und Transport
  - 1.1 USB-Personalitäten
  - 1.2 Composite-Transport und Geräteerkennung
  - 1.3 Fähigkeiten und Konfigurationsgeneration (GLaDISK-Anregungen)
  - 1.4 Ressourcen und Feature-Abfrage
  - 1.5 Platine v1.0: Uhr, Sensor, Versorgungsspannungen, Sync-Sensor (Stufe A10)
  - 1.6 LAN-Transport (Platine v1.0, W5500 U23)
- 2 Rahmen, Zeitbasis und Ereignisse (UFI v2)
  - 2.1 Rahmen (beide Richtungen)
  - 2.2 Flusscode (READ-Daten, WRITE-Daten), 275 MHz
  - 2.3 Ereignisse (type 3, nach EVENTS)
  - 2.4 Zeitbasis und erweiterter Schreibpfad
  - 2.5 Abbruch, Zeitlimits und Wiederverbindung
  - 2.6 Integritätsjobs: JOB 0B
- 3 Befehle und Statuscodes (UFI v2)
  - 3.1 Befehlstabelle
  - 3.2 Statuscodes
  - 3.3 Festlegungen im Detail
  - 3.4 Ergänzungen ab Firmware 1.18-dev (IEC-Adressierung)
  - 3.5 Beispiel: Spur lesen
  - 3.6 Flag-Abhängigkeiten und Wechselwirkungen
- 4 Laufwerk und Medien
  - 4.1 Aufnahmebelege und zusätzliche Laufwerkszeiten (INFO Bit25)
  - 4.2 Positionsgültigkeit, Laufwerksprofile und Mediengeneration (INFO Bit27)
  - 4.3 Optionale Profilprüfung (2026-10-16)
  - 4.4 Portbelegung und gespeicherte Betriebsprofile (INFO Bit28, 2026-10-16)
  - 4.5 Mediengebundene Starts und robuste Anfrageprüfung
  - 4.6 Sitzungs-Schreibsperre (Op 37, Status 22, 1.18-dev)
  - 4.7 Flagmasken-Abfrage (Op 38, 1.18-dev)
  - 4.8 Grund der letzten Abweisung (Op 39, 1.18-dev)
  - 4.9 Prüflesebericht je Sektor (Op 40, 1.18-dev)
  - 4.10 Drehzahlprüfung vor dem Schreiben (Op 41, Status 23, 1.18-dev)
  - 4.11 Weak-Fenster als Zähler (Op 42, 1.18-dev)
  - 4.12 Provenienzblock (Op 43, 1.18-dev)
  - 4.13 Weak-Positionen fein (Op 44, 1.18-dev)
  - 4.14 Präkompensation je Auftrag und Zone (Op 45, `flags2` Bit2, 1.18-dev)
  - 4.15 Spurübersicht als Job (Op 46, 1.18-dev)
  - 4.16 Schwache Zellen schreiben (`flags2` Bit3, 1.18-dev)
  - 4.17 Rohprüfung je Schreibauftrag (`flags2` Bit4, 1.18-dev)
  - 4.18 Sektor-Befundbericht mit Prüfmasken (Op 47, 22.10.2026)
  - 4.19 Gespeicherte Flux-Aufnahme abrufen und erneut analysieren (Op48/52)
  - 4.20 Einzelne Inhaltsvarianten (Op49) und Informationsgewinn
  - 4.21 Zeitabweichungen über mehrere Umdrehungen (Op51)
  - 4.22 Präzises Roh-Prüflesen mit U11-Referenz (Op50/54)
  - 4.23 Flaggesteuerte Kopierschutz-Hinweisanalyse (Op55/56)
  - 4.24 Lesekontext im READ-Ende (READ-Flag Bit5, 1.18-dev)
- 5 Lesen, Schreiben und Prüflesen
  - 5.1 Laufzeitdiagnose und automatisches Host-Prüflesen
  - 5.2 Firmware-Detailanalyse: acht Flag-Bits (1.18-dev)
  - 5.3 GCR-Prüflesen, Umdrehungsvergleich und Wiederholungsstrategie (INFO Bit26)
  - 5.4 Macintosh-GCR, Zonenprüfung und getrennte Abweichungsbefunde (INFO Bit29)
  - 5.5 Lokale Zeitfenster, HFEv3 und getrennte Schwachstellenbefunde
  - 5.6 Erkennungszustände, Positionsbefunde und Wiederholungsursachen
  - 5.7 Apple-II-5,25-Zoll-GCR (Format 7)
  - 5.8 Toleranzflags und Sektorbefunde aller Decoder (Op 34)
- 6 Analysejobs
  - 6.1 Analysejobs, Discovery und Prüfabdeckung (INFO Bit24, 2026-10-14)
  - 6.2 Mehrspur-Analyse mit bestätigter Fortsetzung (0x0F, Op27–29)
  - 6.3 Dauerhafte Analysepläne, Rohspurprüfung und SD-Abschluss
- 7 SD-Karte und Standalone-Betrieb
  - 7.1 Standalone-SD-Schreiben und Prüflesen
- 8 IEC (Commodore)
  - 8.1 IEC-Erkennung und wiederaufnehmbare Schreibaufträge
- 9 Nachweise und offene Qualifikation
  - 9.1 Nachweise und offene Qualifikation
- 10 Greaseweazle-Modus (`protocol=gw`)
  - 10.1 Rahmen
  - 10.2 Befehle
  - 10.3 Flusscode (Abtastrate 68,75 MHz = 275 MHz / 4)
- Anhang A Zuordnung der bisherigen Abschnittsnummern

## 1 Überblick und Transport

### 1.1 USB-Personalitäten

| Modus | VID:PID | Produktname | Klasse | Auswahl |
|---|---|---|---|---|
| UFI-Flux | 1209:4F54 | UFI Flux Engine | CDC (virtueller COM-Port) | Schalter J13 Mitte, `UFI.CFG protocol=ufi` (Standard) |
| Greaseweazle | 1209:4F57 | UFI Flux Engine (gw-compat) | CDC | Schalter Mitte, `UFI.CFG protocol=gw` |
| Composite | 1209:4F58 (Entwicklungs-PID) | UFI Native + Flux (gw-compat) | GW-CDC + UFI-Bulk | `protocol=composite`, `USB_MODE 4` |
| SD-Laufwerk | 1209:4F55 | UFI Flux Storage | Mass Storage | Schalter Pin 3, Taster B ≥ 2 s, Befehl `USB_MODE 1` |
| USB-Floppy | 1209:4F56 | UFI USB Floppy | Mass Storage | Schalter Pin 1, Befehl `USB_MODE 2` |

Nach jedem Wechsel meldet sich das Gerät neu an (ca. 200 ms getrennt). ACT blinkt die Betriebsart: 1× Flux, 2× SD, 3× Floppy, 4× Greaseweazle, 5× Composite. Die Baudrate des COM-Ports ist bedeutungslos, außer **10000 Baud = Kanal zurücksetzen** (Greaseweazle-Konvention, gilt auch für UFI v2).

Im UFI-Flux-Modus versteht das Gerät zwei Protokolle: das alte **UFI v1** (ein Befehl je USB-Paket, siehe `software/ufi_host/README.md`) und **UFI v2** (Abschnitt 3). Ein USB-Paket, das mit `0x55 0xAA` beginnt (also der erste v2-Rahmen in einem Schreibaufruf; nicht innerhalb laufender v1-Schreibdaten), schaltet die Sitzung auf v2 um; sie bleibt v2 bis zum Kanal-Reset (10000 Baud) oder zur Neuanmeldung. Neue Software sollte nur v2 benutzen.

---

### 1.2 Composite-Transport und Geräteerkennung

`protocol=composite` beziehungsweise `ufi mode composite` aktiviert Modus 4.
Die Verbindung bleibt USB Full Speed; beide Funktionen teilen dessen Bandbreite.
Die Entwicklungsidentität `1209:4F58` muss vor Veröffentlichung auf zulässige
VID/PID-Zuteilung geprüft werden. Die Seriennummer stammt aus der MCU-UID und ist
in allen Personalities (4F54–4F58) dieselbe: ein Seriendeskriptor bedient jede
PID, der Host findet dasselbe Board nach einem Moduswechsel und der folgenden
Neu-Enumeration an der Seriennummer wieder.

| Funktion | Interfaces | Endpoints | Protokoll |
|---|---|---|---|
| Greaseweazle CDC-ACM | 0 Control, 1 Data; IAD | OUT 01, IN 81, Notification 82 | unveränderte GW-Rahmen |
| UFI Native | 2, Klasse FF | OUT 03, IN 83; je 64 Byte | ausschließlich UFI v2 |

Der Composite-Wrapper verwendet den bestehenden CDC-Kontext und eigene
Bulk-Puffer/Zustände. Die Konfiguration hat 98 Byte. Das FIFO-Budget liegt bei
736 von 1024 Wörtern. Microsoft-OS-1.0-Deskriptoren ordnen ausschließlich Interface 2
WinUSB zu: String EE, Vendor-Code 20, Compatible-ID WINUSB, GUID
`{9F78D994-00A5-4F27-A1D0-071747F159A8}`. Dies folgt dem
[Microsoft-Verfahren](https://learn.microsoft.com/en-us/windows-hardware/drivers/usbcon/automatic-installation-of-winusb);
die tatsächliche Enumeration auf Windows/Linux/macOS ist noch nicht getestet.

Der Python-Transport verwendet PyUSB/libusb, beansprucht nur Interface 2 und
ändert keine USB-Konfiguration. `--port usb` verlangt genau ein passendes Gerät;
`--port usb:SERIENNUMMER` wählt explizit. Bulk hat keinen v1-Rückfall. Verbindungsaufbau:
Kanal zurücksetzen, PING mit frischer Zufallskennung, INFO, optional DESCRIBE/FEATURES.

### 1.3 Fähigkeiten und Konfigurationsgeneration (GLaDISK-Anregungen)

Neue native Operationen unter `0x0F`: 22 Fähigkeitsmatrix, 23 Konfigurationsbericht,
24 generationsgeprüfter Analyse-Start. Keine neuen INFO-Bits (alle 32 belegt),
keine neuen Jobflags und keine Änderung der Greaseweazle-Kommandos.
Discovery durch `0x0F [22]`: ältere Firmware kann BAD_ARGS melden. Dann fehlen
diese Erweiterungen; das Tool darf keine Generation oder Qualifikation annehmen.

**Op22, Anfrage `[22]`: Antwort Schema 2 mit 320 Byte (1.18-dev; Schema 1 mit
224 Byte bei älteren Builds).** Die Matrix beschreibt den formatunabhängigen
Rohflusspfad (Zeile 0), die sieben Decoderfamilien (Zeilen 1–7) und den
IEC-Blockweg über die Laufwerks-CPU (Zeile 8). „Schreiben“ bedeutet Übertragen
bereits erzeugter Fluxintervalle, keinen Sektorencoder für jedes aufgeführte
Format. Ein Leser nimmt Zeilenanzahl, Zeilengröße und Kopfgröße aus dem Kopf
und findet Zeilen über ihr ID-Byte, nicht über die Position; Schema 1 hat genau
die sechs Zeilen 1–6, Schema 2 die neun Zeilen 0–8 in aufsteigender ID-Reihenfolge.
Keine Dateiformat-Fähigkeitsmatrix.

| Offset | Typ | Inhalt |
|---:|---|---|
| 0–3 | je u8 | Schema 2 (1 bei älteren Builds), Zeilenanzahl 9 (Schema 1: 6), Zeilengröße 32, Headergröße 32 |
| 4 | u32 | Aktuelle Konfigurationsgeneration |
| 8 | u32 | Befundflags |
| 12 | u32 | Bekannte Befundflagmaske 63; PROFILE_CHANGED nur in Op23 |
| 16 | u32 | Flux-Takt, 275000000 Hz |
| 20 | u8 | Ausgewählter Port/Laufwerkstyp |
| 21, 22 | je u8 | Konfigurationsabfrage 23, geprüfter Start 24 |
| 23, 24 | je u8 | Erweiterte Discovery25 und mediengebundener Analyse-Start26 (siehe4.5);0 bei älteren Builds |
| 25–27 | Bytes | Mehrspur-Operationen: 27 Anlegen, 28 Status, 29 Steuern |
| 28–30 | Bytes | Operationen30 Checkpoint,31 Rohspur-Optionen,32 Rohspur-Bericht |
| 31 | u8 | Reserviert, 0 |
| 32 | 9 × 32 Bytes | Zeilen in aufsteigender ID-Reihenfolge 0–8 (Schema 1: 6 × 32 Bytes, IDs 1–6) |

Formatzeile, relative Offsets:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Zeilen-ID: 0 Rohfluss (formatunabhängig), 1 IBM-MFM, 2 Amiga, 3 IBM-FM, 4 HP-MMFM, 5 CBM-GCR, 6 Mac-GCR, 7 Apple-II-GCR (5.7), 8 IEC-Blockweg (Schema 1: nur 1–6) |
| 1 | u8 | Implementierte Softwarepfade, aktuell 15 |
| 2 | u8 | Nach Port, Leitungskonflikten und Belegung derzeit zulässige Softwarepfade |
| 3 | u8 | Offline getestete Teile der Formatfamilie: 10 = Decoder und Inhaltsvergleich |
| 4 | u8 | Hardwarequalifizierte Pfade, aktuell immer 0 |
| 5 | u8 | Implementierte Raw-Flux-Backends: Bit0 klassisch, Bit1 Extended, aktuell 3 |
| 6 | u8 | Nach Port/Belegung zulässige Backends: 0, 1 oder 3; Apple ohne Extended |
| 7 | u8 | Voraussetzungen, aktuell 7: Bit0 Quelldaten erforderlich, Bit1 Intervallprüfung erforderlich, Bit2 physischer Aufbau für Capture/Write/Prüflesen erforderlich |
| 8 | u32 | Decoder-Ratenmaske, keine Schreibratenfreigabe |
| 12, 16 | je u32 | Klassische Schreibgrenzen in Flux-Ticks: 440…65536 |
| 20, 24 | je u32 | Extended-Schreibgrenzen: 220…4294967295 |
| 28 | u32 | Befundflags dieser Zeile |

Pfadmasken bei 1–4: Bit0 Capture, Bit1 Decodierung (Board-Decoder in ANALYZE
DETAIL und Prüflese-Referenz), Bit2 Raw-Flux-Schreiben, Bit3 Inhalts-Prüflesen.
Die Werte „aktuell 15 / 10 / 3 / 7“ in der Tabelle gelten für die Zeilen 1–6; die
übrigen Zeilen weichen ab (Schema 2):

| Zeile | implementiert | zulässig (nicht belegt) | offline geprüft | Backends | Voraussetzungen | Ratenmaske, Grenzen |
|---:|---|---|---|---|---|---|
| 0 Rohfluss | 5 (Capture, Raw-Flux-Schreiben; kein Decoder, kein Prüflesen) | 5 mit Flux-Port ohne Leitungskonflikt, sonst 0 | 5 (Stromparser, Schreibcodec) | 3; zulässig wie Zeilen 1–6 (1 bei Apple) | 7 | Maske 0, Grenzen wie Zeilen 1–6 |
| 7 Apple-II-GCR | 7 (kein Prüflesen: die Firmware-Referenz kennt kein Apple-Format) | 7 nur mit gewähltem Apple-Port (Flux ohne Dichteausgang), sonst 2 (nur Decoder) | 2 (Decoder, MOOF-Korpus) | 1 (nur klassisch); zulässig 1 am Apple-Port | 7 | Maske `0x0001` (250 kbit/s), klassische Grenzen 440…65536, Extended 0/0 |
| 8 IEC-Blockweg | 13 (Blöcke lesen = Bit0, schreiben = Bit2, Rücklesen = Bit3; Bit1 entfällt) | 13 nur mit gewähltem IEC-Laufwerk (Portbericht: Laufwerk ≠ 0 ohne Flux-Bit), sonst 0 | 13 (IEC-Regressionen) | 0 | 5 (Quelldaten, physischer Aufbau) | alles 0 |

Eine formatunabhängige Zustimmung zum Rohfluss-Lesen oder -Schreiben liest das
Werkzeug aus Zeile 0, nicht aus der UND-Verknüpfung der Formatzeilen.
`hardware_qualified` ist in jeder Zeile 0, bis die Bank-Abnahme je Pfad ein Bit
setzt (Abnahme_Schreibpfad.md). Offline-Bit3 belegt den Referenz-/Inhaltsvergleich an
Testdaten, **keinen physisch ausgeführten Write/Read-Zyklus**. Die Tests decken
Formatfamilien ab, nicht jede mögliche Kombination aus Rate, Port und Medium.
Bei belegter Engine ist die Zulässigkeitsmaske 0. Ohne Flux-Port oder bei
Leitungskonflikten bleibt nur der reine Decoderpfad mit Wert 2 zulässig.

Ratenbits 0–13 in dieser Reihenfolge: 250000, 300000, 500000, 1000000, 125000,
150000, 266667, 285714, 307692, 380228, 346021, 312500, 280112, 251256.
Masken der Formate 1–7: `0x000F`, `0x000F`, `0x0037`, `0x0004`, `0x01C1`,
`0x3E04`, `0x0001`. Die Liste beschreibt Decoderkandidaten; Mac-Zonen gelten weiterhin.

Zulässigkeit ist eine Momentaufnahme, keine Erfolgsgarantie: Quelldaten,
Schreibschutz, Index, Laufwerks-/Medienkompatibilität, U11-Kapazität und jedes
einzelne Fluxintervall werden weiterhin vom jeweiligen Befehl geprüft.
Extended erlaubt weiterhin keine Apple-Ausgabe, Vorkompensation oder Skalierung.
DESCRIBE, WRITE_LIMITS und bestehende Befehlsprüfungen bleiben maßgeblich.
Insbesondere bestätigt 1-Mbit/s-Decodierung keine physische ED-Schreibfähigkeit;
kurze ED-Intervalle können die klassische Untergrenze unterschreiten.

**Neue Befundflags, getrennt von bestehenden Auftrags-/Erkennungsflags:**

| Wert | Name | Bedeutung |
|---:|---|---|
| 1 | CAPABILITY_UNCONFIRMED | Physische Fähigkeiten der angeschlossenen Kombination nicht qualifiziert; derzeit immer gesetzt |
| 2 | WRITE_MODE_UNQUALIFIED | Physischer Schreibmodus nicht qualifiziert; derzeit immer gesetzt |
| 4 | PROFILE_CHANGED | Erwartete Konfigurationsgeneration stimmt nicht überein; nur Op23 |
| 8 | NO_FLUX_PORT | Kein Flux-Laufwerk gewählt, etwa NONE oder IEC |
| 16 | BUSY | Engine oder Integritätsjob belegt |
| 32 | LINE_CONFLICT | Portbericht meldet widersprüchliche/nicht unterstützte Leitungsbelegung |

**Op23:** `[23]` oder `[23, expected_generation:u32]`, Antwort 24 Bytes:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0–3 | je u8 | Schema 1, geprüfter Start 24, Gültigkeitsbereich 1 = aktuelle Sitzung, reserviert 0 |
| 4, 8 | je u32 | Aktuelle und angefragte Generation; angefragt 0 ohne Vergleich |
| 12 | u32 | PROFILE_CHANGED=4 bei ungleicher expliziter Erwartung, sonst 0 |
| 16 | u32 | Separate aktuelle Mediengeneration; 0 ohne Flux-Laufwerk |
| 20 | u32 | Unterstützte Vergleichsflagmaske 4 |

Die Konfigurationsgeneration ist ein konservativer globaler Invalidierungszähler,
kein Hash und keine Anzahl geänderter Felder. Sie steigt bei Initialisierung,
Laufwerkswahl, angenommenen Timing-/Extra-Timing-, Laufwerks-/Medienprofil-,
RPM- und Dichte-/DRATE-Einstellungen, einschließlich nativer und GW-Aufrufe
derselben Treiber. Auch erneutes Setzen identischer Werte kann sie erhöhen;
ein zusammengesetzter Profilaufruf kann mehrere Erhöhungen verursachen.
Damit wird A → B → A erkannt. Reine Abfragen, Seek, automatisch beobachtete
Medienprofile und Jobabbruch verändern sie nicht. Abgewiesene Einstellungen
ohne Nebenwirkungen verändern sie ebenfalls nicht. Ein fehlgeschlagener
Laufwerkswechsel, der bereits Leitungen freigegeben hat, invalidiert den Zähler.
Medienwechsel werden weiterhin durch die **separate Mediengeneration** erfasst.

Der u32-Zähler überspringt beim Überlauf 0. Token niemals über Reconnect,
Neustart oder Firmwarewechsel hinweg aufbewahren; nach einem Neustart sind
Werte wiederverwendbar. Nur Gleichheit vergleichen, keine Ordnungsannahmen.

**Op24:** `[24, expected_generation:u32, vollständige_Op1_Anfrage...]`.
Die eingebettete Anfrage beginnt selbst mit Byte 1; alle bisherigen
Analyseoptionen und Längenregeln gelten. Generation 0, ungleiche Generation
oder ein anderes eingebettetes Kommando werden mit BAD_ARGS abgewiesen,
bevor Capture startet. Bei Gleichheit folgt unmittelbar der bestehende
Analyse-Start samt BUSY-, Ressourcen- und Parameterprüfungen. Antwort wie Op1.
Ein Token ist keine Reservierung und wird nicht verbraucht. Op24 schützt
den **Start eines Analysejobs**, nicht eine ganze Mehrspurtransaktion.
Ein separates Op23 vor WRITE ist nur eine Momentaufnahme. Der neue Schreibstart
und die zusätzliche Medienbindung stehen in Abschnitt 4.5.

Empfohlener Ablauf: Profile setzen → Op22/23 lesen → Quelldaten/Analyseauftrag
vorbereiten → Op24 mit gespeichertem Token senden. Bei BAD_ARGS Op23 mit
diesem Token abfragen; PROFILE_CHANGED verlangt erneute Vorbereitung.
Ohne dieses Flag die übrigen Anfrageparameter prüfen. Alte Op1-Aufrufe bleiben
unverändert verfügbar. Hosthelfer: `ufi_host.capabilities.decode_capabilities`,
`decode_configuration`, `guarded_analysis`; Versand über `Device2.request(0x0F, ...)`.

### 1.4 Ressourcen und Feature-Abfrage

`RESOURCE 0A` mit leerer Payload fragt den Besitzer ab; Byte 1 beansprucht den
nativen Zugang, Byte 0 gibt ihn im Leerlauf frei und schaltet die Laufwerke sicher
ab. Antwort: ein Byte, 0 frei, 1 UFI Native, 2 GW. Physische Ressourcen sind derzeit
als eine gemeinsame Gruppe gesperrt. Dies erlaubt keine parallelen Flux-/IEC-Jobs.
Native Aktionen beanspruchen automatisch. GW-Konflikte liefern ACK_BAD_COMMAND,
native Konflikte BUSY. Statusabfragen bleiben möglich. GW gibt bei DESELECT oder
Watchdog frei; Native bei RESOURCE 0, Disconnect oder Moduswechsel. Das CLI gibt
beim Schließen einer Bulk-Sitzung frei. Zusätzlich wird Native nach 30 s Leerlauf
freigegeben. Aktive Operationen, laufende Jobs, ausstehende Antworten und neue
Empfangsdaten verhindern die Freigabe. Danach müssen Laufwerk und Motor bei Bedarf
neu ausgewählt beziehungsweise gestartet werden. GW behält seinen eigenen Watchdog.

`FEATURES 0C` liefert `schema=1, count=14` (10 vor Stufe A10; der Host wertet `count` aus), danach `count` Datensätze à 8 Byte:
`id u16, flags u8, owner u8, limit u32`. Flags: Bit0 implementiert, Bit1 konfiguriert
beziehungsweise verfügbar, Bit2 momentan bereit. Bereit ist eine konservative
Softwareabfrage; sie bestätigt weder ein angeschlossenes Laufwerk noch einen
erfolgreichen Hardwaretest. `owner` entspricht RESOURCE.

| ID | Funktion | limit |
|---|---|---|
| 1 | Flux lesen | maximale Indexanzahl (200) |
| 2 | Erweiterter Flux-Schreibpfad | minimale native Ticks (220) |
| 3 | IEC | größte Geräteadresse (30) |
| 4 | Apple-Port | 0; Boardrevision bestimmt Verfügbarkeit |
| 5 | Amiga-Port | 0 |
| 6 | SD-Speicher | 0; Verfügbarkeit nach SD-Prüfung |
| 7 | PSRAM | aktuell nutzbare Flux-Speicherwörter, auch bei Fallback |
| 8 | Composite-Bulk | Paketgröße (64); verfügbar bei aktivem Composite |
| 9 | Integritätsjobs | maximale DATA-Nutzdaten ohne Jobkopf (4080) |
| 10 | 64-Bit-Zeitbasis | native Timerfrequenz (275000000 Hz) |
| 11 | Echtzeituhr (RTC, Platine v1.0, A10a) | höchstes stellbares Jahr (2099); konfiguriert/bereit = LSE schwingt und der Kalender läuft (`ufi_clock_rtc_ready()`) |
| 12 | SHT40 Temperatur/Feuchte (A10b) | I²C-Adresse 0x44; konfiguriert/bereit = der Sensor hat beim Start mit gültiger CRC geantwortet |
| 13 | SYNC-Sensor auf eigenem Eingang (TIM8, A10c) | Timertakt 275000000 Hz; konfiguriert/bereit = Platine v1.0 und `apple_sync=1` |
| 14 | Versorgungsspannungen am Laufwerksstecker (A10d) | Grenzen in mV: untere 16 Bit 5 V (4500), obere 16 Bit 12 V (10800); konfiguriert/bereit = Platine v1.0 |

### 1.5 Platine v1.0: Uhr, Sensor, Versorgungsspannungen, Sync-Sensor (Stufe A10)

Die 32 Bits des `caps`-Worts in INFO sind belegt (Bit31 = u32-Hinweis); die Fähigkeiten der
Stufe A10 stehen deshalb nur in FEATURES (IDs ab 11) und werden nicht in `caps` gespiegelt.
Die Ops 0x43–0x47 gehören zur Gruppe „Board“ und verhalten sich im Composite-Modus wie
POWER (0x40): sie beanspruchen die native Ressource, sind also keine reinen Abfragen im Sinne
von 1.4. Während READ/WRITE und während eines Dumps sind RTC (0x43), RAILS (0x45) und SYNC (0x46)
erlaubt, SENSOR (0x44) und FDD_PINS (0x47) nicht (Status 1). Alle Zahlen little endian.

**RTC 0x43 (A10a):** Anfrage leer = lesen; 7 Byte `year` u16, `mon`, `day`, `hour`, `min`,
`sec` = stellen und danach lesen. Antwort 8 Byte: `ready` u8, `year` u16, `mon`, `day`,
`hour`, `min`, `sec`. `ready` 0 heißt keine laufende Uhr (Platine vor v1.0 ohne LSE-Quarz,
oder der LSE schwingt nicht an; Felder 0). Stellen ohne Uhr → Status 11, ungültiger Kalender
(Jahr außerhalb 2000–2099, Tag größer als die Monatslänge, Schaltjahre nach der Vierjahresregel,
die für 2000–2099 ausreicht) → Status 16, andere Längen → Status 16, Initialisierungsfenster der
RTC nicht erreicht → Status 5. Die Uhr läuft aus der CR2032 weiter; der Wochentag wird nicht
geführt. Kein Zeitzonen- oder UTC-Begriff: der Host stellt, was er für richtig hält
(`ufi rtc now` stellt die lokale Zeit des PCs).

**Metadaten jeder Aufnahme (A10a):** `DUMPnnnn.JSN` enthält zusätzlich `"rtc"` (0/1), bei
laufender Uhr `"time"` als `"YYYY-MM-DDThh:mm:ss"`, auf v1.0 `"v_fdd5_mv"` und `"v_fdd12_mv"`
(Spannungen am Laufwerksstecker, Op 0x45) und bei antwortendem SHT40 `"temp_c"` und `"rh"` als
Dezimalzahlen mit zwei Nachkommastellen (JSON-Zahlen, keine Strings). `"board"` (Revision) war
schon vorhanden. `DUMPnnnn.LOG` beginnt mit einer Kopfzeile
`UFI dump board <rev> fw <version> <zeit|no-rtc> [5V <mV> mV 12V <mV> mV] [<T> C <RH> %RH]`.
Alle Dateien auf dem SD-NAND tragen seit A10 den FAT-Zeitstempel der Uhr (`get_fattime()` in
`ufi_sd.c`); ohne laufende Uhr bleibt es beim bisherigen festen Datum 2026-01-01 00:00.

**SENSOR 0x44 (A10b):** leere Anfrage; Antwort `status` u8, `temp` i16 (0,01 °C), `rh` u16
(0,01 %RH, auf 0–100 % begrenzt). SHT40 U25 (A9c) an I²C1 PB6/PB7 (2,2 k R19/R20), Adresse 0x44,
Messbefehl 0xFD (hohe Wiederholgenauigkeit, ≤ 8,2 ms), 100 kHz, jeder Transfer mit 10 ms
Timeout, CRC-8 (Polynom 0x31) beider Wörter geprüft. Ohne Sensor (v0.x: PB6/PB7 liegen nur auf
J9) kommt nach etwa 20 ms `status` 1, nie ein Hängen; der Op-Status bleibt dabei 0. `status` 3
heißt, dass die I²C-Initialisierung fehlschlug. Die Messung blockiert die Hauptschleife ≈ 20 ms;
während READ/WRITE und Dumps wird 0x44 deshalb mit Status 1 (busy) abgewiesen. Die Firmware
misst beim Start einmal (FEATURES 12 „bereit“) und für die Metadaten jeder Aufnahme.

**SYNC 0x46 (A10c):** Der Sync-Sensor des Disk-II-Laufwerks (J19 Pin 3) hat seit A11d ein eigenes
Netz: 10 k Pull-up, Schmitt-Inverter U26, PC6 = TIM8_CH1 (AF3), high = Sensorimpuls. Auf v0.x
bleibt der alte Weg (Sensor auf dem INDEX-Bus, Wired-OR); dort antwortet `on` mit Status 11.
Zeitbasis: TIM8 (16 Bit) läuft am selben 275-MHz-Kerntakt wie TIM2, ein TIM8-Takt ist ein
TIM2-Takt. Je Impuls zwei Zeitstempel: `last_t32` = Überlaufzähler (TIM8-Update-Interrupt alle
238 µs) × 65536 + CCR1 (ein Impuls kurz nach einem noch nicht bedienten Überlauf wird an UIF +
CCR1 in der unteren Hälfte erkannt und bekommt das erhöhte Hochwort); `last_t2` = derselbe
Impuls in der TIM2-Zeitbasis der Flussaufnahme: die Capture-ISR liest TIM2->CNT und TIM8->CNT
unmittelbar nacheinander und zieht das 16-Bit-Alter von CCR1 ab (`ufi_sync_map_t2()`, nativ
getestet). Das gilt, solange die Interruptlatenz unter einer TIM8-Periode (238 µs) bleibt; die
ISRs der Prioritätsstufe 0 (Index, Indexsimulation, SYNC, sie verschachteln nicht) brauchen
wenige Mikrosekunden. Fehlerbudget: die beiden Zählerlesungen liegen wenige APB-Takte
auseinander (≤ 40 ns, konstanter Versatz, kein Jitter), U26 ≈ 5 ns, Eingangsfilter (fCK_INT,
N = 8) ≈ 30 ns Verzögerung – weit unter einer Apple-Bitzelle von 1,8 µs. Die Wiederholbarkeit
des Sensorimpulses selbst je Umdrehung ist eine Messung der Inbetriebnahme, keine Aussage der
Firmware. Indexquelle: mit gewähltem Apple-Laufwerk und `apple_sync=1` ersetzt SYNC auf v1.0
die INDEX-Leitung (`source` = 1): die Capture-ISR speist `ufi_flux_index_event()` mit `last_t2`,
der TIM2-Kanal 2 wird ignoriert; READ ab dem Sensorimpuls, WRITE-Start am Sensorimpuls, DIAG_RPM
und der Provenienzblock arbeiten unverändert. Die Umschaltung folgt der Laufwerkswahl in der
Hauptschleife und wird beim READ-Start noch einmal geprüft; sie lässt sich nicht per Op
erzwingen. `on` = 1 startet nur die Zählung (z. B. Sensor auf dem Prüfstand), `on` = 0 hält sie
an, solange SYNC nicht die Indexquelle ist (sonst Status 1). `period` = Takte zwischen den
letzten beiden Impulsen (300 U/min ≈ 55 000 000).

**RAILS 0x45 (A10d):** leere Anfrage; Antwort `v5_mv` u16, `v12_mv` u16, `flags` u8. FDD_5V liegt
über R160/R161 10 k/10 k (× 2) an PC4 = ADC1 INP4, FDD_12V über R162/R163 33 k/10 k (× 4,3) an
PC5 = ADC1 INP8; Quellimpedanz 5 bzw. 7,7 kΩ, Abtastzeit 64,5 Takte wie bei den übrigen
ADC-Kanälen, 16 Bit, Umrechnung `mV × 2` bzw. `(mV × 43 + 5) / 10`. Grenzen 4,5 V / 10,8 V:
`flags` Bit0/Bit1 nur, solange die jeweilige Schiene eingeschaltet ist (Bit2/Bit3 aus
`board_status.power`); Bit4 = Teiler vorhanden (Platine v1.0), ohne Bit4 sind beide Werte 0 (PC4/PC5
sind auf v0.x unbeschaltet). Die eFuse-Fehlerbits bleiben in `board_status.power` Bit2/3. Die
Werte stehen auch in den Metadaten jeder Aufnahme (`v_fdd5_mv`, `v_fdd12_mv`).

**Selbsttest der Schreibsperre (A10d, Posten 20 `wgate_lock`):** nur auf Platine v1.0 (A11f: Q9-Gate =
NOR(WGATE_n, LOCK), LOCK = WLOCK_HW ∨ WLOCK_FW), nur ohne gewähltes Laufwerk und ohne laufenden
Schreibauftrag. Die Firmware setzt WLOCK_FW high (gesperrt, unabhängig vom Frontschalter), prüft
WGATE_SENSE (U10 Pin 13 → PG15) im Ruhezustand, zieht dann PE14 = WGATE_n für 1 ms low und liest
WGATE_SENSE erneut: der Bus darf nicht aktiv werden. `value` 0 = ok, 1 = WGATE trotz Sperre aktiv
(Fehler F1 der A8-Prüfung), 2 = schon im Ruhezustand aktiv (U31/Q9/U10F). Der Messpunkt der H-Liste
(J6 Pin 24 bleibt high bei gesetzter Sperre während eines Lesevorgangs) bleibt die Bestätigung mit
dem Multimeter.

**FDD_PINS 0x47 (A10e):** leere Anfrage = Rücklesungen; 2 Byte `pin4`, `pin33` (0/1/0xFF) setzen
vorher die beiden neuen Ausgänge: J6 Pin 4 über den MOSFET Q12 (1 = Leitung low, `bus_out()`-
Polarität), J6 Pin 33 = PB5 über R156 2,2 k mit R157 2,2 k nach GND (1 = high, erreicht den
Stecker nur mit JP7 auf Signal). Antwort `pins` (Bit0 Pin 2 low – PD14 über U26, Bit1 Pin 4 low –
PD15, Bit2 J23 Pin 20 !CSTOUT aktiv – PB1, Bit3 WGATE-Bus aktiv – PG15, Bit4 WLOCK_HW high =
Frontschalter offen oder Kabel ab – PE6, Bit5/Bit6 eFuse FLT5/FLT12 low – PF9/PF10, Bit7 SYNC high
– PC6) und `outputs` (Bit0 Pin 4 angezogen, Bit1 Pin 33 high). Vor v1.0 ist `pins` 0 und Setzen
antwortet mit Status 11. Die Ausgänge bleiben, bis sie zurückgesetzt werden oder die Firmware neu
startet (Reset: beide aus); `ufi_drive_safe_state()` fasst sie nicht an. Pin 4 wird von der Firmware
sonst nicht benutzt; was ein Laufwerk auf Pin 2/4/34 treibt, ist eine Messung der Inbetriebnahme
(Bring-up-Liste Abschnitt 6).

**Stand der A10-Liste des Owners (PLAN.md, A11 „Reihenfolge“), A10e:** ≥ 5 Umdrehungen –
`REVOLUTIONS_BUFFER` = 20 je Aufnahme, Streaming bis 200 Indexe (FEATURES 1): erfüllt, keine
Änderung. Index-/Sektorzeitstempel – Indexzeiten je Umdrehung im Datenstrom und im Provenienzblock
(4.12), Rohindex-Belege (4.1), Sektorzeiten in ANALYZE (`first_sector_us`) und den Sektorbefunden:
vorhanden; SYNC-Impulse laufen seit A10c in denselben Weg. Amiga-ID – AMIGA_ID 0x1A unverändert.
Schrittlimit – SEEK prüft gegen die höchste Spur des Laufwerksprofils (`seek_physical`, Status 3
darüber), PROBE_TRACKS 0x18 ermittelt sie: vorhanden. Spur puffern vor WGATE – `ufi_write_start()`
beginnt erst, wenn `byte_count` Bytes empfangen sind (Status 10 davor); der Streaming-Ring
zwischen Puffer und TIM3 ändert daran nichts: erfüllt. Pin 34 READY/DSKCHG – die Firmware liest Pin
34 weiter über PF3 (READY, Amiga) bzw. PF2 (DSKCHG, Shugart); die Rücklesungen Pin 2/4 (0x47)
liefern die Messung, welche Laufwerke Pin 2/4/34 wie treiben – eine Zuordnungsänderung folgt erst
nach diesen Messungen, nicht erfunden. **Offen, nicht Teil von A10:** Ringpuffer vor dem Index
(die Aufnahme beginnt mit dem ersten Index nach dem Start, `CAPTURE_WAITING_INDEX`; ein
Vorlauf verlangt den Umbau der DMA-Erfassung in `ufi_flux.c`, Owner-Datei mit offenen Änderungen).

### 1.6 LAN-Transport (Platine v1.0, W5500 U23)

**Hardware:** WIZnet W5500 (U23) an SPI3: PC10 SCK, PC11 MISO, PC12 MOSI (AF6), SCSn PA15,
RSTn PD10, INTn PD5 (je 10 k Pull-up auf der Platine). SPI-Modus 0, 25 MHz (PLL1Q 100 MHz / 4),
ein Chip-Select-Rahmen je Registerzugriff (Adresse, Steuerbyte, Daten). Auf Platinen vor v1.0
(`board_rev` < 10) ist kein Chip bestückt; die Firmware fasst die Pins dort nicht an.

**Start (Stufe L1):** `board_gpio_init()` hält den Chip im Reset, `ufi_lan_init()` übernimmt ihn
nach dem Einlesen von UFI.CFG: RSTn ≥ 500 µs low, danach 2 ms für die PLL (Datenblatt,
Reset-Timing), Software-Reset über MR.RST, dann muss VERSIONR 0x04 lesen. Liest er etwas
anderes, bleibt der Chip im Reset (Zustand „kein Chip“). SHAR steht nach jedem Reset auf
00:00:00:00:00:00 und wird deshalb jedes Mal geschrieben und zurückgelesen.

**MAC-Adresse:** `02` (lokal verwaltet, Unicast) gefolgt von den ersten fünf Bytes von SHA-256
über die ganze 96-Bit-UID des STM32 (12 Byte: die Wörter bei `UID_BASE` + 0/4/8, jedes little
endian). Fest je Platine, ohne Herstellerkennung; `mac=` in LAN.CFG überschreibt sie (ab L2).

**Socketaufteilung im W5500** (16 KB senden + 16 KB empfangen): Socket 0 TCP-Sitzung 8/8 KB,
Socket 1 DHCP (UDP 68) 2/2 KB, Socket 2 Discovery 2/2 KB, Socket 3 ARP-Prüfung 1/1 KB, die
übrigen 0. Interrupts der Sockets 0–3 und der IP-Konflikt (IR Bit7) gehen auf INTn. Der
PHY-Zustand (PHYCFGR: Link, 10/100 Mbit, Halb-/Vollduplex) wird alle 500 ms gelesen.

**LAN.CFG (Stufe L2):** Datei auf dem SD-NAND U15 neben UFI.CFG, gleicher Aufbau: eine Zeile
`key=value`, führende Leerzeichen erlaubt, `#` beginnt einen Kommentar (ganze Zeile oder hinter
dem Wert), der Wert endet am Leerzeichen, `#`, CR oder LF. Schlüssel klein geschrieben
(Großschreibung wird ebenfalls angenommen), unbekannte Schlüssel werden übergangen. Gelesen
werden höchstens 1023 Byte beim Start (nach UFI.CFG) und bei CFG_RELOAD (0x54); fehlt die Datei,
schreibt die Firmware sie mit den Vorgaben (CRLF). Ohne Dateisystem gelten die Vorgaben, ohne
dass etwas geschrieben wird. Ein Wert, der sich nicht lesen lässt, lässt die Vorgabe stehen.

| Schlüssel | Vorgabe | Bedeutung |
|---|---|---|
| `lan` | `on` | `off` (oder `0`) hält den W5500 im Reset |
| `ip` | `dhcp` | `dhcp` (mit Link-Local-Rückfall 169.254.x.y) oder feste Adresse `a.b.c.d` (nicht 0.x, 127.x, ab 224.x) |
| `mask`, `gw`, `dns` | `255.255.255.0`, leer, leer | nur für eine feste Adresse; leer = 0.0.0.0, leere Maske = /24 |
| `hostname` | leer | leer = `ufi-` + letzte drei MAC-Bytes hexadezimal klein (z. B. `ufi-02cab3`); sonst Buchstaben, Ziffern und `-` (nicht am Anfang/Ende), höchstens 32 Zeichen, wird klein geschrieben |
| `mac` | leer | leer = aus der UID; sonst `xx:xx:xx:xx:xx:xx` oder mit `-`, keine Multicast-/Null-Adresse |
| `port` | `55100` | TCP-Port der v2-Sitzung, 1–65535 |
| `lan_write` | `0` | `1` erlaubt schreibende Befehle über LAN, und nur bei offenem Schreibschutzschalter (ab L4) |
| `lan_key` | leer | leer = keine Anmeldung; sonst bis 64 Zeichen ohne Leerzeichen und `#` für die HMAC-Abfrage (ab L4). Längere Schlüssel werden nach 64 Zeichen abgeschnitten (die Anmeldung schlägt dann fehl, statt still abgeschaltet zu werden) |

CFG_RELOAD liest LAN.CFG erst, nachdem seine Antwort hinaus ist; hat sich etwas geändert, startet
der W5500 mit den neuen Einstellungen neu (eine bestehende LAN-Sitzung endet dabei).

**IP-Adresse (Stufe L3):** Ohne Link keine Adresse; mit Link je nach `ip=`:

- **DHCP** (RFC 2131/2132, eigener Client auf Socket 1, UDP 68 → 67): DISCOVER, nach 4 s und
  weiteren 8 s wiederholt; REQUEST für das erste Angebot (Optionen 50/54), ACK → die Adresse wird
  vor der Nutzung per ARP geprüft (RFC 2131 3.1 Schritt 5); ist sie belegt: DECLINE, 10 s Pause,
  von vorn. Jede Nachricht trägt das BROADCAST-Flag (bis zur Bindung ist SIPR 0.0.0.0 und der
  W5500 verwirft Unicast an eine fremde Adresse), Client-ID 61 = `01` + MAC, Hostname 12 =
  `hostname` bzw. `ufi-XXXXXX`, Parameterliste 1/3/6/51/58/59. Übernommen werden Maske (ohne
  Option /24), erster Router, erster DNS-Server, Lease, T1, T2 (ohne Angabe Lease/2 und 7/8).
  Ab T1 REQUEST per Unicast an den Server, ab T2 per Broadcast (alle 10 s wiederholt), mit
  Ablauf der Lease ist die Adresse weg und es beginnt ein neues DISCOVER; NAK ebenso. Leases über
  20 Tage werden wie 20 Tage behandelt (32-Bit-Millisekundenzeit), unendliche nie erneuert.
- **Link-Local-Rückfall** (RFC 3927): kommt nach dem dritten DISCOVER (20 s) kein Angebot,
  wählt die Firmware eine Adresse aus 169.254.1.0–169.254.254.255 (Zufallsgenerator mit der MAC
  als Startwert: dieselbe Platine probiert nach einem Neustart zuerst dieselbe Adresse), wartet
  0–1 s und prüft sie per ARP; belegt → nächste Adresse, nach 10 Konflikten nur noch eine Probe
  je Minute. Maske 255.255.0.0, kein Gateway. Meldet der Chip später einen Konflikt (IR Bit7),
  wird die Adresse aufgegeben und eine neue geprüft. Solange keine LAN-Sitzung verbunden ist,
  versucht die Firmware alle 60 s wieder DHCP und wechselt bei Erfolg auf die DHCP-Adresse.
  ARP-Ankündigungen (Gratuitous ARP) sendet der W5500 außerhalb von MACRAW nicht; Gegenstellen
  lösen die Adresse beim ersten Kontakt selbst auf.
- **Feste Adresse** `ip=a.b.c.d` mit `mask`, `gw`, `dns`: sofort gesetzt, kein DHCP, keine Probe;
  ein gemeldeter Konflikt wird nur angezeigt.

**ARP-Probe ohne MACRAW:** Socket 3 sendet ein UDP-Byte an den Discard-Port (9) des Kandidaten,
während SIPR 0.0.0.0, SUBR 0.0.0.0 und GAR = Kandidat gesetzt sind. Der W5500 löst das Ziel vorher
per ARP auf, und diese Anfrage trägt die Absenderadresse 0.0.0.0 – genau die Probe aus RFC 3927.
Mit Maske 0 ist jedes Ziel lokal, GAR = Kandidat deckt auch den gerouteten Weg ab. Antwort
(SEND_OK) = Adresse belegt, keine Antwort innerhalb RTR × (RCR + 1) (TIMEOUT) = frei.
Link-Local-Probe mit RTR 1,5 s und RCR 2 (drei Anfragen im Abstand 1,5 s), DHCP-Prüfung mit
0,5 s und RCR 1; danach wieder RTR 200 ms, RCR 8 (Vorgaben des Chips). Gewählt statt MACRAW, weil
dafür weder Ethernet-Rahmen gebaut noch ein weiterer Puffer belegt werden muss; dass der Chip die
Anfrage wirklich mit Absender 0.0.0.0 sendet, ist nur mit einem Mitschnitt am Gerät zu bestätigen.

## 2 Rahmen, Zeitbasis und Ereignisse (UFI v2)

### 2.1 Rahmen (beide Richtungen)

```
Offset 0  0x55
       1  0xAA
       2  type     1 = Anfrage (Host)   2 = Antwort   3 = Ereignis   4 = Daten   5 = Ende
       3  cmd      Befehlscode (Antwort/Daten/Ende tragen den Code der Anfrage)
       4  seq      Laufnummer: Antwort/Daten/Ende wiederholen die seq der Anfrage; Ereignisse zählen selbst
       5  status   Anfrage: 0; sonst Statuscode (3.2)
       6  len      u16, Länge der Nutzdaten (0…4096)
       8  payload  len Byte
   8+len  crc      u16, CRC-16/CCITT-FALSE (Polynom 0x1021, Start 0xFFFF) über Byte 2 … 7+len
```

- Rahmen sind unabhängig von USB-Paketgrenzen. Der Empfänger sucht `55 AA`, liest den Kopf und prüft die CRC; bei Fehler verwirft er bis zum nächsten `55 AA`.
- Fehlerhafte Anfrage (CRC, Länge): Antwort `type=2, cmd=0xFF, seq=0, status=14`.
- Pro Anfrage genau **eine Antwort**. Lang laufende Befehle (Lesen, Schreiben) antworten sofort mit dem Annahmestatus und schließen mit einem **Ende-Rahmen** (type 5) ab; dazwischen kommen **Daten-Rahmen** (type 4).
- Flusskontrolle: Das Gerät hält den USB-Endpunkt an, wenn sein Empfangspuffer voll ist (NAK) – der Host schreibt einfach weiter.
- Ereignisse (type 3) kommen nur nach `EVENTS` und nie mitten in einem Rahmen.

### 2.2 Flusscode (READ-Daten, WRITE-Daten), 275 MHz
Gleich dem v1-Stream (`ufi_stream.c`): Zeit 0 = erster Indeximpuls (bzw. Start ohne Index).

| Bytes | Bedeutung |
|---|---|
| `01…EF` | Flusswechsel, Abstand = Wert (Ticks à 3,64 ns) |
| `F0…FC b` | Abstand = 240 + ((Wert − 0xF0) << 8) + b |
| `FD u32` | Indeximpuls, u32 = Ticks nach dem letzten Wechsel (nur Lesen) |
| `FE u32` | Abstand = u32 |
| `00`, `FF` | reserviert |

Lesen endet nach `revs` Indeximpulsen (bzw. Zeitabschnitten); der Ende-Rahmen meldet die Zahl der gesendeten vollen Umdrehungen. Ohne Index (Apple ohne Sync-Sensor, `flags` Bit0) markieren `FD`-Einträge die Abschnittsgrenzen (`period_ms`, 0 = 200 ms).

**Zählregel der Indexmarken (im Code geprüft, `ufi_stream.c`/`ufi_flux.c`, 1.18-dev):**

- Zeit 0 ist der **erste** Indeximpuls (ohne Index: der Aufnahmestart). Erst dieser Impuls schaltet
  die RDATA-Aufnahme ein; Flusswechsel davor gibt es im Strom nicht.
- Der Strom beginnt **ohne** `FD`: der erste Impuls selbst wird nicht als Marke gesendet, der erste
  Abstand zählt ab ihm.
- Jede Umdrehung wird von genau **einem** `FD` abgeschlossen: Umdrehung k (k = 1 … N) umfasst die
  Flusswechsel nach dem (k−1)-ten und vor dem k-ten `FD`. Ein Wechsel genau auf dem Indexzeitpunkt
  gehört zur folgenden Umdrehung.
- Für **N gesendete Umdrehungen stehen genau N `FD`-Marken** im Strom, die letzte ist das letzte
  Element; danach folgen keine Flusswechsel mehr. N ist der Wert `revs` im Ende-Rahmen, bei
  Status 0 gleich dem angeforderten (geklemmten) Wert, bei Status 10 (Speicher voll / Host zu
  langsam) kleiner – die gesendeten N Umdrehungen sind vollständig.
- Abweichung im Fehlerfall: Bei Status 6 (DMA/Überlauf), 3 (kein Index) oder 21 (Medium gewechselt)
  meldet der Ende-Rahmen `revs` = 0, obwohl bereits gesendete Daten-Rahmen schon einzelne `FD`-Marken
  und Flusswechsel enthalten können; die Firmware verwirft dann nur den noch nicht gesendeten Rest.
  Diese Teildaten sind keine gültige Aufnahme; wo sie noch intakt waren, sagt der Lesekontext (4.24).
- Die Ereignisse 0x85 (Bit4) zählen dieselben Marken: `revolution` = k nach dem k-ten `FD`.
- Greaseweazle-Modus (10.3) zählt anders: dort wird auch der erste Impuls als Index gesendet.

**Apple mit Sync-Sensor** (J19, Hall-Sensor nach Applesauce-Art, `UFI.CFG apple_sync=1`, caps Bit7): Der Sensor liegt bis Platine v0.7 auf der INDEX-Leitung, ab v1.0 (A11d/A10c) auf dem eigenen Eingang SYNC (PC6, TIM8_CH1, Op 0x46 in Abschnitt 1.5) – für READ/WRITE ändert sich dadurch nichts; READ liefert dann echte Umdrehungen ab dem Sensorimpuls (`FD` = Sensorimpuls), WRITE beginnt am Sensorimpuls – Spuren bleiben zueinander ausgerichtet. Ohne `apple_sync=1` arbeitet das Apple-Laufwerk mit Zeitabschnitten, auch wenn ein Sensor steckt.

### 2.3 Ereignisse (type 3, nach EVENTS)

| cmd | Ereignis | Nutzdaten |
|---|---|---|
| 0x80 | Diskette gewechselt | `drive` u8 |
| 0x81 | Taster | `button` u8 (0 A, 1 B), `long` u8 |
| 0x82 | Dump/Kopie-Fortschritt | wie DUMP_STATUS |
| 0x83 | Laufwerksversorgung abgeschaltet (Überstrom) | `board_status` |
| 0x84 | Betriebsart-Schalter bewegt | neuer `mode` u8 (vor der Neuanmeldung) |
| 0x85 | Lesefortschritt (1.18-dev; nur mit READ/GUARDED_READ Flags Bit4 und Maskenbit 5) | `revolution` u8 (1 = erste abgeschlossene Umdrehung), `index_ticks` u32 (Dauer dieser Umdrehung in 275-MHz-Takten), `elapsed_ms` u32 seit Beginn der Übertragung; ein Rahmen je Umdrehung zwischen den Daten-Rahmen, der Ende-Rahmen folgt wie bisher |

### 2.4 Zeitbasis und erweiterter Schreibpfad

TIM2 wird per Überlaufinterrupt und atomarem Auslesen auf 64 Bit erweitert.
GW-ticks/linger werden vor der Multiplikation mit vier auf 64 Bit erweitert.
Lange stille Leseabschnitte erzeugen SPACE-Einträge ohne erfundene Fluxflanken.
GW-pre/post verwendet native Timer-Mikrosekunden statt Millisekundenrundung;
die Verzögerung ist kooperativ abbrechbar, aber weiterhin eine Warteschleife.

GW verwendet den neuen Schreibpfad automatisch. Native WRITE verwendet ihn mit
Flag Bit5 (`20` hex): 220…4294967295 Ticks, mindestens drei Fluxintervalle,
Speichergrenze laut DESCRIBE und rund 58 s Gesamtdauer. Der Host muss DESCRIBE-Flag1
prüfen. WRITE_LIMITS beschreibt weiterhin den klassischen Schreibpfad.

PWM2 im Aufwärtszählen lädt ARR und CCR1 per DMA-Burst. Stille Segmente haben
CCR1 größer als ARR; ein zusätzliches stilles Endsegment verhindert die unmittelbare
Wiederholung des letzten Pulses. Der erste Abstand berücksichtigt die Pulsbreite.
Die Berechnung folgt dem Timer-DMA-Prinzip aus
[ST AN4776](https://www.st.com/resource/en/application_note/an4776-generalpurpose-timer-cookbook-for-stm32-microcontrollers-stmicroelectronics.pdf).
Die TIM3-spezifische Burstfolge einschließlich reservierter Zwischenposition,
Buslast, Jitter und Ausgangspolarität benötigen noch die Prüfung am STM32H723.

Der erweiterte native Pfad nimmt **fertige Host-Zeitabstände**: keine Firmware-
Präkompensation, Skalierung, Kappung oder Apple-Viertelspuren.
Entsprechende Flags werden abgewiesen. Hartsektor-Option bleibt verfügbar.
Mit INFO-Bit21 unterstützt der Pfad Firmware-Prüflesen (WRITE-Bit0, Abschnitt 5.2).
Alternativ kann das Tool anschließend separat lesen und vergleichen. Der Host automatisiert
dies mit `--extended --verify` für IBM-MFM und vollständige Amiga-DD/HD-Spuren (siehe unten). `--extended`
aktiviert diesen Pfad; `--integrity` ergänzt unabhängig davon die Transportprüfung.

### 2.5 Abbruch, Zeitlimits und Wiederverbindung

Native EP0-Anfragen sind OUT/Vendor/Interface (`bmRequestType=41`), `wIndex=2`,
`wValue=0`, `wLength=0`: `bRequest=21` setzt nur den nativen Kanal zurück,
`bRequest=22` fordert Abbruch an, sofern Native Besitzer ist. `BulkStream.cancel()`
verwendet 22. CDC-ClearComms bleibt die GW-Konvention mit 10000 Baud und darf einen
nativen Besitzer nicht abbrechen. Nach GW-Upload-Timeout wird bis ClearComms verworfen.
USB-Disconnect löst Abbruch und anschließendes Aufräumen im Hauptprogramm aus.
Composite-Antworten werden in zwei eigene Puffer kopiert und ohne blockierendes
Warten versendet. DATA-Streams und Ereignisse warten hinter diesen Antworten;
der Parser begrenzt die Anzahl ausstehender Antworten. Ein nativer IN-Transfer
ohne Abschluss nach 1 s sperrt den Transport bis zum Kanal-Reset. Dabei wird
nur Endpoint 83 neu gestartet. Nach EP0-Reset 21 muss der Host zusätzlich
CLEAR_FEATURE(ENDPOINT_HALT) für 83 ausführen, damit beide DATA-Toggles wieder
übereinstimmen (`BulkStream.reset_input_buffer()` erledigt dies). GW-Endpoints
werden dabei nicht zurückgesetzt. Reset/Bootloader/Moduswechsel verwenden noch
einen kurzen, begrenzten Flush. Der eigenständige UFI-CDC-Modus bleibt synchron.

IEC_RESET 30 im v2-Protokoll antwortet nach Abschluss eines Zustandsautomaten:
20 ms RESET aktiv, dann mindestens 500 ms Anlaufzeit. Der Hauptablauf läuft dabei
weiter. ABORT/EP0-CANCEL oder Disconnect geben RESET und die IEC-Leitungen frei;
Abbruch liefert Status 20. Weitere Laufwerksaktionen bleiben währenddessen BUSY.
IEC-Byteübertragungen bleiben zeitkritisch synchron. Die 200-/50-ms-Wartephasen
bei der Bereitschaftsprüfung sind jetzt watchdog-versorgt und abbrechbar.

Native offene Jobs/Uploads laufen nach 10 s ohne Fortschritt ab. Aktives Lesen/
Schreiben aktualisiert den Job; der Schreibpfad hat ein eigenes, aus der Dauer
berechnetes Limit bis 60 s. Das Index-Warten bleibt begrenzt. GW-Upload/SINK nutzt
den konfigurierten Watchdog; 0 deaktiviert ihn entsprechend der GW-Einstellung.
Status 20 bedeutet Abbruch über den neuen Cancel-Pfad. Ältere ABORT-/Timeout-Pfade
können weiterhin Status 4 verwenden. Fortschritt: JOB-Zähler, bestehende Events
und WRITE-Status. Die Firmware ist kein vollständig asynchroner Scheduler:
IEC- und Mikrosekunden-Warteschleifen prüfen den Abbruch kooperativ, einzelne
HAL-/Kopfpositionierungsaufrufe bleiben synchron und begrenzen die Reaktionszeit.

### 2.6 Integritätsjobs: JOB 0B

Diese Erweiterung wird explizit aktiviert. Alte READ-/WRITE-Transaktionen ohne
JOB bleiben unverändert. Es gibt jeweils einen flüchtigen Job, keine Warteschlange.
Alle Byteoffsets und Prüfsummen beziehen sich auf den kodierten Flux-Bytestrom,
ausschließlich der Jobköpfe; Indexcodes bei READ gehören zum Bytestrom.

| Request-Payload | Aktion |
|---|---|
| leer | letzten Jobstatus abfragen |
| `01, kind u8, id u64` | öffnen; kind 1 READ, 2 WRITE; id ungleich 0 |
| `02, crc32 u32` | vollständig hochgeladenen WRITE freigeben |

Antwort immer bei Erfolg: `schema u8=1, kind u8, state u8, status u8, id u64,
packets u32, bytes u32, crc32 u32` (24 Byte). Zustände: 0 IDLE, 1 OPEN,
2 ACTIVE (Datenaustausch), 3 READY (Commit akzeptiert/Schreiben), 4 DONE, 5 FAILED.
CRC-32 ist die reflektierte Standard-CRC mit Polynom EDB88320, initial/final XOR
FFFFFFFF, wie `zlib.crc32`; für `123456789` ergibt sich CBF43926.

Nach JOB OPEN folgt der normale READ- beziehungsweise WRITE-Request. Jede DATA-
Payload beginnt nun mit `id u64, packet_no u32, byte_offset u32` (16 Byte), danach
höchstens 4080 Datenbytes. Nummer und Offset beginnen bei 0. Falsche ID, Duplikate,
Lücken und Zählerüberlauf werden abgewiesen. Die bestehende Rahmen-CRC16 bleibt.

Bei WRITE zählt `byte_count` nur die Fluxdaten. Vor COMMIT beginnt kein physisches
Schreiben. COMMIT verlangt die vollständig empfangenen Daten und passende CRC32.
Ein erfolgreiches END enthält vor den bisherigen Ergebnisbytes den 20-Byte-Nachweis
`id u64, packets u32, bytes u32, crc32 u32`. Fehler-END darf ohne Nachweis kommen;
sein Fehlerstatus hat Vorrang. Der Host prüft den Nachweis auch bei READ.
Ein wiederholtes COMMIT startet keinen zweiten Schreibvorgang. Nach verlorenem END
kann JOB abgefragt werden. Nach Neustart sind Status und Job weg; es gibt keine
persistente Exactly-once-Garantie und keine Wiederaufnahme eines Flux-Schreibjobs.

`ufi --integrity ...` aktiviert dies für Flux-READ/WRITE; `ufi job-status` fragt ab.
Der flüchtige Jobstatus beschreibt Übertragung und Abschluss, keinen dauerhaften
Schreibbeweis. Ergebnisqualität bleibt `verify_level` aus Abschnitt 3.3.

## 3 Befehle und Statuscodes (UFI v2)

### 3.1 Befehlstabelle

Laufwerksnummern (`drive`): 0 keins, 1 A, 2 B (PC-Kabel mit Twist), 3 apple (J14), 4 amiga (J7 DF1), 5 iec, 6–9 ds0–ds3 (Shugart), 10 amiga2 (J7 DF2, JP3), 11 apple2 (J15). Spuren immer physisch.

| Code | Name | Anfrage | Antwort-Nutzdaten |
|---|---|---|---|
| **System** | | | |
| 0x00 | PING | beliebig | dieselben Bytes |
| 0x01 | INFO | – | `proto` u8 = 2, `fw_major` u8, `fw_minor` u8, `board_rev` u8 (5/6/7 = Platine v0.5/v0.6/v0.7, 10 = v1.0 mit Teiler 10k/22k = 2269 mV, 0 = unbekannt), `caps` u32, `sample_hz` u32 = 275 000 000, `store_bytes` u32, `max_payload` u16 = 4096, dann ASCII-Text (Firmware, Git-Rev., Datum, PSRAM-Test) |
| 0x02 | STATUS | – | `drive, motor, wprot, track0, disk_changed, ready, track, side` (je u8), dann `board_status` (8 Byte, wie v1 0x1B) |
| 0x03 | RESET | – | Antwort, danach Neustart |
| 0x04 | BOOTLOADER | – | Antwort, danach ROM-DFU |
| 0x05 | USB_MODE | `mode` u8 (0 Flux, 1 SD-Laufwerk, 2 USB-Floppy, 3 Greaseweazle, 4 Composite) | Antwort, danach Neuanmeldung |
| 0x06 | EVENTS | `mask` u32 (Bits = Ereigniscodes − 0x80) | – |
| 0x08 | WRITE_LIMITS (1.15) | – | `schema` u8 = 1, `flags` u8 (Bit0 Inhaltsprüfung, Bit1 lange Spuren per Streaming, Bit2 Kappen per Flag, Bit3 `flags2`-Byte mit Spurrest löschen, Bit4 Startversatz `flags2` Bit1, Bit5 Quell-Indexdauer Bit6 auch ohne Prüflesen, Bit6 Präkompensation je Auftrag `flags2` Bit2, Bit7 Weak-Marker `flags2` Bit3 (4.16), 1.18), `sample_hz` u32, `max_flux` u32 (Wörter des Flussspeichers), `min_ticks` u32 (440), `max_ticks` u32 (65 536) |
| 0x09 | DESCRIBE (1.18) | – | 36 Byte, Schema 1; siehe unten |
| 0x0A | RESOURCE (1.18) | leer, 0 freigeben oder 1 beanspruchen | Besitzer u8; Abschnitt 1.4 |
| 0x0B | JOB (1.18) | Status / Öffnen / Commit | 24 Byte; Abschnitt 2.6 |
| 0x0C | FEATURES (1.18) | leer | 82 Byte; Abschnitt 1.4 |
| 0x0D | DIAGNOSTICS (1.18) | leer; `01` Prozessmesswerte; `02` Zyklen-/Verlustdiagnose | 64 Byte, Schema 1; Abschnitt 5.1 |
| 0x0E | QUALITY (1.18) | leer | zuletzt erzeugter Qualitätsbericht (1824 / 3744 / 4048 Byte je nach Flags), Status 1 ohne Bericht; Abschnitt 5.2 |
| 0x0F | ANALYSIS_JOB (1.18) | `op` u8 (Unterfunktion 0–46), danach je Op | je Op; Op-Tabelle in 6.1, Einzelheiten in den Kapiteln 4 bis 7 |
| 0x07 | SELFTEST (1.14) | – | `n` u8, dann n × (`id` u8, `status` u8: 0 ok, 1 Fehler, 2 nicht prüfbar, 3 Warnung, `value` u32) |
| **Laufwerk** | | | |
| 0x10 | SELECT | `drive` u8 | – |
| 0x11 | MOTOR | `on` u8 | – (wartet die Anlaufzeit ab) |
| 0x12 | SEEK | `track` u8 [, `quarter` u8 0–3 (Apple-Viertelspur, 1.14; ab 1.18 auch Shugart-Halbspur: `quarter` 2 nur bei Doppelschritt-Profil, physische Spur 2·`track`+1, sonst 16)] | – |
| 0x13 | RECAL | – | – |
| 0x14 | SIDE | `side` u8 | – |
| 0x15 | TIMING | – oder bis 11 × u16 (wie v1 0x17; nur die gesendeten Felder werden gesetzt) | aktuelle 11 × u16 (ab 1.16; zuvor 10) |
| 0x16 | LINES | `density` u8, `drate` u8 | – |
| 0x17 | CHECK_DISK | – | `changed` u8, `present` u8 |
| 0x18 | PROBE_TRACKS | – | höchste erreichbare Spur u8 |
| 0x19 | SEEK_TEST | `a` u8, `b` u8, `cycles` u8 | – |
| 0x1A | AMIGA_ID | – | ID u32 |
| 0x1B | DIAG_RPM (1.13) | `revs` u8 (0 = 5, max 50) | `revs` u8, `period_min` u32, `period_avg` u32, `period_max` u32, `pulse` u32 (Ticks 275 MHz), `rpm_x100` u16 |
| 0x1C | DRIVE_SCAN (1.13) | `flags` u8 (Bit0 = Shugart-Bus DS0–DS3 statt a/b/amiga/amiga2) | 4 × (`drive` u8, `found` u8: Bit0 TRK0 gefunden, Bit1 Indeximpulse, Bit2 schreibgeschützt) |
| 0x1D | INDEX_SIM (1.13) | – (Abfrage) oder `rpm` u16 (0 = aus, 200–400), `pulse_us` u16 (0 = 2000), `mode` u8 (1.14: Bit0 Ausgang INDEX_SIM (bis v0.7 J9 Pin 6, ab v1.0 PE5 auf den INDEX-Bus), Bit1 intern; 0 = Bit0) | `rpm` u16, `mode` u8 (1.14) aktueller Zustand |
| 0x1E | SET_RPM (1.14) | – (Abfrage) oder `rpm` u16 (300/360, 0 = Abfrage) | `rpm` u16 (0 = nicht gesetzt), `line` u8 (0 keine, 1 DENSITY, 2 DRATE) |
| 0x26 | FORMAT (1.14) | `track, side` u8, `flags` u8 (Bit0 Prüflesen, Bit1 Amiga, Bit2 1581-Seitentausch), `rate_kbps` u16 (250/300/500/1000), `spt` u8 (Amiga 11, HD 22 ab 1.16), `gap3` u8 (0 = nach Layout), `fill` u8 | `flux_count` u32, `good` u8 (Sektoren mit gültiger CRC beim Prüflesen, 0xFF ohne) |
| 0x25 | ANALYZE (1.14) | `track, side` u8 (`side` ab 1.18 mit Bit 7 = Viertelspurmodus und Viertel in Bits 4–5, Apple; 5.2), `revs` u8 (0 = 3, max 8) | `revs` u8, `encoding` u8 (0 ?, 1 FM, 2 MFM, 3 GCR), `bitcell_ns` u16, `rpm_x100` u16, `peak_ns` 4 × u16, `index_ticks` u32, `transitions` u32, `bitcells` u32, `sectors` u8, `weak_count` u8, `first_sector_us` u32, `weak_mask` u64 (40 Byte) |
| 0x1F | STEP_SCHEDULE (1.14) | `flags` u8 (Bit0 Positionen in Viertelspuren), `n` u8 (≤ 32), n × (`at_ms` u16, `pos` u8) | – |
| **Fluss** | | | |
| 0x20 | READ | `track, side, revs` u8, `flags` u8 (Bit0 ohne Index, Bit1 hartsektoriert, Bit2 `track` in Viertelspuren (Apple 0–3; Shugart ab 1.18 nur Halbspur 2 bei Doppelschritt-Profil), Bit3 Provenienzblock im Ende-Rahmen, Bit4 Fortschrittsereignis 0x85 je Umdrehung, 1.18, Bit5 Lesekontext im Ende-Rahmen, 1.18-dev), `period_ms` u16, bei Bit1 `sectors` u8 (1.14) | Annahme; dann Daten-Rahmen, Ende-Rahmen mit `revs` u8, ab 1.18 gefolgt vom Generationsblock (17 Byte, 4.5), mit Bit3 vom Provenienzblock (96 Byte, 4.12) und mit Bit5 zuletzt vom Lesekontext (132 Byte, 4.24) |
| 0x27 | GUARDED_WRITE | `config_generation, media_generation` je u32, danach vollständige WRITE-Nutzlast (ohne Kommandobyte) | Antwort 0x27; DATA und END weiterhin 0x22, gleiche Sequenz; siehe 4.5 |
| 0x28 | GUARDED_READ (1.18) | `config_generation, media_generation` je u32, danach vollständige READ-Nutzlast (ohne Kommandobyte) | Antwort 0x28 (16 veraltete Konfiguration, 21 anderes/kein Medium, keine Daten vor der Annahme); DATA und END weiterhin 0x20, gleiche Sequenz; siehe 4.5 |
| 0x21 | ABORT | – | – (bricht Lesen/Schreiben ab; der laufende Vorgang endet mit Ende-Rahmen Status 4) |
| 0x22 | WRITE | `track, side` u8, `flags` u8 (Bit0 Prüflesen, Bit1 hartsektoriert, Bit2 `track` in Viertelspuren, Bit3 Zeitskala, Bit4 Kappen erlaubt, Bit5 erweiterter Schreibpfad, Bit6 Quell-Indexdauer (nur mit Bit5; für Prüflesen und Drehzahlprüfung 4.10), Bit7 erwartete Sektormenge EXPECT_SET; Bit2 wie bei READ: Apple 0–3, Shugart ab 1.18 nur Halbspur 2 bei Doppelschritt-Profil), `flux_count` u32, `byte_count` u32, dann in dieser Reihenfolge: bei Bit1 `sectors` u8, bei Bit3 `scale` u16 (1/1000, 500–2000), bei Bit6 `index_ticks` u32, bei Bit7 `expected` 12 Byte (Abschnitt 5.2), zuletzt optional `flags2` u8 (1.18: Bit0 Spurrest bis zum Index löschen, Bit1 `start_offset_ticks` u32 folgt, Bit2 `precomp_ns` u16 folgt (0 = aus, 1–2000, nur klassischer Pfad, 4.14), Bit3 Weak-Marker: `weak_jitter_ticks` u16 und `weak_seed` u32 folgen (nur erweiterter Pfad, nicht mit Bit0, 4.16), Bit4 Rohprüfung: Fingerabdruck-Rücklesen ohne Inhaltsreferenz (verlangt Bit6, nicht mit Bit0, 4.17); andere Bits BAD_ARGS; angekündigt durch WRITE_LIMITS Bit3/Bit4/Bit6/Bit7) | Annahme; dann sendet der Host Daten-Rahmen (type 4, cmd 0x22) mit zusammen `byte_count` Byte; Ende-Rahmen nach dem Schreiben mit `verify_level` u8, `clamped` u32 (1.15), ab 1.18 gefolgt vom Generationsblock (17 Byte, 4.5), `erased_ticks` u32, `start_offset_applied` u32, `weak_applied` u32 und `weak_seed` u32 (38 Byte; 30 vor 4.16) |
| 0x23 | ERASE | `track, side` u8 | – (eine Umdrehung) |
| 0x24 | PATTERN | `track, side` u8, `interval_ns` u16, `duration_ms` u16 | – |
| **Commodore IEC** | | | |
| 0x30 | IEC_RESET | – | – |
| 0x31 | IEC_SEND | `byte` u8, `eoi` u8 | – |
| 0x32 | IEC_RECV | – | `byte` u8, `eoi` u8 |
| 0x33 | IEC_MEM (1.14) | `op` u8 (0 M-W, 1 M-R, 2 M-E, 3 DOS-Befehl, 4 DOS-Status ab 1.16), `addr` u16, dann Daten (M-W ≤ 32 Byte, DOS-Befehl: Text) bzw. `n` u8 (M-R) | M-R: `n` Byte; op 4: Statuszeile von Kanal 15 (ASCII, ≤ 64 Byte) |
| 0x35 | IEC_BLOCK (1.16) | `op` u8 (0 lesen U1, 1 schreiben U2), `track` u8, `sector` u8, `device` u8 (0 = `iec_device`), bei op 1 dann 256 Datenbytes | op 0: 256 Byte; op 1: `dos` u8 (DOS-Fehlercode nach dem Schreiben, 0 = ok) |
| 0x34 | IEC_NIB (1.14) | `halftrack` u8 (2–84), `sync` u8, `mode` u8 (0 Stück ab Sync, 1 roh, 2 nur positionieren), `flags` u8 (Bit0 Laufwerkscode neu laden, Bit1 Stepper-Richtung umgekehrt), `device` u8 (0 = `iec_device`) | `status` u8 (0 ok, 1 kein Sync/keine Bytes, 2 Ursprung nicht gefunden, 3 roh), `synclen` u8 (≈ 11 µs), `maxlen` u16, `halftrack` u8, 512 Rohbytes |
| 0x36 | IEC_TARGET (1.18) | `schema` u8 = 1, `device` u8 (0 = `iec_device`, sonst 8–30), dann die IEC_MEM-Nutzlast (`op`, `addr`, Daten) | wie IEC_MEM; siehe „Ergänzungen ab Firmware 1.18-dev“ (3.3) |
| 0x37 | IEC_READ_STATUS (1.18) | `schema` u8 = 1, `device`, `track`, `sector` u8 | `schema` u8 = 1, `dos` u8, `track`, `sector` u8, dann 256 Datenbytes nur bei `dos` = 0 |
| **Board** | | | |
| 0x40 | POWER | – oder `mask` u8 (Bit0 FDD_5V, Bit1 FDD_12V) | `board_status` 8 Byte |
| 0x41 | USB_POWER | – | `cc1_mv` u16, `cc2_mv` u16, `current_ma` u16 |
| 0x42 | SD_INFO | – | `present, status, card_type, bus_width` u8, `capacity_mb` u32 |
| 0x43 | RTC (A10a, Platine v1.0) | – (lesen) oder `year` u16, `mon, day, hour, min, sec` je u8 (stellen; Kalender 2000–2099, Monatslängen und Schaltjahre werden geprüft, sonst Status 16) | `ready` u8 (1 = Kalender läuft; 0 = keine Uhr: Platine vor v1.0 oder LSE schwingt nicht, Felder dann 0, Stellen antwortet mit Status 11), `year` u16, `mon, day, hour, min, sec` je u8 (8 Byte); Abschnitt 1.5 |
| 0x44 | SENSOR (A10b) | – | `status` u8 (0 ok, 1 kein Sensor/NACK/Timeout, 2 CRC-Fehler, 3 I²C nicht initialisiert), `temp` i16 in 0,01 °C, `rh` u16 in 0,01 %RH (5 Byte); SHT40 U25 an I²C1, Abschnitt 1.5 |
| 0x46 | SYNC (A10c, Platine v1.0) | – (Abfrage) oder `on` u8 (1 = Zählung starten, 0 = anhalten; 11 vor v1.0, 1 = busy solange SYNC die Indexquelle ist) | `active` u8, `source` u8 (1 = SYNC ersetzt INDEX), `level` u8, `count` u32, `last_t2` u32 (TIM2-Zeitbasis), `last_t32` u32 (TIM8 mit Überlaufzähler), `period` u32 (19 Byte); Abschnitt 1.5 |
| 0x45 | RAILS (A10d, Platine v1.0) | – | `v5_mv` u16, `v12_mv` u16 (FDD_5V/FDD_12V am Laufwerksstecker über die Teiler 10 k/10 k und 33 k/10 k), `flags` u8 (Bit0 5 V unter 4,5 V, Bit1 12 V unter 10,8 V – jeweils nur bei eingeschalteter Schiene, Bit2/3 Schiene 5 V/12 V ein, Bit4 Teiler vorhanden; ohne Bit4 (v0.x) sind die Spannungen 0) (5 Byte); Abschnitt 1.5 |
| 0x47 | FDD_PINS (A10e, Platine v1.0) | – (lesen) oder `pin4` u8, `pin33` u8 (je 0 = aus, 1 = an, 0xFF = unverändert; setzt zuerst die Ausgänge J6 Pin 4 (Q12) und Pin 33 (Flippy-Signal), 11 vor v1.0) | `pins` u8 (Bit0 J6 Pin 2 low, Bit1 Pin 4 low, Bit2 J23 !CSTOUT aktiv, Bit3 WGATE-Bus aktiv, Bit4 WLOCK_HW gesperrt, Bit5 FLT5, Bit6 FLT12, Bit7 SYNC high), `outputs` u8 (Bit0 Pin 4 angezogen, Bit1 Pin 33 high) (2 Byte); Abschnitt 1.5 |
| **Dateien (SD-NAND, FAT32)** | | | |
| 0x50 | DIR | `start` u16, Pfad (ASCII, leer = Wurzel) | `count` u8, `more` u8, dann je Eintrag `size` u32, `attr` u8 (0x10 = Ordner), `nlen` u8, Name |
| 0x51 | READ_FILE | `offset` u32, `len` u16 (≤ 4096), Name | Dateiinhalt (kürzer am Dateiende) |
| 0x52 | WRITE_FILE | `offset` u32, `flags` u8 (Bit0 neu anlegen/kürzen), `nlen` u8, Name, Daten | geschriebene Bytes u16 |
| 0x53 | DELETE | Name | – |
| 0x54 | CFG_RELOAD | – | – (UFI.CFG neu einlesen; ab L2 auch LAN.CFG, angewandt nach der Antwort, Abschnitt 1.6) |
| **Ohne PC** | | | |
| 0x60 | DUMP_START | – oder `drive, tracks, sides, revs` u8 [, `format` u8 (1.16: 0 SCP, 1 IMG, 2 ADF; 1.17: 3 HFE; 4 HFEv3)] | – |
| 0x61 | DUMP_STATUS | – | `state, error, track, side` u8, `file_no` u16, `fs_result` u8, `storage_ready` u8 (`state` 10–12 = Schreiben von SD; `error` 1 Speicher, 2 Laufwerk, 3 voll/Schreibfehler, 4 abgebrochen, 5 Format/Abbild nicht erkannt, 6 schreibgeschützt, 7 Datei fehlt) |
| 0x62 | DUMP_ABORT | – | – |
| 0x63 | COPY_START | – | – |
| 0x64 | WRITE_START (1.16) | Dateiname (ASCII 8.3, ≤ 63; leer = `UFI.CFG write_file`) | – (Fortschritt über DUMP_STATUS, Zustand 10–12) |

**DESCRIBE (0x09, 1.18):** leere Anfrage; andere Längen ergeben Status 16. Reine Abfrage, auch während eines laufenden Auftrags. Mehrbytefelder little endian:

| Offset | Typ | Bedeutung |
|---|---|---|
| 0 | u8 | Schema = 1 |
| 1 | u8 | Transportmaske: Bit0 UFI-CDC, Bit1 GW-CDC, Bit2 Composite verfügbar; aktuell 7 |
| 2 | u16 | Flags: Bit0 Composite aktiv, Bit1 erweiterter Schreibpfad, Bit2 Jobs, Bit3 Ressourcenverwaltung |
| 4 | u32 | gleiche Capability-Maske wie INFO |
| 8 | u32 | nativer Takt, 275000000 Hz |
| 12 | u32 | GW-Takt, 68750000 Hz |
| 16 | u32 | aktuell nutzbare Flux-Speicherwörter |
| 20 | u16 | maximale UFI-Nutzdatenlänge |
| 22 | u16 | maximale GW-Indexanzahl, 200 |
| 24 | u32 | maximaler einzelner GW-ticks/linger-Parameter, 0xFFFFFFFF |
| 28 | u32 | minimale GW-Schreibintervalllänge, 55 |
| 32 | u32 | maximale GW-Schreibintervalllänge, 1073741823 |

Host: `Device2.describe()` beziehungsweise `ufi describe`. Unbekannte Schemata werden abgewiesen; zusätzliche Felder hinter Schema 1 dürfen ignoriert werden. Ältere Firmware kündigt Bit16 nicht an und unterstützt 0x09 nicht. Capabilities sind keine Bestätigung elektrischer Hardwaretests.

**caps (INFO):** Bit0 PSRAM ok, Bit1 SD-NAND bereit, Bit2 IEC, Bit3 Amiga, Bit4 Amiga2/JP3-fähig (v0.7), Bit5 Shugart-Bus, Bit6 Apple-Port (v0.7), Bit7 Sync-Sensor aktiviert (`apple_sync=1`), Bit8 Greaseweazle-Modus verfügbar, Bit9 Dateizugriff, Bit10 Ereignisse, Bit11 Laufwerksdiagnose und Indexsimulation (DIAG_RPM, DRIVE_SCAN, INDEX_SIM; Firmware 1.13), Bit12 FORMAT (1.14), Bit13 SELFTEST (1.14), Bit14 WRITE_LIMITS (1.15), Bit15 adressierte IEC-Kommandos und strukturierter DOS-Lesestatus (1.18), Bit16 DESCRIBE, Bit17 Integritätsjobs, Bit18 Ressourcenverwaltung, Bit19 FEATURES, Bit20 DIAGNOSTICS, Bit21 Firmware-Detailanalyse und erweitertes Firmware-Prüflesen (1.18, Abschnitt 5.2), Bit22 erweiterte Analyseflags TIMING/GAPS/GLITCHES/POSITIONS/SYNC/WINDOWS mit QX-Bericht (Abschnitt 5.2), Bit23 IBM-FM-/HP-MMFM-Decoder, erweitertes Prüflesen und Diagnoseparameter v2 mit QD-Bericht (Abschnitt 5.2).

**INFO Bit24:** Analysejobs 0x0F, detaillierte Discovery, Ergebnis-Seiten,
Commodore-GCR-Analyse, WRITE-Erwartungsmenge und ausgewählte Selbsttests
(Abschnitt 6.1). Dieses Bit erweitert die obige Capabilityliste.

**Beschäftigt (Status 1, ab 1.18):** Die allgemeine Jobsperre lässt PING, INFO,
STATUS, EVENTS, WRITE_LIMITS, DESCRIBE, ABORT, DUMP_STATUS und DUMP_ABORT zu. Beim Dump
gilt zusätzlich dessen Sperre; ABORT wird dort abgewiesen, DUMP_ABORT bleibt
zulässig. Konkurrierende Laufwerks-, IEC-, Datei-, Stromversorgungs- und
Indexsimulationsbefehle werden abgewiesen. WRITE wartet höchstens 1 s auf den
Indeximpuls (sonst Ende-Status 4); schlägt das Positionieren vor dem Schreiben
fehl, endet WRITE mit Status 3.

### 3.2 Statuscodes

| Status | Bedeutung |
|---|---|
| 0 | OK |
| 1 | beschäftigt (Dump läuft, Vorgang aktiv) |
| 2 | kein Laufwerk angewählt / Laufwerk fehlt |
| 3 | Positionieren fehlgeschlagen (Spur 0) |
| 4 | kein Indeximpuls / abgebrochen |
| 5 | Zeitüberschreitung |
| 6 | DMA-/Prüflese-Fehler |
| 7 | USB-Fehler |
| 8 | IEC: kein Gerät |
| 9 | IEC: keine Quittung |
| 10 | Puffer voll / Überlauf |
| 11 | nicht unterstützt (auf diesem Board) |
| 12 | schreibgeschützt |
| 13 | Speicher (SD-NAND/Dateisystem) |
| 14 | Rahmenfehler (CRC/Länge) |
| 15 | unbekannter Befehl |
| 16 | ungültige Argumente |
| 17 | Datei nicht gefunden |
| 18 | Flussabstand nicht darstellbar (1,6–238 µs; WRITE ohne Bit4), nichts geschrieben (1.15) |
| 19 | Spur länger als eine Umdrehung: der Index hat das Schreiben beendet, die Spur ist abgeschnitten (1.15) |
| 20 | über den Cancel-Pfad abgebrochen (1.18, Abschnitt 2.5) |
| 21 | Mediengeneration oder angewähltes Laufwerk hat sich während einer geschützten nativen Operation geändert, `MEDIA_CHANGED` (INFO Bit27, Abschnitt 4.2) |
| 22 | Sitzungs-Schreibsperre aktiv, `SOFT_WRITE_LOCK` (Op 37, Abschnitt 4.6): das Schreiben wurde von der Sperre dieser Sitzung abgewiesen, nicht vom Schreibschutz der Diskette oder dem Jumper (1.18) |
| 23 | Drehzahl außerhalb der Toleranz um die Quell-Indexdauer, `SPEED_MISMATCH` (Op 41, Abschnitt 4.10): vor Kopfbewegung und Schreibtor abgewiesen, nichts geschrieben (1.18) |

### 3.3 Festlegungen im Detail

- **seq:** Der Host nummeriert Anfragen 1…255 (danach wieder 1); 0 ist der Rahmenfehler-Antwort vorbehalten. Höchstens **eine Anfrage gleichzeitig** offen. Ereignisse zählen ihre eigene seq ab 0 (Überlauf 255 → 0).
- **Zeitüberschreitung:** Kommt eine Antwort nicht (z. B. wegen CRC-Fehler verworfen), wartet der Host 1 s (Lesen/Schreiben: bis zum Ende-Rahmen, Zeitlimit großzügig) und setzt dann den Kanal zurück (10000 Baud). Eine Antwort mit `cmd=0xFF, seq=0, status=14` gehört zur gerade offenen Anfrage.
- **INFO:** `store_bytes` = Größe des Flussspeichers (8 MB PSRAM, sonst interner Ersatzpuffer), nicht der SD-NAND (dafür SD_INFO). Der Text danach ist ASCII, Zeilen durch `\n` getrennt, ohne abschließendes Nullbyte: Name/Version, Git-Stand, Build-Datum, PSRAM-Ergebnis, ab 1.14 die Reset-Ursache (`reset: …`).
- **board_status (8 Byte):** `power` u8 (Bit0 FDD_5V an, Bit1 FDD_12V an, Bit2/3 eFuse-Fehler 5 V/12 V – Platine v1.0 ab A11c: FLT des TPS25921A U28/U29 low bei Unter-/Überspannung oder Übertemperatur, nur bewertet solange die Schiene eingeschaltet ist, eine Änderung löst Ereignis 0x83 aus, der eFuse versucht selbst erneut), `flags` u8 (Bit0 WRITE-LOCK gesteckt – v0.x Jumper J11/JP4; v1.0 Frontschalter SCHREIBEN offen oder Frontkabel ab, Platine A9b: Schreiben ist dort im Grundzustand gesperrt und nur bei Schalter EIN plus Firmware-Freigabe WLOCK_FW während eines Schreibauftrags möglich; ab A11f sperrt die Hardware das Schreibtor als NOR(WGATE_n, LOCK) mit U31 – PE14 bleibt dafür auch auf v1.0 aktiv low (`bus_out_inverted()` nimmt PE14 aus), bei gesetzter Sperre kann WGATE nicht aktiv werden, Prüfung bei der Inbetriebnahme an J6 Pin 24, Bit1/2 Überstrom 5 V/12 V ausgelöst, Bit3 SD-NAND bereit, Bit4/5 Taster A/B gedrückt, Bit6 Sicherheitsmodus, Bit7 Sitzungs-Schreibsperre aktiv, Abschnitt 4.6, 1.18), `i5_ma` u16, `i12_ma` u16, `board_id_mv` u16.
- **Takt und Uhr (Platine v1.0, Stufe A9c):** Der HSE-Takt kommt auf v1.0 aus einem 25-MHz-TCXO (X1, HCMOS, ±2,5 ppm) im Bypass-Modus; die Firmware probiert beim Start zuerst den Bypass und fällt nach dem HSE-Timeout (100 ms) auf den Quarzoszillator der v0.x-Platinen zurück (`ufi_clock_hse_bypass()`). SELFTEST Posten 2 `usb_sof` misst auf v1.0 damit die TCXO-Abweichung. Echtzeituhr: 32,768-kHz-Quarz Y3 an PC14/PC15 (LSE, Treiberstufe medium-high) und CR2032 BT1 an VBAT; die Firmware startet die RTC nach der Platinenerkennung (`ufi_clock_rtc_init()`, nur board_rev 10, LSE-Wartezeit 2 s), ein nie gestellter Kalender beginnt bei 2000-01-01 00:00:00 und läuft von der Zelle weiter, VBAT-Laden bleibt aus. Protokoll-Ops zum Stellen und Lesen der Uhr sowie das Auslesen des Feuchte-/Temperatursensors SHT40 (U25, I²C 0x44) folgen in Stufe A10; bis dahin ändert sich an den Rahmen nichts.
- **READ:** `revs` 1…200. Der Ende-Rahmen ist maßgeblich: Nur so viele Umdrehungen gelten, wie er meldet (bei Überlauf können `FD`-Marken fehlen oder überzählig sein). Ende-Status 4 = kein Index **oder** per ABORT abgebrochen; 10 = Überlauf (Host las zu langsam). Ab 1.18 folgt hinter `revs` der 17-Byte-Generationsblock (4.5); alte Parser lesen weiter nur Byte 0, neue prüfen `len ≥ 18`.
- **WRITE_LIMITS (1.15):** reine Abfrage (auch während eines Dumps), Nutzdaten müssen leer sein (sonst 16). Der Host (`ufi write`, `write-disk`) holt die Grenzen einmal je Verbindung und weist Aufträge lokal ab, die die Firmware mit Status 10 oder 18 ablehnen würde – mit Spur, Index und Wert; mit Kappen-Flag entfällt die Intervallprüfung. Ältere Firmware ohne Bit 14: keine Vorprüfung.
- **WRITE Index während des Schreibens (1.15):** Kommt der nächste Indeximpuls, bevor die letzte Periode ausgegeben ist, fällt das Schreibtor sofort und der Auftrag endet mit Status 19 (zuvor galt dieser Sicherheitsstopp als Erfolg). Die Spur war länger als eine Umdrehung; Abhilfe: Zeitskala < 1 oder weniger Flusswechsel.
- **WRITE Bereichsprüfung (1.15):** Nach Zeitskala und Präkompensation muss jede Periode 440…65 536 Takte (1,6…238 µs) betragen. Liegt eine außerhalb, wird der Auftrag **vor** Kopfbewegung und Schreibtor mit Ende-Status 18 abgewiesen; nichts auf der Diskette ändert sich. Mit `flags` Bit4 werden solche Perioden stattdessen auf die Grenze gesetzt, und der Ende-Rahmen meldet ihre Zahl in `clamped`. Firmware ≤ 1.14 kappte still. Diese Grenzen gelten für den klassischen nativen Pfad. GW und WRITE-Bit5 verwenden den erweiterten Pfad und weisen unzulässige Daten vor WGATE ab.
- **WRITE Ende-Nutzdaten (1.15):** `verify_level` u8: 0 keine Aussage, 1 Schreiben abgeschlossen (Timer/DMA ohne Fehler), 2 Rücklesen plausibel (nur Zahl der Flusswechsel ±5 %), 3 Inhalt geprüft, 4 nicht prüfbar; `clamped` u32. Stufe 3: Die Firmware dekodiert vor dem Rücklesen die gepackte Spur selbst (IBM-MFM, sonst Amiga; Datenrate 250/300/500/1000 kbit/s aus dem kürzesten häufigen Abstand) und merkt sich Sektorkennung, Größencode und CRC-32 der Datenbytes (128 << N, bis 64 Sektoren; Firmware 1.15–1.16 prüfte stets 512 Byte, bei 1024-Byte-Sektoren also nur die erste Hälfte). Dann liest sie zwei Umdrehungen zurück: Alle Referenzsektoren wieder mit gültiger CRC und gleichen Daten gefunden → 3 (Jitter stört nicht, die Zahl der Flusswechsel ist dann unerheblich); ein fehlender oder veränderter Sektor → Ende-Status 6. Spuren ohne dekodierbare Sektoren (GCR, Schutzspuren, nur Flux) erhalten höchstens Stufe 2 (Zahl der Flusswechsel ±5 %, mindestens 100), sonst Status 6 – „verifiziert“ heißt das nicht. Der Ende-Rahmen trägt die Nutzdaten auch bei Fehlerstatus; Firmware ≤ 1.14 sendet keine. Ab 1.18 folgt hinter `clamped` der 17-Byte-Generationsblock (4.5), auch nach einem Start über GUARDED_WRITE 0x27, und dahinter `erased_ticks` u32 und `start_offset_applied` u32 (30 Byte insgesamt); alte Parser lesen weiter Byte 0 und 1–4.
- **WRITE Spurrest löschen (1.18, `flags2` Bit0):** Hinter allen flaggesteuerten Feldern darf genau ein weiteres Byte `flags2` stehen (WRITE_LIMITS meldet die Fähigkeit in Bit3; andere `flags2`-Bits → 16). Mit Bit0 bleibt nach dem letzten Intervall das Schreibtor offen und WDATA ruhig, also dasselbe Signal wie ERASE 0x23, bis zum nächsten Index; dann schließt die Firmware das Tor und meldet im Ende-Rahmen `erased_ticks` (Länge des gelöschten Rests in Takten von 275 MHz, 0 ohne Option oder wenn der Index die Spur vorher abgeschnitten hat, Status 19 wie bisher). Damit bleibt hinter einer kürzeren neuen Spur nichts von der alten stehen. Apple-Disk-II-Laufwerke haben auf diesem Pfad keinen Index: Bit0 → Status 11. Greaseweazle-Schreibbefehle sind nicht betroffen. Host: `write_track(..., erase_rest=True)`, `ufi write --erase-rest`, `WriteResult.erased_ticks`.
- **WRITE Startversatz (1.18, `flags2` Bit1):** Hinter dem `flags2`-Byte folgt `start_offset_ticks` u32 (1…275 000 000 Takte, also bis eine Sekunde; 0 oder mehr → 16 mit Op-39-Feld 14, fehlende Bytes → Feld 14 Detail 1; WRITE_LIMITS Bit4 kündigt die Fähigkeit an). Das Schreibtor öffnet dann nicht am Index, sondern bei Index + Versatz: der Indeximpuls stellt Vergleichskanal 4 des Flusstimers auf diesen Zeitpunkt, dessen Interrupt öffnet das Tor und startet den Schreibtimer; das erste Intervall zählt ab dort. So geht eine Spur mit ihrer ursprünglichen Schnittstelle (Splice) zurück. Weitere Indeximpulse während der Wartezeit werden ignoriert (ein Versatz länger als eine Umdrehung landet in der nächsten); der Indexstopp (Status 19) gilt ab dem verzögerten Start, Spurrest löschen (Bit0) lässt sich kombinieren, ABORT verwirft den wartenden Start. Der Ende-Rahmen trägt hinter `erased_ticks` `start_offset_applied` u32: gemessener Abstand Torbeginn − Index einschließlich Interruptlatenz, 0 ohne Bit1 oder wenn das Tor nie öffnete (Ende-Nutzdaten damit 30 Byte). Apple Disk II: Versatz ab dem Sync-Sensor-Impuls mit `apple_sync=1`, sonst Status 11. Greaseweazle-Schreibbefehle sind nicht betroffen. Host: `write_track(..., start_offset=N)`, `ufi write --start-offset N`, `WriteResult.start_offset_applied`.
- **WRITE Zeitskala (Bit3, 1.14):** Die Firmware multipliziert jeden Flussabstand mit `scale`/1000 (Rest wird mitgeführt, kein Drift), bevor Präkompensation und Grenzen greifen. Damit lassen sich Abbilder mit anderer Drehzahl auf einem Laufwerk mit fester Drehzahl schreiben (Faktor = Drehzahl der Quelle / Drehzahl des Ziels, z. B. 360-U/min-Abbild auf 300-U/min-Laufwerk: 1,2) und die Zonen von Macintosh-400K/800K-Disketten (variable Drehzahl) auf einem PC-Laufwerk nachbilden (Faktor je Spurzone). Außerhalb 0,5…2,0 Status 16.
- **WRITE-Daten (1.15):** Zulässig sind nur die Codes `01…EF`, `F0…FC b` und `FE u32` (u32 ≠ 0); `00`, `FD` (Indexmarke) und `FF` sowie mehr Übergänge als `flux_count` beenden den Auftrag **sofort** mit Ende-Status 10, weitere Daten-Rahmen dieser seq werden verworfen (Firmware ≤ 1.14 sammelte danach weiter und konnte dabei Speicher überschreiben). `flux_count` ist nur durch den Flussspeicher begrenzt (Status 10 in der Annahme-Antwort): Die Firmware spielt die Perioden aus einem Ring von zwei 2048er-Hälften in SRAM2, den die DMA-Halb-/Vollinterrupts aus der gepackten Spur nachladen (ein DMA-Transfer kann höchstens 65 535 Perioden; Firmware ≤ 1.14 übergab längere Spuren trotzdem einem einzigen Transfer und schnitt sie damit ab). Der Stopp nach der letzten Periode wird gegen den DMA-Zähler ausgezählt; kommt ein Nachladen zu spät (Unterlauf), endet der Auftrag mit Status 6 und das Schreibtor fällt sofort ab.
- **WRITE:** Der Host wartet die Annahme-Antwort ab und sendet erst dann die Daten-Rahmen (je ≤ 4096 Byte Nutzdaten, beliebig aufgeteilt, zusammen genau `byte_count`). Ende-Status: 0 OK, 12 schreibgeschützt, 4 kein Index (Laufwerk mit Index), 6 Prüflesen fehlgeschlagen (`flags` Bit0), 10 Daten fehlerhaft/zu viele Wechsel.
- **Dateinamen:** FAT 8.3 (keine langen Namen), Großbuchstaben, Pfadtrenner `/` erlaubt (z. B. `DUMPS/DUMP0001.SCP`), höchstens 64 Zeichen.
- **READ_FILE:** Weniger Bytes als angefordert (auch 0) = Dateiende.
- **WRITE_FILE:** `flags` Bit0 legt die Datei neu an bzw. kürzt sie auf 0 (auch mit 0 Datenbytes). Weniger geschriebene als gesendete Bytes = Speicher voll; die Antwort trägt dann Status 13.
- **EVENTS:** Unbekannte Bits in `mask` werden ignoriert; `mask = 0` schaltet alle Ereignisse ab.
- **DUMP_START:** Nutzdaten leer (Werte aus UFI.CFG) oder 4 Byte, ab 1.16 auch 5 Byte (`format`; ohne das Byte gilt `UFI.CFG format`; Werte über 4 → Status 16).
- **Abbild von SD schreiben (1.16):** `UFI.CFG button_a=write` + `write_file=NAME` (Taster A ≥ 1 s) oder WRITE_START mit dem Namen. Die Firmware erkennt die Datei an Endung und Größe: `.SCP` → Umdrehung 0 jeder Spur als Flux (Zellen 25 ns → Takte mit Rundung auf die absolute Zeit wie `ufi write-disk`; Perioden außerhalb 1,6–238 µs werden gekappt, da ein Flux-Abbild sie enthalten darf; Seitenauswahl aus dem SCP-Kopf; Spuren ohne Eintrag werden übersprungen), `.HFE` (1.17) → HxC-Bitzellen mit der Bitrate aus dem Kopf (Spuren, Seiten, Einzel-/Doppelschritt ebenfalls aus dem Kopf; 1000 kbit/s abgelehnt): Da eine HFE-Spur genau eine nominelle Umdrehung enthält, wird die Zellenlänge so skaliert, dass die Spur 98,5 % der gemessenen Umdrehung füllt, begrenzt auf ±3 % (sonst würde sie am Index abgeschnitten, Status 19); `WRITE.LOG` nennt den Faktor (`ok (scale 0.995)`), Rücklesen wie bei SCP; `.IMG/.IMA/.DSK/.ST` → IBM-MFM nach Größe (160K–2.88M, DMF, ST 720/800/810K; 360K mit `double_step` und 250/300 kbit/s je nach gemessener Drehzahl; ED wird abgelehnt), `.ADF` 880K/1.76M → Amiga, `.D81` → 1581-Layout (Seitentausch) auf einem PC-Laufwerk oder, bei `drive=iec`, Blöcke per U2; `.D64/.D71` nur mit `drive=iec` (1571 beidseitig nach `U0>M1`; die Diskette muss formatiert sein). Sektorspuren werden wie bei FORMAT aus der gemessenen Umdrehung zu 98 % gefüllt. Nach jeder Spur folgt ein Rücklesen: Sektorabbilder brauchen alle Sektoren mit gültiger CRC, Flux die Zahl der Flusswechsel ±5 %. `WRITE.LOG` listet je Spur `ok`, `read-back BAD`, `not in the file` oder den Schreibfehler, bei IEC die abgelehnten Blöcke mit DOS-Code, zum Schluss `written … tracks, bad …`. Fehler (ERR-LED, `error`): 1 Speicher, 2 Laufwerk, 5 Abbild nicht erkannt oder falsches Laufwerk, 6 schreibgeschützt (auch DOS 26), 7 Datei fehlt; Taster B bricht ab. Während des Jobs gilt das Gerät als beschäftigt (Status 1).
- **Dump dekodiert (1.16):** `UFI.CFG format=img|adf` oder DUMP_START Byte 5: Statt Flux schreibt die Firmware die Sektoren des auf Spur 0 erkannten Formats (IBM-MFM 250/300/500/1000 kbit/s oder Amiga DD/HD, Qualitätsprobe wie bisher) als `DUMPnnnn.IMG` bzw. `.ADF` – zylinderweise, Seite 0 vor Seite 1, Sektoren 1…`spt` in `sector_bytes` (aus dem ersten gültigen Sektorkopf, 128–1024), fehlende Sektoren als Nullen (Spur im `.LOG` als `BAD`, Zahl in `bad_tracks` der `.JSN`). `img` und `adf` unterscheiden sich nur im Namen; ADF-Reihenfolge ist dieselbe. Wird auf Spur 0 kein Format erkannt (GCR, Apple, unformatiert), endet der Dump mit Fehler 5 (ERR-LED blinkt fünfmal). `double_step=auto`, `tracks=0` und `track_search` wirken wie beim SCP-Dump; Kopierschutz und schwache Bits gehen in diesem Format verloren.
- **UFI.CFG:** Textdatei, Zeilen `schlüssel=wert` (Schlüssel klein), `#` beginnt einen Kommentar, CRLF; unbekannte Schlüssel werden ignoriert. Schlüssel: `drive, tracks, sides, revs, button_a, copy_from, copy_to, protocol, apple_sync, index_sim, index_sim_mode` (`index_sim=300|360` schaltet die Indexsimulation beim Start ein, 1.13; `index_sim_mode=pin|internal|both`, 1.14), `rpm_line, rpm_360, rpm` (3-Mode-Laufwerke, 1.14), `iec_device`, `track_search`, `iec_type`, `double_step`, `auto_dump` (1.14), `tg43`, `format` (`scp|img|adf`, ab 1.17 auch `hfe`), `write_file` (1.16; `button_a=write`); `tracks=0` = automatisch.
- **Dump als HFE (1.17):** `UFI.CFG format=hfe` oder DUMP_START Byte 5 = 3: `DUMPnnnn.HFE` im HxC-HFE-v1-Format (512-Byte-Kopf `HXCPICFE`, Spurtabelle in Block 1, je Zylinder 512-Byte-Blöcke mit 256 Byte Seite 0 und 256 Byte Seite 1, ein Bit je MFM-Zelle, LSB zuerst). Je Spur geht Umdrehung 0 durch die Taktrückgewinnung des MFM-Decoders (Periode ±12 %, 1/16 des Fehlers in die Frequenz, halber Rest in die Phase; Lücken über 8 Zellen bleiben Nullen). Bitrate und Kodierung (`ISOIBM_MFM`/`AMIGA_MFM`) kommen aus der Spur-0-Probe; ohne erkanntes Format gilt 500 kbit/s bei mehr als 55 000 Flusswechseln je Umdrehung, sonst 250, Kodierung `unknown` (Hinweis im `.LOG`). Seite 0 legt die Spurlänge des Zylinders fest (Zellen einer Umdrehung bei nomineller Rate, auf Bytes aufgerundet, Rest Nullen), Seite 1 wird darauf gekürzt oder aufgefüllt; der Kopf trägt Spuren, Seiten, Bitrate, mittlere Drehzahl und `double_step`. ED (1 Mbit/s) passt nicht in HFE v1 (Spurlänge > 65 535 Byte) → Fehler 5. `.JSN` nennt `format` hfe und die verwendete `rate_bps`. Das Host-Werkzeug rechnet mit denselben Ganzzahlen (`ufi read-disk -o x.hfe`), ein Dump und die Host-Umrechnung derselben Flussdaten sind byteidentisch.
- **IEC_NIB (1.14, 1541-Rohspuren):** Die Firmware lädt beim ersten Aufruf (oder mit Bit0) ein 6502-Programm (`firmware/tools/iec_nib.s`, 6 + 32 Byte je M-W) nach $0310 im Laufwerk, schreibt die Parameter nach $0300 (Halbspur, Sync-Index, Modus, Phasenrichtung), startet es mit M-E und wartet bis zu 6 s, bis das Laufwerk wieder auf ATN antwortet (es läuft mit gesperrten Interrupts). Das Programm fährt den Kopf in Halbschritten (eigener Positionszähler $0308, den der Host nach der Kalibrierung setzt; 0 = nicht bewegen), setzt die Dichte-Bits der Spur, sucht den Spurursprung (Sync nach der längsten Strecke ohne Sync, also der Header-Sync von Sektor 0), überspringt `sync` Syncs, misst die Sync-Länge und liest 512 Bytes; während eines Syncs liefert es je Bytezeit ein $FF, Syncs erscheinen also als $FF-Folgen mit ungefährer Länge. Modus 1 liest 512 Bytes ohne Bezug (unformatierte Spuren), Modus 2 positioniert nur. Die Antwort kommt aus $0304 (Status, Sync-Länge, längste Strecke, Zähler) und $0600–$07FF; $0700 ist der BAM-Puffer des DOS, der Host sendet zum Schluss `I0`. Das Zusammensetzen der Stücke (alle 2 Syncs eines, Ausrichtung an den $FF-Folgen, Ende beim Wiederauftauchen des Ursprungs) und die G64-Ausgabe macht `ufi iec-nib`; Dauer etwa 10–20 s je Spur. Kein Bitzellen-Timing, schwache Bits nur über Mehrfachlesen.
- **IEC-Dump (1.14):** DUMP_START mit `drive` = 5 (iec), per UFI.CFG `drive=iec` oder Taster A, liest eine 1541/1571/1581-Diskette über den Laufwerkscontroller (Kanal 2 „#“, `U1`-Blocklesen, Gerät `iec_device`, Standard 8). Laufwerkstyp aus `UFI.CFG iec_type` (1541/1571/1581) oder bei `auto` aus der Einschaltmeldung nach einem Bus-Reset („73,CBM DOS V2.6 1541“, „V3.0 1571“, „V10 1581“; andere Meldungen → 1541): 1541 → `DUMPnnnn.D64` mit `tracks` 35 (683 Blöcke, 174 848 Byte) oder 40 (768 Blöcke; andere Werte gelten als 35); 1571 → `.D71` (70 Spuren, 1366 Blöcke, nach `U0>M1`; `tracks=35|40` liest nur Seite 1 in D64-Geometrie); 1581 → `.D81` (80 Spuren × 40 Sektoren, 819 200 Byte). `sides`/`revs` werden ignoriert. Nicht lesbare Blöcke stehen als Nullen in der Datei und mit Spur/Sektor in `DUMPnnnn.LOG`. DUMP_STATUS meldet in `track` die 1541-Spur (1–40), `side` 0. Dauer einige Minuten (seriell, etwa 0,3–0,6 s je Block); Abbruch wie gewohnt (DUMP_ABORT, Taster B). Kein Flux: Kopierschutz und schwache Bits sind so nicht erfassbar.
- **IEC_BLOCK (1.16, Blöcke über den Laufwerkscontroller):** op 0 liest wie der IEC-Dump (`#` auf Kanal 2, `U1:2 0 t s`, 256 Byte). op 1 schreibt: `#` öffnen, `B-P:2 0`, 256 Byte auf Kanal 2 senden, `U2:2 0 t s`, dann die DOS-Meldung von Kanal 15 lesen; `dos` ist deren zweistelliger Code (0 ok, 25 Verify-Fehler, 26 schreibgeschützt, 66 ungültige Spur/Sektor, 74 nicht bereit). Status ≠ 0 heißt, der Bus selbst hat nicht mitgespielt (8/9/5); Status 0 mit `dos` ≠ 0 heißt, das Laufwerk hat den Block abgelehnt. Die Diskette muss formatiert sein (`N0:NAME,ID` per IEC_MEM op 3, etwa eine Minute), 1571 beidseitig nach `U0>M1`. Etwa 0,4–0,7 s je Block (seriell, DOS-Status inklusive), eine D64 in 5–8 Minuten. `ufi iec-write abbild.d64 [--verify] [--new NAME,ID]`, `ufi iec-read ziel.d64 --type 1541|1571|1581 --tracks 35|40`, `ufi iec-status`. Nicht während eines Dumps (Status 1).
- **FORMAT (1.14):** Die Firmware erzeugt die Spur selbst (Encoder des USB-Floppy-Modus): IBM-MFM mit 512-Byte-Sektoren 1…`spt` (IAM, Standard-Gaps; Gap 3 wird gekürzt, wenn die Spur nicht reicht) oder Amiga-Trackdisk (Bit1; 11 Sektoren bei 250 kbit/s, ab 1.16 auch HD mit 22 Sektoren bei 500 kbit/s), Datenbytes = `fill`. Die Umdrehungszeit wird je Motorlauf einmal gemessen (zwei Indexabstände, 60 s gültig) und die Spur zu 98 % gefüllt. Geschrieben wird ab Index mit Präkompensation; mit Bit0 folgt ein Prüflesen über zwei Umdrehungen (`good` zählt nur Sektoren mit gültiger CRC **und** Datenbytes = `fill`). Blockierend (bis etwa 2 s je Spur). Status 2 ohne Laufwerk/IEC, 16 Apple oder ungültige Werte (Rate, `spt` 1–36, Amiga ≠ 11/22), 12 schreibgeschützt, 10 Sektoren passen nicht in die Umdrehung, 4 kein Index. Nützliche Layouts: 1.44M 500/18, 720K 250/9, 1.2M 500/15, 360K 300/9 (mit `double_step`), DMF 500/21, Amiga 250/11, Amiga HD 500/22, 1581 250/10 + Bit2.
- **ANALYZE (1.14):** liest `revs` Umdrehungen blockierend (kein Datenstrom, bis 4 s) und wertet aus: Umdrehungszeit/Drehzahl, Flusswechsel, Histogramm der Abstände in 100-ns-Klassen mit bis zu vier Spitzen (lokale Maxima über 2 % der Wechsel), daraus Kodierung und Bitzelle: Verhältnis 1:1,5 (4/6/8 µs) = MFM bzw. an Apple-Laufwerken GCR, Bitzelle = halbe erste Spitze; 1:2 mit dritter Spitze ≈ 3 = C64-GCR, Bitzelle = erste Spitze; 1:2 ohne = FM, Bitzelle = doppelte erste Spitze. `bitcells` = Umdrehungszeit / Bitzelle (Long-Track-Erkennung). Schwache Bits: die Umdrehung wird in 64 Zeitfenster geteilt; weicht die Zahl der Flusswechsel eines Fensters zwischen den Umdrehungen um ≥ 3 ab, gilt es als schwach (`weak_mask` Bit w). Für MFM werden die Sektoren von Umdrehung 0 dekodiert (`sectors` = gültige CRC) und die Zeit vom Index bis zur ersten gültigen Sektor-ID gemeldet (Spurausrichtung, radialer Versatz des Laufwerks). Status 2 ohne Laufwerk, 4 ohne Index, 5 bei Zeitüberschreitung. `ufi analyze 0 0 -r 5`.
- **Dump-Metadaten (1.16):** Zu jedem Dump entsteht neben Abbild und `.LOG` die Datei `DUMPnnnn.JSN` (JSON; 8.3-Name, daher `.JSN`): `file`, `format` (scp/d64/d71/d81/img/adf, ab 1.17 hfe), `firmware`, `git`, `board` (5/6/7), `drive`, `sample_hz`; bei Flux-Dumps `tracks`, `sides`, `revs`, `double_step`, `rpm` (Mittel der Spuren), `encoding` (mfm/amiga/unknown aus der Qualitätsprobe), `rate_bps`, `spt`, `sector_bytes`, `bad_tracks`; bei IEC-Dumps `dos` (Einschaltmeldung), `tracks`, `bad_blocks`; immer `checksum` (Bytesumme des Abbilds hinter dem SCP-Kopf, identisch mit dem Prüfsummenfeld im SCP-Kopf; bei IEC über alle Bytes). Per READ_FILE abholbar.
- **Dump (1.14):** `tracks=0` ermittelt vor dem Dump die höchste erreichbare Spur (PROBE_TRACKS) und liest bis dahin (max. 84); `UFI.CFG track_search=1` liest bei `double_step` (40-Spur-Diskette im 80-Spur-Laufwerk) eine schwache Spur zusätzlich eine physische Spur weiter außen und innen und behält die beste Position.
- **Dump `double_step` (1.14):** `UFI.CFG double_step=0|1|auto`. Bei `auto` liest die Firmware nach Spur 0 (Format aus der Qualitätsprobe bekannt) einmal die physische Spur 2: Melden die Sektorköpfe dort Zylinder 1, liegt eine 40-Spur-Diskette (48 tpi) im 80-Spur-Laufwerk; dann werden zwei Schritte je Spur geschaltet, der Kopf rekalibriert, `tracks` > 42 auf 40 (`tracks=0`) bzw. 42 gekürzt und `double step: on` ins LOG geschrieben. Ohne erkanntes Format (kein MFM/Amiga) bleibt es bei einfachen Schritten. Nicht für Apple und IEC.
- **Auto-Dump (1.14):** `UFI.CFG auto_dump=1` startet einen Dump von selbst, sobald eine Diskette eingelegt wird (PC-A/B- und Amiga-Laufwerke mit DSKCHG-Leitung; nicht Shugart-Bus, Apple, IEC). Im Leerlauf wird das Laufwerk alle 500 ms kurz angewählt und die DSKCHG-Leitung gelesen (ohne Diskette gesetzt); dann alle 2 s ein Schrittpaar (hörbar), das die Verriegelung bei eingelegter Diskette löscht; 1 s später beginnt der Dump wie mit Taster A. Nach dem Dump muss die Diskette erst entnommen werden. Ausgesetzt, solange der Host ein anderes Laufwerk gewählt hat, eine Übertragung läuft, der SD-/USB-Floppy-Modus aktiv ist oder `protocol=gw` gilt.
- **STEP_SCHEDULE (1.14, Spiradisc-artige Spuren):** Kopfbewegungen während des nächsten READ oder WRITE: `at_ms` zählt ab dem Startereignis der Operation (Indeximpuls, der Umdrehung 0 bzw. das Schreiben beginnt; ohne Index der Beginn der Aufnahme), `pos` ist die Zielposition in Spuren oder, mit Bit0, in Viertelspuren (Spur × 4 + Viertel, Apple). Die Einträge müssen zeitlich geordnet sein (sonst Status 16), die Hauptschleife führt sie bei Fälligkeit mit dem normalen Positionieren aus (Shugart: Schritt + Einschwingzeit, Apple: Phasen; Genauigkeit etwa 1–2 ms, die Aufnahme läuft dabei weiter). Der Plan gilt einmalig für die nächste Operation und verfällt, wenn sie vorher endet. `ufi read 17 0 --steps 50:17.1,100:17.2,150:17.3`.
- **SET_RPM (1.14, 3-Mode-Laufwerke mit 300/360 U/min):** Die Firmware schaltet die in `UFI.CFG rpm_line=density|drate` gewählte Busleitung (DENSITY = Pin 2, DRATE = Pin 6 über JP1 – dort auch DS3 des Shugart-Busses) auf den Pegel, der laut `rpm_360=low|high` 360 U/min bedeutet (Laufwerksjumper beachten), und wartet bei laufendem Motor die Hochlaufzeit (`spinup_ms`) ab. Ohne konfigurierte Leitung Status 11, andere Werte als 300/360 Status 16; die Antwort enthält immer den aktuellen Zustand. `rpm=300|360` setzt die Drehzahl beim Start. Mit `rpm_line=density` überschreibt LINES (0x16) die Wahl. Kontrolle mit DIAG_RPM.
- **Hartsektoriert (READ/WRITE Bit1, 1.14):** `sectors` = Zahl der Sektorlöcher (10, 16, 32; ≥ 2, sonst Status 16, ebenso ohne das Byte). Die Firmware zählt alle Löcher auf INDEX und nimmt nur das Indexloch als Index: den Impuls, der nach weniger als ¾ des vorigen Abstands kommt (das Indexloch liegt in der Mitte zwischen zwei Sektorlöchern; das folgende Sektorloch hat denselben kurzen Abstand, davor war der Abstand aber bereits kurz). Umdrehungen und `FD`-Marken zählen also von Indexloch zu Indexloch, WRITE beginnt und endet am Indexloch. Bis zur Erkennung vergehen bis zu einer Umdrehung plus zwei Löcher; die Einstellung gilt nur für den jeweiligen Befehl (Dump, Kopie und Greaseweazle-Modus arbeiten weichsektoriert).
- **Viertelspuren (Apple Disk II, 1.14):** SEEK mit `quarter` 1–3 bzw. READ/WRITE mit Bit2 (`track` = Spur × 4 + Viertel, 0–156) setzen den Kopf zwischen die Halbspuren: gerade Viertel = eine Phase, ungerade Viertel = beide Nachbarphasen gleichzeitig bestromt (bleiben bis zum nächsten Positionieren an). Bei anderen Laufwerken antwortet die Firmware mit Status 16; Spur × 4 + Viertel > 156 mit Status 3. Nach Abwahl des Laufwerks (Phasen stromlos) gilt eine Viertelposition als unbekannt, der nächste Seek rekalibriert. STATUS meldet weiterhin die ganze Spur. Kopierschutz-Spuren (z. B. Spur 17,5) liest man mit `ufi read 17 0 --quarter 2`.
- **DIAG_RPM (1.13):** misst am gewählten Laufwerk `revs` Umdrehungen über die INDEX-Leitung (Motor wird gestartet und läuft weiter); Status 2 ohne Laufwerk oder bei IEC, Status 4 ohne Indeximpuls (auch Apple ohne `apple_sync=1`), Status 5 bei Abbruch der Impulsfolge. `rpm_x100` = 6000 × 275 000 000 / `period_avg`, gerundet; `pulse` ist die mittlere Impulsbreite (Low-Zeit auf dem Kabel).
- **DRIVE_SCAN (1.13):** wählt nacheinander jedes der vier Laufwerke, startet den Motor, rekalibriert (TRK0 = Laufwerk antwortet), wartet bis 600 ms auf zwei Indeximpulse (= Diskette eingelegt und dreht) und liest den Schreibschutz; anschließend ist wieder das zuvor gewählte Laufwerk aktiv. Dauert bis zu einigen Sekunden je Laufwerk, bewegt die Köpfe.
- **INDEX_SIM (1.13):** Indeximpulse mit `rpm` Impulsen je Minute, Impuls = Low für `pulse_us`, erzeugt von TIM15 Kanal 1 auf PE5 (Firmware ab Platine v1.0; auf v0.7 lag der Ausgang als Open-Drain auf J9 Pin 6 = PE1, das jetzt die Modusleitung MODE2 des Front-Headers ist). Auf der Platine v1.0 geht der Impuls über einen Open-Drain-Treiber (ab A11b der MOSFET Q11, bis A11a der LS07 U7F; die Firmware wählt die Gate-Polarität nach `board_rev`, am Bus ändert sich nichts) fest auf die INDEX-Busleitung; der Sync-Sensor J19 speist seit A11d nicht mehr den INDEX-Bus, sondern das eigene Netz SYNC an PC6 (TIM8_CH1), dessen Auswertung (Apple-Sync über SYNC statt INDEX, Zeitstempel) in Stufe A10 folgt – bis dahin ist `apple_sync=1` auf v1.0 ohne Wirkung (Wired-OR wie der Sync-Sensor J19): die Rückschleife für den Selbsttest braucht keinen Draht mehr, und ein gewähltes Laufwerk sieht die Impulse ebenfalls, also Bit0 nur ohne laufendes Laufwerk oder bewusst einsetzen. Für Flippy-Disketten (PC-Laufwerk schreibt ohne Index nicht) liegt der 3,3-V-Impuls zusätzlich auf dem Logikanalysator-Header J21 Pin 10. Die Antwort enthält immer den aktuellen Zustand (auch bei Status 16 für `rpm` außerhalb 200–400, `pulse_us` ≥ halbe Umdrehung oder unbekannte `mode`-Bits). **`mode` Bit1 intern (1.14):** Der Impulsbeginn gilt im Lese- und Schreibpfad als Indeximpuls (READ-Umdrehungen, WRITE-Start und -Ende, ERASE, DIAG_RPM); die INDEX-Leitung des Laufwerks wird solange ignoriert, damit der über den Bus zurückgespiegelte Impuls nicht doppelt zählt. Ohne Bit0 bleibt PE5 ein ruhender Ausgang (Bus losgelassen). `UFI.CFG index_sim_mode=pin|internal|both`.
- **SELFTEST (1.14):** blockierend (etwa 0,5 s), nicht während READ/WRITE/Dump. Posten: 1 `timer` TIM2-Takte je ms gegen SysTick (275 000 ± 2 %); 2 `usb_sof` Quarzabweichung in ppm gegen 128 USB-Rahmen des Hosts (2 = nicht enumeriert, Warnung ab ±500 ppm); 3 `psram` Größe des Flussspeichers, frischer Mustertest über die obersten 64 KB (Fehler: `value` = Offset); 4 `sd` Kapazität in MB; 5/6 `rail_5v`/`rail_12v` Strom in mA (1 = Überstrom ausgelöst, 2 = abgeschaltet/Sicherheitsmodus); 7 `board_id` Teiler in mV (3 = keine bekannte Revision); 8 `usbc_cc` beworbener Strom in mA (3 = kein CC, USB-A-Kabel); 9 `fdd_lines` Laufwerkseingänge ohne gewähltes Laufwerk, Bitmaske der Leitungen auf Low (Bit0 INDEX, 1 TRK0, 2 WPROT, 3 DSKCHG, 4 READY, 5 RDATA; 3 = etwas zieht, z. B. ein angeschlossenes Laufwerk; 2 = Laufwerk gewählt); 10 `iec_loop` Rückschleife durch die IEC-Treiber/Empfänger (Bit0–3 ATN/CLK/DATA/SRQ nicht als gezogen gelesen = Fehler, Bit4–7 nicht als losgelassen gelesen = Warnung, RESET bleibt unberührt); 11 `index_loop` Indexsimulation 300 U/min auf PE5, Impuls muss binnen 250 ms auf INDEX erscheinen (Platine v1.0: fest über U7F auf den INDEX-Bus, kein Draht; v0.7: Draht J9-6 → J6-8; 2 = Impuls kommt nicht an); 12 `last_reset` Absturzaufzeichnung beim Start (`value` 1 HardFault, 2 MemManage, 3 BusFault, 4 UsageFault; 3 = vorhanden); 13 `stack_free` seit dem Start nie benutzte Stack-Bytes (beim Start wird das freie DTCM unter dem Stackzeiger mit einem Muster gefüllt; 3 unter 2 KB, 1 unter 512 Byte; der Linker prüft, dass die DTCM-Daten mindestens 16 KiB Stack frei lassen); 14 `log` `SELFTEST.LOG` geschrieben (`value` = Bytes). Ab Stufe A10 (Platine v1.0; in der Antwort vor `log`, bis zu 24 Posten): 15 `rtc` Kalender läuft, `value` = JJJJMMTT (2 = keine Uhr auf v0.x, 1 = v1.0 ohne schwingenden LSE); 16 `sht40` `value` = Temperatur in 0,01 °C (obere 16 Bit, Zweierkomplement) | Feuchte in 0,01 % (untere 16 Bit), bei Fehler `value` = SENSOR-`status` (1 auf v1.0, 2 auf v0.x); 17 `sync` Impulse des Sync-Sensors in 250 ms (`value`; 2 = keiner gesehen oder v0.x – ohne drehendes Apple-Laufwerk mit Sensor nichts zu beurteilen); 18/19 `v_fdd5`/`v_fdd12` Spannung am Laufwerksstecker in mV (1 = unter 4,5 V bzw. 10,8 V bei eingeschalteter Schiene, 2 = Schiene aus oder v0.x ohne Teiler); 20 `wgate_lock` Schreibsperre A11f (`value` 0 ok, 1 = WGATE trotz Sperre aktiv, 2 = schon im Ruhezustand aktiv; 2 = v0.x, Laufwerk gewählt oder Schreibauftrag läuft); 21 `fdd_pins` Rücklesungen wie FDD_PINS `pins` (`value`; 3 = WGATE-Bus aktiv oder eFuse-Fehler, 2 = v0.x). Dieselbe Tabelle steht als Text in `SELFTEST.LOG`.
- **Absturzaufzeichnung (1.14):** Ein Cortex-M-Fault legt Register (r0–r3, r12, lr, pc, xpsr), CFSR/HFSR/MMFAR/BFAR und die Laufzeit im DTCM ab und löst sofort einen Reset aus (statt bis zum Watchdog zu hängen). Beim nächsten Start schreibt die Firmware `CRASH.LOG` (nur der letzte Fault, per READ_FILE abholbar) und INFO meldet in der fünften Textzeile `reset: HardFault pc=… lr=… cfsr=… (CRASH.LOG)`; sonst steht dort die Reset-Ursache (`power-on`, `pin`, `software`, `watchdog`, `brown-out`).
- **TIMING / TG43 (1.16):** Die Zeitwerte sind 11 × u16: `step_pulse_us, step_rate_us, settle_us, dir_change_us, side_settle_us, spinup_ms, select_settle_us, motor_off_s, double_step, precomp_ns, tg43`. Mit `tg43` = 1 folgt die DENSITY-Leitung (J6 Pin 2) nach jedem Positionieren und Rekalibrieren der physischen Spur: ab Spur 43 gezogen (low), darunter losgelassen. Auf dem 50-poligen Bus eines 8-Zoll-Laufwerks ist Pin 2 des Adapters TG43 („track greater than 43“), das den Schreibstrom auf den inneren Spuren senkt; ohne das Signal sind Schreibvorgänge dort unzuverlässig. Nicht zusammen mit `rpm_line=density` (dann hat die Drehzahlwahl Vorrang) und nicht am Apple-Port. `UFI.CFG tg43=1` setzt das Feld beim Start, LINES (0x16) überschreibt den Pegel bis zum nächsten Positionieren. Ältere Hosts senden 10 Felder und lassen `tg43` unberührt. Standardwerte: 10 µs, 3 ms, 15 ms, 15 ms (ab 1.18, zuvor 0: Beruhigung bei Richtungsumkehr), 200 µs, 500 ms, 10 ms, 30 s, 0, 0xFFFF, 0. Benannte Sätze (Standard, Konservativ) setzt ANALYSIS_JOB Op 18 (4.4).
- **Nicht in v2:** Timer-/GPIO-Diagnose gibt es nur in v1; die Drehzahl liefern DIAG_RPM und ANALYZE.

### 3.4 Ergänzungen ab Firmware 1.18-dev (IEC-Adressierung)

INFO-Capability Bit 15 kündigt adressierte IEC-Kommandos und DOS-Status beim
Blocklesen an. Die bisherigen IEC_MEM- und IEC_BLOCK-Payloads bleiben unverändert.

| Kommando | Request-Payload | Erfolgreiche Transportantwort |
|---|---|---|
| `0x36 IEC_TARGET` | `schema=1, device, op, addr_lo, addr_hi, ...` | wie IEC_MEM |
| `0x37 IEC_READ_STATUS` | `schema=1, device, track, sector` | `schema=1, dos, track, sector` plus 256 Datenbytes ausschließlich bei `dos=0` |

`device=0` verwendet `UFI.CFG iec_device`; sonst sind Adressen 8–30 zulässig.
IEC_TARGET kapselt IEC_MEM mit M-W, M-R, M-E, DOS-Kommando oder Statusabfrage.
DOS-Kommandos haben höchstens 40 Bytes. Bei IEC_READ_STATUS beschreibt der
Frame-Status Bus-/Protokollfehler; ein DOS-Lesefehler hat Frame-Status 0, seinen
zweistelligen DOS-Code in der Payload und keine Sektordaten. Nach U1 wird der
Fehlerkanal gelesen, bevor der Kanal-2-Puffer als gültiger Sektor angenommen wird.
Die angegebenen Spur-/Sektorbytes sind die angeforderte Adresse.

Während aktiver Capture-/Write-/Dump-Jobs werden konkurrierende IEC-, Datei-,
Modus-, Stromversorgungs- und Diagnoseoperationen mit BUSY abgewiesen. PING, INFO,
STATUS, EVENTS, WRITE_LIMITS, DESCRIBE, ABORT, DUMP_STATUS und DUMP_ABORT sind von dieser
allgemeinen Sperre ausgenommen; die zusätzliche Dump-Sperre gilt weiterhin.
ABORT beendet den Flux-Job. Im Composite-Modus kann EP0-CANCEL auch die
Warteschleifen synchroner IEC-Aufrufe abbrechen; siehe Abschnitt 2.5.

### 3.5 Beispiel: Spur lesen

```
Host:  55 AA 01 10 01 00 01 00  01               crc   SELECT drive=1 (A)
Gerät: 55 AA 02 10 01 00 00 00                   crc   OK
Host:  55 AA 01 11 02 00 01 00  01               crc   MOTOR on
Gerät: 55 AA 02 11 02 00 00 00                   crc
Host:  55 AA 01 20 03 00 06 00  00 00 03 00 00 00 crc  READ track 0, side 0, 3 revs
Gerät: 55 AA 02 20 03 00 00 00                   crc   angenommen
Gerät: 55 AA 04 20 03 00 00 10  <4096 Byte Fluss> crc  Daten …
Gerät: 55 AA 05 20 03 00 01 00  03               crc   Ende, 3 Umdrehungen
```

### 3.6 Flag-Abhängigkeiten und Wechselwirkungen

Flags gehören immer zu ihrem jeweiligen Request-/Konfigurationsfeld. Gleiche
Bitwerte in READ, WRITE, ANALYZE, Jobflags und Op55 haben verschiedene Bedeutungen;
Masken aus verschiedenen Feldern dürfen nicht miteinander verodert werden.
Es gibt keine sinnvolle globale Einstellung „alle Flags einschalten“.

| Kombination | Implementiertes Verhalten |
|---|---|
| ANALYZE Bit1–7 ohne DETAIL Bit0 | BAD_ARGS; auch ADAPTIVE benötigt DETAIL. `0xFF` ist als Basis-Analysemaske zulässig. |
| QUALITY-Diagnosefeatures CODING/MARKS/MARGIN | Miteinander und mit den Basis-Analyseflags kombinierbar. Schema2 mit37 Bytes benötigt mindestens ein bekanntes Feature sowie gültige Parameter. |
| FAST_FORMAT_HINT + CONFIRM_CONTENT (Policy1) | Bestätigung hat Vorrang: vollständige Suche, kein Cache-Kurzlauf. Angefordertes Feature bleibt in QD-Byte8 sichtbar; QD-Byte9 Bit3 bleibt aus. Echte Mehrdeutigkeit der vollständigen Suche verhindert weiterhin eine Bestätigung. |
| FAST_FORMAT_HINT + aktive Op55-Optionen / REV_COMPARE / Policy4 | Hinweis wird nicht verwendet; vollständige Suche. Auch explizites Format oder explizite Rate verhindern den Kurzlauf. |
| ADAPTIVE + Kopierschutzhinweise | Alternative PLL-Profile dürfen Decoderbefunde liefern; sie zählen nicht als zusätzliche unabhängige Umdrehungen für Kopierschutzhinweise. |
| RETRY_MISSING ohne EXPECT_SET | BAD_ARGS. EXPECT_SET benötigt eine nichtleere Sektormaske; ohne EXPECT_SET muss sie0 sein. |
| Policies2–4 ohne RETRY_MISSING oder RETRY_ERRORS | BAD_ARGS. Policy1 steuert Wiederholungen selbst; sie benötigt genügend Versuche für die verlangten Bestätigungen. |
| Policy4 mit nur einer Umdrehung je Versuch | BAD_ARGS. Ab zwei Umdrehungen zulässig; REV_COMPARE wird automatisch angefordert. |
| WRITE Inhaltsprüfung Bit0 + Weak-Marker `flags2` Bit3 | BAD_ARGS. Weak-Marker benötigen den erweiterten Schreibpfad. |
| WRITE Inhaltsprüfung Bit0 + Rohprüfung `flags2` Bit4 | BAD_ARGS. Dies sind alternative Auftragsarten. Das zusätzliche Op31-Timingprofil darf dagegen mit Inhaltsprüfung laufen. |
| Weak-Marker + Rohprüfung `flags2` Bit4 | Zulässig im erweiterten Pfad mit Quell-Indexperiode; Referenz entsteht nach der Weak-Verarbeitung aus der tatsächlich erzeugten Vorlage. Keine Bestätigung vollständiger Sektorinhalte. |
| Extended-WRITE + Viertelspur-/Skalierungs-/CLAMP-Bits oder Präkompensation | NOT_IMPL; diese Optionen gehören zum klassischen Writer. Extended-Daten sind bereits durch den Sender zeitlich festgelegt. |
| Op31 REQUIRE_TIMING_MATCH ohne RAW_TIMING_PROFILE | BAD_ARGS. Bei Rohprüfung `flags2` Bit4 gilt der konfigurierte Pflichtvergleich ebenfalls. |
| Op50 strenger Vergleich ohne Aktivierung | BAD_ARGS. Aktivierung reserviert U11-Speicher und verkleinert den gemeinsamen Capture-/Write-Puffer. |
| Op50 aktiv + geprüftes WRITE mit Splice-Offset, Gate-Vor-/Nachlauf, klassischem Pfad oder fehlender Quellperiode | Vor WGATE abgelehnt; siehe4.22. Ohne angefordertes Prüflesen wird durch Op50 allein kein Prüflesen gestartet. |
| Decoder-Toleranzen + Fehler-/Kopierschutzanalyse | Kombinierbar. Annahme beschädigter Sektoren bedeutet keine Reparatur: Fehlerzähler und Befundbits bleiben maßgeblich. |
| Op55 + zu wenige Aufnahmeumdrehungen / zu kleines Zeitbudget | Kein zusätzlicher Leseauftrag durch die Flags allein. Bericht meldet unzureichende Abdeckung beziehungsweise die Analyse endet mit Timeout. |

Mehr angeforderte Auswertung benötigt zusätzliche Rechenzeit und Berichtskapazität.
Ein Zeitbudget muss dazu passen. Strenge Op31-/Op50-Vergleiche können absichtlich
einen Fehler liefern, obwohl ein Sektorinhalt erfolgreich dekodiert wurde. Ihre
Toleranzen sind verschieden: Op31 in Promille, Op50 in Flux-Ticks. Das Tool muss
Inhaltsbefund, Timingbefund und Fehlerstatus getrennt auswerten.

Greaseweazle besitzt eigene Befehle/Flags. Die nativen Analysemasken werden dort
nicht in bestehende Greaseweazle-Flags eingebettet.

## 4 Laufwerk und Medien

### 4.1 Aufnahmebelege und zusätzliche Laufwerkszeiten (INFO Bit25)

Diese Erweiterung verwendet ausschließlich native UFI-Kommandos. Greaseweazle-
Befehle, Antworten und Streamtokens bleiben unverändert. Die Daten entstehen in
der Firmware; das Tool kann Lochfolgen, Transportpausen und Aufnahmebedingungen
formatunabhängig auswerten. Es wird keine automatische Hard-Sector-Erkennung behauptet.

Zwei neue Flags genügen: `RAW_INDEX=0x0008` und `TRANSFER_TRACE=0x0010`.
Sie gehören in die **Jobflags**, nicht in ANALYZE-Flags, Diagnoseflags oder WRITE-Flags.
Bei Analysejobs gelten sie für jeden Versuch; nur die Belege des letzten Versuchs
bleiben erhalten. Der 160-Byte-Aufnahmekontext entsteht auch ohne diese Flags.

Für natives READ, synchrones ANALYZE und WRITE-Prüflesen setzt Op 7 die
Diagnoseaufnahmeflags: Request `[7]` liest, `[7, flags u16]` setzt. Nur Maske
`0x0018` erlaubt; unbekannte Bits ergeben BAD_ARGS, Setzen bei laufenden Operationen
oder offenen Integritätsjobs BUSY. Antwort 16 Bytes: Schema 1 bei Offset 0,
konfigurierte Flagmaske u8 bei 1, letzte Capture-ID u32 bei 4, Indexlimit 240
bei 8 und Transportlimit 48 bei 9; übrige Bytes 0. Defaultflags sind 0.
Einstellungen bleiben bis zum nächsten Setzen oder Neustart erhalten. Ein
Analysejob verwendet seine eigenen Flags und verändert diese Defaults nicht.

#### Belege abrufen: Op 8 oder Jobabschnitte 4–6

Op-8-Request exakt 12 Bytes: `8 u8, capture_id u32, section u8, offset u32,
count u16`. Capture-ID muss ungleich 0 und aktuell sein; sonst BAD_ARGS.
Abschnitt 0 = Kontext, 1 = Rohindex, 2 = Transporthistorie, 3 = Laufwerks-/Medienstatus
beim Aufnahmestart (40 Bytes, ab INFO Bit27; 4.13). Während einer
Operation BUSY. `count=1..1024`; Offset größer als Gesamtlänge ergibt RANGE,
Offset gleich Länge eine leere Seite. Antwortheader 24 Bytes:
`schema u8=1, section u8, reserved u16=0, capture_id u32, capture_id u32,
offset u32, total_size u32, payload_crc32 u32`. Danach Nutzdaten. CRC32 wie bei
Op 3; beide ID-Felder enthalten hier die Capture-ID.

Bei Analysejobs liefern Op-3-Abschnitte 4/5/6 dieselben Inhalte, mit dem normalen
Job-/Generationsheader. Capture-ID steht im Jobstatus bei Offset 60. Eine neu
begonnene Aufnahme ersetzt alle Aufnahmebelege; alte Jobabschnitte 4–6 ergeben
dann BUSY. Vor der nächsten Aufnahme abrufen und im Tool archivieren. IDs gelten
nur innerhalb des laufenden Firmwarestarts; nach Neustart neu abfragen.

#### Kontext: 160 Bytes, Schema 1

Der Snapshot entsteht nach Seek/Seitenwahl vor dem Start der Aufnahme.
Alle nicht genannten Bytes sind 0. Zeiten sind Nominalwerte, keine gemessene
Kalibrierung. CRC32 ist eine Vergleichshilfe, keine kryptographische Identität.

| Offset | Typ | Inhalt |
|---:|---|---|
| 0–3 | je u8 | Schema 1, Boardrevision, Diagnoseaufnahmeflags, Laufwerkstyp |
| 4 | u32 | Capture-ID |
| 8 | u32 | Nominaler Flux-Takt, 275000000 Hz |
| 12 | u32 | Startzeit in HAL-ms seit Boot, mit u32-Überlauf |
| 16 | u64 | Flux-Timerstand beim Snapshot; nicht der erste Index |
| 24–27 | je u8 | Track, Seite, angeforderte Umdrehungen, PSRAM aktiv (0/1) |
| 28 | u32 | Nutzkapazität des Fluxspeichers in u32-Wörtern |
| 32 | u32 | Synthetische Umdrehungsperiode in Ticks, 0 bei Indexsteuerung |
| 36 | u32 | Indexmaskierung in Ticks |
| 40, 41 | je u8 | Konfigurierte Hard-Sector-Zahl, interne Indexquelle aktiv |
| 42, 43 | je u8 | Logischer Zustand DENSITY, DRATE (1 = asserted) |
| 44, 46 | je u16 | Angeforderte Laufwerks-RPM (0 = nicht gesetzt), Indexsimulator-RPM |
| 48 | 11 × u16 | Bestehendes DRIVE_TIMING in dessen Wire-Reihenfolge |
| 70 | 6 × u16 | Zusatzzeiten einschließlich reserviertem Nullfeld, siehe Op 10 |
| 84 | u32 | CRC32 über Kontextbytes 24 bis einschließlich 81 |
| 88 | 24 Bytes | Firmwareversion, NUL-terminiert, bei Bedarf gekürzt |
| 112 | 32 Bytes | Build-/Git-Kennung, NUL-terminiert, bei Bedarf gekürzt |
| 144, 148 | je u32 | Globale Capture-Overruns und Capture-DMA-Fehler beim Start |
| 152, 156 | je u32 | Globale native RX-Pausen und TX-Timeouts beim Start |

Die letzten vier Felder sind **Ausgangszähler**, keine Fehlerzahlen dieser
Aufnahme. Der Kontext bleibt beim Abschluss unverändert.

#### Rohindex und Transporthistorie

Beide Abschnitte beginnen mit 32 Bytes: Schema u8=1 bei 0, Abschnitt u8 bei 1
(1 Rohindex, 2 Transport), Flagmaske u8 bei 2, aktiv u8 bei 3, Capture-ID u32
bei 4, Anzahl u32 bei 8, verworfene Einträge u32 bei 12, Eintragsgröße u32 bei
16, Kapazität u32 bei 20; Bytes 24–31 sind 0. Danach folgen genau `Anzahl`
Einträge. Volle Puffer behalten die ersten Einträge und zählen weitere bis
UINT32_MAX als verworfen. Auch ohne aktiviertes Flag existiert ein leerer Header.

**Rohindex:** maximal 240 Einträge zu 16 Bytes. Je Eintrag: Timerstand u64,
DMA-Ringposition u32, Grundmaske u32. Gründe: Bit0 passiert Indexfilter,
Bit1 durch Zeitmaske verworfen, Bit2 Sektorloch/Filter-Warmup, Bit3 interne
Indexquelle, Bit4 physischer Impuls bei gewählter interner Quelle ignoriert.
Bit3 ergänzt den jeweiligen Filtergrund. Im periodengesteuerten Modus bedeutet
Bit0 nicht automatisch eine Umdrehungsgrenze. Zeit und Position stammen aus
dem Indexpfad; der DMA-Cursor ist eine Näherung beim ISR-Aufruf, keine exakt
zugeordnete Fluxkante. Elektrisch weggefilterte oder vom Interruptsystem nicht
erfasste Impulse können nicht rekonstruiert werden.

**Transport:** maximal 48 Einträge zu 24 Bytes. Je Eintrag: elapsed_ms u32,
produzierte Fluxwörter u32, ausstehende Pufferwörter u32, an natives USB
übergebene Rahmenbytes u32, Grundmaske u32, reserviert u32=0.
Gründe: Bit0 Start, Bit1 periodische Probe, Bit2 USB-Übergabe,
Bit3 USB belegt/Rückstau, Bit4 Abschluss, Bit5 Capture-Fehler beim Abschluss.
USB-Beobachtungen werden zusammengefasst; reguläre Proben frühestens alle 20 ms.
USB-Bytes enthalten Rahmen und gegebenenfalls Integritätsheader; sie bestätigen
keinen Empfang im Tool. Analyse ohne Fluxstream hat entsprechend keine
gesendeten Fluxrahmen. Ohne Streaming ist der Pufferfüllstand keine USB-Warteschlange.
Bei längeren Aufnahmen können auch Abschlussproben verworfen werden; dann
Verlustzähler beachten. END-/Jobstatus bleibt die maßgebliche Fehlerauskunft.

Auch ein expliziter Aufnahmestopp friert die Historie ein. Die letzte Probe
entsteht vor dem Abschalten des Capture-Pfads, solange der DMA-Cursor noch
gültig ist. Wiederholter Stopp verändert sie nicht. Das Abschlussbit alleine
unterscheidet normalen Abschluss und Abbruch nicht; dafür END-/Jobstatus lesen.

#### Op 10: zusätzliche Laufwerkszeiten

Request `[10]` liest. Setzen: `[10, schema u8=1, sechs u16]`, exakt 14 Bytes.
Antwort in beiden Fällen: `[schema u8=1, sechs u16]`, 13 Bytes. Die sechs Werte:

| Reihenfolge | Parameter | Bereich | Default |
|---:|---|---:|---:|
| 0 | DENSITY-/DRATE-Umschaltwartezeit, µs | 0–60000 | 0 |
| 1 | Mindestzeit Motorstart bis erster Schritt, ms | 0–2000 | 0 |
| 2 | Zusätzliche Beruhigungszeit vor Schreiben, µs | 0–60000 | 20000 |
| 3 | Schreibfreigabe-Vorlauf, µs | 0–200 | 0 |
| 4 | Schreibfreigabe-Nachlauf, µs | 0–200 | 0 |
| 5 | Reserviert | nur 0 | 0 |

Ungültige Werte ergeben BAD_ARGS ohne Teiländerung. Setzen bei aktiver Operation
oder offenem Integritätsjob ergibt BUSY. Die Werte gelten global bis zum nächsten
Setzen oder Neustart; keine automatische SD-Speicherung. Bestehendes DRIVE_TIMING
bleibt erhalten. Motorwartezeit berücksichtigt bereits verstrichene Spinupzeit.
SET_LINES setzt beide Leitungen vor der zusätzlichen Wartezeit; Density-, RPM-
und TG43-Umschaltungen berücksichtigen ebenfalls die Umschaltwartezeit.

Vorlauf/Nachlauf erzeugen stille DMA-Wellenformsegmente bei aktivem WGATE und
gelten nur für **nativen erweiterten WRITE am 34-Pin-Port** (WRITE Bit5).
Vorlauf verschiebt die erste Fluxkante gegenüber WGATE, Nachlauf folgt dem
letzten Schreibpuls. Klassischer nativer WRITE und Apple-WRITE weisen gesetzte
Randzeiten vor WGATE mit NOT_IMPL zurück. Indexbegrenzung und bestehende
Schutzprüfungen bleiben wirksam; Randzeiten dürfen keine gewünschte Spurgrenze
überschreiten. Greaseweazle verwendet weiterhin seine eigenen Pre-/Post-Werte
und die bisherige 20-ms-Schreibvorbereitung. Gemeinsame Laufwerksparameter
wirken weiterhin auf gemeinsam genutzte Motor-/Leitungssteuerung.

Empfohlener Toolablauf: INFO Bit25 und Op 0 prüfen, Zusatzzeiten bei Bedarf setzen,
für Standardaufnahmen Op 7 mit `0x0018` setzen oder bei Analysejobs diese Bits
in Jobflags aufnehmen; Aufnahme abwarten, aktuelle Capture-ID ermitteln und
Kontext/Index/Transport paginiert abrufen. Verlustzähler sichtbar anzeigen.
Die Tests prüfen Protokoll, Grenzen, Zeitüberlauf, Indexfilter und Wellenformen
offline. Elektrische Zeiten und Datensicherheit beim echten Schreiben müssen
mit dem fertigen Board gemessen werden.

### 4.2 Positionsgültigkeit, Laufwerksprofile und Mediengeneration (INFO Bit27)

Implementiert am 2026-10-15. Eigenständige UFI-Implementierung der im Review von
`skiselev/floppy_bios` beschriebenen Zustandskonzepte. Keine Übernahme von
BIOS-Maschinencode oder FDC-/ISA-Registerzugriffen. Die Erweiterung betrifft
direkt angeschlossene Fluxlaufwerke; IEC-Geräte erhalten dadurch keine
erfundene Kopfposition oder Wechselanzeige. Neue **Statusbits** statt weiterer
Analyseflags; Greaseweazle-Wire-Befehle und dessen Statusnummern bleiben erhalten.

#### Op 13: 40-Byte-Status

Request `[13]`; alle Mehrbytewerte Little Endian. Antwort:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Ausgewählter Laufwerkstyp gemäß bestehender DRIVE-Aufzählung |
| 2 | u16 | Statusbits, siehe unten |
| 4 | u32 | Mediengeneration; 0 bei NONE/IEC |
| 8, 9 | je u8 | Mechanischer Spurzähler, ausgewählte Seite |
| 12 | 8 Bytes | Laufwerksprofil |
| 20 | 12 Bytes | Medienprofil |
| 32 | u8 | Letzter Invalidierungsgrund: 0 Initialisierung, 1 beobachtete Wechselanzeige, 2 manuelle Meldung, 3 Laufwerksprofil geändert |

Übrige Bytes 0. Die Statusbits sind:

| Bit / Wert | Name | Bedeutung |
|---|---|---|
| 0 / 0x0001 | POSITION_VALID | Positionszählung gilt nach erfolgreicher Kalibrierung/Positionierung als gültig |
| 1 / 0x0002 | MEDIA_CHANGE_SUPPORTED | Wechselanzeige ist für dieses Laufwerk konfiguriert und über den Port auswertbar |
| 2 / 0x0004 | MEDIA_CHANGE_ASSERTED | Letzter ausgewerteter Pegel der Wechselanzeige war aktiv |
| 3 / 0x0008 | MEDIA_PROFILE_VALID | Ein deklariertes oder aus Analysebefunden abgeleitetes Medienprofil liegt vor |
| 4 / 0x0010 | POSITION_ESTIMATED | Apple Disk II: Position aus Schrittzählung/Anschlagreferenz, ohne Track-0-Sensor |

POSITION_VALID ist keine unabhängige Messung jeder Spur. Ohne dieses Bit ist der
Spurzähler kein verlässliches Seekziel. MEDIA_PROFILE_VALID bestätigt keine
vollständig fehlerfreie Diskette; Herkunft und unbekannte Geometriefelder beachten.
Bei NONE/IEC sind Statusbits und Mediengeneration 0.

#### Laufwerksprofil: Op 14

`[14]` liest den Status, `[14, profile[8]]` setzt das Profil des ausgewählten
Fluxlaufwerks. Beide antworten mit dem vollständigen 40-Byte-Status.

Profilbytes: 0 Schema=1; 1 höchste physische Spur 6–83; 2 Kopfzahl 1–2;
3 Wechselanzeige verwenden 0/1; 4–5 nominale RPM u16 (0 unbekannt, 300 oder 360);
6–7 reserviert=0. Apple erlaubt höchstens Spur 39, einen Kopf und keine
Wechselanzeige. An den Shugart-DS-Ports ist Pin 34 READY; deren Wechselanzeige
kann nicht aktiviert werden. Ungültige Kombinationen ergeben BAD_ARGS.

Defaults: maximale Spur 83, zwei Köpfe, RPM unbekannt; Apple Spur 39/ein Kopf.
Wechselanzeige ist bei PC A/B und Amiga vorbelegt, bei Apple und Shugart-DS aus.
Das ist eine Anschlussannahme, keine automatische Erkennung der Laufwerkshardware.
Bei einem PC-Laufwerk ohne zuverlässige Wechselanzeige muss das Tool Byte 3 auf 0
setzen. Nominale RPM sind Metadaten; die vorhandene RPM-Steuerung bleibt separat.
Maximalspur und Kopfzahl werden bei regulären Seek-/Seitenwahloperationen geprüft.
Der explizite PROBE_TRACKS-Endanschlagtest bleibt ein gesonderter Diagnosepfad.

Profiländerung macht die Position ungültig, verwirft das Medienprofil und vergibt
eine neue Generation. Vor dem nächsten normalen Seek wird neu kalibriert.
Grenzen ersetzen keine mechanische Vermessung des Laufwerks.

#### Medienprofil: Op 15

`[15]` liest den Status, `[15, profile[12]]` deklariert das Medienprofil.

| Profiloffset | Typ | Bedeutung |
|---:|---|---|
| 0 | u8 | Format 1 MFM, 2 Amiga, 3 FM, 4 HP-MMFM, 5 CBM-GCR, 6 Macintosh-GCR (5.4), 7 Apple-II-5,25-Zoll-GCR (5.7); 0 löscht das Profil |
| 1 | u8 | Zylinderzahl 1–84 oder 0 unbekannt |
| 2 | u8 | Kopfzahl 1–2 oder 0 unbekannt |
| 3 | u8 | Sektoren je Spur 1–64 oder 0 unbekannt/variabel |
| 4 | u8 | Sektorgrößencode N=0–3; bei automatisch erkanntem Profil nicht als Geometrienachweis verwenden |
| 5 | u8 | Deklarierter mechanischer Schrittfaktor 1 oder 2 |
| 6 | u8 | Herkunft: 1 Hostdeklaration; 2 Firmwarebefund (nur Antwort) |
| 7 | u8 | Reserviert=0 |
| 8 | u32 | Daten-/Kanalbitrate gemäß Format |

Bei Format 0 müssen alle zwölf Bytes 0 sein. Setzen verlangt Herkunft 1.
Raten: MFM/Amiga 250/300/500/1000 kbit/s, FM 125/150/250/300/500 kbit/s,
HP-MMFM 500 kbit/s, GCR 250000/266667/285714/307692 bit/s. Deklarierte Geometrie
muss innerhalb des Laufwerksprofils liegen: `(cylinders-1)*step_factor <= max_track`.
Das Profil ist eine **Beschreibung**, kein Befehl zur Formatierung, RPM- oder
Density-Umschaltung. Den tatsächlichen Double-Step-Modus weiterhin über
DRIVE_TIMING einstellen; der Host muss Beschreibung und wirksame Einstellungen
konsistent halten. Bei zonierten Formaten kann eine einzelne Rate nur den
deklarierten beziehungsweise zuletzt untersuchten Bereich beschreiben.

Ein erfolgreich ausgewerteter Analysejob mit Sektorfunden und eindeutiger
Format-/Ratenwahl darf ein leeres oder zuvor automatisch erkanntes Profil
aktualisieren: Format, Rate, aktueller Schrittfaktor, Herkunft 2. Zylinder-,
Kopf- und Sektorzahl bleiben unbekannt=0. Eine Hostdeklaration wird nicht ersetzt.
Dies ist ein Spur-Befund und kein Scan der ganzen Diskette.

Op-14-/15-Setzen und Op 16 sind bei aktiven Operationen oder offenen
Integritätsjobs mit BUSY gesperrt. Lesende Abfragen bleiben möglich. Einstellungen
sind pro Laufwerksauswahl gespeichert, aber flüchtig bis Neustart. Bestehende
Laufwerkszeiten bleiben die bisherigen globalen Parameter.

#### Mediengeneration und Abbruch

Die Generation ist ein sitzungsbezogener, laufwerksübergreifend vergebener
u32-Zähler ungleich 0, keine Disk-ID oder dauerhafte Seriennummer. Bei Neustart
beginnt ein neuer Namensraum. Eine beobachtete inaktive→aktive Wechselanzeige
vergibt eine neue Generation und löscht das Medienprofil. Ein dauerhaft aktiver
Pegel zählt einmal; erst nach beobachteter Freigabe ist ein weiterer Übergang
erkennbar. Sampling erfolgt beim ausgewählten Laufwerk mit eingeschaltetem Motor,
unter anderem vor STEP, da STEP die Hardware-Wechselanzeige löschen kann.
Kurze, zwischen Abfragen liegende Wechsel und mehrere Wechsel bei gehaltenem
Latch können nicht unterschieden werden. Ein deaktiviertes/nicht vorhandenes
Signal darf niemals als Beweis „Diskette unverändert“ verwendet werden.

`[16]` meldet einen Medienwechsel manuell im Leerlauf und liefert den Status.
Das Tool soll dies bei bekanntem Wechsel ohne auswertbares Signal verwenden.
Der Befehl löscht das Medienprofil und erhöht die Generation; er bewegt den
Kopf nicht und quittiert den Hardware-Latch nicht. Bei NONE/IEC keine Wirkung.

Native Aufnahme, Analysejob und nativer WRITE merken sich die Mediengeneration.
Eine beobachtete Abweichung oder andere Laufwerksauswahl beendet die geschützte
Operation mit Status 21. WRITE prüft bereits vor dem Schreiben und zusätzlich
im Ablauf; dabei wird WGATE beim Abbruch freigegeben. Die Prüfung erfolgt durch
Software-Polling, ist kein physischer Schreibschutz und kann bereits geschriebene
Teile nicht rückgängig machen. Greaseweazle-Rohoperationen erhalten keine neuen
Statuswerte. Separate ERASE/PATTERN-Kommandos und komplette mehrspurige Dump-/
Formatabläufe besitzen dadurch keine transaktionsweite Mediengarantie.

Ein eingefrorener Status gehört als Jobabschnitt 9 zum Jobstart. Abschnitt 10
gehört zum Start der letzten Aufnahme und wird bei einer neuen Capture-ID
ungültig. Derselbe Aufnahmestatus ist über Op 8, Abschnitt 3 abrufbar, auch bei
Standardaufnahmen. Die bisherigen 160 Kontextbytes bleiben unverändert.
Aktueller Op-13-Status darf sich nach einem Wechsel unterscheiden; alte Berichte
bleiben zur Dokumentation ihres ursprünglichen Mediumzustands abrufbar.

Nach fehlgeschlagener Kalibrierung, unerwartetem Track-0-Signal und Sicherheits-/
Protokollreset gilt die Position als unbekannt. Ein normaler Seek kalibriert dann
neu. Erreicht die Rückwärtsbewegung Track 0, obwohl eine andere Spur angefordert
war, antwortet die Firmware mit SEEK_FAIL statt Erfolg auf der falschen Spur.
Apple-Positionen behalten ihre gesonderte Kennzeichnung ohne Track-0-Sensor.

### 4.3 Optionale Profilprüfung (2026-10-16)

Unterstützung über Op 0, Jobflagmaske bei Offset 4 erkennen: Bit 5
`CHECK_PROFILE=0x0020`. Keine Änderung am Greaseweazle-Protokoll. Das Flag
gehört zur Jobflagmaske, nicht zu Qualitäts-, Evidence- oder Recovery-Masken.
Es lässt sich mit den bisherigen Jobflags kombinieren.

Vor dem Analysejob Laufwerksprofil (Op 14), Medienprofil (Op 15) und wirksame
DRIVE_TIMING-Einstellungen setzen. Die Prüfung ändert keine Einstellungen,
schaltet keine Drehzahl um und schreibt nicht auf das Medium. Sie vergleicht
den eingefrorenen Aufnahmezustand mit den Befunden der Analyse. Ein Konflikt
ist ein Berichtsbefund und erzeugt keinen zusätzlichen Fehlerstatus oder Retry.

Bericht über Op 3, Abschnitt 11 mit Job-ID und Berichtsgeneration abrufen.
Ohne Flag, vor Abschluss oder nach fehlgeschlagenem letztem Versuch: BUSY.
Jeder neue Versuch verwirft den alten Prüfbericht. Der fertige Bericht bleibt
auch nach einer späteren unabhängigen Aufnahme gespeichert.

| Offset | Typ | Bedeutung |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Wirksamer Schrittfaktor 1/2; 0 ohne Aufnahmekontext |
| 2 | u8 | Eindeutig erkanntes Format; 0 ohne ausreichenden Sektorbefund |
| 3 | u8 | Feste Toleranz der Indexperiode: 20 Promille |
| 4 | u32 | Ergebnisbits |
| 8 | u32 | Ursachenmaske |
| 12 | u32 | Mediengeneration der Aufnahme |
| 16 | u32 | Erwartete RPM aus Laufwerksprofil; 0 unbekannt |
| 20 | u32 | Mittlere physische Indexperiode in Timerticks; 0 nicht verwertbar |
| 24 | u32 | Timerfrequenz in Hz |
| 28 | u32 | Eindeutig erkannte Bitrate; 0 unbekannt |

Alle Mehrbytefelder Little Endian. Ergebnisbits sind unabhängig kombinierbar:

| Wert | Name | Bedeutung |
|---:|---|---|
| 1 | RPM_MISMATCH | Mittlere Indexperiode liegt mehr als 2 Prozent außerhalb der erwarteten Periode |
| 2 | PROFILE_CONFLICT | Mindestens ein überprüfbarer Profilparameter widerspricht Einstellungen/Befunden |
| 4 | INSUFFICIENT_EVIDENCE | Mindestens eine Prüfgrundlage fehlt |

Ursachenmaske: 1 Schrittfaktor abweichend; 2 deklarierte Geometrie überschreitet
bei wirksamem Schrittfaktor die Laufwerksgrenzen; 4 Format abweichend;
8 Bitrate abweichend; 16 Medienprofil fehlt; 32 physische Indexperiode fehlt;
64 eindeutiger Format-/Ratenbefund fehlt; 128 erwartete RPM fehlt;
256 Aufnahmeeinstellungen fehlen. RPM_MISMATCH wird direkt im Ergebnis gemeldet.

Simulierte Indexsignale, Zeitfensteraufnahmen und konfigurierte Hard-Sector-Modi
liefern keine Drehzahlbestätigung. Die mittlere Periode stammt ausschließlich
aus den ausgewerteten Umdrehungen; Momentanschwankungen werden hier nicht bewertet.
GCR-Zonenraten werden nicht gegen eine einzelne globale Medienrate geprüft.
Ein Ergebnis 0 bestätigt nur die durchgeführten Vergleiche, nicht die komplette
Geometrie, Fehlerfreiheit oder mechanische Eignung der Diskette.

Beispielkombinationen für IBM MFM (Rate am tatsächlich verwendeten Laufwerk):

| Medium / Laufwerk | Erwartete RPM | Bitrate | Schrittfaktor |
|---|---:|---:|---:|
| 360 KB / 40-Spur-Laufwerk | 300 | 250000 | 1 |
| 360 KB / 1,2-MB-Laufwerk mit 360 RPM | 360 | 300000 | 2 |
| 720 KB / 3,5-Zoll-DD | 300 | 250000 | 1 |
| 1,44 MB / 3,5-Zoll-HD | 300 | 500000 | 1 |
| 1,2 MB / 5,25-Zoll-HD | 360 | 500000 | 1 |

Beim zweiten Beispiel bleiben die Sektor-IDs logisch 0–39; der mechanische
Schrittfaktor bildet sie auf physische Spuren 0–78 ab. Diese Kombinationen
sind Prüffälle, keine automatisch angewendeten Hardwarepresets. Die Firmware
erzwingt keine PC-Geometrien für Amiga, FM, HP-MMFM oder Sonderformate.

### 4.4 Portbelegung und gespeicherte Betriebsprofile (INFO Bit28, 2026-10-16)

Eigenständige Erweiterung nach dem Review von ISA FDC / Monster FDC; keine
Übernahme von ISA- oder BIOS-Code. Alle Mehrbytefelder Little Endian.

#### Op 17: Portbericht

Request `[17]`, Antwort 24 Bytes:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Ausgewählter Laufwerkstyp |
| 2 | u8 | Portmerkmale, siehe unten |
| 3 | u8 | Select-Maske: PC A/B = 3; Shugart DS0–DS3 = 15; andere = 0 |
| 4 | u8 | Konfigurierte Drehzahlleitung: 0 keine, 1 DENSITY, 2 DRATE |
| 5 | u8 | Drehzahlpolarität: 1 = aktiv/Low bedeutet 360 RPM, 0 = High bedeutet 360 RPM |
| 6 | u8 | TG43 konfiguriert |
| 7 | u8 | Aktuelle Konfliktmaske |
| 8 | u16 | Zuletzt gesetzte Drehzahl; 0 nicht gesetzt, kein Messwert |
| 10 | u8 | Gespeichertes Betriebsprofil vorhanden (0/1) |
| 11 | u8 | Reserviert 0 |
| 12 | u32 | Untergrenze normaler Schreibintervalle in Timerticks |
| 16 | u32 | Obergrenze normaler Schreibintervalle in Timerticks |
| 20 | u32 | Timerfrequenz in Hz |

Portmerkmale bei Offset 2: Bit0 gemeinsamer Fluxpfad; Bit1 gemeinsame
Motorleitung der Shugart-DS-Gruppe; Bit2 DRATE für DS3 reserviert;
Bit3 DENSITY-Ausgang; Bit4 Wechselanzeige konfiguriert;
Bit5 Pin 34 ist READY; Bit6 zusätzliche Jumperverbindung erforderlich
(DS3: JP1, Amiga2: JP3). Die Select-Maske beschreibt die Anschlussart,
keine erkannten Laufwerke. Jumper werden nicht elektrisch erkannt. Ebenso
beschreibt die Tabelle keine automatische Erkennung angeschlossener Kabel.

Konfliktmaske: 1 DRATE als Drehzahlleitung auf einem Shugart-DS-Bus;
2 DENSITY gleichzeitig für Drehzahl und TG43; 4 Drehzahlleitung/TG43 auf
NONE, IEC oder Apple. DRATE ist auf der gesamten DS-Gruppe reserviert,
auch wenn gerade DS0 statt DS3 ausgewählt ist.

#### Op 18: Betriebseinstellungen pro Laufwerk

`[18]` liest die **wirksamen** Einstellungen. `[18, profile[40]]` validiert,
wendet an und speichert das Profil für das ausgewählte Fluxlaufwerk.
Beide antworten mit 40 Bytes. NONE/IEC: NO_DRIVE; aktive Operation oder
Integritätsjob beim Setzen: BUSY. Ungültige Profile: BAD_ARGS ohne Änderung.

| Profiloffset | Länge | Inhalt |
|---:|---:|---|
| 0 | 1 | Schema 1 |
| 1 | 22 | DRIVE_TIMING: bisherige elf u16-Felder in bestehender Reihenfolge |
| 23 | 12 | Zusätzliche Laufwerkszeiten: sechs u16 wie Op 10, ohne dessen Schemabyte |
| 35 | 1 | Drehzahlleitung 0/1/2 |
| 36 | 1 | Polarität 0/1 wie Op 17 |
| 37 | 2 | Ziel-RPM: 0 ohne Leitung; 300 oder 360 mit Leitung |
| 39 | 1 | Request: 0; Antwort: gespeichertes Profil vorhanden (0/1) |

Schrittfaktor und TG43 müssen 0/1 sein, STEP-Pulsdauer ungleich 0.
Für Zusatzzeiten gelten die Grenzen von Op 10. Leitung und Ziel-RPM müssen
zusammenpassen; die oben aufgeführten Leitungskonflikte werden abgewiesen.
Bei aktivem Motor wartet eine gesetzte Drehzahl die konfigurierte Anlaufzeit.
Unbelegte DENSITY/DRATE-Ausgänge werden beim Anwenden freigegeben; DRATE wird
auf dem Shugart-DS-Bus nicht verändert. TG43 berücksichtigt eine gültige
physische Kopfposition. Apple erhält keine Änderungen an diesen FDD-Ausgängen.

**Zeit-Voreinstellungen (1.18):** `[18, id]` (exakt 2 Bytes) setzt die
Bewegungs- und Beruhigungszeiten auf einen benannten Satz und antwortet wie
`[18]` mit den wirksamen 40 Bytes. Nur `step_rate_us`, `settle_us`,
`dir_change_us`, `side_settle_us`, `spinup_ms`, `select_settle_us` sowie die
Zusatzzeiten 0 (Umschaltwartezeit) und 1 (Motorstart bis erster Schritt) ändern
sich; STEP-Pulsdauer, Motor-Aus, Schrittfaktor, Precomp, TG43, Schreibberuhigung,
Schreibfreigabe-Ränder, Drehzahlleitung und das gespeicherte Profil bleiben
unverändert. Zum Speichern anschließend `[18, profile[40]]` senden. NONE/IEC:
NO_DRIVE; unbekannte Kennung: BAD_ARGS; aktive Operation oder Integritätsjob:
BUSY.

| id | Name | step_rate | settle | dir_change | side_settle | spinup | select_settle | Umschalt | Motor→Schritt |
|---:|---|---:|---:|---:|---:|---:|---:|---:|---:|
| 1 | Standard | 3 ms | 15 ms | 15 ms | 200 µs | 500 ms | 10 ms | 0 | 0 |
| 2 | Konservativ | 8 ms | 40 ms | 38 ms | 1 ms | 800 ms | 60 ms | 60 ms | 200 ms |
| 3 | 8 Zoll (1.18-dev) | 8 ms | 20 ms | 20 ms | 1 ms | 1000 ms | 20 ms | 0 | 100 ms |

**8 Zoll** (SA800/850-Klasse, BASF 6102 und Verwandte): diese Laufwerke verlieren bei den
3 ms der Standardvorgabe Schritte, genau das Fehlerbild eines PC-Controllers an einem
8-Zoll-Laufwerk; 8 ms Schritt und 20 ms Beruhigung sind der sichere Bereich. Drei
Voraussetzungen, die keine Voreinstellung ersetzt: (1) Der 34-polige Bus hat keine
HEAD-LOAD-Leitung; der 50-auf-34-Adapter legt HLD auf MOTOR oder SELECT, deshalb wartet die
Voreinstellung 100 ms vom Motor bis zum ersten Schritt. (2) Die Terminierung dieser
Laufwerke ist 150–220 Ω (teils Thevenin 220/330 Ω) und verlangt rund 23 mA Senkstrom je
Leitung; die UFI Headless v0.7 treibt den Bus mit 74LS07-Open-Collector-Stufen (40 mA) und
erfüllt das, anders als viele Super-I/O-Controller. (3) Drehzahl 360 U/min, FM 250 kbit/s
und MFM 500 kbit/s: `rpm_line` beziehungsweise die Datenrate wie beim 1,2-MB-Laufwerk
wählen; hartsektorierte 8-Zoll-Medien mit 32 Löchern über READ/WRITE Bit1.

„Konservativ" entspricht den Standardwerten der KryoFlux-Firmware 3.00
(`REA_Firmware_Analyse_2026-10-17.md`); deren Dichte-Wartezeit von 500 ms ist
durch die Feldgrenze auf 60 ms gekappt. Gedacht für alte oder langsame
Laufwerke, bei denen Spurfehler nach Richtungsumkehr oder nach dem Positionieren
auftreten.

Das gespeicherte Profil wird bei **nativer SELECT-Auswahl** dieses Laufwerks
wieder angewendet. Das gilt für v2 und das ältere native Protokoll.

**Ablage auf SD (1.18):** Jedes erfolgreiche `[18, profile[40]]` schreibt alle
gespeicherten Profile in `UFIPROF.BIN` im Wurzelverzeichnis der SD (8 Byte Kopf
`UFIPROF` + Schema 1, dann je Laufwerkstyp 40 Byte in Enum-Reihenfolge, dann
CRC-32 (IEEE) über alles davor; 492 Byte). Schlägt das Schreiben fehl, bleibt
das Profil im RAM wirksam und gespeichert, die Antwort ist der Speicherstatus
(STORAGE oder der SD-Fehler). Beim Start werden die Profile nach `UFI.CFG`
gelesen; Datei fehlend, falsche Größe, falsche CRC oder Schema → keine Profile;
ein einzelner Eintrag, der die laufwerksunabhängige Formprüfung nicht besteht
(Schema, STEP-Puls, Schrittfaktor/TG43 0/1, Zusatzzeitengrenzen, Leitung/RPM
passend), wird verworfen. Die laufwerksabhängige Prüfung erfolgt weiterhin beim
Anwenden durch SELECT. Zum Vergessen aller Profile die Datei löschen
(`ufi rm UFIPROF.BIN`) und neu starten; Zeit-Voreinstellungen und einzelne
TIMING-/Op-10-Änderungen schreiben die Datei nicht.
Ohne gespeichertes Profil bleibt das bisherige Verhalten mit gemeinsamen
Einstellungen erhalten. Einzelne TIMING-/RPM-/Op-10-Änderungen verändern die
wirksamen Werte, überschreiben aber nicht automatisch das gespeicherte Profil;
zum Speichern Op 18 erneut senden (Antwortbyte 39 vorher auf 0 setzen).
Greaseweazle-SELECT und interne Laufwerkswechsel laden diese nativen Profile
nicht. Das Tool muss den Besitzerwechsel des gemeinsamen Fluxpfads beachten.

#### Leitungszugriffe und CHECK_PROFILE

Native TIMING lehnt TG43/Drehzahlkonflikte ab. SET_RPM lehnt Konflikte vor
jedem GPIO-Zugriff ab. Native SET_LINES lehnt Eingriffe auf Shugart-DS,
NONE/IEC/Apple oder bei reservierter Drehzahl-/TG43-Leitung vollständig ab;
es gibt keine teilweise Änderung. Ein direkter DENSITY-Zugriff wird bei
Reservierung für Drehzahl oder TG43 abgewiesen. Greaseweazle SET_PIN nutzt
dafür seinen vorhandenen ACK_BAD_PIN; keine neuen GW-Befehle oder Statuswerte.

CHECK_PROFILE erhält zwei zusätzliche, unabhängig kombinierbare Ergebnisbits:

| Wert | Name | Bedeutung |
|---:|---|---|
| 8 | LINE_CONFLICT | DS3/DRATE- oder TG43/Drehzahlkonflikt |
| 16 | UNSUPPORTED_CONFIGURATION | Drehzahlleitung/TG43 für ungeeigneten Port konfiguriert |

In der Ursachenmaske von Abschnitt 11 stehen dazu Bits 16, 17, 18 für die
drei Konfliktursachen in gleicher Reihenfolge wie Op 17. Bestehende Bits bleiben
unverändert. Kein neues Jobflag erforderlich. Abschnitt 12 speichert den
Portbericht vor dem letzten Aufnahmeversuch, auch bei fehlgeschlagenem Start;
er beschreibt keine späteren Änderungen am Laufwerk. Er ist unabhängig vom
CHECK_PROFILE-Flag verfügbar.

#### ED: Softwarebefund und Schreibgrenze

Die Offline-Tests prüfen vollständige 36-Sektor-Spuren bei 1 Mbit/s, einschließlich
Inhaltsvergleich, Jitter, Änderung im letzten Sektor, abgeschnittener Aufnahme
und zu kleinem Encoderpuffer. Diese synthetischen Tests bestätigen keine
physische ED-Aufnahme oder ED-Schreibfunktion.

Der normale Schreibpfad hat weiterhin mindestens 440 Timerticks bei 275 MHz
(1,6 µs). ED benötigt auch etwa 1 µs Abstand; solche Daten werden ohne Clamp
abgewiesen. **Clamp ist keine ED-Unterstützung:** das Anheben kurzer Intervalle
verändert den Datenstrom. Die neue Portantwort meldet die realen Intervallgrenzen,
damit das Tool Decoderfähigkeit und Schreibfähigkeit getrennt darstellen kann.

### 4.5 Mediengebundene Starts und robuste Anfrageprüfung

**Discovery:** `0x0F [25]` liefert ab 1.18-dev 20 Bytes (Schema 2; Builds mit Schema 1
liefern die ersten 16): Byte0 Schema, Byte1 Schreibstart `0x27`, Byte2 Daten-/Endkommando
`0x22`, Byte3 Präfixgröße8. u32 bei4 ist die Pflichtprüfmaske3 (Bit0 Konfiguration,
Bit1 Medium), bei8 die aktuelle Konfigurationsgeneration, bei12 die aktuelle
Mediengeneration. Schema 2 ergänzt Byte16 Lesestart `0x28`, Byte17 Daten-/Endkommando
`0x20`, Byte18 Präfixgröße8, Byte19 reserviert 0. Ein Parser akzeptiert Schema 1 mit
≥16 und Schema 2 mit ≥20 Bytes. Byte23 der Op22-Matrix kündigt Op25 an. Alte Builds mit
Byte23=0 unterstützen diese Starts nicht; kein stiller Rückfall auf ungeprüftes Schreiben
oder ungebundenes Lesen.

**GUARDED_READ `0x28` (1.18-dev):** Nutzlast `expected_config:u32, expected_media:u32,
READ-Nutzlast...` mit der unveränderten READ-Nutzlast (Längen 3/4/6/7 und alle Flags wie
bei 0x20; optionale Felder bleiben eindeutig, weil der Präfix vor dem Kommando steht).
Beide Generationen müssen ungleich0 sein. Veraltete/fehlende Konfiguration: Status16;
fehlendes/gewechseltes Medium: Status21; vor der Annahme geht kein Daten-Rahmen hinaus.
Nach dem Aufnahmestart wird das Medium wie bei 0x27 erneut geprüft; ein inzwischen
erkannter Wechsel bricht die Aufnahme ab und die Annahme trägt Status21. Die Annahme
antwortet mit Kommando `0x28`; DATA und END kommen mit **Kommando0x20 und derselben
Sequenznummer**, der Ende-Rahmen trägt den Generationsblock. BUSY-, Ressourcen- und
Parameterprüfungen bleiben wie bei READ. Host: `read_track(..., expected_config=,
expected_media=)`.

**GUARDED_WRITE `0x27`:** Nutzlast
`expected_config:u32, expected_media:u32, WRITE-Nutzlast...`.
Beide Generationen müssen ungleich0 sein. Die bestehende WRITE-Nutzlast beginnt
mit `track, side, flags`, enthält ihre Größen und optionalen Felder unverändert.
Vor der Vorbereitung prüft die Firmware beide Generationen. Veraltete/fehlende
Konfiguration: Status16 BAD_ARGS. Fehlendes/gewechseltes Medium: Status21
MEDIA_CHANGED. Die normalen BUSY-, Ressourcen-, Schreibschutz- und Parameter-
prüfungen bleiben erhalten. Nach der Vorbereitung wird das Medium erneut geprüft,
damit kein zwischenzeitlicher Wechsel als neue Grundlage übernommen wird.

Die Annahmeantwort trägt Kommando `0x27`. Erst nach erfolgreicher Annahme sendet
das Tool DATA mit **Kommando0x22 und derselben Sequenznummer**. END verwendet
ebenfalls0x22. Integritätsjob-Header, CRC, Timeout, Streaming und Prüflesen bleiben
wie bei WRITE. Eine abgewiesene Vorbereitung erzeugt keinen WRITE-END-Rahmen.
Die vorhandenen Medienprüfungen vor und während des Schreibens bleiben wirksam.
Die Prüfung bindet an die erkennbare Mediengeneration, nicht an eine physische
Seriennummer: unerkannte Wechsel ohne Change-Signal erfordern weiterhin Op16.

**Mediengebundene Analyse:** `0x0F [26, expected_config:u32,
expected_media:u32, vollständige_Op1_Anfrage...]` prüft beide Generationen
vor dem normalen Analyse-Start. Nach dem Start wird die Mediengeneration erneut
geprüft; ein währenddessen erkannter Wechsel beendet den Job mit MEDIA_CHANGED.
Byte24 der Op22-Matrix kündigt Op26 an.
Fehlercodes wie oben; Antworten und Berichte wie Op1. Op24 bleibt für die
bisherige reine Konfigurationsprüfung verfügbar. Diese Befehle sichern einen
einzelnen Start, keine atomare gesamte Diskettenkopie. Sitzungsgültigkeit und
Überlaufregeln der Generationen aus1.3 gelten unverändert.

Hostbeispiel:

```python
state = dev.configuration()  # nach SELECT und allen Profileinstellungen
dev.write_track(track, side, deltas,
                expected_config=state['generation'],
                expected_media=state['media_generation'])
```

Vorher Unterstützung über die Matrix/Discovery prüfen. Ohne beide Argumente
bleibt `write_track` beim bisherigen WRITE. `guarded_analysis(generation,
start_payload, media_generation=...)` erzeugt Op26. Lokale Parameterfehler
werden vor dem Öffnen eines Integritätsjobs abgefangen; bei negativer WRITE-
Annahme schließt der Host einen von ihm geöffneten Job mit ABORT.

**Parserkorrekturen:** READ akzeptiert Nutzlastlängen3,4,6 oder7. Länge3 ist
die Legacy-Kurzform, Länge4 enthält Flags und verwendet bei NO_INDEX 200ms;
Länge6 enthält zusätzlich `period_ms`; Länge7 ist nur mit HARD_SECTORS gültig.
HARD_SECTORS verlangt Länge7 und mindestens2 Sektorlöcher. Unbekannte Flagbits,
halbe Felder, Anhänge und NO_INDEX-Perioden über2000ms werden mit BAD_ARGS
abgewiesen; Periode0 bedeutet weiterhin200ms. Viertelspuranteile ungleich0
verlangen einen Apple-Port. Gescheiterter Capture-Start stellt die vorherige
Hartsektor-Einstellung wieder her (der Erkennungszähler wird neu initialisiert).

WRITE setzt die Hartsektor-Konfiguration erst nach erfolgreicher Prüfung und
Vorbereitung. Ungültige Compact-Größen werden vor der Vorbereitung abgewiesen
und löschen keinen vorhandenen Prüfbericht mehr. Die Fähigkeitsmatrix meldet
BUSY auch bei einer noch gehaltenen Greaseweazle-Ressource. Diese Korrekturen
und Erweiterungen benötigen **keine zusätzlichen Flags**; Greaseweazle-Wireformat
und die bisherigen gültigen WRITE-Anfragen bleiben unverändert.

#### Generationsblock in den Ende-Rahmen (1.18-dev)

Damit ein Bericht belegen kann, dass eine Spur von **einem** Medium stammt, auch wenn kein
geschützter Start (0x27, 0x28, Op 26) benutzt wurde, hängt die Firmware hinter die
bisherigen Ende-Nutzdaten von READ 0x20 (`revs` u8) und WRITE 0x22 (`verify_level` u8,
`clamped` u32; ebenso nach einem Start über 0x27) denselben 17-Byte-Block:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u32 | Konfigurationsgeneration (Op 23) zum Zeitpunkt des Ende-Rahmens |
| 5 | u32 | Mediengeneration, an die die Operation beim Start gebunden wurde |
| 9 | u32 | Mediengeneration zum Zeitpunkt des Ende-Rahmens |
| 13 | u32 | Capture-ID der Aufnahme (dieselbe wie Op 8 / Qualitätsbericht); 0 bei WRITE |

Die Ende-Nutzdaten sind damit 18 Byte (READ; 114 mit READ-Flag Bit3, dann folgt der
Provenienzblock aus 4.12 hinter dem Generationsblock; mit Bit5 folgt zuletzt der 132-Byte-Lesekontext
aus 4.24: 150 Byte, mit Bit3 und Bit5 246 Byte) beziehungsweise 22 Byte (WRITE; 30 mit den
anschließenden `erased_ticks` u32 und `start_offset_applied` u32, 3.3) lang; die alten
Felder stehen unverändert vorn, alte Parser lesen weiter nur sie. Der Block steht
auch in Ende-Rahmen mit Fehlerstatus, soweit diese bisher Nutzdaten trugen (READ-Ende nach
ABORT trägt `revs` = 0 plus Block; WRITE-Ende mit 16/10/5/20 ohne Nutzdaten bleibt leer).
Start- und Endgeneration sind bei einem fehlerfreien Lauf gleich, weil ein erkannter
Wechsel die Operation mit Status 21 beendet; eine Differenz ist nur bei Status 21 oder
bei einem Wechsel nach dem letzten Prüfpunkt sichtbar. Der Block ersetzt Op 16 nicht:
ohne auswertbares Wechselsignal bleibt die Generation gleich, obwohl die Diskette
getauscht wurde. Host: `Capture.generations`, `WriteResult.generations` (Wörterbuch mit
`config_generation`, `media_generation_start`, `media_generation_end`, `capture_id`;
`None` bei älterer Firmware).

### 4.6 Sitzungs-Schreibsperre (Op 37, Status 22, 1.18-dev)

Forensische Werkzeuge wollen vor dem ersten Lesen sicherstellen, dass die Firmware in
dieser Sitzung **kein** Schreibkommando mehr ausführt, auch nicht durch einen Bedienfehler
im Werkzeug oder ein zweites Programm am selben Port. Dafür gibt es eine Sperre, die nur
eingeschaltet werden kann:

- `0x0F [37]` liest den Zustand: Antwort `[Schema 1, state u8]` mit Bit0 = Sitzungssperre
  aktiv, Bit1 = WRITE-LOCK-Jumper gesteckt. Beide sind getrennt sichtbar.
- `0x0F [37, 1]` schaltet die Sperre ein und liefert dieselbe Antwort; wiederholtes
  Einschalten ist erlaubt. Jede andere Nutzlast (`[37, 0]`, `[37, 2]`, längere Anfragen)
  wird mit BAD_ARGS abgewiesen: **ein Ausschalten über USB gibt es nicht.** Die Sperre
  endet nur mit einem Reset des Mikrocontrollers (Einschalten, NRST, Watchdog). Ein
  Wechsel der USB-Persönlichkeit (Greaseweazle-Modus, Massenspeicher), ein Protokoll-Reset
  oder ABORT heben sie nicht auf. Die Sperre ist nicht von der Laufwerksauswahl abhängig
  und wird nicht gespeichert.
- `board_status.flags` Bit7 (STATUS 0x02, POWER 0x40, Ereignis 0x83) zeigt die aktive
  Sperre in jedem Statusblock; Bit0 bleibt der Jumper.

**Was mit Status 22 abgewiesen wird** (bei der Annahme, bevor ein Kopf bewegt oder ein
Tor geöffnet wird): WRITE 0x22, GUARDED_WRITE 0x27, ERASE 0x23, PATTERN 0x24, FORMAT 0x26,
COPY_START 0x63 und WRITE_START 0x64 (SD → Diskette), IEC_BLOCK 0x35 mit Op 1 (U2-Block
schreiben) sowie IEC_MEM 0x33 und IEC_TARGET 0x36 mit den Unterfunktionen 0 (M-W), 2 (M-E)
und 3 (DOS-Befehl), weil sie den Laufwerksspeicher oder die Diskette im Laufwerk ändern
können. Ein WRITE, der vor dem Einschalten angenommen wurde, endet mit Ende-Status 22,
sobald die Firmware das Schreiben beginnen würde; nichts wird geschrieben. Status 22
unterscheidet die Sitzungssperre vom Schreibschutz der Diskette und vom Jumper (beide 12).

**Erlaubt bleiben** alle Lesekommandos, ANALYZE, Analysejobs, QUALITY, IEC_READ_STATUS,
IEC_BLOCK Op 0, IEC_MEM Op 1 (M-R) und Op 4 (DOS-Status), Laufwerkssteuerung (SELECT,
MOTOR, SEEK, RECAL, SIDE) sowie Dateischreibzugriffe auf die SD-NAND (WRITE_FILE, DELETE,
Dumps nach SD): die SD-Karte ist nicht der Beweisträger.

**Andere Pfade:** Im Greaseweazle-Modus wirkt die Sperre wie der Jumper, das heißt die
GW-Schreibkommandos erhalten die vorhandene GW-Quittung „schreibgeschützt“; es gibt dort
keinen eigenen Code. Ein Werkzeug darf diese GW-Antwort also nicht als „Jumper gesteckt“
deuten, sondern fragt im nativen Modus Op 37. Der USB-Floppy-Modus (Massenspeicher) und
das eigenständige Schreiben per Taster sind ebenfalls wie beim Jumper blockiert.

Host: `write_lock(engage=False)` liefert `{'session', 'jumper', 'locked'}`, `ufi write-lock
[engage]`; `BoardStatus.session_locked` liest Bit7. FakeV2 modelliert Op 37, Bit7 und die
Abweisungen mit 22.

### 4.7 Flagmasken-Abfrage (Op 38, 1.18-dev)

Ein Werkzeug soll genau die Optionen anbieten, die diese Firmware annimmt, ohne sie aus
Versionsnummern abzuleiten. `0x0F [38]` liefert 16 Bytes; alle Werte sind die Konstanten,
mit denen die Parser prüfen, nicht eine zweite Pflegeliste:

| Offset | Typ | Inhalt | Stand 1.18-dev |
|---:|---|---|---|
| 0 | u8 | Schema | 1 |
| 1 | u8 | READ-Flags (0x20, 0x28) | 0x3F (0x1F vor dem Lesekontextbit 5, 0x0F vor dem Fortschrittsbit 4, 0x07 vor dem Provenienzbit 3) |
| 2 | u8 | WRITE-Flags (0x22, 0x27) | 0xFF |
| 3 | u8 | WRITE `flags2` (Bit0 Spurrest löschen, Bit1 Startversatz, Bit2 Präkompensation je Auftrag, Bit3 Weak-Marker, Bit4 Rohprüfung) | 0x1F (0x0F vor 4.17, 0x07 vor 4.16, 0x03 vor 4.14) |
| 4 | u8 | Analyseflags (ANALYZE 0x25, Op 1 Byte 3); Regel bleibt: Bits 1–7 brauchen Bit 0 | 0xFF |
| 5 | u8 | höchster Formathinweis | 7 |
| 6 | u16 | Op-33-Flags (Apple-GCR) | 0x003F |
| 8 | u16 | Op-34-Toleranzflags | 0x001F |
| 10 | u16 | QUALITY-Merkmale (`features`, Op 1 Länge 37) | 0x000F |
| 12 | u16 | Diagnoseflags (`diagnostic_flags`, 5.2) | 0x0018 |
| 14 | u8 | Positionsbyte der Analyse (Seite, Viertelspurmodus, Viertel) | 0xB1 |
| 15 | u8 | reserviert | 0 |

Reine Abfrage: auch während Dump, Jobs oder gehaltener Ressource beantwortet, nie BUSY;
jede Nutzlast nach dem Op-Byte → BAD_ARGS. Eine Maske sagt nur, welche Bits der Parser
kennt; Kombinationsregeln (DETAIL-Pflicht, Bit 6 nur mit 0x20, Bit 7 nur mit 0x21, Apple-Bedingungen) stehen
bei den Kommandos. Host: `flag_masks()`, `ufi masks`.

### 4.8 Grund der letzten Abweisung (Op 39, 1.18-dev)

Ein Werkzeug soll dem Benutzer sagen können, **welche** Option die Firmware abgelehnt hat,
statt nur „ungültige Argumente“. Der Antwortrahmen selbst bleibt unverändert (ein
Detailbyte in jeder 16er-Antwort würde den Rahmenvertrag aller Kommandos ändern); stattdessen
merkt sich die Firmware die letzte Abweisung, und `0x0F [39]` liefert 6 Bytes:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Abgewiesenes Kommando; 0 = keine Abweisung seit Reset |
| 2 | u8 | Sequenznummer dieser Anfrage |
| 3 | u8 | Status der Abweisung (16; 21 bei Generationsprüfungen) |
| 4 | u8 | Feld, siehe unten |
| 5 | u8 | Detail zum Feld |

Jede Anfrage löscht die Notiz vor dem Parsen; nur Abweisungen mit 16 oder 21 schreiben
sie. Eine später angenommene Anfrage lässt sie stehen, Kommando und Sequenz sagen, zu
welcher Abweisung sie gehört. Das Werkzeug fragt direkt nach einem 16 ab und vergleicht
Kommando und Sequenz mit seiner Anfrage, bevor es das Feld nennt.

Felder, gemeinsam für READ 0x20/0x28, WRITE 0x22/0x27 und ANALYZE 0x25:

| Feld | Name | Detail |
|---:|---|---|
| 0 | unbekannt | der Parser liefert keine Aufschlüsselung |
| 1 | Länge | Mindestlänge, die die Flags verlangen (READ: 3 = keine gültige Länge, 6/7 = Form mit Periode/Sektoren) |
| 2 | Flags | die unbekannten Bits |
| 3 | Spur/Viertel | 1 = Viertelspur auf dem Apple-Port ungültig, 2 = Shugart-Laufwerk: „half track needs the double-step profile“ (Viertelform nur als Halbspur 2 und nur mit `double_step` = 1, 1.18) |
| 4 | Seite/Position | das Byte |
| 5 | Umdrehungen/Periode | 1 = Periode > 2000 ms |
| 6 | Sektoren | der übergebene Wert (< 2) |
| 7 | Zeitskala | 1 = außerhalb 500–2000 |
| 8 | `index_ticks` | 1 = verlangt Bit 5 (erweiterter Pfad, 0x60; bis 1.18-dev-Vorstufen 0x21), 2 = Null |
| 9 | Sektormenge (EXPECT_SET) | 1 = verlangt 0x21, 2 = leere Menge, 3 = Größencode > 3, 4 = Reservebyte ≠ 0 |
| 10 | `flags2` | die unbekannten Bits |
| 11 | Konfigurationsgeneration | 1 = Null oder veraltet |
| 12 | Mediengeneration | 1 = Null oder anderes Medium (Status 21) |
| 13 | Analyseoptionen | ANALYZE-/Op-1-Optionsblock als Ganzes abgelehnt; Detail 0 |
| 14 | Startversatz (`flags2` Bit1) | 1 = die vier Bytes fehlen, 2 = 0 oder über 275 000 000 |
| 15 | Präkompensation (`flags2` Bit2) | 1 = die zwei Bytes fehlen, 2 = über 2000 ns |
| 16 | Weak-Marker (`flags2` Bit3) | 1 = die sechs Bytes fehlen, 2 = Jitter über 8192 Takte, 3 = ohne erweiterten Pfad (Bit5), 4 = zusammen mit Prüflesen (Bit0) |
| 17 | Rohprüfung (`flags2` Bit4) | 1 = ohne Quell-Indexdauer (Bit6), 2 = zusammen mit Prüflesen (Bit0) |

Reine Abfrage, nie BUSY; Nutzlast nach dem Op-Byte → BAD_ARGS. Host: `last_refusal()`
mit Feldnamen, `ufi refusal`.

### 4.9 Prüflesebericht je Sektor (Op 40, 1.18-dev)

Das Firmware-Prüflesen (5.2) baut vor dem Schreibtor eine Referenz aus bis zu 64 Sektoren
(CHRN und CRC-32 der Daten) und vergleicht zwei zurückgelesene Umdrehungen damit; bisher
erreichte den Host nur `verify_level` und Status 6. Ab 1.18-dev merkt sich der Vergleicher
je Referenzsektor das Ergebnis, und `0x0F [40]` liefert den Bericht des letzten geprüften
WRITE (0x22, 0x27, Flags 0x01/0x21/0x61):

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Format der Referenz: 0 keine (nichts zu listen), 1 IBM-MFM, 2 Amiga, 3 IBM-FM, 4 HP-MMFM, 5 Commodore-GCR |
| 2 | u8 | Anzahl n der Einträge (0–64) |
| 3 | u8 | Bit0 Referenz übergelaufen (> 64 Sektoren), Bit1 widersprüchliche Duplikate in der Quelle, Bit2 Stufe 3 erreicht, Bit3 Rücklesen fehlt (Prüfen angefordert, Rücklesen lief nie) |
| 4 | u8 | Spurbyte wie in der WRITE-Anfrage gesendet (Viertelspurform eingeschlossen) |
| 5 | u8 | Seite |
| 6 + 5·i | 5 × u8 | Eintrag i: `c, h, r, n` (Größencode) und Zustand: 0 nie mit brauchbarem Kopf zurückgelesen, 1 CRC gültig und gleicher Inhalt, 2 nur mit CRC-/Prüfsummenfehler zurückgelesen, 3 CRC-gültig mit anderem Inhalt |

Höchstens 326 Byte. Der Kopf mit Spur, Seite und Bit3 entsteht bei der Annahme eines WRITE
mit Prüfbit; der Vergleicher füllt Format, Anzahl, Flags und Einträge nach dem Rücklesen
(über drei Decoderprofile hinweg gilt je Sektor das beste Ergebnis: gültig vor abweichend
vor CRC-Fehler vor fehlend). Ein WRITE ohne Prüfbit lässt den Bericht unverändert, daher
vergleicht das Werkzeug Spur und Seite mit seinem Auftrag. Reine Abfrage, nie BUSY;
Nutzlast nach dem Op-Byte → BAD_ARGS. Host: `verify_report()`, `ufi verify-report`
(listet Einträge mit Zustand ≠ 1 als CHRN mit Grund).

### 4.10 Drehzahlprüfung vor dem Schreiben (Op 41, Status 23, 1.18-dev)

Wird eine Spur mit ihrer Quell-Indexdauer zurückgeschrieben (WRITE Flags Bit 6
`index_ticks`), läuft die Zieldrehzahl sonst stillschweigend in die Zelllängen ein. Mit
einer Toleranz misst die Firmware vor dem Schreiben die eigene Indexdauer des Laufwerks
und weist zu große Abweichungen ab:

- `0x0F [41]` liest, `0x0F [41, 1, toleranz_promille:u16]` setzt (0 = Prüfung aus,
  Voreinstellung; 1–500; während eines laufenden Vorgangs BUSY). Antwort 11 Bytes:
  Schema 1, `toleranz_promille` u16, `gemessene_indexdauer` u32, `quell_indexdauer` u32
  (Takte 275 MHz; beide 0, bis eine Prüfung lief). Die Einstellung ist flüchtig.
- Mit Toleranz ≠ 0 misst jeder native WRITE/GUARDED_WRITE mit Bit 6 (erweiterter Pfad
  `0x60`, mit Prüflesen `0x61`) nach dem Empfang der
  Daten und **vor** Kopfbewegung und Schreibtor zwei aufeinanderfolgende Indexabstände
  (dieselbe Messung wie FORMAT, höchstens etwa 600 ms). Ist
  |gemessen − Quelle| > Quelle · Toleranz / 1000, endet der Auftrag mit Ende-Status 23
  `SPEED_MISMATCH` (Ende-Nutzdaten vorhanden, `verify_level` 0); nichts wird geschrieben.
  Die Messwerte bleiben in Op 41 lesbar, damit das Werkzeug dem Benutzer beide
  Drehzahlen nennen kann.
- Ohne Bit 6 gibt es keine Quelldauer, die Prüfung entfällt. Greaseweazle-Schreibbefehle
  prüfen nicht. Apple-Disk-II-Laufwerke ohne Sync-Sensor haben keinen Index: Prüfung
  entfällt. Misslingt eine aktivierte Messung, wird der Auftrag bereits hier mit dem
  Messfehler abgewiesen (etwa NO_INDEX, TIMEOUT oder CANCELLED); das Schreibtor bleibt
  geschlossen. Op41 setzt die gemessene Dauer dabei auf0 und zeigt die aktuelle
  Quelldauer, damit kein alter erfolgreicher Messwert als neuer erscheint.

Die Prüfung ersetzt keine Zeitskala: Wer ein 360-U/min-Abbild auf 300 U/min schreiben
will, setzt `scale` (Bit 3) und schickt kein Bit 6 oder eine passend umgerechnete Quelle.
Host: `speed_check(tolerance_permille=None)`, `ufi speed-check [PROMILLE|off]`.

### 4.11 Weak-Fenster als Zähler (Op 42, 1.18-dev)

ANALYZE 0x25 zählt die Flusswechsel je 1/64 Umdrehung über alle aufgenommenen Umdrehungen
und meldet in `weak_mask`/`weak_count` die Fenster, deren Zahl zwischen den Umdrehungen um
mindestens 3 schwankt. Die feste Schwelle bleibt; zusätzlich behält der Analysator die
Differenzen selbst, damit ein Werkzeug seine eigene Schwelle anlegt oder eine Wärmeleiste
zeigt. `0x0F [42]` liefert 68 Bytes:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Umdrehungen, auf denen die Differenzen beruhen; 0 = kein ANALYZE seit Reset oder weniger als 2 Umdrehungen |
| 2 | u8 | Spur der Analyse |
| 3 | u8 | Seiten-/Positionsbyte wie gesendet (`side | 0x80 | quarter << 4` bei Viertelspuren) |
| 4 + w | u8 | Fenster w (0–63): größte minus kleinste Flusswechselzahl über die Umdrehungen, auf 255 gekappt; Fenster w deckt [w/64, (w+1)/64) der Indexperiode ab, wie Bit w in `weak_mask` |

Gehört zum letzten ANALYZE 0x25; der asynchrone Analysejob (Op 1) führt seine eigenen
QX-Fenster und ist nicht abgedeckt. Ein abgebrochener oder fehlgeschlagener ANALYZE lässt
Byte 1 auf 0. Reine Abfrage, nie BUSY; Nutzlast nach dem Op-Byte → BAD_ARGS. Host:
`weak_windows()`, `ufi weak [--min N]`.

### 4.12 Provenienzblock (Op 43, 1.18-dev)

Ein Archiv muss eine Rohdatei an den Gerätezustand binden, aus dem sie stammt. Die
Teile dafür lagen verstreut (INFO, STATUS, Op 18, Op 23, Generationsblock,
Aufnahmekontext). `0x0F [43]` liefert sie als einen Datensatz, 96 Bytes, Schema 1,
reine Abfrage (auch während Dump, Jobs oder gehaltener Ressource; Nutzlast hinter dem
Op-Byte → BAD_ARGS):

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Länge 96 |
| 2 | u8 | Board-Revision aus dem BOARD_ID-Teiler: 5, 6, 7; 0 unbekannt |
| 3 | u8 | Flags: Bit0 Firmware aus unsauberem Arbeitsbaum („+“ hinter der Revision), Bit1 WRITE-LOCK-Jumper, Bit2 Sitzungs-Schreibsperre (4.6), Bit3 `apple_sync=1`, Bit4 Aufnahme vorhanden (Capture-ID ≠ 0) |
| 4 | 3 × u32 | MCU-Unique-ID (UID_BASE, wie in der USB-Seriennummer) |
| 16 | u16 | MCU-Device-ID (IDCODE Bits 0–11, STM32H723 = 0x483) |
| 18 | u16 | Silizium-Revision (IDCODE Bits 16–31) |
| 20 | u16 | BOARD_ID-Spannung in mV |
| 22, 23 | je u8 | Firmware-Version Major, Minor |
| 24 | 12 Bytes | Git-Revision als Text (NUL-gefüllt, ohne „+“) |
| 36 | 12 Bytes | Build-Datum `__DATE__` als Text (NUL-gefüllt) |
| 48 | u32 | Flux-Takt in Hz (275 000 000) |
| 52 | u32 | Konfigurationsgeneration (Op 23) |
| 56 | u32 | Aktuelle Mediengeneration |
| 60 | u32 | Capture-ID der letzten Aufnahme (0 ohne Aufnahme) |
| 64 | u32 | Mediengeneration, an die diese Aufnahme gebunden war |
| 68 | u32 | Letzte vom Flussmesser gesehene Indexperiode in Takten (Aufnahme oder Schreibvorgang), 0 ohne zwei Impulse |
| 72 | u32 | Letzte Messung der Drehzahlprüfung (Op 41), 0 ohne Prüfung |
| 76 | u8 | Gewähltes Laufwerk (Kennung wie SELECT) |
| 77 | u8 | 1 = Betriebsprofil vom Werkzeug gespeichert (Op 18), 0 = Firmware-Vorgabe |
| 78 | u16 | Drehzahleinstellung (SET_RPM), 0 = Vorgabe |
| 80 | u32 | Laufzeit seit Reset in ms |
| 84 | u16 | Strom der 5-V-Laufwerksversorgung in mA (board_status; die Firmware rechnet den INA180-Wert nach der Platinenrevision um: v0.x 0,1 Ω = 2 mV/mA, v1.0 50 mΩ = 1 mV/mA) |
| 86 | u16 | Strom der 12-V-Laufwerksversorgung in mA |
| 88 | u8 | `power` wie STATUS: Bit0 FDD_5V an, Bit1 FDD_12V an |
| 89 | u8 | board_status-Flags wie STATUS (Jumper, Überstrom, SD, Taster, Sicherheitsmodus, Sitzungssperre) |
| 90 | u8 | Taktflags (1.18-dev, vorher reserviert 0): Bit0 Taktquelle TCXO (HSE-Bypass, Platine v1.0), 0 = Quarzoszillator; Bit1 Offset 91/93 gültig; Bit2 Offset 94/95 gültig |
| 91 | i16 | Letzte Taktabweichung in ppm aus dem SELFTEST-Punkt `usb_sof` (TIM2 gegen 128 USB-Rahmen des Hosts, also relativ zum Host-Takt, nicht absolut), auf ±32767 begrenzt; 0 ohne Bit1 |
| 93 | u8 | Alter dieser Messung in Minuten (255 = 255 oder mehr); 0 ohne Bit1 |
| 94 | i8 | Platinentemperatur in ganzen °C, letzter **zwischengespeicherter** SHT40-Wert (Start-Probe, SENSOR 0x44, Log-Kopf); 0 ohne Bit2 |
| 95 | u8 | Relative Feuchte in ganzen % aus demselben Wert; 0 ohne Bit2 |

Offset 90–95 (1.18-dev): die Firmware misst dafür nie neu – SENSOR braucht rund 20 ms, die
`usb_sof`-Messung bis 400 ms –, sie meldet nur den letzten Wert seit dem Reset; ohne Messung
bleiben Bit1/Bit2 und die Felder 0, wie bei älterer Firmware. Wer einen frischen Wert braucht,
ruft vor dem Lesen SELFTEST (Punkt 2) beziehungsweise SENSOR auf. Das Schema bleibt 1; ältere
Hosts lasen die Bytes als reserviert und ignorieren sie. Host: `tcxo`, `clock_ppm`,
`clock_ppm_age_min`, `temp_c`, `rh_percent` (`None` ohne Gültigkeitsbit).

Der Block ersetzt keine Prüfsumme der Firmware: die SHA-256 des geflashten Abbilds
berechnet der Host aus der Datei, die er geflasht hat, und legt sie neben den Block.

**Per Flag an die Aufnahme gebunden:** READ und GUARDED_READ mit Flags Bit3 (`0x08`;
Op 38 READ-Maske ab 1.18-dev `0x3F`) hängen denselben 96-Byte-Block an den Ende-Rahmen hinter den
Generationsblock (Ende-Nutzdaten 114 Byte, auch bei Fehlerstatus und nach ABORT, soweit
der Ende-Rahmen Nutzdaten trägt). Rohdaten und Herkunft kommen so in einer Transaktion;
die Capture-ID im Block ist die der gerade beendeten Aufnahme. Ohne Bit3 bleibt der
Ende-Rahmen bei 18 Byte.
Host: `provenance()`, `read_track(..., provenance=True)` → `Capture.provenance`,
`ufi provenance [--json]` (JSON zum Ablegen neben der Rohdatei), `ufi read --provenance`.

### 4.13 Weak-Positionen fein (Op 44, 1.18-dev)

Op 42 (4.11) zählt je 1/64 Umdrehung; für die Lage instabiler Flusswechsel ist das zu
grob. ANALYZE 0x25 zählt seit 1.18-dev zusätzlich je **1/1024 Umdrehung** die Flusswechsel
jeder Umdrehung und behält je Fenster Minimum und Maximum über die Umdrehungen (Zähler
auf 255 gekappt; Fenster w der Umdrehung k reicht von `index_time·w/1024` bis
`index_time·(w+1)/1024` Takte nach deren Index, Flusswechsel nach dem Index entfallen).
`0x0F [44]` listet die Fenster, deren Maximum − Minimum die Schwelle erreicht,
`[44, schwelle:u8]` mit eigener Schwelle 1–255 (Vorgabe 2; 0 → BAD_ARGS). Reine Abfrage,
nie BUSY; nur der letzte ANALYZE; mit einer Umdrehung ist die Liste leer.

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Verglichene Umdrehungen (0 = keine Daten) |
| 2 | u8 | Spur wie gesendet |
| 3 | u8 | Seitenbyte wie gesendet (Viertelform eingeschlossen) |
| 4 | u16 | Fensterzahl je Umdrehung, 1024 |
| 6 | u8 | Angewandte Schwelle |
| 7 | u8 | Bit0 Liste bei 64 Einträgen abgeschnitten (höhere Schwelle wählen) |
| 8 | u16 | Anzahl n der Einträge (0–64) |
| 10 | n × 4 | je Eintrag: Fenster u16 (0–1023, aufsteigend), Maximum − Minimum u8, Minimum u8 |

Antwortlänge 10 + 4·n, höchstens 266 Byte. Die Zählung ist eine Differenz von
Flusswechselzahlen, keine Bitzuordnung: ein Fenster mit Differenz 1 bei 500 kbit/s MFM
umfasst rund 25 Bitzellen. Host: `weak_positions(threshold=2)`, `ufi weak --positions
[--threshold N]`.

### 4.14 Präkompensation je Auftrag und Zone (Op 45, `flags2` Bit2, 1.18-dev)

Die Schreib-Präkompensation kam bisher allein aus TIMING `precomp_ns` (0xFFFF = nach
Datenrate, 0 = aus, sonst ns) und einer festen Spurregel im Codec (Spur < 40 keine,
40–60 die Hälfte, > 60 voll). Ab 1.18-dev zählt die Regel die **physische** Spur (bei
Doppelschritt-Profil liegt logische Spur n auf 2n); vorher zählte sie die gesendete
logische Spur, was unter Doppelschritt die falsche Zone traf. Zwei neue Wege, beide nur
für den klassischen Schreibpfad (Extended-Intervalle sind hostseitig gezeitet; `flags2`
Bit2 mit Bit5 → Status 11):

- **Je Auftrag:** WRITE/GUARDED_WRITE `flags2` Bit2, dahinter `precomp_ns` u16: 0 = aus für
  diesen Auftrag (keine Spurregel, auf jeder Spur), 1–2000 ns wie gesendet ohne Regel.
  Angekündigt durch WRITE_LIMITS Flags Bit6; fehlende Bytes → 16 mit Op 39 Feld 15 Grund 1,
  über 2000 → Grund 2. Ein Host, der selbst präkompensiert, sendet 0 und verhindert so die
  doppelte Verschiebung.
- **Zonenprofil (Op 45), sitzungslokal und flüchtig:** `[45]` liest; `[45, 1, n, n × (ab_physischer_Spur u8,
  ns u16)]` setzt 1–4 Zonen in aufsteigender Reihenfolge, die erste ab Spur 0, ns 0 = aus in
  der Zone, höchstens 2000; `[45, 0]` löscht das Profil. Setzen während eines laufenden
  Vorgangs → BUSY, Lesen nie. Antwort 19 Bytes:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Quelle des letzten Schreibauftrags: 0 TIMING/automatisch mit Spurregel, 1 Zonenprofil, 2 Vorgabe je Auftrag |
| 2 | u8 | Zonenanzahl 0–4 |
| 3 | 4 × 3 | Zonen: ab physischer Spur u8, ns u16; ungenutzte Zonen 0 |
| 15 | u16 | Zuletzt angewandte Präkompensation in ns |
| 17 | u8 | Logische Spur des letzten Auftrags |
| 18 | u8 | Physische Spur des letzten Auftrags |

Rangfolge beim Packen: Vorgabe je Auftrag, sonst Zone der physischen Spur, sonst
TIMING/automatisch mit Spurregel. Die Quelle (Byte 1) erlaubt dem Werkzeug, im
Schreibbericht wörtlich „Board: aus / Profil / automatisch“ zu nennen. Host:
`write_track(..., precomp_ns=)`, `precomp_profile()`, `ufi write --precomp NS`,
`ufi precomp [--zones 0:125,40:62 | --clear]`.

### 4.15 Spurübersicht als Job (Op 46, 1.18-dev)

Bevor ein Werkzeug entscheidet, welche Spuren es mit ANALYZE, Analysejob oder READ im
Detail liest, braucht es den Überblick über die ganze Diskette: welche Spuren formatiert
sind, FM/MFM/GCR, HD oder DD, Drehzahl, Flusswechselzahl. Bisher kostete das je Spur eine
eigene Anfrage mit Seek und mehreren Umdrehungen. `0x0F [46]` fährt den Spurbereich
**auf dem Board** ab, liest je Spur und Seite **eine Umdrehung**, wendet dieselbe
Histogrammeinordnung wie ANALYZE 0x25 an (Gipfelverhältnis 1,5 → MFM, am Apple-Port GCR;
2,0 mit drittem Gipfel ≥ 2,8 → GCR; 2,0 ohne → FM) und hält die Tabelle, die der Host
schon während des Laufs liest. Sitzungslokal, flüchtig; ein neuer Start ersetzt die Tabelle.

- `[46]` liest: nie BUSY, auch während des Laufs (dann die bisher fertigen Einträge).
  Antwort Kopf 28 Byte + Cursor × 20 Byte, höchstens 28 + 168·20 = 3388 Byte.
- `[46, 1, erste u8, letzte u8, seiten u8, flags u8]` startet: `erste ≤ letzte ≤ 83`,
  `seiten` als Maske (Bit0 Seite 0, Bit1 Seite 1; 1–3), `flags` Maske 0x01 (Bit0 zählt je
  Spur die IBM-MFM-Sektoren mit gültiger CRC; langsamer). BUSY, solange ein anderer Vorgang,
  Analysejob oder Mehrspurplan läuft; ohne gewähltes Laufwerk Status 2. Antwort: Kopf mit
  Zustand 1, Cursor 0.
- `[46, 0]` bricht ab: Zustand 3 mit Fehler 20; Antwort Kopf. Das allgemeine ABORT und ein
  Protokollreset beenden den Lauf ebenso.
- Alles andere → BAD_ARGS. Während des Laufs gilt das Board als belegt (Laufwerksbefehle
  und Starts → BUSY; Status, Discovery und Abfragen bleiben lesbar).

Kopf (28 Byte, Little Endian):

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Kopflänge 28 |
| 2 | u8 | Zustand: 0 keiner seit Reset, 1 läuft, 2 fertig, 3 abgebrochen/fehlgeschlagen |
| 3 | u8 | Fehlerstatus des ganzen Laufs: 0; 20 abgebrochen, 21 Medium gewechselt, sonst der Status des Schritts, der ihn beendet hat |
| 4 | u8 | erste Spur |
| 5 | u8 | letzte Spur |
| 6 | u8 | Seitenmaske |
| 7 | u8 | Flags |
| 8 | u8 | Eintragslänge 20 |
| 9 | u8 | reserviert 0 |
| 10 | u16 | Cursor: gültige Einträge |
| 12 | u16 | Gesamtzahl = (letzte − erste + 1) · Seitenzahl |
| 14 | u16 | reserviert 0 |
| 16 | u32 | Laufzeit in ms (laufend: bisher) |
| 20 | u32 | Konfigurationsgeneration beim Start (Op 23) |
| 24 | u32 | Mediengeneration beim Start |

Eintrag (20 Byte; Reihenfolge Spur aufsteigend, je Spur die Seiten der Maske aufsteigend):

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Spur (logisch, wie gesendet) |
| 1 | u8 | Seite |
| 2 | u8 | Status dieses Schritts: 0 ok, sonst UFI-Status (4 kein Index, 3 Seek, 5 Timeout 4 s, 10 Puffer, 6 DMA) |
| 3 | u8 | Kodierung: 0 unbekannt/leer, 1 FM, 2 MFM, 3 GCR |
| 4 | u16 | erster Histogrammgipfel in ns (0 = keiner; ≈ 2000 MFM DD, ≈ 1000 MFM HD, ≈ 4000 FM DD; Bitzelle = Gipfel/2 bei MFM, Gipfel·2 bei FM, Gipfel bei GCR) |
| 6 | u8 | IBM-MFM-Sektoren mit gültiger CRC (nur mit Flag Bit0 und Kodierung 2, sonst 0) |
| 7 | u8 | Zahl der gefundenen Gipfel 0–4 (0 = leer oder Rauschen) |
| 8 | u32 | Indexperiode in 275-MHz-Takten (U/min = 60 · 275 000 000 / Takte) |
| 12 | u32 | Flusswechsel dieser Umdrehung |
| 16 | u16 | zweiter Histogrammgipfel in ns (0 = keiner) |
| 18 | u16 | dritter Histogrammgipfel in ns (0 = keiner) |

Ein Schritt mit Status ≠ 0 lässt die Bytes 3–19 auf 0. Schrittfehler beenden den Lauf
nicht; **drei Schrittfehler in Folge** beenden ihn mit diesem Status im Kopf, damit ein
Laufwerk ohne Diskette nicht 168 Indexwartezeiten durchläuft. Ein Medienwechsel beendet
den Lauf mit 21 und ist zusätzlich an der Mediengeneration im Kopf erkennbar; die
Konfigurationsgeneration bindet die logischen Spurnummern an das Laufwerksprofil
(`double_step`). Die Gipfel 2 und 3 stehen neben dem Urteil, damit ein Werkzeug die
Kodierung nachrechnen kann, ohne die Spur erneut zu lesen. Nicht enthalten: Weak-Bits
(brauchen ≥ 2 Umdrehungen je Spur: ANALYZE/Op 44 je Spur), Viertel- und Halbspuren, Events
(der Host fragt `[46]` einmal je Sekunde ab). Ein 80-Spur-Lauf über beide Seiten dauert
etwa eine Minute. Die Tabelle liegt in D2-SRAM1 neben dem Aufnahmebeleg (3360 Byte); AXI-
Reserve und DTCM-Stapel bleiben unberührt. Host: `track_survey(first, last, sides=3,
sectors=False)`, `track_survey_status()`, `track_survey_wait()`, `track_survey_abort()`,
`ufi survey [FIRST LAST] [--side 0|1] [--sectors] [--status | --abort]`.

### 4.16 Schwache Zellen schreiben (`flags2` Bit3, 1.18-dev)

Kopierschutzspuren leben von Zellen, die bei jedem Lesen anders ausfallen. Erkennen konnte
das Board sie (ANALYZE, Op 42/44); schreiben konnte es nur deterministische Flusswechsel,
dazu seit dem erweiterten Pfad (2.4, WRITE Bit5) beliebig lange stille Lücken ohne
Kunstimpulse. Ab 1.18-dev kann der Host im erweiterten Pfad einzelne Flusswechsel
**markieren**, und das Board würfelt sie bei jedem Schreibvorgang neu.

Anfrage: WRITE/GUARDED_WRITE `flags2` Bit3, dahinter `weak_jitter_ticks` u16 (0–8192 Takte
zu 275 MHz, also bis rund 30 µs) und `weak_seed` u32 (0 = das Board wählt einen Startwert,
sonst genau dieser). Angekündigt durch WRITE_LIMITS Flags Bit7; `flags2`-Maske 0x0F; Op 39
Feld 16 mit den Gründen 1 (sechs Bytes fehlen), 2 (Jitter über 8192), 3 (ohne Bit5: die
klassischen u16-Perioden tragen keine Marker), 4 (mit Bit0: eine Inhaltsreferenz kann
gewürfelte Zellen nicht beurteilen). Greaseweazle-Schreibbefehle sind nicht betroffen.

Mit Bit3 trägt jedes u32-Intervall der Daten zwei Markerbits, die Bits 0–29 das Intervall
selbst (220 … 2³⁰−1 Takte; die Bereichsprüfung läuft auf dem maskierten Wert):

- **Bit31 JITTER:** der Flusswechsel am Ende dieses Intervalls wird um einen zufälligen
  Versatz r aus [−jitter, +jitter] verschoben; das folgende Intervall ändert sich um −r, alle
  späteren Wechsel behalten ihre absolute Lage. r wird so begrenzt, dass beide Intervalle
  ≥ 220 Takte bleiben.
- **Bit30 DROP:** der Flusswechsel am Ende dieses Intervalls entfällt mit Wahrscheinlichkeit
  1/2; das Intervall geht im nächsten auf. Beide Marker zusammen: erst die Drop-Entscheidung,
  Jitter nur, wenn der Wechsel bleibt.
- Das letzte Intervall der Spur wird nie verändert; seine Marker werden verworfen.

Der Durchgang läuft einmal je Schreibauftrag auf dem Board, vor Bereichsprüfung, Seek und
Schreibtor; eine Spur, die dabei unter drei Intervalle fällt, wird mit Status 18 abgewiesen.
Der Zufallsstrom ist festgelegt, damit ein Werkzeug das Muster offline nachrechnen oder mit
demselben Startwert exakt wiederholen kann (zwei Laufwerke gleich beschreiben): Startwert
s (0 → 0x9E3779B9) wird gemischt (`s *= 0x9E3779B1; s ^= s >> 16; s *= 0x85EBCA6B;
s ^= s >> 13`, 32 Bit; 0 → 0x9E3779B9), jede Entscheidung ist ein xorshift32-Schritt
(`x ^= x << 13; x ^= x >> 17; x ^= x << 5`): bei DROP entscheidet Bit16 der Ausgabe, bei
JITTER ist r = ((Ausgabe >> 8) mod (2·jitter + 1)) − jitter. Die Marker werden in Reihenfolge
der Intervalle abgearbeitet; ein Intervall ohne Marker verbraucht keinen Schritt.

Ende-Rahmen: hinter `start_offset_applied` folgen `weak_applied` u32 (verschobene plus
entfallene Flusswechsel dieses Auftrags, 0 ohne Bit3) und `weak_seed` u32 (der verwendete
Startwert, gleich dem gesendeten oder der vom Board gewählte; 0 ohne Bit3); die
Ende-Nutzdaten wachsen von 30 auf 38 Byte, ältere Hosts lesen weiterhin nur die vorderen
Felder. Gegenprobe am Gerät: die Spur mit ANALYZE (≥ 3 Umdrehungen) lesen und mit Op 44 die
feinen Weak-Positionen holen; die Differenzen müssen in den markierten Fenstern liegen
(Abnahmeplan). Host: `write_track(deltas, extended=True, weak_jitter=, weak_seed=)` mit den
Konstanten `WEAK_JITTER`/`WEAK_DROP` in den Intervallen, `mark_weak(deltas, fenster, drop=)`,
`apply_weak(deltas, jitter, seed)` als Nachrechnung, `WriteResult.weak_applied`/`weak_seed`,
`ufi --extended write … --weak-jitter N [--weak-seed S] --weak-window A:B[,…]
[--weak-drop-window A:B[,…]]`.

### 4.17 Rohprüfung je Schreibauftrag (`flags2` Bit4, 1.18-dev)

Der Zeit-/Dichte-Fingerabdruck aus 6.3 (64 indexbezogene Fenster × 8 logarithmische
Intervallklassen, Op 31/32) lief bisher nur zusammen mit dem Firmware-Prüflesen (WRITE
Bit0). Der erweiterte Pfad weist Prüflesen aber ab, wenn die Quelle keine dekodierbaren
Referenzsektoren hat, also gerade bei Schutz- und Rohspuren, für die ein formatunabhängiger
Vergleich gedacht ist. `flags2` Bit4 fordert den Fingerabdruck ohne Inhaltsreferenz an:

- Anfrage: WRITE/GUARDED_WRITE `flags2` Bit4, keine weiteren Bytes. Verlangt Bit6
  (`index_ticks`, also den erweiterten Pfad): das Profil wird auf die Quellumdrehung
  normiert. Nicht zusammen mit Bit0 (dort entsteht der Fingerabdruck über Op-31-Flag 0x01).
  Op 39 Feld 17: 1 = ohne Bit6, 2 = mit Bit0. Eine Quelle, aus der kein Profil entsteht (ein
  Intervall über die Indexdauer hinaus), wird **vor** Seek und Schreibtor mit Status 16
  abgewiesen. Ankündigung: Op-38-Maske `flags2` 0x1F (WRITE_LIMITS-Flags sind voll belegt).
  Mit Weak-Markern (Bit3) kombinierbar; das Profil entsteht dann aus den gewürfelten
  Intervallen, die das Board tatsächlich schreibt.
- Ablauf: Quellprofil nach dem Empfang der Daten, Schreiben, Rücklesen von zwei
  Umdrehungen, Profil der Umdrehung 0, Vergleich mit der Op-31-Toleranz (Vorgabe 30 ‰;
  Op-31-Flag 0x01 ist **nicht** nötig). Der Bericht steht danach in Op 32 (`[32]`, 288 Byte,
  Aufbau wie in 6.3). Ende-Rahmen: `verify_level` 2, wenn der Fingerabdruck passt;
  `verify_level` 1 (Schreiben abgeschlossen) mit Ende-Status 0, wenn er abweicht
  (Diagnose, Vorgabe); Ende-Status 6, wenn Op-31-Flag 0x02 `REQUIRE_TIMING_MATCH` gesetzt
  ist. Die Zählplausibilität (±5 %) wird auf diesem Weg nicht angewandt; die Impulszahlen
  stehen im Bericht. Greaseweazle- und SD-Schreibaufträge sind nicht betroffen.
- Grenzen wie in 6.3: Das Profil ist verlustbehaftet, gleiche Profile können verschiedene
  Spuren haben, Indexversatz und Jitter erzeugen Unterschiede; kein Inhaltsbeweis, keine
  Kopierschutzfreigabe. Host: `write_track(..., extended=True, index_ticks=, raw_check=True)`,
  `raw_verify_options(flags=, tolerance_permille=)`, `raw_verify_report()`,
  `ufi --extended write … --raw-check`, `ufi raw-check [--tolerance N] [--profile] [--require] [--off]`.

### 4.18 Sektor-Befundbericht mit Prüfmasken (Op 47, 22.10.2026)

Die Firmware sammelt bei der Detailanalyse (synchron und Analysejob) sowie beim
sektorbasierten Firmware-Prüflesen zusätzliche Beobachtungen. Kein neues Startflag:
die bestehenden Decoder-/Toleranzeinstellungen bleiben maßgeblich. Reines READ,
Spurübersicht, Rohprofil-Prüflesen und Greaseweazle erzeugen diesen Bericht nicht.
Die bisherigen Berichte und ihre Flagbytes bleiben unverändert.

`ANALYSIS_JOB 0x0F [47]` fragt die Unterstützung ab, auch während einer Operation:
16 Byte, Byte0 Schema1, Byte1 Kopfgröße32, Byte2 Datensatzgröße32, Byte3 maximale
Seitengröße8, bei4 u32 unterstützte Befundmaske `0x0001EFFF`, Byte8 Kapazität64,
Rest0. Keine SD-Zugriffe. Ältere Firmware weist die unbekannte Operation ab.

Seite abrufen: `[47, analyse_generation:u32, erster:u8, anzahl:u8]`, insgesamt7 Byte.
Generation aus dem QX-Analysebericht (u32 bei Offset36) verwenden, nicht Konfigurationsgeneration oder
Job-ID. `anzahl`1–8, `erster`0–Datensatzanzahl; der Endcursor liefert einen leeren Kopf.
Während aktiver Operationen BUSY, bei fehlendem/veraltetem Bericht NOT_FOUND,
bei ungültiger Länge, Generation0, Cursor oder Seitengröße BAD_ARGS. Es gibt keinen
beständigen Snapshot über eine neue Analyse hinweg: jede Seite muss zur selben
Analyse-Generation gehören. Beim Start einer neuen Detailanalyse wird der Speicher
geleert. Fehler-/Abbruchberichte können unvollständig sein.

Optional12 Byte: an die7 Byte `erwartetes_C, erwartetes_H, erwartetes_N,
erste_Sektornummer, letzte_Sektornummer` anhängen. C/H/N=255 bedeutet unbekannt,
N sonst0–7 (128<<N Byte), Sektorintervall inklusive, erste≤letzte. Das ist eine vom
Tool erklärte Erwartung, keine automatisch erkannte Geometrie. Die Firmware
vergleicht sie ausschließlich für diese Antwort, ohne den Bericht oder Decoder zu
verändern. So lassen sich auch abweichende Längen oder Seitennummern gezielt prüfen.
Die gleiche Erwartung für alle Seiten verwenden. Ein CBM-/Apple-Kopf enthält keine
Seitenkennung; dafür bleibt die Seitenprüfung auch bei vorgegebenem H unausgeführt.

Antwortkopf32 Byte:

| Offset | Typ | Bedeutung |
|---|---|---|
| 0–3 | 4×u8 | Schema1, Kopf32, Datensatz32, Decoderformat |
| 4 | u32 | Analyse-Generation |
| 8–10 | 3×u8 | Gespeicherte Datensätze, erster, gelieferte Anzahl |
| 11 | u8 | Bit0 Beobachtungen verworfen (Speicher voll), Bit1 Analysefehler, Bit2 Format mehrdeutig |
| 12 | u32 | Verworfene Beobachtungen, sättigend; keine Anzahl eindeutiger Sektoren |
| 16 | u32 | Unterstützte Befundmaske |
| 20–23 | 4×u8 | Aufnahmespur, Seite, Viertelspur-Positionsbyte, Analysestatus |
| 24 | u32 | Decodierrate |
| 28–29 | 2×u8 | Abgeschlossene nominale Umdrehungsmaske, angeforderte Umdrehungen |
| 30 | u8 | 1 bei expliziter Profilerwartung in dieser Anfrage, sonst0 |
| 31 | u8 | Reserviert0 |

Anschließend höchstens8 Datensätze à32 Byte; maximale Antwort288 Byte:

| Offset im Datensatz | Typ | Bedeutung |
|---|---|---|
| 0–3 | 4×u8 | Beobachtetes C/H/R/N in der bestehenden Decoder-Konvention |
| 4 | u32 | `checked`: mindestens einmal tatsächlich ausgeführte Prüfungen |
| 8 | u32 | `findings`: jemals beobachtete Auffälligkeiten |
| 12 | u32 | Position des ersten Feldes mit bekannter Länge, in275-MHz-Ticks relativ zur Umdrehung; bei reiner Kopfbeobachtung Kopfende |
| 16,18 | je u16 | Erwartete und erreichte Feldlänge dieser Beobachtung, Einheit bei23 |
| 20 | u8 | Ursachenmaske: Bit0 abgeschlossenes Datenfeld, Bit1 Kopf/Block abgewiesen oder Kopf beobachtet, Bit2 lange Lücke, Bit3 ungültiger Code, Bit4 Aufnahmeende, Bit5 neue GCR-Synchronisation |
| 21–22 | 2×u8 | Beobachtete Umdrehungen und PLL-Profile als Bitmasken |
| 23 | u8 | Längeneinheit:0 Nutzbytes,1 Decoderbytes inkl. Markierungen/Prüfsumme,2 Amiga-Rohlongwords; Länge0 bedeutet nicht bestimmt |
| 24–25 | 2×u8 | Erwartetes C/H (ohne explizites Profil: Aufnahmezuordnung, CBM C=Aufnahmespur+1) |
| 26–29 | 4×u8 | Nur bei Profil: erwartetes N, erster/letzter Sektor, Profilkennzeichen1; sonst0 |
| 30–31 | 2×u8 | Rohwert des Format-Offsets, Regel-ID; derzeit AmigaDOS Sektoren bis Lücke und Regel1, sonst0 |

Ein Eintrag fasst gleiche CHRN-IDs über Umdrehungen und PLL-Profile zusammen.
Masken werden vereinigt: ein später gutes Feld löscht einen früheren Fehler nicht.
Die Längen/Position gehören zur ersten Beobachtung mit bestimmter Länge; sie sind
kein eigener Datensatz pro Ursache. Für die genaue Wiederholung Rohdaten behalten.
`findings & bit` bedeutet auffällig, `checked & bit` bei freiem Befundbit bedeutet
mindestens einmal ohne diesen Befund geprüft. Beide Bits0 bedeutet **nicht geprüft**,
nicht „korrekt“. Wiederholte IDs mit anderen Daten sind kein Beweis nachträglichen
Schreibens seit Formatierung und kein physischer Weak-Nachweis.

| Bit | Name | Bedeutung / Umsetzung |
|---|---|---|
| 0 | DATA_BAD | Datenprüfsumme geprüft und falsch; auch bei tolerierter Annahme |
| 1 | DATA_UNCHECKED | Datenprüfung nicht möglich, z.B. unvollständiges Feld oder nicht unterstützter HP-ECC-Block |
| 2 | TRACK | Kopfspur weicht von Erwartung ab |
| 3 | SIDE | Kopfseite weicht von Erwartung ab |
| 4 | LENGTH | N weicht vom erklärten Profil ab; ohne Profil bei IBM auch N>3 außerhalb der Decoderkapazität |
| 5 | NUMBER | Sektornummer außerhalb erklärtem Profil bzw. formatbekannter Grenzen |
| 6 | TRUNCATED | Datenfeld durch Lücke, Codefehler, GCR-Sync oder Aufnahmeende unterbrochen |
| 7 | REJECTED | Beobachtung vom Decoder unter aktueller Policy verworfen; kein Image-Exporturteil |
| 8 | EXTRA | Nichtleere Amiga-Labels/Mac-Tags oder abweichende CBM-Kopffüllbytes; nicht automatisch versteckte Daten |
| 9 | ENDING | Abweichende GCR-Schlussmarken/Füllbytes |
| 10 | BLOCK_ID | Abweichendes Amiga-Formatbyte, Mac-Datensektor-ID oder nicht unterstützter HP-ECC-Block |
| 11 | OFFSET | AmigaDOS-Regel1: Sektoren-bis-Lücke außerhalb1…11 (DD) bzw.1…22 (HD); keine allgemeine Winkelabweichung |
| 12 | PROTECTION | Im Sektorbericht reserviert; Kopierschutz-Hinweise und konkrete Signaturregeln separat über Op56 (4.23) |
| 13 | VARIANTS | Unterschiedliche Inhalte bei wiederholten prüfsummengültigen Beobachtungen; kein KryoFlux-`+n`-Äquivalent |
| 14 | HEADER_BAD | Kopfprüfsumme falsch |
| 15 | NO_HEADER | Keine brauchbare Kopfzuordnung |
| 16 | CODING | Kodierungsfehler, sofern der betreffende Decoder diese Prüfung ausführt |
| 17 | DATA_ABSENT | Reserviert für Formate ohne Datenprüfsumme; nicht mit falscher/nicht prüfbarer Prüfsumme gleichsetzen |

Ohne explizites Profil wird für IBM kein Sektornummernbereich geraten. Die bekannten
Grenzen der übrigen Decoder sind nicht stets die konkrete Diskettengeometrie (z.B.
CBM höchstens21 Sektoren statt automatisch zonenspezifischer Anzahl). Kopf-CRC-Fehler,
fehlende Köpfe und Viertelspurmodus verhindern die automatische Positionsbewertung.
Mit explizitem Profil müssen Kopfkennung und Kopfprüfung verwertbar sein.

Abgebrochene Felder an der Aufnahmegrenze können normale, über den Index laufende
Sektoren sein. Nicht jeder verworfene Synchronisationskandidat besitzt eine
zuordenbare Sektor-ID und damit einen Eintrag. Ein voller Bericht meldet den Verlust
ausdrücklich. PROTECTION und DATA_ABSENT sind reserviert, weil ohne konkrete
Formatregeln keine belastbare Aussage möglich ist. Exportentscheidungen entsprechend
KryoFlux `*N` gehören weiterhin zum Tool. Die Kürzel waren eine Anregung für die
Diagnose, keine Übernahme der DTC-Semantik oder des DTC-Protokolls.

Speicher:64 Befunde plus64 Inhaltsvarianten (4.20) in3856 Byte SRAM1,
kein Heap, keine unbeschränkte Liste. AmigaDOS-Regel1 wird nur bei verwertbarem
Header geprüft; Bytes30/31 bewahren den zuletzt beobachteten auffälligen Rohwert
und die Regelkennung. Andere Format-Offsets bleiben ungeprüft. Grundlage:
[Greaseweazle AmigaDOS-Decoder](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/codec/amiga/amigados.py)
(Formatbyte, Sektor-ID und `0 < togo <= nsec`).
Bestehende Start-, Toleranz- und Greaseweazle-Flags ändern sich nicht.

### 4.19 Gespeicherte Flux-Aufnahme abrufen und erneut analysieren (Op48/52)

Alle folgenden Ops sind Unterbefehle von `ANALYSIS_JOB 0x0F`, keine
Greaseweazle-Kommandos. Mehrbyte-Werte bleiben little endian. `[48]` liefert
ein Manifest der letzten abgeschlossenen und finalisierten Aufnahme:
32 Byte Kopf, danach je Umdrehung8 Byte (höchstens8 Umdrehungen).

| Offset | Typ | Inhalt |
|---:|---|---|
| 0–3 | 4×u8 | Schema1, Op48, maximal128 Samples pro Seite, verfügbare Umdrehungen |
| 4,8,12 | je u32 | Speicher-Aufnahme-ID, eingefrorene Mediengeneration, Beleg-Capture-ID |
| 16–19 | 4×u8 | Spur, Seite, Positionsbyte wie Op47, Speicherart (1 U11,0 interner Puffer) |
| 20 | u32 | Timerfrequenz275000000 Hz |
| 24 | u8 | 1 bei synthetischen Umdrehungsgrenzen, sonst0 |
| 25–27 | Bytes | Reserviert0 |
| 28 | u32 | Inhaltsfingerabdruck der gesamten Aufnahme einschließlich Grenzen/Kontext |
| 32+8·r | 2×u32 | Samplezahl und Indexperiode der Umdrehung r |

Aufnahme-ID0 bedeutet keine erhaltene Aufnahme. Ein neuer Capture-Start, eine
Speicherumschaltung oder ein CPU-Schreiber (WRITE, Formatierung, SD-Laden/Kopieren,
PSRAM-Selbsttest, GW-Schreibdaten) entwertet die vorherige ID **vor** der Änderung.
Reines Lesen/Analysieren entwertet sie nicht. Standard-READ und GW-Livestreaming
werden nicht nachträglich als erhaltene, vollständige Aufnahmen angeboten.

Seitenrequest16 Byte: `[48, id:u32, rev:u8, first_sample:u32, count:u16,
fingerprint:u32]`. `count`1…128; `rev`0-basiert. ID und Fingerabdruck müssen
zum Manifest passen, sonst NOT_FOUND. Cursor über dem Ende oder ungültige
Umdrehung: RANGE. Cursor am Ende: gültige leere Seite. Die Antwort besteht aus
40 Byte Kopf und `returned_count × 4` Byte absoluten Timestamps relativ zum
jeweiligen Index; **keine** Deltas und keine komprimierten Streambytes.

| Offset | Typ | Inhalt |
|---:|---|---|
| 0–3 | 4×u8 | Schema1, Umdrehung, zurückgegebene Samplezahl, Samplebreite4 |
| 4,8,12,16 | je u32 | Aufnahme-ID, Startcursor, Samplezahl der Umdrehung, Indexperiode |
| 20,24 | je u32 | Eingefrorene Mediengeneration, Beleg-Capture-ID |
| 28 | u8 | Synthetische Grenzen wie im Manifest |
| 29–31 | Bytes | Reserviert0 |
| 32 | u32 | CRC32 über Bytes0…31, danach Bytes36…Ende (CRC-Feld ausgelassen) |
| 36 | u32 | Inhaltsfingerabdruck aus dem Manifest |
| 40 | u32[] | Timestamps in nativen Timerticks |

CRC-Konvention wie `ufi_crc32`: Start0, Verkettung über den zurückgegebenen
CRC-Wert. Der Inhaltsfingerabdruck ist eine schnelle, nicht kryptographische
32-Bit-Mischung: Start2166136261; für jeden normalisierten Timestamp, danach
je Umdrehung Samplezahl/Indexperiode, dann die8 Kontextbytes
(Spur,Seite,Position,synthetisch,Beleg-ID LE), zuletzt Mediengeneration jeweils
`h = (h XOR Wert) * 16777619 mod 2^32`. Das Tool gibt den Manifestwert zurück.
Er ergänzt die Sitzungs-ID um eine Inhaltsbindung; Hash-Kollisionen sind möglich.

Bei USB-Trennung bleiben bereits finalisierte Daten im laufenden MCU erhalten.
Nach Wiederverbindung Manifest abfragen, ID/Fingerabdruck/Grenzen/Kontext mit dem
gespeicherten Manifest vergleichen und erst dann ab dem letzten vollständig
CRC-geprüften Block fortsetzen. Seiten sind idempotent; verlorene Antworten
werden mit identischem Request wiederholt. Aktive Vorgänge werden vom bestehenden
USB-Abbruch beendet und nicht automatisch neu gestartet. MCU-Neustart und
Stromverlust garantieren keine Erhaltung. Bei abweichendem Manifest die lokale
Teilaufnahme verwerfen oder als unvollständig getrennt aufbewahren. Es gibt
keine Wiederaufnahme eines Schreibvorgangs über dieses Verfahren.

Erneute Firmware-Analyse ohne Laufwerkszugriff:
`[52, id:u32, new_job_id:u32, fingerprint:u32, ANALYZE-Parameter]`.
Der Parameterblock entspricht Op1 ab dessen Byte24, Länge4/9/33/37, mit
Detailflag Bit0; Spur/Seite/Positionsbyte müssen zum Manifest passen. Seine
Umdrehungsanforderung wird durch die tatsächlich gespeicherte Anzahl ersetzt.
Ein Durchlauf ohne Retry; Format/Rate/PLL- und Diagnoseoptionen bleiben wählbar.
Der Vergleichsschalter und die Flankentoleranz kommen aus Op11. Status/Ergebnis
werden wie bei Op1 mit neuer Job-ID und neuer Ergebnisgeneration abgeholt.
Statusbyte43 ist1 für diese Speicheranalyse,0 für eine neue Aufnahme.
Ein zwischenzeitlicher Medienwechsel beendet die Speicheranalyse nicht; ihr
Kontext bleibt an die ursprüngliche Aufnahme gebunden und aktualisiert kein
aktuelles Laufwerks-Medienprofil. Beide Ops liefern BUSY bei belegtem Datenpfad.
Op52 wird auch von einem offenen Mehrspurplan/Integritätsjob blockiert.

**Unvollständige Hardware-Verlustaufnahme, ausdrückliches Opt-in (Schema2):**
Wird während der Aufnahme RDATA-/INDEX-Overcapture erkannt, endet sie mit
Status6 (`DMA`). Die normale Op48-Abfrage bietet diese Daten weiterhin nicht als
vollständiges Archiv an. `[48,1]` fragt stattdessen ein festes48-Byte-Manifest ab.
Es beschreibt ein chronologisch geordnetes erhaltenes Rohdatenfenster; beim
Ringpuffer kann dessen ältester Teil bereits überschrieben sein. Kein Nachweis
der Vollständigkeit und keine Lokalisierung oder exakte Anzahl verlorener Flanken.

| Offset | Typ | Manifest Schema2 |
|---:|---|---|
| 0–3 | 4×u8 | Schema2, Op48, maximal128 Samples je Seite, Fenster vorhanden0/1 |
| 4,8,12 | je u32 | Aufnahme-ID, Mediengeneration, reserviert0 |
| 16–19 | 4×u8 | Spur, Seite, Positionsbyte, Speicherart wie Schema1 |
| 20 | u32 | Native Timerfrequenz275000000 Hz |
| 24 | u8 | Flags6: Bit1 INCOMPLETE, Bit2 ABSOLUTE_TIMESTAMPS |
| 25,26,27 | 3×u8 | Status6, Verlustmaske (Bit0 RDATA, Bit1 INDEX), reserviert0 |
| 28 | u32 | Nicht kryptographischer Inhaltsfingerabdruck |
| 32,36 | je u32 | Zahl erhaltener Samples, Indexperiode0 (nicht bestätigt) |
| 40,44 | je u32 | Logischer Startsample des Fensters, reserviert0 |

Ohne erhaltenes Fenster sind ID/Anzahl/Status/Flags0. Seitenrequest17 Bytes:
`[48,id:u32,region=0:u8,offset:u32,count:u16,fingerprint:u32,selector=1:u8]`.
Die Antwort verwendet den40-Byte-Seitenkopf aus Schema1 mit Schema2 bei Byte0,
Region0 bei Byte1, Indexperiode/Beleg-ID0, Flags6 bei28, Status6 bei29 und
Verlustmaske bei30. Byte31 bleibt0. Nutzdaten folgen ab40, höchstens128 Samples.
CRC32 und ID-/Fingerprint-Bindung entsprechen dem Seitenverfahren oben.
Unbekannter Selektor oder Count0/>128: BAD_ARGS; fremde ID/Fingerprint:
NOT_FOUND; Region ungleich0 oder Offset hinter dem Ende: RANGE. Am Ende ist
eine leere Seite gültig. Verlorene Antworten dürfen identisch wiederholt werden.

Die Samples sind **unveränderte absolute32-Bit-TIM2-Zeitstempel modulo2^32**,
keine indexrelativen Zeiten, keine Deltas und keine64-Bit-Wanduhr. Die Reihenfolge
folgt dem logischen Aufnahmefenster. Verlorene Ereignisse werden weder ergänzt
noch interpoliert. Fingerprintfolge: FNV-Start2166136261, dann Aufnahme-ID,
logischer Start, Samplezahl, vier Positions-/Verlustbytes, alle Rohzeitstempel,
zuletzt Mediengeneration, jeweils `(h XOR Wert)*16777619 modulo2^32`.
Die ID/Fingerprint-Bindung ist sitzungsbezogen und nicht kollisionssicher.

Neue Aufnahme, Speicherumschaltung oder anderer Pufferschreiber entwerten auch
dieses Fenster. MCU-Neustart/Stromverlust erhalten es nicht garantiert. Op52
akzeptiert diese unvollständigen Fenster nicht. Der erste Manifestabruf berechnet
den Fingerprint nach gestoppter DMA; dabei wird kein weiterer Vollpuffer angelegt.
Dieser Abruf ist für Hardware-Overcapture vorgesehen, nicht als allgemeines
Archiv für jeden Abbruch-, Medienwechsel- oder USB-Fehler.

### 4.20 Einzelne Inhaltsvarianten (Op49) und Informationsgewinn

`[49]` liefert16 Byte Discovery: Schema1, Kopfgröße32, Satzgröße32,
Seitengröße8, Kapazität64, danach0. Seitenrequest
`[49, generation:u32, first:u8, count:u8]`, `count`1…8. Die Generation gehört
zum Analyseergebnis, nicht zur Speicher-Aufnahme-ID. BUSY während Operationen,
NOT_FOUND bei veralteter Generation, BAD_ARGS bei ungültigem Cursor/Umfang.
Ein Cursor genau am Ende liefert nur den32-Byte-Kopf.

| Kopfoffset | Inhalt |
|---:|---|
| 0–3 | Schema1, Kopfgröße32, Satzgröße32, Format |
| 4 | u32 Ergebnisgeneration |
| 8–11 | Gesamtzahl, erster Index, Anzahl dieser Seite, Flags (Bit0 Überlauf,Bit1 Analysefehler) |
| 12 | u32 Anzahl nicht gespeicherter Variantenbeobachtungen, sättigend |
| 16–19 | Spur, Seite, Positionsbyte,1 (Fingerabdruckvergleich) |
| 20–31 | Reserviert0 |

| Satzoffset | Inhalt |
|---:|---|
| 0–3 | C,H,R,N |
| 4,8 | je u32 CRC32 und FNV-1a32 über den beobachteten Inhaltsblock |
| 12,16 | je u32 erste/letzte beobachtete Feld-Endposition, Timerticks relativ zum Index |
| 20 | u32 vereinigte Befundmaske |
| 24,26 | je u16 Blocklänge und Zahl der Beobachtungen (sättigend) |
| 28–30 | Masken der beobachteten Umdrehungen, PLL-Profile, gültigen Umdrehungen |
| 31 | Reserviert0 |

Gültige Umdrehungen benötigen Decoderannahme ohne Daten-/Headerprüfsummenfehler,
fehlenden Header oder gemeldeten Kodierungsfehler. Auch fehlerhafte Varianten
bleiben als Beobachtung erhalten. Identität: CHRN, Blocklänge und beide Hashwerte.
Die Blocklänge umfasst Amiga529 Byte (512 Daten+16 Label+Lückenoffset), CBM258
(Daten+Disk-ID), Mac525 (Tags+Daten+Format), sonst Decoder-Nutzdaten. FNV-1a32
startet bei2166136261, pro Byte XOR und Multiplikation mit16777619.
Keine Mehrheitsentscheidung und keine Zusammenführung widersprüchlicher Inhalte.
Die Nutzdaten selbst werden nicht nochmals intern kopiert; Op48 liefert die
erhaltenen Rohdaten zur Rekonstruktion im Tool. Die Variante gehört zu ihrer
Ergebnisgeneration; vor einem Retry benötigte Rohaufnahmen zuerst abrufen.

Op11 akzeptiert zusätzlich Policy3 `STOP_ON_NO_NEW_CONTENT`. Bestehende
RETRY_MISSING/RETRY_ERRORS-Flags bestimmen den Wiederholungsbedarf. Neue gültige
CHRN+CRC32/FNV32-Identitäten gegenüber den bisherigen Versuchen gelten als Gewinn,
auch wenn die Anzahl guter Sektoren unverändert ist. Nach der konfigurierten Zahl
Versuche ohne neuen Inhalt endet der Job mit Grund2. Versuchslimit, Budget und
EXPECT_SET-Endprüfung bleiben gültig. Es werden höchstens64 Identitäten über
Versuche gespeichert. Bei voller Registrierung oder verloren gegangenen
Variantenbeobachtungen wird kein Stillstand behauptet; dann begrenzen Zeit/Versuche.
Versuchsbewertung (Jobabschnitt16), Byte18: neu registrierte gültige Identitäten;
Byte19 Bit0: Registrierung unvollständig. Das ist CRC-basierter Informationsgewinn,
keine kryptographische Gleichheit oder Wiederherstellung aus mehreren Versuchen.

**Policy4 `INVESTIGATE_VARIANTS` (1.18-dev):** benötigt weiterhin ein Retry-Flag
und mindestens zwei Umdrehungen pro Versuch. Zusätzlich zu gültigen Varianten
zählen nominal beobachtete Varianten mit fehlerhafter Datenprüfsumme bei brauchbarem
Header sowie der Übergang einer bekannten Variante zu gültiger Datenprüfsumme.
Reine alternative PLL-Beobachtungen und unsichere/fehlende Header gelten nicht
als neue unabhängige Variante. REV_COMPARE wird für diese Policy automatisch
aktiviert; eine verkürzte Formaterkennung bleibt damit aus.

Je vier der128 Vergleichsfenster ergeben einen von32 Bereichen. Mindestens drei
fehlende/zusätzliche Flanken markieren einen Bereich. Ein gegenüber bisherigen
Versuchen neues32-Bit-Bereichsmuster zählt einmal als zusätzlicher Gewinn,
einschließlich des ersten unveränderten Ausgangsmusters. Es werden höchstens
acht Muster und64 Inhaltsidentitäten gespeichert. Das ist eine grobe Evidenz für
die Planung weiterer Leseversuche, kein Kopierschutzurteil und keine vollständige
Timing- oder Nutzdatenrekonstruktion.

Policy4 liest auch bei bereits fehlerfreien Sektoren weiter, bis das konfigurierte
Stagnationslimit ohne neue Evidenz erreicht ist. Fehlende Vergleichsevidenz oder
überfüllte Registrierung erlauben keinen vorzeitigen Stagnationsschluss; dann
begrenzen Budget und Versuchszahl. Bewertungsbyte18 zählt die neuen Evidenzeinträge;
Byte19 Bit0 meldet unvollständige Inhaltsregistrierung, Bit1 fehlenden Rohvergleich.
EXPECT_SET und die bisherigen Endprüfungen bleiben wirksam. Die neue Policy ist
ausdrücklich zu wählen; Policies0–3 behalten ihre bisherigen Aufgaben.

### 4.21 Zeitabweichungen über mehrere Umdrehungen (Op51)

Aktivierung wie bisher über Op11 `report_mask` Bit0 `REV_COMPARE` für Analysejobs
einschließlich Op52. Gegenüber Umdrehung0 werden Umdrehungen1…7 monoton und
eins-zu-eins verglichen, mit linearer Normierung auf die Referenz-Indexperiode.
`[51]`:16 Byte Discovery mit Schema1,Fensterzahl128,Satzgröße32,Seitengröße8,Rest0.
Request `[51, generation:u32, first_window:u8, count:u8]`, maximal8 Fenster.
Cursor128 liefert leere Seite; größerer Cursor BAD_ARGS. BUSY während Operationen;
ohne gewählten, fertiggestellten Vergleich oder mit falscher Generation NOT_FOUND.

Kopf32 Byte: Bytes0–3 Schema1,Fensterzahl128,erster Index,Anzahl; u32 bei4 Generation,
bei8 Toleranz in Ticks,bei12 Referenzperiode. Byte16 verglichene Umdrehungsmaske,
Byte17=1 (nur Indexausrichtung), Rest0. Je32-Byte-Satz:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0,4,8 | je u32 | Zugeordnete, fehlende, zusätzliche Flanken |
| 12,16 | je u32 | Mittlere absolute und maximale absolute Zeitabweichung |
| 20 | i32 | Mittlere vorzeichenbehaftete Abweichung (Beobachtung minus Referenz) |
| 24,25 | je u8 | Masken der Umdrehungen mit Zuordnungen bzw. Differenzen |
| 26–31 | Bytes | Reserviert0 |

Mittelwerte werden ganzzahlig gegen0 gerundet und beziehen sich ausschließlich
auf zugeordnete Flanken innerhalb der Toleranz. Fehlende/zusätzliche Flanken
separat auswerten; fehlende Zuordnungen ergeben Mittelwert0 ohne Aussage über
Gleichheit. Die Statistik wiegt Flanken, nicht Umdrehungen, gleich. Gleichmäßige
Drehzahldifferenz wird ausgeglichen; lokaler Jitter, Indexversatz und echtes
wechselndes Verhalten können sich weiterhin überlagern. Keine automatische
Kopierschutz- oder physische Weak-Bit-Klassifikation. Statistik4096 Byte in SRAM4,
explizit je Vergleich initialisiert; bisherige Berichte behalten ihr Layout.

### 4.22 Präzises Roh-Prüflesen mit U11-Referenz (Op50/54)

`[50]` liest die Einstellungen. Setzen:12 Byte
`[50, schema=1, flags:u8, reserved=0, tolerance_ticks:u32, phase_ticks:u32]`.
Flags Bit0 aktiviert den Vergleich, Bit1 verlangt Übereinstimmung (nur mit Bit0).
Andere Bits BAD_ARGS. Vorgabe aus; Toleranz1…1000000, Vorgabe110 Ticks.
Setzen nur ohne aktiven Vorgang, offenen Mehrspurplan oder Integritätsjob;
es erhöht die Konfigurationsgeneration. Aktivieren ohne U11: NOT_IMPL.

Antwort24 Byte: Bytes0–3 Schema1,Flags,Bericht-Op54,Fensterzahl128; u32 bei4
Toleranz,bei8 Phase,bei12 reservierte Referenzkapazität in u32-Worten.
Bytes16/17: Referenz bereit/Bericht verfügbar, Byte18=1 (sitzungslokal), Byte19=0;
u32 bei20 aktuelle Bericht-ID oder0. Die Phase verschiebt nur die erwarteten
Zeitpositionen für den Vergleich; sie verändert nicht die Schreibausgabe.

Aktivierung reserviert die obere Hälfte des8-MiB-U11 als Referenz. Capture-Daten
und der komplette DMA-Überlaufchunk bleiben in der unteren Hälfte. Nutzbare
Capture-Kapazität dann1015808 Samples, Referenz1048576 Samples; ohne Reservierung
2064384 Capture-Samples. Deaktivieren gibt die Hälfte wieder frei. Die Umschaltung
entwertet erhaltene Aufnahmen. WRITE liest die Kapazität bei jeder Vorbereitung
neu. Die Reservierung kostet kein zusätzliches AXI-Array; Vergleichszustand liegt
in SRAM4. Diese Speichergröße ist kein Nachweis erreichbarer U11-DMA-Bandbreite.

Anwendung für native indexgebundene WRITE-Aufträge mit erweitertem u32-Pfad,
Quell-Indexperiode und Firmware-Prüflesen (WRITE Bit0 oder `flags2` Bit4 Rohprüfung).
Nach dem Weak-Durchlauf und vor WGATE kopiert die Firmware die tatsächliche
Vorlage als kumulierte Timestamps nach U11. Splice-Offset, zusätzliche Gate-Vor-/
Nachläufe, klassischer u16-Pfad oder fehlende Periode werden vor WGATE abgelehnt.
Phase plus kumulierte Deltas müssen in eine Referenzumdrehung passen; Umbruch
über die Indexgrenze benötigt ein späteres Teilbereichsmodell. Ein partieller
Schreibbereich kann deshalb keine uneingeschränkte Spurgleichheit behaupten.

Beide Rückleseumdrehungen werden einzeln mit der Vorlage verglichen. Im strengen
Modus führt jede fehlende/zusätzliche Flanke außerhalb der Toleranz zu Status6;
im beobachtenden Modus bleibt der bisherige Prüflesestatus maßgeblich und der
genaue Bericht zeigt die Differenzen. Ungültige Timestamps, Abbruch und Timeout
bleiben Fehler. Der Vergleich erhöht den Inhaltsnachweis nicht automatisch auf
Stufe3. Drehzahl-Normierung und monotone Zuordnung wie4.21; es gibt keine heimliche
Phasenanpassung. Indexjitter und Laufwerkselektronik können Abweichungen verursachen.

`[54]`: Discovery16 Byte (Schema1,Kopf32,Satz12,Seitengröße8,Fenster128,Rest0).
Request `[54, report_id:u32, first_window:u8, count:u8]`. Bericht-ID aus Op50;
BUSY während Operationen, NOT_FOUND bei fehlendem/veraltetem Bericht.
Cursor0…128, count1…8. Antwort32 Byte Kopf plus12 Byte je Fenster:
Bytes0–3 Schema1,erster Index,Anzahl,Umdrehungsmaske (Bits1/2 = Rücklese0/1);
u32 bei4 Bericht-ID,8 Toleranz,12 Referenzperiode,16 Phase,20 Quell-Flankenzahl;
Byte24=1 (explizite Indexausrichtung),25 Flags,26…31=0.
Satz: u32 fehlende Flanken, u32 zusätzliche Flanken, u8 Umdrehungsmaske,3 Byte0.
Vorbereitung eines neuen WRITE oder Änderung von Op50 entwertet den Bericht.
Greaseweazle verwendet weiterhin seinen bestehenden Protokoll- und Schreibpfad;
die nativen Vergleichsflags werden dort nicht angewendet. Eine aktive U11-
Reservierung begrenzt weiterhin die gemeinsame verfügbare Speicherkapazität.

### 4.23 Flaggesteuerte Kopierschutz-Hinweisanalyse (Op55/56)

Die Firmware wertet beim nativen Detail-ANALYZE sowie bei Analysejobs und
Speicheranalyse Op52 auf Wunsch Kopierschutzmerkmale aus. Sie meldet konkrete
Beobachtungen, Schutzkandidaten und bekannte Signaturbestandteile. Ein Kandidat
kann auch durch Medienfehler oder Lesebedingungen entstehen. Keine automatische
Reparatur, keine Änderung der Rohdaten und kein höherer WRITE-Prüflesegrad.
Sektorbasiertes WRITE-Prüflesen, gewöhnliches READ, Spurübersicht und Greaseweazle
aktivieren diese Klassifikation nicht. Standardmäßig sind alle Prüfgruppen aus.

#### Einstellungen und sechs Flags: Op55

`ANALYSIS_JOB 0x0F [55]` liest die Einstellungen. Setzen: genau8 Byte
`[55, schema=1, flags:u16, min_revs:u8, min_repeat:u8, timing_permille:u16]`.

| Flag | Name | Prüfung |
|---:|---|---|
| `0x0001` | CONTENT | Inhaltsvarianten über unabhängige Umdrehungen sowie Differenzen der Rohflanken |
| `0x0002` | ERRORS | Wiederkehrende stabile Daten-CRC-Fehler und Kodierungsfehler |
| `0x0004` | LAYOUT | Doppelte CHRN-Kennungen, Kopfpositionen und Längenauffälligkeiten |
| `0x0008` | TIMING | Lokale Bitzellen-Zeitabweichungen und lange Bereiche ohne Fluxwechsel |
| `0x0010` | POSITION | Verschobene Sektorpositionen und veränderte zyklische Sektorreihenfolge |
| `0x0020` | SIGNATURE | Bekannte, einzeln implementierte Signaturregeln; Katalogversion1 siehe unten |

Maske `0x003F`; andere Bits ergeben BAD_ARGS ohne Teiländerung. `min_revs`2…8,
Vorgabe3: benötigte vollständige Umdrehungen. `min_repeat`2…8, Vorgabe2:
übereinstimmende Wiederholungen für den stabilen CRC-Fehlerbefund.
`timing_permille`10…250, Vorgabe30: Schwelle des absoluten lokalen Unterschieds
zur Median-Bitzellenzeit. Einstellungen gelten für folgende Analysen und werden
beim Beginn übernommen. Der Bericht behält diese Werte auch nach späterer
Umkonfiguration. Keine SD-/Flash-Persistenz; nach MCU-Neustart Flags0.

Setzen ist während einer Operation, bei offenem Integritätsjob oder Mehrspurplan
BUSY; Lesen bleibt erlaubt. Erfolgreiches Setzen erhöht die Konfigurationsgeneration.
Für einen generationgebundenen Start danach die neue Generation verwenden.

Antwort24 Byte:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0,1 | je u8 | Schema1, Bericht-Op56 |
| 2 | u16 | Unterstützte Flags `0x003F` |
| 4 | u16 | Eingestellte Flags |
| 6,7 | je u8 | `min_revs`, `min_repeat` |
| 8 | u16 | `timing_permille` |
| 10–14 | je u8 | Berichtslimit48, Seitengröße8, Kopfgröße32, Satzgröße32, Regelkatalogversion1 |
| 15–23 | Bytes | Reserviert0 |

Beispiel für alle Prüfgruppen mit Standardgrenzen:
`[55, 1, 0x3F, 0, 3, 2, 30, 0]`. Danach einen Detailanalysejob mit vorzugsweise
4…8 Umdrehungen starten oder eine erhaltene Aufnahme über Op52 erneut auswerten.
Op52 erfindet keine weiteren Umdrehungen. Reicht die Aufnahme nicht, meldet der
Bericht unzureichende Prüfung. Ausschalten: `[55, 1, 0, 0, 3, 2, 30, 0]`.

CONTENT veranlasst bei kooperativen Analysejobs den Roh-Umdrehungsvergleich auch
ohne Op11 `REV_COMPARE`. Bei synchronem Detail-ANALYZE sind die sektorbasierten
Regeln verfügbar; der fehlende Rohvergleich wird ausdrücklich gemeldet.
ERRORS aktiviert im Analysedecoder zusätzlich die Kodierungsprüfung. Damit können
Sektoren mit falschen Clockbits im bestehenden Qualitätsbericht anders bewertet
werden. Die eingestellten Decoder-Toleranzflags bleiben maßgeblich; der
Greaseweazle-Pfad und die Bewertung von WRITE werden nicht umgestellt.

#### Bericht und Zustände: Op56

`[56]` liefert16 Byte Discovery: Schema1,Kopfgröße32,Satzgröße32,Seitengröße8,
Limit48,Katalogversion1,danach0. Seitenrequest7 Byte:
`[56, generation:u32, first_record:u8, count:u8]`, `count`1…8.
Die Generation ist die des Qualitätsberichts. BUSY während Operationen;
NOT_FOUND ohne Ergebnis oder bei veralteter Generation; BAD_ARGS für ungültige
Länge/Seitengröße/Cursor. Der Cursor am Ende liefert einen leeren32-Byte-Kopf.
Auch eine Analyse ohne aktivierte Prüfgruppen besitzt einen Bericht mit Zustand0.

| Kopfoffset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema1 |
| 1 | u8 | Zustand gemäß folgender Tabelle |
| 2,3 | je u8 | Erster Datensatz, Anzahl dieser Seite |
| 4 | u32 | Ergebnisgeneration |
| 8,10,12 | je u16 | Angeforderte, ausreichend geprüfte, unzureichend geprüfte Gruppen |
| 14 | u16 | Einschränkungsmasken |
| 16 | u32 | Nicht gespeicherte Beobachtungen/Berichtseinträge, sättigend |
| 20–24 | je u8 | Gesamtzahl der Einträge, `min_revs`, `min_repeat`, vollständige Umdrehungsmaske, Format |
| 25–27 | je u8 | Spur, Seite, Positionsbyte |
| 28 | u16 | `timing_permille` |
| 30,31 | je u8 | Analysestatus, reserviert0 |

| Zustand | Bedeutung |
|---:|---|
| 0 | Nicht aktiviert; keine Kopierschutzprüfung durchgeführt |
| 1 | Keine Hinweise innerhalb der ausreichend geprüften aktivierten Regeln |
| 2 | Auffälligkeit erkannt |
| 3 | Schutzkandidat; Ursache weiterhin mehrdeutig |
| 4 | Konkreter bekannter Signaturbestandteil erkannt |
| 5 | Nicht ausreichend untersucht; kein positiver Befund |

Der höchste vorhandene Befund bestimmt Zustand2…4. Er kann mit unzureichend
geprüften anderen Gruppen zusammen auftreten. Daher immer alle Masken auswerten.
Zustand1 bedeutet niemals „Diskette ungeschützt“. Abbruch, fehlerhafte oder
unvollständige Analyse begrenzt positive Aussagen auf Auffälligkeiten, Zustand2.

Einschränkungen bei Offset14, bitweise:

| Bit | Bedeutung |
|---:|---|
| 0 | Analyse/Beobachtung unvollständig oder fehlerhaft |
| 1 | Begrenzter Speicher voll; Beobachtungen oder Einträge gingen verloren |
| 2 | Weniger vollständige Umdrehungen als `min_revs` |
| 3 | Keine verwertbaren vollständigen Sektorbeobachtungen für die gewählten Regeln |
| 4 | Benötigter Roh-Umdrehungsvergleich fehlt |
| 5 | Rohvergleich ist nur am Index ausgerichtet; Differenzen können Ausrichtungsfehler enthalten |
| 6 | Gewöhnliche Formaterkennung mehrdeutig |
| 7 | Lokale Zeitbasis/Abdeckung für die Dichteprüfung unzureichend |

Ein32-Byte-Datensatz enthält:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0,2 | je u16 | Regel-ID, Prüfgruppe als Flag |
| 4,8 | je u32 | Repräsentative Fundposition und Bereichslänge in Timerticks; normale Bezugsumdrehung0 |
| 12,16 | je u32 | Beobachteter Wert und Vergleichswert, regelabhängig |
| 20–23 | 4×u8 | C,H,R,N; jeweils255 bei spurbezogenen Befunden ohne CHRN |
| 24 | u8 | Umdrehungsmaske; bei Rohdifferenzen Bits1…7 ohne Referenz0 |
| 25 | u8 | Befundstufe:1 Auffälligkeit,2 Kandidat,3 bekannte Signatur |
| 26 | u8 | Qualifier: Bit0 nur Indexausrichtung, Bit1 keine genaue Einzelposition, Bit2 Daten-CRC ungültig, Bit3 Befund aus zusammengefasstem Decoderbericht |
| 27 | u8 | Werteinheit:0 Anzahl,1 Timerticks,2 Promille,3 Formatwert |
| 28 | u32 | Stützende Beobachtungen; je nach Regel Umdrehungen, Sektoren oder Flankenintervalle |

Fundpositionen stammen aus erfassten Feld-Enden bzw. der Flanke, an der ein Sync
erkannt wurde. Sie sind keine erfundenen exakten Sektorstartpositionen. Bei
Qualifier Bit1 sind Position/Bereich nicht als genaue Lokalisierung zu verwenden.
Je nach Regel beziehen sich Masken und Stützzahl auf unterschiedliche Arten von
Beobachtungen; eine größere Zahl ist keine Prozentwahrscheinlichkeit.

#### Regelkatalog1

Nur der nominale PLL-Durchlauf liefert unabhängige Sektorbeobachtungen.
Alternative PLL-Profile zählen nicht als weitere Umdrehungen. Kopf-CRC-Fehler
und fehlende Header verhindern die Zuordnung dieser Sektorregeln.

| ID | Gruppe | Regel und Bewertung |
|---:|---|---|
| 1 | CONTENT | Verschiedene angenommene Inhalte ohne gemeldete Daten-/Kodierungsfehler bei gleicher CHRN über mindestens `min_revs`; Kandidat. Wert/Stütze=Anzahl gültiger Umdrehungen. Nicht aktivierte Kodierungsprüfungen werden dadurch nicht nachträglich bestätigt. |
| 2 | CONTENT | Fehlende/zusätzliche Rohflanken in einem1/128-Fenster; Auffälligkeit. Wert=Summe fehlend+zusätzlich, Vergleich=Toleranz in Ticks, Stütze=Anzahl betroffener Vergleichsumdrehungen. |
| 3 | ERRORS | Gleicher Datenfingerabdruck mit falscher Daten-CRC in mindestens `min_repeat` Umdrehungen; Kandidat bei ausreichender Aufnahme, sonst Auffälligkeit. Keine Behauptung, dass der Fehler absichtlich erzeugt wurde. |
| 4 | ERRORS | Kodierungsfehler im beobachteten Sektor; Auffälligkeit. Wert/Stütze=Anzahl betroffener Umdrehungen. |
| 5 | LAYOUT | Gleiche CHRN an mindestens zwei verschiedenen Winkelpositionen pro Umdrehung; Kandidat bei Wiederholung in `min_revs`, sonst Auffälligkeit. Positionen müssen mehr als1/1000 Umdrehung auseinanderliegen. |
| 6 | LAYOUT | Verwertbarer Header passt nicht zur Aufnahmeposition; Auffälligkeit. Wert/Vergleich sind `(C<<8)\|H`, bei Formaten ohne Seitenfeld ist die Erwartungsseite0. Keine automatische Kopfkorrektur. |
| 7 | LAYOUT | Unterschiedliche N-Werte unter den beobachteten Sektoren; Auffälligkeit. Wert/Stütze=Sektoranzahl, Vergleich=N der ersten Beobachtung. Gemischte Längen können beabsichtigt und legitim sein. |
| 8 | TIMING | Lokaler Median-bezogener Unterschied der geschätzten Bitzellenzeit erreicht `timing_permille`; Auffälligkeit. Wert=absoluter Unterschied in Promille, Vergleich=Schwelle, Stütze=Flankenintervalle. |
| 9 | TIMING | Längste beobachtete Lücke mindestens1/8 Umdrehung; Auffälligkeit, auch bei leeren/defekten Spuren möglich. Wert=maximale Lücke, Vergleich=zugehörige Schwelle, keine genaue Einzelposition. |
| 10 | POSITION | Sektorposition nach Drehzahlnormierung um mehr als1/100 Umdrehung verschoben; Auffälligkeit. Wert=maximale Abweichung zur ersten Beobachtung, Vergleich=Schwelle; Indexjitter nicht ausgeschlossen. |
| 11 | POSITION | Zyklische Reihenfolge derselben eindeutigen Sektormenge verändert; Auffälligkeit. Normales Interleave und reine zyklische Rotation werden akzeptiert. Wert/Stütze=abweichende Umdrehungen. |
| 12 | CONTENT | Wechselnder Inhalt bei ungültiger Daten-CRC und verwertbarem Header über `min_revs`; Kandidat mit Qualifier Bit2. Weak-Verhalten und beschädigte Medien bleiben mögliche Ursachen. |
| 13 | LAYOUT | IBM-MFM/FM-Header N>3 außerhalb der unterstützten Decodergröße; Auffälligkeit aus dem Befundbericht. Wert=N, Vergleich=unterstütztes Maximum3; kein Beweis eines ungültigen Diskettenformats. |
| `0x1001` | SIGNATURE | Moderner Copylock-Sektor6: siehe unten. Wert/Stütze=Anzahl vollständiger Treffer, Vergleich=`min_revs`. |

Bei mehrfach vorkommenden CHRN-IDs werden Inhaltswechsel und Positionswechsel
nicht pauschal als wechselndes Einzelmaterial bewertet; diese Gruppen werden
als unzureichend zuordenbar markiert. CRC32 und FNV-1a dienen als nicht
kryptographische Inhaltsfingerabdrücke. Metadaten-/Labelvarianten können über
Op47/49 zusätzlich untersucht werden; die CONTENT-Regeln vergleichen die
zugeordneten Nutzdaten (Mac einschließlich Tags/Format).

Die Dichteprüfung benötigt pro Fenster mindestens64 geeignete Intervalle mit
mindestens zwei verschiedenen Zellvielfachen. Höchstens10% verworfene Intervalle,
Streuung höchstens5% der mittleren Zellzeit; Median aus mindestens drei geeigneten
Fenstern. Gleichmäßige Drehzahlunterschiede und normale Interleave-Reihenfolgen
sind keine eigenen Schutzkandidaten. Die Aufnahmedauer- und Transportbelege
Op7/43 bleiben für die Bewertung von Messfehlern relevant.

#### Erste konkrete Signaturregel: moderner Copylock-Sektor6

Der unabhängige DD-MFM-Scanner sucht raw Sync `0x8914`, danach die MFM-kodierte
Sektor-ID6, den16-Byte-Kenntext,496 passende Bytes eines nichtnullen23-Bit-LFSR
und einen Null-Abschluss. Die Clockbits des gesamten dekodierten Felds werden
mitgeprüft. Stufe3 erfordert vollständige Treffer in mindestens `min_revs`
unabhängigen, vollständigen Umdrehungen. Ein einzelner Treffer bleibt Stufe1.
Ein Textfund, ein falscher Sync, ein falscher Index, eine beschädigte Datenfolge,
illegale Clockbits, ein falscher Abschluss oder ein abgeschnittenes Feld genügen nicht.

Die Regel erkennt diesen Signaturbestandteil, nicht sämtliche Copylock-Varianten
oder die vollständige Schutzfunktion eines Titels. Die übrigen10 Sektoren,
gemeinsamer Track-Seed und die speziellen schnelleren/langsameren Sektorzeiten
werden damit nicht vollständig bestätigt. Alte/erweiterte Copylock-Varianten
und am Aufnahmeindex durchschnittene Signaturfelder sind noch nicht unterstützt.
Der Scanner startet bei einer DD-Zellzeit von2µs und folgt innerhalb±12,5%.
Grundlage der eigenständigen Implementierung ist die öffentlich freigegebene
[Copylock-Formatbeschreibung in disk-utilities](https://github.com/keirf/disk-utilities/blob/master/libdisk/format/amiga/copylock.c).

Der Bericht hält höchstens48 Einträge und64 CHRN-Beobachtungssätze. Überlauf
wird gemeldet und verhindert einen vollständigen Negativbefund. Speicher liegt
in SRAM4, ohne Heap und ohne zusätzliches großes AXI-Array. `SF_PROTECTION`
im bisherigen Op47 bleibt reserviert; Op56 liefert die regelgebundene Aussage.
Softwaretests mit synthetischen Merkmalen und Referenzformaten ersetzen keine
spätere Laufwerks-/U11-/Signalqualifikation auf der Platine.

### 4.24 Lesekontext im READ-Ende (READ-Flag Bit5, 1.18-dev)

Wünsche F1–F5 der UFT-Abstimmung vom 26.10.2026: wo Daten verloren gingen, wo der Kopf wirklich
stand, welche DENSITY/DRATE-Pegel anlagen, welcher Takt lief und ob `revs` geklemmt wurde – als
**zusätzliche Metadaten**. Der Flussstrom bleibt Byte für Byte unverändert, es gibt keine neuen
Strom-Token. READ und GUARDED_READ mit Flags Bit5 (`0x20`; Op 38 READ-Maske `0x3F`) hängen den Block
als **letzten** an den Ende-Rahmen: hinter den Generationsblock (4.5) und, falls Bit3 gesetzt ist,
hinter den Provenienzblock (4.12). Ende-Nutzdaten: 150 Byte (Bit5), 246 Byte (Bit3 + Bit5); auch bei
Fehlerstatus (6, 3, 21) und Status 10, soweit der Ende-Rahmen Nutzdaten trägt.

**Erkennung durch den Host:** Ältere Firmware weist Bit5 mit Status 14 ab (Op 39: Feld 2, Detail
0x20) – vorher Op 38 lesen (Bit5 in der READ-Maske). Der Block beginnt bei Offset 18 (114 mit Bit3),
Schema-Byte 1, Längenbyte 132. Die vorderen Blöcke und ihre Offsets bleiben unverändert; ohne Bit5
bleibt das Ende 18/114 Byte. Im Integritätsjob (JOB 0B) steht der Block hinter dem 20-Byte-Anhang;
der Job-Ende-Puffer fasst seit 1.18-dev alle Blöcke (vorher wurden Ende-Nutzdaten über 44 Byte –
also auch der Provenienzblock – dort abgeschnitten).

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Länge 132 |
| 2 | u8 | Flags: Bit0 `revs` geklemmt (F5), Bit1 DENSITY/DRATE am Ende anders als am Start, Bit2 Spur/Seite/Status am Ende anders als am Start, Bit3 Offset 16/20 gültig, Bit4 Offset 24–31 gültig, Bit5 Taktquelle TCXO (HSE-Bypass), Bit6 mehr Verlustfenster als Plätze |
| 3 | u8 | `revs` wie angefragt (Rohbyte) |
| 4 | u8 | `revs` wie gelesen: 0 → 1, über 200 → 200 (F5) |
| 5, 6 | je u8 | DENSITY, DRATE am Start (1 = asserted, wie Op-8-Kontext Offset 42/43; F3) |
| 7 | u8 | Am Ende: Bit0 DENSITY, Bit1 DRATE |
| 8, 9 | je u8 | Kopfposition am Start (nach Seek und Seitenwahl): mechanischer Spurzähler, Seite (wie Op 13 Offset 8/9; F2) |
| 10 | u8 | Positionsbyte der Anfrage: 0 ganze Spur, sonst `0x80 | Viertel << 4` (Apple-Viertelspur, Shugart-Halbspur = 2) |
| 11 | u8 | Op-13-Statusbits (niederwertiges Byte) am Start: Bit0 POSITION_VALID, Bit4 POSITION_ESTIMATED, … |
| 12, 13, 14 | je u8 | Spurzähler, Seite, Statusbits am Ende (der Ablaufplan STEP_SCHEDULE 0x1F kann den Kopf während des Lesens bewegen) |
| 15 | u8 | Verlustfenster dieser Aufnahme insgesamt (F1) |
| 16 | i32 | Letzte Taktabweichung in ppm aus SELFTEST `usb_sof` (relativ zum USB-Takt des Hosts; F4) |
| 20 | u32 | Alter dieser Messung in ms |
| 24 | i16 | Letzte zwischengespeicherte Platinentemperatur (SHT40) in 0,01 °C |
| 26 | u16 | Relative Feuchte in 0,01 % |
| 28 | u32 | Alter dieses Werts in ms |
| 32 | u8 | Belegte Verlustplätze (0–4) |
| 33 | 3 Bytes | 0 |
| 36 | 4 × 24 Bytes | Verlustfenster, unbelegte Plätze 0 |

Ein Verlustfenster:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Art: Bit0 RDATA-Überlauf (mindestens ein Flusswechsel fehlt), Bit1 INDEX-Überlauf, Bit2 DMA-Übertragungsfehler, Bit3 Speicher voll/Host zu langsam (danach nichts aufgezeichnet), Bit4 offenes Ende (kein Erkennungszeitpunkt), Bit5 vor dem ersten Index (keine Stromzeit, Offset 1–19 dann 0) |
| 1 | u8 | `FD`-Marken im Strom vor dem Fensterbeginn (Umdrehung des Beginns, 0 = vor dem ersten `FD`) |
| 2 | u8 | dasselbe für das Fensterende |
| 3 | u8 | 0 |
| 4 | u64 | Fensterbeginn in Ticks seit dem ersten Index (Zeitbasis des Stroms, über TIM2-Überläufe exakt aus den Indexabständen summiert): letzter Zeitpunkt, an dem die Firmware die Aufnahme als intakt geprüft hat |
| 12 | u64 | Fensterende in Ticks seit dem ersten Index: Erkennung und Abbruch; 0 bei offenem Ende |
| 20 | u32 | Flusswechsel, die vor dem Fensterbeginn sicher gespeichert waren (Untergrenze, Stromreihenfolge ab dem ersten Index) |

Bedeutung und Grenzen:

- **Jeder Verlust beendet die Aufnahme** (der Strom setzt nach einer Lücke nicht fort), deshalb
  meldet 1.18-dev höchstens **ein** Fenster; die vier Plätze lassen Raum für spätere Erweiterungen.
  RDATA-/INDEX-Überlauf: das Überlaufbit des Timers bleibt bis zum Löschen stehen, darum lag der
  Verlust sicher zwischen der letzten Prüfung ohne Bit (Beginn) und der Prüfung mit Bit (Ende). Die
  Firmware prüft bei jedem TIM2- und DMA-Interrupt und in jedem Hauptschleifendurchlauf; die
  Fensterbreite ist damit typischerweise unter einer Millisekunde, auf der Platine noch nicht gemessen.
  Stromdaten ab dem Fensterbeginn sind nicht vertrauenswürdig; davor sind sie es. Speicher voll /
  Host zu langsam (Status 10): Beginn = Zeitstempel des letzten gespeicherten Flusswechsels, offenes
  Ende; liegt der Beginn hinter dem letzten gesendeten `FD`, sind alle gesendeten Umdrehungen
  vollständig. DMA-Fehler: Beginn = letzte intakte Prüfung, offenes Ende.
- Damit unterscheidet ein Werkzeug eine echte flussfreie Zone (kein Fenster, Status 0) von
  verlorenen Daten (Fenster mit Lage).
- Takt und Klima sind **zwischengespeicherte** Werte (wie Provenienz Offset 90–95): die READ-
  Annahme misst nie, damit sie nicht blockiert. Die ppm stammen aus dem letzten SELFTEST-Punkt
  `usb_sof` gegen den Host-Takt (USB erlaubt dem Host ±500 ppm), also keine absolute Messung.
- Kopfposition: Spurzähler und Gültigkeit sind Firmware-Buchführung wie in Op 13, keine
  unabhängige Messung der Spur.
- Host: `read_track(..., read_context=True)` → `Capture.read_info` (`ReadInfo`, Verlustfenster als
  `LossWindow`; `None` bei älterer Firmware); bei Fehlerstatus trägt die `DeviceError` das Feld
  `read_info`. `ufi read --read-info` druckt die Zusammenfassung.

## 5 Lesen, Schreiben und Prüflesen

### 5.1 Laufzeitdiagnose und automatisches Host-Prüflesen

`DIAGNOSTICS 0D` (INFO-Bit20) ist eine reine Abfrage, auch während eines Jobs.
Antwort: `schema u8=1, flags u8 (Bit0 U11/PSRAM aktiv), owner u8, reserved u8=0`,
danach diese 15 u32-Werte in little endian (insgesamt 64 Byte):

| Index | Name im Host | Bedeutung |
|---|---|---|
| 0 | sample_hz | native Timerfrequenz |
| 1 | store_words | aktuell nutzbare Flux-Wörter, ohne Auffangblock |
| 2 | pending_words | produzierte, noch nicht konsumierte Wörter, auf Kapazität begrenzt |
| 3 | peak_words | größter beobachteter Füllstand der letzten/aktuellen Aufnahme |
| 4 | capture_overruns | Aufnahmeüberläufe |
| 5 | capture_dma_errors | Aufnahme-DMA-Fehler |
| 6 | write_dma_errors | Schreib-DMA-Fehler |
| 7 | write_underruns | erkannte verspätete Schreibpuffer-Nachfüllungen |
| 8 | native_rx_pauses | native RX-Pausen wegen Pufferfüllstand |
| 9 | native_tx_timeouts | native IN-Transfers ohne Abschluss innerhalb 1 s |
| 10 | lease_releases | automatische Freigaben des nativen Besitzers |
| 11 | heap_bytes | begrenzte Heapreservierung |
| 12 | axi_free_bytes | statische AXI-Reserve laut Linker |
| 13 | stack_space_bytes | statischer Abstand DTCM-Daten/Stackobergrenze |
| 14 | native_idle_timeout_ms | 30000 |

Fehler-/Ereigniszähler laufen seit dem Start und können bei 2^32 umlaufen.
Füllstand und Maximum werden beim Abfragen der erzeugten Flux-Wörter erfasst;
es handelt sich nicht um einen lückenlos gemessenen Hardware-Höchststand.
Stack/Heap-Felder melden Reservierungen, keine aktuelle Nutzung. Für tatsächlichen
Stackverbrauch weiterhin SELFTEST `stack_free` verwenden. IRQ-Zähler können
zwischen einzelnen Feldern fortschreiten; dies ist keine transaktionsweite Momentaufnahme.
Host: `Device2.diagnostics()` oder `ufi --port usb diagnostics`.

**Prozessmesswerte (1.18-dev):** `0D` mit exakt einem Nutzbyte `01` liefert
ebenfalls64 Bytes: `schema=1, mode=1, count=13, reserved=0`, danach13 u32-LE-Werte
und8 reservierte Nullbytes. Die leere Anfrage behält das bisherige Format.

| Index | Wert |
|---:|---|
| 0 | Zeitbasis1000 Hz; Zeitmaxima sind Millisekunden aus HAL_GetTick |
| 1 / 2 | Anzahl Hauptschleifendurchläufe / längster Durchlauf |
| 3 / 4 | Anzahl kooperativer Analyseschritte / längster Schritt |
| 5 | Anzahl Prüfleseschritte |
| 6 / 7 | Streaming-Flusswechsel verarbeitet / Cachebytes invalidiert |
| 8 | Gestartete Erkennungs-Kandidatenläufe |
| 9 / 10 | Genutzte Format-Hinweise / Rückfälle vom Hinweis auf vollständige Suche |
| 11 / 12 | Gemeinsame Sektor-Fingerabdruckberechnungen / dabei gelesene Datenbytes |

Die neuen Summenzähler sättigen bei UINT32_MAX; Maxima gelten seit MCU-Start.
Sehr kurze Schritte können0 ms messen. Keine CPU-Zyklusmessung, keine garantierte
Echtzeitlatenz und kein transaktionsweiter Schnappschuss. Ein älterer Firmwarestand
kann die Anfrage mit BAD_ARGS ablehnen. Die bisherige Host-Diagnosemethode fragt
weiter das alte Format ab; das eigene Tool kann `0D/01` ergänzen.

**Zyklus-/Verlustdiagnose (1.18-dev):** `0D` mit Nutzlast `02` liefert64 Bytes:
`schema=1, mode=2, count=15, flags:u8`, danach15 u32-LE-Werte ohne Reservebytes.
Die bisherigen Varianten leer/`01` behalten ihre Formate. Ältere Firmware darf
`02` mit BAD_ARGS ablehnen; das Tool kann diese reine Abfrage zum Erkennen nutzen.

| Index | Wert |
|---:|---|
| 0 | CPU-Takt aus SystemCoreClock in Hz, keine externe Frequenzkalibrierung |
| 1 / 2 | Gültig gemessene Hauptschleifen-Aufrufe / maximale Zyklen |
| 3 / 4 | Capture-DMA-Handler: gültige Aufrufe / maximale Zyklen |
| 5 / 6 | Write-DMA-Handler: gültige Aufrufe / maximale Zyklen |
| 7 / 8 | Gesamter Streaming-Service: gültige Aufrufe / maximale Zyklen |
| 9 / 10 | Kooperativer Analyseschritt einschließlich Erkennung/Rohvergleich: gültige Aufrufe / maximale Zyklen |
| 11 / 12 | Prüfleseschritt: gültige Aufrufe / maximale Zyklen |
| 13 / 14 | Erkannte RDATA-/INDEX-Overcapture-Episoden seit MCU-Start |

Flags: Bit0 Zyklusmessung verfügbar; Bit1 mindestens ein zu langer/mehrdeutiger
Zeitraum verworfen; Bit2 abweichender CPU-Takt beobachtet; Bit3 Zähler gesättigt;
Bit4 DWT nicht verfügbar/angehalten oder ohne Zählerfortschritt. Fehlerbits bleiben
bis MCU-Neustart gesetzt. Bit4 löscht zusätzlich Bit0; ohne Bit0 sind die Werte
kein gültiger Leistungsnachweis. Maxima gültiger früherer Messungen bleiben erhalten.

Die Messung verwendet DWT CYCCNT, ohne den Zähler im Betrieb zurückzusetzen.
Der Start prüft neben dem Freigabebit auch tatsächlichen Zählerfortschritt.
Bei550MHz wird ein beobachteter Abschnitt ab7808ms verworfen; eine volle
SysTick-Periode wird als Unsicherheit berücksichtigt. Der32-Bit-DWT-Umbruch
innerhalb einer ausreichend kurzen Messung ist zulässig. SysTick muss regulär
laufen; insbesondere ist dies kein Nachweis gegen mehrsekündig festhängende
Interrupts oder externe Debugger-Eingriffe. Die Messpunkte klammern den
Treiberaufruf ein; IRQ-Eintritt/-Austritt und der Verwaltungsaufwand der Messung
sind nicht vollständig enthalten. Keine per-Flanke-Interrupts werden hinzugefügt.

Vordergrundmessungen enthalten Unterbrechungen durch IRQs. Verschachtelte
Prüflese-/Analyseschritte überlappen; Maxima nicht addieren und daraus keine
CPU-Auslastung ableiten. Die Streaming-Arbeitsmenge je Serviceaufruf variiert.
Es handelt sich um Zyklen je Service-/Verarbeitungsschritt, keine feste Zyklenzahl
pro Sample. Aufrufzähler zählen gültige Messungen, nicht verworfene Abschnitte.
Die Momentaufnahme wird kurz unter erhaltener PRIMASK kopiert; Serialisierung
erfolgt danach. Es gibt keine USB-Ausgabe oder Heap-Allokation im Messpfad.

Overcapture wird am aktiven RDATA-Kanal (TIM2 CH1 oder Apple CH3) und am tatsächlich
verwendeten physischen INDEX-Kanal erkannt. Vor dem Datenfenster angesammelte
RDATA-Flags, der inaktive RDATA-Kanal sowie absichtlich ignorierte physische
INDEX-Pulse bei internem Index zählen nicht als Aufnahmeverlust. Synthetische
Aufnahmen überwachen physisches INDEX nur bei angeforderten RAW_INDEX-Belegen.
Erkannter Verlust stoppt den Eingabekanal/DMA und liefert Status6 statt Erfolg.
Je Aufnahme und Kategorie zählt höchstens eine Episode; viele verlorene Flanken
können hinter einem einzigen Hardwareflag stehen. Diese beiden Zähler sättigen.
Die alten DMA-Fehlerzähler zählen weiterhin DMA-Bus-/Transferfehler und ersetzen
die neuen Overcapture-Zähler nicht. Ein lang laufender Stream endet außerdem
explizit mit BUFFER_FULL vor dem Überlauf seines32-Bit-Samplecursors.

Das Ausbleiben eines Hardwareflags beweist weder intakte Eingangselektronik noch einen
verlustfreien Start am physischen Index. Prüfablauf und Grenzen:
[Hardwarequalifikation](Firmware_Hardwarequalifikation_2026-10-22.md).

**Kooperatives Firmware-Prüflesen:** Native WRITE-Aufträge bleiben bis zum Abschluss
im Zustand WRITE_VERIFYING. Aufnahme, präziser Vergleich, Rohprofil und Inhaltsprüfung
werden schrittweise bearbeitet; der Host wartet weiterhin auf denselben WRITE-END,
ohne weitere Bestätigung zu senden. Die internen Capture-Daten erzeugen keine
zusätzlichen READ-Antworten. Zwischen den Schritten werden USB, Schutzüberwachung
und Abbruch bedient. Greaseweazle-Befehle und die synchronen internen Aufrufer
behalten ihre Aufruf-/Antwortreihenfolge. Vorverarbeitung und erste mechanische
Anlauf-/Positionierungswartezeiten sind damit nicht vollständig asynchronisiert.

Bei `--extended --verify` dekodiert der Host vor WRITE CRC-gültige IBM-MFM-Sektoren
bei 250/300/500/1000 kbit/s aus der Quelle. Ohne solche Sektoren wird anschließend auf Amiga geprüft; ohne unterstützte
Referenz wird vor dem Schreiben abgebrochen. Nach WRITE liest er zwei Umdrehungen und vergleicht für
jede Referenzsektorkennung Datenmarke und sämtliche Datenbytes bei gültiger CRC.
Fehlende, veränderte oder widersprüchliche Sektoren führen zu einem Fehler.
Ein leicht verändertes Flux-Timing allein ist kein Inhaltsfehler.

`WriteResult.host_verified_sectors` nennt die verglichene Anzahl; das Host-Ergebnis
setzt `verify_level=3`. Der bisherige Python-Host verwendet dafür weiterhin
separates Host-Prüflesen ohne WRITE-VERIFY-Flag. Das ist unabhängig vom neuen
Firmware-Prüflesen mit INFO-Bit21 und WRITE-Bit0 (Abschnitt 5.2). Die Aussage umfasst nur die dekodierten Referenzsektoren, keine
magnetische Flux-Identität, absichtlich ungültigen CRCs, schwachen Bits oder
Kopierschutzstrukturen. GCR erhält hier noch keine automatische Prüfbehauptung.

Amiga-DD/HD wird zusätzlich mit vollständigen 11/22-Sektor-Referenzen unterstützt.
Verglichen werden Nutzdaten, 16 Labelbytes und Sektoren-bis-Gap bei gültiger
Header-/Datenprüfsumme. Alternative PLL-Profile sind auf drei Versuche je
Umdrehung begrenzt; widersprüchliche gültige Varianten scheitern immer.
`WriteResult.host_verified_format` und `readback_quality` geben Format und
Qualitätsbericht an. Quellspurlängen aus SCP/HFE ermöglichen das Zusammensetzen
indexgeteilter Sektoren, ohne das Wire-Protokoll zu ändern.
Details und Offline-CLI: [Amiga_Prueflesen.md](Amiga_Prueflesen.md).

<a id="49-firmware-detailanalyse-zwei-flag-bits-118-dev"></a>

### 5.2 Firmware-Detailanalyse: acht Flag-Bits (1.18-dev)

Die folgenden Erweiterungen laufen **auf dem STM32**, auf den im U11-/Fallback-
Puffer erfassten Fluxdaten. Das Tool setzt die Flags und erhält vorbereitete
Messwerte und Decoderbefunde. Es braucht dafür keinen eigenen Decoder.
Die Bewertung von Medienqualität oder Kopierschutz bleibt beim Tool.
`INFO.caps` **Bit 21** kennzeichnet Detailanalyse und Firmware-Prüflesen im
erweiterten Schreibpfad. Ältere Firmware ohne dieses Bit unterstützt diese
Erweiterungen nicht. Entwicklungsstand: offline geprüft, Hardwarequalifikation offen.

#### ANALYZE 0x25: Request und Flags

Bestehende Requests mit 2 oder 3 Bytes bleiben gültig und liefern unverändert
40 Antwortbytes. Der neue vierte Request-Byte enthält die Analyseflags:

| Offset | Typ | Bedeutung |
|---:|---|---|
| 0 | u8 | Zylinder/Spur, wie bisher (ganze Spur, auch im Viertelspurmodus) |
| 1 | u8 | Bit 0 Seite. Ab 1.18-dev zusätzlich Bit 7 = Viertelspurmodus (Apple-Laufwerke 0–3; Shugart nur Halbspur 2 bei Doppelschritt-Profil, sonst BAD_ARGS) mit dem Viertel 0–3 in Bits 4–5: `side | 0x80 | quarter << 4`. Andere Bits oder Werte > 1 ohne Bit 7 → BAD_ARGS. Gilt ebenso für die ANALYZE-Nutzlast im Analysejob (Op 1, Byte 25) |
| 2 | u8 | Umdrehungen: 0 = 3, maximal 8 |
| 3 | u8 | Analyseflags, siehe unten |
| 4 | u8, optional | Formathinweis: 0 automatisch, 1 IBM-MFM, 2 Amiga-MFM, 3 IBM-FM, 4 HP-MMFM, 5 Commodore-GCR, 6 Macintosh-GCR (INFO Bit29, 5.4), 7 Apple-II-5,25-Zoll-GCR (5.7, nur ausdrücklich) |
| 5 | u32 LE, optional | Datenrate in bit/s: 0 automatisch; 125000/150000/250000/300000/500000/1000000 |

Gültige Requestlängen: **2, 3, 4, 9, 33 oder 37 Bytes**. Die beiden optionalen
Hinweisfelder müssen gemeinsam vorhanden sein und setzen Detailanalyse voraus.
Sie sind keine weiteren Flags. Ein Formathinweis beschreibt den gewünschten
Decoder und ist kein Beweis, dass das Medium dieses Format tatsächlich enthält.

| Bit | Maske | Name | Firmwarearbeit |
|---:|---:|---|---|
| 0 | `0x01` | DETAIL | 125-ns-Intervallhistogramm, Werte je Umdrehung, Sektorkennungen und Prüfsummenbefunde |
| 1 | `0x02` | ADAPTIVE | Zusätzlich zwei alternative PLL-Profile auf denselben Rohdaten; benötigt DETAIL |
| 2 | `0x04` | TIMING | Anzahl, Minimum/Maximum, Summe und Quadratsumme der Fluxintervalle je Umdrehung |
| 3 | `0x08` | GAPS | Positionen langer Intervalle und stiller Endabschnitte |
| 4 | `0x10` | GLITCHES | Positionen kurzer Intervalle; keine Filterung |
| 5 | `0x20` | POSITIONS | Decoder-Feldgrenzen und CHRN je Sektorbeobachtung im Standardprofil |
| 6 | `0x40` | SYNC | Konfigurierbares Rohzell-Bitmuster suchen, unabhängig vom Sektordecoder |
| 7 | `0x80` | WINDOWS | Kurze originale Intervallfolgen bei Auffälligkeiten als Werkzeug für das Tool |

Damit gelten **`0x00` = bisherige Analyse**, **`0x01` = Details** und
**`0x03` = Details plus adaptive Dekodierung**, **`0xFF` = alle acht Basisfunktionen**.
Jedes gesetzte Bit 1–7 benötigt DETAIL; eine nichtleere Maske ohne Bit0 ergibt BAD_ARGS.
Die neuen Bits 2–7 benötigen `INFO.caps` Bit22. Es sind keine Formatflags pro Diskettentyp erforderlich.
Die Flags lösen weder Schreiben noch zusätzliche mechanische Lesewiederholungen aus.

Beispiele, nur Request-Payload (übliche v2-Rahmung und CRC außen herum):

```text
00 00 03 00                 Spur 0, Seite 0, drei Umdrehungen, bisherige Analyse
00 00 03 01                 dieselbe Aufnahme mit Details
00 00 03 03                 zusätzlich alternative PLL-Profile
00 00 03 FF                 alle acht Basisfunktionen mit Standardparametern
00 00 03 95                 Timing, kurze Intervalle und Rohfenster
00 00 03 03 02 90 D0 03 00  adaptiv, Amiga, 250000 bit/s
00 00 03 03 01 20 A1 07 00  adaptiv, IBM-MFM, 500000 bit/s
```

IBM-MFM umfasst unter anderem PC, Atari ST und Commodore 1581. Der Firmware-
Sektordecoder unterstützt dabei 128/256/512/1024 Nutzdatenbytes. Amiga unterstützt
DD/HD, 512 Nutzdatenbytes und 16 Labelbytes. IBM-FM unterstützt ebenfalls
128/256/512/1024 Bytes. HP-MMFM unterstützt 256 Bytes, CHR = Zylinder, Kopf,
originale Sektornummer 0–29 und N=1. GCR/andere Kodierungen erhalten
weiterhin formatunabhängige Flux-Messwerte, aber noch keine vollständige
Sektordekodierung oder Inhaltsbestätigung durch diese Funktion.

#### Antwort: bisherige 40 Bytes plus Qualitätsblock

Mit DETAIL folgen auf die unveränderten 40 ANALYZE-Bytes **1824 Bytes
Qualitätsblock**, insgesamt **1864 Payloadbytes**, solange Bits 2–7 und die
erweiterten Diagnoseflags null sind.
Mit mindestens einem neuen Bit folgt zusätzlich der unten beschriebene
**1920-Byte-QX-Block**: insgesamt **3784 ANALYZE-Payloadbytes**.
Die neuen Diagnoseflags erzwingen QX und hängen zusätzlich **304 Bytes QD** an:
**4088 ANALYZE-Payloadbytes**, weiterhin unter dem 4096-Byte-Limit. Alle mehrbyteigen Werte sind
Little Endian. Die folgenden Offsets beziehen sich auf den Qualitätsblock,
also auf Antwortoffset 40. Reservierte Bytes werden mit 0 übertragen.

| Offset | Größe | Inhalt |
|---:|---:|---|
| 0 | 1 | Schema = 1 |
| 1 | 1 | Ausgewählter Decoder: 0 unbekannt/keine Erkennung, 1 IBM-MFM, 2 Amiga-MFM, 3 IBM-FM, 4 HP-MMFM, 5 Commodore-GCR |
| 2 | 1 | Tatsächlich erfasste Umdrehungen, bis 8 |
| 3 | 1 | Belegte Sektoreinträge, bis 64 |
| 4 | 1 | Analysestatus: 0 erfolgreich beendet, sonst v2-Fehlernummer |
| 5 | 1 | Begonnene PLL-Profile: 1 oder bis zu 3; bei Abbruch nicht als vollständig behandeln |
| 6 | 1 | Histogramm-Bins = 128 |
| 7 | 1 | Binbreite = 125 ns |
| 8 | 4 | Ausgewählte Datenrate in bit/s, 0 ohne Erkennung |
| 12 | 4 | Nicht gespeicherte Sektorbeobachtungen wegen voller 64er-Tabelle |
| 16 | 1 | Gleichstand der besten Erkennungskandidaten: 1 = mehrdeutig |
| 17 | 1 | Zylinder/Spur der Aufnahme |
| 18 | 1 | Seite der Aufnahme |
| 19 | 1 | Positionsbyte der Aufnahme (1.18): 0 ganze Spur, `0x80 | Viertel << 4` Apple-Viertelspur; Spur in Offset 17 bleibt die ganze Spur |
| 20 | 16 | Acht u16-Zähler gültiger Kandidatensektoren: IBM 250/300/500/1000, dann Amiga 250/300/500/1000 kbit/s; maximal zwei Umdrehungen, gesättigt bei 65535 |
| 36 | 4 | Flüchtige Berichtsgeneration, erhöht je Analyse, Wrap erlaubt |
| 40 | 4 | Zeitstempeltakt = 275000000 Hz |
| 44 | 4 | Dauer der Detailauswertung in ms, ohne Aufnahmezeit |
| 48 | 12 | Sechs weitere Kandidatenzähler u16: FM 125/150/250/300/500 kbit/s, HP-MMFM 500 kbit/s (INFO Bit23) |
| 60 | 2 | Toleranzflags (Op 34) dieses Decodierlaufs, u16; 0 = strikt |
| 62 | 2 | Apple-II-Flags (Op 33) dieses Decodierlaufs, u16 |
| 64 | 768 | 64 Sektoreinträge zu je 12 Bytes |
| 832 | 384 | 8 Umdrehungen × 3 Profile × 16 Bytes |
| 1216 | 512 | 128 Histogrammzähler u32; letztes Bin enthält auch längere Intervalle |
| 1728 | 96 | 8 Umdrehungen × 12 Bytes: Indexdauer, Flux-Anzahl, größte beobachtete Lücke, jeweils u32 |

Sektoreintrag, relativ zu `64 + 12*i`:

| Offset | Größe | Inhalt |
|---:|---:|---|
| 0 | 4 | C/H/R/N-Kennung; Amiga wird als C=Zylinder, H=Seite, R=Sektor+1, N=2 dargestellt |
| 4 | 4 | CRC32 der ersten gültigen Variante: IBM Nutzdaten; Amiga Nutzdaten + 16 Labelbytes + Sektoren-bis-Gap |
| 8 | 1 | Umdrehungsmaske gültiger Beobachtungen |
| 9 | 1 | Davon im Standard-PLL-Profil gefunden |
| 10 | 1 | Umdrehungsmaske beobachteter Datenprüfsummenfehler |
| 11 | 1 | Sticky-Befunde: Bit0 abweichende CRC32-Variante, Bit1 gelöschte IBM-Datenmarke, Bit2 HP-Defektspurmarke, Bit3 Datenfeld ohne gültigen Kopf angenommen (Kennung 255/255), Bit4 Kodierungsfehler im Datenfeld, Bit5 Kopf-CRC/-Prüfsumme falsch, Bit6 Daten-CRC/-Prüfsumme falsch, Bit7 Epilog/Nachspann weicht ab (GCR). Bits 3 und 5–7 setzen alle Decoder (5.8); ein Sektor in `seen` mit einem dieser Bits wurde nur durch Toleranzflags angenommen |

Beobachtungsmasken: Bit0 ist Umdrehung 0. Sektoren über einer Indexgrenze werden
der Umdrehung ihrer **Dekodierfertigstellung** zugeordnet. CRC32 ist eine kompakte
Inhaltskennzeichnung, keine kryptografische Garantie. Bei einem Konflikt bleibt
die Kennzeichnung der ersten Variante erhalten; keine Mehrheitsentscheidung.
Das Tool kann den erwarteten Sektorsatz mit den gemeldeten Kennungen vergleichen.

Versuchseintrag bei `832 + 16*(3*umdrehung + profil)`:
`u64 Sektormaske` (Bit i verweist auf Sektoreintrag i), `u32 Headerprüfsummenfehler`,
`u32 Datenprüfsummenfehler`. Profile: 0 Standard (Initialperiode 1000 ‰,
Frequenzteiler 16, Phasenteiler 2), 1 schneller (960 ‰, 8, 2),
2 gedämpft (1040 ‰, 32, 4). Nicht ausgeführte Einträge bleiben 0.
Ohne Formaterkennung wird nur das Histogrammprofil ausgeführt.

Eine Lücke kann über die Indexgrenze reichen. Die letzte stille Reststrecke der
Aufnahme fließt in die größte Lücke ein; das Histogramm zählt Intervalle bis zu
erfassten Flanken, nicht eine künstliche Abschlussflanke. Die Rohdaten werden
nicht geändert. Ein fehlender Decoderbefund ist kein Beweis für eine defekte Spur.
Bei Apple ohne Indexsensor verwendet die Aufnahme Zeitabschnitte; bei aktivierter
Indexsimulation können Indexabstände ebenfalls künstlich sein. Das Tool muss die
gewählte Laufwerks-/Indexkonfiguration berücksichtigen, bevor es daraus eine
physische Drehzahl oder Drehzahlschwankung ableitet.

#### Erweiterte Parameter und QX-Ergebnisblock (INFO Bit22)

Alle neuen Funktionen arbeiten auf der vorhandenen Aufnahme. Rohflux, PLL-Eingang
und Schreibfunktionen werden dadurch nicht verändert. Keine Heap-Allokation,
keine zusätzliche vollständige Spurkopie. Alte Requests und Antworten bleiben
unverändert, wenn Bits 2–7 nicht gesetzt sind. Greaseweazle ist davon unberührt.

Optionaler **33-Byte-Request**: Bytes 0–8 wie oben, danach exakt diese Felder.
Alle Felder sind explizit anzugeben; Null bedeutet nur bei `cell_ticks` automatisch.
Bei 4-/9-Byte-Requests gelten die aufgeführten Standardwerte.

| Offset | Typ | Bedeutung / Standard / Grenze |
|---:|---|---|
| 9 | u8 | Parameterversion 1 |
| 10 | u8 | Gemeinsames Fundstellenlimit, Standard 48, zulässig 1–48 |
| 11 | u8 | Rohfensterlänge in Intervallen, Standard 4, zulässig 1–4 |
| 12 | u8 | Musterlänge, Standard 16, zulässig 1–32 |
| 13 | u32 | `glitch_ticks`: kurze Intervalle strikt kleiner; Standard 220, mindestens 1 |
| 17 | u32 | `gap_ticks`: lange Intervalle größer/gleich; Standard 4400, größer als glitch_ticks |
| 21 | u32 | `cell_ticks`: Rohzellperiode für SYNC, Standard 0 = ausgewählte Datenrate, sonst 100–100000 |
| 25 | u32 | Bitmuster, Standard `0x4489`; Bits oberhalb der Musterlänge müssen 0 sein |
| 29 | u32 | Detail-Zeitbudget einschließlich Formaterkennung in ms; Standard 10000, zulässig 1–10000 |

Zeitwerte sind TIM2-Ticks bei 275 MHz. Automatische Rohzellperiode ist
`275000000 / (2 * erkannte_Datenrate)`, bei Format 5 dagegen `275000000 / erkannte_Datenrate`, ohne Erkennung 550 Ticks. Für andere
Kodierungen die passende Rohzellperiode explizit setzen. Musterbits werden
MSB zuerst auf der Fluxfolge gesucht: jede Flanke beendet eine Folge von
Nullzellen mit einer Eins; Intervall/Zellperiode wird auf die nächste ganze
Zellenzahl gerundet. Dies ist eine zusätzliche feste Quantisierung, kein
vollständiger GCR-/FM-Decoder und keine weitere adaptive PLL. Intervalle mit
0 oder mehr als 64 gerundeten Zellen unterbrechen die Mustersuche. Auch an
Indexgrenzen beginnt die Mustersuche neu; grenzüberschreitende Muster können
fehlen. Musterfundstellen sind Näherungen mit Zellauflösung.

QX beginnt bei Qualitätsblockoffset **1824**, also ANALYZE-Payloadoffset **1864**.
Alle folgenden Offsets sind relativ zum QX-Anfang. Mehrbytewerte sind LE,
nicht belegte Felder und reservierte Bytes sind null.

| Offset | Größe | Bedeutung |
|---:|---:|---|
| 0 | 2 | ASCII `QX` |
| 2 | 1 | Schema 1 |
| 3 | 1 | Angeforderte Analyseflags |
| 4 | 4 | Blockgröße 1920 |
| 8 | 1 | Gespeicherte Fundstellen, maximal 48 |
| 9 | 1 | Angefordertes Fundstellenlimit |
| 10 | 1 | Maske vollständig im Standardprofil ausgewerteter Umdrehungen |
| 11 | 1 | 1, wenn eine Quadratsumme auf UINT64_MAX gesättigt wurde |
| 12 | 4 | Wegen Ergebnislimit verworfene Fundstellen, gesättigt bei UINT32_MAX |
| 16 | 4 | glitch_ticks |
| 20 | 4 | gap_ticks |
| 24 | 4 | Effektiv verwendete Rohzellperiode |
| 28 | 4 | Bitmuster |
| 32 | 4 | Detail-Zeitbudget in ms |
| 36 | 1 | Musterlänge |
| 37 | 1 | Maximale Rohfensterlänge |
| 38 | 1 | Endgrund: 0 fertig, sonst v2-Fehlernummer (z.B. 5 Timeout, 20 Abbruch) |
| 39 | 25 | Reserviert |
| 64 | 320 | Acht Timing-/Abdeckungseinträge zu 40 Bytes |
| 384 | 1536 | 48 Fundstellen zu 32 Bytes |

Timing-Eintrag bei `64 + 40*umdrehung`: `u32 Anzahl verarbeiteter Intervalle`,
`u32 Minimum`, `u32 Maximum`, `u64 Summe`, `u64 Quadratsumme`,
`u32 Anzahl kurzer Intervalle`, `u32 Anzahl langer Intervalle`,
`u32 Zeitstempel der letzten verarbeiteten Flanke`. Minimum, Maximum, Summe und
Quadratsumme benötigen TIMING; die übrigen Abdeckungswerte entstehen mit jedem
neuen Flag. Daraus berechnet das Tool Mittelwert und Populationsvarianz
`Quadratsumme / Anzahl - (Summe / Anzahl)^2`; bei Anzahl 0 oder gesättigter
Quadratsumme keine Varianz ausgeben. Das bestehende Histogramm liefert die
Intervallgruppen, die Einzelwerte je Umdrehung erlauben Vergleiche über die Zeit.

Fundstellen sind ein gemeinsamer, in Erkennungsreihenfolge im Standardprofil
gefüllter Pool (nicht zwingend nach geschätzter Startzeit sortiert).
Bei vielen Treffern können spätere Typen fehlen. Für gezielte Untersuchungen nur
die benötigten Flags setzen. Das volle Limit beendet die Analyse nicht:
Statistik und Abdeckung laufen weiter, verworfene Fundstellen werden gezählt.
Ein voller Pool ohne weitere Treffer ist keine Trunkierung; `dropped > 0` ist es.

Gemeinsame Fundstellenfelder: Byte0 Typ, Byte1 End-Umdrehung, Byte2 Zusatzwert,
Byte3 typabhängige Flags, u32 Start bei +4, u32 Ende bei +8; beide Zeitstempel
sind relativ zum jeweiligen Index. Bedeutung der restlichen Bytes:

| Typ | Inhalt |
|---:|---|
| 1 Gap | +12 u32 Intervall-/Abschnittslänge, +16 u32 Fluxindex; Byte3 Bit2: Intervall begann vor dem Index (Start auf 0 begrenzt), Bit3: stille Reststrecke ohne Abschlussflanke |
| 2 Glitch | Wie Gap, aber kurzes Intervall; Bit2 hat dieselbe Bedeutung |
| 3 Sektor | Byte2 Start-Umdrehung, Byte3 Bit0 Datenprüfsumme gültig; +12 vier Bytes C/H/R/N, +16 u32 Fluxindex des Endes |
| 4 Sync | Start = Ende = geschätzte Position des letzten Musterbits; Byte2 Musterlänge; +12 u32 Muster, +16 u32 Zellperiode, +20 u32 Fluxindex |
| 5 Rohfenster | Start = Ende = Zeitstempel der auslösenden Flanke; Byte2 tatsächliche Anzahl 1–4; Byte3 Ursache: 1 lang, 2 kurz, 3 Datenprüfsummenfehler, 4 Musterfund, 6 gültiger Inhaltskonflikt; +12 bis +27 bis zu vier originale u32-Intervalle, +28 u32 Fluxindex |

Sektorpositionen markieren die **Flanke, die den Feld-Sync vervollständigt**,
und die Flanke, die den Sektordecoder abschließt. IBM: Datenfeld-Sync (nicht der
vorangehende ID-Header); Amiga: Sync vor Header und Daten; FM/MMFM: Ende
der Datenadressmarke vor den Nutzdaten. Es sind begrenzte
Decoderpositionen, keine exakten Schreib-Splice-Grenzen. Grenzüberschreitende
Sektoren haben unterschiedliche Start-/End-Umdrehungen. IBM-MFM-/Amiga-/IBM-FM-/HP-MMFM-Befunde
im Standardprofil erzeugen diese Positionsdatensätze; adaptive Zusatzfunde
bleiben über die bisherigen Sektormasken zugänglich.

Rohfenster enthalten bis zu drei vorausgehende Intervalle und das aktuelle
Intervall, in zeitlicher Reihenfolge, ohne Veränderung oder Wiederholung.
Am Umdrehungsanfang kann das Fenster kürzer sein. Das erste Intervall kann noch
die stille Reststrecke der vorherigen Umdrehung enthalten. WINDOWS allein mit
DETAIL sammelt kurze/lange Intervalle und Decoderfehler/-konflikte; Musterfenster
benötigen zusätzlich SYNC. Fenster um Fehler zeigen das Decoderende, nicht
notwendigerweise die tatsächliche Fehlerstelle. Die kleine lokale Auswahl ist
kein Ersatz für einen vollständigen Rohflux-READ.

Die stille Reststrecke wird als Gap-Abschnitt ausgegeben, aber nicht als
künstliche Flanke in Statistik oder Rohfenster eingefügt. Über Indexgrenzen kann
der nächste volle Intervall-Gap denselben stillen Abschnitt erneut enthalten;
das Tool darf solche Einträge nicht ungeprüft aufsummieren. Die erste Strecke
nach Aufnahmebeginn ist möglicherweise nur ein Teilintervall. Grenzwerte sind
Messkonfiguration, keine automatische Diagnose „defekt“ oder „Kopierschutz“.

Teilberichte: v2-Status, QX-Endgrund, Umdrehungsmaske, letzte Flankenposition und
Verlustzähler gemeinsam auswerten. Ein Timeout bei späteren adaptiven Profilen
kann einen vollständigen Standarddurchlauf, aber unvollständige Decoderbefunde
hinterlassen. Für einen Aufnahmefehler vor Beginn der Detailanalyse gibt es
keinen neuen Qualitätsbericht.

#### FM/MMFM und erweitertes Diagnoseflag-Feld (INFO Bit23)

Die Greaseweazle-Kommandos und ihre Rahmenformate bleiben unverändert. Diese
Erweiterung gehört zum nativen UFI-Protokoll. Formatwahl und Diagnosearbeit sind
getrennt: `format=3/4` wählt den Decoder; Flags wählen die zusätzlichen Befunde.
`format=0` prüft nun auch FM und HP-MMFM. Die Kandidatenbewertung verwendet
CRC-gültige Sektoren ohne erkannte Kodierungsfehler und ohne HP-Defektmarke.
Das Erkennungsbudget bleibt begrenzt; Format-/Ratenhinweise sparen Suchdurchläufe.

IBM-FM: automatische Kandidaten 125/150/250/300/500 kbit/s. HP-MMFM: automatisch
500 kbit/s. Explizite gültige Ratenhinweise werden direkt verwendet, wenn auch
ein Format angegeben ist. Der Formathinweis ist weiterhin kein Erkennungsbeweis.

HP-MMFM arbeitet mit LSB-zuerst übertragenen Bytes; seine CRC wird über die
zeitliche Datenbitfolge ohne Adressmarke geprüft. Im Bericht beziehen sich die
CRC32-Werte auf **256 Bytes in physischer Reihenfolge auf der Diskette**.
Ein HPI-Abbild kann eine andere Wort-/Byteanordnung erwarten; diese Abbildung
gehört ins Tool. Die FM-CRC schließt die Adressmarke ein. Die Implementierung
wurde eigenständig anhand der [HP-Dokumentation und Referenzen](HP9895_Review.md)
entwickelt; ROM- oder Disassembly-Code wurde nicht übernommen.

Ein **37-Byte-ANALYZE-Request** erweitert den Parameterblock:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0–8 | wie bisher | Spur, Seite, Umdrehungen, Basisflags, Format, Datenrate |
| 9 | u8 | Parameterversion **2** |
| 10–32 | wie Version 1 | Fundstellenlimit, Fensterlänge, Muster, Schwellwerte, Zeitbudget |
| 33 | u16 LE | `diagnostic_flags`, mindestens ein bekanntes Bit |
| 35 | u16 LE | `margin_permille`, 1–499; empfohlener Ausgangswert 250 |

DETAIL (Basisbit0) bleibt Pflicht. Bits 2–7 dürfen null sein: Diagnoseflags
aktivieren den QX-Fundstellenpool selbst. Unbekannte Diagnosebits, falsche
Version, ungültige Grenzwerte oder abgeschnittene Requests ergeben BAD_ARGS.
Version 1 mit 33 Bytes bleibt gültig. Das neue Feld bietet Platz für spätere
Erweiterungen; Formatnamen werden nicht als einzelne Flags kodiert.

| Diagnosebit | Maske | Name | Arbeit |
|---:|---:|---|---|
| 0 | `0x0001` | CODING_ERRORS | Regeln der Clock-/Datenzellen nach Feld-Synchronisation prüfen; Verletzungen getrennt von CRC zählen und lokalisieren |
| 1 | `0x0002` | ADDRESS_MARKS | Erkannte Feld-/Adressmarken im Standardprofil als Fundstellen ausgeben |
| 2 | `0x0004` | PLL_MARGIN | Große digitale PLL-Restfehler in synchronisierten Feldern zählen und lokalisieren |
| 3 | `0x0008` | FAST_FORMAT_HINT | Optionaler, validierter Format-Suchhinweis; Einschränkungen unten |
| 4–15 | — | Reserviert | Müssen null sein |

`diagnostic_flags=0x0007` aktiviert alle drei Diagnosefunktionen.
Zusammen mit `Basisflags=0xFF` sind alle drei Diagnosefunktionen und die Basisanalysen
angefordert. Beispiel für HP-MMFM, Spur 0, Seite 0, drei Umdrehungen, 500 kbit/s:

```text
00 00 03 FF 04 20 A1 07 00 02 30 04 10 DC 00 00 00
30 11 00 00 00 00 00 00 89 44 00 00 10 27 00 00 07 00 FA 00
```

`FAST_FORMAT_HINT` ist standardmäßig aus und nur für automatische Format-/Ratensuche
wirksam. Ein früherer erfolgreicher Lauf mit mindestens zwei brauchbaren
Sektorkennungen liefert einen Suchhinweis, gebunden an Archiv-Medientoken,
Konfigurationsgeneration, Decoder-Toleranzen, Apple-Marken/Symboltabelle und
formatabhängige GCR-Zone. Beim nächsten passenden Auftrag wird dieser Decoder
zunächst auf den Erkennungsumdrehungen geprüft. Mindestens zwei CRC-gültige
Beobachtungen erlauben den verkürzten Suchlauf; andernfalls folgt die vollständige
Suche. Ein Medien-, Einstellungs- oder Zonenwechsel verhindert die Wiederverwendung.

QD-Byte8 enthält die angeforderte Maske; QD-Byte9 Bit3 ist nur gesetzt, wenn der
Hinweis tatsächlich genutzt wurde. Dann bleibt `ambiguous=1`, weil konkurrierende
Decoder nicht vollständig geprüft wurden. Die übrigen Diagnosebits behalten ihre
Bedeutung. Explizite Format-/Ratenvorgaben, aktive Kopierschutzoptionen (Op55),
CONFIRM_CONTENT (Retry-Policy1) und angeforderte Roh-Umdrehungsvergleiche nutzen
keinen verkürzten Suchlauf. Bei Policy1 bleibt das angeforderte Feature sichtbar,
die Erkennung prüft jedoch alle passenden Kandidaten; echte Mehrdeutigkeit bleibt
ein Hindernis für die Bestätigung. Für eine
vollständige Formaterkennung weiterhin0x0007 beziehungsweise kein Bit3 verwenden.

Die feste SYNC-Mustersuche ist unabhängig vom Decoder. Nützliche Suchmuster
für eigene Requests: FM-ID `0xAAAAF57E` (32 Bits, bei 250 kbit/s 550 Ticks je
Rohzelle), HP-ID `0x55552A54`, HP-Daten `0x55552A44` (je 32 Bits, bei
500 kbit/s 275 Ticks). Diese Muster schließen einen Teil der vorherigen
Synchronisation ein; sie sind Hinweise, kein gültiger Sektor ohne CRC-Prüfung.

**QD-Block:** beginnt bei Qualitätsblockoffset **3744**, beziehungsweise
ANALYZE-Payloadoffset **3784**. Alle Werte LE; feste Größe 304 Bytes.

| Offset | Größe | Bedeutung |
|---:|---:|---|
| 0 | 2 | ASCII `QD` |
| 2 | 1 | Schema 1 |
| 3 | 1 | Decoderformat 0–4 |
| 4 | 4 | Blockgröße 304 |
| 8 | 1 | Angeforderte Diagnosebits 0–2 |
| 9 | 1 | Anwendbare Diagnosebits; 0 bei unbekanntem Decoder |
| 10 | 2 | margin_permille |
| 12 | 1 | Vollständig bearbeitete Umdrehungen im Standardprofil, Bitmaske |
| 13 | 1 | Endgrund: 0 fertig, sonst v2-Fehlernummer |
| 14 | 1 | Vollständig bearbeitete Umdrehungen im schnellen PLL-Profil |
| 15 | 1 | Vollständig bearbeitete Umdrehungen im gedämpften PLL-Profil |
| 16 | 288 | 8 Umdrehungen × 3 Profile × 12 Bytes |

Eintrag bei `16 + 12*(3*umdrehung + profil)`:
`u32 Kodierungsverletzungen`, `u32 erkannte Adress-/Feldmarken`,
`u32 Intervalle mit PLL-Restfehler über der Schwelle`. Auch bei Abbruch sind
bereits gezählte Befunde vorhanden; Vollständigkeit aus den Masken ableiten.
FM/MMFM validiert Clockregeln ohnehin für sichere Headerzuordnung; entsprechende
Zähler können auch ohne CODING_ERRORS entstehen. MARKS zählt Decoderereignisse
auch ohne deren Fundstellenausgabe. PLL_MARGIN ist ohne sein Flag abgeschaltet.

Neue QX-Fundstellentypen:

| Typ | Bedeutung |
|---:|---|
| 6 | Kodierungsverletzung(en) an dieser Flanke |
| 7 | Erkannte Adress-/Feldmarke(n) |
| 8 | PLL-Restfehler über der Schwelle |

Für alle drei: Start=Ende=Flankenzeitstempel, Byte2 Decoderformat, Byte3 zuletzt
erkannte Marke, +12 u32 neue Ereignisse dieser Kategorie an der Flanke,
+16 u32 Fluxindex. Typ6/8 können mehrere interne Decoderzellen zusammenfassen.
Mit WINDOWS entstehen dazu Rohfenster mit Ursache **7** (Kodierung) oder **9**
(PLL-Restfehler). Fundstellen und Fenster nutzen weiterhin denselben 48er-Pool;
Zähler und Abschlussmasken bleiben auch bei vollem Pool erhalten.

Marken: IBM `FE` ID, `FB` Daten, `F8` gelöschte Daten; FM zusätzlich `FC` Index,
wenn nach Null-Sync erkannt. Amiga `A1` bezeichnet den gefundenen Sync-Verbund.
HP `70` ID, `F0` Defektspur-ID, `50` Daten, `D0` ECC-Daten. **ECC-Dateninhalte
werden nicht dekodiert**: D0 wird als Marke gemeldet, aber kein erfolgreicher
Sektor daraus erzeugt. Ein neuer ID-Header ersetzt den vorherigen; ungültige
Header oder zu große ID-/Datenabstände liefern keine falsche Sektorzuordnung.

Kodierungsfehler bedeuten eine verletzte Clockregel innerhalb eines bereits
synchronisierten Feldes. Legale Sondermarken sind ausgenommen. CRC-Zähler
bleiben reine Prüfsummenbefunde. Ein CRC-gültiger Sektor kann daher eine gesetzte
Kodierungsfehler-Kennzeichnung haben; ein gültiger CRC allein ist kein Nachweis
sauberer Kodierung. Header mit solchen Fehlern autorisieren keinen Folgesektor.

PLL_MARGIN zählt, wenn `abs(Intervall*256 + Phase - Zellenzahl*PLL-Periode)`
größer als `PLL-Periode * margin_permille / 1000` ist. Gezählt wird nur im
synchronisierten Decoderfeld und bei 1–8 quantisierten Rohzellen. Das ist ein
**digitaler Timingindikator**, keine Messung analoger Signalreserve und keine
Nachbildung des analogen HP-Margin-Detektors. Ergebnisse hängen vom PLL-Profil ab.

**Firmware-Prüflesen:** Der native erweiterte WRITE-Pfad (`0x21`/`0x61`) erkennt
nun auch FM-/HP-MMFM-Quellen. Kein weiterer WRITE-Flag ist nötig. Kodierungsfehler
oder HP-Defektmarken in CRC-gültigen Quellsektoren verhindern die Freigabe; beim
Rücklesen zählen solche Beobachtungen nicht als gültiger Inhaltsnachweis.
FM-Markierung „gelöscht“ wird zusätzlich zum Dateninhalt verglichen.
Prüfgrad 3 bestätigt alle erfassten Referenzsektoren, nicht die Vollständigkeit
eines unbekannten Diskettenformats. Undekodierbare Quelle bleibt NOT_IMPL.
Es gibt hier keinen neuen FM/MMFM-Sektorencoder oder HP-IB-Bustreiber:
Geschrieben wird die vom Tool gelieferte Rohfluxspur. Der klassische gepackte
WRITE-Pfad behält seine bisherige IBM-MFM-/Amiga-Erkennung.

#### QUALITY 0x0E: letzten Firmwarebericht abholen

Request ohne Payload; Antwort ist der zuletzt erzeugte Qualitätsbericht:
1824 Bytes ohne neue Flags, **3744 Bytes** mit QX-Block, **4048 Bytes** mit
QX und QD (erweiterte Diagnoseflags). Ohne Bericht
antwortet die Firmware BUSY. Ein neuer Detail-ANALYZE-Auftrag verwirft den alten
Bericht, bevor die Aufnahme startet. Ein fehlgeschlagener Aufnahmeversuch kann
daher keinen alten Erfolg liefern. Abbrüche während der Auswertung liefern,
soweit vorhanden, einen Teilbericht mit Fehlerstatus. Generation und Spur/Seite
dienen dem Tool zur Zuordnung. Der Bericht ist flüchtig, nicht persistent.

Beim bisherigen ANALYZE 0x25 sind Aufnahme und Auswertung begrenzte, synchrone Firmwareoperationen:
Aufnahme bis 4 s, Detailauswertung bis 10 s. Tool-Timeout für Detail-ANALYZE
mindestens 16 s wählen. Watchdog und Abbruch werden während der Arbeit geprüft.
Puffermangel ist ein Fehler, kein erfolgreicher Vollbericht. Mit kleinem internem
Fallback kann bereits eine Mehrfachaufnahme die Kapazität überschreiten.

#### WRITE 0x22: Firmware-Prüflesen auch mit erweitertem Writer

Das bestehende **WRITE-Bit0 (`0x01`, VERIFY)** bleibt bestehen; es ist unabhängig
von den ANALYZE-Flags. Mit **Bit5 (`0x20`, Extended)** ergibt **`0x21`** einen
vollständigen Firmwareablauf: Quelle dekodieren, schreiben, zwei Umdrehungen
lesen und mit bis zu drei PLL-Profilen gegen die Referenz prüfen. Der Host muss
keinen separaten READ-/Dekodierlauf durchführen. Das normale WRITE-END meldet
Erfolg/Fehler und den Verifikationsgrad; QUALITY liefert die Decoderbefunde.
Für WRITE-END mindestens 30 s beziehungsweise Quelldauer plus 25 s vorsehen,
je nachdem welcher Wert größer ist. Quellprüfung und Auswertung haben jeweils
ein eigenes 10-s-Limit; Timeout oder Abbruch ergeben keinen Prüferfolg.

Neues optionales **WRITE-Bit6 (`0x40`, SOURCE_INDEX)** hängt nach den bestehenden
optionalen Feldern ein `u32 index_ticks` an. Es verlangt den erweiterten Pfad
(Bit 5): mit VERIFY `0x61`, seit 1.18-dev auch ohne VERIFY `0x60` (WRITE_LIMITS
Bit 5 kündigt das an; davor BAD_ARGS, Op 39 Feld 8 Grund 1), jeweils zuzüglich
etwaiger Hard-Sector-Flags. Das Feld ist die Quell-Umdrehungsdauer in 275-MHz-Ticks,
nicht ein weiterer Schreibimpuls. Mit VERIFY erlaubt es das Dekodieren eines am
Index geteilten Quellsektors; ohne dieses Feld muss die Quelle alle zu prüfenden
Sektoren vollständig enthalten. Ohne VERIFY dient es allein der Drehzahlprüfung
(4.10); ohne eingestellte Toleranz wird es angenommen und ignoriert.

Unbekannte, widersprüchliche oder nicht prüfbare Quellen werden beim erweiterten
Prüfschreiben vor WGATE abgewiesen. Amiga verlangt 11/22 eindeutige Sektoren mit
passender Zielspur/-seite. IBM prüft die dekodierten Referenzsektoren, nicht eine
vermutete Standardgeometrie. Der Firmware-Inhaltsvergleich verwendet CRC32,
bei Amiga über Nutzdaten plus Labels. Eine gültige abweichende Beobachtung führt
zum Fehler, auch wenn eine andere Umdrehung passt. Abbruch, Capture-Overflow
oder fehlende Referenzsektoren ergeben keinen Inhaltserfolg.

Das klassische Prüflesen unbekannter Formate kann weiterhin Grad 2
(`readback_plausible`) liefern. Das ist ausdrücklich **keine Inhaltsprüfung**.
GCR, absichtlich ungültige Prüfsummen und Kopierschutz werden nicht pauschal als
inhaltlich verifiziert bezeichnet. Diagnose-Flags ändern weder Flux-Takt noch
das Greaseweazle-Protokoll; sie gehören ausschließlich zum nativen v2-Protokoll.

### 5.3 GCR-Prüflesen, Umdrehungsvergleich und Wiederholungsstrategie (INFO Bit26)

Implementiert ab 2026-10-15. Diese Erweiterung belegt **keine weiteren Bits der
Job-, WRITE- oder ANALYZE-Flags**. Neue Reportauswahl und Retry-Policy haben
eigene Felder. Greaseweazle-Befehle bleiben unverändert. Alle folgenden Ops
gehören zum nativen Kommando `0x0F`.

#### Commodore-GCR-Prüflesen

Erweiterter nativer WRITE mit Prüflesen (WRITE Bits 5 und 0, zusammen `0x21`)
erkennt zusätzlich Commodore-GCR in den vier Kanalbitraten 250000, 266667,
285714 und 307692 bit/s. Der Inhaltsvergleich umfasst 256 Datenbytes und die
beiden Disk-ID-Bytes in Headerreihenfolge. Beide GCR-XOR-Prüfsummen müssen gültig
sein; veränderte gültige Inhalte oder Disk-IDs ergeben einen Fehler. Erfolgreicher
Vergleich aller Referenzsektoren liefert wie bisher `verify_level=3`.

Kein neues GCR-Flag nötig. Die Discovery meldet Format 5 nun mit
`extended_write_verify=1`. Klassischer WRITE erhält dadurch keine GCR-Garantie.
GCR-Header-Spurnummern sind 1-basiert; mechanische Spurposition und Sektor-C/H/N
bleiben getrennt. EXPECT_SET (WRITE Bit7) kann die vollständige erwartete Menge
vor WGATE verlangen: beispielsweise Spurkennung C=18, H=0, N=1, R=0–18.
Ohne EXPECT_SET bestätigt Level 3 nur die im Quellmaterial gefundenen Sektoren.
Für über den Spurübergang verteilte Referenzsektoren die bestehende Quell-
Indexdauer (WRITE Bit6) übergeben. Keine Garantie für Kopierschutzreproduktion.

#### Op 12: Fähigkeiten, 32 Bytes

Request `[12]`. Antwort: Schema 1 bei Byte 0, unterstützte Berichtsmaske u8=1
bei 1, Policy-Bitmaske u8=31 bei 2 (Policies0–4), maximale Versuchszahl u8=8
bei 3. Toleranzminimum u32=1 bei 4, Maximum u32=10000 bei 8,
Vergleichsberichtgröße u32=1568 bei 12, Fensterzahl u8=128 bei 16.
Übrige Bytes sind 0.

#### Op 11: Konfiguration und Defaults

Request `[11]` liest. Setzen verwendet exakt 12 Bytes:

| Requestoffset | Typ | Inhalt / Default |
|---:|---|---|
| 0 | u8 | Op 11 |
| 1 | u8 | Schema 1 |
| 2 | u16 | Berichtsmaske, Default 0; nur REV_COMPARE=0x0001 erlaubt |
| 4 | u8 | Retry-Policy0–4, Default0; Policy3/4 siehe4.20 |
| 5 | u8 | Benötigte aufeinanderfolgende Inhaltsbestätigungen 2–8, Default 2 |
| 6 | u8 | Stagnationslimit 1–7, Default 2 |
| 7 | u8 | Reserviert, muss 0 sein |
| 8 | u32 | Vergleichstoleranz 1–10000 in nominalen Flux-Ticks, Default 110 |

Antwort: 11 Bytes entsprechend Requestbytes 1–11, ohne Op. Auch bei inaktiver
Policy müssen alle Werte gültig sein. Setzen während aktiver Operationen oder
offenem Integritätsjob ergibt BUSY; ungültige Werte BAD_ARGS ohne Teiländerung.
Konfiguration gilt für folgende **Analysejobs** und wird beim Start übernommen;
Abschnitt 8 enthält deren Snapshot. Kein Einfluss auf synchrones ANALYZE,
Standard-READ oder WRITE. Keine automatische SD-Persistenz.

Policy 0 behält die bisherigen RETRY_MISSING-/RETRY_ERRORS-Entscheidungen.

Policy 1 (`CONFIRM_CONTENT`) liest erneut, bis dieselbe nichtleere Sektormenge
mit identischen C/H/R/N-Kennungen und Inhalts-CRC32 in der eingestellten Zahl
aufeinanderfolgender Versuche vorliegt. Reihenfolge der Sektorfunde ist egal.
Decoderfehler, Konflikte, Defekt-/Kodierungsbefunde oder fehlende EXPECT_SET-
Sektoren setzen die Bestätigung zurück. Ein gültiger, aber geänderter Inhalt
beginnt eine neue Folge mit 1. Start verlangt `max_attempts >= confirmations`.
Beim Versuchslimit ohne Bestätigung: Status 6 und Abschlussgrund 3. Diese
Policy benötigt keine RETRY-Flags. Ohne EXPECT_SET kann eine stabile Teilmenge
bestätigt werden; für Vollständigkeit EXPECT_SET verwenden. Verglichen werden
CRC32-basierte Inhaltsbefunde, keine kryptographischen Beweise.

Policy 2 (`STOP_ON_STAGNATION`) benötigt RETRY_MISSING oder RETRY_ERRORS.
Bei bestehendem Wiederholungsbedarf zählt ein Versuch als Verbesserung, wenn
mehr konfliktfreie gültige Sektoren gefunden werden oder bei gleicher Anzahl
weniger Header-, Daten-, Konflikt- und Kodierungsfehler vorliegen. Die erste
Aufnahme setzt den Ausgangswert. Nach der konfigurierten Zahl weiterer Versuche
ohne Verbesserung endet der Job mit Abschlussgrund 2. Keine Vermischung von
Sektorfunden verschiedener Versuche; die bisherige EXPECT_SET-Endprüfung gilt.
Bei RETRY_ERRORS ohne EXPECT_SET kann das Ergebnis weiterhin Status 0 mit
verbleibenden Befunden haben. Immer Bericht und Abschlussgrund auswerten.

Maximalzahl und Gesamtzeitbudget aus dem Startrequest gelten weiterhin; Abbruch,
Aufnahmefehler und Timeout haben Vorrang. Das Analysezeitbudget umfasst auch
den optionalen Vergleich. Der Status enthält Abschlussgrund bei Byte 53,
Bestätigungszahl bei 54 und Stagnationszahl bei 55. Grund 0 bleibt der allgemeine
Normal-/Fehlerfall; Byte 2 und Zustand bleiben maßgeblich.

#### REV_COMPARE-Bericht, Jobabschnitt 7

Der Firmwarevergleich arbeitet ohne Decoderabhängigkeit mit den gespeicherten
Flankentimestamps. Jede vollständige Umdrehung 1–7 wird gegen Umdrehung 0
verglichen. Zeitpositionen werden linear auf deren Indexdauer normiert; dies
kompensiert unterschiedliche mittlere Drehzahlen. Zwei Flanken innerhalb der
eingestellten Toleranz werden monoton eins-zu-eins zugeordnet. Nicht zugeordnete
Referenzflanken zählen als fehlend, zusätzliche Flanken als extra. Die Arbeit
erfolgt kooperativ in Schritten mit höchstens 256 Zuordnungsentscheidungen.

Bericht: 32-Byte-Header plus 128 × 12 Bytes. Paging und CRC32 wie Abschnitt 6.1.

| Headeroffset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Fensterzahl 128 |
| 2 | u8 | Abgeschlossene Vergleiche: Bit r für Umdrehung r, Bit0 immer 0 |
| 4 | u32 | Qualitätsberichtgeneration |
| 8 | u32 | Zeittoleranz in Referenzticks |
| 12 | u32 | Indexdauer der Referenzumdrehung |
| 16, 20 | je u32 | Kleinste/größte Indexdauer über Referenz und verglichene Umdrehungen |

Übrige Headerbytes 0. Fenstereintrag: `missing u32, extra u32,
revolution_mask u8, reserved[3]=0`. Fenster k entspricht dem normierten
Spuranteil `[k/128, (k+1)/128)`; eine Flanke genau am Ende wird Fenster 127
zugeordnet. Zähler summieren die Vergleiche und sättigen bei UINT32_MAX.
Die Maske zeigt, in welchen Umdrehungen Unterschiede auftraten. Auch komplett
leere Umdrehungen werden verglichen. Bei nur einer Umdrehung bleibt die
Vergleichsmaske 0: das ist **kein Nachweis von Stabilität**.

Phase 3 im Jobstatus meldet den Vergleich; Revolutionsfeld 42 und Flankenzähler
44 bezeichnen die gerade verglichene Umdrehung und deren bisher bearbeitete
Flanken. Ein abgebrochener oder nicht angeforderter Vergleich liefert beim
Abruf BUSY; ein neuer Qualitätsbericht invalidiert alte Vergleichsseiten.

Die Auswertung liefert lokale Abweichungsbefunde, keinen Weak-Bit-Beweis.
Indexjitter, verschobener Spuranfang, Spindelgeschwindigkeit innerhalb einer
Umdrehung und die gewählte Toleranz beeinflussen das Ergebnis. Es gibt keine
automatische Phasenverschiebung oder zyklische Ausrichtung. Bei synthetischen
Indexperioden ist deren Bezug zur tatsächlichen Diskettenumdrehung zu beachten.
Keine amplitudenbezogene Aussage aus digitalen RDDATA-Flanken ableiten.

Beispiel: Op 11 mit Reportmaske 1, Policy 1, zwei Bestätigungen und Toleranz 110
setzen; Analysejob mit mindestens zwei Versuchen und mehreren Umdrehungen
starten. Anschließend Jobstatus, Abschnitte 0/1, Vergleich 7 und Konfiguration 8
abrufen. RAW_INDEX und TRANSFER_TRACE bleiben unabhängig über Jobbits 3/4 wählbar.

### 5.4 Macintosh-GCR, Zonenprüfung und getrennte Abweichungsbefunde (INFO Bit29)

Implementiert am 2026-10-16. Format-ID **6 = Macintosh 400K/800K GCR**.
Sektoren sind nullbasiert, C=0–79, H=0/1 und N=2. Jeder Sektor enthält
12 Tag-Bytes und 512 Nutzbytes. Header-XOR, Datenprüfsumme, wiederholte Sektor-ID
und DE/AA-Abschluss werden geprüft. Inhalts-CRC32 der Qualitätsberichte umfasst
**525 Bytes: Tags, Nutzdaten, Header-Formatbyte**, in dieser Reihenfolge.
Extended native WRITE baut ebenfalls Macintosh-Inhaltsreferenzen auf und kann
sie beim Prüflesen vergleichen. Die normalen Schreibgrenzen bleiben bestehen.
Es wird weder ein Mac-Sektorencoder noch eine Lisa-/Twiggy-Emulation ergänzt.

ANALYZE/Analysejobs erlauben Format 6 mit Rate 500000 (native 2-µs-Kanalbitzeit)
oder den unten genannten fünf Raten für ein Laufwerk bei festen 300 U/min.
Rate 0 aktiviert die Suche. Bei automatischer Ratenwahl werden für Mac 500000
und die Rate der angeforderten logischen Spurzone versucht; ausdrücklich
angegebene Raten werden unabhängig von dieser Vorauswahl geprüft.

#### Op 19 und Kandidatenliste

Request `[19]`; Antwort 48 Bytes, Mehrbytefelder Little Endian:

| Offset | Typ | Bedeutung |
|---:|---|---|
| 0–3 | vier u8 | Schema 1, Format 6, erster Kandidat 18, Anzahl 6 |
| 4 | u32 | Für den Qualitäts-Inhaltsfingerabdruck verwendete Bytes: 525 |
| 8 | u32 | Größe des Befundberichts: 48 |
| 12 | u32 | Jobabschnitt des Befundberichts: 13 |
| 16–39 | sechs u32 | 500000, 380228, 346021, 312500, 280112, 251256 bit/s |
| 40–44 | fünf u8 | 80 Zylinder, 16 Zylinder je Zone, maximal 12/minimal 8 Sektoren, 12 Tag-Bytes |
| 45–47 | drei u8 | Reserviert 0 |

Op 0 behält seine fünf eingebetteten Formatdeskriptoren und deren Offsets bei;
Format 6 wird separat über Op 19 beschrieben. Kandidatenanzahl bei Op-0-Offset
160 ist jetzt **24**. Jobabschnitt 3 wächst von 36 auf **48 Bytes** (24 u16).
Die ersten 18 Einträge behalten ihre Bedeutung. Nicht versuchte Kandidaten
bleiben 0. Die bisherigen Qualitätsbericht-Grundgrößen bleiben unverändert.

#### Medienprofil: zwei ausdrücklich unterscheidbare Zeitmodelle

Op 15 erlaubt Format 6, maximal 80 Zylinder. Sein bisher reserviertes Byte 7
hat ausschließlich bei Format 6 folgende Bedeutung:

- **0:** Rate bei Offset 8 ist ausdrücklich deklariert; eine der sechs Raten
  von Op 19. Beispielsweise 500000 für native variable Mac-Drehzahl. Eine
  einzelne erwartete RPM-Zahl beschreibt hier nicht sämtliche Spurzonen.
- **1 = MAC_FIXED_300:** Rate bei Offset 8 muss 0 sein. Erwartete Kanalrate
  wird aus physischer Kopfposition und wirksamem Schrittfaktor bestimmt.
  Dafür muss POSITION_VALID gesetzt sein. Das Laufwerksprofil sollte 300 RPM
  deklarieren; 0 bleibt unbekannt, 360 widerspricht diesem Zeitmodell.

Andere Werte sind BAD_ARGS. Für Formate 1–5 muss Byte 7 weiterhin 0 bleiben.
Automatisch erkannte Profile setzen Byte 7 nicht: Das Modell wird nie allein
aus einem dekodierten Sektor als Laufwerkseigenschaft angenommen.

| Logische Zylinder | Sektoren je Spur | Kanalrate bei festen 300 RPM |
|---|---:|---:|
| 0–15 | 12 | 380228 |
| 16–31 | 11 | 346021 |
| 32–47 | 10 | 312500 |
| 48–63 | 9 | 280112 |
| 64–79 | 8 | 251256 |

Sektorzahl im Medienprofil vorzugsweise 0 (= zonenabhängig) lassen. Eine
deklarierte feste Sektorzahl wird im festen 300-RPM-Modell gegen die aktuelle
Zone geprüft. Dies ist eine Prüfung des ausdrücklich gewählten Modells, keine
automatische Drehzahlsteuerung oder Vermessung eines nativen Mac-Laufwerks.

CHECK_PROFILE nutzt weiterhin Jobflag 0x0020. Neue Ergebnisbits in Abschnitt 11:

| Wert | Name | Bedeutung |
|---:|---|---|
| 32 | ZONE_MISMATCH | Befund widerspricht dem gewählten Zonenmodell oder dekodierte IDs widersprechen Spur/Seite/Zonensektorbereich |
| 64 | ALIGNMENT_UNCERTAIN | Bei Mac-Befunden: kein bestätigter spurübergreifender Phasenbezug vorhanden |

Zusätzliche Ursachenbits: Bit9 keine gültige Position für Zonenprüfung;
Bit10 deklarierte RPM widerspricht MAC_FIXED_300; Bit11 Rate/Sektorzahl
widerspricht der Zone; Bit19 gültig dekodierte ID liegt außerhalb der
angeforderten Spur/Seite/Zonensektoren. Bit9 setzt INSUFFICIENT_EVIDENCE,
Bit10 PROFILE_CONFLICT. Fehlende Sektoren allein sind kein ZONE_MISMATCH.

#### Jobabschnitt 13: Befunde getrennt auswerten

Abruf wie andere Qualitätsabschnitte mit Job-ID und Berichtsgeneration.
Bei inzwischen ersetztem Qualitätsbericht: BUSY. Kein zusätzliches Jobflag.

| Offset | Typ | Bedeutung |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Format-ID |
| 2 | u8 | Mac-Spurzone 0–4; sonst 255 |
| 3 | u8 | Erwartete Mac-Sektoren 8–12; sonst 0 |
| 4 | u32 | Befundbits |
| 8 | u64 | Passende, gültige und nicht konfliktbehaftete Mac-Sektor-IDs |
| 16 | u64 | Erwartete Mac-Sektor-IDs |
| 24 | u32 | Sektoren mit verschiedenen prüfsummengültigen Inhalten |
| 28 | u32 | Sektoren mit fehlerhaften/unsicheren Beobachtungen |
| 32 | u32 | Headerfehler des nominalen Decoderprofils |
| 36 | u32 | Datenfehler des nominalen Decoderprofils |
| 40 | u32 | Kodierungsfehler des nominalen Decoderprofils |
| 44 | u32 | Tag-Länge: Mac 12, sonst 0 |

Befundbits: 1 Mac-ID außerhalb Spur/Seite/Zone; 2 Mac-Sektorsatz unvollständig;
4 spurübergreifende Ausrichtung unbestätigt; 8 verschiedene gültige Inhalte;
16 fehlerhafte/unsichere Dekodierung; 32 kein prüfsummengültiger Sektor;
64 mehrdeutige Formaterkennung. Die Sektorzähler können sich überschneiden.
Bit4 (Wert 16) unterscheidet Fehlerbefunde von Bit3 (Wert 8), das ausdrücklich
verschiedene gültige Inhalte beschreibt. Kein Befund ist ein Weak-Bit-Beweis.

Der bestehende Umdrehungsvergleich in Abschnitt 7 ergänzt reservierte Felder:
u32 bei Offset 24: Bit0 fehlende Flanken, Bit1 zusätzliche Flanken;
u32 bei Offset 28: Bit0 Ausrichtung unbestätigt. Fenster, Normalisierung,
Toleranz und Reportgröße bleiben gleich. Ohne angeforderten REV_COMPARE bleibt
der Vergleich nicht verfügbar; unbekannte Daten werden nicht als „stabil“ gemeldet.

Eine tatsächliche spurübergreifende Winkelmessung ist nicht implementiert.
Die Berichte stellen diese fehlende Messgrundlage ausdrücklich dar. MOOF bleibt
ein Container für das Tool; die Firmware verarbeitet Flux und Sektorbefunde.

### 5.5 Lokale Zeitfenster, HFEv3 und getrennte Schwachstellenbefunde

INFO-Bit30 kündigt die lokale Auswertung an. Keine neuen Jobbits: Analyseflag
`TIMING=0x04` oder `WINDOWS=0x80` aktiviert die Messung (zusammen mit dem
vorhandenen DETAIL-Bit0). Für Flankenunterschiede zusätzlich `REV_COMPARE=1`
im Recovery-Reportmask-Feld setzen. `RAW_INDEX` liefert weiterhin die
Indexereignisse; der Aufnahmekontext beschreibt die Indexquelle. Die lokale
Auswertung normalisiert Positionen auf die Dauer der jeweiligen Umdrehung,
führt aber keine Phasenausrichtung durch. Auch bei echtem Index bleibt diese
Unsicherheit ausdrücklich erhalten.

**Discovery:** `0x0F`, Nutzlast `[20]`, Antwort 24 Bytes:
Bytes 0–3 = Schema 1, Abschnitt 14, Fensterzahl 32, Eintragsgröße 40.
u32 bei 4 = 1312 Gesamtbytes, bei 8 = Aktivierungsmaske `0x84`, bei 12 =
Ergebnisflagmaske `0xff`, bei 16 = 64 Mindestintervalle, bei 20 = 30 Promille
Abweichungsschwelle. Keine Zustandsänderung.

**Abruf:** Op3, Abschnitt 14, mit Job-ID, Generation und CRC-Paginierung wie
Abschnitt 13. Ersetzte/fehlende Ergebnisse liefern BUSY. 1312 Bytes insgesamt:

| Offset | Typ | Bedeutung |
|---:|---|---|
| 0–2 | je u8 | Schema 1, 32 Fenster, 40 Bytes je Fenster |
| 3 | u8 | Vollständig analysierte nominale Umdrehungen als Bitmaske |
| 4 | u32 | Qualitätsgeneration |
| 8 | u32 | Erkannte/gewählte Nominalrate |
| 12 | u8 | Format-ID |
| 13 | u8 | Gewählte Aktivierungsbits aus `0x84`; 0 = keine lokale Zeitmessung |
| 14 | u8 | Vollständig verglichene Umdrehungen; 0 = kein Flankenvergleich verfügbar |
| 15 | u8 | Formaterkennung mehrdeutig |
| 16 | u32 | Mindestintervalle: 64 |
| 20 | u32 | Änderungsschwelle: 30 Promille |
| 24 | u32 | Median der ausreichend belegten Fenstermittel, Zellzeit in Q8-Timerticks |
| 28 | u32 | Reserviert, 0 |
| 32 | 32 × 40 Bytes | Fenster in zeitlicher Reihenfolge |

Fenster k deckt `[k/32, (k+1)/32)` der jeweiligen Umdrehung ab. Alle Felder
eines Fenstereintrags sind u32 little endian:

| Relativer Offset | Inhalt |
|---:|---|
| 0 | Ergebnisflags |
| 4 | Akzeptierte Intervalle über nominal analysierte Umdrehungen |
| 8, 12, 16 | Mittel, Minimum, Maximum der geschätzten Zellzeit, Q8-Timerticks |
| 20 | Nicht verwendbare Intervalle |
| 24 | Flanken mit neuem Header-, Daten- oder aktiviertem Kodierungsfehler |
| 28 | Beobachtungen mit gültigem Inhalt abweichend vom zuerst gültigen Sektorinhalt |
| 32, 36 | Fehlende/zusätzliche Flanken; Summe aus vier bestehenden Vergleichsfenstern |

| Wert | Flag | Bedeutung |
|---:|---|---|
| 1 | SUFFICIENT_TIMING | Mindestens 64 Intervalle, mindestens zwei verschiedene Zellvielfache, höchstens 10 % verworfen, Zellzeitspannweite höchstens 5 % des Mittels |
| 2 | RATE_CHANGE_CANDIDATE | Ausreichendes Fenster, mindestens drei ausreichende Fenster insgesamt, eindeutige Formaterkennung, mindestens 3 % Abstand vom Median |
| 4 | DECODE_ERRORS | Decoderfehler am Endpunkt einer beobachteten Flanke |
| 8 | CONTENT_VARIATION | Verschiedene prüfsummengültige Inhalte beobachtet |
| 16 | MISSING_EDGES | Flanken fehlen gegenüber Referenzumdrehung |
| 32 | EXTRA_EDGES | Zusätzliche Flanken gegenüber Referenzumdrehung |
| 64 | ALIGNMENT_UNCERTAIN | Keine zusätzliche Phasenausrichtung; derzeit immer gesetzt |
| 128 | INCOMPLETE | Analysefehler oder nicht alle angeforderten nominalen Umdrehungen fertig |

Die Schätzung teilt das Fluxintervall durch das nächstgelegene ganzzahlige
Vielfache der nominalen Zellzeit (1–8 Zellen). Abweichungen über 12,5 %,
indexübergreifende Intervalle und längere Lücken werden ausgeschlossen. Der
Schätzer verwendet nicht den adaptiven PLL-Zustand. Eine globale, gleichmäßige
Drehzahlabweichung erzeugt allein kein lokales Änderungssignal. Lokale
Drehzahlschwankungen, falsche Nominalraten oder spezielle Datenmuster können
trotzdem Kandidaten erzeugen: Flag 2 beweist weder Kopierschutz noch eine
bestimmte Kodierung. Kein Fensterbefund beweist physische Weak Bits.

Fehler und Inhaltskonflikte werden im Fenster der erkennenden Abschlussflanke
gezählt, nicht als exakte Ausdehnung des gesamten Sektors. Korrelation bedeutet
räumliche Nähe, keine nachgewiesene gemeinsame Ursache. Kodierungsfehler
setzen das bestehende CODING-Feature voraus. Zähler sättigen; Phasen- und
Rateninformationen werden nicht zwischen verschiedenen Aufnahmen vermischt.
Hostparser: `ufi_host.regions.decode_regions(payload)`.

**HFEv3 im Tool:** Der Host akzeptiert `HXCHFEV3`, Revision 0, und interpretiert
NOP, INDEX, BITRATE, SKIP und RAND unter Erhalt von Zellzeiten, Indexpositionen
und schwachen Bereichen. Zellzeiten sind Vielfache von 1/36 MHz; Fluxintervalle
werden auf absoluter Zeitbasis auf 275 MHz gerundet. Bei einer Indexmarke wird
die Spur an diese Marke gedreht, ohne Marke bleibt der Containeranfang Referenz.
HFEv2 und unbekannte Opcodes werden weiterhin abgewiesen.

Deterministische HFEv3-Spuren werden mit dem nativen Extended-WRITE-Pfad
geschrieben, ohne automatische 98,5-%-Anpassung. `--verify` wählt hierbei
Firmware-Prüflesen, also WRITE-Bits `0x21` und bei Quellperiode zusätzlich
`SOURCE_INDEX=0x40`. Die Periode dient der Referenzdecodierung; sie ist kein
neuer Schreibendzeit-Befehl. Unbekannte/ungeeignete Sektorformate können beim
Prüflesen vor dem Schreiben abgewiesen werden. Host-API:
`write_track(..., extended=True, firmware_verify=True, index_ticks=...)`;
`verify=True` bleibt der bestehende separate Host-Prüfpfad und darf nicht
gleichzeitig gesetzt werden.

RAND und mehrere Indexmarken bleiben als Metadaten zugänglich, werden aber
ohne implementierte physische Weak-Write-/Hartsektor-Zuordnung nicht automatisch
geschrieben. Export solcher HFEv3-Objekte als HFEv1 wird abgewiesen, damit keine
Zeit-/Weak-Metadaten verloren gehen. SD-Firmware unterstützt zusätzlich v3 gemäß7.1/6.3; Greaseweazle-
Kommandos und Flags bleiben unverändert.

### 5.6 Erkennungszustände, Positionsbefunde und Wiederholungsursachen

INFO-Bit31 kündigt diese Erweiterung an (Capability-Wort als **u32** behandeln).
Keine neuen Auftragsflags: `CHECK_PROFILE`, `EXPECTED_SET` und die bestehende
`CONFIRM_CONTENT`-Recovery-Policy bleiben zuständig. Alte Statuswerte und die
40-Byte-Versuchshistorie bleiben unverändert.

**Discovery:** `0x0F [21]` liefert 24 Bytes. Bytes 0–3: Schema 1, Abschnitt 15,
Abschnitt 16, Historieneintragsgröße 20. u32 bei 4: 64 Bytes Erkennungsbericht;
bei 8: Erkennungsflagmaske 1023; bei 12: Ursachenmaske 32767; bei 16: maximal
8 Versuche; bei 20: neues Profilprüfungsflag 128.

**Abschnitt 15:** 64 Bytes, letzte versuchte Aufnahme. Abruf über Op3 mit
Job-ID, aktueller Jobgeneration, Offset und Seiten-CRC. Auch ein früher
Aufnahmefehler ohne Qualitätsgeneration bleibt abrufbar (Generation dann 0).
Host-Referenz (ufi_host, 1.18-dev): `analysis_start(job_id, track, side, …)` (Op 1 mit
DETAIL), `analysis_status()` / `analysis_wait()` (Op 2), `analysis_abort()` (Op 4),
`analysis_section(job_id, generation, section)` (Op 3 seitenweise mit Kopf- und
CRC-Prüfung), `recognition_discovery()` (Op 21) und `recognition(job_id)` (Abschnitt 15
dekodiert, `recognition.decode_recognition` plus Flagnamen); Kommandozeile
`ufi recognize SPUR SEITE [--expect MASKE --chn C H N --attempts N --retry]`.

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Format-ID; 0 ohne Decoderbericht |
| 2, 3 | je u8 | Recovery-Policy, Zahl aufeinanderfolgender Inhaltsbestätigungen |
| 4 | u32 | Erkennungsflags |
| 8 | u32 | Ursachenmaske des letzten Versuchs |
| 12 | u32 | Sektor-Datensätze mit mindestens einer gültigen Datenbeobachtung und ohne unsicheren Header; Inhaltskonflikte können enthalten sein |
| 16 | u32 | Davon mit abweichendem C oder H bei aktivierter Prüfung |
| 20, 28 | je u64 | Explizit erwartete und beobachtete passende Sektor-ID-Masken |
| 36 | 12 Bytes | Medienprofil aus Aufnahmekontext, ersatzweise Jobstart; Herkunft im Profilbyte 6: 0 unbekannt, 1 vorgegeben, 2 beobachtet |
| 48, 49 | je u8 | Vergleichs-C/H: explizite Erwartung oder angeforderte logische Spur/Seite |
| 50 | u8 | Erwartetes N, nur bei Byte51 = 1 relevant |
| 51 | u8 | Expliziter C/H/N/Sektorsatz übergeben |
| 52, 56 | je u32 | Qualitätsgeneration, Capture-ID; 0 wenn nicht vorhanden |
| 60 | u8 | Versuchsnummer ab 1 |
| 61 | u8 | Phase bei Aufzeichnung: 1 Aufnahme/Vorbereitung, 2 Analyse |
| 62 | u8 | Entscheidung: 0 keine Wiederholung, 1 erneut aufnehmen, 2 Inhalt bestätigt, 3 Stagnationsstopp, 4 Versuchslimit |
| 63 | u8 | Abschließender Jobstatus |

**Erkennungsflags:**

| Wert | Name | Aussage |
|---:|---|---|
| 1 | PROFILE_PRESENT | Medienbeschreibung vorhanden, unabhängig von Herkunft |
| 2 | FORMAT_OBSERVED | Mindestens ein gültiger Sektor eines bekannten Formats beobachtet |
| 4 | SECTOR_SET_CONFIRMED | Expliziter C/H/N/Sektorsatz exakt beobachtet; keine Konflikte, fehlerhaften Sektorbeobachtungen, nominalen Header-/Daten-/aktivierten Kodierungsfehler oder unvollständige/mehrdeutige Analyse |
| 8 | GEOMETRY_UNCONFIRMED | Vollständige Diskettengeometrie durch diesen Spurauftrag nicht bestätigt; immer gesetzt |
| 16 | AMBIGUOUS | Formaterkennung mehrdeutig; getrennt von einer unvollständigen Aufnahme |
| 32 | HEADER_POSITION_MISMATCH | Gültige Sektorbeobachtungen haben unerwartetes C/H |
| 64 | CONTENT_CONFIRMED | Bestehende CONFIRM_CONTENT-Policy hat ihre Wiederholungszahl erreicht |
| 128 | INCOMPLETE | Aufnahme-/Analysefehler, fehlender Bericht, verlorene Sektor-Datensätze oder unvollständige nominale Durchläufe |
| 256 | POSITION_CHECKED | Vergleich aktiviert und mindestens eine gültige Sektorbeobachtung vorhanden |
| 512 | PROFILE_DECLARED | Profil ausdrücklich vorgegeben, nicht nur automatisch beobachtet |

`FORMAT_OBSERVED` kann zusammen mit `AMBIGUOUS` vorkommen. Ein gefundener Sektor
bestätigt weder Geometrie noch Vollständigkeit. `SECTOR_SET_CONFIRMED` beschreibt
den expliziten Satz dieser Spur; es verlangt keine Wiederholung. Diese wird
getrennt durch `CONTENT_CONFIRMED` gemeldet. Ohne expliziten Sektorsatz bleibt
das Flag mit Wert 4 aus; aus einer Sektoranzahl wird kein ID-Satz erfunden.

**Positionsprüfung:** Mit EXPECTED_SET werden dessen C/H verwendet. Alternativ
aktiviert CHECK_PROFILE den Vergleich bei einem ausdrücklich vorgegebenen,
zum Decoderformat passenden Profil für IBM-MFM, Amiga, IBM-FM, HP-MMFM und
Mac-GCR. Vergleich gegen die angeforderte logische Spur/Seite, keine zusätzliche
Multiplikation bei Doppelschritten. Decoder-normalisierte IDs werden verwendet.
CBM-GCR benötigt EXPECTED_SET mit passender Nummerierung (Header-Spur ab 1,
Header-Seite 0). Abweichende IDs können Absicht sein und führen allein weder
zu einer erneuten Aufnahme noch zu Rekalibrierung oder einem Schreibvorgang.

Bei CHECK_PROFILE ergänzt Abschnitt 11 `HEADER_POSITION_MISMATCH=128` und
Ursachenbit20. Bestehende Mac-Zonenbefunde bleiben separat erhalten.

**Abschnitt 16:** 20 Bytes je tatsächlich begonnenem Versuch, maximal 160 Bytes.
u32 bei 0: Erkennungsflags; bei 4: Ursachen; bei 8: gültige Sektor-Datensätze;
bei12: C/H-Abweichungen; Byte16 Phase, Byte17 Entscheidung. Policy3 verwendet
Byte18 für neue gültige Inhaltsidentitäten und Byte19 Bit0 für unvollständige
Registrierung (4.20). Policy4 zählt in Byte18 neue Evidenzeinträge einschließlich
Rohvergleichsmustern; Byte19 Bit0 meldet unvollständige Registrierung, Bit1 fehlende
Rohvergleichsevidenz. Bei Policies0–2 bleiben diese Bytes0.
Reihenfolge wie Abschnitt 1. Früh gescheiterte Wiederholungen erben keine
Decoderbefunde des vorherigen Versuchs. Entscheidungen beschreiben den Ablauf,
Ursachen den Befund; ein Befund muss keine Wiederholung ausgelöst haben.

| Ursachenwert | Bedeutung |
|---:|---|
| 1 | Explizit erwartete Sektoren fehlen im vorhandenen Decoderbericht |
| 2, 4, 8 | Nominale Header-, Daten- bzw. aktivierte Kodierungsfehler |
| 16 | Unterschiedliche gültige Sektorinhalte |
| 32 | DMA-/Transportstatus aus Aufnahme oder Analyse |
| 64 | Aufnahmepuffer voll |
| 128 | Seek/kein Laufwerk oder unerwartete C/H-IDs; Originalstatus in Abschnitt 1 unterscheidet diese Fälle |
| 256 | Kein Index |
| 512 | Medien-/Laufwerksgeneration gewechselt |
| 1024, 2048 | Timeout bzw. Abbruch |
| 4096 | Weitere Inhaltsbestätigung benötigt |
| 8192 | Unvollständige Evidenz, einschließlich verlorener Detailereignisse |
| 16384 | Decoderbericht vorhanden, aber kein gültiges Format beobachtet |

Der historische Endstatus DMA kann weiterhin einen fehlenden erwarteten Satz
bezeichnen. Er wird nicht nachträglich als Transportfehler klassifiziert:
Ursachen entstehen am tatsächlichen Aufnahme-/Analysefehler oder aus Befunden.
Medienwechsel und Transportfehler bleiben terminal; diese Erweiterung führt
keine automatische mechanische Wiederherstellung ein. Wiederholungsgrenzen,
Zeitbudget und Abbruchpfade bleiben erhalten.

Hostparser: `ufi_host.recognition.decode_recognition()` und
`decode_attempt_assessments()`. Greaseweazle-Wireformat unverändert.

### 5.7 Apple-II-5,25-Zoll-GCR (Format 7)

Decoder für 16-Sektor-Disketten des Apple II (DOS 3.3, ProDOS; 6-and-2-Kodierung,
250 kbit/s, 4-µs-Zellen), eigenständig nach dem dokumentierten Spuraufbau umgesetzt
(Referenztabellen in `REA_Floppy_Emu_Analyse_2026-10-17.md`). Adressfeld `D5 AA 96`,
dann Volume, Spur, Sektor und Prüfsumme (XOR) in 4-and-4-Kodierung, Epilog `DE AA`;
Datenfeld `D5 AA AD`, 342 Nibbles, Prüfnibble, Epilog `DE AA`. Der Decoder löst die
laufende XOR-Kette und die 6-and-2-Aufteilung auf und liefert je Sektor 256 Nutzbytes.

- **Sektorkennung:** `c` = Spur 0–39, `h` = 0, `r` = **physischer** Sektor 0–15,
  `n` = 1 (256 Byte). Die Volume-Nummer steht im Decoder (`disk_id`), nicht in der
  Kennung. DOS-3.3- oder ProDOS-Reihenfolge stellt der Host her (Tabellen
  `0 7 14 6 13 5 12 4 11 3 10 2 9 1 8 15` bzw. `0 8 1 9 2 10 3 11 4 12 5 13 6 14 7 15`).
- **Gültigkeit:** Kopf nur mit passender XOR-Prüfsumme, Spur < 40, Sektor < 16 und
  Epilog; Datenfeld nur innerhalb von 4000 Zellen nach einem gültigen Kopf. `crc_ok`
  im Rückruf bedeutet Prüfnibble und Epilog stimmen; Nibbles außerhalb des
  6-and-2-Alphabets oder ohne gesetztes höchstes Bit zählen als Kodierfehler.
- **Zugang:** Format 7 wird **nur ausdrücklich** gewählt (Formathinweis in ANALYZE und
  in den Analysejobs, Laufwerks-/Medienprofil Op 14); die automatische Erkennung
  (Kandidatenliste, 5.3/6.1) enthält kein Apple-II-Profil, damit Berichtsaufbau und
  Op-0-Fähigkeiten unverändert bleiben. Qualitätsbericht, Prüflesen und Vergleich
  arbeiten mit 256-Byte-Sektoren wie bei IBM `N=1`; die Mac-spezifischen Zonenfelder
  bleiben bei Format 7 leer (255). Halb- und Viertelspuren werden über das Positionsbyte
  im Seitenfeld von ANALYZE und Analysejob angefahren (5.2, Byte 1: `0x80 | Viertel << 4`,
  nur Apple-Laufwerke); der Bericht trägt die Position an Offset 19, die Kennung der
  Sektoren bleibt die gelesene Spurnummer.
- **Decoder-Optionen (Op 33):** `[33]` liest, `[33, Schema 1, 12 Bytes]` setzt (exakt
  14 Bytes); Antwort `[1, 12 Bytes]`. Die 12 Bytes: Adressprolog (3, Standard
  `D5 AA 96`), Datenprolog (3, `D5 AA AD`), Adressepilog (2, `DE AA`), Datenepilog (2,
  `DE AA`), Flags u16. Alle Markenbytes müssen Bit 7 gesetzt haben, Adress- und
  Datenprolog dürfen nicht gleich sein, unbekannte Flagbits ergeben BAD_ARGS; Setzen bei
  aktiver Operation oder Integritätsjob BUSY. Die Optionen gelten global bis zum
  nächsten Setzen oder Neustart und wirken auf jeden Format-7-Decoder (ANALYZE,
  Analysejobs, Prüflesen).

| Flag | Bedeutung |
|---:|---|
| 0x0001 | Kopf mit falscher 4-and-4-Prüfsumme trotzdem verwenden (Sektor wird gemeldet) |
| 0x0002 | Epiloge nicht verlangen |
| 0x0004 | keine Grenze Spur < 40 / Sektor < 16 (Sektor < 64 bleibt; Spiral-/Halbspurschutz) |
| 0x0008 | falsches Prüfnibble des Datenfelds setzt `crc_ok` nicht zurück (geänderte Prüfsummenregel) |
| 0x0010 | Datenfeld auch ohne vorausgehenden gültigen Kopf annehmen; Kennung dann `c = r = 255` |
| 0x0020 | 13-Sektor-Datenfelder (DOS 3.2, 5-and-3): 410 Nibbles aus dem 32er-Alphabet, Aufteilung 154 Niederbit- plus 256 Hochbit-Nibbles, XOR-Kette und Prüfnibble wie bei 6-and-2; Adressprolog dazu meist `D5 AA B5` (Op 33 setzen) |

- **Nibble-Statistik (Op 35, 1.18-dev):** `[35]` liefert 178 Bytes über die letzte
  Format-7-Analyse (Rücksetzen bei jedem Analysestart mit Formathinweis 7, gezählt nur vom
  Standard-PLL-Profil, Umdrehung aus dem Qualitätsablauf). Aufbau: `schema` u8 = 1,
  `revs` u8 (0 = noch keine Analyse), dann 8 × je Umdrehung fünf u16: `nibbles` (gerahmte
  Nibbles, Felder und Syncs), `sync_nibbles` (FF mit nachlaufenden Nullzellen, also
  10-Bit-Selbstsync; der letzte FF des Stroms bleibt ohne Folgenibble ungezählt),
  `address_fields` (Adressprologe), `data_fields` (angenommene Datenprologe),
  `orphan_headers` (gültiger Kopf ohne Datenfeld bis zum nächsten Kopf oder 4000 Zellen);
  danach 16 × je physischem Sektor sechs u8: `volume` des letzten Kopfs,
  `sync_before_addr` (zusammenhängende Sync-Nibbles vor dem Adressprolog, Kappung 255),
  `header_to_data` (Zellen/8 vom Kopfende bis zum Ende des Datenprologs),
  `sync_before_data`, `flags` (Sektorbefunde 0x08/0x20/0x40/0x80 des letzten Datenfelds),
  `seen` (Umdrehungsmaske der Datenfelder). Damit lassen sich Nibble-Zählschutz,
  veränderte Sync-Längen, Volume-Wechsel je Sektor und Köpfe ohne Daten ohne Hostdecodierung
  erkennen. Host: `Device2.apple_gcr_stats()`, `ufi apple-stats`.
  **Schema 2** (Antwort 274 Bytes, die ersten 178 wie Schema 1): ab Offset 178 je
  Umdrehung drei u16 `trail1/trail2/trail3` (FF-Nibbles mit 1, 2 bzw. ≥ 3 nachlaufenden
  Nullzellen; ≥ 3 = Zeitbits von Schutzverfahren), ab Offset 226 vier Einträge
  `third` u8, reserviert u8, `count` u16 (häufigste dritte Nibbles nach `D5 AA`, die weder
  Adress- noch Datenprolog sind, zum Auffinden fremder Marken ohne Raten), ab Offset 242
  16 × u16 `position` (Zellen vom Umdrehungsbeginn bis zum Beginn des Adressprologs des
  zuletzt gesehenen Kopfs je physischem Sektor; Sektorabstände und Spiralschutz).
- **Symboltabelle (Op 36, 1.18-dev):** `[36]` liest, `[36, Schema 1, 64 Symbole]` setzt
  (exakt 66 Bytes); Antwort `[1, 64 Symbole]`. Alle Symbole müssen Bit 7 gesetzt haben und
  verschieden sein, sonst BAD_ARGS; Setzen bei aktiver Operation BUSY. Die Tabelle gilt für
  den 6-and-2-Datenpfad des Format-7-Decoders bis zum nächsten Setzen oder Neustart (die
  Standardtabelle setzt zurück); der Macintosh-Decoder bleibt unberührt. Host:
  `Device2.apple_gcr_table()`, `ufi apple-table [standard|HEX128]`.
- **13-Sektor-Disketten (Flag 0x0020):** Datenfelder werden als 5-and-3 gelesen (410
  Nibbles, fester 32er-Zeichensatz, Aufteilung nach *Beneath Apple DOS*: 154 Nibbles mit den
  unteren drei Bits, dann 256 mit den oberen fünf, Byte 255 getrennt), Kennung und Prüfregeln
  wie bei 16 Sektoren. Für DOS-3.2-Disketten zusätzlich `addr_prologue = D5 AA B5` setzen.
- **Befunde je Sektor:** Zusätzlich zu `crc_ok` setzt der Decoder Sektorflags, die der
  Qualitätsbericht in das Flagbyte des Sektoreintrags übernimmt: 0x08 Datenfeld ohne
  gültigen Kopf (Flag 0x0010), 0x20 Kopfprüfsumme falsch, 0x40 Datenprüfnibble falsch,
  0x80 ein Epilog weicht ab (dieselben Bits wie bei allen anderen Decodern, 5.8). Die Zähler
  `header_errors`, `data_errors` und `code_errors` (Nibble außerhalb des Alphabets oder
  ohne Bit 7) zählen unabhängig von den Toleranzflags weiter, so dass eine „tolerante"
  Analyse die Abweichungen trotzdem ausweist.
- **Nicht enthalten:** 13-Sektor-Disketten (5-and-3) und Schreiben im Format 7; für
  Schutzverfahren jenseits geänderter Marken (Zeitbits, Nibble-Zählungen, Synchronlücken)
  bleibt der Flusspfad (READ, NIB über den Host) zuständig.

### 5.8 Toleranzflags und Sektorbefunde aller Decoder (Op 34)

Die Toleranzflags aus 5.7 gibt es ab 1.18-dev auch für die Decoder der Formate 1–6
(IBM-MFM, Amiga, IBM-FM, HP-MMFM, Commodore-GCR, Macintosh-GCR), mit denselben Bitwerten.
Sie wirken **nur auf die Analyse-Decoder** (ANALYZE, Analysejobs, QUALITY); Dump nach IMG/
ADF/D64, Prüflesen nach WRITE und die Formaterkennung bleiben strikt. Fehlerzähler
(`header_errors`, `data_errors`, `code_errors`) zählen unter Toleranz weiter.

- **Op 34:** `[34]` liest, `[34, Schema 1, Flags u16 LE]` setzt (exakt 4 Bytes); Antwort
  `[1, Flags u16]`. Unbekannte Bits oder falsches Schema → BAD_ARGS, Setzen bei aktiver
  Operation oder Integritätsjob → BUSY. Die Flags gelten bis zum nächsten Setzen oder
  Neustart; Op 34 ist während eines laufenden Jobs nur lesend erlaubt. Host:
  `Device2.decoder_tolerance()` / `ufi tolerance`.

| Flag | IBM-MFM / FM | HP-MMFM | Amiga | Commodore | Macintosh |
|---:|---|---|---|---|---|
| 0x0001 Kopf behalten | ID-Feld mit falscher CRC wird verwendet | wie MFM | Kopfprüfsumme falsch, Sektor trotzdem gemeldet | Kopfblock-Prüfsumme falsch, trotzdem verwendet | 4-and-4-Kopfprüfsumme falsch, trotzdem verwendet |
| 0x0002 Epiloge nicht verlangen | ohne Wirkung | ohne Wirkung | ohne Wirkung | ohne Wirkung | `DE AA` hinter Kopf und Daten nicht verlangt |
| 0x0004 keine Geometriegrenze | ohne Wirkung (N ≤ 3 bleibt Puffergrenze) | Zylinder > 76 / Sektor ≥ 30 zugelassen | Formatbyte ≠ 0xFF und Sektor ≥ 22 zugelassen | Spur außerhalb 1–84 / Sektor ≥ 21 zugelassen | Zylinder ≥ 80 und gesetzte Reservebits zugelassen |
| 0x0008 Daten ohne Prüfsumme | Daten-CRC falsch setzt `crc_ok` nicht zurück | wie MFM | Datenprüfsumme falsch, Sektor gilt als gelesen | Datenblock-Prüfsumme falsch, gilt als gelesen | Prüfnibbles falsch, gilt als gelesen |
| 0x0010 Daten ohne Kopf | Datenfeld ohne gültiges ID-Feld; Größe aus dem zuletzt gesehenen ID-Feld (auch mit falscher CRC), sonst 512 Byte | wie MFM, 256 Byte | ohne Wirkung (Kopf und Daten sind ein Block) | Datenblock ohne Kopfblock | Datenfeld ohne Adressfeld, Sektornummer nicht prüfbar |

Kopflose Datenfelder tragen die Kennung `c = r = 255` (`h = 0`, `n` wie oben). Bei
Commodore liegt die Disk-ID weiterhin in Byte 256/257, bei Macintosh das Formatbyte
in Byte 524.

**Sektorbefunde** (Flagbyte des Sektoreintrags, 5.2, Offset 11), für alle Formate
einschließlich Format 7: Bit3 (0x08) Datenfeld ohne gültigen Kopf angenommen, Bit5 (0x20)
Kopf-CRC/-Prüfsumme falsch, Bit6 (0x40) Daten-CRC/-Prüfsumme falsch, Bit7 (0x80) Epilog
weicht ab. Die Bits werden auch ohne Toleranzflags gesetzt, soweit der Sektor überhaupt
gemeldet wird (Bit6 etwa bei einem normalen CRC-Fehler). **Herkunftsregel:** Ein Sektor,
der in der Umdrehungsmaske `seen` steht und eines der Bits 3, 5, 6 oder 7 trägt, wurde nur
durch ein Toleranzflag als gelesen gewertet; ohne Toleranz stünde er in `bad` oder gar
nicht im Bericht. Ein Sektor jenseits der Geometriegrenzen (0x0004) ist an seiner
Kennung erkennbar. Der Qualitätsblock trägt an Offset 60 die Op-34-Flags und an Offset 62
die Op-33-Flags, die bei diesem Decodierlauf wirksam waren, damit ein Bericht später
eindeutig als strikt oder tolerant decodiert eingeordnet werden kann.

## 6 Analysejobs

### 6.1 Analysejobs, Discovery und Prüfabdeckung (INFO Bit24, 2026-10-14)

**Neues natives Kommando `ANALYSIS_JOB = 0x0F`**. Das erste Payloadbyte ist
die Unterfunktion. Alle Mehrbytewerte sind Little Endian. Greaseweazle verwendet
weiter seine bisherigen Befehle und Datenformate. Die bisherigen Analyseflags
bleiben erhalten; neue Ablaufentscheidungen erhalten ein eigenes `u16 job_flags`.
So werden Decoderbefunde und Wiederholungsstrategie nicht vermischt.

| Op | Exakte Requestlänge | Funktion |
|---:|---:|---|
| 0 | 1 | Fähigkeiten und Grenzen abfragen, Antwort 192 Bytes |
| 1 | 24 + ANALYZE-Requestlänge | Aufnahme und kooperative Auswertung starten |
| 2 | 5 | Jobstatus: `op u8, job_id u32` |
| 3 | 16 | Ergebnisabschnitt seitenweise lesen |
| 4 | 5 | Genau diesen Job abbrechen: `op u8, job_id u32` |
| 5 | 5 | Ausgewählte Bring-up-Tests ausführen |
| 6 | 1 | Referenzinventar des letzten WRITE abfragen |
| 7 | 1 / 3 | Diagnoseaufnahmeflags lesen / setzen (4.1) |
| 8 | 12 | Aufnahme-Kontext / Rohindex / Transporthistorie lesen (4.1) |
| 10 | 1 / 14 | Zusätzliche Laufwerkszeiten lesen / setzen (4.1) |
| 11 | 1 / 12 | Vergleichsberichte und Wiederholungsstrategie lesen / setzen (5.3) |
| 12 | 1 | Fähigkeiten der Vergleichs-/Wiederholungserweiterung (5.3), 32 Bytes |
| 13 | 1 | Laufwerks-/Medienstatus, 40 Bytes (4.2) |
| 17 | 1 | Portbelegung, Leitungskonflikte und Schreibgrenzen, 24 Bytes (4.4; INFO Bit28) |
| 18 | 1 / 2 / 41 | Wirksame Betriebseinstellungen lesen / Zeit-Voreinstellung anwenden (1.18) / pro Laufwerk speichern und anwenden, 40 Bytes Antwort (4.4) |
| 19 | 1 | Macintosh-GCR-Fähigkeiten, 48 Bytes (5.4; INFO Bit29) |
| 20 | 1 | Discovery der lokalen Zeitfenster (Abschnitt 14 des Ergebnisses): Schema, 32 Fenster à 40 Byte, Gesamtbytes, Aktivierungsmaske, 24 Bytes (5.5) |
| 21 | 1 | Discovery des Erkennungsblocks (Abschnitt 15, 64 Bytes, Abruf über Op 3): 24 Bytes (5.6) |
| 22 | 1 | Fähigkeitsmatrix: Schema 2, Generation, Flags, neun Zeilen (Rohfluss, Formate 1–7, IEC), 320 Bytes; ältere Builds Schema 1 mit sechs Formatzeilen, 224 Bytes (1.3) |
| 23 | 1 / 5 | Konfigurationsbericht, mit `expected_generation` u32 meldet `PROFILE_CHANGED` bei Abweichung, 24 Bytes (1.3) |
| 24 | 5 + Op-1-Anfrage | Analysestart gebunden an die Konfigurationsgeneration (`expected_generation` u32), sonst wie Op 1 (1.3) |
| 25 | 1 | Discovery der medien­gebundenen Starts: Schema 2, GUARDED_WRITE 0x27, GUARDED_READ 0x28, aktuelle Generationen, 20 Bytes (4.5) |
| 26 | 9 + Op-1-Anfrage | Analysestart gebunden an Konfigurations- und Mediengeneration (je u32); anderes Medium → Status 21 (4.5) |
| 27 | 44–77 | Mehrspurplan anlegen: Plan-ID, Generationen, Spurbereich, Seiten, Op-1-Anfrage (6.2) |
| 28 | 1 | Mehrspurstatus, 32 Bytes; zugleich Discovery der Mehrspur-Analyse (6.2) |
| 29 | 8 | Mehrspursteuerung: Plan-ID u32, erwarteter Cursor u16, Aktion u8 (0 Schritt starten/wiederholen, 1 Ergebnis bestätigen und Cursor weiter, 2 Plan beenden) (6.2) |
| 30 | 1 | Checkpoint des dauerhaften Plans auf SD, 16 Bytes (6.3) |
| 31 | 1 / 5 | Rohspur-Timingprüfung lesen / setzen: Schema 1, Flags u8, Toleranz u16 in Promille (6.3) |
| 32 | 1 | Letzter Rohspurbericht, 288 Bytes oder NOT_FOUND (6.3) |
| 33 | 1 / 14 | Apple-II-GCR-Decoderoptionen lesen / setzen, 13 Bytes Antwort (5.7, Format 7) |
| 34 | 1 / 4 | Toleranzflags der Decoder für Format 1–6 lesen / setzen, 3 Bytes Antwort (5.8) |
| 35 | 1 | Apple-II-Nibble-Statistik der letzten Format-7-Analyse, 274 Bytes, Schema 2 (5.7) |
| 36 | 1 / 66 | Apple-II-6-and-2-Symboltabelle lesen / setzen, 65 Bytes Antwort (5.7) |
| 37 | 1 / 2 | Sitzungs-Schreibsperre abfragen / einschalten (`[37, 1]`), 2 Bytes Antwort; kein Ausschalten über USB (4.6) |
| 38 | 1 | Flagmasken, die die Anfrageparser annehmen, 16 Bytes (4.7); reine Abfrage, nie BUSY |
| 39 | 1 | Letzte Abweisung mit Status 16/21: Kommando, Sequenz, Status, Feld, Detail, 6 Bytes (4.8); reine Abfrage, nie BUSY |
| 40 | 1 | Prüflesebericht je Sektor des letzten geprüften WRITE: Kopf 6 Bytes + 5 Bytes je Referenzsektor, höchstens 326 (4.9); reine Abfrage, nie BUSY |
| 41 | 1 / 4 | Drehzahlprüfung vor dem Schreiben lesen / Toleranz setzen (`[41, 1, promille u16]`, 0 = aus), 11 Bytes Antwort mit letzter Messung (4.10) |
| 42 | 1 | Weak-Fenster des letzten ANALYZE 0x25: Schema, Umdrehungen, Spur, Seitenbyte, 64 × Differenz der Flusswechselzahl je 1/64 Umdrehung, 68 Bytes (4.11); reine Abfrage, nie BUSY |
| 43 | 1 | Provenienzblock: Board, MCU-ID, Firmware-Revision und Build, Generationen, Capture-ID, letzte Indexperiode, Laufwerkseinstellung, Laufzeit; 96 Bytes (4.12); reine Abfrage, nie BUSY |
| 44 | 1 / 2 | Weak-Positionen fein des letzten ANALYZE: Fenster je 1/1024 Umdrehung, deren Maximum − Minimum der Flusswechselzahl die Schwelle (Vorgabe 2, optional u8 1–255) erreicht; Kopf 10 Byte plus n × 4, höchstens 266 (4.13); reine Abfrage, nie BUSY |
| 45 | 1 / 2 / 3 + 3·n | Präkompensationsprofil (4.14): `[45]` liest, `[45, 0]` löscht, `[45, 1, n, Zonen]` setzt 1–4 Zonen (ab physischer Spur u8, ns u16); Antwort 19 Bytes mit Quelle, Zonen und letzter angewandter Präkompensation; Setzen während eines Vorgangs BUSY |
| 46 | 1 / 2 / 6 | Spurübersicht (4.15): `[46]` liest Kopf 28 Byte + fertige Einträge à 20 Byte (nie BUSY, auch während des Laufs), `[46, 1, erste, letzte, seitenmaske, flags]` startet eine Umdrehung je Spur und Seite auf dem Board, `[46, 0]` bricht ab; höchstens 3388 Byte |
| 47 | 1 / 7 / 12 | Sektor-Befunde (4.18): Discovery oder generationgebundener Seitenabruf, optional mit erwartetem C/H/N und Sektorintervall; maximal288 Byte |
| 48 | 1 / 16; zusätzlich2 / 17 | Vollständiges Archiv / ausdrücklicher Abruf unvollständiger Hardware-Verlustfenster, höchstens552 Byte (4.19) |
| 49 | 1 / 7 | Inhaltsvarianten der Ergebnisgeneration, höchstens288 Byte (4.20) |
| 50 | 1 / 12 | Präzises Roh-Prüflesen lesen / konfigurieren,24 Byte Antwort (4.22) |
| 51 | 1 / 7 | Statistik der Zeitabweichungen je Fenster, höchstens288 Byte (4.21) |
| 52 | 17 / 22 / 46 / 50 | Gespeicherte Aufnahme erneut analysieren;64 Byte Jobstatus (4.19) |
| 54 | 1 / 7 | Präziser Roh-Prüflesebericht, höchstens128 Byte pro Seite (4.22) |
| 55 | 1 / 8 | Kopierschutz-Hinweisanalyse konfigurieren: sechs Flags und Schwellen,24 Byte Antwort (4.23) |
| 56 | 1 / 7 | Generationgebundener Kopierschutz-Hinweisbericht mit Regel-IDs und Einschränkungen, höchstens288 Byte (4.23) |
| 14 | 1 / 9 | Laufwerksprofil lesen / setzen, Antwort 40-Byte-Status |
| 15 | 1 / 13 | Medienprofil lesen / deklarieren, Antwort 40-Byte-Status |
| 16 | 1 | Medienwechsel manuell melden, Antwort 40-Byte-Status |

Nur ein Analysejob gleichzeitig (die Spurübersicht Op 46 zählt dabei als laufender
Vorgang); ein neuer angenommener Job ersetzt den alten
Job und seine Historie. Andere aktive Laufwerksoperationen und offene
Integritätsjobs verhindern den Start mit BUSY. Status und Discovery bleiben
während der Analyse abfragbar. QUALITY 0x0E und Ergebnis-Seiten liefern dann BUSY,
damit das Tool keinen noch wachsenden Bericht als fertiges Ergebnis übernimmt.
Das allgemeine ABORT und ein Protokollreset beenden auch den Analysejob.

Die Dekodierung verarbeitet höchstens 256 Fluxintervalle pro Main-Loop-Aufruf;
zusätzlich prüft sie alle 32 Intervalle ein 2-ms-Zielbudget. Das ist keine harte
Echtzeitgarantie. Aufnahme und Dekodierung laufen als Zustandsmaschine. Seek und
Motorvorbereitung verwenden weiterhin den vorhandenen synchronen Treiber.
Keine zusätzlichen Vollspurkopien, keine Heap-Allokation; Aufnahme bleibt in U11
oder im vorhandenen internen Fallback. Ein zu kleiner Puffer ergibt einen Fehler.

#### Op 0: Fähigkeiten, Schema 1

Reservierte Bytes sind 0. Format-/Ratenlisten beschreiben implementierte Decoder
und automatische Suchkandidaten, keine durch Hardwaretests bestätigten Formate.

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Anzahl Formate: 5 |
| 2 | u8 | Unterstützte ANALYZE-Flags: 0xFF |
| 3 | u8 | Diagnoseflagmaske: 7 |
| 4 | u32 | Unterstützte Jobflags: 127 (0x007F) ab 1.18-dev mit RESEEK; 63 (0x003F) davor, CHECK_PROFILE ab 2026-10-16 |
| 8 | u32 | Maximales Gesamtbudget: 60000 ms |
| 12 | u32 | Maximales Analysebudget pro Versuch: 10000 ms |
| 16 | u32 | Maximale Seitennutzdaten: 1024 Bytes |
| 20–23 | je u8 | Max. Versuche 8, Umdrehungen 8, Fundstellen 48, Fensterintervalle 4 |
| 24–26 | je u8 | Basis-, QX-, QD-Schema: jeweils 1 |
| 28 | u32 | Maximale Qualitätsberichtgröße: 4048 Bytes |
| 32 | u32 | Flux-Takt: 275000000 Hz |
| 40 | 6 × u32 | Ratenindices 0–5: 250000, 300000, 500000, 1000000, 125000, 150000 |
| 64 | 5 × 12 Bytes | Formateinträge, nachstehend beschrieben |
| 128 | 3 × u32 | Ratenindices 6–8: 266667, 285714, 307692 |
| 140 | u32 | Selbsttestmaske: 0x1FFF |
| 144 | u32 | Selbsttests mit zusätzlicher Freigabe: 0x0604 |
| 148, 152 | je u32 | PLL-Margin-Grenzen: 1, 499 Promille |
| 156 | u32 | Maximale Syncmusterlänge: 32 Bits |
| 160 | u32 | Suchkandidaten: 18 |
| 164 | u32 | Bekannte native WRITE-Flagbits: 0xFF; Kombinationen sind weiterhin eingeschränkt |
| 168, 172 | je u32 | Explizite Rohzellperiode: 100–100000 Ticks |
| 176, 180 | je u32 | Historieneintrag 40 Bytes, Seitenheader 24 Bytes |
| 184–187 | je u8 | Kontextschema 1, Transportlimit 48, Indexlimit 240, Zusatz-Timingschema 1 |
| 188 | u32 | Diagnoseaufnahmeflagmaske 0x0018 |

Formateintrag: `id u8, analyse u8, extended_write_verify u8, reserved u8,
max_sector_bytes u32, auto_rate_mask u32`. IDs 1–5 unterstützen erweitertes
Prüfschreiben (GCR ab INFO Bit26). Masken beziehen sich auf
die neun Ratenindices. Maximaler Sektorinhalt: IBM-MFM/FM 1024, Amiga 512,
HP-MMFM/GCR 256 Bytes; Labels und Disk-ID können zusätzlich in Inhaltsvergleiche
einfließen. Detail-Parametergrenzen aus Abschnitt 5.2 gelten unverändert.

#### Op 1: Start und Jobflags

| Offset | Typ | Bedeutung |
|---:|---|---|
| 0 | u8 | Op 1 |
| 1 | u32 | Vom Tool gewählte Job-ID, ungleich 0 |
| 5 | u16 | Jobflags, unbekannte Bits werden abgewiesen |
| 7 | u8 | Maximale Aufnahmeversuche 1–8, einschließlich Erstaufnahme |
| 8 | u32 | Gesamtbudget 1–60000 ms, einschließlich aller Aufnahmen/Auswertungen |
| 12 | u64 | Erwartete Sektormaske: Bit R = Sektor-ID R, R 0–63 |
| 20–22 | je u8 | Erwartete C/H/N-Werte, N 0–3 |
| 23 | u8 | Reserviert, muss 0 sein |
| 24 | variabel | Vollständiger ANALYZE-Request mit DETAIL: 4, 9, 33 oder 37 Bytes |

| Jobflag | Wert | Wirkung |
|---|---:|---|
| RETRY_MISSING | 0x0001 | Dieselbe Spur erneut aufnehmen, wenn erwartete Sektoren fehlen; benötigt EXPECT_SET |
| RETRY_ERRORS | 0x0002 | Bei Inhaltskonflikten, Header-/Datenprüfsummen- oder erkannten Kodierungsfehlern erneut aufnehmen |
| EXPECT_SET | 0x0004 | Nichtleere Sektormaske verlangen; fehlende erwartete Sektoren ergeben am Ende Status 6 |
| RAW_INDEX | 0x0008 | Beobachtete Index-/Sektorlochereignisse einschließlich verworfener Impulse aufzeichnen (4.1) |
| TRANSFER_TRACE | 0x0010 | Begrenzte Aufnahme-/Transporthistorie aufzeichnen (4.1) |
| CHECK_PROFILE | 0x0020 | Profil, wirksamen Schrittfaktor und Aufnahmebefunde vergleichen; Ergebnis in Abschnitt 11 (4.3) |
| RESEEK | 0x0040 | 1.18-dev: vor jeder Wiederholungsaufnahme (ab Versuch 2) einen Schritt nach außen (Spur − 1, auf Spur 0 nach innen, gleiches Viertel) und mit dem normalen Seek der Aufnahme zurück, damit jeder Versuch von einer neu eingeschwungenen Kopflage liest statt dieselbe Lage erneut abzutasten (Fusion unabhängiger Lesungen, OTDR); der erste Versuch bleibt unverändert; scheitert der Rückschritt, endet der Auftrag wie ein gescheiterter Aufnahmestart (Status 3, Ursache `position` im Versuch) |

Ohne EXPECT_SET muss die Maske 0 sein. C/H/N wählen die erwarteten Kennungen,
nicht die mechanische Zielposition; diese steht im angehängten ANALYZE-Request.
Beispiel Amiga DD: `C=Spur,H=Seite,N=2,Maske=0xFFE` für R=1–11.
IBM mit neun Sektoren R=1–9: `N=2,Maske=0x3FE`.
GCR Spur 18 mit 19 Sektoren R=0–18: `C=18,H=0,N=1,Maske=0x7FFFF`.

Ein Sektor zählt, wenn mindestens ein gültiger Fund vorliegt und weder
Inhaltskonflikt noch Defekt- oder Kodierungsflag gesetzt ist. Zusätzliche
Sektoren verhindern einen Analyseerfolg nicht. Wiederholungen sind vollständige
Aufnahmen derselben Spur; keine automatischen Schreib-, Reinigungs- oder
Nachbarspurversuche. Aufnahmefehler, Abbruch und Timeout beenden den Job sofort.
Maximal 4 s Aufnahme und das gewählte Detailbudget gelten zusätzlich pro Versuch.
Bei RETRY_ERRORS ohne EXPECT_SET kann ein abgearbeiteter Job erfolgreich enden,
obwohl Befunde verbleiben: Joberfolg bedeutet dann erfolgreich ausgeführte
Untersuchung, nicht fehlerfreie Diskette. Das Tool muss den Bericht auswerten.

Die Firmware führt Fundmengen verschiedener Versuche **nicht** zu einem künstlichen
Vollbild zusammen. Qualitätsbericht, Fluxpuffer und `seen` gehören zum letzten
Versuch; die Historie hält Zusammenfassungen aller bis zu acht Versuche.

#### Op 2/4 und Startantwort: 64-Byte-Status

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Zustand: 0 unbenutzt, 1 Aufnahme, 2 Analyse, 3 fertig, 4 fehlgeschlagen/abgebrochen |
| 2 | u8 | Positive UFI-Fehlernummer; während aktiver Zustände noch kein Endergebnis |
| 3 | u8 | Begonnene Versuche |
| 4 | u32 | Job-ID |
| 8 | u32 | Verstrichene ms, nach Abschluss eingefroren |
| 12 | u32 | Berichtgeneration des aktuellen/letzten begonnenen Versuchs; 0, solange dieser keinen Bericht hat |
| 16, 24, 32 | je u64 | Erwartet, gefunden, fehlend (`expected & ~seen`) |
| 40 | u8 | Analysephase: 1 Erkennung, 2 Detailauswertung, 3 Umdrehungsvergleich; nur im Zustand 2 gültig |
| 41 | u8 | Suchkandidat 0–17 oder PLL-Profil 0–2 |
| 42 | u8 | Aktuelle Umdrehung, 0-basiert |
| 43 | u8 | 1 Speicheranalyse über Op52,0 neue Aufnahme |
| 44 | u32 | Verarbeitete Flanken der aktuellen Umdrehung |
| 48, 49 | je u8 | Maximale Versuche, aktuelle Jobflags (derzeit fünf Bits) |
| 50–52 | je u8 | Erwartete C/H/N |
| 53 | u8 | Abschlussgrund: 0 normal/sonstiger Fehler, 1 Inhaltsbestätigung erreicht, 2 kein Fortschritt, 3 Versuchslimit bei weiter bestehendem Wiederholungsbedarf |
| 54, 55 | je u8 | Aufeinanderfolgende Inhaltsbestätigungen, Versuche ohne Verbesserung (5.3) |
| 56 | u32 | Gesamtbudget ms |
| 60 | u32 | Capture-ID des letzten Versuchs; 0, wenn keine Aufnahme begonnen hat |

Fremde Job-IDs werden abgewiesen. Wiederholtes Op 4 nach Abschluss verändert
den Endzustand nicht. Ein Transport-Reply mit Status 0 bestätigt den Statusabruf,
nicht automatisch den Erfolg des Jobs; dafür Zustand und Byte 2 auswerten.

Beim Beginn eines Wiederholungsversuchs werden Generation, Fundmenge und
Capture-ID zurückgesetzt. Scheitert bereits Seek oder Aufnahmestart, bleibt
der Fehler in dessen Historieneintrag erhalten; Befunde des vorherigen Versuchs
stehen ausschließlich in dessen Historie und werden nicht als aktueller Erfolg
ausgegeben. Ohne begonnene Aufnahme bleibt die Capture-ID 0.

#### Op 3: Ergebnis-Seiten mit Generation und CRC32

Request: `op u8=3, job_id u32, generation u32, section u8, offset u32, count u16`.
`count` ist 1–1024. Generation aus dem abgeschlossenen Jobstatus übernehmen.
Offset größer als Abschnittslänge ergibt RANGE; Offset gleich Länge eine leere
Seite. Die letzte Seite darf kürzer ausfallen. Keine Seiten während aktiver Jobs.

Antwortheader: `schema u8=1, section u8, reserved u16=0, job_id u32,
generation u32, offset u32, total_size u32, payload_crc32 u32`; danach die
Seitennutzdaten. CRC32 ist IEEE wie `ufi_crc32(0, payload, size)`, zusätzlich zur
CRC des USB-Rahmens. Das Tool prüft Job, Generation, Abschnitt, Offset, Gesamtlänge
und CRC bei jeder Seite. Seiten können wiederholt oder in anderer Reihenfolge
abgerufen werden. Ein neuer Qualitätslauf invalidiert alte Abschnitte 0/3 mit
BUSY; es wird kein fremder Bericht unter alter Generation geliefert.

| Abschnitt | Inhalt |
|---:|---|
| 0 | Qualitätsbericht ohne 40-Byte-ANALYZE-Präfix: Basis, optional QX/QD |
| 1 | Versuchshistorie, `begonnene_Versuche * 40` Bytes |
| 2 | Derselbe 64-Byte-Jobstatus |
| 3 | 18 × u16 Suchbewertungen: MFM vier Raten, Amiga vier, FM fünf, HP eine, GCR vier |
| 4 | Aufnahme-Kontext, 160 Bytes (4.1) |
| 5 | Rohindexbericht, höchstens 3872 Bytes (4.1) |
| 6 | Transporthistorie, höchstens 1184 Bytes (4.1) |
| 7 | Umdrehungsvergleich, 1568 Bytes (5.3) |
| 8 | Beim Jobstart übernommene Vergleichs-/Wiederholungskonfiguration, 11 Bytes (5.3) |
| 9 | Laufwerks-/Medienstatus beim Jobstart, 40 Bytes (4.2) |
| 10 | Laufwerks-/Medienstatus beim Start der letzten Aufnahme, 40 Bytes (4.2) |
| 11 | Optionaler Profilprüfbericht der letzten erfolgreichen Analyse, 32 Bytes (4.3) |
| 12 | Portbericht vor dem letzten Aufnahmeversuch, 24 Bytes (4.4) |
| 13 | Befundzusammenfassung und Macintosh-Zonensektoren, 48 Bytes (5.4) |

Kandidatenreihenfolge: MFM/Amiga jeweils 250/300/500/1000 kbit/s; FM
125/150/250/300/500; HP 500; GCR 250000/266667/285714/307692 bit/s.
Bei explizitem Format und Rate findet keine Suche statt; Scores bleiben 0.

Historieneintrag: +0 Format u8, +1 Umdrehungen u8, +2 Sektoranzahl u8,
+3 positive Fehlernummer u8, +4 Versuchsdauer u32, +8 Generation u32,
+12 gefundene CHN/R-Maske u64, +20 Inhaltskonflikte u32,
+24 Headerfehler u32, +28 Datenfehler u32, +32 Kodierungsfehler u32,
+36 Summe verworfener Sektor-/Fundstellenberichte u32. Fehlerzähler stammen aus
dem Standardprofil. Bei Fehler vor der Auswertung bleiben Decoderfelder 0.

#### Commodore-GCR direkt im Fluxdecoder

`format=5` liest 1541-/1571-Sektorheader und 256-Byte-Datenblöcke, prüft die
GCR-Symbole sowie beide XOR-Prüfsummen und erkennt Syncfolgen ab zehn Einsen.
Die vier Raten sind **Kanalbitraten**. Die Disk-ID aus dem Header fließt in den
CRC32-Befund ein: 256 Datenbytes, dann die zwei ID-Bytes in Headerreihenfolge.
Unterschiedliche gültige Inhalte oder Disk-IDs erzeugen einen Konflikt.

CHRN: C ist die unveränderte On-Disk-Spurnummer (z.B. 18, bei 1571 auch 36–70),
H=0, R ist die Sektornummer ab 0, N=1. Der Header enthält keine eigene Kopfnummer;
mechanische Spur/Seite stehen separat im Bericht. Das Tool übernimmt die Abbildung
auf D64/D71. Die 1581 verwendet IBM-MFM, keine GCR-Spuren. Das ist keine Apple-GCR-
oder beliebige Kopierschutzdekodierung. GCR-Prüfschreiben ist noch nicht als
Inhaltsprüfung freigegeben; Discovery meldet dafür `extended_write_verify=0`.

#### WRITE-Bit7: EXPECT_SET, und Op 6: Prüfabdeckung

**WRITE `0x80`** ist nur mit VERIFY und Extended zulässig: `0xA1`, beziehungsweise
`0xE1` mit SOURCE_INDEX. Nach den bisherigen optionalen Feldern folgen exakt
12 Bytes: `expected_R_mask u64, C u8, H u8, N u8, reserved u8=0`.
Maske muss ungleich 0 sein, N 0–3. Die Firmware verlangt vor WGATE eine
konfliktfreie Referenz mit **genau** diesen CHRN-Sektoren. Fehlende, zusätzliche
oder abweichende Kennungen werden mit BAD_ARGS abgewiesen. Damit lässt sich
verhindern, dass eine unvollständig dekodierte Quelle nur gegen sich selbst
geprüft und als vollständige Formatspur bezeichnet wird.

Op 6 liefert das Referenzinventar nach Ende des Schreibvorgangs; während einer
aktiven Laufwerksoperation BUSY. Header 32 Bytes, danach `count * 8` Bytes:

| Offset | Typ | Inhalt |
|---:|---|---|
| 0 | u8 | Schema 1 |
| 1 | u8 | Letzter WRITE-Prüfgrad, gleiche Bedeutung wie WRITE-END |
| 2 | u8 | Bezugsmenge: 0 keine Referenz, 1 dekodierte Quelle, 2 explizite Erwartungsmenge |
| 3 | u8 | Anzahl Referenzsektoren, max. 64 |
| 4 | u8 | Referenzformat, 0 ohne erkannte Referenz |
| 5, 6 | je u8 | Referenzüberlauf, widersprüchliche Quelle |
| 8 | u64 | Angeforderte Sektormaske, sonst 0 |
| 16–18 | je u8 | Angeforderte C/H/N |
| 32 | count × 8 Bytes | `C,H,R,N` je u8, `Inhalts-CRC32 u32` |

Die Liste ist ein Referenzinventar, kein zusätzlicher Erfolgscode. Erst
WRITE-END-Status 0 und Grad 3 bestätigen den Inhaltsvergleich. Grad 3 mit
Bezugsmenge 1 bestätigt nur die dekodierte Quelle; mit Bezugsmenge 2 die zuvor
deklarierte Menge. Rohflux, absichtlich ungültige Sektoren und unbekannte
Schutzbereiche sind damit nicht automatisch geprüft. Der nächste angenommene
WRITE verwirft das Inventar; daher unmittelbar nach WRITE-END abholen.

#### Op 5: gezielte Bring-up-Diagnostik

Request: `op u8=5, schema u8=1, selected_tests u16, flags u8`.
Maskenbit `id-1` wählt einen bisherigen SELFTEST-Eintrag; zulässig 0x0001–0x1FFF
als nichtleere Teilmenge. Antwort `schema u8=1, count u8`, danach ausschließlich
gewählte Einträge `id u8, status u8, value u32` in aufsteigender ID-Reihenfolge.
Statuswerte entsprechen SELFTEST. Kein automatisches SELFTEST.LOG.

| IDs | Tests | Zusätzliche Freigabe |
|---|---|---|
| 1, 2 | Timer, USB-SOF | nein |
| 3 | U11/PSRAM-Schreibtest | `flags & 1` |
| 4 | SD-Präsenz und Kapazität, lesend | nein |
| 5, 6, 7, 8, 9 | Versorgung, Board-ID, USB-CC, Floppy-Eingänge | nein |
| 10, 11 | IEC-Loopback, Index-Loopback mit ausgegebenem Signal | `flags & 1` |
| 12, 13 | Letzter Reset, Stackreserve | nein |

Unbekannte Flags oder intrusive Testauswahl ohne Bit0 ergeben BAD_ARGS. Während
aktiver Operationen oder offener Integritätsjobs BUSY. Diese Auswahl ist eine
konkrete Protokollfreigabe des Tools: PSRAM-Test überschreibt seinen Testbereich,
Loopbacks treiben Pins. Der SD-Eintrag fragt nur Präsenz und Kapazität ab. Tests laufen synchron mit
ihren bisherigen Zeitgrenzen. Diskettenschreibtests gehören weiterhin zu den
separaten expliziten WRITE/PATTERN-Befehlen. Für USB-Durchsatz stehen PING-Echo
und DIAGNOSTICS-Zähler bereit; SOF prüft den Zeitbezug, nicht den Nutzdurchsatz.

Offline wurden Decoder, Jobzustände, Seitenintegrität und ein externer
IBM-MFM-Aufnahmesatz geprüft. Diese Softwaretests ersetzen keine elektrische
Inbetriebnahme, U11-Bandbreitenmessung oder Schreib-/Prüflesetests am fertigen Board.

### 6.2 Mehrspur-Analyse mit bestätigter Fortsetzung (0x0F, Op27–29)

Keine neuen Flagbits und keine Änderung des Greaseweazle-Protokolls. Op22 kündigt
an Byte25/26/27 die Operationen 27/28/29 an. Nur Lese-/Analyseaufträge;
Schreibaufträge werden nicht automatisch wiederholt.

Op27: `27:u8, plan_id:u32, config_generation:u32, media_generation:u32,
first_track:u8, last_track:u8, sides:u8, vollständige_Op1_Vorlage`.
Alle Mehrbytewerte Little Endian. ID und Generationen ungleich0, Spuren0..83,
inklusive Grenzen, Seiten1 oder2. START-Vorlage maximal61 Bytes; deren normale
Parameterprüfungen gelten. C/H-Erwartung und Capture-C/H werden für jeden Schritt
auf die jeweilige Spur/Seite gesetzt. Unterschiedliche ID-Abbildungen benötigen
weiterhin einzelne Analyseaufträge. Reihenfolge: Zylinder, dann Seite.
Anlegen bewegt kein Laufwerk und startet noch keine Erfassung.

Op28: `[28]` liefert32 Bytes und ist zugleich die Discovery für diesen Bericht:

| Offset | Typ | Bedeutung |
|---|---|---|
| 0 | u8 | Schema1 |
| 1 | u8 | 0 leer, 1 bereit, 2 läuft, 3 Ergebnis wartet, 4 beendet, 5 fehlgeschlagen |
| 2 | u8 | Fehlerstatus; bei Abbruch CANCELLED |
| 3 | u8 | Steueroperation29 |
| 4,8,12,16 | je u32 | Plan-ID, Konfiguration, Medium, aktueller Einzeljob |
| 20,22 | je u16 | bestätigte Spuranzahl/Cursor, Gesamtanzahl |
| 24,25,26 | je u8 | erste/letzte Spur, Seitenanzahl |
| 27,28 | je u8 | aktuelle Spur/Seite, bei Ende0 |
| 29–31 | Bytes | reserviert0 |

Op29: `29:u8, plan_id:u32, expected_cursor:u16, action:u8`.
Aktion0 startet den aktuellen Schritt oder wiederholt einen fehlgeschlagenen;
Aktion1 bestätigt ein erfolgreiches, zuvor vom Tool gesichertes Ergebnis und
setzt den Cursor genau einmal weiter; Aktion2 beendet den Plan. Der nächste
Schritt startet erst durch eine weitere Aktion0. Veraltete Cursor/IDs: BAD_ARGS;
laufender Schritt oder Bestätigung ohne Ergebnis: BUSY. Vor jedem Start werden
Konfigurations- und Mediengeneration erneut geprüft. Die normalen Einzeljob-
Abbrüche und Medienprüfungen gelten auch innerhalb jeder Erfassung.

Ergebnisse werden mit den bestehenden Op2/Op3 über die Einzeljob-ID gelesen.
Während des Plans sind fremde Analyse-Starts und Änderungen der Analyseoptionen
gesperrt. Andere Schnittstellen können zwischen Schritten weiterhin Einstellungen
oder Ergebnisse ändern: Generationen prüfen und Bericht vor ACK sichern. Es gibt
keine Reservierung des Laufwerks über eine längere Hostpause hinweg.

Der Cursor bleibt bei Kommunikationspause und Einzeljob-Abbruch im RAM erhalten.
Dies ist **keine dauerhafte Wiederaufnahme nach Reset/Stromausfall**. Nach einem
Neustart sind Plan und Generationen ungültig; das Tool muss Medium und Profile neu
zuordnen und einen neuen Plan anlegen. Ein gespeicherter Host-Checkpoint allein
beweist weder dieselbe Sitzung noch dieselbe Diskette. Op28 nach Verbindungsabbruch
abfragen; insbesondere eine verlorene ACK-Antwort nicht blind wiederholen.

### 6.3 Dauerhafte Analysepläne, Rohspurprüfung und SD-Abschluss

Die Erweiterungen betreffen ausschließlich das native Protokoll. Keine zusätzlichen
Greaseweazle-Flags. Op22-Headerbytes28/29/30 kündigen Op30/31/32 an. Alte Op27–29
bleiben im bisherigen Wireformat; ohne ausdrückliche Aktivierung bleibt ein Plan
nur im RAM. Die Reset-Grenze aus6.2 gilt für solche nicht gespeicherten Pläne.

**Op30 — Checkpoint auf SD:** `[30]` liefert16 Bytes: Schema1 an0,
unterstützte Optionsmaske3 an1, Recordgröße128 an2, Anzahl Slots2 an3,
aktuelle Sequenz `u32` an4, Persistenz aktiv `u8` an8, Rest0. Diese Abfrage
liest und verändert die SD nicht.

Setzen/Laden: `30:u8, action:u8, flags:u16, current_config:u32,
current_media:u32, media_id[16]`, insgesamt28 Bytes, Little Endian.

| Flag | Wert | Bedeutung |
|---|---|---|
| PERSIST_CHECKPOINT | 0x0001 | Bestätigten Fortschritt auf SD speichern |
| MEDIA_RECONFIRMED | 0x0002 | Tool hat die physische Medienzuordnung nach Neustart erneut bestätigt |

Action1 verlangt Flags1 und einen ruhenden, vorhandenen Plan. Es speichert den
aktuellen Cursor und aktiviert das Speichern bei jedem folgenden Op29-ACK.
Action2 verlangt Flags3 und keinen lebenden Plan: neuesten gültigen Prüfstand
laden, aber **keine Erfassung starten**. `media_id` ist eine vom Tool verwaltete,
nichtleere16-Byte-Kennung; Firmware kann daraus keine physische Diskettenidentität
beweisen. Tool muss Medium tatsächlich neu zuordnen, nicht bloß das Flag setzen.

Beide Aktionen verlangen aktuelle Konfigurations-/Medientoken. Beim Laden müssen
Medienkennung, Laufwerksauswahl, Betriebsparameter, deklarierte Medien-/Laufwerks-
profile und Recovery-Einstellungen zum gespeicherten Stand passen. Ebenso werden
Decoder-Toleranzen (Op34), Apple-Marken/Optionen (Op33), die Apple-Symboltabelle
(Op36), Evidenzflags, Zusatzzeiten und Kopierschutzoptionen (Op55) verglichen. Dafür speichert
die Firmware CRC32-Fingerprints; diese sind keine Authentifizierung. Das Tool stellt
Profile vor dem Laden wieder her und liest danach die neuen Generationen.
Erfolg liefert den unveränderten32-Byte-Op28-Status. Falsche Parameter/Profile:
BAD_ARGS; andere Medienkennung: MEDIA_CHANGED; SD-/CRC-Fehler: STORAGE.

Dateien `UFIPLAN0.CKP` und `UFIPLAN1.CKP` sind reservierte128-Byte-Records mit
Magic UFP1, Dateischema2, Sequenz, Plan-ID, Cursor, START-Vorlage, Medienkennung,
Recovery-Einstellungen und CRC32. Der zusätzliche Einstellungsfingerabdruck liegt
in den bisher reservierten Bytes5–7 (untere24 Bits) und18 (obere8 Bits), ebenfalls
von der Record-CRC geschützt. Alte Dateischema1-Records besitzen diese Bindung nicht
und werden beim Fortsetzen mit BAD_ARGS abgewiesen; dafür einen neuen Plan anlegen.
USB-Anfrageformat, Antwortschema und Recordlänge bleiben unverändert. Während eines
offenen Plans sind auch Änderungen über Op33/34/36 BUSY. Schreiben abwechselnd, schließen und zurücklesen.
Ein zerrissener neuester Record fällt auf den älteren gültigen zurück; dabei kann
eine bereits gelesene Spur erneut anstehen. Ergebnisse bleiben im Tool, nicht im
Checkpoint. Vor ACK muss das Tool den Bericht sichern. Schlägt das Speichern fehl,
bleibt der RAM-Cursor unverändert. ACK erst nach erfolgreichem Persistieren.
FAT-Metadatenkorruption kann beide Slots verlieren; keine Zusage atomarer Hardware-
Stromausfallsicherheit. Abbrechen löscht den gespeicherten Prüfstand nicht; erneutes
Laden erfordert wieder Action2/Flags3. Sequenzüberlauf wird abgewiesen.

**Schreibschutz im Hartaudit ergänzt (1.18-dev):** ERASE und PATTERN verlangen
einen laufenden Motor und freie Capture-/Schreib-/Streamingpfade. Fehler bei SEEK
und Seitenwahl werden vor dem Schreibtor zurückgegeben. Während Wartezeit und
Schreibdauer prüfen sie Abbruch, Schreibsperren, Schreibschutz und Medientoken und
bedienen den Watchdog; jeder Ausgang nach Schreibbeginn schließt WGATE beziehungsweise
Apple-WRREQ und stoppt den Timer. PATTERN begrenzt die Dauer auf1–5000 ms und setzt
seinen Timer unabhängig von einer vorherigen erweiterten Waveform zurück.
Native WRITE prüft Abbruch und Sperren nochmals im tatsächlichen Index-/Offset-Start;
CANCELLED wird nicht als TIMEOUT ausgegeben. Das Prüflesen bestätigt Erfolg erst
nach einer abschließenden Abbruch-/Medienprüfung. Ein Op40-Bericht vor dem ersten
Prüfauftrag besteht deterministisch aus sechs Bytes (`01 00 00 00 00 00`).

**Op31 — Rohspur-Timingprüfung:** Abfrage `[31]`, Setzen
`[31, schema=1, flags:u8, tolerance_permille:u16]`.

| Flag | Wert | Bedeutung |
|---|---|---|
| RAW_TIMING_PROFILE | 0x01 | Zusätzliches lokales Zeit-/Dichteprofil beim WRITE-Prüflesen berechnen |
| REQUIRE_TIMING_MATCH | 0x02 | Abweichendes oder nicht verfügbares Profil lässt den Schreibauftrag fehlschlagen; verlangt0x01 |

Standardflags0, Toleranz30 (3%). Zulässig1..1000. Unbekannte Bits/inkonsistente
Kombinationen: BAD_ARGS. Setzen nur im Leerlauf ohne lebenden Analyseplan und mit
nativem Ressourcenbesitz; Konfigurationsgeneration wird geändert. Antwort8 Bytes:
Schema1, aktive Flags, Toleranz `u16`, unterstützte Maske3, Report-Op32,
Fensteranzahl64, reserviert0.

Wirkt bei Schreibaufträgen mit aktiviertem Firmware-Prüflesen, einschließlich
geprüfter SD-Aufträge/Kopien. Quelle muss eine gültige Indexdauer haben; natives
WRITE kann diese über das bestehende Source-Index-Feld übermitteln. Im Pflichtmodus
führt eine unbrauchbare Quelle bereits **vor WGATE** zu BAD_ARGS. Ein unpassendes
Rücklesen führt zu DMA-Fehler; Diagnosemodus liefert den Befund ohne dieses Veto.
Ein positives Profil hebt Verify-Level2 niemals zu Inhalts-Level3 an. Seit 1.18-dev fordert
WRITE `flags2` Bit4 (4.17) den Fingerabdruck auch ohne Prüflesen an; Flag 0x01 ist dafür
nicht nötig, Flag 0x02 und die Toleranz gelten auch dort.

Algorithmus:64 indexbezogene Winkelbereiche, jeweils8 logarithmische Klassen der
auf die Umdrehung normierten Übergangsintervalle. Klassenobergrenzen in Einheiten
`delta * 1048576 / revolution_ticks`:6,12,24,48,96,192,384; Klasse7 darüber.
Vergleichssumme D ist die absolute Bin-Differenz über alle Bereiche.
Passkriterium: `D*1000 <= tolerance*(source_edges+readback_edges)`.
Ein langsameres/schnelleres, sonst gleiches Laufwerk wird damit normalisiert.
Das Profil ist verlustbehaftet: gleiches Profil kann unterschiedliche Spuren haben.
Indexverschiebung, Grenzfälle und Jitter können umgekehrt Unterschiede verursachen;
kein automatischer Weak-Bit-Befund und keine Kopierschutzfreigabe.

**Op32 — letzter Rohspurbericht:** `[32]`, Antwort288 Bytes oder NOT_FOUND.

| Offset | Typ | Bedeutung |
|---|---|---|
| 0,1 | u8,u8 | Schema1,64 Fenster |
| 2 | u8 | 1 plausibel,2 abweichend,4 nicht verfügbar |
| 3 | u8 | 1: verlustbehaftetes Profil, kein Inhaltsbeweis |
| 4,8 | u32,u32 | Quell-/Rücklese-Umdrehung in275MHz-Ticks |
| 12,16 | u32,u32 | Quell-/Rücklese-Impulszahl |
| 20 | u32 | gesamte absolute Bin-Differenz D |
| 24 | u16 | Toleranz in Promille |
| 26–31 | Bytes | reserviert0 |
| 32 | 64×u32 | Differenz je Fenster |

Bericht wird beim nächsten vorbereiteten WRITE ungültig; er beschreibt die erste
vollständig zurückgelesene Umdrehung des letzten geprüften Schreibvorgangs.

**SD-Dateien:** Neue Aufnahmen beginnen als `DUMPnnnn.PRT`. Erst nach allen Daten,
Headern, erfolgreichem Dateischluss und Umbenennung erscheint die endgültige
.SCP/.HFE/.IMG/.ADF/.D64/.D71/.D81-Datei. Vorhandene Enddateien und .PRT-Dateien
werden übersprungen, nicht überschrieben. FAT ist weiterhin kein transaktionales
Dateisystem. Dies betrifft Standalone-Aufnahmen; das allgemeine WRITE_FILE-Protokoll
wurde nicht zu einem atomaren Uploadprotokoll geändert.

Ein fehlendes/beschädigtes FAT-Dateisystem wird nicht mehr automatisch formatiert.
Ersteinrichtung des SD-Speichers erfolgt ausdrücklich als FAT32 über USB-Massenspeicher.
So kann ein fehlgeschlagener Mount keinen Wiederaufnahme-Prüfstand still löschen.

**HFEv3-SD-Export:** DUMP_START mit Format4 oder `format=hfe3` in UFI.CFG.
Ausgabe: PLL-rekonstruierte Zellen der ersten Umdrehung, nominale explizite Bitrate,
eine Indexmarke und korrekt maskierte Opcode-Datenbytes. Keine erfundenen RAND-
Marker und keine Zusage unverändert erhaltener variabler Rohzeiten. Dafür SCP verwenden.
Je Seite maximal32767 codierte Bytes; der Export reserviert128 Blöcke je Zylinder
und füllt ungenutzte Bytes mit NOP. Dadurch sind beide Seiten unabhängig groß;
Dateien werden größer als kompakte HFEv1-Abbilder. Kapazitäts-/Zeitgrenzen führen
zum Fehler und lassen .PRT zurück. Klassisches HFEv1 bleibt Format3.

## 7 SD-Karte und Standalone-Betrieb

### 7.1 Standalone-SD-Schreiben und Prüflesen

HFEv3-SD-Dateien laufen über `hfe_v3_decode`: explizite Zellzeiten (36-MHz-Einheiten
auf275MHz gerundet), SKIPBITS und eine Indexmarke mit Rotation. Die Firmware liest
256-Byte-Seitenblöcke in den vorhandenen Stagingpuffer und schreibt direkt in den
vorhandenen Fluxspeicher/U11. Alle Dateispuren werden vor dem ersten Schreiben
auf Decoderfehler, Grenzen, RAND und Mehrfachindex geprüft. RAND/Weak und mehrere
Indexmarken bleiben als Schreibquelle abgewiesen. Unbekannte oder unvollständige
Inhaltsreferenzen können im Extended-Schreibpfad zusätzlich abgewiesen werden.
HFEv3-SD-Export ist zusätzlich über Format4 verfügbar (Details4.23); physische Weak-Reproduktion bleibt nicht qualifiziert.
HFEv1-Pufferüberlauf ist ein Fehler, kein Erfolg mit abgeschnittener Spur.

SD-Schreiben und direkte Kopie sichern die dekodierten Original-Sektor-IDs und
CRC32-Inhaltsfingerprints **vor** Timerpacking und Rücklesen. IMG/ADF verlangen den
vollständigen erwarteten CHRN-Satz; falsche Nutzdaten, fehlende Referenzsektoren oder
widersprüchliche Duplikate erzeugen keinen positiven Inhaltsbefund. Relevante
formatabhängige Labels/Tags werden vom bestehenden Verifikationsdecoder einbezogen.
Das ist ein CRC32-Inhaltsvergleich, kein kryptografischer Identitätsbeweis.

Die Logs unterscheiden `source content verified` (vollständiges Sektorimage),
`decoded source sectors verified` (nur die dekodierten Referenzsektoren einer
Rohspur) und `pulse-count only; content UNVERIFIED` (unbekannte Rohspur). Die letzte
Stufe prüft nur die Impulszahl; sie beweist weder Timingtreue noch Inhaltsgleichheit.
Nicht dekodierte Schutzbereiche einer Rohspur erhalten keine Inhaltsfreigabe.
Nicht darstellbare Intervalle werden beim geprüften lokalen Schreiben abgewiesen,
nicht still geklemmt. Abbruch und erkannter Medienwechsel stoppen den SD-Auftrag.

## 8 IEC (Commodore)

### 8.1 IEC-Erkennung und wiederaufnehmbare Schreibaufträge

`ufi iec-identify --device 8 [--fingerprint]` liest bekannte ROM-Signaturen und
optional einen SHA-256 des ROM-Bereichs. Signaturfakten folgen
[OpenCBM detect.c](https://github.com/OpenCBM/OpenCBM/blob/master/opencbm/lib/detect.c).
ROM-Familien 1541/C/II, 1571 und 1581 sowie einige Ersatz-ROMs werden unterschieden;
unbekannte und CMD-FD-Signaturen liefern ausdrücklich kein unterstütztes Modell.
ROM-Erkennung ist keine sichere Bestimmung der physischen Platinenrevision.
`iec-read --type auto` verwendet diese Erkennung; unbekannte ROMs verlangen Auswahl.

`iec-write IMAGE --checkpoint JOB.json --retries 2` speichert Quell-SHA256,
Geometrie, Geräteadresse und Ergebnisse je Sektor. Jeder erfolgreiche U2-Schreibversuch
muss per U1 bytegenau zurückgelesen werden. Ein unsicherer Versuch wird vor U2 als
pending gespeichert. Mit `--resume` werden zuerst alle bereits bestätigten Sektoren
gegen die eingelegte Diskette geprüft; erst danach folgen ausstehende Sektoren.
Veränderte Quelle, Geometrie oder bestätigte Medieninhalte brechen vor weiteren
Schreibzugriffen ab. Schreibschutz wird nicht wiederholt. Kein Medien-ID-Verfahren
kann hier einen Diskettenwechsel ohne bereits bestätigte Sektoren erkennen.
Ein IEC-Checkpoint ist ein Hostjob und kein flüchtiger Firmware-Fluxjob.

## 9 Nachweise und offene Qualifikation

### 9.1 Nachweise und offene Qualifikation

`python firmware/tests/check_all.py` umfasst produktive C-Module, extrahierte
Kontrollfunktionen mit Register-/Bus-Doubles, einen auf Commit
`26690f89967d519e0106ab9566019a026b920bb4` fixierten offiziellen GW-Codec,
Parser-Fuzzing, Deskriptorprüfung, 64-Bit-Überlauf, Host-Fehlerfälle und py65-Laufwerkscode.
Details und Builddaten: [Implementation_Status.md](Implementation_Status.md).
Es ist keine vollständige STM32- oder VICE-Laufwerksemulation. Unabhängige
Format-Korpora, umfangreichere Zustandsraumtests, Remote-Sanitizer-CI und echte
Enumeration/Flux-/IEC-Messungen bleiben weitere Arbeit. Es gibt noch keine
gemessene Zusage vollständiger Greaseweazle- oder Laufwerkskompatibilität.

## 10 Greaseweazle-Modus (`protocol=gw`)

Unabhängige Umsetzung des Greaseweazle-Protokolls (Keir Fraser, `cdc_acm_protocol.h`, public domain). Die Greaseweazle-Werkzeuge (`gw info`, `gw read`, `gw write`, `gw erase`, `gw seek`, `gw delays`, `gw reset`) erkennen das Gerät am Produktnamen „gw-compat“ auch ohne `--device`.

### 10.1 Rahmen
- Befehl: `[cmd, len, args…]`, `len` = Gesamtlänge inkl. der zwei Kopfbytes. Befehle dürfen beliebig über USB-Pakete verteilt sein; nicht pipelinen.
- Antwort: `[cmd, ack]` + befehlsabhängige Daten. `ack` 0 = OK.
- Kanal-Reset: SET_LINE_CODING mit 10000 Baud verwirft Teilbefehle und bricht einen laufenden Lesevorgang ab.

| ack | Bedeutung |
|---|---|
| 0 | OK |
| 1 | unbekannter/ungültiger Befehl |
| 2 | kein Indeximpuls |
| 3 | Spur 0 nicht gefunden |
| 4 | Flux-Überlauf (Lesen) |
| 5 | Flux-Unterlauf (Schreiben) |
| 6 | schreibgeschützt (Diskette oder WRITE-LOCK-Jumper) |
| 7 | kein Laufwerk angewählt |
| 8 | kein Bus-Typ gesetzt |
| 9 | ungültige Laufwerksnummer |
| 10 | ungültiger Pin |
| 11 | ungültiger Zylinder |
| 12 | Speicher voll |

### 10.2 Befehle

| Code | Name | Argumente | Antwortdaten |
|---|---|---|---|
| 0 | GET_INFO | `idx` u8 | 32 Byte (siehe unten) |
| 2 | SEEK | `cyl` s8 oder s16 | – (Zylinder 0…83, negativ = ungültig) |
| 3 | HEAD | `head` u8 (0/1) | – |
| 4 | SET_PARAMS | `idx`=0, bis 8 × u16 | – |
| 5 | GET_PARAMS | `idx`=0, `n` u8 | `n` Byte |
| 6 | MOTOR | `unit` u8, `on` u8 | – |
| 7 | READ_FLUX | `ticks` u32, `max_index` u16, [`linger` u32] | Flussdaten bis Byte 0 |
| 8 | WRITE_FLUX | `cue_at_index` u8, `terminate_at_index` u8, [`hard_sector_ticks` u32 = 0] | nach den Daten: 1 Statusbyte |
| 9 | GET_FLUX_STATUS | – | (Status steht im ack) |
| 12 | SELECT | `unit` u8 | – |
| 13 | DESELECT | – | – |
| 14 | SET_BUS_TYPE | 0 keiner, 1 IBM-PC, 2 Shugart | – |
| 15 | SET_PIN | `pin` u8 (nur 2 = DENSITY), `level` u8 | – |
| 16 | RESET | – | – (Einschaltzustand) |
| 17 | ERASE_FLUX | `ticks` u32 (Löschdauer, ab 1.15 ausgewertet: 1…5000 ms) | nach dem Löschen: 1 Statusbyte |
| 18 | SOURCE_BYTES (1.14) | `len` u32, `seed` u32 | `len` Bytes aus dem Generator von `gw bandwidth` (`r&255`, dann `r = r>>1 ^ 0x80000062` bei gesetztem Bit 0, sonst `r>>1`) |
| 19 | SINK_BYTES (1.14) | `len` u32, `seed` u32, danach `len` Datenbytes | nach den Daten 1 Statusbyte: 0 = alle Bytes passten zum Generator, 1 = verfälscht |
| 20 | GET_PIN | `pin` u8 (8 INDEX, 26 TRK0, 28 WRPROT, 34 DSKCHG/READY) | `level` u8 (0 = low = aktiv) |
| 22 | NOCLICK_STEP (1.14) | – | – (ein Schritt nach außen auf Zylinder 0 für Flippy-Umbauten; ack 11 auf anderen Zylindern) |

Nicht unterstützt (ack 1): 1 UPDATE, 10 GET_INDEX_TIMES (im aktuellen Host als
Kommandoname vorhanden), 11 SWITCH_FW_MODE, 21 TEST_MODE. Die vollständige
Greaseweazle-Verhaltenskompatibilität ist noch nicht nachgewiesen; siehe 4.1.

**GET_INFO idx 0:** `fw_major, fw_minor` (= UFI-Firmware, z. B. 1.14), `is_main=1`, `max_cmd=22`, `sample_freq` u32 = **68 750 000**, `hw_model=0x55`, `hw_submodel=1` (gw zeigt „Unknown (0x5501)“), `usb_speed=0` (Full Speed), `mcu_id=0`, `mcu_mhz` u16 = 550, `sram_kb` u16 = 564, `usb_buf_kb` u16 = 8, Rest 0. **idx 1:** Bandbreite: `min_bytes, min_us, max_bytes, max_us` (je u32) der letzten SOURCE/SINK-Übertragung (eine Messung, min = max; vor der ersten ein Nennwert von 8 KB je 8 ms). **idx 7 / 8+n:** Laufwerk aktuell / Einheit n: `flags` u32 (Bit0 Zylinder gültig, Bit1 Motor an), `cyl` s32.

**Bus/Einheiten:** IBM-PC: 0 = Laufwerk A, 1 = B (Kabel mit Twist). Shugart: 0–3 = DS0–DS3 (gerades Kabel, DS3 nur mit JP1). Amiga-Port, IEC und Apple-Port sind im Greaseweazle-Modus nicht erreichbar.

**Parameter (idx 0, je u16):** select_delay µs, step_delay µs, seek_settle ms, motor_delay ms, watchdog ms (Standard 10000), pre_write µs, post_write µs, index_mask µs. Seit 1.18 sind pre/post minimale Wartezeiten vor/nach WGATE, mit nativen Timer-Mikrosekunden gemessen; sie verändern keine Fluxintervalle. `index_mask` unterdrückt weitere Indexflanken innerhalb der Sperrzeit.

**Lesen (1.18):** beginnt sofort, zählt reale Indeximpulse und endet am früheren Zeitlimit oder nach `max_index` plus `linger` (Standard 500 µs). `ticks=0` deaktiviert das angeforderte Zeitlimit, `max_index=0` das Indexlimit. ticks/linger akzeptieren den vollständigen u32-Bereich; mehr als 200 Indeximpulse werden abgelehnt. Die Zeitbasis verarbeitet mehrere Timerumläufe, offline geprüft; Langzeitaufnahmen auf Hardware bleiben unqualifiziert.

**Schreiben (1.18):** Beide Cue/Terminate-Flags werden ausgewertet. Das letzte,
vom offiziellen Host angehängte Dummyintervall wird verworfen. Der erweiterte
TIM3/DMA-Pfad gibt Intervalle von 220 bis 4294967295 nativen Ticks aus
(GW: 55 bis 1073741823 Ticks). Lange Abstände bestehen aus stillen Timersegmenten
und genau einem 400-ns-Puls am Ende. Die erste fallende Flanke liegt beim ersten
Delta; die folgenden Flanken haben die gelieferten Abstände. Kurze ASTABLE-Perioden
von etwa 1,25 µs sind damit rechnerisch darstellbar. Vor WGATE werden Speicher-,
Intervall- und Gesamtdauergrenzen geprüft; kein stilles Kappen. Die Gesamtdauer
ist auf rund 58 s begrenzt, zusätzlich gelten Speicher und Indexabbruch.
Die physische Pulsform und DMA-Latenz sind noch nicht gemessen.

### 10.3 Flusscode (Abtastrate 68,75 MHz = 275 MHz / 4)
| Bytes | Bedeutung |
|---|---|
| `1…249` | Flusswechsel, Abstand = Wert |
| `250…254, b` | Abstand = 250 + (Wert − 250) × 255 + b − 1 |
| `0xFF 1 N28` | Indeximpuls, N = Ticks ab dem letzten Wechsel (Cursor unverändert) |
| `0xFF 2 N28` | Lücke: N Ticks zum nächsten Wechsel addieren |
| `0xFF 3 N28` | (nur Schreiben) Lücke vorher mit Wechseln im Abstand N füllen |
| `0` | Ende des Datenstroms |

N28 = 4 Byte: `1|(N<<1)`, `1|(N>>6)`, `1|(N>>13)`, `1|(N>>20)`.

Lesen liefert `max_index − 1` volle Umdrehungen ab einem Indeximpuls (der erste Impuls kommt als Index mit N = 0). Schreiben beginnt immer am Index und endet am nächsten; die Vorkompensation macht `gw` selbst.

---

## Anhang A Zuordnung der bisherigen Abschnittsnummern

| bisher | jetzt | Titel |
|---|---|---|
| 2.1 | 10.1 | Rahmen |
| 2.2 | 10.2 | Befehle |
| 2.3 | 10.3 | Flusscode (Abtastrate 68,75 MHz = 275 MHz / 4) |
| 3.1 | 2.1 | Rahmen (beide Richtungen) |
| 3.2 | 3.1 | Befehle |
| 3.3 | 3.2 | Statuscodes |
| 3.4 | 2.2 | Flusscode (READ-Daten, WRITE-Daten), 275 MHz |
| 3.5 | 2.3 | Ereignisse (type 3, nach EVENTS) |
| 3.6 | 3.3 | Festlegungen im Detail |
| 3.7 | 3.5 | Beispiel: Spur lesen |
| 4.1 | 1.2 | Composite-Transport und Geräteerkennung |
| 4.2 | 1.4 | Ressourcen und Feature-Abfrage |
| 4.3 | 2.6 | Integritätsjobs: JOB 0B |
| 4.4 | 2.5 | Abbruch, Zeitlimits und Wiederverbindung |
| 4.5 | 2.4 | Zeitbasis und erweiterter Schreibpfad |
| 4.6 | 8.1 | IEC-Erkennung und wiederaufnehmbare Schreibaufträge |
| 4.7 | 9.1 | Nachweise und offene Qualifikation |
| 4.8 | 5.1 | Laufzeitdiagnose und automatisches Host-Prüflesen |
| 4.9 | 5.2 | Firmware-Detailanalyse: acht Flag-Bits (1.18-dev) |
| 4.10 | 6.1 | Analysejobs, Discovery und Prüfabdeckung (INFO Bit24, 2026-10-14) |
| 4.11 | 4.1 | Aufnahmebelege und zusätzliche Laufwerkszeiten (INFO Bit25) |
| 4.12 | 5.3 | GCR-Prüflesen, Umdrehungsvergleich und Wiederholungsstrategie (INFO Bit26) |
| 4.13 | 4.2 | Positionsgültigkeit, Laufwerksprofile und Mediengeneration (INFO Bit27) |
| 4.14 | 4.3 | Optionale Profilprüfung (2026-10-16) |
| 4.15 | 4.4 | Portbelegung und gespeicherte Betriebsprofile (INFO Bit28, 2026-10-16) |
| 4.16 | 5.4 | Macintosh-GCR, Zonenprüfung und getrennte Abweichungsbefunde (INFO Bit29) |
| 4.17 | 5.5 | Lokale Zeitfenster, HFEv3 und getrennte Schwachstellenbefunde |
| 4.18 | 5.6 | Erkennungszustände, Positionsbefunde und Wiederholungsursachen |
| 4.19 | 1.3 | Fähigkeiten und Konfigurationsgeneration (GLaDISK-Anregungen) |
| 4.20 | 4.5 | Mediengebundene Starts und robuste Anfrageprüfung |
| 4.21 | 6.2 | Mehrspur-Analyse mit bestätigter Fortsetzung (0x0F, Op27–29) |
| 4.22 | 7.1 | Standalone-SD-Schreiben und Prüflesen |
| 4.23 | 6.3 | Dauerhafte Analysepläne, Rohspurprüfung und SD-Abschluss |
| (ohne Nummer) Ergänzungen ab Firmware 1.18-dev | 3.4 | IEC-Adressierung |
| 1 | 1.1 | USB-Personalitäten |
| 2 | 10 | Greaseweazle-Modus |
| 3 | 2 und 3 | UFI v2: Rahmen, Befehle, Festlegungen |
| 4 | 4 bis 9 | Erweiterungen ab 1.18-dev, nach Themen verteilt |

