# REA-Analyse: HxC-Gotek-Firmware und KryoFlux-Firmware

Stand: 17.10.2026 (zweiter Durchgang mit Ghidra). Werkzeug: REA (`rea-agents` 6.5.0,
Skill-Version 42) mit Ghidra 12.1 in WSL, dazu die GNU-ARM-Toolchain und eigene
Python-Skripte. Nichts wurde ausgeführt, geflasht oder in UFI übernommen; Pseudocode
wurde gelesen, nicht kopiert. Dieses Dokument ergänzt
[HxC_Gotek_Review.md](HxC_Gotek_Review.md) und
[KryoFlux_Binary_Review.md](KryoFlux_Binary_Review.md) (Dateioffsets, erste Befunde).

## Werkzeugstand

- Windows: REA ist für Claude Code registriert, aber ohne Analyse-Engine (weder Hopper
  noch Ghidra noch IDA); engine-frei lief nur `inspect-artifact` auf dem HxC-Archiv
  (Evidence `ev_2ffccc47…76fa`, Inspection `ai_70fa4afe…405d`).
- WSL Ubuntu (als `root`, da `sudo` ein Passwort verlangt): OpenJDK 21.0.12, Node 22,
  npm 9.2, Binwalk 2.4.3, Ghidra 12.1 PUBLIC (Build 20260513) unter
  `/opt/ghidra_12.1_PUBLIC`. Damit laufen `analyze`, `decompile`, `search`, `trace`,
  `xrefs`, `search-bytes` und `inspect-native-load-image`.
- Aufruf aus Windows (ein Beispiel; `--snapshot` hält die Ghidra-Analyse zwischen den
  Aufrufen, die Positionsangabe in `--procedure` ist eine Prozedur oder ein Dateioffset):

```bash
wsl -u root -e bash -lc 'export GHIDRA_INSTALL_DIR=/opt/ghidra_12.1_PUBLIC REA_BINWALK_COMMAND=/usr/bin/binwalk; cd /root; \
  npx -y rea-agents@6.5.0 decompile /mnt/c/<pfad>/kf_image.bin --procedure FUN_00201180 \
  --target-format raw-binary --architecture arm --entry-point 0x200000 --provider ghidra \
  --snapshot /root/kf.snapshot --format json'
```

- `--entry-point` ist bei REA zugleich Lade- und Einstiegsadresse. Für das
  KryoFlux-Abbild muss deshalb das um den 24-Byte-Vorspann gekürzte Abbild
  (`kf_image.bin`, siehe unten) analysiert werden; auf der Rohdatei stimmen sonst alle
  absoluten Adressen um 0x18 nicht, und Ghidra findet weder Strings noch Tabellen.

## HxC-Gotek-Archiv: Firmware ist verschlüsselt

790 Dateien, davon 549 `.UPD` (45,2 MB). Alle 549 beginnen mit demselben 21-Byte-Präfix
`48 58 43 46 55 50 44 41 54 45 00 04 00 43 58 46 45 00 00 00 00` (`HXCFUPDATE`, dann
`04 00`, `CXFE`, vier Nullbytes). An Offset 0x15 steht ein u32 little endian, und für
alle 549 Dateien gilt Dateigröße = 512 + 512 × dieser Wert: ein 512-Byte-Header plus
Nutzlast in 512-Byte-Blöcken. Die Nutzlast hat 7,993–7,998 Bit/Byte Entropie und keine
lesbaren Strings; die vier Bytes nach dem Blockzähler unterscheiden sich pro Datei
(Prüfsumme oder IV, nicht belegt). Beobachtung: Nutzlast verschlüsselt oder stark
komprimiert. Folgerung: aus dem Archiv lässt sich kein Gotek-Maschinencode
disassemblieren, auch nicht mit Ghidra. Analysierbar bleiben die Windows-Werkzeuge
(`hxcfe.exe`, `libhxcfe.dll`, `HxCFloppyEmulator.exe`, `SecSerialBridge.exe`), die
HFE-/QD-/CFG-Beispiele und die Dokumentation; davon wurde bereits der HFE-Korpus
genutzt.

## KryoFlux `firmware_kf_usb_rosalie_x64.bin`

25 288 Byte, SHA-256 `7033398d…3b88`, ARM-Code für AT91SAM7S (64 KiB SRAM), Firmware
3.00s vom 27.03.2018. Ghidra über REA auf dem gekürzten Abbild: Evidence
`ev_1dfdbf63…9de6` (`analyze`: ein Segment 0x200000–0x206284, 171 Prozeduren, 36 Strings),
`ev_da5666aa…4d19` (Hauptprogramm), `ev_f03b595a…7341` (Kommandoverteiler),
`ev_758a74b9…c764` (Spursuche), `ev_d7a094a2…2a34` (Motor), `ev_c6924d9c…4dec`
(Schrittimpuls), `ev_c566592f…e574` (Taktinitialisierung). Alle Adressen unten sind
Linkadressen; Dateioffset = Linkadresse − 0x200000 + 0x18.

### Ladeformat (korrigiert gegenüber dem ersten Durchgang)

| Stelle | Befund |
|---|---|
| Datei 0x00–0x17 | 24-Byte-Vorspann: `ldr r0,=0x210000; mov sp,r0; msr CPSR_c,#0xD3; b <Kopierstub>`; kein Teil des gelinkten Abbilds |
| Datei 0x18 | Beginn des gelinkten Abbilds (Vektortabelle, Linkadresse 0x200000) |
| Datei 0x629c | Kopierstub: kopiert 0x6284 Byte von 0x202018 nach 0x200000 und springt auf den Reset bei 0x200070 |
| 0x200070 | Reset: Stack 0x210000, `.data`-Kopie leer (Quelle = Ziel = 0x206284), `.bss` 0x206284–0x2067e0 löschen, Betriebsmodi mit eigenen Stacks, Aufruf 0x203540 (Takt), dann 0x201ed4 (Hauptprogramm) |

Der Host (DTC) lädt also die ganze Datei nach 0x202000 ins SRAM, der Vorspann
verschiebt das Abbild auf seine Linkadresse. Die Firmware läuft komplett aus dem RAM,
weshalb das Gerät die Atmel-SAM-BA-Kennung behält. Die Aussage des ersten Durchgangs
(„Host lädt nach 0x202018, Reset bei 0x200088") war um den Vorspann verschoben.

### Takt

0x203540 (`ev_c566592f…`): Hauptoszillator ein, `CKGR_PLLR = 0x1048100E` (DIV 14,
MUL 72, also ×73), `PMC_MCKR = 7` (PLL, Vorteiler 2) → MCK = 18,432 MHz × 73 / 28 =
48,0549 MHz. Watchdog aus (`WDT_MR = 0x8000`), AIC mit Standardvektoren, Flash-Waitstate
gesetzt. Die im Abbild stehende Zeichenkette `sck=24027428.571428, ick=3003428.5714285`
ist MCK/2 und MCK/16 und deckt sich mit der öffentlichen Stream-Spezifikation.

### USB-Schnittstelle: binäre Steueranfragen, Textantworten

Gerätedeskriptor bei 0x20619c: USB 2.0, Klasse FF, EP0 8 Byte, VID 03EB PID 6124,
Strings „www.softpres.org", „KryoFlux DiskSystem", Serie „00000000". Konfiguration
(0x206214, 60 Byte): selbstversorgt, 500 mA; Interface 0 ohne Endpunkte mit
CDC-Funktionsdeskriptoren; Interface 1 Klasse FF mit EP1 OUT Bulk 64 und EP2 IN Bulk 64.

Der Kommandoverteiler 0x201180 (`ev_f03b595a…`) bekommt das 8-Byte-Setup-Paket einer
Vendor-Steueranfrage. Es gibt keinen `name=value`-Parser in der Firmware; die Namen
dienen nur der Antwort:

- `bRequest` Bit 0–6 = Parameternummer (0–30, größere Werte → 30 `invalid_option`),
  Bit 7 gesetzt = nur lesen.
- Wert = `wValue << 16 | wIndex` (32 Bit).
- Nie gesetzt werden 0 `status`, 1 `info`, 2 `result`, 4 `index`; 7–11 (`motor`,
  `density`, `side`, `track`, `stream`) nur wenn ein Laufwerk gewählt ist
  (`device` ≥ 0); bei 5–11 wird `result` vorher auf 0 gesetzt.
- Antwort über EP0 als Text `"%08x: %s=%d"` (Zeitstempel, Name, Wert). `info=1` hängt
  `name=KryoFlux DiskSystem, version=3.00s, date=…, time=…` an, `info=2` die
  Hardwarezeile `hwid=1, hwrv=1, hs=1, ` plus `sck`/`ick` und `wb=…, wa=…, wq=…`
  (der Infoblock im Datenstrom enthält dieselben Zeilen ohne `wb/wa/wq`). `index=N` liefert `%d` Einträge und je `, %08x` Zeitstempel der
  letzten Indeximpulse aus einem Ring (0x2009ec, 0x2006d8).
- Aktionen: `reset` → Parameter auf Standard, Laufwerk neu wählen; `device` →
  Datenstrom aus, Laufwerk wählen (0x2028cc); `motor` → 0x202448; `density` →
  0x202294; `side` → 0x2023cc; `track` → auf `min_track…max_track` begrenzt, dann
  Spursuche 0x202698; `stream` → Betriebsart (0x200ff4).

### Parameter und Standardwerte

Namenstabelle 0x205f30 (31 Zeiger; „track" ist das vom Compiler zusammengelegte Ende
von „min_track"), Wertetabelle 0x206710 im `.bss`, Standardwerte aus 0x201fd0. Zeiten
in Mikrosekunden (Beobachtung der Verwendung: Argument einer Mikrosekunden-Warteschleife
0x203238, die auch mit 20000 und 50000 aufgerufen wird).

| Nr. | Name | Standard | Verwendung (belegt im Pseudocode) |
|---|---|---|---|
| 0–5 | status, info, result, data, index, reset | 0 | Kommandos ohne Zeitwert |
| 6 | device | bleibt bei `reset` erhalten | gewähltes Laufwerk, −1 = keines |
| 7–11 | motor, density, side, track, stream | 0 | Zustände |
| 12 | min_track | 0 | untere Grenze der Spursuche |
| 13 | max_track | 81 | obere Grenze der Spursuche |
| 14 | t_set_line | 4 | Warten nach jedem Leitungswechsel (Motor, Dichte, Seite ohne Motor) |
| 15 | t_density_select | 500 000 | nach Dichtewechsel bei laufendem Motor; Teil der Einschaltwartezeit |
| 16 | t_drive_select | 60 000 | Laufwerkswahl |
| 17 | t_side_select | 1 000 | nach Seitenwechsel bei laufendem Motor |
| 18 | t_direction_select | 12 | nach dem Setzen von DIR, vor dem Schritt |
| 19 | t_spin_up | 800 000 | Einschalten: Warten = max(t_spin_up, t_step_after_motor, t_density_select) |
| 20 | t_step_after_motor | 200 000 | siehe 19 |
| 21 | t_step_signal | 4 | Breite des Schrittimpulses |
| 22 | t_step | 8 000 | Abstand zwischen Schritten |
| 23 | t_track0_signal | 1 000 | Warten vor dem Lesen von TRK00 |
| 24 | t_direction_change | 38 000 | bei Richtungsumkehr vor dem Umschalten von DIR |
| 25 | t_head_settling | 40 000 | nach Spursuche und Kalibrierung |
| 26 | t_write_gate_off | 1 200 | nach Abschalten von WGATE |
| 27 | t_write_gate_on | 8 | nach Einschalten von WGATE |
| 28 | t_bypass_off | 8 | nach Abschalten der Bypass-Leitung (Pin 14, um Spur 0 herum) |
| 29 | t_bypass_on | 8 | nach Einschalten der Bypass-Leitung |
| 30 | invalid_option | 0 | Fehlerplatz |

### Laufwerkssteuerung

- Pins kommen aus einer Tabelle (0x206140, 22 Einträge; je Eintrag Maske, PIOA-Basis
  0xFFFFF400, Flags). Kein Peripherieregister steht als Literal im Code, einzige
  Ausnahme ist die AIC-Basis im Interrupt-Einsprung. Aus dem Code belegt: Pin 1 WGATE,
  2 SIDE, 3 STEP, 4 DIR, 5/6 MOTOR für Laufwerk 0/1, 9 DENSITY, 14 Bypass-Leitung,
  17 TRK00; 12/13 werden im Wartezyklus als Blinkmuster angesteuert (LEDs, Schluss aus
  dem Muster). Gelesene Pins sind über ein Flag-Bit der Tabelle invertierbar. Die
  Eingänge 18–21 (INDEX, RDATA, WPT, Wechsel/Bereit) sind in der Tabelle als Eingänge
  erkennbar, ihre Zuordnung wurde nicht einzeln belegt.
- Schritt (0x202510): Richtung setzen, bei Umkehr `t_direction_change`, STEP für
  `t_step_signal` aktiv, danach `t_step`, dann `t_track0_signal` und TRK00 lesen;
  Spurzähler mit 0xFFFF als „unbekannt".
- Spursuche (0x202698): Motor an; bei unbekannter Spur bis zu 86 Schritte nach außen,
  bis TRK00 kommt, dann `t_head_settling`; Zielspur auf `min_track…max_track` begrenzt;
  nach dem Positionieren `t_head_settling`; Motorzustand danach wiederhergestellt.
- Motor (0x202448): Pin je Laufwerk, Dichte neu anlegen, `t_set_line`; beim Einschalten
  Warten = max(`t_spin_up`, `t_step_after_motor`, `t_density_select`).
- Laufwerkswahl (0x2028cc/0x20289c): Motor aus, Zustände zurücksetzen, Spur unbekannt,
  dann Motor an, Seite, Spursuche, Motor wieder wie zuvor.
- WGATE (0x202354) mit `t_write_gate_on`/`_off`.

### Datenstrom und Schreiben

- `stream`: Wert Bit 0–7 = Betriebsart (0 aus, 1 lesen, 2 schreiben), Bit 8–21 ein
  Zähler (vermutlich Umdrehungen; nicht weiterverfolgt). Umschalten nur aus 0 heraus.
- Lesen (0x200984, 0x2019f0): Erfassungspuffer vermutlich 32 KiB (Aufruf mit 0x8000 und
  0x400), Ausgabe in 16-Byte-Einheiten über EP2 IN; der Strom beginnt mit einem OOB-Block Typ 4 (`0x0D 0x04 len`), dessen Text
  genau die bekannte DTC-Infozeile ist (`name=…, hwid=1, hwrv=1, hs=1, sck=…, ick=…`);
  OOB Typ 3 (Ende, 8 Byte) und acht `0x0D` (EOF) liegen vorbereitet. Das deckt sich mit
  der öffentlichen Stream-Beschreibung, die Kodierung der Flusswerte selbst wurde nicht
  weiter rekonstruiert.
- Schreiben (0x20190c, 0x201890, 0x2005f0): Host schickt über EP1 OUT einen 24-Byte-Kopf
  `"KFW"` + Versionsbyte 1, u32 ≥ 50, u32 ≠ 0, Datenlänge ≤ Puffer, u32 ≠ 0,
  Gesamtlänge = Datenlänge + Feld4 + 24; Verstöße liefern `result` 10–16.
  Schreibfehler landen ebenfalls in `result`.

### Was das für UFI bedeutet

- Protokollseitig nichts Neues: KryoFlux nutzt Vendor-Steueranfragen mit Parameternummern
  und Textantworten; UFI v2 ist binär mit CRC und bleibt dabei.
- Die Zeitwerte sind der nützliche Teil: KryoFlux ist durchweg konservativer als die
  UFI-Standardwerte (`firmware/src/ufi_drive.c`, per `DRIVE_TIMING` änderbar):

| Größe | KryoFlux-Standard | UFI-Standard |
|---|---|---|
| Schrittimpuls | 4 µs | 10 µs |
| Schrittabstand | 8 ms | 3 ms |
| Kopfberuhigung nach Spursuche | 40 ms | 15 ms |
| Richtungsumkehr | 38 ms zusätzlich | 0 |
| Seitenwechsel | 1 ms (mit Motor) | 200 µs |
| Hochlauf | 800 ms (max. mit 200 ms / 500 ms) | 500 ms |
| Laufwerkswahl | 60 ms | 10 ms |
| Dichtewechsel | 500 ms (mit Motor) | nicht gesondert |
| Kalibrierung | bis 86 Schritte, TRK00-Sensor | 80 Halbschritte (Apple) bzw. Sensor |

  Die Abnahme am Gerät (`Abnahme_Schreibpfad.md`, Laufwerksprofile) sollte den
  Richtungsumkehr-Zuschlag und die längere Kopfberuhigung als Messpunkte aufnehmen;
  bis dahin keine Änderung der UFI-Standardwerte (keine Messung vorhanden).
- Offen geblieben: Zuordnung der vier Eingangspins, Flusskodierung des Datenstroms,
  Bedeutung des `stream`-Zählers. Alles mit der beschriebenen Strecke machbar, für UFI
  ohne erkennbaren Nutzen.

## Reproduktion

Gekürztes Abbild: `kf_image.bin` = Datei[0x18 : 0x18 + 0x6284] (25 220 Byte, SHA-256
beginnt mit `314c974a`). Analyse mit `analyze`, Prozedurliste mit
`search . --kind procedures --mode regex`, dann `decompile --procedure FUN_xxxxxxxx`
mit gemeinsamem `--snapshot`. Parallele Aufrufe brauchen getrennte Snapshots.
Zwischenergebnisse (nicht versioniert) liegen im Scratchpad `rea/`: `kf_arm2.dis`
(objdump des gekürzten Abbilds), `kf_params.py`, `kf_pool2.py`, `kf_desc.py`,
`kf_trim.py`, `rea2.sh` und die REA-JSON-Ausgaben `kf2_*.json`, `kf3_*.json`,
`kf4_*.json`.
