# ADF-Drive-Firmware: Vergleich mit UFI

Stand: 2026-10-13. Geprüfter externer Commit:
[`97b5b213ba41bacc890b87f19deca1ac7603aa19`](https://github.com/Niteto/ADF-Drive-Firmware/tree/97b5b213ba41bacc890b87f19deca1ac7603aa19).
Quellcodeprüfung, kein Build des Fremdprojekts und kein Hardwaretest.

## Ergebnis und Lizenz

Das Projekt eignet sich als Vergleich für Amiga-Sektoren, Prüflesen und
Spurdiagnose. Es ist kein austauschbarer Treiber für UFI: `platformio.ini`
verwendet Arduino, Teensy/MK20DX256 mit 96 MHz und USB MTP plus Serial.
UFI verwendet STM32H723, Timer/DMA, Greaseweazle und das native USB-Protokoll.

UFI trägt im Wurzelverzeichnis eine MIT-Lizenz. Das Fremdprojekt enthält
GPLv3 in `LICENSE`; die untersuchte eingebettete ADFLib-Datei
`lib/adflib/adf_file.cpp` trägt GPL-2.0-or-later. Fremdcode darf deshalb
nicht einfach als MIT-Code in UFI erscheinen. Vor einer direkten Übernahme
sind die jeweiligen Dateirechte und die Bedingungen für die Auslieferung
des kombinierten Werks zu klären. Für die folgenden Vorschläge ist keine
Codekopie nötig. Es wurde kein Fremdcode in Produktionsdateien übernommen.

## Konkrete Verbesserungen, in empfohlener Reihenfolge

1. **Amiga-Prüflesen im erweiterten Host-Schreibpfad.**
   `writeWithVerify()` in `src/floppy_rw.cpp` liest nach dem Schreiben zurück;
   `decodeTrack(..., compOnly, ...)` in `src/utility_functions.cpp` vergleicht
   540 dekodierte Bytes einschließlich Metadaten und Nutzdaten.
   UFI besitzt Amiga-Encoding/Decoding in `ufi_mfm.c` und firmwareseitige
   Inhaltsprüfung in `ufi_verify.c`. `ufi_host/readback.py` unterstützt bislang
   nur IBM MFM. Dort sollte ein eigener Amiga-DD/HD-Decoder ergänzt werden:
   Header- und Datenprüfsumme, Spur/Sektor, 512 Nutzdatenbytes, widersprüchliche
   Duplikate und unvollständige Spuren prüfen. Für eine vollständige
   Amiga-Sektorprüfung auch die 16 Labelbytes erfassen; der aktuelle UFI-
   Decoder liefert sie nicht an den Verifikations-Callback weiter.
   Normale ADF-Nutzdatenprüfung und vollständige Sektorprüfung müssen als
   unterschiedliche Zusagen dokumentiert werden.

2. **Spurqualität und begrenzte adaptive Wiederholungen.**
   Das Fremdprojekt sammelt Intervallhistogramme, verschiebt Trennschwellen
   mit `adjustTimings()` und protokolliert Leseversuche in `weakTracks`.
   UFI besitzt bereits eine histogrammbasierte Ratenschätzung in
   `verify_guess_rate()`. Sinnvolle Ergänzung: Qualität je Sektor und Umdrehung,
   fehlende/fehlerhafte/widersprüchliche Sektoren und begrenzte erneute
   Dekodierung mit unterschiedlichen PLL-Parametern auf denselben Rohdaten.
   Rohdaten unverändert archivieren; abweichende gültige Sektoren nicht
   stillschweigend zu einem scheinbar eindeutigen Ergebnis zusammenführen.

3. **Spurlänge und Schreibnaht gezielt prüfen.**
   `floppyTrackMfmEncode()` legt den Gap vor die Sektoren, damit ein
   Überlappen beim Schreiben den Gap trifft. Für UFI daraus Tests ableiten:
   verschiedene gemessene Umdrehungszeiten, erster/letzter Sektor,
   Indexversatz und DD/HD. Feste Teensy-Timingwerte nicht übernehmen.
   Dies ist eine zusätzliche Prüfung, kein nachgewiesener UFI-Fehler.

4. **Unabhängige Amiga-Testvektoren.**
   Zusätzliche Vektoren mit nichtleeren Labels, verschobenen Syncs,
   beschädigten Prüfsummen, fehlenden und doppelten Sektoren sowie Jitter.
   Erwartete Ergebnisse unabhängig vom UFI-Encoder erzeugen oder aus
   belegten Referenzen gewinnen; Herkunft und Lizenz der Fixtures festhalten.
   Ein erfolgreicher eigener Encoder/Decoder-Rundlauf allein genügt nicht.

## Nicht direkt übernehmen

- **Timer-/ISR- und Bitband-Code:** `setupFTM0()`, `ftm0_isr()` und
  PIT-Schreiben sind MK20-spezifisch. Im Lesepfad werden Intervalle bereits
  auf MFM-Zelllängen reduziert und andere verworfen. Das ist kein Ersatz
  für UFI-Rohflux-Erfassung und U11-Pufferung.
- **MTP als Ersatz für USB:** MTP ist ein anderer Dateitransport und löst
  weder Greaseweazle-Kompatibilität noch native Flux-/IEC-Jobs. Ein zusätzlicher
  MTP-Modus würde weiteren Firmware-, RAM- und Testaufwand erzeugen.
- **Destruktive Dichteprüfung:** `probeTrack()` ruft mehrfach `eraseTrack()`
  auf. Diese Funktion darf nicht in die normale Medienerkennung gelangen.
  Das Projekt besitzt daneben auch ein separates lesendes `densityDetect()`.
- **Ungeprüfte Schreibwiederholungen:** `writeWithVerify()` wertet den
  Rückgabewert von `writeTrack()` nicht direkt aus. Für UFI müssen
  Schreibschutz, Abbruch und Schreibfehler unmittelbar erhalten bleiben;
  ein alter, zufällig passender Disketteninhalt belegt keinen erfolgreichen
  neuen Schreibvorgang.
- **ADF-Dateisystem im MCU:** Die mitgelieferte ADFLib ist für Dateizugriff
  interessant, aber für UFI zunächst besser auf dem Host aufgehoben.

## Quellen

- [Lizenz](https://github.com/Niteto/ADF-Drive-Firmware/blob/97b5b213ba41bacc890b87f19deca1ac7603aa19/LICENSE)
- [Plattform](https://github.com/Niteto/ADF-Drive-Firmware/blob/97b5b213ba41bacc890b87f19deca1ac7603aa19/platformio.ini)
- [Lesen und Schreiben](https://github.com/Niteto/ADF-Drive-Firmware/blob/97b5b213ba41bacc890b87f19deca1ac7603aa19/src/floppy_rw.cpp)
- [Decoder, Encoder und Histogramme](https://github.com/Niteto/ADF-Drive-Firmware/blob/97b5b213ba41bacc890b87f19deca1ac7603aa19/src/utility_functions.cpp)

Die vier Vorschläge wurden anschließend umgesetzt: siehe
[Amiga-Prüflesen und Spurqualität](Amiga_Prueflesen.md) für aktuellen Umfang,
Bedienung und Tests. Die obigen Befunde beschreiben den Zustand zum Zeitpunkt
des Vergleichs. Elektrische Timings, Schreibnaht und reale Laufwerksverträglichkeit
bleiben bis zur fertigen Platine unbestätigt.
