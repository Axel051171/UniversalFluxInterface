---
name: ufi-firmware-build-flash
description: >
  Configures, builds, sizes, flashes and debugs the UFI STM32H723 bare-metal firmware: firmware/CMakeLists.txt
  (arm-none-eabi-gcc, Ninja, STM32Cube H7 V1.11.0 path, options UFI_FORCE_INTERNAL_FLUX_STORE, UFI_WATCHDOG,
  UFI_RELEASE_OPTIMIZATION, UFI_GIT_REV), the linker script STM32H723ZGTX_FLASH.ld, the post-build gates
  check_memory.py and check_stack.py, the reproducible build repro_build.py, the CMake targets flash, flash-dfu
  and debug, SWD flashing with OpenOCD, and the CI job in .github/workflows/firmware.yml. Use when the firmware
  has to be configured or built, when a source file is added, renamed or removed, when the link or a post-build
  gate fails, when a RAM section or the linker script changes, when a variant image (fallback store, -O3, -Os,
  no watchdog) is needed, when the version string is bumped, or when a board has to be flashed or debugged.
  Trigger phrases: "build the firmware", "cmake configure", "add a source file", "linker error", "memory budget",
  "stack frame exceeds", "flash the board", "DFU", "OpenOCD", "reproducible build", "Firmware bauen",
  "Firmware flashen", "Build schlägt fehl", "neue Quelldatei einbinden", "Speicherbudget", "Linkerskript",
  "Board flashen", "per SWD flashen". DO NOT use for: the native test and regression gate check_all.py
  (ufi-test-gate), fuzzing and the check_static.py module lists in depth (ufi-fuzz-static-analysis), PSRAM start-up
  and the RAM bank design (ufi-memory-psram), the bench bring-up script and self-test (ufi-bringup-diagnostics),
  pin assignment (ufi-board-pins).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "cmake", "linker", "flash"]
---

# UFI Firmware Build and Flash

## Purpose

Describes how the STM32H723ZGT6 firmware in `firmware/` is configured, built, checked for size, flashed and
debugged, and which build-time gates fail for which reason. The build is one CMake target, `ufi_firmware.elf`,
whose link step is followed by two Python gates before `.hex` and `.bin` exist.

## Invariants and traps

1. **Every `firmware/src/*.c` must be listed in `PROJECT_SOURCES`** in `firmware/CMakeLists.txt`. There is no
   glob. `tools/check_stack.py` compares the `.su` files of the build with `src/*.c` and stops with
   `Missing/stale project stack-usage files` when a file exists in `src/` but is not built, or when a removed or
   renamed source left a `.su` behind in the build directory. CI's
   `check_static.py --compile-commands` fails the same way (`Incomplete ARM analysis: N/M project modules`).
2. **No `.bin` without green gates.** The `POST_BUILD` step runs `check_memory.py`, then `check_stack.py`, and only
   then `objcopy`. A failed gate leaves a fresh `.elf` next to a stale `.bin`/`.hex` from an earlier build. Never
   flash a `.bin` after a failed build.
3. **The memory budget is tight and checked on the linked ELF** (`check_memory.py`, also `ASSERT`s in the linker
   script). Floors: AXI SRAM free after the heap >= 32768 B, stack reserve in DTCM (`_estack - _edtcm`) >= 16384 B,
   heap exactly 8192 B, USB RX rings exactly 16384 B in DTCM (`.dtcm.usb_rx`), `.evidence` <= 8192 B.
   `firmware/build/memory_usage.json` of the last local build shows about 36.5 KiB AXI free and 20.7 KiB stack
   space, so roughly 4 KiB of new `.bss`/`.data` (AXI) or `.dtcm` data is all that fits. Plain globals land in
   AXI SRAM (`.data`, `.bss`).
4. **Two symbols are pinned by name and size**: `flux_fallback` (224 KiB, in AXI, 32-byte aligned) and
   `write_ring` (8192 B, in SRAM2, 32-byte aligned). Renaming or resizing either one fails `check_memory.py`
   (`flux fallback changed or not DMA reachable`, `write ring outside SRAM2`). `_sbrk` must exist (`ufi_heap.c`).
5. **Stack gate**: each project function frame <= 2048 B and no unbounded frame (no VLA, no `alloca`); the gate
   reads GCC `-fstack-usage` output. It does not sum call chains or interrupt nesting. Largest frame of the last
   local build: `ufi_selftest`, 1056 B.
6. **Project sources build with zero warnings** (`-Wall -Wextra`). CI greps `build.log` for `warning:` and fails
   on every line that does not contain `STM32CubeH7`. Only the vendor HAL/USB sources get `-Wno-unused-parameter`.
7. **Options are cached per build directory.** Use a separate `-B` directory per variant (the repo uses
   `firmware/build/qualification_fallback`, `firmware/build/cpu_O3`, `firmware/build/cpu_Os`); do not flip an option
   in `firmware/build` and forget it. The default image has no option set.
8. **`UFI_GIT_REV`** is taken from git only when the work tree's top level is the parent of `firmware/`; a dirty
   tree gets a `+` suffix, otherwise `dev`. It is a compile definition, so a commit changes the binary. The
   version string `UFI_FW_VERSION` (now `1.18-dev`) lives in `firmware/CMakeLists.txt`, not in a header.
9. **Flashing on the bench is SWD, not DFU.** `docs/Bringup_2026-10-24.md` section 8.1: USB-DFU did not enumerate
   on both v1.0 boards (cause open); the boards are flashed with a Tigard (FT2232H) on J4 through OpenOCD. The
   CMake targets `flash` (`st-flash`) and `debug` (`interface/stlink.cfg`) assume an ST-LINK.
10. **Watchdog**: `UFI_WATCHDOG` is ON by default and defines `USE_WATCHDOG` (IWDG1, 32.8 s,
    `UFI_WATCHDOG_INIT` in `include/ufi_fixes.h`). It is frozen while a debugger halts the core. An unexpected
    restart shows as `reset: ... watchdog` in INFO. Build with `-DUFI_WATCHDOG=OFF` only for a bench experiment.

## Build configuration

| Item | Value (source: `firmware/CMakeLists.txt`) |
|---|---|
| Toolchain | `arm-none-eabi-gcc` set in the file itself, no toolchain file; C11; `-mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard` |
| Generator | Ninja (`-G Ninja`); `CMAKE_EXPORT_COMPILE_COMMANDS` is ON |
| Build type | `Release` is forced when none is given (`-O2 -DNDEBUG`); `Debug` = `-Og -g3 -DDEBUG` |
| `STM32CUBE_PATH` | cache path, default `$ENV{HOME}/STM32Cube/Repository/STM32Cube_FW_H7_V1.11.0`; pass it explicitly when `HOME` is unset (Windows shells). HAL, CMSIS, USB device library (CDC + MSC), FatFs `ff.c` and the startup file come from there |
| `UFI_RELEASE_OPTIMIZATION` | `2` (baseline), `3` or `s`; any other value is a configure error |
| `UFI_FORCE_INTERNAL_FLUX_STORE` | OFF; ON builds the qualification image that bypasses the PSRAM U11 (INFO reports `PSRAM bypassed (qualification)`), used in `ufi_flux.c` and `ufi_psram.c` |
| `UFI_WATCHDOG` | ON; see trap 10 |
| `UFI_GIT_REV` | empty = detect; `repro_build.py` passes `src-<hash>` |
| Fixed definitions | `STM32H723xx`, `USE_HAL_DRIVER`, `HSE_VALUE=25000000`, `USE_USB_HS` |
| Link | `-specs=nosys.specs -specs=nano.specs`, `--gc-sections`, map file `ufi_firmware.map`, `--print-memory-usage` |
| ccache | used automatically when found on PATH |

Outputs in the build directory: `ufi_firmware.elf`, `.hex`, `.bin`, `.map`, `memory_usage.json`,
`stack_usage.json`, `compile_commands.json`.

## Linker script: where data goes

`firmware/STM32H723ZGTX_FLASH.ld` (FLASH 1024K at 0x08000000).

| Section | Bank | Rule |
|---|---|---|
| `.data`, `.bss`, `.axi_sram*`, heap | AXI SRAM 320K, 0x24000000 | default for globals; heap 8 KiB fixed; 32 KiB must stay free behind `__heap_end` |
| `.dtcm`, `.dtcm.usb_rx`, `.dtcm*`, stack | DTCM 128K, 0x20000000 | NOLOAD and outside `.bss` (contents survive a reset), not reachable by DMA1/2; stack grows down from `_estack` |
| `.usb_buffer`, `.evidence`, `.sram1*` | SRAM1 16K, 0x30000000 | NOLOAD |
| `.sram2*` | SRAM2 16K, 0x30004000 | NOLOAD, 32-byte aligned, DMA1 reachable (write ring) |
| `.backup` | SRAM4 16K, 0x38000000 | NOLOAD; D3 SRAM, not the battery-backed backup SRAM |

`check_memory.py` rejects every section that crosses a bank boundary, lies in AXI behind `__heap_end`, or lies in
DTCM behind `_edtcm`. A new output section therefore has to be placed before the heap (AXI) or inside `.dtcm`.
The bank addresses are repeated as literals in `check_memory.py`: a changed `MEMORY` block needs the same change
there. The linker script and both gate scripts are `LINK_DEPENDS`, so editing them relinks.

## Procedures

### Add, rename or remove a source file

1. Create `firmware/src/<name>.c` and its header in `firmware/include/`.
2. Add `src/<name>.c` to `PROJECT_SOURCES` in `firmware/CMakeLists.txt` (remove the old line on a rename).
3. For a `ufi_*.c` module, classify it in `firmware/tools/check_static.py`: `PURE` (no HAL, analysed as built),
   `NEEDS_REAL_HAL` (needs real HAL/USB/FatFs headers), or neither (analysed against `firmware/tests/hal_stub/`).
   Unlisted modules fall into the stub group and fail there if the stub header lacks a declaration.
4. After a rename or removal, delete the build directory or the stale `.su`/object files; otherwise
   `check_stack.py` reports stale files.
5. Build; the two gate lines `Memory budget OK: ...` and `Project stack frames OK: ...` must appear.
6. Run the offline gate and the static analysis (see `ufi-test-gate`, `ufi-fuzz-static-analysis`).

### Add RAM data

1. Decide the bank from the table above (DMA target: AXI or SRAM2, never DTCM; state that must survive a reset
   without re-initialisation: a NOLOAD section).
2. Use the section attribute pattern from Examples. For AXI/DTCM check the headroom in `memory_usage.json` first.
3. If the object is a new fixed budget, add a `require(...)` to `validate()` in `tools/check_memory.py` and, where
   the linker can express it, an `ASSERT` in the linker script (as done for `.evidence` and the USB RX rings).
4. Never lower `_Min_AXI_Reserve`, `_Min_Stack_Size` or the limits in `check_memory.py`/`check_stack.py` to make a
   build pass without the owner's decision; free memory instead.

### Flash a board

1. Build with green gates; flash `ufi_firmware.bin` at `0x08000000`.
2. SWD (bench standard): Tigard on J4, config and command in Examples. An ST-LINK works with
   `cmake --build firmware/build --target flash`.
3. DFU (ROM bootloader): entered by button A held at power-up (`PIN_BTN_A`, read in `ufi_init()` right after
   `board_gpio_init()`), by `ufi bootloader` from the host (`ufi_request_bootloader()` sets a magic and resets),
   or by BOOT (SW2) + RESET (SW1) on the board; then `cmake --build firmware/build --target flash-dfu`.
   On v1.0 SW3/SW4 are not fitted; button A is J12 pin 13 to GND.
4. After `program ... reset`, INFO must report the reset cause `software`.

## Examples

```bash
# default image (CI uses the same two commands)
cmake -S firmware -B firmware/build -G Ninja -DSTM32CUBE_PATH=C:/Users/<user>/STM32Cube/Repository/STM32Cube_FW_H7_V1.11.0
cmake --build firmware/build

# qualification image without the PSRAM, in its own directory
cmake -S firmware -B firmware/build/qualification_fallback -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DUFI_FORCE_INTERNAL_FLUX_STORE=ON -DSTM32CUBE_PATH=<cube>
cmake --build firmware/build/qualification_fallback --parallel 4

# gates by hand on an existing build
python firmware/tools/check_memory.py firmware/build/ufi_firmware.elf --report firmware/build/memory_usage.json
python firmware/tools/check_stack.py firmware/build --report firmware/build/stack_usage.json

# all project modules against the real ARM headers (CI build job)
python firmware/tools/check_static.py --compile-commands firmware/build/compile_commands.json

# two fresh builds must be byte-identical (pinned gcc 14.2.1, cmake 4.4.3, ninja 1.13.2, Cube FW.H7.1.11.0)
python firmware/tools/repro_build.py --cube <cube>

# flash targets
cmake --build firmware/build --target flash        # st-flash write ufi_firmware.bin 0x08000000
cmake --build firmware/build --target flash-dfu    # dfu-util -a 0 -s 0x08000000:leave -D ufi_firmware.bin
openocd -f tigard-swd.cfg -f target/stm32h7x.cfg -c "program ufi_firmware.bin 0x08000000 verify reset exit"
```

`tigard-swd.cfg` is not a file in the repo; its eight lines are printed in `docs/Bringup_2026-10-24.md` 8.1.

Section placement as written in the sources (`ufi_main.c`, `ufi_gw.c`, `ufi_quality_runtime.c`):

```c
__attribute__((section(".dtcm"))) static volatile uint32_t g_boot_magic;       /* survives a reset: NOLOAD, not in .bss */
__attribute__((section(".dtcm.usb_rx"), aligned(32))) static uint8_t rx[RX_SIZE];
__attribute__((section(".sram1"),aligned(4))) static sector_details_t sector_details;
```

Most module state that the CPU alone touches already sits in `.dtcm` to keep the AXI budget unchanged.

**Done criterion**: `cmake --build` ends with both gate lines and `arm-none-eabi-size` output, no `warning:` from a
file under `firmware/src` or `firmware/include`, and `check_static.py --compile-commands` reports all project
modules. CI (`.github/workflows/firmware.yml`, job `build`) then uploads `.bin`, `.hex`, `.elf` and both JSON
reports.

## Prerequisites

- `arm-none-eabi-gcc` with newlib, CMake >= 3.20, Ninja, Python 3 (the gates use the standard library only).
- STM32Cube H7 V1.11.0. CI fetches it sparsely from `STMicroelectronics/STM32CubeH7` tag `v1.11.0` with
  submodules (HAL driver, CMSIS device, USB device library, FatFs).
- For `check_static.py`: clang on PATH. For `repro_build.py`: exactly the locked tool versions.
- For flashing: OpenOCD 0.12 with a Tigard or ST-LINK, or `st-flash`, or `dfu-util`.

## Limitations

- `check_stack.py` budgets single frames only; call-chain depth and IRQ nesting are not proven.
- `check_memory.py` checks placement and budgets, not run-time use; the run-time stack high-water mark comes
  from `ufi_stack_paint()` and the SELFTEST item `stack_free` on the board.
- `repro_build.py` proves two local builds equal; CI builds with Ubuntu's toolchain, so CI and local binaries
  differ. It writes into `firmware/tests/build/repro-*` and never deletes earlier runs.
- The `debug` target starts OpenOCD with `&` inside a CMake command and assumes an ST-LINK; it is not the bench
  flow and has no bench evidence in the docs.
- `README.md` still says hardware and firmware are untested on a real device and that the LAN firmware is
  missing; `docs/Bringup_2026-10-24.md` section 8 and the commit history (LAN L1-L6) are newer.
- A `Debug` build is not part of CI; whether it passes the memory gate is not recorded.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| `Missing/stale project stack-usage files; rebuild the firmware` | a `.c` in `firmware/src` is not in `PROJECT_SOURCES`, or a removed/renamed source left a `.su` in the build directory | add the file to `PROJECT_SOURCES`; delete the build directory after a rename |
| `Incomplete ARM analysis: N/M project modules` | same mismatch seen by `check_static.py --compile-commands` | same fix, then reconfigure so `compile_commands.json` is current |
| Link fails with `AXI SRAM: less than 32 KiB reserve` or gate says `AXI reserve below 32 KiB` | new globals in `.data`/`.bss`/`.axi_sram` | shrink or move the data (PSRAM, SRAM1, `const` into FLASH); do not lower the reserve |
| `DTCM: .dtcm data leaves less than _Min_Stack_Size for the stack` / `stack reserve below 16 KiB` | new `.dtcm` data | move it out of DTCM or shrink it |
| `USB RX rings: expected 16 KiB in DTCM` | size of the `.dtcm.usb_rx` buffers changed | keep the total at 16384 B or change the `ASSERT` and `check_memory.py` together |
| `Stack frame exceeds 2048 B: <function>` or `Unbounded stack use` | large local array, VLA or `alloca` | make the buffer `static` in a fitting section or pass it in |
| `<section> crosses RAM boundary` / `exceeds checked AXI budget` | a new output section was placed behind the heap or overflows its bank | place it before `._user_heap_stack` or in another bank |
| Configure error for HAL sources, or thousands of missing files | `STM32CUBE_PATH` wrong; `HOME` unset | pass `-DSTM32CUBE_PATH=...` (forward slashes) |
| CI red in step Build although the link passed | a `warning:` line from a project file | fix the warning; vendor lines are only exempt when their path contains `STM32CubeH7` |
| INFO shows revision `dev` | `firmware/` is not inside its own git work tree (for example a copy inside another repository) | build from the repo or pass `-DUFI_GIT_REV=...` |
| `repro_build.py`: `gcc: expected 14.2.1, found ...` | tool version differs from `LOCK` | install the locked version; do not edit `LOCK` to match the machine |
| DFU device does not appear | known open bench finding on v1.0 | flash over SWD (J4) |
| OpenOCD prints `clearing lockup after double fault` on first connect | empty flash | normal, continue |
| Board resets about every 33 s in a long operation | IWDG not fed on that path | find the missing `UFI_WATCHDOG_FEED`; confirm with `-DUFI_WATCHDOG=OFF` in a separate build directory |
