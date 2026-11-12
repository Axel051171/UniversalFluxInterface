---
name: ufi-bringup-diagnostics
description: >
  Start-up order of the UFI firmware (ufi_init in ufi_main.c), safe start and ROM bootloader buttons,
  reset-cause reporting (RCC_RSR), independent watchdog IWDG1, fault capture into DTCM and CRASH.LOG,
  the SELFTEST op 0x07 item table, debug/diagnostic commands, drive-rail overcurrent and eFuse
  monitoring, and the bench bring-up run of a new board with software/ufi_host/ufi_host/bringup.py.
  Use when changing ufi_main.c, ufi_selftest.c, ufi_crash.c, ufi_diag.c, ufi_debug.c, ufi_power.c,
  ufi_sense.c, ufi_board.c or bringup.py, when adding a self-test item or a bring-up step, when a
  board does not enumerate, loops in reset, or reports an unexpected reset cause, and when
  interpreting a bring-up report. Trigger phrases: "bring-up", "selftest item", "reset cause",
  "watchdog", "safe start", "crash log", "board does not boot", "Inbetriebnahme", "Selbsttest
  erweitern", "Platine startet nicht", "Reset-Ursache", "Prüfstandslauf", "sicherer Start",
  "Absturzaufzeichnung". DO NOT use for: PSRAM start-up phases, latch and memory layout
  (ufi-memory-psram), flashing and CMake toolchain (ufi-firmware-build-flash), the gate itself
  (ufi-test-gate), pin tables (ufi-board-pins), v2 frame format (ufi-usb-protocol), LAN
  (ufi-lan-w5500), write lock logic (ufi-write-path-safety).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "bringup", "selftest", "watchdog"]
---

# UFI bring-up and diagnostics

## Purpose

Describes what runs between reset and the main loop, which diagnostics the firmware offers to judge a
freshly assembled board, and how the host bring-up script turns them into a PASS/FAIL report. The
ordering rules here come from the first bench run of 2026-11-11 (two v1.0 boards looped in reset
without USB) and the seven "Hardening" commits that followed.

## Start-up order (ufi_init, firmware/src/ufi_main.c)

`main()` = `ufi_check_bootloader()` → `ufi_init()` → `ufi_main_loop()`. The order inside `ufi_init()`
is load-bearing:

| # | Call | Why it sits there |
|---|---|---|
| 1 | `ufi_stack_paint()` | paints free DTCM below the stack pointer; SELFTEST `stack_free` reads it |
| 2 | `ufi_crash_capture_reset()` | copies `RCC->RSR` and clears the flags before any later code can reset again |
| 3 | `HAL_Init()`, `SystemClock_Config()`, `UFI_DWT_INIT()`, `ufi_cycles_init()` | 550 MHz, cycle counter |
| 4 | `UFI_WATCHDOG_INIT()` | IWDG1 starts here; every later step must finish or feed within 32.8 s |
| 5 | `board_gpio_init()`, `HAL_Delay(5)`, read `PIN_BTN_A` / `PIN_BTN_B` | buttons before the first step that may never return |
| 6 | `ufi_psram_skip(...)` when B is held | must precede `ufi_flux_init()` |
| 7 | `ufi_drive_init()`, `ufi_iec_init()`, `ufi_flux_init()`, `ufi_write_init()` | `ufi_flux_init()` runs the PSRAM start-up and DMA write test |
| 8 | `ufi_board_init(!safe_mode)` | sensing init, then FDD_5V, 50 ms, FDD_12V; nothing in safe mode |
| 9 | `ufi_clock_rtc_init()` only for `ufi_board_rev() >= 10` | v0.x has no LSE crystal |
| 10 | `ufi_v2_init()` then `ufi_usb_init()` | transport routing state exists before USB can deliver |
| 11 | `ufi_sd_init()`, `ufi_config_load()`, `ufi_lan_init()` | LAN needs LAN.CFG from the SD NAND |
| 12 | `ufi_crash_boot()` | needs the file system: writes CRASH.LOG |
| 13 | `ufi_mode_init()`, three ERR blinks when `BOARD_HAS_PSRAM && !ufi_flux_store_is_psram()` | |

## Invariants and traps

- **Buttons before PSRAM.** A = `ufi_request_bootloader()` (ROM DFU), B = safe start. They were read
  after `ufi_flux_init()` and were unreachable while the board looped in the PSRAM test. Never move
  code that touches external hardware above the button read.
- **Safe start means three effects:** PSRAM untouched (INFO text `PSRAM skipped: safe start (button
  B)`), drive rails stay off, ERR LED on, `BOARD_STATUS` flags bit6 set. On v1.0 the buttons are front
  header J12 pins 13/14 to GND; SW3/SW4 are not fitted.
- **Bootloader entry survives a reset through DTCM.** `g_boot_magic` lives in `.dtcm` (NOLOAD, not
  cleared by start-up code); `ufi_check_bootloader()` must stay the first statement of `main()`.
  The same property means every other `.dtcm` object holds garbage after power-on: validate it
  (`crash_rec_t` uses magic + checksum) or clear it in an init function (`ufi_sense_init()`).
- **Reset cause is captured once, reported later.** `ufi_crash_capture_reset()` stays the second
  statement. `ufi_crash_boot()` only formats. INFO shows all causes joined by `+` plus the raw value,
  e.g. `reset: power-on+software (rsr 01FA0000)`; implied flags are suppressed (brown-out and pin with
  power-on, pin with software/watchdog). With a fault record the line is `reset: HardFault pc=...
  lr=... cfsr=... (CRASH.LOG)`. Nothing on the host parses this text; keep it display-only.
- **Faults reset at once.** `UFI_FAULT_HANDLER` in `stm32h7xx_it.c` branches to `ufi_crash_fault()`,
  which stores the frame in DTCM and calls `NVIC_SystemReset()`. A fault inside start-up therefore
  becomes a reset loop without USB. Any new start-up step that can fault needs a way out (the PSRAM
  code uses a trial marker; see ufi-memory-psram).
- **Watchdog is real since Hardening 2/7.** CMake option `UFI_WATCHDOG` (default ON) defines
  `USE_WATCHDOG`; before that the feed macros expanded to nothing. Period `UFI_WATCHDOG_PERIOD_MS`
  32768 (prescaler 256, reload 4095) in `ufi_fixes.h`, frozen while a debugger halts the core. The
  period is the longest IWDG offers on purpose: the feed sites in long operations are unproven on a
  drive. Do not shorten it without bench data. A new blocking loop needs `UFI_WATCHDOG_FEED()` and a
  bounded HAL timeout (PSRAM uses `PSRAM_HAL_TIMEOUT_MS` 100 instead of the HAL default 5 s).
- **Two self-tests exist.** `ufi_selftest()` (v2 op 0x07, `ufi_selftest.c`) is the bring-up table.
  `ufi_debug_selftest()` in `ufi_debug.c` is the old v1 bitmask that only checks clock-enable bits.
- **SELFTEST ids are a wire contract.** Item = `id u8, status u8, value u32`; status 0 ok, 1 fail,
  2 skipped, 3 warning. Ids 1-13, then 15-22, and id 14 `log` is always last. Ids 1-13 are also
  callable one by one through `ufi_selftest_selected()` (13-bit mask, used by the analysis job).
- **Items must not disturb a drive.** Items that drive lines skip unless `ufi_drive_get_current() ==
  DRIVE_NONE` and no write is pending (`t_wgate_lock`, `t_index_loop`, `t_fdd_lines`). `t_psram` and
  `t_psram_dma` destroy the flux store and skip during capture or write.
- **Measurement windows start on an edge.** `t_timer()` waits for a tick edge (was 19-20 ms, up to
  -5 %); `t_usb_sof()` restarts when the frame number jumps by more than 4 (one jump gave about
  -1 000 000 ppm). Keep both when touching these items.
- **Rail protection has two layers** (`ufi_board.c`): firmware trip above `OVERCURRENT_MA` 1500 for
  `OVERCURRENT_SAMPLES` 5 samples of 10 ms (rail off, `trip_mask`, ERR LED, `UFI_V2_EVT_POWER`), and on
  v1.0 the TPS25921A eFuse `PIN_FLT5` / `PIN_FLT12` (`fault_mask`, `board_status.power` bits 2/3, rail
  stays on because the eFuse retries). FLT is also low while the rail is disabled, so the firmware
  counts it only for an enabled rail; SELFTEST `fdd_pins` still warns then and `bringup.py`
  `_fdd_pins_verdict()` judges FLT against `board_status.power`.
- **Current scale depends on the board revision:** 1 mV/mA on v1.0 (50 mR shunt), 2 mV/mA before.
- **State placement.** AXI SRAM `.bss/.data` is frozen at the gate value; sensing state sits in DTCM,
  and the linker asserts that `.dtcm` leaves `_Min_Stack_Size` free.
- **HAL-bound modules have no native test.** `ufi_crash`, `ufi_debug`, `ufi_diag`, `ufi_power`,
  `ufi_selftest`, `ufi_sense`, `ufi_board`, `ufi_main` are in `NEEDS_REAL_HAL` of
  `firmware/tools/check_static.py`. Pure logic belongs in a HAL-free file like `ufi_sense_calc.c`
  (tested by `firmware/tests/test_sense.c`); `ufi_selftest_selected()` is extracted verbatim by
  `firmware/tests/test_analysis_job.py`, so its signature and loop are pinned.

## SELFTEST items (ufi_selftest.c)

| id | name | fail / warn means |
|---|---|---|
| 1 `timer` | TIM2 ticks per ms, 275 000 ± 2 % | fail = clock tree |
| 2 `usb_sof` | ppm against 128 USB frames | skip = not enumerated, warn beyond ±500 |
| 3 `psram`, 22 `psram_dma` | pattern over top 64 KB; capture DMA stream writes | 22 warn = no DMA request arrived (test broken, not the PSRAM) |
| 4 `sd`, 14 `log` | capacity MB; SELFTEST.LOG bytes | |
| 5/6 `rail_5v/12v`, 18/19 `v_fdd5/12` | mA; mV at the connector | fail = trip, or below 4.5 V / 10.8 V with the rail on |
| 7 `board_id`, 8 `usbc_cc` | divider mV; advertised mA | warn = unknown revision / no CC |
| 9 `fdd_lines`, 10 `iec_loop`, 11 `index_loop` | idle bus inputs, IEC loopback, index simulation | 11 skip = pulse did not arrive |
| 12 `last_reset`, 13 `stack_free` | fault kind; unused stack bytes | 13 fail below 512, warn below 2048 |
| 15 `rtc`, 16 `sht40`, 17 `sync` | v1.0 sensing | fail on v1.0, skip on v0.x |
| 20 `wgate_lock`, 21 `fdd_pins` | value 1 = lock leaks, 2 = WGATE active at idle | |

## Procedure: add a SELFTEST item

1. `firmware/src/ufi_selftest.c`: write `static void t_name(st_item_t* it)`; append the name to
   `item_names[]` at index = id (next free id is 23); add `ITEM(23, t_name(&items[n]));` before the
   `#undef ITEM`. Check capacity: `items[24]` and the `24` in `ufi_v2.c` case `V2_SELFTEST` hold 21
   items plus `log` today; `text[800]` stops adding SELFTEST.LOG lines silently near 760 bytes.
2. Skip (status 2) when the hardware is absent (`ufi_board_rev() < 10u`) or a drive is selected; do not
   fail a v0.x board for a v1.0 part.
3. Host: `SELFTEST_ITEMS` in `software/ufi_host/ufi_host/protocol2.py`; `SELFTEST_EXPECT` and, when the
   part is mandatory on v1.0, `V10_STRICT` in `bringup.py`.
4. Host test: append to `V10_SELFTEST` in `software/ufi_host/tests/test_bringup.py`. Append only: tests
   address existing entries by list index (`selftest_items[17]` is id 19, `[18]` id 20, `[19]` id 21).
5. Docs: SELFTEST paragraph and op 0x07 row in `docs/USB_Protokoll.md`; bench expectation in
   `docs/Bringup_2026-10-24.md` section 7.4.
6. Run the gate (see Examples).

## Procedure: add a bring-up step (bringup.py)

1. Add `_step(dev, rep, ctx, ...)` and call it from `run()`. Use `rep.guarded()` for requests so a
   refusal becomes `skip`; use `ctx.v10` / `ctx.a10` to skip on v0.x boards and pre-A10 firmware.
2. Keep the safety contract: nothing moves a head without `--drive`, nothing opens WGATE without
   `--write-test`.
3. Extend the fake device in `test_bringup.py` (`FakeV10`) and add a fault case to `test_v10_faults`.
4. Add the row to the table in `docs/Bringup_2026-10-24.md` section 7.4.

## Procedure: change the start-up sequence

1. Place the new call by the table above; state in a comment which earlier step it depends on.
2. Bound every wait and decide what a failure reports (INFO text, SELFTEST item, LED) instead of
   hanging; a hang now ends in `reset: ... watchdog` after 32.8 s.
3. If the step can fault, add a native model test in the style of
   `firmware/tests/test_psram_init.py` and register the group in `firmware/tests/check_all.py`.
4. Record bench status honestly in `docs/Bringup_2026-10-24.md` section 8.2a ("not checked on
   hardware" is the norm for the hardening stages).

## Examples

```bash
# build (watchdog on by default; -DUFI_WATCHDOG=OFF only for a debugging image)
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build

# done criterion: offline gate green, plus the host tests of the area
python firmware/tests/check_all.py
PYTHONPATH=software/ufi_host python -m pytest software/ufi_host/tests/test_bringup.py -q -p no:cacheprovider

# bench run of a v1.0 board (from software/ufi_host)
python -m ufi_host.bringup --port COMx --expect-rev 10 --report bringup_v10.json
python -m ufi_host.bringup --port COMx --expect-rev 10 --no-12v      # USB-C only
ufi info ; ufi selftest ; ufi psram ; ufi psram retest ; ufi power 3 ; ufi rails
```

Pattern of an item that respects the rules above:

```c
static void t_rtc(st_item_t* it)
{
    ufi_rtc_time_t t;
    if (!ufi_sense_rtc_get(&t)) {
        it->status = ufi_board_rev() >= 10u ? ST_FAIL : ST_SKIP;
        return;
    }
    it->value = ufi_calendar_yyyymmdd(t.year, t.mon, t.day);
    it->status = ST_OK;
}
```

## Prerequisites

- arm-none-eabi-gcc, Ninja, STM32Cube H7 V1.11.0 for the cross build; clang or gcc and
  `pip install -r requirements-test.txt` for the gate.
- Bench: current-limited supply, SWD probe on J4 (ST-LINK, or Tigard with the OpenOCD config in
  `docs/Bringup_2026-10-24.md` section 8.1), USB-C cable with CC, scratch drive and scratch disk for
  write tests only.
- Reading order: `docs/Bringup_2026-10-24.md` sections 7 and 8, then
  `kicad/UFI_Headless/docs/Inbetriebnahme.md` (written for v0.6, cable notes for v1.0).

## Limitations

- Hardening stages 1-7 were built and passed the offline gate but have not run on a board; only the
  pre-hardening firmware passed the bench run (47 ok, 3 warn, 0 fail, USB-C only, no drive, no 12 V,
  no LAN cable).
- No evidence yet that every long operation feeds the watchdog within 32.8 s, or that a real hang ends
  in a reset.
- USB-DFU through the ROM bootloader did not enumerate on either v1.0 board; cause open. Flash over SWD.
- Only the last fault is kept (one record, one CRASH.LOG); without a working SD NAND the record is
  still cleared after the INFO line is built.
- `SELFTEST.LOG` and CRASH.LOG need the SD NAND; `log` fails without it and `bringup.py` tolerates
  that on v0.x only.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| Board resets in a loop, never enumerates | fault during start-up, handler resets at once (2026-11-11: bus error from a memory-mapped PSRAM write) | hold button B (J12 pin 14 to GND) for a safe start, then read `ufi info` and `CRASH.LOG` |
| INFO says `power-on` after a software reset | stale RCC_RSR flags; capture ran late or was moved | keep `ufi_crash_capture_reset()` as second statement of `ufi_init()` |
| `reset: ... watchdog` after a long read, write, format or dump | a loop ran 32.8 s without `UFI_WATCHDOG_FEED()` | add the feed in that loop; do not disable `UFI_WATCHDOG` in a release image |
| `selftest.timer` varies by up to -5 % | window not started on a tick edge | keep the edge wait in `t_timer()` |
| `selftest.usb_sof` near -1 000 000 ppm | frame-number jump counted as frames | keep the restart on `step > 4u` in `t_usb_sof()` |
| `selftest.v_fdd12` FAIL on a USB-only bench | rail enabled without a 12 V input | run `bringup.py` with `--no-12v` |
| `fdd_pins` warn with FLT5/FLT12 while rails are off | ENUV low reads as undervoltage on the TPS25921A | expected; judged by `_fdd_pins_verdict()` |
| `features.v10` complained about SYNC | SYNC exists only with `apple_sync=1` in UFI.CFG | reported as a note by `bringup.py` |
| `index_loop` skip on v1.0 | PE5 → U7F → INDEX path broken (hardwired on v1.0) | strict item in `V10_STRICT`; check U7F |
| `rtc` fail on v1.0 | LSE not oscillating within 2 s | check Y3/C76/C77/BT1 |
| Three ERR blinks at power-up | PSRAM fitted but not in use | `ufi psram`; continue in ufi-memory-psram |
| pytest step of `check_all.py` fails with "No module named pytest" | gate interpreter lacks the test requirements | `pip install -r requirements-test.txt` in that interpreter |
