# Häufige Fragen

**Sprache / Language:** [Deutsch](FAQ) | [English](FAQ-EN)

## Kann ich die Sprache wechseln

Ja. Oben auf jeder Seite öffnet „Deutsch | English“ dieselbe Anleitung in der anderen Sprache. Die Inhalte und Platinenbeschriftungen stehen in beiden Sprachen bereit. Die Auswahl erfolgt per Klick und wird nicht automatisch anhand der Browsersprache umgeschaltet. Technische Quelldokumente im Hauptrepository können weiterhin deutsch sein.

## Welchen Vorteil hat UFI gegenüber anderen Flux-Controllern

Vor allem die Kombination mehrerer Laufwerksanschlüsse mit Standalone-Speicherung, LAN, Versorgungsüberwachung und offener Firmware. Ein feineres natives Timerraster kann außerdem bei Timinganalysen helfen. Die Unterschiede zu KryoFlux, Greaseweazle, SuperCard Pro, Applesauce+ und FluxEngine stehen mit Quellen auf [Vorteile und Vergleich](Vorteile-und-Vergleich). Höhere Rettungsquote oder Geschwindigkeit sind damit noch nicht nachgewiesen.

## Brauche ich einen Raspberry Pi

Nein. Die aktuelle Headless-Ausführung arbeitet mit dem STM32H723. Ältere CM5-/Modulkonzepte gehören zur Projekthistorie.

## Reicht USB als Stromversorgung

Für den dokumentierten USB-Bring-up des Boards ja. Laufwerke benötigen ihre passende Versorgung; insbesondere 12 V werden nicht allein durch das USB-Kabel bereitgestellt. Siehe [Anschlüsse](Anschluesse).

## Ist das USB High Speed

Das Board verwendet den internen Full-Speed-PHY. Der Name der STM32-USB-Peripherie ist keine Angabe der tatsächlichen Verbindungsgeschwindigkeit.

## Sind alle genannten Formate bereits mit Laufwerken getestet

Nein. Viele Pfade sind implementiert und offline getestet. Die offenen Gerätemessungen stehen auf [Projektstatus](Projektstatus).

## Ist eine SCP-Datei verlustfrei zur nativen Aufnahme

Der Host exportiert SCP in 25-ns-Einheiten. Die native Zeitbasis ist feiner. Für native Ticks und erhaltene Protokollrahmen `.ufi` verwenden; siehe [Dateiformate](Dateiformate).

## Bedeutet Verify immer identische Daten

Nein. Erst die jeweilige Nachweisklasse lesen. Ausgabe abgeschlossen, Rücklesen plausibel und Inhaltsvergleich sind unterschiedliche Aussagen. Siehe [Schreiben und Prüflesen](Schreiben-und-Prueflesen).

## Beweist eine Weak-Auswertung Kopierschutz

Nein. Die beobachteten Unterschiede können auch durch Medium, Verschmutzung, Laufwerk oder Drehzahlschwankungen entstehen. Die Auswertung beschreibt instabile Bereiche der untersuchten Aufnahme.

## Funktioniert ein Apple-Laufwerk am Mac-Anschluss

Die Ports J14 und J23 haben unterschiedliche Aufgaben. Anschluss, Versorgung und Steuerung müssen zum konkreten Laufwerk passen. Siehe [Apple und Macintosh](Apple-und-Macintosh).

## Warum antwortet ufi nach einem Moduswechsel nicht

SD, Floppy und reiner Greaseweazle-Modus besitzen nicht den normalen UFI-CDC-Zugang. USB wird neu angemeldet. Modus und Zugriffswerkzeug müssen zusammenpassen.

## Kann ich eine Sitzungsschreibsperre wieder ausschalten

`write-lock engage` wirkt bis zum MCU-Reset. Es gibt keinen USB-Befehl zum Aufheben. Hardware-Schreibschutz und Zustand nach dem Reset zusätzlich prüfen.

## Was gehört in eine Fehlermeldung

Genauer Befehl, Firmware-Revision, Boardrevision, Laufwerk, Medium, Versorgung, Kabel, Status und Diagnosebericht. Schlüssel und andere private Angaben vor Veröffentlichung entfernen. [Fehlerhilfe](Fehlerhilfe).
