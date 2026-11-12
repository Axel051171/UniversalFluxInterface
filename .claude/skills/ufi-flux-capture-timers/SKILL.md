---
name: ufi-flux-capture-timers
description: >
  Flux read path and time base of the UFI firmware (STM32H723): timer allocation, TIM2 input capture with
  DMA1 stream 0 in double-buffer mode, the 275 MHz time base with INDEX, SYNC and the index simulation on
  that same base, overcapture and overflow handling, the ring store and the compact stream sent over USB
  while the disk turns, interrupt priorities and hot-path constraints. Use when changing ufi_flux.c,
  ufi_stream.c, ufi_sync.c, ufi_sched.c, ufi_clock.c or stm32h7xx_it.c, when adding a timer channel or an
  interrupt, when a capture ends with a DMA, overflow or no-index error, or when the stream code gets a new
  element. Trigger phrases: "flux capture", "input capture DMA", "timer allocation", "index pulse",
  "capture overrun", "stream encoder", "ISR priority", "Flux lesen", "Timer-Zuteilung", "Zeitbasis 275 MHz",
  "Indeximpuls", "Capture bricht ab", "Lesestrom", "Interrupt-Priorität". DO NOT use for: where the buffers
  live, PSRAM start-up and cache or linker rules (ufi-memory-psram); TIM3 write DMA and write gate
  (ufi-write-path-safety); frame layout, opcodes and status codes on the wire (ufi-usb-protocol);
  Greaseweazle command set (ufi-usb-modes-gw-msc); pin and alternate-function tables (ufi-board-pins);
  drive select, seek and motor (ufi-drive-interfaces); how the gate scripts work (ufi-test-gate).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "flux-capture", "timers", "dma", "streaming"]
---

# UFI Flux Capture and Timers

## Purpose

Describes how the firmware timestamps every flux transition with TIM2 at 275 MHz, stores the stamps by DMA,
cuts them into revolutions at index pulses on that same counter, and streams them to the host while the disk
turns. It lists the invariants that keep stamps exact and loss visible, and the touchpoints that must change
together when the capture, the stream code or a timer assignment changes.

## Map

| File | Role |
|---|---|
| `firmware/src/ufi_clock.c` | `SystemClock_Config()`: HSE 25 MHz, PLL M5/N220/P2 = 550 MHz, HCLK /2, all APB /2, so every timer runs at 275 MHz |
| `firmware/src/ufi_flux.c` | TIM2 + DMA1_Stream0 capture, index handling, loss checks, finalising, store selection |
| `firmware/src/ufi_stream.c` | main-loop encoder: ring store to compact stream (v1, v2 DATA frames, Greaseweazle code) |
| `firmware/src/ufi_sync.c` | SYNC sensor on TIM8_CH1 (v1.0 boards only), mapped into TIM2 time |
| `firmware/src/ufi_diag.c` | index simulation on TIM15 (`TIM15_IRQHandler`) |
| `firmware/src/ufi_sched.c` | head steps at fixed times after index 0 (`ufi_sched_arm()` from the index ISR) |
| `firmware/src/stm32h7xx_it.c` | TIM2, DMA1_Stream0/1, TIM3, OTG_HS handlers; TIM8 and TIM15 handlers sit in their modules |
| `firmware/include/ufi_firmware.h` | `FLUX_TIMER_FREQ`, `REVOLUTIONS_BUFFER` (20), `REVOLUTIONS_STREAM` (200), `capture_state_t`, the `ufi_flux_*` / `ufi_stream_*` API |
| `docs/Timer_Zuteilung.md` | binding allocation table; a new assignment is entered there first |

## Timer and interrupt allocation (as coded)

| Timer | Use | IRQ priority (preempt, sub) |
|---|---|---|
| TIM2, 32 bit, free-running, prescaler 0 | CH1 RDATA capture (no filter) by DMA1_Stream0; CH2 INDEX capture (`INDEX_FILTER` 0x3, about 30 ns) by interrupt; CH3 Apple/Mac RDDATA capture, same DMA stream; CC4 compare for the delayed write start; update interrupt counts `timer_high` | `TIM2_IRQn` 0,0 |
| DMA1_Stream0 | capture chunks, transfer complete and errors | 1,0 |
| TIM3 + DMA1_Stream1 | write path (`ufi_write.c`) | 0,1 |
| TIM8, 16 bit | CH1 SYNC capture, update interrupt as overflow counter | 0,3 |
| TIM15 | index simulation; update interrupt calls `ufi_flux_index_event(TIM2->CNT)` in internal mode | 0,3 |
| OTG_HS | USB | 5,0 |

Docs versus code: `docs/Timer_Zuteilung.md` and one comment in `ufi_flux.c` name TIM7 for the internal index
simulation; no TIM7 code exists, the event comes from the TIM15 update interrupt. `ufi_firmware.h` still
says "TIM2/TIM5" and mentions a CM5 host; both are leftovers.

## Invariants and traps

1. **One counter for flux and index.** RDATA, INDEX and RDDATA are capture channels of the same TIM2 counter,
   so revolution borders are exact. An index source on another timer must be converted to TIM2 time before
   `ufi_flux_index_event(t)`; `ufi_sync.c` reads `TIM2->CNT` and `TIM8->CNT` back to back and calls
   `ufi_sync_map_t2()` (`ufi_sense_calc.c`). That mapping holds only while the latency stays below one TIM8
   period (238 us).
2. **All index producers run at preemption level 0** (TIM2, TIM8, TIM15) and so never nest; the capture DMA
   ISR is level 1. `dma_pos()` relies on this: it is called from level 0 and adds a pending, unserviced
   transfer-complete itself. A new ISR that calls `ufi_flux_index_event()` or `dma_pos()` must use level 0.
3. **TIMx_SR is cleared by writing zeros.** Use `capture_clear_status()` (`TIM2->SR = ~bits`), never
   `SR &= ~bits`: a read-modify-write wipes flags that were set in between, including the overcapture
   flags. Reading `TIM2->CCR2` clears CC2IF.
4. **Loss is an error, never silent.** `capture_check_loss()` runs first in `ufi_flux_tim2_irq()`,
   `ufi_flux_dma_irq()` and `ufi_flux_poll()`. An overcapture flag (CC1OF or CC3OF for RDATA, CC2OF for
   INDEX) halts the capture with `error_code` 2. `capture_halt()` disables the capture channel before it
   reads the flags, freezes the DMA, then takes `end_pos`. Keep that order.
5. **`error_code` values**: 1 store full, host too slow or sample cursor near its 32-bit limit (the stream
   reports `UFI_ERR_BUFFER_FULL`), 2 DMA error or overcapture (`UFI_ERR_DMA`), 3 no index within
   `INDEX_TIMEOUT_MS` 600 (`UFI_ERR_NO_INDEX`), 4 media changed (`UFI_ERR_MEDIA_CHANGED`).
6. **DMA is driven by registers, not by the HAL.** A stream counts at most 65535 items, so the store is
   filled in chunks of `chunk_words` (at most `CHUNK_MAX_WORDS` 32768, a multiple of 8 words) in
   double-buffer mode; the ISR points the idle memory register (M0AR or M1AR by the CT bit) at the next
   chunk. When nothing is free it points at `dma_scratch`, one full chunk behind the usable store. That
   block must stay a full chunk: a late ISR lets the DMA write a whole NDTR there.
7. **RDATA starts at the first index pulse** (`capture_rdata_arm()` inside `ufi_flux_index_event()`), except
   for an index-less capture (`period_ticks` nonzero), a Greaseweazle capture (`ufi_flux_capture_gw()`), and
   the lead-in (`ufi_flux_lead_in()`, v2 READ flag bit6, PSRAM store only).
8. **Stamp arithmetic wraps.** TIM2 overflows every 15.6 s. Compare stamps as `(int32_t)(a - b)`; for spans
   that may exceed one wrap use `ufi_flux_now64()`, which accounts a pending update flag under PRIMASK.
9. **Two shapes of store contents.** After a non-streamed capture `ufi_flux_poll()` finalises in place:
   borders moved to the exact index stamp (`align_boundary()`), samples made relative to their index, at
   most `REVOLUTIONS_BUFFER` revolutions. While streaming (`ufi_flux_set_streaming(true)`) the store is a
   ring of absolute stamps, logical sample n at `n % store_words`, and nothing is finalised.
   `ufi_flux_get_revolution()` returns NULL for a streamed capture.
10. **The CPU must drop its cache before reading what the DMA wrote.** `encode()` invalidates 32-byte
    aligned slices right before reading, `ufi_flux_poll()` invalidates the used range, and
    `ufi_flux_capture_start()` calls `SCB_CleanInvalidateDCache()` so no dirty line is evicted over fresh
    DMA data. A new reader of live samples needs the same step.
11. **The streamer must not outrun the DMA or the index ISR.** While the capture runs, `encode()` leaves the
    newest word alone (`wr--`), stops at stamps younger than `TS_MARGIN` (2750 ticks, 10 us) so the index
    interrupt for older samples has run, and does at most `STREAM_WORK_WORDS` (1024) per call. Each loop step
    may add `ROOM` (32) bytes at most; a new stream element that can exceed this needs a larger `ROOM`.
12. **Ring reuse is gated by the streamer.** `ufi_flux_stream_consumed(rd)` frees chunks; without a free
    chunk the ISR sets `stream_scratch` and the capture ends with `error_code` 1. The fallback store has two
    chunks, the PSRAM store 63 (31 with the reference area of `ufi_flux_reference_reserve()`).
13. **Without READ flag bit6 the stream is byte-identical to earlier firmware.** `lead_in()` in
    `firmware/tests/test_stream_runtime.py` pins length and digest of a synthetic capture. New stream
    content must be opt-in.
14. **Store contents are shared with the write path and other modules.** Every CPU writer calls
    `ufi_flux_invalidate()` first; it bumps `store_generation`, so `ufi_flux_archive_id()` returns 0.
15. **New state goes to `.dtcm`, and `.dtcm` is not zeroed.** The AXI SRAM reserve is nearly used up, so
    recent additions carry `__attribute__((section(".dtcm")))`. That section is NOLOAD: an initialiser has
    no effect, the variable must be set in an init or start function (see `lead_request` in
    `ufi_flux_init()`). DMA cannot reach DTCM.
16. **Long loops over the store feed the watchdog** every 1024 words (`UFI_WATCHDOG_FEED()`), as in
    `ufi_flux_poll()` and `ufi_flux_loss_snapshot()`.
17. **No measured MCU timing exists.** `docs/Firmware_CortexM7_Optimierung_2026-10-22.md` states that ITCM
    placement, LTO, inlining and interrupt budgets need bench data. The default stays
    `UFI_RELEASE_OPTIMIZATION=2`; 3 and s are comparison builds only. Host benchmarks
    (`benchmark_mfm_fastpaths.py`) say nothing about the Cortex-M7.

## How the regression tests bind to the source

`firmware/tests/test_capture_integrity.py`, `test_stream_runtime.py` and `test_memory.py` do not link the
module. `function('ufi_flux.c', 'name')` in `test_regressions.py` cuts the production function out of the
source by a regular expression and compiles it with hand-written doubles for registers and for every static
it touches. Consequences:

- A function definition must start in column 0 and end with the first `}` in column 0.
- Renaming or splitting an extracted function, or using a new static, helper or register in it, breaks the
  test build until the double is added in the test file. The extracted names are listed in each test
  (`dma_pos`, `capture_halt`, `capture_check_loss`, `ufi_flux_capture_start`, `ufi_flux_index_event`,
  `ufi_flux_tim2_irq`, `ufi_flux_dma_irq`, `ufi_flux_written`, `ufi_flux_loss_window`, `encode`,
  `lead_trim`, `lead_open`, `configure_store`, `select_store`, ...).
- The tests model `capture_clear_status()` as the only hardware primitive, which is why invariant 3 matters.
- `test_precision_runtime.py` compiles the whole `ufi_precision.c` with the `ufi_firmware.h` include removed.

## Procedures

### Change capture or ISR logic in `ufi_flux.c`

1. Read the affected function and its cases in `test_capture_integrity.py` first.
2. Keep the entry order of the ISRs: update flag, `capture_check_loss()`, CC4 compare, CC2 index.
3. Guard state shared with the thread side by PRIMASK save and restore (`__get_PRIMASK()`,
   `__disable_irq()`, `__set_PRIMASK(mask)`), as `ufi_flux_written()` does; the tests assert that an
   already masked caller stays masked.
4. Add the test case and any new double, then run the commands under Examples.
5. If a status or `error_code` reaches the host, update `stream_service()` in `ufi_stream.c`, the READ info
   path (`ufi_flux_loss_window()`), `docs/USB_Protokoll.md` and the host (skill ufi-usb-protocol).

### Add or change a stream element in `ufi_stream.c`

1. Make it opt-in by a READ flag; update the format comment at the top of `ufi_stream.c`.
2. Emit it through a `put_*` helper, check `fill <= cap - ROOM` before, raise `ROOM` if needed.
3. Handle all three framings: v1 header, v2 DATA frame (`cap = UFI_V2_MAX_PAYLOAD`, less `UFI_JOB_HEADER`
   during a job), Greaseweazle (`gw`, ticks shifted by `GW_TICK_SHIFT`, no private elements).
4. Host decoder: `decode_stream()` / `decode_stream_lead()` in `software/ufi_host/ufi_host/protocol.py`,
   with tests under `software/ufi_host/tests`.
5. Extend `test_stream_runtime.py`; the pinned digest of the default stream must not change.
6. Document it in `docs/USB_Protokoll.md`.

### Assign a timer channel or add an interrupt

1. Enter it in `docs/Timer_Zuteilung.md` first; pins go to `firmware/include/board.h` in that commit
   (skill ufi-board-pins). TIM2 has no free channel; TIM5 shares PA0 to PA3 with TIM2.
2. Use prescaler 0 so one tick equals one TIM2 tick; a 16-bit timer needs an overflow counter and the
   pending-update check of `TIM8_CC_IRQHandler()`.
3. Pick the priority from the table above; an ISR that touches capture state uses preemption level 0.
4. Put the handler in `stm32h7xx_it.c` or in the owning module, wrapped in `ufi_profile_begin()` /
   `ufi_profile_end(UFI_CYCLE_...)` when it is a hot path.
5. A change of `SystemClock_Config()` dividers changes `FLUX_TIMER_FREQ`, `GW_SAMPLE_FREQ`, the
   `sample_hz` value the host reads from INFO, and SELFTEST item `timer`; avoid it.

## Examples

```bash
# focused checks for this area (repo root, clang or gcc on PATH)
python firmware/tests/test_capture_integrity.py
python firmware/tests/test_stream_runtime.py
python firmware/tests/test_precision_runtime.py
python firmware/tests/test_regressions.py
python firmware/tests/run_tests.py cycle_profile
python firmware/tests/run_tests.py fastpaths

# done criterion: the whole offline gate and the ARM build with its memory and stack checks
python firmware/tests/check_all.py
cmake -S firmware -B firmware/build -G Ninja
cmake --build firmware/build
```

Stamp comparison and status acknowledge as used throughout `ufi_flux.c`:

```c
if (lead_ticks && (int32_t)(t - lead_arm) < (int32_t)lead_ticks) return;  /* wrap-safe "earlier than" */
TIM2->SR = ~(TIM_SR_CC2IF | TIM_SR_CC2OF);                                /* write zero to clear */
```

## Prerequisites

- clang or gcc for the native tests; `pip install -r requirements-test.txt` for the full gate.
- arm-none-eabi-gcc, CMake, Ninja and STM32Cube H7 V1.11.0 for the firmware build.
- `docs/USB_Protokoll.md` sections 4.24 (READ info, loss window) and 4.25 (lead-in) for wire meaning.

## Limitations

- The tests run extracted control flow against register doubles; they are no cycle-accurate model and
  prove no interrupt latency, DMA request rate or throughput.
- A capture with a drive has not run on a board yet (bench run of 2026-11-11 was without drive); the
  margins `TS_MARGIN`, `STALL_MS` and `INDEX_TIMEOUT_MS` are design values.
- `check_stack.py` budgets single frames, not call chains or interrupt nesting.
- Write timing on TIM3 and the buffers' memory placement are outside this skill.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| Test build fails with `cannot extract production function ufi_flux.c:NAME` | function renamed, or its definition or closing brace no longer in column 0 | restore the layout or update the name list in the test |
| Test build fails with an undeclared identifier inside an extracted function | new static, helper or register without a double | declare the double in the test harness string |
| `regression_stream_lead_in` digest assertion fails | default stream bytes changed | make the new content opt-in; never update the digest for a default-path change |
| READ ends with status `UFI_ERR_DMA` and `capture_rdata_loss` counts up | RDATA overcapture: DMA request not served before the next edge | look for a long masked section or a level 0 ISR that runs too long; do not mask the flag |
| READ ends with `UFI_ERR_BUFFER_FULL`, `capture_overruns` counts up | ring had no free chunk: host or main loop too slow, or fallback store with two chunks | check `capture_peak_words`, the PSRAM state (skill ufi-memory-psram), blocking work in the main loop |
| READ ends with `UFI_ERR_NO_INDEX` | no pulse for 600 ms: no disk, motor off, wrong index source (SYNC selected, simulation internal) | check `ufi_flux_index_source()`, `ufi_flux_index_source_sync()`, drive state |
| Index stamps jitter by a whole chunk or revolutions have wrong sample counts | `dma_pos()` called from a lower-priority context without PRIMASK | call it at level 0 or under PRIMASK as `ufi_flux_written()` does |
| Variable in `.dtcm` holds garbage after reset | NOLOAD section, initialiser ignored | assign it in the init or start function |
| Lead-in READ answers status 11 | flag bit6 with index-less read, Apple/Mac without sync sensor, or fallback store | use the PSRAM store and an indexed drive |
