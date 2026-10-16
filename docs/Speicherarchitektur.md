# Speicheraufteilung nach der Optimierung (1.18-dev)

Stand: 2026-10-13. Softwareprüfung ohne fertige Platine.

## U11 ist der externe Flux-Puffer

Die Bestückungsdateien nennen U11 als **APS6404L-3SQR-SN, 8 MiB PSRAM**.
`ufi_psram.c` initialisiert ihn über OCTOSPI1 und prüft ID, Datenleitungen,
Adressleitungen und Testmuster. Erst bei erfolgreicher Prüfung verwendet
`ufi_flux.c` den externen Speicher für Capture und Schreibdaten. Der bestehende
Schreib-DMA-Ring bleibt im internen SRAM2; er wird aus dem Flux-Puffer gespeist.

| Betrieb | Physischer Puffer | Nutzbare Flux-Wörter | Nutzdatenbytes |
|---|---:|---:|---:|
| U11 erfolgreich geprüft | 8 MiB | 2.064.384 | 8.257.536 |
| U11 fehlt oder Prüfung fehlgeschlagen | 224 KiB intern | 38.224 | 152.896 |

Die Differenz ist insbesondere ein vollständiger DMA-Auffangblock. Er verhindert,
dass ein nachlaufender DMA-Transfer gespeicherte Daten oder andere Variablen
überschreibt. Seine Größe darf nicht zugunsten einer schöneren Kapazitätsangabe
verkleinert werden. Der interne Ersatzpuffer bleibt statisch reserviert, auch
wenn U11 funktioniert. Er wird in dieser Änderung nicht mit anderen Daten überlagert.
Eine erneute Speicherwahl nach einem PSRAM-Fehler setzt Zeiger, Kapazität und
Status wieder vollständig auf den internen Ersatzpuffer zurück.

## Umgesetzte Reihenfolge

1. Die beiden CPU-verarbeiteten Empfangsringe für GW und UFI v2 (je 8192 Byte)
   liegen jetzt im DTCM. USB-Interrupts kopieren hinein; kein DMA verwendet diese
   Adressen. Die Ringe liegen in einer NOLOAD-Section. Ihre BSS-Indizes starten
   bei null; jeder gelesene Eintrag wurde vorher geschrieben und veröffentlicht.
   Retain-Daten für Bootloader und Crashbericht werden weiterhin nicht gelöscht.
2. Der Heap ist von 16 auf 8 KiB reduziert. Die ARM-Linkerdatei zeigte tatsächlich
   eingebundene newlib-Funktionen `_malloc_r`, `_realloc_r` und `_sbrk_r`; deshalb
   wurde der Heap nicht entfernt. Ein eigenes `_sbrk` begrenzt ihn auf die
   Linkersymbole `__heap_start` und `__heap_end`. Überlauf oder unzulässiges
   Verkleinern liefert ENOMEM, ohne den Heapzeiger zu verändern.
3. U11-Kapazität und interner 224-KiB-Ersatzpuffer bleiben erhalten. Flux-DMA-Daten
   werden nicht ins für DMA1/2 unzugängliche DTCM verschoben. SRAM1 und der freie
   Teil von SRAM2 bleiben als weitere Reserve verfügbar.
4. Der Linker erzwingt mindestens 32 KiB AXI-Reserve und 16 KiB Stackreserve.
   Jeder Firmware-Link prüft zusätzlich das tatsächliche ELF mit
   `firmware/tools/check_memory.py`: Heapgrenzen, Puffergrößen, Speicherbank,
   Stackabstand und Cache-Line-Ausrichtung der DMA-Puffer. Das Ergebnis steht in
   `firmware/build/memory_usage.json` und wird als CI-Artefakt veröffentlicht.

Die nachfolgende Belegung enthält auch den anschließenden Zuverlässigkeitsausbau:
Zwei zusätzliche Composite-Antwortpuffer (je 4106 Byte) liegen im DTCM und werden
von der CPU in den USB-FIFO geschrieben. Dadurch bleiben die AXI-Reserven erhalten;
der freie DTCM-Stackraum beträgt weiterhin mehr als die geforderten 16 KiB.

## Gemessene Buildbelegung

| Bereich | Vorher | Nachher |
|---|---:|---:|
| AXI-SRAM belegt, einschließlich reserviertem Heap | 307.440 B | 282.944 B |
| AXI-SRAM frei | 20.240 B | **44.736 B** |
| Statische DTCM-Daten | 81.704 B | 106.336 B |
| DTCM zwischen Datenende und Stackobergrenze | 49.368 B | **24.736 B** |
| Heapreservierung | 16.384 B | 8.192 B |
| SRAM2-Schreibring | 8.192 B | 8.192 B |

Der AXI-Gewinn beträgt **24.496 Byte**; kleine Abweichungen zu 24 KiB entstehen
durch Ausrichtung und Verwaltungsdaten. AXI ist jetzt zu 86,35 % statt 93,82 %
belegt. Der DTCM-Abstand ist verfügbarer Stackraum, keine Messung des tatsächlichen
maximalen Stackverbrauchs. SELFTEST `stack_free` liefert später den Laufzeitwert.

## Prüfung und Grenzen

`python firmware/tests/check_all.py` testet unter anderem Heap voll/leer,
fehlgeschlagene Anforderungen ohne Zustandsänderung, extreme positive/negative
Längen, PSRAM-Erfolg und -Ausfall in wechselnder Reihenfolge sowie unveränderte
Nutzkapazität und getrennten Auffangblock. Absichtlich falsche Speicherlayouts
müssen vom Budgetprüfer abgewiesen werden. Der ARM-Build verwendet nachweislich
die eigene begrenzte `_sbrk`-Implementierung.

Auf der Platine sind weiterhin U11-Stabilität und Durchsatz unter DMA-/USB-Last,
Cache-Kohärenz, Stack-Höchstverbrauch und Heapbedarf der Bibliotheksaufrufe zu
prüfen. Der Speicherplan erhöht Reserven und begrenzt Fehlerfolgen; er ersetzt
diese Messungen nicht. Ohne bestandenen U11-Selbsttest wird dessen Kapazität
nicht als nutzbarer Speicher gemeldet.

## Ergänzung: Aufnahmebelege (2026-10-14)

Die Rohindex- und Transportaufzeichnung verwendet `.evidence` in SRAM1:
240 × 16 + 48 × 24 = **4992 Bytes**. SRAM1 wird vor der ersten Aufnahme getaktet.
Die NOLOAD-Einträge werden vollständig geschrieben, bevor ihre Anzahl erhöht
wird; nur veröffentlichte Einträge gelangen in Berichte. Verwaltungsdaten und
der 160-Byte-Kontext werden regulär initialisiert. Ein Linker-ASSERT begrenzt
die Sektion auf 8192 Bytes. Diese CPU-Daten werden nicht für DMA verwendet.

Aktueller ARM-Build: AXI 290088/327680 Bytes, **37592 Bytes frei**; DTCM 104 KiB
einschließlich Heap, Stackadressraum 24576 Bytes; SRAM1 4992/16384 Bytes,
SRAM2 8192/16384 Bytes. Die bestehende 32-KiB-AXI-Mindestreserve bleibt bestehen.
U11/Fallback-Flusskapazität und DMA-Schreibpuffer werden nicht verkleinert.
Volle Diagnosepuffer verwerfen neue Einträge mit saturierendem Verlustzähler,
ohne Heapbedarf. USB-Seiten nutzen den vorhandenen 4096-Byte-Arbeitspuffer.

## Ergänzung: Umdrehungsvergleich und Inhaltsbestätigungen (2026-10-15)

Der Umdrehungsvergleich legt weitere 1568 Byte Zustand in SRAM1 ab. Zusammen
belegt `.evidence` nun 6560 Byte und bleibt unter dem 8192-Byte-Linkerlimit.
128 Fenster halten nur fehlende/zusätzliche Flanken und Revolutionsmasken;
Referenz und Vergleich lesen direkt aus der vorhandenen Aufnahme.
Für Wiederholungsbestätigungen werden bis zu 64 Sektor-ID/CRC32-Paare gehalten,
keine zweite Rohspur. AXI-Belegung: 290640 Byte, frei **37040 Byte**.
U11-Nutzkapazität, interner Flux-Fallback und DMA-Schreibpuffer bleiben gleich.

## Ergänzung: Laufwerks- und Medienzustand (2026-10-15)

Laufwerksprofile, Mediengenerationen und eingefrorene 40-Byte-Statusberichte
erhöhen die AXI-Belegung gegenüber dem vorherigen Build um 440 Byte auf
**291080/327680 Byte; 36600 Byte bleiben frei**. Die 32-KiB-Mindestreserve
bleibt erfüllt. SRAM1 bleibt bei 6560 Byte, SRAM2 bei 8192 Byte; Stackadressraum
24576 Byte und Heap 8192 Byte bleiben unverändert. Die Erweiterung benötigt
keinen zusätzlichen Rohspurpuffer und verändert die U11-Nutzkapazität nicht.

## Ergänzung: Profilprüfbericht (2026-10-16)

Der optionale CHECK_PROFILE-Bericht erhöht die persistente AXI-Belegung netto
um 32 Byte auf 291112 Byte; **36568 Byte bleiben frei**. Die Prüfung nutzt
vorübergehend 200 Byte lokale Kontextarrays zusätzlich zu ihren Skalaren;
es gibt keinen Heapbedarf und keinen weiteren Rohspurpuffer. SRAM1, U11 und
die reservierten Stack-/Heapgrößen bleiben unverändert. Der ARM-Build erfüllt
weiterhin die 32-KiB-AXI-Mindestreserve.

## Ergänzung: Betriebsprofile und Portbericht (2026-10-16)

Zwölf 40-Byte-Profilplätze und ein 24-Byte-Portbericht pro Analysejob erhöhen
die persistente AXI-Belegung um 504 Byte: **291616 Byte belegt, 36064 Byte frei**.
Profile sind statisch allokiert; U11-Kapazität, SRAM1 und DMA-Puffer bleiben
unverändert. Die native Profilauswahl nutzt kleine lokale Kopien der Einstellungen
für Validierung und Anwendung. Die Linker-Mindestreserve bleibt erfüllt;
Stack-Höchstverbrauch ist weiterhin auf der Platine zu messen.

## Ergänzung: Macintosh-GCR (2026-10-16)

Der Decoder verwendet den vorhandenen Sektorpuffer für die In-place-Umsetzung
von 6-and-2-Symbolen zu 524 Bytes Tags/Nutzdaten. Kein zweiter Sektor- oder
Spurpuffer wird angelegt. Die Kandidatenliste wächst um sechs u16-Einträge;
mit bestehendem Alignment steigt die AXI-Gesamtbelegung netto um 8 Byte auf
291624 Byte. **36056 Byte bleiben frei**. SRAM1 und U11 bleiben unverändert.
Der 48-Byte-Befundbericht wird bei Bedarf erzeugt; er benötigt keinen dauerhaft
reservierten Berichtspuffer. Die 32-KiB-AXI-Mindestreserve bleibt erfüllt.

## Ergänzung: lokale HxC-inspirierte Auswertung (2026-10-16)

32 Fenster mit je 40 Bytes Zustand benötigen zusätzlich 1280 Bytes AXI-RAM.
Die 1312-Byte-Wireantwort wird im vorhandenen Antwortpuffer aufgebaut; die
Medianberechnung braucht vorübergehend 128 Bytes für 32 Mittelwerte. Kein Heap,
kein zweiter Fluxpuffer. Flankenvergleiche verwenden den vorhandenen Zustand.
U11, DMA-Puffer und SRAM1 bleiben unverändert.

ARM-Build: **292904 Bytes AXI belegt, 34776 Bytes frei**. Damit verbleiben
2008 Bytes oberhalb der 32-KiB-Mindestreserve. Weitere persistente Analysen
müssen diese knappe Zusatzreserve berücksichtigen. Stackadressraum 24576 Bytes,
Heap 8192 Bytes; tatsächlicher Stackhöchstverbrauch bleibt hardwareabhängig.

## Ergänzung: Erkennung und Wiederholungsursachen (2026-10-16)

Acht Versuchsdatensätze zu je 20 Bytes und eine 12-Byte-Profilkopie benötigen
mit Alignment netto weitere 176 Bytes. Berichte entstehen im vorhandenen
Antwortpuffer. Kein Heap und keine zusätzlichen Flux-/DMA-Puffer.
AXI: **293080 Bytes belegt, 34600 Bytes frei** (1832 Bytes oberhalb der
32-KiB-Mindestreserve). U11 bleibt unverändert; FLASH 183868 Bytes.

## Ergänzung: Fähigkeitsmatrix und Konfigurationsgeneration (2026-10-16)

Die 224-Byte-Matrix entsteht im bestehenden USB-Antwortpuffer. Ein u32-Zähler
speichert die Konfigurationsgeneration; keine Kopien aller Laufwerksprofile,
kein Heap und kein weiterer Fluxpuffer. Durch bestehendes Alignment bleiben
die belegten Regionen unverändert: AXI **293080 belegt /34600 frei**, SRAM1
6560 Bytes. FLASH steigt auf 184744 Bytes. Die Reserve oberhalb des
32-KiB-Minimums bleibt 1832 Bytes; U11 und DMA-Puffer bleiben unverändert.

## Ergänzung: mediengebundene Starts und Parserprüfung (2026-10-16)

GUARDED_WRITE und Analyse-Op26 verwenden die bestehenden Zähler, Jobzustände
und Antwortpuffer. Keine zusätzliche persistente RAM-Belegung. Der finale Build
belegt FLASH184968 Bytes; AXI293080 belegt /34600 frei, SRAM1 6560 Bytes.
