# Anschlüsse

**Sprache / Language:** [Deutsch](Anschluesse) | [English](Connections)

Diese Übersicht bezieht sich auf **UFI Headless v1.0**. Frühere Revisionen unterscheiden sich insbesondere bei Schreibsperre, Apple-/Mac-Port und einzelnen Lötbrücken. Vor dem Umstecken Strom abschalten und Pin 1 sowie Kabelrichtung am eigenen Board prüfen.

## Anschlussübersicht v1.0

| Anschluss | Verwendung | Beachten |
|---|---|---|
| J1 | USB-C | Host-Verbindung und Boardversorgung |
| J22 | RJ45 | W5500-LAN |
| J6 | 34-poliges PC-/Shugart-Laufwerk | PC-Kabel mit Twist und gerader Shugart-Bus benötigen unterschiedliche Auswahl |
| J7 | Amiga-Laufwerk | Header für Adapter zum externen DB23-Anschluss |
| J14 | Apple Disk II, 20-polig | Nicht mit dem Macintosh-Port verwechseln |
| J23 | Macintosh-3,5-Zoll-Laufwerk | v1.0; JP5/JP6 nur nach laufwerksspezifischer Prüfung |
| J8 | Commodore IEC | Adapter zum seriellen Laufwerksbus |
| J19 | Sync-Sensor | Indexersatz für geeignete Apple-Aufnahmen |
| J12 | Frontbedienung | Taster, Anzeigen und Schreibfreigabeschalter |
| J4 | SWD | Programmieren und Debuggen |
| J3 | Laufwerksversorgung | Geschaltete Versorgung; Polung gegen Schaltplan prüfen |
| J17 | Versorgungseingang | Eigene Eingangsbelegung; nicht mit J3 verwechseln |

## Gemeinsam verwendete Leitungen

PC/Shugart und Amiga teilen Steuerleitungen. Eine Anschlusskombination ist daher nicht automatisch unabhängig nutzbar. DS0–DS3 am geraden Shugart-Bus teilen die Motorleitung.

JP1 kann J6 Pin 6 als DRATE oder DS3 einbeziehen; diese Nutzungen schließen sich aus. JP3 betrifft die zweite Amiga-Auswahl. Auf v1.0 ist J6 Pin 3 fest Masse; ältere Anleitungen mit einer umschaltbaren JP2 gehören zu früheren Revisionen.

## Apple und Macintosh

Auf v0.7 wurde eine zweite Disk-II-Freigabe über J15 geführt. Auf v1.0 dient Laufwerkskennung 11 dem Mac-Port J23. Der Name `apple2` in älteren Beispielen ist deshalb keine allgemeine Empfehlung für v1.0.

Die v1.0-Sync-Leitung wird separat über TIM8 erfasst. Historische Beschreibungen einer direkten Verbindung mit dem normalen INDEX-Signal nicht ungeprüft auf v1.0 anwenden.

## Verbindliche Belegung

Für selbst gebaute Kabel die konkrete Revision im KiCad-Schaltplan und die Firmware-Pinzuordnung prüfen. Ältere Adapterzeichnungen können abweichende Steckernamen enthalten; insbesondere ist J4 auf v1.0 der SWD-Anschluss.

- [Schaltplan](https://github.com/Axel051171/UniversalFluxInterface/blob/main/kicad/UFI_Headless/UFI_Headless.kicad_sch)
- [Firmware-Pindefinitionen](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/include/board.h)
- [Board-Abstraktion](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/board_headless.c)
- [Timer- und Peripheriezuteilung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Timer_Zuteilung.md)

Weiter: [Laufwerke](Laufwerke) · [Amiga](Amiga) · [Apple und Macintosh](Apple-und-Macintosh).
