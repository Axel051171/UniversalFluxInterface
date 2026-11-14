# Installation

**Sprache / Language:** [Deutsch](Installation) | [English](Installation-EN)

The PC tool is called `ufi`. It requires Python 3.10 or later and is installed from `software/ufi_host`. Normal USB operation uses a serial CDC connection. Composite mode requires additional USB libraries.

## Get the source

```console
git clone https://github.com/Axel051171/UniversalFluxInterface.git
cd UniversalFluxInterface
```

Run the following installation commands from this project directory. Building firmware also requires an ARM toolchain; see [Firmware](Firmware-EN).

## Windows with PowerShell

```powershell
py -3 -m venv .venv
.\.venv\Scripts\python.exe -m pip install -e ./software/ufi_host
.\.venv\Scripts\ufi.exe --help
.\.venv\Scripts\ufi.exe info
```

The explicit executable paths avoid having to activate the environment. After activation, the shorter `ufi` command is available. On Windows, CDC normally appears as a COM port. Automatic discovery looks for VID/PID `1209:4F54`.

## Linux and macOS

```sh
python3 -m venv .venv
. .venv/bin/activate
python -m pip install -e ./software/ufi_host
ufi --help
ufi info
```

On Linux, check serial-device permissions if necessary; the relevant group depends on the distribution. On macOS, specify the actual `/dev/cu.*` port if discovery cannot identify a single device.

## Composite with native USB bulk

```console
python -m pip install -e "./software/ufi_host[usb]"
ufi --port usb info
```

Firmware must be in Composite mode. The host claims native interface 2. With multiple devices, `--port usb:SERIAL` selects an MCU serial number; replace `SERIAL` with the real value. Windows may need the appropriate WinUSB binding for this interface. Do not reconfigure the Greaseweazle interface.

## Connection and help

```console
ufi --help
ufi read --help
ufi write --help
ufi --port COM5 info
```

Global options such as `--port`, `--host`, `--integrity` and `--extended` precede the subcommand. Subcommand options follow it.

The Python package version and firmware version are separate values. Use `ufi info`, `ufi features`, `ufi describe` and `ufi masks` to discover device capabilities.

Sources: [Package definition](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/pyproject.toml), [host reference, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/README.md).
