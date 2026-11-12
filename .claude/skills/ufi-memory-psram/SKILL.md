---
name: ufi-memory-psram
description: >
  Memory layout of the UFI firmware (STM32H723) and its external 8 MB QSPI PSRAM flux store on OCTOSPI1:
  linker regions and sections, which buffer lives in DTCM, AXI SRAM, D2 SRAM1/SRAM2, D3 SRAM4 or PSRAM,
  cache and DMA rules, the bounded 8 KiB heap, the PSRAM start-up phases with trial marker, latch and
  fallback to the internal store, and the memory and stack budget checks that run on every link. Use when
  adding or moving a static buffer, touching STM32H723ZGTX_FLASH.ld, ufi_psram.c, ufi_heap.c, check_memory.py
  or check_stack.py, when the link step fails on a budget, when the board reports "PSRAM off" or runs on the
  fallback store, or when a qualification image without U11 is needed. Trigger phrases: "memory layout",
  "linker script", "PSRAM init", "flux store fallback", "AXI reserve", "stack budget", "cache invalidate",
  "Speicheraufteilung", "Speicherbudget", "PSRAM startet nicht", "Ersatzpuffer", "Linker-Fehler",
  "Stackreserve", "Puffer verschieben". DO NOT use for: capture state machine, timers and the stream encoder
  that fill the store (ufi-flux-capture-timers); the write DMA ring logic and write gate
  (ufi-write-path-safety); toolchain, CMake presets and flashing (ufi-firmware-build-flash); bench bring-up
  flow and SELFTEST interpretation (ufi-bringup-diagnostics); OCTOSPI pin assignment (ufi-board-pins);
  SELFTEST and INFO wire formats (ufi-usb-protocol).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "memory", "psram", "linker", "cache"]
---

# UFI Memory Layout and PSRAM Flux Store

## Purpose

Explains where every class of data lives on the STM32H723, which rules follow from DMA reach, caches and
uninitialised sections, and how the 8 MB PSRAM (U11, APS6404L) becomes the flux store only after a chain of
tests, with a 224 KiB internal fallback otherwise. It names the checks that reject a wrong layout at link
time and the touchpoints for buffer and PSRAM start-up changes.

## Map

| File | Role |
|---|---|
| `firmware/STM32H723ZGTX_FLASH.ld` | regions, sections, the three ASSERTs (AXI reserve, USB RX rings, stack reserve) and the `.evidence` limit |
| `firmware/tools/check_memory.py` | checks the linked ELF (sections, symbols) after every link; writes `firmware/build/memory_usage.json` |
| `firmware/tools/check_stack.py` | checks GCC `.su` frame sizes; writes `firmware/build/stack_usage.json` |
| `firmware/src/ufi_heap.c` | `_sbrk()` bounded by `__heap_start` / `__heap_end` |
| `firmware/src/ufi_psram.c`, `firmware/include/ufi_psram.h` | OCTOSPI1 set-up, phases, tests, trial marker, latch, status block |
| `firmware/src/ufi_flux.c` | `select_store()`, `configure_store()`, `store_dma_gate()`, `ufi_flux_dma_selftest()`, `ufi_flux_store_retest()` |
| `firmware/src/system_stm32h7xx.c` | `SystemInit()` enables I-cache and D-cache |
| `firmware/src/ufi_resources.c` | lease of the drive/timer/store bundle: `UFI_OWNER_NATIVE` or `UFI_OWNER_GW`, main loop only |
| `firmware/tools/psram_probe.tcl` | OpenOCD script: PSRAM ID by GPIO over SWD, without firmware |
| `docs/Speicherarchitektur.md` | design record; its byte counts are historic (see Limitations) |

## Regions and what lives there

| Region | Section | Contents | Rules |
|---|---|---|---|
| DTCM 128 K, 0x20000000 | `.dtcm`, `.dtcm.usb_rx`, `.dtcm.*` | stack (top `_estack`), USB RX rings of `ufi_v2.c` and `ufi_gw.c` (2 x 8192, ASSERT = 16384), composite reply buffers, crash record, `g_boot_magic`, `psram_trial`, dump stage buffer, decoder state | CPU only: DMA1/2 cannot reach it. NOLOAD, never zeroed, survives a reset. At least 16 KiB between `_edtcm` and `_estack` |
| AXI SRAM 320 K, 0x24000000 | `.data`, `.axi_sram`, `.bss`, heap | `flux_fallback` (224 KiB, aligned 32), ordinary statics, 8 KiB heap | DMA reachable, cached. At least 32 KiB free after `__heap_end`; nothing may be placed behind the heap |
| SRAM1 16 K, 0x30000000 (D2) | `.usb_buffer`, `.evidence`, `.sram1` | capture evidence records (limit 8192 by ASSERT), survey table, sector details | CPU only by convention. Clock enabled in `ufi_flux_init()` (`__HAL_RCC_D2SRAM1_CLK_ENABLE()`) |
| SRAM2 16 K, 0x30004000 (D2) | `.sram2` | `write_ring` (8192 bytes, aligned 32, DMA1), LAN state | Clock enabled in `ufi_write.c` (`__HAL_RCC_D2SRAM2_CLK_ENABLE()`) |
| SRAM4 16 K, 0x38000000 (D3) | `.backup` | decoder and comparison state | The name misleads: this is not the battery-backed backup SRAM |
| PSRAM 8 MiB, `OCTOSPI1_BASE` 0x90000000 | none (pointer) | flux store when all tests pass | MPU region 0; reachable by CPU and DMA |

Flux store capacity (`configure_store()`): PSRAM 63 chunks x 32768 words = 2,064,384 words; fallback
2 chunks x 19112 = 38,224 words. One further full chunk behind the usable store is `dma_scratch`. With
`ufi_flux_reference_reserve(true)` (precision verify) the upper 4 MiB of the PSRAM hold the reference and the
store shrinks to 31 chunks. The fallback array stays reserved even when the PSRAM works.

## Invariants and traps

1. **Every section except `.data` and `.bss` is NOLOAD and not cleared.** An initialiser on a variable in
   `.dtcm`, `.axi_sram`, `.sram1`, `.sram2`, `.evidence` or `.backup` is ignored, and the value after a
   reset is whatever was there. Set such state in an init function. The firmware depends on this for
   `g_boot_magic` (`ufi_main.c`), the crash record (`ufi_crash.c`) and `psram_trial`; do not add a start-up
   memset of DTCM.
2. **A DMA buffer never goes to DTCM**, is aligned to 32 bytes and sized in multiples of 32 bytes
   (`aligned(32)`, `chunk_words &= ~7u`). `check_memory.py` enforces this for `flux_fallback` and
   `write_ring` by symbol name and exact size; renaming or resizing either one fails the link step.
3. **Cache protocol around DMA** (D-cache is on for AXI, D2 SRAM and the PSRAM window):
   before a capture `SCB_CleanInvalidateDCache()` so no dirty line lands on fresh DMA data; before the CPU
   reads DMA-written words `SCB_InvalidateDCache_by_Addr()` with a 32-aligned address and length; before
   DMA reads CPU-written words `SCB_CleanDCache_by_Addr()` (write path). The stream tests assert the
   alignment of each invalidate call of the encoder.
4. **The AXI budget is nearly spent.** New persistent state goes to `.dtcm` (CPU data) or reuses an
   existing buffer; the pattern in recent commits is `__attribute__((section(".dtcm")))` with a comment
   why. DTCM in turn must keep the 16 KiB stack reserve, and SELFTEST item `stack_free` reports the painted
   high-water mark (`ufi_stack_paint()`, `ufi_stack_free()`).
5. **The heap is exactly 8 KiB and for the main loop only.** `_sbrk()` returns ENOMEM without moving the
   cursor on overflow or on shrinking below the start. `check_memory.py` requires 8192 bytes and the
   presence of `_sbrk`. Project code uses static storage, not `malloc`.
6. **Stack frames**: `-fstack-usage` is on for the ELF target; `check_stack.py` rejects any project frame
   above 2048 bytes and any frame that is not `static` or `dynamic,bounded` (no VLA, no `alloca`). It also
   requires one `.su` file per `firmware/src/*.c`, so a source file that exists but is not part of the
   target, or a stale build directory, fails the step.
7. **The PSRAM is used only after proof.** Order in `ufi_init()` (`ufi_main.c`): `ufi_crash_capture_reset()`,
   GPIO, read buttons (B = safe start calls `ufi_psram_skip()`), then `ufi_flux_init()`:
   `select_store(ufi_psram_init() == UFI_OK)`, timer and DMA set-up, `store_dma_gate()`.
8. **Start-up phases** (`UFI_PSRAM_PH_*`, shared by marker, INFO text and status block): 1 controller,
   2 QPI reset, 3 SPI reset, 4 read ID, 5 enter QPI, 6 map window, 7 first read, 8 first write, 9 32-bit
   writes, 10 cache lines, 11 DMA writes. Every HAL result is checked; a failure goes through
   `phase_failed()`, which clears the marker.
9. **Bench faults of 2026-11-11 that must not come back** (pinned by `test_psram_init.py`):
   - A chip left in QPI mode ignores the one-line reset and has no READ ID. Reset 0x66/0x99 and exit-quad
     0xF5 go out on four lines first, then the one-line reset.
   - READ ID runs at `OSPI_PROBE_PRESCALER` 16 (17.2 MHz, limit 33 MHz); the working clock
     `OSPI_PRESCALER` 8 (34.4 MHz) is set after the ID. 68.75 MHz needs sample shifting proven on a board.
   - Erratum ES0491 2.8.6: a memory-mapped write with DQSE clear ends in a bus error. The write command
     sets `HAL_OSPI_DQS_ENABLE` although the chip has no DQS pin.
   - Every controller wait is `PSRAM_HAL_TIMEOUT_MS` (100); the HAL default of 5 s per call exceeds the
     32.8 s watchdog on a dead controller.
   - The memory-mapped timeout counter (`PSRAM_IDLE_CYCLES`) releases CS# after the read prefetch so the
     chip can refresh; its interrupt is disabled because nobody handles it.
10. **Trial marker and latch.** `trial(phase)` stores `TRIAL_MAGIC | phase` in `psram_trial` (DTCM, survives
    reset) before each risky step. A start that dies there (bus fault, watchdog) makes the next start
    turn it into `LATCH_MAGIC`: the PSRAM stays off on every later start until a power-on reset
    (`RCC_RSR_PORRSTF` from `ufi_crash_reset_flags()`) or a host retest. This keeps a faulty board
    reachable over USB; do not weaken it to "one skipped start".
11. **Two write tests decide.** `psram_narrow_test()` runs with the window mapped as device memory
    (`psram_mpu(false)`) to prove that single 32-bit writes do not zero the neighbouring word, then
    `psram_line_test()` runs cacheable (`psram_mpu(true)`, the working setting). After timer set-up
    `ufi_flux_dma_selftest()` writes through the capture stream itself. Result 0 keeps the PSRAM;
    `UFI_DMA_TEST_NOT_RUN` keeps it and says so (`UFI_PSRAM_STATE_DMA_NOT_RUN`); any other value calls
    `select_store(false)`.
12. **Every store selection resets pointers, capacity and validity** (`select_store()` calls
    `ufi_flux_invalidate()`). `ufi_flux_store_retest()` is refused with `UFI_ERR_BUSY` during capture or
    write, cleans and invalidates the window, selects the fallback during the init, then selects again.
13. **Tests that write the store destroy its contents**: SELFTEST item 3 `psram` (last 16384 words),
    item 22 `psram_dma`, and the retest. Each calls `ufi_flux_invalidate()` first; a new store writer must
    do so too.
14. **`ufi_psram.h` stays free of HAL types**, and `test_psram_init.py` compiles the whole `ufi_psram.c`
    against a stub HAL with `UFI_FIRMWARE_H` predefined. A new HAL call, register or macro in `ufi_psram.c`
    needs its stub in that test.

## Procedures

### Add or move a static buffer

1. Decide the region: DMA target or source means AXI SRAM or SRAM2, aligned and sized to 32 bytes; CPU-only
   state means `.dtcm` (or `.sram1` / `.backup` for larger retained tables).
2. Add the attribute and explicit initialisation; no initialiser, no reliance on zero.
3. For a new section name extend `STM32H723ZGTX_FLASH.ld` inside an existing region block. In AXI it must
   sit before `._user_heap_stack`; `check_memory.py` rejects any AXI section that ends after `__heap_end`
   and any DTCM section that ends after `_edtcm`.
4. If a checked size or symbol changes (`flux_fallback`, `write_ring`, heap, USB RX rings), change the
   linker ASSERT, `validate()` in `check_memory.py` and `budget_tests()` in `firmware/tests/test_memory.py`
   together.
5. Build; read `firmware/build/memory_usage.json` (`axi_free` at least 32768, `stack_space` at least 16384)
   and put the new figures into the commit message, as earlier commits do.

### Change the PSRAM start-up

1. Keep the phase list in three places in step: the enum in `ufi_psram.h`, `phase_name[]` in
   `ufi_psram.c`, and `PSRAM_PHASES` / `PSRAM_STATES` in `software/ufi_host/ufi_host/protocol2.py`
   (used by `Device2.psram_status()`).
2. Call `trial(phase)` before any step that can hang or fault, and leave through `phase_failed()` on error.
3. Extend the chip and controller model in `firmware/tests/test_psram_init.py` (a failure at every
   controller call is iterated there) and run it.
4. A changed INFO text or status block (32 bytes, schema 1, `ufi_psram_status_pack()`) also changes
   `docs/USB_Protokoll.md` (SELFTEST `'P'`, INFO texts) and section 8.2a of `docs/Bringup_2026-10-24.md`.
5. State in the commit what was not run on a board; none of the hardening stages has bench proof yet.

### Build a qualification image without U11

Configure with `-DUFI_FORCE_INTERNAL_FLUX_STORE=ON`: `ufi_flux_init()` selects the fallback without touching
the controller, INFO shows "PSRAM bypassed (qualification)", `ufi_flux_store_retest()` returns
`UFI_ERR_NOT_IMPL`. The lead-in READ (flag bit6) and the precision reference are unavailable on the fallback.

## Examples

```bash
# native checks for this area (repo root, clang or gcc on PATH)
python firmware/tests/test_memory.py        # heap bounds, store selection, rejected ELF budgets, stack rules
python firmware/tests/test_psram_init.py    # whole ufi_psram.c against a controller and chip model
python firmware/tests/test_regressions.py   # includes dma_layout_tests()

# ARM build: check_memory.py and check_stack.py run after the link; .hex/.bin appear only if both pass
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build
cmake -S firmware -B firmware/build-noram -G Ninja -DUFI_FORCE_INTERNAL_FLUX_STORE=ON

# done criterion
python firmware/tests/check_all.py

# on a board (host tool): status block, or start-up and both write tests again
ufi psram
ufi psram retest

# hardware cross-check without firmware: expects ID 0D 5D
openocd -f tigard-swd.cfg -f target/stm32h7x.cfg -f firmware/tools/psram_probe.tcl
```

Placement pattern for CPU-only state that must not cost AXI SRAM (`ufi_flux.c`):

```c
/* DTCM: the AXI SRAM budget stays unchanged; cleared at flux init, latched at every start. */
__attribute__((section(".dtcm"))) static uint32_t lead_request, lead_ticks, lead_arm;
```

## Prerequisites

- arm-none-eabi-gcc with `nm` and `objdump`, CMake, Ninja, Python 3, STM32Cube H7 V1.11.0 for the build
  and its two budget checks.
- clang or gcc for the native tests; `pip install -r requirements-test.txt` for the whole gate.
- For bench work: SWD probe with OpenOCD, the host tool from `software/ufi_host`.

## Limitations

- `docs/Speicherarchitektur.md` describes the design correctly but its figures stopped in October 2026
  (it names 34600 B free AXI and 24576 B stack space; the commit of the lead-in feature reports 36688 B and
  20704 B). It also predates the narrow-write test, the DMA test and the latch. Trust
  `memory_usage.json` and the code.
- The comment at `FLUX_STORE_FALLBACK_WORDS` in `ufi_firmware.h` says 56k samples; that is the physical
  array, usable are 38,224.
- `check_stack.py` budgets single frames only; call chains, library frames and interrupt nesting are not
  covered. The real high-water mark comes from SELFTEST `stack_free` on a board.
- In `test_psram_init.py` the window is plain host RAM: narrow-write padding, DQS behaviour and DMA writes
  of the real OCTOSPI are not tested there.
- PSRAM stability under DMA and USB load, the 68.75 MHz clock and cache coherence are open bench items.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| Link fails: "AXI SRAM: less than 32 KiB reserve" | new statics in `.data`, `.bss` or `.axi_sram` | move CPU-only state to `.dtcm` with explicit init, or reuse an existing buffer |
| Link fails: "DTCM: .dtcm data leaves less than _Min_Stack_Size for the stack" | too much moved to DTCM | use `.sram1` or `.backup` for CPU-only tables |
| Link fails: "USB RX rings: expected 16 KiB in DTCM" | size of an `rx` ring in `ufi_v2.c` or `ufi_gw.c` changed | keep 2 x 8192 or change ASSERT, `check_memory.py` and `test_memory.py` together |
| Post-build: "flux fallback changed or not DMA reachable" / "write ring outside SRAM2" | symbol renamed, resized or moved | restore, or update `validate()` and `budget_tests()` |
| Post-build: "Missing/stale project stack-usage files; rebuild the firmware" | source added or removed without a clean build, or a `src/*.c` not in the target | delete the build directory and rebuild; add the file to `CMakeLists.txt` |
| Post-build: "Stack frame exceeds 2048 B" or "Unbounded stack use" | large local array, VLA or `alloca` | make the buffer static in a suitable section |
| INFO: "PSRAM off: last start ended in phase N ..." | an earlier start died inside the PSRAM start-up; latch set | power cycle or `ufi psram retest`; the phase number names the step to examine |
| INFO: "PSRAM not answering (id .. ..) gpio .. .." | READ ID wrong; `gpio 0D 5D` means chip fine and controller set-up wrong, `FF FF` means chip silent | check the controller configuration, or the soldering of U11 with `psram_probe.tcl` |
| INFO: "PSRAM found, 32-bit writes unsafe (...)" or "DMA writes unsafe (run N code C)" | narrow writes disturb neighbouring words | board stays on the fallback store by design; do not bypass the test |
| INFO: "PSRAM skipped: safe start (button B)" | button B held at start-up | restart without the button, or `ufi psram retest` |
| Start-up loops with HardFault inside a cache clean | memory-mapped write answered with a bus error (DQSE cleared) | keep `HAL_OSPI_DQS_ENABLE` on the write command |
| Three ERR blinks at start-up, `caps` bit0 clear | flux store is the fallback | read the INFO text and `ufi psram` for state and phase |
| Variable in a NOLOAD section holds an old value after reset | section is not cleared | initialise in code |
