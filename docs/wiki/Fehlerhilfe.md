# Fehlerhilfe

**Sprache / Language:** [Deutsch](Fehlerhilfe) | [English](Troubleshooting)

Zuerst Verbindung, Betriebsart, Firmware und Boardzustand prüfen. Wiederholte Schreibversuche sind keine geeignete Diagnose für einen ungeklärten Laufwerksfehler.

## Grundinformationen

```console
ufi info
ufi status
ufi psram
ufi diagnostics
ufi features
ufi net
```

Bei einem Fehlerbericht die Ausgaben zusammen mit dem genauen Befehl, Boardrevision, Laufwerksmodell, Versorgung, Kabel und dem erwarteten Ergebnis angeben. Geheimnisse aus LAN.CFG bzw. der Kommandozeile entfernen.

## Typische Symptome

| Symptom | Nächster sinnvoller Schritt |
|---|---|
| Kein Gerät gefunden | USB-Datenkabel, Port und Modus prüfen; in GW-/SD-/Floppy-Modus gibt es keinen normalen UFI-CDC-Port |
| Mehrere Geräte gefunden | COM-/Geräteport oder USB-Seriennummer ausdrücklich wählen |
| PSRAM nicht verfügbar | Startmeldung und `ufi psram` lesen; Ersatzpuffer hat deutlich weniger Kapazität |
| Kein Index | Motor, Laufwerksauswahl und INDEX prüfen; Disk II benötigt Sensor oder Zeitscheiben |
| Seek fehlgeschlagen | TRK0, Richtung, Schrittzeiten und zulässige Geometrie prüfen |
| Puffer voll | Aufnahmeparameter, Datenrate und Abnahme durch den Host prüfen; Ersatzpuffer berücksichtigen |
| `busy` | Laufenden Auftrag, USB-/LAN-Besitzer und Composite-Ressourcen prüfen |
| Kein LAN-Link | Kabel, Gegenstelle, W5500-Erkennung und `lan=on` prüfen |
| DFU erscheint nicht | Dokumentiert offener Punkt; den belegten SWD-Weg verwenden |

`ufi psram retest` führt Start-/Schreibtests des Speichers erneut aus. Erst die ursprüngliche Diagnose sichern und sicherstellen, dass kein wichtiger Aufnahmeinhalt mehr benötigt wird.

## Häufige Statuscodes

| Code | Bedeutung | Reaktion |
|---|---|---|
| 1 | Busy | Besitzer und laufenden Auftrag prüfen |
| 3 | Seek fehlgeschlagen | Kopfposition und Sensoren prüfen |
| 4 | Kein Index | Auswahl, Motor, INDEX-/Sync-Quelle prüfen |
| 5 | Timeout | Verbindung und erwartete Ereignisse prüfen |
| 6 | DMA-/Verify-Fehler | Diagnosezähler und Bericht sichern |
| 10 | Puffer voll / Überlauf | Aufnahme als unvollständig behandeln |
| 11 | Nicht implementiert / Kombination nicht unterstützt | Fähigkeiten und Backend prüfen |
| 12 | Schreibschutz | Frontschalter und Laufwerksschreibschutz prüfen |
| 14 | Rahmen-/CRC-Fehler | Transport und Protokoll prüfen |
| 16 | Ungültige Argumente | `ufi refusal`, Befehlsmaske und Parameter prüfen |
| 18 | Schreibintervall außerhalb des Bereichs | Geeigneten Pfad wählen; nicht blind kappen |
| 19 | Spur länger als verfügbarer Indexbereich | Timing, Drehzahl und Spurlänge prüfen |
| 20 | Abgebrochen | Abbruchursache und Nachzustand prüfen |
| 21 | Medium geändert | Identität prüfen und Auftrag neu vorbereiten |
| 22 | Sitzungsschreibsperre | Bleibt bis MCU-Reset gesetzt |
| 23 | Drehzahlabweichung | Quellperiode und reale Drehzahl vergleichen |
| 24–26 | LAN-Zugriff / Authentifizierung | Zugriffsart und Schlüssel prüfen |

Die Zahl ist ein UFI-Protokollstatus, kein Betriebssystem-Fehlercode. Vollständige Definition: [ufi_fixes.h](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/ufi_fixes.h).

## Nach Abbruch

`ufi abort` beendet einen erreichbaren laufenden Auftrag. Danach Status, Motorzustand und Schreibsperre kontrollieren. Bei Originalmedien Hardware-Schreibschutz aktiv lassen. Einen Reset erst durchführen, wenn benötigte Diagnosen bzw. erhaltene Aufnahmedaten gesichert sind.

Fehler melden: [GitHub Issues](https://github.com/Axel051171/UniversalFluxInterface/issues).
