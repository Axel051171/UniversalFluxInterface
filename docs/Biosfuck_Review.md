# biosfuck: Bewertung für UFI

Geprüft am 16.10.2026, Repository-Tree
`6033c3d9f2c6802265f65ad54da6ed875fbc8678`.
[Quellenstand](https://github.com/Moldytzu/biosfuck/tree/6033c3d9f2c6802265f65ad54da6ed875fbc8678).
Gelesen: Dateiliste, `bf.asm`, `buildrun.sh`, `LICENSE` (MIT).
Keine fremden Skripte oder Binärdateien ausgeführt.

## Inhalt und Übertragbarkeit

Ein 8086-Real-Mode-Bootsektor mit einem rudimentären Brainfuck-Interpreter.
Die vorhandenen Handler unterstützen nur Plus und Zeichenausgabe. BIOS INT 10h
übernimmt die Bildschirmausgabe. Das Buildskript verwendet NASM, QEMU und GDB.
`boot.flp` ist laut GitHub-Dateiliste nur 512 Bytes groß; es ist kein vollständiges
Diskettenabbild mit belegter Geometrie oder FAT-Dateisystem.

Der gelesene Programmcode enthält weder INT-13h-Diskettenzugriffe noch einen
FDC-Treiber, Flux-Zeitmessung, Formatdecoder oder IEC-Unterstützung. Das PC-BIOS
lädt den Bootsektor; daraus ergibt sich keine portable Lesefunktion für UFI.

## Auffälligkeiten bei statischer Prüfung

- Die Initialisierung startet CX mit 255 und vermindert es sowohl durch DEC
  als auch durch LOOP. Der Zähler bleibt an der LOOP-Prüfung ungerade und erreicht
  dort nicht 0. Die vorgesehenen 255 Initialisierungsschritte werden dadurch
  nicht korrekt begrenzt; Speicher außerhalb des Tape-Bereichs wird beschrieben.
- LODSB bewegt SI bereits, abhängig vom Direction Flag. Das zusätzliche INC SI
  überspringt bei gelöschtem DF jedes zweite Programmzeichen. CLD fehlt ebenfalls.
- Vor dem BIOS-Interrupt wird kein eigener definierter SS:SP-Stack eingerichtet.

Diese Befunde stammen aus dem Quelltext, nicht aus einem ausgeführten Bootversuch.

## Entscheidung für UFI

Keine Firmware- oder Protokollübernahme empfohlen. Ein Bootsektor könnte als
beliebige 512-Byte-Testnutzlast dienen, bietet gegenüber den vorhandenen
Codec-/Prüflesetests und externen Diskettenkorpora aber keinen erkennbaren
zusätzlichen Nachweis. Die Signatur 55 AA allein bestätigt weder Geometrie,
Dateisystem, physische Lesbarkeit noch erfolgreiche Bootfähigkeit.

Keine neuen Flags nötig. Eine spätere Bootsektorinterpretation gehört primär
in das Hosttool; die Firmware liefert bereits Sektorinhalte, Prüfsummen- und
Lesebefunde. Greaseweazle bleibt unverändert. Für dieses reine Review wurden
keine Firmwaretests erneut ausgeführt und keine Firmwaredateien geändert.
