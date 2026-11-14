# Greaseweazle

**Sprache / Language:** [Deutsch](Greaseweazle) | [English](Greaseweazle-EN)

UFI besitzt einen Greaseweazle-kompatiblen Protokollmodus. Dabei bedienen Greaseweazle-Werkzeuge das Board. Das ist ein eigener Transportweg mit eigenem Befehlsumfang; UFI-spezifische Analysefunktionen sind dadurch nicht automatisch Greaseweazle-Funktionen.

## Modus auswählen

Aus dem UFI-Modus:

```console
ufi mode gw
```

Oder das Flux-Protokoll in `UFI.CFG` auf `protocol=gw` setzen. Nach dem Wechsel meldet sich das Board neu an. Der reine GW-Modus verwendet VID/PID `1209:4F57`.

Die Greaseweazle-Software entsprechend ihrer eigenen Anleitung installieren. Zum ersten Prüfen:

```console
gw info
gw read --help
```

Die konkreten Laufwerks- und Formatparameter hängen von der eingesetzten Greaseweazle-Version ab. Vor einem Schreibbefehl zusätzlich die UFI-[Schreibfreigabe](Schreiben-und-Prueflesen) prüfen.

## Composite

Im Modus `composite` stehen Greaseweazle und eine separate native UFI-Bulk-Schnittstelle zur Verfügung. Für Letztere das USB-Extra installieren und verwenden:

```console
ufi --port usb info
```

Eine Schnittstelle darf die Arbeit der anderen nicht durch Reset oder Umkonfiguration des gesamten USB-Geräts unterbrechen. Die Firmware koordiniert den Besitz der Laufwerksressourcen. Zwei Programme dürfen keine unabhängigen gleichzeitigen Schreib-/Lesevorgänge auf demselben Laufwerk erwarten.

## Zurückkehren

Im reinen GW-Modus gibt es keinen normalen UFI-CDC-Port für `ufi mode flux`. Für die Rückkehr den vorgesehenen Betriebsartenschalter bzw. die Konfiguration über den SD-Modus verwenden.

Referenzen: [UFI-GW-Implementierung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_gw.c), [Greaseweazle-Projekt](https://github.com/keirf/greaseweazle).
