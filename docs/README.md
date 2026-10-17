# Dokumentation

Stand: Firmware 1.18-dev, Platine v0.7; alle Softwareaussagen sind ohne aufgebaute Hardware entstanden.

## Schnittstelle und Umsetzungsstand

| Datei | Inhalt |
|---|---|
| [USB_Protokoll.md](USB_Protokoll.md) | Verbindliche Beschreibung beider USB-Protokolle (Greaseweazle-Modus, UFI v2 mit allen Erweiterungen ab 1.18-dev); wird mit jeder Firmwareänderung fortgeschrieben |
| [Implementation_Status.md](Implementation_Status.md) | Was in 1.18-dev umgesetzt ist, mit Nachweisen und Grenzen (englisch) |
| [Firmware_Erweiterungen_2026-10-16.md](Firmware_Erweiterungen_2026-10-16.md) | Letzter geprüfter Stand: dauerhafte Analysepläne, Rohspurprofil, HFEv3-Export, Prüfläufe, Binärhashes |
| [Firmware_Abschluss_2026-10-16.md](Firmware_Abschluss_2026-10-16.md) | Historischer Zwischenstand davor: Korrekturen zur Prüfungs- und Entwicklungsanweisung |
| [Offline_Review_2026-10-16.md](Offline_Review_2026-10-16.md) | Historischer Zwischenstand davor: mediengebundene Starts, Clang-Analyse, Restarbeiten |
| [Speicherarchitektur.md](Speicherarchitektur.md) | Aufteilung von DTCM, AXI-SRAM, SRAM1/2 und PSRAM-Fluxpuffer; Prüfung durch `firmware/tools/check_memory.py` bei jedem Link |
| [Amiga_Prueflesen.md](Amiga_Prueflesen.md) | Amiga-Sektorprüfung und Spurqualität im Host |

## Abnahme am Gerät (noch nicht ausgeführt)

| Datei | Inhalt |
|---|---|
| [Abnahme_Schreibpfad.md](Abnahme_Schreibpfad.md) | Messplan für den Schreibpfad (Loopback, Verify-Stufen, Index/WGATE, Schreibschutz) |
| [../kicad/UFI_Headless/docs/Inbetriebnahme.md](../kicad/UFI_Headless/docs/Inbetriebnahme.md) | Inbetriebnahme der Platine |

## Vergleiche mit fremder Floppy-Firmware

Geprüft wurde jeweils ein fester Commit beziehungsweise eine Datei mit Hash; es wurde kein Fremdcode übernommen.

| Datei | Projekt |
|---|---|
| [ADF_Drive_Review.md](ADF_Drive_Review.md) | ADF-Drive-Firmware (Amiga-Laufwerk am Arduino) |
| [Biosfuck_Review.md](Biosfuck_Review.md) | biosfuck |
| [Floppy_Emu_Review.md](Floppy_Emu_Review.md) | Floppy Emu 231209M (englisch) |
| [REA_Floppy_Emu_Analyse_2026-10-17.md](REA_Floppy_Emu_Analyse_2026-10-17.md) | Floppy Emu 231209M, Binärabbild zerlegt: AVR-Programm und XC9572XL-XSVF, GCR-Tabellen und -Kodierer, 7-Bit-CPLD-Schnittstelle, Zonentabellen |
| [GLaDISK_Review.md](GLaDISK_Review.md) | GLaDISK |
| [HP9895_Review.md](HP9895_Review.md) | HP 9895 Firmware (hp9895fw) |
| [HxC_Gotek_Review.md](HxC_Gotek_Review.md) | HxC-Firmwarearchiv für Gotek |
| [KryoFlux_Binary_Review.md](KryoFlux_Binary_Review.md) | KryoFlux-Binärdatei, statische Untersuchung |
| [REA_Firmware_Analyse_2026-10-17.md](REA_Firmware_Analyse_2026-10-17.md) | REA mit Ghidra (WSL) über HxC-Archiv (UPD verschlüsselt) und KryoFlux-Abbild: Ladeformat, USB-Steueranfragen, 31 Parameter mit Standardwerten, Laufwerkssteuerung, Taktvergleich mit UFI |
| [Sergev_Floppy_BIOS_Review.md](Sergev_Floppy_BIOS_Review.md) | sergev/floppy-bios |

## Hardware

| Datei | Inhalt |
|---|---|
| [Amiga_DB23_Adapter_Cable.md](Amiga_DB23_Adapter_Cable.md) | Adapterkabel für externe Amiga-Laufwerke am UFI |
