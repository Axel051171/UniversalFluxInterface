# GLaDISK: Vergleich mit UFI

Geprüft am 16.10.2026, Stand
`5290673e2f1a148799f80be63f95811e65a41b39`.
[Repository](https://github.com/640-KB/GLaDISK/tree/5290673e2f1a148799f80be63f95811e65a41b39).

## Einordnung und gelesene Quellen

GLaDISK ist ein x86-PC-Option-ROM für Floppy-Controller, kein Flux-USB-Gerät.
Die README beschreibt v0.4 als Vorabversion und nennt ED-/Perpendicular-
Unterstützung, FIFO, controllerabhängige Konfiguration sowie vier Laufwerke.
Diese Fähigkeiten hängen vom Build und vom Controller ab.

Geprüft wurden README.md, src/GLADISK.ASM, src/GLADISK.INC,
src/INT_13_H.ASM und src/FDC.INC. Der Assemblerkopf nennt GPL Version 3 oder
später. Kein Code wurde in UFI übernommen; keine ROM-/COM-Datei ausgeführt.
Quelldateien liegen nur im ignorierten Build-Cache. Kein Build oder Hardwaretest
von GLaDISK durchgeführt.

## Technisch relevante Beobachtungen

- `FDC_DETECT` in
  [GLADISK.ASM](https://github.com/640-KB/GLaDISK/blob/5290673e2f1a148799f80be63f95811e65a41b39/src/GLADISK.ASM)
  unterscheidet Fähigkeiten über DUMPREG, VERSION, UNLOCK und NSC-MODE.
  Die Kommentare trennen 1-Mbit/s-Ratenfähigkeit ausdrücklich von
  Perpendicular-/2,88-MB-Unterstützung. Die grundlegende HD-Erkennung ist
  teilweise noch TODO bzw. buildabhängige Annahme; das ist keine universelle
  automatische Hardwareerkennung.
- `FDC_SETUP_DRV` in
  [INT_13_H.ASM](https://github.com/640-KB/GLaDISK/blob/5290673e2f1a148799f80be63f95811e65a41b39/src/INT_13_H.ASM)
  verwaltet die Gültigkeit zuletzt gesendeter Laufwerksparameter. Medienwechsel
  invalidiert die Konfiguration. Standard-/Enhanced-Perpendicular und
  Vorkompensation werden getrennt behandelt.
- `FDC_SET_EPMODE` prüft Controller- und Laufwerksfähigkeiten, hat jedoch
  ausdrücklich noch TODOs für Laufwerke 2/3. Die allgemeinen Aussagen über
  Vierlaufwerksunterstützung belegen daher nicht jede ED-Kombination.
- [GLADISK.INC](https://github.com/640-KB/GLaDISK/blob/5290673e2f1a148799f80be63f95811e65a41b39/src/GLADISK.INC)
  enthält modellabhängige Schalter für FIFO, Perpendicular und Implied Seek.
  Implied Seek ist in mehreren Varianten deaktiviert oder als unvollständig
  markiert. FIFO-Schwellen und FDC-Zeitkodierungen sind keine STM32-DMA-Werte.

## Bereits in UFI vorhanden

Laufwerks-/Medienprofile, native Profilwiederherstellung bei Laufwerkswahl,
Mediengenerationen, CHECK_PROFILE, genaue Schreibgrenzen je Backend,
Transportberichte, Positionsgültigkeit und getrennte Erkennungszustände.
Die UFI-Decoder unterstützen ED-MFM-Testspuren. Daraus folgt keine Qualifikation
der realen ED-Schreibkette. Der klassische Schreibpfad hat andere Grenzen als
Extended WRITE. GLaDISK-FDC-Kommandos wie CONFIGURE/FIFO/EPMODE können diese
Grenzen nicht verändern: UFI verwendet Timer/DMA statt eines PC-FDC.

## Umgesetzte Folgerungen (16.10.2026)

1. **Einheitliche Fähigkeitsmatrix für das Tool:** je Format/Betriebsart
   Capture, Decodierung, Schreiben und Prüflesen getrennt ausweisen; zusätzlich
   Voraussetzungen und Qualifikationsstand. Auf vorhandenen DESCRIBE,
   WRITE_LIMITS, FEATURES und Decoder-Discovery aufbauen. ED darf beispielsweise
   nicht allein wegen 1-Mbit/s-Decodierung als physisch schreibqualifiziert gelten.
2. **Konfigurationsgeneration getrennt von Mediengeneration:** Änderungen an
   Timing, Doppelschritt, Dichte/RPM und Profilen generationsgebunden erfassen.
   Ein über mehrere Befehle verteilter Tool-Auftrag kann dann prüfen, ob seine
   Voraussetzungen zwischen Vorbereitung und Ausführung geändert wurden.
   Die bestehenden Sperren während aktiver Operationen bleiben bestehen.
3. **Gezielte Wechsel-/Rückfalltests:** Laufwerk A/B mit unterschiedlichen
   Profilen, DD/HD/ED-Kombinationen, 40-Spur-Medien mit Doppelschritt,
   Reset/Abbruch und abgelehnte Konfigurationen. Sicherstellen, dass keine
   veralteten Parameter als aktuell gemeldet oder stillschweigend übernommen
   werden. Hardwarequalifikation bleibt eine separate spätere Prüfung.

Native Op22 liefert die 224-Byte-Matrix mit sechs Formatfamilien, getrennten
Pfadmasken, Backendgrenzen, Decoder-Raten und Qualifikationsstand. Zulässigkeit
bedeutet Softwarevoraussetzungen nach Port/Belegung, keine physische Zusage.
Offline-Prüflesen bezeichnet den getesteten Inhaltsvergleich; Hardwaremasken
bleiben 0. Schreiben ist Raw-Flux-Ausgabe, kein universeller Sektorencoder.

Op23 meldet die konservative Konfigurationsgeneration getrennt von der
Mediengeneration. Op24 vergleicht sie atomar vor einem normalen Analyse-Start.
Die erste Absicherung galt für Analyse-Starts; inzwischen ergänzt Abschnitt4.20
einen generations- und mediengebundenen WRITE-Start sowie Analyse-Op26.
Keine Mehrspurtransaktion. Gleiche Einstellungen dürfen den
Zähler ebenfalls invalidieren, A → B → A wird dadurch erkannt.

Neue Befundflags: `CAPABILITY_UNCONFIRMED=1`, `WRITE_MODE_UNQUALIFIED=2`,
`PROFILE_CHANGED=4`, `NO_FLUX_PORT=8`, `BUSY=16`, `LINE_CONFLICT=32`.
Keine zusätzlichen Auftragsbits nötig. Layouts und Token-Gültigkeit stehen
in [USB_Protokoll.md, Abschnitt 4.19](USB_Protokoll.md#419-fähigkeiten-und-konfigurationsgeneration-gladisk-anregungen).
INFO verwendet inzwischen alle 32 Capability-Bits. Weitere Discovery sollte
deshalb versionierte Einträge/Operationen erweitern, statt bestehende INFO-Bits
umzudeuten. Kein neues Greaseweazle-Kommando erforderlich.

Validierung: vollständige Offline-Suite bestanden; anschließend erweiterte
Produktionscode-Tests für native A/B-Wahl, Doppelschritt, RPM-Leitungen,
DD/HD/ED-Profilaustausch, atomare Ablehnung, Initialisierung, Analyse-Abbruch
und veraltete Starttoken sowie zwei neue Hostparser-Tests bestanden.
Eine ungültige direkte Laufwerksnummer wird jetzt vor GPIO-Zugriff abgewiesen.
ARM-Build ohne Warnungen, AXI-Reserve unverändert. Kein GLaDISK-Code übernommen.
