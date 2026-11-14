# Projektstatus

**Sprache / Language:** [Deutsch](Projektstatus) | [English](Project-Status)

**Stand dieser Übersicht: 14. November 2026.** Aktueller Dokumentationsbezug ist UFI Headless v1.0 mit Firmware 1.18-dev. Änderungen im Entwicklungszweig können weiterlaufen; für einen Test immer die konkrete Revision festhalten.

## Was die Statusangaben bedeuten

| Begriff | Aussage |
|---|---|
| Implementiert | Code und Schnittstelle sind vorhanden |
| Offline geprüft | Der genannte Softwaretest wurde auf einem Host ausgeführt |
| Am Gerät geprüft | Der dokumentierte Ablauf lief auf einer bestimmten Platine |
| Offen | Der betreffende Nachweis steht noch aus |

Diese Angaben gelten pro Funktion und Aufbau. Sie sind keine pauschale Freigabe aller Gerätefunktionen.

## Hardware und Bring-up

| Bereich | Dokumentierter Stand |
|---|---|
| Fertigungsdaten v1.0 | Freigegeben, Tag `hw-v1.0`; ERC-/DRC- und Fertigungsnachweise im Bericht |
| USB-Bring-up einer v1.0-Platine | Am 11./12. November 2026 bestanden: 49 OK, 3 WARN, 0 FAIL; nur USB-C, PSRAM in Benutzung |
| Zweite Platine | Wiederholung mit dem endgültigen Firmwarestand offen |
| Lesen mit echtem Laufwerk | Offen |
| Schreiben und Rücklesen mit echtem Laufwerk | Offen |
| Laufwerksversorgung mit 12 V und Handmessungen | Offen |
| LAN mit angeschlossenem Kabel | Offen |
| USB DFU | Enumeration beim dokumentierten Aufbau fehlgeschlagen; Ursache offen |

Quelle: [README](https://github.com/Axel051171/UniversalFluxInterface/blob/main/README.md), [Bring-up-Bericht, insbesondere Abschnitt 8](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Bringup_2026-10-24.md).

## Firmware und Host

Die Implementierung umfasst Flux-Streaming, klassische und erweiterte Schreibpfade, mehrere Decoderfamilien, Analysejobs, Standalone-Dumps, USB-Betriebsarten und LAN. Native Tests, Kontrollflussprüfungen, Fuzzing und ARM-Build ergänzen sich.

Testanzahlen ändern sich mit der Entwicklung. Für eine konkrete Freigabe den zugehörigen CI-/Offline-Bericht und die tatsächlich ausgeführten Korpora verwenden. Übersprungene Prüfungen bleiben sichtbar; ein grünes Grund-Gate ist kein Nachweis eines vollständig ausgeführten Korpuslaufs.

## Änderungen vom 14. November 2026

Alle folgenden Punkte sind durch Offline-Tests belegt und noch nicht auf einer Platine gelaufen.

- **Lesefortschritt:** Die Fortschrittsmeldung je Umdrehung wurde bis dahin nie gesendet und trug falsche Werte. Sie kommt jetzt an, auch für die letzte Umdrehung, mit der Länge von Index zu Index und der Zeit seit Lesebeginn.
- **Schreibschutz:** Ein neues Ereignis meldet, wenn sich der Schreibschutz des gewählten Laufwerks ändert (nicht für Disk II und IEC).
- **Verworfene Ereignisse:** Das Gerät zählt, wie viele Ereignisse es nicht zustellen konnte, getrennt nach Ursache. Ereignisse bleiben Hinweise; verbindlich ist der Abschluss jedes Auftrags.
- **Abbilddateien:** SCP, HFE, G64 und die Apple-/Mac-Formate werden erst vollständig geschrieben und geprüft und dann an ihren Platz gesetzt. Ein Abbruch hinterlässt keine halbe Datei und überschreibt kein vorhandenes Abbild.
- **Geschützte Aufträge:** Wer Decoder-Toleranz, Drehzahlprüfung oder Präkompensation ändert, muss das Konfigurations-Token danach neu holen; ein älteres wird abgewiesen.
- **USB und LAN gleichzeitig:** Einstellungen lassen sich nicht mehr von der Verbindung ändern, die das Laufwerk gerade nicht besitzt.
- **Kopfschritte nach Zeitplan:** Einträge später als 15,6 s werden ausgeführt; ein fehlgeschlagener Schritt beendet den Auftrag mit einem Fehler, statt an der falschen Position weiterzulesen.
- **Viertelspuren:** Das PC-Werkzeug führt Spur und Viertelschritt getrennt.
- **LAN:** `ufi net` zeigt einen Zähler für fehlgeschlagene Übertragungen zum Netzwerkbaustein, sobald er über null liegt.

Einzelheiten: [Protokoll, Abschnitte 4.26 und 4.27](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).

## Nächste Messungen

1. USB-Prüflauf auf der zweiten Platine wiederholen.
2. Versorgung und Signale mit Testlaufwerk prüfen.
3. Bekannte Fluxfolgen bzw. bekannte Medien lesen und unabhängig vergleichen.
4. Schreibsignal zuerst ohne Medium am Analysator abnehmen.
5. Testmedium schreiben und unabhängig zurücklesen.
6. LAN-Link, Adressvergabe, Datenübertragung und Fehlerfälle am Gerät prüfen.

Pläne: [Schreibpfad-Abnahme](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Abnahme_Schreibpfad.md), [Hardwarequalifikation](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Firmware_Hardwarequalifikation_2026-10-22.md).

Historische Berichte bleiben wertvoll für die Entstehungsgeschichte. Eine ältere Aussage wie „LAN-Firmware fehlt“ oder „noch keine Hardware aufgebaut“ wird durch jüngere, konkret dokumentierte Ergebnisse ersetzt.
