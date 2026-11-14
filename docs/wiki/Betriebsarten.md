# Betriebsarten

**Sprache / Language:** [Deutsch](Betriebsarten) | [English](Operating-Modes)

UFI kann sich am USB als verschiedene Geräte anmelden. Ein Moduswechsel trennt die bisherige USB-Verbindung; Programme müssen danach den passenden Anschluss neu öffnen.

| Modus | Zweck | Host-Zugriff |
|---|---|---|
| `flux` | UFI-Protokoll, Rohaufnahmen, Analyse und Schreiben | `ufi`, USB-CDC |
| `gw` | Greaseweazle-kompatibles Protokoll | Greaseweazle-Werkzeuge |
| `composite` | Greaseweazle plus eigene native UFI-Schnittstelle | Greaseweazle und `ufi --port usb` mit Besitzkoordination |
| `sd` | Interner Speicher als USB-Massenspeicher | Dateisystem des PCs |
| `floppy` | Diskette als USB-Blocklaufwerk | Betriebssystem des PCs |

## Modus vom Host wechseln

Im erreichbaren UFI-Modus zum Beispiel:

```console
ufi mode sd
```

Nach dem Wechsel ist `ufi` über den bisherigen CDC-Port nicht verfügbar. Massenspeicher vor dem Zurückschalten am PC auswerfen. Der Betriebsartenschalter bzw. die dokumentierte Tasterbedienung ermöglicht die Rückkehr, wenn keine native Kommandoschnittstelle erreichbar ist.

Die Firmwarekonfiguration `protocol=ufi` oder `protocol=gw` legt das Flux-Protokoll der entsprechenden Schalterstellung fest. Details: [SD und Standalone](SD-und-Standalone).

## USB Floppy

In `floppy` erscheint die eingelegte Diskette als Blockgerät. Die Firmware setzt unterstützte Sektorformate auf physische Diskettenzugriffe um. Dieser Modus ist für normale Dateisystemzugriffe gedacht; Roh-Flux und ungewöhnliche Kopierschutzstrukturen werden über `flux` aufgenommen.

Ein Dateisystem kann bereits beim Einbinden Schreibzugriffe auslösen. Originalmedien deshalb mit aktivem Schreibschutz verwenden. Änderungen am Host-Dateisystem können außerdem erst zeitversetzt auf die Diskette zurückgeschrieben werden.

## Gleichzeitig über USB und LAN

Eine aktive Sitzung besitzt die gemeinsam verwendeten Laufwerksressourcen. Andere Transporte dürfen nicht parallel den Kopf oder den Flux-Puffer verändern. Bei `busy` zuerst laufende Aufträge bzw. den Besitzer über `ufi net` und die Statusabfragen prüfen.

Weiter: [Greaseweazle](Greaseweazle) · [LAN](LAN) · [Fehlerhilfe](Fehlerhilfe).

Referenz: [USB-Kompositgerät und Betriebsarten](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md).
