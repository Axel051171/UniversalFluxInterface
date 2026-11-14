# Inbetriebnahme

**Sprache / Language:** [Deutsch](Inbetriebnahme) | [English](Bring-up)

Ein neues Board wird stufenweise geprüft: Sichtprüfung, Versorgung, Firmware, USB und erst danach Laufwerk. Die Schreibfreigabe bleibt bis zu den vorgesehenen Schreibtests gesperrt.

## Vor dem ersten Einschalten

1. Boardrevision und Bauteilbestückung mit den Fertigungsunterlagen vergleichen.
2. Pin-1-Markierungen, Polaritäten und Lötbrücken prüfen.
3. Frontschalter SCHREIBEN ausschalten oder das Frontkabel abziehen.
4. Kurzschlüsse der Versorgungsschienen ausschließen.
5. Laufwerke zunächst abgesteckt lassen.

Für Versorgung und Messpunkte den [Bring-up-Plan für v1.0](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Bringup_2026-10-24.md) verwenden. Die ältere Inbetriebnahme-Checkliste enthält Abschnitte für frühere Revisionen und muss passend zu v1.0 gelesen werden.

## Firmware und USB prüfen

Die am Prüfstand verwendete Programmierung erfolgt über SWD. Die USB-DFU-Enumeration war beim dokumentierten Bring-up offen; siehe [Firmware](Firmware).

Nach erfolgreichem Flashen zunächst ohne Laufwerk:

```console
ufi info
ufi status
ufi psram
ufi selftest
ufi diagnostics
```

`selftest` ist ein aktiver Hardwaretest. Die vollständige Liste seiner Prüfschritte steht in der [Protokollreferenz](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md). Meldungen `skip` und `warn` getrennt von erfolgreichen Prüfungen betrachten.

## Automatischen Bericht erstellen

Für eine v1.0-Platine nur über USB, ohne angeschlossene 12-V-Quelle:

```console
python -m ufi_host.bringup --port COM5 --expect-rev 10 --no-12v --report bringup-usb.json
```

Den Port anpassen. Der Ablauf prüft Boardfunktionen und kann Versorgungsschienen schalten sowie die RTC setzen. Ohne `--drive` bewegt er keinen Kopf; ohne `--write-test` führt er keinen Diskettenschreibtest aus. Ein Bericht wird mit OK/WARN/FAIL/SKIP und einem Gesamturteil erzeugt.

Für einen ersten Laufwerkslesetest, nach geprüfter Versorgung und bei aktivem Schreibschutz:

```console
python -m ufi_host.bringup --port COM5 --expect-rev 10 --drive a --read-revs 50 --report bringup-drive.json
```

Dieser Ablauf setzt die passende externe Laufwerksversorgung voraus. `--write-test` ausschließlich für den vorgesehenen Abnahmeschritt mit entbehrlichem Medium hinzufügen.

## Ergebnisse festhalten

Mindestens Boardkennung, Firmware-Revision bzw. Binärhash, Versorgung, Laufwerksmodell, Kabel, Medium, Messwerte und JSON-Bericht aufbewahren. Ein USB-Test ohne Laufwerk bestätigt keine erfolgreiche Diskettenaufnahme.

Dokumentierter Ausgangspunkt: Eine v1.0-Platine bestand am 11./12. November 2026 den USB-Lauf mit PSRAM-Nutzung, 49 OK, 3 WARN und 0 FAIL. Die zweite Platine und die noch offenen Laufwerks-, 12-V- und LAN-Messungen stehen auf [Projektstatus](Projektstatus).
