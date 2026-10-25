# Cortex-M7: Aufnahmeintegrität, Messung und Hardwarequalifikation

Stand: 23.10.2026; begonnen am 22.10.2026. Zielhardware bleibt das vorhandene
STM32H723ZGT6-Board mit U11 APS6404L und 8 MiB externem RAM. Lesen und Schreiben
bleiben vorgesehen. Änderungen betreffen Firmware, Prüfprogramme und Dokumentation;
die Produktsoftware auf dem PC wurde für diesen Schritt nicht verändert.

**Ergebnis:** Die unten aufgeführten Softwareprüfungen bestehen. Alle elektrischen,
mechanischen und zeitlichen Hardwareprüfungen stehen auf **NOT_RUN**. Dies ist keine
Freigabe zum Beschreiben wertvoller Disketten und kein Nachweis eines Vorsprungs
gegenüber KryoFlux.

## Umgesetzte Firmwareänderungen

- TIM2 überwacht Overcapture des aktiven RDATA-Kanals CH1 beziehungsweise Apple CH3
  und des tatsächlich verwendeten physischen INDEX-Kanals CH2. Alte Flags werden
  beim Scharfschalten gezielt gelöscht. TIM2-SR wird entsprechend seiner
  Write-zero-to-clear-Semantik beschrieben, ohne Read/Modify/Write zum Quittieren.
- Verlustprüfungen liegen im Indexpfad, am Eintritt des Capture-DMA-Handlers und
  im Pollpfad. Auch letzter Index und zeitgesteuertes Ende dürfen einen erkannten
  Fehler nicht durch einen erfolgreichen Abschluss überschreiben. Es entstehen
  keine Interrupts je Flux-Flanke.
- Stoppen sperrt zuerst den Capture-Eingang und DMA-Anforderungen, beendet laufende
  DMA-Transfers und bestimmt dann das erhaltene Fenster. Scratch-Daten sind davon
  ausgeschlossen. Vor Überlauf des logischen 32-Bit-Samplecursors endet ein langer
  Stream ausdrücklich mit BUFFER_FULL.
- Erkannte RDATA-/INDEX-Verlustepisoden werden pro Aufnahme und Kategorie höchstens
  einmal gezählt. Sie bezeichnen weder die genaue Anzahl noch die Position aller
  verlorenen Ereignisse. Ein Fehler endet mit Status6 (DMA), nicht mit Erfolg.
- Native DIAGNOSTICS `0D`, Nutzlast `02`, liefert sechs DWT-Messbereiche sowie
  Verlustzähler. Leer und `01` bleiben unverändert. Die Firmware setzt CYCCNT für
  die Messung nicht zurück, prüft Zählerfortschritt und kennzeichnet unbrauchbare
  Zeiträume, Taktwechsel und Sättigung. Greaseweazle-Befehlsformate bleiben erhalten.
- Op48 kann mit ausdrücklichem Selektor1 das erhaltene unvollständige Fenster einer
  Hardware-Verlustaufnahme liefern. Es enthält rohe absolute TIM2-u32-Zeitstempel,
  Generation, Verlustmaske und Fingerabdruck. Das normale Archiv und Op52-Replay
  akzeptieren dieses Fenster nicht als vollständige Aufnahme. Andere Abbrucharten
  erhalten dadurch nicht automatisch ein Rohdatenarchiv.
- Eine eigene Buildoption erzwingt den internen Puffer für reproduzierbare Tests
  des Fallbackpfads, ohne U11 auszulöten. Die normale Firmware wählt weiterhin U11
  beziehungsweise bei fehlgeschlagener Initialisierung den Fallback.

Exakte Nutzlasten und Grenzen: [USB-Protokoll, Op48 und Abschnitt5.1](USB_Protokoll.md).
Die bereits vorhandenen Blockverarbeitung, begrenzten Cache-Invalidierungen und
DMA-Puffer bleiben erhalten. Im neuen Messpfad gibt es keine Heap-Allokation oder
USB-/Textausgabe. Ein Verschieben nach ITCM, O3, LTO oder Assembler setzt einen
gemessenen Engpass voraus; es wird hierdurch nicht pauschal aktiviert.

## Messwerte richtig interpretieren

Die aktuelle Konfiguration verwendet 550 MHz CPU und 275 MHz für TIM2/TIM3:
ein nomineller Timer-Schritt entspricht etwa 3,64 ns. Das ist Auflösung, keine
gemessene Genauigkeit. Eingangsschaltung, Quelltakt, Filter und Startverhalten
müssen zusätzlich geprüft werden. USB OTG_HS benutzt den internen Full-Speed-PHY;
der Name der Peripherie bedeutet auf diesem Board keine 480-Mbit/s-Verbindung.

DIAGNOSTICS02 misst Hauptschleife, Capture-DMA-Treiber, Write-DMA-Treiber,
Streaming-Service, Analyseschritt und Prüfleseschritt. Maxima sind Zyklen je Aufruf,
nicht je Sample. Aufrufzähler zählen nur gültig gemessene Abschnitte. Vordergrundwerte
enthalten Unterbrechungen; verschachtelte Bereiche dürfen nicht addiert werden.
IRQ-Eintritt/-Austritt und Messverwaltung sind nicht vollständig eingeschlossen.
Eine Messung mit angehaltenem DWT oder unzuverlässigem SysTick ist kein Nachweis.

**Offene Startgrenze:** Beim externen Index wird RDATA derzeit in dessen Interrupt
scharfgeschaltet. Zwischen physischem Index und Scharfschalten liegt eine noch
ungemessene Verzögerung. Overcapture erkennt keine davor eingetroffenen Flanken.
Deshalb werden Vollständigkeit ab physischem Index und garantierte Zeitgrenzen
noch nicht behauptet. Ein unzureichendes Messergebnis erfordert eine gesonderte
Änderung der Startarchitektur, etwa Vorlaufaufnahme mit Zuordnung zum Index.

## Speichermatrix

Die Firmware reserviert einen vollständigen DMA-Scratchblock. Qualifiziert wird
die nutzbare Kapazität, nicht die Nennkapazität des Bauteils:

| Zustand | Nutzbare Capture-Samples | Nutzbare Capture-Bytes |
|---|---:|---:|
| U11 vollständig verfügbar | 2064384 | 8257536 |
| U11 mit reservierter Referenzhälfte | 1015808 | 4063232 |
| Interner Fallback | 38224 | 152896 |

Die Referenzhälfte bietet zusätzlich 1048576 Samples. Der interne Speicherbereich
ist insgesamt 224 KiB groß; dieser Wert ist keine nutzbare Capture-Kapazität.
Aktuelle Kapazität immer aus DIAGNOSTICS/WRITE_LIMITS abfragen, nicht fest einbauen.

Bei Ereignisrate R und B freien Samples ist B/R nur eine obere Grenze für eine
überbrückbare Abnahmepause. Vorhandener Rückstand, Scratch, Verwaltungszeiten und
Transport-Timeouts müssen berücksichtigt werden. U11 kann Pausen abfangen, aber
keinen dauerhaft zu langsamen USB-Pfad ausgleichen.

Fallback-Testabbild separat bauen; den Cube-Pfad gegebenenfalls anpassen:

```powershell
cmake -S firmware -B firmware/build/qualification_fallback -G Ninja `
  -DCMAKE_BUILD_TYPE=Release -DUFI_FORCE_INTERNAL_FLUX_STORE=ON `
  -DSTM32CUBE_PATH=C:/Users/Axel/STM32Cube/Repository/STM32Cube_FW_H7_V1.11.0
cmake --build firmware/build/qualification_fallback --parallel 4
```

Dieses Abbild meldet `PSRAM bypassed (qualification)`. Das ist beabsichtigtes
Umgehen von U11, kein Nachweis eines defekten Chips. Buildoption und BIN-Hash
gehören ins Messprotokoll; das Standardabbild bleibt ohne diese Option.

## Elektrische Voraussetzungen vor dem ersten Lauf

Status: **BLOCKED / Sicht- und elektrische Prüfung ausstehend**. Das vorhandene
Foto bestätigt keine Betriebsbereitschaft. U11 ist als AP Memory 6404L-3SQR lesbar.
Mehrere FDD-Widerstandsnetzwerke sowie mutmaßlich C46/C50 sind auf dem Foto nicht
bestückt; Funktion und Bestückung anhand Schaltplan/BOM prüfen. Dasselbe gilt für
J11 WRITE LOCK und die tatsächliche Stellung von J18. J2 und J7 sind je nach
Versorgung beziehungsweise gewünschtem Anschluss optional. Keine fremden
KiCad-Änderungen wurden für diese Arbeit verändert.

1. Erforderliche Bestückung, Pin1, Lötbrücken und Widerstand zwischen Versorgung
   und Masse prüfen. Versorgung strombegrenzt und zur gewählten Einspeisung passend
   aufbauen; keine Versuchsschritte mit absichtlich falscher Polarität.
2. Laut Projekt: J17 Pin1=5V, Pin2/3=GND, Pin4=12V. J18 1–2 wählt Buck,
   2–3 externe5V; ohne Brücke besteht der USB-Fallback. Vor Einspeisung mit der
   tatsächlichen Platinenrevision abgleichen. TP1=3,3V, TP2=5V, TP3=12V, TP4=GND.
3. Zunächst ohne Laufwerk Rails, Stromaufnahme, Reset und SWD prüfen, danach USB.
   Laufwerksversorgung und gemeinsame Masse ausdrücklich prüfen.
4. Erst danach Signalgenerator beziehungsweise Laufwerk anschließen. Signalpegel
   müssen zur jeweiligen Eingangsschaltung passen; Versorgungsanschlüsse sind
   keine Signaleingänge. Für reine Lesetests Schreibsperre aktiv halten.

## Reproduzierbare Prüfungen nach Aufbau

Jeder Lauf erhält BIN-SHA256, Compiler/Profil/Optionen, Board- und Siliziumrevision,
U11-Status, Kapazität/Referenzreservierung, Quelle und deren Genauigkeit, Kabel,
Laufwerks-/Medienkennung, Temperatur sowie DIAGNOSTICS leer/01/02 vorher/nachher.
Rohdaten samt SHA256, Generator-Sollfolge, Parameter, Laufzeit und Ergebnis
PASS/FAIL/NOT_RUN/UNQUALIFIED aufbewahren. Nicht ausgeführte Felder bleiben leer;
Software-Sollwerte werden nicht als Hardware-Messwerte eingetragen.

### Endliche gepufferte Erfassung

Status: **NOT_RUN**. Benötigt werden zwei synchronisierte Generatorausgänge für
RDATA und INDEX sowie ein unabhängiges Messgerät. Das Testadapter muss TRK0 passend
bereitstellen, damit die Positionierung ohne angeschlossenes Laufwerk funktioniert;
am FDD-Port J6 liegt TRK0 auf Pin26 und ist aktiv niedrig. Pegel, Pinbelegung und
Inversion der tatsächlichen Platine vorher kontrollieren.

- FDD-Port, Spur0/Seite0, kein Viertelspurmodus, kein Hardsector, INDEX_SIM aus.
  Geprüfte aktive Flanke am Anschluss beachten: Die LVC14-Inversion ist im
  Defaultpfad zwischen externer fallender und TIM2 steigender Flanke enthalten.
- INDEX regelmäßig alle200ms liefern, einschließlich Vorlauf. Der bestehende
  Timeout ohne Index beträgt600ms; ein künstlicher Abstand von2s wäre unzulässig.
- Eine Umdrehung gepuffert mit ANALYZE/DETAIL beziehungsweise Analysejob aufnehmen,
  ohne EXPECT_SET, Wiederholungsstrategie oder verlangte Inhaltsbestätigung.
  Ein expliziter Formathinweis begrenzt gegebenenfalls die Decodersuche; synthetische
  Pulse brauchen keine gültigen Sektoren zu bilden. Aufnahmeintegrität separat
  vom Decoderergebnis bewerten. Sauberes Op48-Archiv vor neuer Aufnahme abholen.
- RDATA-Burst50ms nach Startindex beginnen. Raten100k,250k,500k und1M Ereignisse/s
  jeweils prüfen: bei U11 N=10000/25000/50000/100000, entsprechend100ms Burst;
  beim Fallback jeweils N=2000. Alle Bursts enden vor dem nächsten Index und passen
  in den jeweiligen Puffer. Die Raten sind Testpunkte, keine zugesicherte Grenzrate.
- Anzahl und sämtliche Differenzen modulo2^32 gegen die Generatorfolge vergleichen,
  nicht nur die Summe. Gleichmäßige und wechselnde Intervalle, lange Lücken,
  Timerumbruch, wiederholte Starts/Stops und Abbruch prüfen. Für jede Kombination
  mindestens100 Läufe; alle drei Speicherzustände getrennt protokollieren.
- Grenzfälle der Kapazität gesondert prüfen: bis knapp unter der abgefragten
  Kapazität muss die gespeicherte Sollfolge stimmen; oberhalb muss ein klarer
  Fehler folgen. Kein stilles Abschneiden als Erfolg. INDEX dabei weiter regelmäßig
  liefern und angeforderte Umdrehungen dem Burst anpassen.

Erfolg verlangt exakte Ereignisanzahl und Sollabstände innerhalb eines vor dem
Lauf festgelegten Fehlerbudgets, keinen unerklärten Verlust und keinen falschen
Erfolgsstatus. Das Fehlerbudget enthält Generator-/Referenzunsicherheit und
gemessenen Frequenzfehler. Nominelle3,64ns allein erfüllen diese Prüfung nicht.

### Indexgrenze und Fehlererkennung

Status: **NOT_RUN**. Einzelne RDATA-Flanken relativ zum physischen Index von
−10µs bis+10µs in1µs-Schritten verschieben; bei Bedarf Bereich/Schrittweite erweitern.
Erste und letzte akzeptierte Flanke am Messgerät und im Archiv vergleichen,
unter USB-/Analyse-Last wiederholen. Startlatenz und Streuung ausdrücklich berichten.
Bleibt ein unzulässiges Loch, ist die Anforderung vollständiger Spuraufnahme offen.

RDATA-Überlast, schnelle INDEX-Folgen und Stoppen unmittelbar am letzten Index
getrennt auslösen. Erwartet werden eindeutiger Fehler, passende Episodenzähler
und kein sauberes Archiv. Wenn Overcapture auftritt, Schema2-Fenster, Fingerabdruck,
Seiten-CRC, Verlustmaske und Ablehnung durch Op52 prüfen. Einzelne überlange Lücken
sind ohne bekannte Sollfolge nicht automatisch als verlorene Flanken erkennbar.

### USB-Pausen und Dauerbetrieb

Status: **NOT_RUN**. Endliche gepufferte Aufnahme und gleichzeitiges Dauerstreaming
getrennt prüfen. Abnahme jeweils10/100/250/1000ms pausieren, danach fortsetzen;
zusätzlich10s kontinuierlich streamen. Rate, maximale Belegung, Aufrufmaxima und
Endstatus aufzeichnen. Native TX hat weiterhin einen Stall-Timeout von über1000ms;
1000ms ist deshalb ein Grenz-/Negativtest und kein zugesicherter Erfolgsfall.

Erwartung: unveränderte Daten oder eindeutiger Fehler und anschließende Erholung,
niemals stiller Verlust. Höchste vollständig bestandene Rate und längste bestandene
Pause je Speicher-/USB-Zustand angeben. Auch negative Versuche dokumentieren.
Aus DWT-Maxima keine durchschnittliche CPU-Auslastung oder garantierte Worst-Case-
Laufzeit ableiten; zusätzlich reale DMA-Blockabstände und Reserven messen.

### Schreiben, Rücklesen und Vergleich

Status: **NOT_RUN**. Erst nach bestandener Lese-/Elektroprüfung mit entbehrlichen
Medien testen. WDATA und WGATE unabhängig messen: bekannte Intervalle, Startversatz,
Indexschnitt, Präkompensation, lange stille Segmente, Abbruch und Unterlauf.
Bei Unterlauf/Abbruch muss WGATE entsprechend der definierten Abschaltfolge enden.
Schreibimpulse direkt mit Sollfolge vergleichen, danach striktes Firmware-Prüflesen
und einen unabhängigen Leser verwenden. Weak-Muster statistisch und getrennt von
normalen Spuren beurteilen. Selbst erfolgreiches eigenes Prüflesen ersetzt keine
unabhängige Prüfung der geschriebenen Daten.

Ein KryoFlux-Vergleich verwendet dasselbe Laufwerk, dieselben Medien/Einstellungen
und dasselbe Zeitbudget, wechselnde Reihenfolge A/B und B/A sowie gute und schwierige
Spuren je unterstütztem Format. Unabhängig bekannte Sektorinhalte, benötigte Versuche,
Zeit und reproduzierbare Fehler zählen; CRC allein reicht bei falschen Inhalten nicht.
Firmwareversion und tatsächliche Zeitbasis des Vergleichsgeräts dokumentieren.
Erst daraus lässt sich ein Vorteil für einen konkret benannten Bereich ableiten.

## Ausgeführte Softwareprüfungen

| Prüfung | Ergebnis am23.10.2026 |
|---|---|
| `check_all.py --require-corpora --jobs 4` | 32/32 Gruppen,0 übersprungen |
| Native C-Suiten | 25 bestanden; darunter9 Profil- und31 Metrikprüfungen |
| Bestehende Host-Regressionen | 197 bestanden; keine Änderung der Produktsoftware |
| Unabhängige externe Korpora | alle5 bestanden |
| IEC-/6502-Prüfung | 23/23 bestanden |
| Statische Analyse mit tatsächlichen ARM-/Cube-Headern | alle59 Projektmodule bestanden |
| Linux GCC ASan/UBSan | alle25 nativen Suiten sowie Capture-/Analysejob-/Protokolltests bestanden |
| Native-v2-Fuzzer | 2272246 Läufe in92s, einschließlich neuer Mutationen |
| Greaseweazle-Fuzzer | 283449 Läufe in31s, einschließlich neuer Mutationen |
| ARM Release/O2, Standard und erzwungener Fallback | beide gebaut, Speicher-/Einzelstack-Gates bestanden |

Capture-Tests führen tatsächliche Guard-/Stop-/Index-/Deadline-Funktionen mit
Peripheriemodellen aus. W0C und DMA-Stillstand werden ausdrücklich modelliert;
die Tests ersetzen keine Messung des Busses. Fuzzer prüfen ihre eingebundenen
Parserpfade, nicht den vollständigen MCU-Betrieb. Die übrigen vier Fuzzertargets
wurden für diesen Schritt nicht erneut ausgeführt. CI wurde lokal vorbereitet,
aber nicht remote ausgeführt. Keine Hardware wurde durch diese Prüfungen geflasht.

Logs: `firmware/tests/build/qualification_{gate,static,build,asan_final,v2_fuzz,gw_fuzz}.log`;
Gate-Snapshot: `firmware/tests/build/qualification_gate.json`. Buildberichte liegen
als `memory_usage.json` und `stack_usage.json` im jeweiligen Buildverzeichnis.

| Ressource | Standard | Interner Testbuild |
|---|---:|---:|
| Flash | 228380B | 224156B |
| AXI belegt / frei | 290992 /36688B | 290896 /36784B |
| DTCM statisch / Stackraum | 109984 /21088B | 109984 /21088B |
| SRAM1 /SRAM2 /SRAM4 | 16040 /8192 /15668B | 16040 /8192 /15668B |
| Heap | 8192B | 8192B |
| Größter gemessener Projekt-Stackframe | 848B | 848B |

Standard:731 Funktionen mit Stackbericht. Einzelne Frames sind keine kumulative
Aufrufketten-/IRQ-Stackgrenze. Gegenüber dem vorherigen Cortex-M7-Stand entstehen
im Standardbuild3120B mehr Flash und128B mehr AXI-Belegung. Der frische Fallbackbuild
meldet eine bestehende `-Wmisleading-indentation`-Warnung in der externen Cube-FatFs-
Datei `ff.c`; keine neue Projektwarnung wurde festgestellt.

BIN-SHA256, Standard `firmware/build/ufi_firmware.bin`:
`1de7b1105784134979fb7d1e47a694314c11d594b37d9db560a53aadc709722b`.

BIN-SHA256, Testbuild `firmware/build/qualification_fallback/ufi_firmware.bin`:
`30e7a3d639d60f4817b122a59d5504d0f63b166ed425d23c39604aa4dd20e1c2`.

## Technische Grundlagen und verbleibende Grenzen

Register-/Timer-/DMA-Zuordnung wird für den H723 gegen
[ST RM0468 im offiziellen Dokumentationsverzeichnis](https://www.st.com/en/microcontrollers-microprocessors/stm32h723-733/documentation.html)
geprüft. Cache-Kohärenz ist ausdrücklich Teil des Datenpfads, siehe
[ST AN4839](https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf).
Die reale DMA-/Cache-Funktion auf U11, Taktgenauigkeit, Indexstartlatenz,
Spitzendurchsatz, Schreibsignal und kumulierter Stack unter IRQ-Verschachtelung
bleiben unqualifiziert. Die Softwareänderungen machen diese Punkte prüfbarer;
ein Hosttest kann sie nicht physisch nachweisen.
