# SuperCard Pro: Updater 1.3 und Firmware-Container zerlegt (2026-10-17)

Quelle: `scp_updater13.zip` (CBMSTUFF, Jim Drew, Updater-Build 27.04.2022) mit
`SCP_updater.exe` (VB6, FTDI D2XX) und `SCP.fwu` (262 176 Byte). Werkzeuge: Ghidra 12.1
headless in WSL (x86-PE und dsPIC33F), eigene Python-Skripte für Containerformat,
Bitpackung und Statistik (Scratchpad `scp/tools/`, `scp/scripts/`). Es wurde nichts aus
den Binärdateien übernommen; alle Aussagen sind Lesebefunde mit Adresse.

Zweck für UFI: Vergleich der Aufnahme- und Schreibmethode eines verbreiteten Flux-Geräts
mit unserer Firmware, Kenntnis des SCP-Bootloader- und Updateformats für spätere
Werkzeuge (Lesen von `.fwu`, Abgleich des SCP-Dateiformats mit der Gerätelogik).

## 1 Ergebnis in Kürze

| Punkt | Befund |
|---|---|
| Prozessor | dsPIC33FJ256GP210A, Fcy = 40 MHz (Timer 7: PR 624 × Vorteiler 64 = 1 ms; Timer 8: 62 × 64 ≈ 0,1 ms) |
| USB | FTDI-Parallel-FIFO auf PORTA[7:0], WR = LATG2, TXE# = RF7; zwei UARTs (U1/U2) für RS-232-Kommandos |
| Flux-Aufnahme | Input Capture 5 auf Timer 3, 25 ns je Tick (Vorteiler 1:1), Interrupt je Flanke, 16-Bit-Zellen big-endian, Überlauf = Zelle `0000` durch Timer-3-Überlauf-Interrupt |
| Index | Change-Notification-Interrupt; Umlaufzeit aus 32-Bit-Timer 4/5, Zähler je Umdrehung kopiert |
| Flux-Schreiben | Output Compare 8 auf Timer 3, Interrupt je Zelle, Mindestzelle 30 Ticks (750 ns), Zellen > 6000 Ticks (150 µs) werden in 50-Tick-Stücke zerlegt, Schreibschutz über RD4 |
| Datenspeicher | Externes Byte-SRAM an PORTE/LATE, Adresszähler LATB mit Bankbits LATA12/13 und LATF13, WE = LATF12; zusätzlich 10-KiB-Ring im internen RAM (0x1000–0x37FF) |
| Bootloader | Im Abbild enthalten (0x400–0x75A), ASCII-Kommandos `SI`, `RF`, `WF`, `EX`, `R!`; aktiv wenn RF4 bei Reset low ist |
| `.fwu` | 4 Byte je Klartextbyte, Schlüsselstrom aus den drei Füllbytes selbst (kein geheimer Schlüssel); Klartext = 8-Byte-Kopf `FW` + 64 KiB Programmabbild in Bootloader-Drahtform |
| Drahtform | 24-Bit-Befehl → 32 Bit: unteres Wort um 3 nach rechts rotiert, oberes Wort = rotl6(unteres Rohwort) XOR (Opcode-Byte << 6); gelöschtes Wort `FFFFFF` ↔ `C03FFFFF` |

## 2 Updater (`SCP_updater.exe`)

VB6-nativ (MSVBVM60), 17 selbst erkannte plus 13 über die VB6-Prologsignatur
(`55 8B EC 83 EC xx 68 … 64 A1 00 00 00 00`) angelegte Routinen. FTDI über
`FTD2XX.DLL` (FT_OpenEx, SetBaudRate, SetTimeouts, SetUSBParameters 64 KiB, Read, Write).

### 2.1 Ablauf

1. `Form_Load` (0x413C24): Gerät öffnen (0x412D54: `FT_Open(0)`, Timeouts 100 ms,
   Latency 2, USB-Parameter 0x10000), dann `SI` senden (0x4130EE) und 4 Byte lesen:
   `'J' 'D' hw fw`. Jedes Versionsbyte ist nibble-gepackt (`major*16+minor`).
   Antwort ohne `JD` oder Hardware 0.0 → „SuperCard Not in update mode!“.
2. Datei laden (0x41590B Dialog bzw. 0x415EEF `SCP.fwu` im Programmordner): Datei
   binär in das 262 176-Byte-Feld lesen, dann 0x41620D entschlüsseln (Abschnitt 3).
3. Prüfen (0x41564B): Klartext beginnt mit `FW`; Byte 3 = Dateiversion (hier 0x13 =
   1.3); Bytes 4–7 → 32-Bit-Wert (0x1000). Geräte-Firmware ≤ 1.1 wird als „zu alt“
   abgewiesen, Dateiversion wird gegen die Geräteversion angezeigt.
4. Flashen (0x4165F9) mit Schreibroutine 0x416EAC (`WF`, bis zu 50 Wiederholungen je
   Block) und Rücklesevergleich 0x4170B5 (`RF`, Vergleich Byte für Byte mit dem
   Klartext). Blockgröße fest 0x200 Befehle = 0x800 Byte Drahtform = eine
   Löschseite des dsPIC33F.

### 2.2 Schreibplan

Längen und Geräteadressen zählen in 16-Bit-Programmadressen (ein Befehl = 2 Adressen);
Quelloffsets zeigen in den Klartext (8 Byte Kopf + Abbild).

| Schritt | Quelle (Klartext) | Geräteadresse | Länge (Adressen) | Inhalt |
|---|---|---|---|---|
| 1 | 0x1008 | 0x000800 | 0x1000 | Interrupt-Routinen 0x800–0xA2E, Hilfsroutinen 0x1400–0x161E |
| 2 | 0x0008 | 0x000000 | 0x0200 | Reset-Vektor, IVT und AIVT |
| 3 (Gerät ≥ 1.2) | 0x4008 | 0x002000 | 0x3000 | Sprung nach `main`, Versionswörter, Hauptfirmware 0x2100–0x39EE |
| 4 (Gerät ≤ 1.1) | 0x0808 | 0x000400 | 0x0400 | Bootloader selbst (0x400–0x75A); davor `EX 07CE`, danach `EX 042C` und erneut `SI` |

Meldungen „Step 3 … DO NOT POWER OFF“ gehören zum Bootloader-Austausch alter Geräte.

## 3 Containerformat `.fwu`

```
Datei = 65 544 Gruppen zu 4 Byte  (= 262 176 Byte)
Gruppe i: d, f1, f2, f3
k1   = (f1 + f2 + f3) mod 256
k2   = acc mod 256            acc startet bei 0x55 und wächst nach jedem Byte um k1
klar[i] = d XOR k1 XOR k2
```

(Disassembly 0x41620D–0x416363; Verschlüsselungsgegenstück 0x416363 erzeugt die
Füllbytes mit VB-`Rnd`.) Die Füllbytes stehen in der Datei, der Schlüsselstrom ist also
vollständig aus ihr ableitbar; die Entropie von 7,99 Bit/Byte täuscht eine Chiffre vor.
Klartext: `46 57 00 13 00 00 10 00` + 65 536 Byte Abbild. Byte 2 ist 0, Byte 3 die
Version, Bytes 4–7 ergeben 0x1000 (vom Updater nur gespeichert).

## 4 Drahtform der Programmwörter

Das Abbild liegt so vor, wie der Bootloader Wörter sendet und empfängt (0x4D4–0x500
beim Lesen, 0x524–0x556 beim Schreiben):

```
Lesen  (RF):   W4 = low16 ror 3            W5 = (high8 XOR W4) rol 6      gesendet: W4, W5 (LE)
Schreiben (WF): W4 = raw_low rol 3          high8 = (raw_high ror 6) XOR raw_low
```

Statistisch bestätigt: alle 16 384 Langwörter ohne Fremdbits in den oberen 16 Bit,
`RETURN` (0x060000) erscheint 85-mal als `01800000`, `RETFIE` 12-mal, Reset-Vektor
`GOTO 0x404`, alle 253 `RCALL`-Ziele gerade und in Code, GOTO/CALL-Paare konsistent.
Belegte Bereiche: 0x0000–0x009E IVT, 0x0104–0x019E AIVT, 0x0200–0x0210 Sprungtabelle,
0x0400–0x075A Bootloader, 0x0800–0x0A2E Interrupts, 0x1400–0x161E Hilfsroutinen,
0x2000–0x2004 (`JD`, Version 1.3, `BRA main`), 0x2100–0x39EE Hauptfirmware; Rest
gelöscht. Konfigurationswörter sind nicht Teil der Datei.

## 5 Bootloader (0x404–0x75A)

Reset: Stack 0xF00, SPLIM 0xFF0, CORCON Bit 2, Peripherie-Init (0x57C: alle Pins digital,
TRIS A/B 0, TRISC 0x9000, TRISD 0x313A, TRISE 0, TRISF 0xFC, TRISG 0x180; LATA 0x600,
LATD/LATE 0xAC4, LATF 0x1003, LATG 0xD00F), Timer 7/8 als 1-ms- und 0,1-ms-Takt, UART1/2
mit BRG 0x103 (BRGH 0: 9615 Bd; die Hauptfirmware stellt beide UARTs mit BRGH 1 neu ein).
Ist RF4 low, bleibt der Bootloader aktiv (0x42C), sonst `GOTO 0x2100`.

| Kommando | Bytes danach | Wirkung |
|---|---|---|
| `S` `I` | – | Antwort `J` `D`, Hardware 0x11, Firmware-Byte aus Programmwort 0x2002 (hier 0x13) |
| `R` `F` | page, addr_hi, addr_lo, len_hi, len_lo | liest `len` Befehle ab TBLPAG:addr nach RAM 0x3000 und sendet je Befehl 4 Byte Drahtform |
| `W` `F` | page, addr_hi, addr_lo, dann 0x200 × 4 Byte | puffert eine Seite in RAM 0x3000, löscht sie (NVMCON 0x4042) und schreibt 8 Zeilen à 64 Befehle (NVMCON 0x4001), Freigabe 0x55/0xAA |
| `E` `X` | addr_hi, addr_lo | `GOTO` auf die 16-Bit-Adresse |
| `R` `!` | – | `RESET` |

Während der Übertragung werden LATC13/14 (LEDs) geschaltet; die Byte-Ein-/Ausgabe
(0x72E Empfang mit Carry = kein Byte, 0x74C Senden) läuft über den FTDI-FIFO.

## 6 Hauptfirmware 1.3 (0x2100–0x39EE)

### 6.1 Start und Kommandoschleife

0x2100: RAM 0x1000 ff. löschen, Parameter setzen (0x878 = 1000, 0x87A = 5000,
0x87C = 750, 0x87E = 15: Auswahl-, Schritt-, Motor- und Spur-0-Zeiten wie in den
SCP-Standardparametern; 0x822 = 20 000, 0x824 = 20 000/419), RAM-Test (0x2F2C, bei
Fehler Dauerblinken LATC13/14), zweiter Selbsttest (0x36A6, zehnmal blinken bei
Fehler). Schleife 0x216C: RF6 zeigt FIFO-Daten (USB), U1STA/U2STA Bit 0 RS-232; der
Kanal steht in RAM 0x800 Bit 7/8, dann Kommandohandler 0x2188.

0x2188: Kommandobyte nach RAM 0xD00, Längenbyte, Nutzdaten, Prüfsumme = Summe aller
Bytes + 0x4A (0x21B8). Verteilung über `BRA W0` (0x21D0). Antwort = Kommandobyte +
Status, 0x4F = ok; Fehlerwerte 1–6 für unbekanntes Kommando, Länge, Prüfsumme usw.,
0x09 kein Index, 0x0C falsche Länge, 0x0F Schreibschutz, 0x11 nicht bereit (Werte wie
im veröffentlichten SCP-Protokoll).

### 6.2 Flux lesen

Aufnahmeflags (RAM 0x86A): Bit 0 Start am Index, Bit 1 8-Bit-Zellen, Bits 2–3
Auflösungsschieber (Zelle = Capture >> n, also 25/50/100/200 ns), Bits 4–6 weitere
Optionen. Einrichtung (0x294A–0x29B6): T3CON 0 (Vorteiler 1:1 → 25 ns), PR3 0xFFFF,
IC5CON 2 (jede fallende Flanke), Prioritäten 6, Index-Warten bis 500 ms (Timer 7), danach
Fehler 0x09 (kein Index) oder 0x11.

Vier Capture-Handler, per `GOTO W13` aus der IC5-ISR (0x88C, setzt TMR3 = 0 nach jeder
Flanke) angesprungen:

| Handler | Ziel | Zelle |
|---|---|---|
| 0x890 | interner Ring 0x1000–0x37FF | 16 Bit (IC5BUF + 8) >> n; beim Index Marke 0xFF00 |
| 0x8C0 | interner Ring | 8 Bit; beim Index 0xFF, 0x00 |
| 0x8FC | externes SRAM | 16 Bit big-endian (hi, lo), Bankfortschaltung über LATA13/12 und LATF13, Überlaufzähler 0x840 |
| 0x93E | externes SRAM | 8 Bit |

Timer-3-Überlauf-ISR (0x9EA) schreibt die Zelle `00 00` ins SRAM und zählt 0x840 (bzw.
0x83E wenn Z gesetzt): das ist die SCP-Dateiregel „0x0000 = 65 536 Ticks ohne
Flanke“. Index-ISR (0x85A, Change Notification): liest TMR4/TMR5HLD (32-Bit-Umlaufzeit)
nach 0x838/0x836, setzt TMR4 auf 10 neu, kopiert die Zähler 0x83E/0x840 nach
0x83A/0x83C und setzt Flag Bit 2; RD13 ist die Indexleitung. SRAM voll → RAM 0x800 Bit 4.

Folgen für die Messgenauigkeit: Jede Flanke kostet eine ISR mit rund 20 Befehlen; der
Zähler wird in der ISR genullt, Totzeit zwischen zwei Flanken geht also in die nächste
Zelle ein. Kürzere Abstände als die ISR-Laufzeit (um 0,5–0,75 µs) werden verschluckt.

### 6.3 Flux schreiben (0x327E)

Prüfungen: RD4 = Schreibschutz (Status 0x0F), Zellenzahl 0 oder Hi-Wort > 7 (0x0C).
TMR1 und Zähler löschen, LATE als Eingang (SRAM lesen), optional Motor/Select, Timer 3
Vorteiler 1:1, PR3 = OC8RS = 200 (Vorlauf 5 µs), OC8CON Modus 0xD (Timer 3), Interrupt
Priorität 6, Start am Index wenn Flag 0x82C Bit 0 (500 ms Timeout), Schreibtor LATD2.

OC8-ISR (0x972): nächste 16-Bit-Zelle aus dem SRAM (hi, lo, Adresszähler LATB mit
Banküberlauf wie beim Lesen), Zelle − 1 nach PR3/OC8RS, Minimum 29 (30 Ticks = 750 ns);
Zellen über 6000 Ticks: PR3 = 49, dann busy-wait auf (Zelle/50) OC8-Perioden, Rest
normal. Nach der letzten Zelle (Zähler 0x832/0x834) Schreibtor LATD2 setzen und Flag
Bit 5 melden. Es gibt keine Vorkompensation und kein Verifikationslesen in der Firmware.

### 6.4 Interruptbelegung

| Vektor | Quelle | Routine |
|---|---|---|
| Traps 0x06–0x0C | Oszillator, Adress-, Stack-, Rechenfehler | 0x802/0x812/0x822/0x832 |
| IRQ 3 | Timer 1 | 0x2004 |
| IRQ 8 | Timer 3 Überlauf | 0x9EA |
| IRQ 19 | Change Notification (Index) | 0x85A |
| IRQ 39 | Input Capture 5 (Flux) | 0x88C |
| IRQ 44 | Output Compare 8 (Schreiben) | 0x972 |
| alle übrigen | – | 0x800 (`RETFIE`) |

## 7 Vergleich mit UFI

| Thema | SuperCard Pro 1.3 | UFI 1.18-dev |
|---|---|---|
| Zeitbasis | 40 MHz, 16-Bit-Zellen, Überlaufmarke 0x0000 | 275 MHz, 32-Bit-Intervalle, kein Überlauf im Format |
| Erfassung | Interrupt je Flanke, Software schreibt ins SRAM | Timer-DMA in Ringpuffer, CPU frei |
| Index | CN-Interrupt, 32-Bit-Timerwert, Zähler je Umdrehung | Timer-Capture im gleichen Zeitraster wie der Fluss |
| Schreiben | OC-Interrupt je Zelle, Mindestzelle 750 ns, 150-µs-Stückelung | DMA-Zeitgeber, 440–65 536 Ticks, Streaming über eine Umdrehung |
| Prüflesen | keines in der Firmware | Firmware-Prüflesen, Vergleich, Qualitätsbericht |
| Update | ASCII-Bootloader, Seite 512 Befehle, Rücklesen durch Host | DFU/Datei auf SD |

Die SCP-Dateiregeln (16-Bit-Zellen big-endian, 0x0000 als Überlauf, Indexzeiten je
Umdrehung) sind damit direkt aus der Gerätelogik belegt, nicht nur aus der
Formatbeschreibung; der `scp.py`-Import in `ufi_host` entspricht ihnen.

## 8 Offene Punkte

- Der zweite Selbsttest (0x36A6) und die Sprungtabelle 0x200 (Einsprünge in die
  Capture-Handler) sind nicht weiter verfolgt.
- Die Bedeutung der Aufnahmeflag-Bits 4–6 und der Parameter 0x822/0x824 ist nicht
  belegt.
- Konfigurationswörter (Oszillator, Schutzbits) liegen außerhalb der Datei.
