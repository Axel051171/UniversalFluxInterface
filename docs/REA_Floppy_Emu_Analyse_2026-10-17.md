# Floppy Emu 231209M: Zerlegung der Firmwaredatei

Stand: 17.10.2026. Eingabe: `femu-231209M.zip` (SHA-256 `d5fac3b5…a94f`, wie in
[Floppy_Emu_Review.md](Floppy_Emu_Review.md)), darin `femu.bin` (184 361 Byte, SHA-256
`56d540b4cfb23a334077e085e9a48b11a39063cb1aec7001b1603d419498b980`). Werkzeuge: REA
`inspect-artifact` (Evidence `ev_a93b7829…6514`, Archivinventar mit 10 Knoten), eigene
Python-Skripte, `avr-objdump` (binutils-avr in WSL) und Ghidra 12.1 headless mit
Prozessor `avr8:LE:16:extended` und einem eigenen Exportskript (REA kennt für Rohabbilder
nur 6502/x86/ARM, daher Ghidra direkt). Nichts wurde ausgeführt, kein Code übernommen;
Pseudocode wurde gelesen, nicht kopiert. Die erste Untersuchung hatte die Dokumentation
und die MOOF-Beispiele ausgewertet; dieser Durchgang betrifft das Binärabbild.

## Aufbau der Datei

| Bereich | Inhalt |
|---|---|
| 0x00000–0x112a8 | AVR-Programm (ATmega1284P, 128 KiB Flash): vollständige Vektortabelle mit 35 `jmp`, Code bis ≈ 0x112a8 (≈ 70 KiB), Zeichenketten ab 0x11000 |
| 0x112a8–0x1efff | Nullen, bei 0x1eff8 der Abschluss `DD DD DD DD 00 01 00 00` |
| 0x1f000–0x1ffff | 0xFF (unprogrammierter Teil des Bootbereichs) |
| 0x20000–0x2d028 | XSVF-Strom für die CPLD (53 289 Byte, endet mit `XCOMPLETE`) |

Die Datei ist also Flashabbild plus angehängter JTAG-Programmierstrom; der Bootloader
der Platine spielt beides ein („UPDATING CPLD" ist eine der Anzeigen). Die Nachrichten
`expected CPLD ver %d` / `actual CPLD ver %d` und `firmware.xvf` zeigen, dass die
AVR-Seite die CPLD-Version prüft und ältere getrennte Dateien kennt.

## CPLD-Teil (XSVF)

Geparst nach XAPP503: 3 358 `XSDRTDO`, 228 `XTDOMASK`, 225 `XRUNTEST`, 15 `XSIR`.
Die erste Operation ist `XSIR 0xFE` (IDCODE) mit erwartetem TDO `0xF9604093` unter Maske
`0x0FFFFFFF`: IDCODE `0x9604093` = **Xilinx XC9572XL**. Befehlsfolge FE, FF, E8, ED, F0,
E8, EA, F0, E8, E8, EE, E8, FF, F0, FF (IDCODE, BYPASS, ISPEN, FERASE, FPGM, …, FVFY,
ISPEX, BYPASS): Löschen, Programmieren (108 Wartezeiten zu 20 000 µs = 108 Zeilen des
XC9572XL), Verifizieren; 167 549 verglichene Datenbits. Der Logikinhalt der CPLD ist aus
dem Programmierstrom nicht lesbar ohne Fuse-Map des Bausteins; nicht weiterverfolgt.

## AVR-Teil

### Betriebsarten und Oberfläche (Strings)

Dieses „M"-Abbild bedient drei Betriebsarten: Macintosh 3.5"-Diskette, Macintosh
HD20-Festplatte, Lisa 3.5"-Diskette (`Mac/Lisa ???` als Fehlertext; die Apple-II-Arten
aus `emumode.txt` gehören zum „A"-Abbild). Weitere Befunde aus den Zeichenketten:
Hardwaretest `CHECK PINS - DEFAULT`, `CHECK PIN CLKO-H/-L`, `CHECK PIN RESET`,
`BAD FUSES %02X %02X %02X`, `Floppy Emu - Model %c`; Displaytypen SSH1106 1,3" und
SSD1309 2,4"; HD20-Protokollfehler (`CMD CHECKSUM`, `UNK COMMAND`, `RESPONSE SIZE`,
`RUNT WRITE`, `GROUP! %02X %02X %02X`); Bildtypen 400K/800K/1440K roh, DiskCopy, MOOF,
„file box"; MOOF mit FLUX-Daten wird abgewiesen (`contains FLUX data`, `Unsupported`);
Partitionssuche `Apple_HFS`; Dateinamen `FEMU.BIN`, `EMUMODE`, `FAVDISKS`, `ZULULOG`,
`README`. Für das „Boxing" liegt bei 0x19e1–0x1d80 ein HFS-Gerüst im Flash (Volume
„Emu Disk", Finder-Einträge, Typ/Creator `TEXTttxt`, `SIT!`, `PACT`), das zwei Funktionen
(0x96fa, 0xa912) in ein temporäres Abbild kopieren.

### Tabellen im Flash

| Offset | Inhalt | Nutzer (Bytefunktion, Größe) |
|---|---|---|
| 0x1317 | CRC-16/CCITT-Tabelle (256 × u16, `0x1021`) | 0x4e54 (220 B, Meldung `checksum fail`), Hilfsroutinen 0x5152/0x51a0 |
| 0x1517 | GCR-6&2-Decodiertabelle (110 Byte ab Code 0x90) | 0x46d4 (1920 B, Schreibpfad) |
| 0x1585 | GCR-6&2-Codiertabelle (64 Byte, 0x96…0xFF) | 0x51f2 (626 B, Sektor-Kodierer, 17 Zugriffe) |
| 0x15c5 | Sektoren je Spur, 80 Einträge: 16 × 12, 11, 10, 9, 8 | ISR 0x4466, 0x46d4, 0x6012, 0x65d2, 0x7512 |
| 0x1615 | kumulierte Sektornummer je Spur, 80 × u16 (0, 12, 24, …, 0x318) | 0x65d2 (3386 B), 0x7512 (5086 B) |

Die Zonentabellen bestätigen die in UFI (Format 6) verwendete Geometrie 12/11/10/9/8
Sektoren in fünf Zonen zu 16 Spuren; die 800K-Variante nutzt dieselben Tabellen je Seite.

### GCR-Kodierer (0x51f2)

Pseudocode zeigt den bekannten Sony-Algorithmus: drei Prüfsummenbytes werden je
Dreiergruppe mit Übertrag rotiert und mit den Datenbytes verknüpft (XOR der Daten mit dem
jeweils vorigen Prüfsummenbyte, Addition mit Übertrag in das nächste), danach die
6&2-Aufteilung `(a & 0xC0) >> 2 | (b & 0xC0) >> 4 | c >> 6` als erstes Nibble-Byte und
`a & 0x3F`, `b & 0x3F`, `c & 0x3F` als drei weitere Bytes, jedes über die Tabelle bei
0x1585 kodiert und einzeln an die CPLD übergeben. Der Schreibpfad (0x46d4, aus der
PCINT0-ISR aufgerufen) decodiert umgekehrt und meldet `checksum failure 0/1/2` je nach
nicht passendem Prüfsummenbyte. Das entspricht dem, was UFI aus der öffentlichen
Spezifikation umgesetzt hat; keine Abweichung gefunden.

### Schnittstelle AVR ↔ CPLD

Nur vier Interrupts sind belegt: PCINT0 (0x4f30), PCINT2 (0x608a), PCINT3 (0x4466) und
Timer0-Überlauf (0xc8d0, 32-Bit-Tickzähler bei RAM 0x37e0). Die Byteausgabe an die CPLD
(0x5134, mit gesperrten Interrupts) ist:

```
out  PORTA, data & 0x7F     ; 7 Datenleitungen PA0..PA6
sbi  PORTC, 5               ; Strobe
warte bis PINA7 = 1, dann bis PINA7 = 0   ; Quittung der CPLD
cbi  PORTC, 5
```

Die CPLD erhält also nur sieben Bit je GCR-Byte, das immer gesetzte höchste Bit ergänzt
sie selbst, und sie erledigt die Serialisierung mit 2 µs je Bit; der AVR liefert Bytes
im Takt der Quittung. Die Gegenrichtung (Host schreibt) kommt nibbleweise über PINA0–3
mit PINA7 als Taktflanke in der PCINT0-ISR an (`PINA & 0x0F`, zwei Nibbles zu einem Byte).
Die PCINT3-ISR rechnet mit der Sektorentabelle die Position innerhalb der Spur aus.
Pinwechsel-Interrupts statt Timer: Die Zeitbasis liegt in der CPLD, nicht im AVR.

### Schreibstrategie

Die Meldungen `Saved trk %02d, %lums`, `Reverted trk %02d`, `wr wrong track %d/%d` und
`bad sector %d for t%d` belegen eine spurweise Arbeitsweise: Eine Spur wird im SRAM
gehalten (16 KiB reichen für 12 × 524 Byte), beim Spurwechsel zurückgeschrieben und die
Dauer protokolliert; Schreibzugriffe auf eine andere als die aktive Spur werden
verworfen. Belegt durch Strings und die Trennung in Spuraufbau-Funktionen (0x65d2,
0x7512) und ISR-Pfade, nicht durch vollständige Verfolgung des Codeflusses.

## Was das für UFI bedeutet

- Geometrie, 6&2-Tabellen und Prüfsummenverfahren decken sich mit der UFI-Umsetzung
  (Format 6, `Floppy_Emu_Review.md`); nichts zu ändern.
- Die 7-Bit-Übergabe und die CPLD-Zeitbasis sind Emulatorarchitektur und für einen
  Flussleser ohne Bedeutung.
- MFM-1440K-Emulation und HD20-Protokoll sind erkennbar (CRC-Tabelle, Protokollmeldungen),
  liefern aber nichts, was UFI nicht aus den Spezifikationen hat.

## Zweites Abbild: `apple-II-0.1R-F8-modelB.zip` (Apple-II-Firmware, Modell B)

Archiv-SHA-256 `62ded3c6…2fa2`; zweiteiliges Update: `femu.bin` (126 976 Byte, SHA-256
`1f16fe32…1367`, AVR) und `firmware.xvf` (53 289 Byte, SHA-256 `df872cfc…0957`, CPLD),
dazu `readme.txt` mit der getrennten Einspielreihenfolge (erst CPLD mit NEXT+PREV, dann
AVR mit SELECT+PREV; „WRONG CPLD" bis beide Teile passen). Gleiche Werkzeugkette wie oben.

| Befund | Apple-II-Abbild 0.1R / F8 | Macintosh-Abbild 231209M |
|---|---|---|
| Datei | AVR-Flash 0x00000–0x0bf48 (≈ 48 KiB Code), Rest Nullen, Abschluss `DD DD DD DD 00 01 00 00` bei 0x1eff8; CPLD getrennt | AVR ≈ 70 KiB plus CPLD-Strom ab 0x20000 |
| Vektortabelle | gemischt `rjmp`/`jmp` (älterer Compilerstand), dieselben vier Interrupts: PCINT0 0x2aca, PCINT2 0x22a2, PCINT3 0x1fba, Timer0-Überlauf 0x7ba2 | 35 × `jmp`, gleiche vier Interrupts |
| CPLD | XSVF für XC9572XL (IDCODE `0xF9604093`), identische Befehlsfolge und Längen, aber 6 612 abweichende Datenbytes: eigene Logik „F8" für Apple II | Logik für Mac/Lisa |
| Betriebsarten (Strings) | Smartport, Apple II 5.25, Apple II 3.5; Menütexte kennen auch Lisa/Mac/Hard Disk als „nicht unterstützt" | Mac 3.5, HD20, Lisa |
| Bildtypen | 140K NIB, 140K DOS-3.3-Reihenfolge, 140K ProDOS-Reihenfolge, 2MG (NIB/ProDOS/DOS 3.3, 140K und 800K), 800K DiskCopy, 800K roh; Prüfung „not ProDOS format", „bad 2MG format" | DiskCopy, MOOF, roh, Boxing |
| Tabellen | DOS-3.3-Interleave 0x3c8 (`0 7 14 6 13 5 12 4 11 3 10 2 9 1 8 15`), ProDOS-Interleave 0x3d8 (`0 8 1 9 … 7 15`), kumulierte Mac-Sektoroffsets 0x3e8, Sektoren je Spur 16 × 12/11/10/9/8 bei 0x488, GCR-6&2-Kodiertabelle 0x514, Decodiertabelle 0x554, CRC-16-Tabelle 0x5be | gleiche GCR- und Zonentabellen, kein Interleave (Mac-Abbilder sind blockgeordnet) |
| Nutzer der Tabellen | Interleaves und 6&2 in 0x2d2e–0x2dae (Funktion 0x15ae, 660 Byte, Datenfeld-Nibblizer), 6&2 und Zonentabelle in 0x22ca (3 282 Byte, größte Funktion, 3,5-Zoll-Spuraufbau); Smartport-Paketaufbau 0x2cae (Marken `C3 80 81 84 83`) | Kodierer 0x51f2, Spuraufbau 0x65d2 / 0x7512 |
| Zonenzeiten | zwei Fünfergruppen u16 bei 0xbdcb: 403 439 483 538 604 und 394 429 472 525 590; Produkt mit 12/11/10/9/8 je ≈ 4 830 konstant, also Zeit- oder Längenwerte je Zone in zwei Varianten | im Code, nicht als Tabelle gefunden |
| Hardwaretests | `CHECK PIN CLKO-H/-L`, `CHECK PIN RESET`, `SELF-TEST OK`, `BAD FUSES`, `WRONG CPLD: %d != %d` | gleich, plus Modellbuchstabe |

Ghidra fand 190 Funktionen. Die Schnittstelle AVR↔CPLD (7 Datenleitungen, Strobe,
Quittung) ist unverändert; die Spurdaten für 5,25 Zoll entstehen aus Sektorabbildern
mit den beiden Interleave-Tabellen und derselben 6&2-Tabelle, der Lesepfad meldet
`Saved trk %02d in %lu` wie beim Mac-Abbild.

**Für UFI:** UFI kennt bisher den Macintosh-GCR-Decoder (Format 6, 6&2 mit 7-Byte-Kopf
und 524-Byte-Sektoren), aber keinen Decoder für Apple-II-5,25-Zoll-Sektoren (Kopf mit
4&4-kodierten Volume/Spur/Sektor/Prüfsumme, 343 Datennibbles, Epilog `DE AA EB`), obwohl
der Apple-Port Disk-II-Laufwerke bedient. Die beiden Interleave-Tabellen und die
Prüfregeln oben sind die Referenz dafür, falls ein solcher Decoder (als Format 7)
gebaut werden soll; aus dem Abbild übernommen wird nichts.

## Reproduktion

Scratchpad `femu/` (nicht versioniert): `femu_probe.py` (Aufbau, Entropie, Vektoren,
Strings), `femu_xsvf.py` (XSVF-Parser), `femu_tables.py` (Tabellensuche),
`femu_xref.py` (Ladestellen von Flashadressen über `ldi`- und `subi/sbci`-Paare, Zuordnung
zu Ghidra-Funktionen), `scripts/ExportDecomp.java` (Ghidra-Postskript: alle Funktionen
dekompilieren), `avr/femu_avr.dis` (`avr-objdump -b binary -m avr51 -D`),
`avr/femu_decomp.txt` (224 Funktionen). Ghidra-Aufruf:

```bash
wsl -u root -e bash -lc '/opt/ghidra_12.1_PUBLIC/support/analyzeHeadless /root/ghproj femu \
  -import femu_avr.bin -processor avr8:LE:16:extended -scriptPath <dir>/scripts \
  -postScript ExportDecomp.java <dir>/avr/femu_decomp.txt'
```

Ghidras AVR-Registernamen (`PINF`, `ADCSR`, `UBRR0L`) stammen aus dem generischen
avr8-Modell und passen nicht zum ATmega1284P; die objdump-Adressen (`0x00` PINA,
`0x02` PORTA, `0x08` PORTC) sind maßgeblich.
