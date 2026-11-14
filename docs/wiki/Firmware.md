# Firmware bauen und flashen

**Sprache / Language:** [Deutsch](Firmware) | [English](Firmware-EN)

Die Firmware wird mit CMake und Ninja für den STM32H723 gebaut. Benötigt werden die ARM-GCC-Toolchain, Python für die Build-Prüfungen und **STM32CubeH7 v1.11.0** mit HAL, CMSIS, USB Device Library und FatFs.

## Release bauen

Aus dem Projektstamm, mit einem an die eigene Installation angepassten Cube-Pfad:

```console
cmake -S firmware -B firmware/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTM32CUBE_PATH=C:/STM32Cube/STM32Cube_FW_H7_V1.11.0
cmake --build firmware/build
```

Unter Linux kann der letzte Parameter beispielsweise auf `/opt/STM32Cube_FW_H7_V1.11.0` zeigen. Der Pfad ist ein Installationsbeispiel, kein automatisch angelegter Ordner.

Der Build erzeugt unter anderem `ufi_firmware.elf`, `.bin`, `.hex`, eine Map-Datei und Speicher-/Stackberichte. Nach einem fehlgeschlagenen Build kann noch eine ältere BIN-Datei vorhanden sein. Nur das Ergebnis eines vollständig erfolgreichen Builds flashen.

## Buildoptionen

| Option | Verwendung |
|---|---|
| `UFI_WATCHDOG` | Watchdog; im normalen Build eingeschaltet |
| `UFI_FORCE_INTERNAL_FLUX_STORE` | PSRAM für gezielte Fallback-Prüfungen umgehen |
| `UFI_RELEASE_OPTIMIZATION` | Optimierungsprofil `2`, `3` oder `s`; Standard `2` |
| `UFI_GIT_REV` | Eingebettete Quellrevision ausdrücklich setzen |

Varianten in getrennten Buildordnern anlegen, damit Cache-Einstellungen nicht versehentlich in das normale Abbild übernommen werden. Die eingebettete Revision erhält bei erkannten Änderungen am Arbeitsstand ein `+`.

## Über SWD programmieren

J4 ist auf v1.0 der SWD-Anschluss. Debugadapter, Zielspannung, Masse, SWDIO, SWCLK und gegebenenfalls Reset nach der tatsächlichen Steckerbelegung verbinden. Mit einem passenden ST-LINK/OpenOCD-Aufbau beispielsweise:

```console
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "program firmware/build/ufi_firmware.elf verify reset exit"
```

Der dokumentierte Prüfstand verwendete auch einen Tigard/FT2232H-Adapter; dessen Konfiguration unterscheidet sich vom ST-LINK-Beispiel. Kein neues Firmwareabbild während eines laufenden Schreibauftrags programmieren.

Nach dem Neustart `ufi info` prüfen und Revision sowie Binärhash mit dem vorgesehenen Abbild vergleichen.

## USB DFU

Firmware und Host enthalten den Einstieg in den STM32-ROM-Bootloader. Am dokumentierten v1.0-Prüfstand erschien das DFU-Gerät jedoch nicht; die Ursache ist offen. SWD ist deshalb der belegte Programmierweg dieses Stands.

Wenn DFU auf dem eigenen Aufbau nachweislich enumeriert, lautet der vorgesehene Flashbefehl:

```console
dfu-util -a 0 -s 0x08000000:leave -D firmware/build/ufi_firmware.bin
```

## Prüfungen vor der Weitergabe

[Entwicklung und Tests](Entwicklung-und-Tests) beschreibt Offline-Gate, statische Analyse und Fuzzing. Die Geräteabnahme erfolgt gesondert nach [Inbetriebnahme](Inbetriebnahme) und [Schreibpfad-Abnahme](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Abnahme_Schreibpfad.md).

Quelle: [CMake-Konfiguration](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/CMakeLists.txt).
