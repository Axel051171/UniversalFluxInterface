# REA-Analyse: HxC-Gotek-Firmware und KryoFlux-Firmware

Stand: 17.10.2026. Werkzeug: REA (`rea-agents` 6.5.0, Skill-Version 42) sowie die
GNU-ARM-Toolchain und eigene Python-Skripte. Nichts wurde ausgeführt, geflasht oder in
UFI übernommen. Dieses Dokument ergänzt [HxC_Gotek_Review.md](HxC_Gotek_Review.md) und
[KryoFlux_Binary_Review.md](KryoFlux_Binary_Review.md); dort stehen die bereits
umgesetzten Anregungen.

## Werkzeugstand (bestimmt den Umfang)

- REA ist für Claude Code registriert (`~/.claude.json`, aligned), die MCP-Werkzeuge
  waren in dieser Sitzung aber nicht verbunden; die Analyse lief über die REA-Kommandozeile.
- Keine Analyse-Engine vorhanden: weder Hopper noch Ghidra (`GHIDRA_INSTALL_DIR`) noch
  IDA. Damit sind `decompile`, `function` und `inspect-native-api` nicht verfügbar.
  `inspect-firmware-regions` verlangt Linux mit Binwalk; die WSL-Instanz (Ubuntu 26.04)
  hat weder Binwalk noch Java/Ghidra, und `sudo` verlangt ein Passwort.
- Engine-frei lief `inspect-artifact` auf dem HxC-Archiv (Evidence
  `ev_2ffccc47a2bee4007d4ec822588f7448edfb85cce3951bda0a1baec9118f76fa`, Inspection
  `ai_70fa4afe8585ec186cc93ee6d998ecd89d966e6ada677512d28e4d2964f1405d`, SHA-256
  `fab679db…0052`, Format zip, eine Root-Manifest-Beobachtung, keine Hypothesen).
  Auf dem KryoFlux-Rohabbild lehnt `inspect-artifact` ab (`unsupported binary format`);
  REA kennt Rohabbilder nur über `decompile --target-format raw-binary --architecture arm
  --entry-point …`, also nur mit Engine.

Nachrüstung für die Dekompilierung (Entscheidung des Nutzers, nicht ausgeführt):
JDK 21 und ein entpacktes Ghidra-12.1.x-Release, `GHIDRA_INSTALL_DIR` darauf setzen,
Claude Code neu starten, dann `rea decompile <bin> --target-format raw-binary
--architecture arm --entry-point 0x200000`. Unter WSL zusätzlich `apt install binwalk`
für `inspect-firmware-regions`.

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
genutzt. Die Firmware-Dateien `HXCFEUSB_HFE_V3_0_0_0a.upd` (77 824 Byte) bis
`HxCFEUSB_V5_3_1_1a_OSD_U100LQD.UPD` (101 376 Byte) zeigen nur das Wachstum des
Abbilds über die Versionen.

## KryoFlux `firmware_kf_usb_rosalie_x64.bin`

25 288 Byte, SHA-256 `7033398d…3b88`, ARM-Code (ARM7TDMI-Konventionen), Ziel
AT91SAM7-Familie (PIOA `0xFFFFF400`, AIC `0xFFFFF000`, Timer/Counter `0xFFFA0000`).
Disassemblierung mit `arm-none-eabi-objdump -b binary -marm -D` nach Bestimmung der
Ladeadresse; alle Adressen unten sind Dateioffsets bzw. Linkadressen ab `0x200000`.

**Lade- und Startmechanik (neu, aus dem Code belegt):**

| Stelle | Befund |
|---|---|
| Offset 0x00–0x10 | `ldr r4,[0x14]`, `mov sp,r4` mit `0x00210000` = Stackanfang am Ende von 64 KiB SRAM; `msr CPSR_c,#0xD3`; `b 0x629c` |
| Offset 0x629c | Kopierschleife von `0x202018` nach `0x200000`, Ende `0x20829c`, danach `b` auf Linkadresse `0x200088` |
| Offset 0x88 | eigentlicher Reset: Stack, `.data`-Kopie (Offset 0xA8–0xC0), `.bss`-Löschung (ab 0xC4), Sprung in C-Code |

Das Abbild wird also vom Host (DTC) an `0x202018` ins SRAM geladen, verschiebt sich
selbst nach `0x200000` und startet dort; das Ende der Kopie `0x20829c` liegt hinter dem
Dateiende (Datei bis `0x2062c8`), der Rest ist RAM. Damit ist der Chip ein SAM7 mit
64 KiB SRAM (zum Beispiel SAM7S256) und die Firmware läuft komplett aus dem RAM; das
erklärt, warum der Host sie bei jedem Start überträgt und warum das USB-Gerät die
Atmel-SAM-BA-Kennung `03EB:6124` behält.

**Parametertabelle:** Bei `0x205f48` liegen 31 Zeiger auf die Namen `status`, `info`,
`result`, `data`, `index`, `reset`, `device`, `motor`, `density`, `stream`, `min_track`,
`max_track` und die vierzehn `t_*`-Zeitparameter, direkt gefolgt von den Werten
`0x0C 0x0D 0x0E`; die Namen stehen ab `0x205dc8` wortausgerichtet hintereinander. Das
ist die Schlüsselliste des `name=value`-Protokolls des Hosts, kein Sprungziel.
Die Zeiger auf die Tabelle selbst erscheinen nicht als Literal im Abbild, der Zugriff
läuft also über berechnete Adressen; welche Funktion sie auswertet, ist ohne
Dekompiler nicht bestimmt. Eine zweite Tabelle bei `0x206158` (22 Zeiger) verweist in
die `sck=…`/`ick=…`-Strings und bildet die Ausgabe des `info`-Berichts.
192 Funktionsrückkehrstellen; die Hauptschleife, der USB-Handler und die
Flusserfassung am Timer/Counter sind damit lokalisierbar, aber nicht rekonstruiert.

**Takte:** `sck=24027428.57` ist MCK/2 bei MCK = 18,432 MHz × 73/28 = 48,0549 MHz, der
üblichen SAM7-PLL; `ick=3003428.57` = sck/8. Beides deckt sich mit der öffentlichen
Stream-Dokumentation und ist damit keine neue Information für UFI.

## Was das für UFI bedeutet

- Von HxC ist nichts weiter zu holen als bereits genutzt (HFE-Korpus, Dokumentation).
- Von KryoFlux sind Zeitparameter-Namen und Protokollschlüssel bekannt; Defaults,
  Hard-Sector-Erkennung und PLL stehen erst mit Dekompiler zur Prüfung. UFI hat die vier
  Anregungen aus der ersten Untersuchung bereits umgesetzt (INFO Bit25).
- Offen und nur mit Engine lösbar: der `name=value`-Parser, die Index-/Sektorloch-
  Klassifikation und die Timer-Konfiguration der Erfassung.

Ablage der Zwischenergebnisse (nicht versioniert): Scratchpad `rea/` mit
`kf_arm.dis`, `kf_strings.txt`, `hxc_rea_artifact.json`.
