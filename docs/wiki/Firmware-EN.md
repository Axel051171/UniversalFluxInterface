# Building and flashing firmware

**Sprache / Language:** [Deutsch](Firmware) | [English](Firmware-EN)

Firmware is built for STM32H723 using CMake and Ninja. Requirements are the ARM GCC toolchain, Python for build checks, and **STM32CubeH7 v1.11.0** including HAL, CMSIS, USB Device Library and FatFs.

## Build a release

From the project root, replace the Cube path with your installation:

```console
cmake -S firmware -B firmware/build -G Ninja -DCMAKE_BUILD_TYPE=Release -DSTM32CUBE_PATH=C:/STM32Cube/STM32Cube_FW_H7_V1.11.0
cmake --build firmware/build
```

On Linux, the last argument might point to `/opt/STM32Cube_FW_H7_V1.11.0`. These are example installation paths, not automatically created directories.

Outputs include `ufi_firmware.elf`, `.bin`, `.hex`, a map, and memory/stack reports. A failed build may leave an older BIN behind. Flash only the result of a completely successful build.

## Build options

| Option | Purpose |
|---|---|
| `UFI_WATCHDOG` | Watchdog; enabled in the normal build |
| `UFI_FORCE_INTERNAL_FLUX_STORE` | Bypass PSRAM for deliberate fallback testing |
| `UFI_RELEASE_OPTIMIZATION` | `2`, `3` or `s`; default `2` |
| `UFI_GIT_REV` | Explicit embedded source revision |

Use separate build directories for variants to avoid carrying cached settings into the normal image. The embedded revision gets a `+` when working-tree changes are detected.

## Program through SWD

J4 is the v1.0 SWD connector. Match adapter, target voltage, ground, SWDIO, SWCLK and reset to the actual pinout. With a suitable ST-LINK/OpenOCD setup:

```console
openocd -f interface/stlink.cfg -f target/stm32h7x.cfg -c "program firmware/build/ufi_firmware.elf verify reset exit"
```

The documented bench also used a Tigard/FT2232H adapter, which needs a different configuration. Do not program firmware during a write operation.

After restart, check `ufi info` and compare the revision and binary hash with the intended image.

## USB DFU

Firmware and host implement entry into the STM32 ROM bootloader. The DFU device did not enumerate on the documented v1.0 bench; the cause remains open. SWD is the demonstrated programming route for that setup.

If DFU demonstrably enumerates on your setup, the intended command is:

```console
dfu-util -a 0 -s 0x08000000:leave -D firmware/build/ufi_firmware.bin
```

## Before distribution

See [Development and tests](Development-and-Tests) for the offline gate, static analysis and fuzzing. Device acceptance is separate: [Bring-up](Bring-up) and [write-path acceptance plan, German](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/Abnahme_Schreibpfad.md).

Source: [CMake configuration](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/CMakeLists.txt).
