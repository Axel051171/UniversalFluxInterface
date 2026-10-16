# sergev/floppy-bios: Vergleich mit UFI

Geprüft am 16.10.2026, Stand
`72943c5a9fe9830ddd6f331d9b6505c5361a8855`.
[Repository](https://github.com/sergev/floppy-bios/tree/72943c5a9fe9830ddd6f331d9b6505c5361a8855).

## Einordnung

C-Portierung des PC-Floppy-BIOS von Sergey Kiselev, Copyright der Portierung
2025 Serge Vakulenko. Damit derselbe konzeptionelle Ursprung wie das bereits
betrachtete skiselev/floppy_bios. Das Projekt verwendet PC-FDC-Kommandos,
x86-I/O, BIOS-Datenbereiche und PC-DMA. UFI arbeitet mit STM32-Timern,
Fluxdaten und eigenen Sektordecodern. Es liefert keinen zusätzlichen Flux-,
Macintosh-, Amiga- oder IEC-Decoder für UFI.

Die README nennt GPL-3.0-or-later. Keine Quellblöcke wurden übernommen.
Dateien wurden lediglich im ignorierten Build-Cache für die Prüfung abgelegt;
kein BIOS ausgeführt oder auf Hardware getestet.

## Konkrete Auffälligkeiten der C-Portierung

- `set_drive_calibrated()` setzt Bit `phys_drive`, während
  `check_drive_calibrated()` Bit `phys_drive+1` prüft. Die Funktionen stimmen
  damit nicht überein.
- `fdc_wait_ready_at()` und `fdc_wait_irq_at()` begrenzen ihre äußeren Schleifen,
  haben aber unbeschränkte innere Warteschleifen auf das Refresh-Bit. Bleibt
  dieses Signal stehen, erreicht die Ausführung den äußeren Timeout nicht.
- `fdc_get_result()` kann nach weniger als der angeforderten Anzahl Bytes
  Erfolg melden, ohne dem Aufrufer die tatsächliche Länge mitzuteilen.
- `fdc_read_id_internal()` liest in ein lokales `result[7]`; anschließend wertet
  `fdc_get_error()` den BIOS-Statusbereich aus. In diesem Pfad wird das lokale
  Ergebnis nicht dorthin kopiert. Die Fehlerentscheidung kann daher auf altem
  Status beruhen.
- `fdc_disk_change_internal()` verwendet für Medienzustand und Seek fest
  physisches Laufwerk 0 statt einer übergebenen Laufwerksnummer.

Dies sind Befunde aus der Quellprüfung, keine Hardwaremessungen. Belegstellen:
[floppy_bios.c](https://github.com/sergev/floppy-bios/blob/72943c5a9fe9830ddd6f331d9b6505c5361a8855/floppy_bios.c),
[fdc_io.c](https://github.com/sergev/floppy-bios/blob/72943c5a9fe9830ddd6f331d9b6505c5361a8855/fdc_io.c),
[int_13.c](https://github.com/sergev/floppy-bios/blob/72943c5a9fe9830ddd6f331d9b6505c5361a8855/int_13.c).

## Bereits in UFI vorhanden

Getrennte Laufwerks-/Medienprofile, Positionsgültigkeit, begrenzte Kalibrierung,
Mediengenerationen, Datenraten-Kandidaten, Profilprüfung, Wiederholungsbudget,
Bestätigungs-/Stagnationspolitik, Fehlerhistorie und Inhalts-Prüflesen. Die
FDC-Zustandsmaschine bietet hierfür kaum neue Funktionen. BIOS-Timingwerte
sind keine universellen Vorgaben für die angeschlossenen UFI-Laufwerke.

## Sinnvolle nächste UFI-Erweiterungen — Vorschläge, nicht implementiert

1. **Erkennungsstatus präzisieren:** konfiguriertes Profil, beobachtetes Format,
   bestätigter Sektorsatz und Mehrdeutigkeit getrennt ausgeben. Ein einzelner
   gefundener Sektor darf keine vollständig erkannte Diskettengeometrie bedeuten.
   Kandidaten für Ergebnismerkmale: `FORMAT_OBSERVED`, `SECTOR_SET_CONFIRMED`,
   `GEOMETRY_UNCONFIRMED`. Bestehende CONFIRM_CONTENT-Politik weiterverwenden.
2. **Spur-/Seiten-ID-Abgleich verallgemeinern:** nicht nur Mac-Zonen, sondern
   auch andere Formate gegen ein explizit gewähltes Medienprofil prüfen.
   Kandidat `HEADER_POSITION_MISMATCH`; abweichende IDs bei absichtlichen
   Sonderformaten lediglich melden. Keine automatische Kopfbewegung aus IDs
   ableiten. Bestehendes CHECK_PROFILE verwenden.
3. **Wiederholungsursachen trennen:** Transport-/Pufferfehler, fehlende
   Sektoren, Prüfsummenfehler und Positionsprobleme im Wiederholungsbericht
   getrennt begründen. Keine zusätzliche Aufnahme nach Medienwechsel und
   keine Rekalibrierung allein wegen abweichender Sektor-IDs. Erst bei einer
   tatsächlich implementierten mechanischen Wiederherstellung wäre eine
   separate, ausdrücklich aktivierte Recovery-Policy nötig.

Bei der ersten Prüfung erfolgte noch keine Firmware-/USB-Änderung.

## Umsetzung nach Freigabe — 16.10.2026

Die drei Vorschläge sind nun unabhängig implementiert, ohne BIOS-Code zu
übernehmen. INFO-Bit31, Discovery-Op21 und die neuen Abschnitte 15/16 ergänzen
das native Analyseprotokoll. Bestehende Jobflags und Statuswerte bleiben erhalten.

- Erkennungsflags unterscheiden Profilherkunft, gültige Formatbeobachtung,
  exakten expliziten Sektorsatz, Wiederholungsbestätigung, Mehrdeutigkeit und
  unvollständige Evidenz. Vollständige Diskettengeometrie bleibt unbestätigt.
  Automatische Medienbeobachtung verlangt jetzt mindestens einen gültigen
  Sektor; die bloße Zahl auch fehlerhafter Sektor-Datensätze reicht nicht mehr.
- CHECK_PROFILE prüft C/H bei passendem deklariertem Profil für IBM-MFM,
  Amiga, IBM-FM, HP-MMFM und Mac-GCR. CBM-GCR wird mit expliziten C/H/N/ID-
  Erwartungen geprüft, ohne eine Spurnummerierungsregel zu erraten.
  Profilprüfungsflag 128 und Ursachenbit20 melden Abweichungen. Kein automatischer
  Seek oder Retry aufgrund dieses Befunds.
- Pro Versuch bleiben Erkennungszustand, Ursachen, gültige Datensätze,
  Positionsabweichungen, Phase und Wiederholungsentscheidung erhalten.
  Transport-/Puffer-/Positionsfehler werden von Sektor- und Bestätigungsbefunden
  getrennt. Ein früher Fehlversuch erbt keine ältere erfolgreiche Erkennung.

Wire-Details und alle Flagwerte: USB_Protokoll.md 4.18. Hostparser in
`ufi_host.recognition`. Vollständige Offline-Suite erfolgreich: 150 Hosttests,
native Tests, Laufzeit-/Jobtests, Greaseweazle, externe Korpora und 23/23
6502-Prüfungen. Gezielte Erkennungs-/Jobtests nach finalen Anpassungen erneut
erfolgreich. ARM-Build warnungsfrei. Kein Hardwaretest.
