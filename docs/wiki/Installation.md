# Installation

**Sprache / Language:** [Deutsch](Installation) | [English](Installation-EN)

Das PC-Werkzeug heißt `ufi`. Es benötigt Python ab Version 3.10 und wird aus `software/ufi_host` installiert. Der normale USB-Modus verwendet eine serielle CDC-Verbindung; Composite benötigt zusätzliche USB-Bibliotheken.

Für die grafische Bedienung gibt es zusätzlich [UnifiedFloppyTool mit UFI](UnifiedFloppyTool). Dort sind Controllerwahl, COM-Port, UFI-Werkzeuge und der Unterschied zum Python-Host beschrieben. Die folgende Installation betrifft die Kommandozeile.

## Quellcode holen

```console
git clone https://github.com/Axel051171/UniversalFluxInterface.git
cd UniversalFluxInterface
```

Alle folgenden Installationsbefehle werden aus diesem Projektordner ausgeführt. Für Änderungen an der Firmware wird zusätzlich eine ARM-Toolchain benötigt, siehe [Firmware](Firmware).

## Windows mit PowerShell

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -e ./software/ufi_host
.\.venv\Scripts\ufi.exe --help
.\.venv\Scripts\ufi.exe info
```

Mit den vollständigen Programmpfaden ist keine Aktivierung der Umgebung nötig. Wer die Umgebung aktiviert, kann die kürzere Schreibweise `ufi` verwenden. Unter Windows erscheint die CDC-Verbindung üblicherweise als COM-Port. Die automatische Suche richtet sich nach VID/PID `1209:4F54`.

## Linux und macOS

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e ./software/ufi_host
ufi --help
ufi info
```

Unter Linux gegebenenfalls die Zugriffsrechte des seriellen Geräts prüfen. Die zuständige Gruppe hängt von der Distribution ab. Unter macOS den tatsächlichen `/dev/cu.*`-Port verwenden, wenn die automatische Erkennung kein eindeutiges Gerät findet.

## Composite mit nativem USB-Bulk

```console
python -m pip install -e "./software/ufi_host[usb]"
ufi --port usb info
```

Die Firmware muss im Composite-Modus laufen. Das Host-Werkzeug beansprucht dessen native Schnittstelle 2. Für mehrere Geräte kann `--port usb:SERIAL` die MCU-Seriennummer auswählen; `SERIAL` ist durch den tatsächlichen Wert zu ersetzen. Windows kann eine passende WinUSB-Zuordnung für diese Schnittstelle benötigen. Die Greaseweazle-Schnittstelle dabei nicht umkonfigurieren.

## Verbindung und Hilfe

```console
ufi --help
ufi read --help
ufi write --help
ufi --port COM5 info
```

Globale Optionen wie `--port`, `--host`, `--integrity` und `--extended` stehen vor dem Unterbefehl. Optionen des Unterbefehls folgen dahinter.

Die Paketversion des Python-Hosts und die Firmwareversion sind verschiedene Angaben. Für die angebotenen Gerätefunktionen `ufi info`, `ufi features`, `ufi describe` und `ufi masks` auswerten.

Quelle: [Paketdefinition](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/pyproject.toml), [Host-Anleitung](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/README.md).
