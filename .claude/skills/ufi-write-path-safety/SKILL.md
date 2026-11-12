---
name: ufi-write-path-safety
description: >
  Safety rules, invariants and change procedures for every UFI firmware path that can put flux onto a
  disk: the write gate (hardware lock switch plus firmware enable WLOCK_FW, session write lock, drive
  write protect), the WRITE state machine in ufi_write.c, the compact codec and u16 packing with
  precompensation, the TIM3 + DMA ring stream, the extended waveform backend, verify levels and the raw
  fingerprint check, ERASE / PATTERN diagnostics, abort and cancel behaviour, FORMAT, and the hardware
  acceptance plan docs/Abnahme_Schreibpfad.md. Use when changing ufi_write.c, ufi_write_codec.c,
  ufi_write_stream.c, ufi_write_wave.c, ufi_verify.c, ufi_raw_verify.c, ufi_cancel.c, ufi_format.c or the
  WRITE parsing in ufi_v2.c, when adding a WRITE option or a new command that writes, when a write
  regression fails, or when reviewing whether a change can open WGATE. Trigger phrases: "write path",
  "WGATE", "write gate", "write lock", "precompensation", "verify level", "raw check", "erase track",
  "abort a write", "Schreibpfad", "Schreibsperre", "Schreibschutz", "Prüflesen", "Spur löschen",
  "Abnahme Schreibpfad", "darf das schreiben". DO NOT use for: seek, motor, drive select and drive
  profiles (ufi-drive-interfaces); TIM2 capture and index timing (ufi-flux-capture-timers); v2 frame
  format, status codes and the dispatcher (ufi-usb-protocol); Greaseweazle and USB floppy modes
  (ufi-usb-modes-gw-msc); the flux store and PSRAM (ufi-memory-psram); how the offline gate and the
  fuzzers are run (ufi-test-gate, ufi-fuzz-static-analysis); pin assignments (ufi-board-pins).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "write-path", "safety", "verify"]
---

# UFI Write Path Safety

## Purpose

Describes how the firmware decides that flux may reach a disk, how the write waveform is produced, and
which checks must stay in place when that code changes. A wrong change here destroys media that cannot
be replaced, so the skill lists the invariants first and the tests that pin each of them.
The write path has never run on hardware: `docs/Abnahme_Schreibpfad.md` is a plan, not a result.

## Invariants that must never be weakened

1. **Refuse before the head moves.** `ufi_write_start()` runs `pack_deltas()` (range check, status 18),
   the reference build, both lock checks, `ufi_drive_write_protected()` and the op 41 speed check
   **before** `ufi_drive_seek_q()` and before WGATE. A new check goes in front of the seek, never behind.
2. **Two independent gates.** Hardware: on board v1.0 the front switch SCHREIBEN (read back on
   `PIN_WREN_SENSE`, high = locked, also with the cable off) and the firmware line WLOCK_FW (`PIN_WLOCK`,
   PE4, pulled high through reset) both have to be open or WGATE does not reach the bus. Firmware:
   `ufi_board_write_enable(true)` is called only inside `ufi_write_start()` and `write_finish()` always
   calls `ufi_board_write_enable(false)`. Never open WLOCK_FW at start-up, in idle code or "for the whole
   session". On v0.x PE4 is the WRITE LOCK jumper input; `ufi_board_jumper_locked()` picks the right pin.
3. **The gate is re-checked in the interrupt.** `start_now()` (index or TIM2 CC4 context) tests cancel,
   session lock, hardware lock and drive write protect again right before `bus_out(&PIN_FDD_WGATE, true)`.
   Only IRQ-safe reads belong there; media observation stays in the main loop.
4. **`write_finish()` drops the destructive signal first**: WGATE off, /WRREQ high, WLOCK_FW high, and
   only then timer and DMA stop. Every exit (complete, error, abort, underrun, index cut, timeout) goes
   through it. Do not add an early `return` between gate-on and `write_finish()`.
5. **Every error return of `ufi_write_start()` needs `ufi_write_abort()` by the caller.** Some refusals
   happen after `ufi_board_write_enable(true)`; `ufi_write_service()` and `write_local_run()` abort on
   any non-zero result. A new caller must do the same.
6. **Never write past the index, never call it success.** An index during `WRITE_ACTIVE` on the native
   path sets `index_cut` and ends with `UFI_ERR_TRACK_LONG` (19). Only `erase_pending` (flags2 bit0) and
   the GW path with `terminate_index` end successfully at the index.
7. **Underrun ends the write.** `ufi_write_dma_irq()` treats HT and TC both pending, `ws.underrun`, or
   the other flag pending after `ring_fill()` as an underrun: `write_finish(WRITE_ERROR)`, status 6,
   `ufi_metrics.write_underruns`. No retry, no silent continue.
8. **No clamping unless asked.** `write_pack()` validates the whole track in pass 1 and leaves the input
   untouched on `WRITE_CODEC_ERR_RANGE`; out-of-range periods are clamped only with `cfg.clamp`, and then
   counted in the END payload. GW data is never clamped.
9. **The session lock is engage-only.** `ufi_board_soft_lock()` has no counterpart; only an MCU reset
   clears it. `ufi_board_write_locked()` = session lock OR hardware lock, so GW, USB floppy, dump and
   FORMAT are blocked as "write protected"; the v2 dispatcher answers status 22 through `locked_write()`.
10. **A write is bound to one medium.** `write_media_token` is taken in `ufi_write_prepare()` and compared
    again at start, after seek, after the settle delay, in every service pass and during verify
    (`UFI_ERR_MEDIA_CHANGED`, 21). The GW path is the only exemption.
11. **Verify levels are evidence classes, not a score.** 0 none, 1 timer and DMA finished, 2 read-back
    plausible (transition count within 5 %, at least 100), 3 every reference sector came back with the
    same content, 4 unverifiable. `verify_match_result()` returns 0 on any mismatch or missing sector.
    The raw fingerprint (`ufi_raw_verify.c`) is "a lossy timing/density fingerprint, never a content
    verdict": it yields level 2 at most. Do not let a weaker check report a higher level.
12. **An enabled safety check must not fail open.** If `ufi_diag_rpm()` fails while the speed check is
    on, the write is refused with that error (pinned in `regression_write`).
13. **No USB traffic from interrupts.** IRQ code sets `receive_failed` / `write_start_error`; the main
    loop (`ufi_write_service()`) reports.

## Architecture in one table

| Piece | File | Notes |
|---|---|---|
| State machine, gate, timer, DMA | `firmware/src/ufi_write.c` | `write_state_t`: IDLE, RECEIVING, WAITING_INDEX, ACTIVE, VERIFYING, COMPLETE, ERROR |
| Compact decoder, u32 to u16 packing, precomp | `ufi_write_codec.c/.h` | pure, no HAL; `WRITE_MIN_DELTA` 440 ticks, `WRITE_MAX_DELTA` 65536, `WRITE_PULSE_TICKS` 110 |
| Ring bookkeeping | `ufi_write_stream.c/.h` | pure; two halves of `WSTREAM_HALF` 2048 items, exact stop after the last period |
| Extended waveform | `ufi_write_wave.c/.h` | pure; ARR/reserved/CCR1 burst items, `WAVE_MIN_TICKS` 220, long gaps split into silent segments, weak-cell markers |
| Content reference and match | `ufi_verify.c/.h` | pure; MFM, Amiga and further formats via `mfm_dec_init_format()` |
| Raw fingerprint | `ufi_raw_verify.c/.h` | pure; `RAW_WINDOWS` x 8 interval bins |
| Cancel flag and cancellable delays | `ufi_cancel.c` | `ufi_delay_us()` / `ufi_delay_ms()` return early when cancelled |
| FORMAT | `ufi_format.c` | encodes a track into the flux store, writes with `ufi_write_local()` |
| WRITE request parsing | `ufi_v2.c` `cmd_write()`, `cmd_guarded_write()`, `locked_write()` | flags, flags2, refusal reasons |

Two backends share TIM3 and DMA1_Stream1 (request TIM3_UP), tick = 275 MHz:

- **Classic** (default): TIM3 counts down, PWM mode 1, DMA writes one u16 into ARR per update.
  Supports scale, precompensation, clamp, Apple Disk II (toggle on TIM3_CH2, /WRREQ, no index).
- **Extended** (`wave_mode`, WRITE flags bit5; always on the GW path): TIM3 counts up, PWM mode 2 with
  preload, DMA burst of three halfwords through `TIM3->DMAR`. Intervals are written as sent: no scale,
  no precompensation, no Apple. `cmd_write()` answers status 11 for those combinations.

Each backend leaves the timer in its own configuration, so every start sets DIR, OC1PE and CCR1
explicitly. `ufi_write_pattern()` does too; the last assertions of `test_write_safety.py` pin this.

## Traps

- **The flux store is shared and reused.** Host deltas arrive as u32 in `ufi_flux_store()`, are packed
  in place to u16 (`write_arr` aliases `write_buffer`), and the verify capture overwrites the same
  memory. A content reference therefore has to exist before the read-back: `verify_ref_build()` from the
  packed track, or `verify_ref_build_u32()` before `pack_deltas()` (`ufi_write_local_checked()`).
- **The ring lives in `.sram2`** because DMA1 reads D2 SRAM2 behind the data cache; every fill is
  followed by `SCB_CleanDCache_by_Addr()`. Moving `write_ring` or dropping the clean gives stale periods.
- **Regression tests extract production functions by name.** `function('ufi_write.c', name)` in
  `firmware/tests/test_regressions.py` cuts the text from the signature to the first `}` in column 0.
  Renaming, merging or reformatting an extracted function breaks the gate, and a new callee inside one
  needs a stub in every harness that extracts it. Extracted today: `ufi_write_start`, `write_finish`,
  `ufi_write_abort`, `data_done`, `start_now`, `ufi_write_delayed_start`, `ufi_write_index_handler`,
  `end_of_data`, `write_event_cmd`, `ufi_write_receive_failed`, `ufi_write_data_complete`,
  `ufi_write_service` and the typedef `write_context_t` (`regression_write`); `blocking_write_guard`,
  `blocking_write_wait`, `ufi_erase_track_ms`, `ufi_write_pattern` (`regression_write_safety`).
- **Per-write options must be reset.** `ufi_write_prepare()` resets the fields of `g_write` and calls
  `write_options_reset()` for the file statics that only `cmd_write()` sets (precompensation override,
  weak-cell markers); otherwise local writers (`ufi_format.c`, `ufi_floppy.c`, `ufi_dump.c`) and the v1
  path inherit the last v2 value. Put a new option into `g_write` or into `write_options_reset()`;
  `test_write_safety.py` checks both the reset and the call.
- **Cancellable delays return early.** After `ufi_delay_us()` or `ufi_delay_ms()` test
  `ufi_cancel_requested()` before any gate action, as `ufi_write_start()` does after the settle delay.
- **Diagnostic writes own their duration.** `ufi_erase_track_ms()` and `ufi_write_pattern()` set
  `diagnostic_write`, which makes `ufi_write_index_handler()` ignore the index; both poll
  `blocking_write_guard()` (cancel, locks, write protect, motor, medium) the whole time and cap the
  duration at 5000 ms.
- **Precompensation counts the physical track.** `ufi_write_precomp_select()` uses 2n with double step
  and picks: per-write override, else profile zones (op 45), else TIMING / auto with 0 below track 40,
  half up to 60, full above. `pack_cfg_t.precomp_fixed` tells `write_pack()` not to apply the track rule
  a second time.
- **Mac drive (board v1.0, id 11)**: write protection comes from the Sony status registers; no drive or
  no disk counts as protected, whole tracks only, never the SYNC sensor as index
  (`test_mac_write_guard.py`).
- **Status numbers** are the negated `ufi_error_t` values in `firmware/include/ufi_fixes.h`: 4 no index,
  5 timeout, 6 DMA / underrun / verify failed, 10 malformed or oversized data, 11 not implemented,
  12 write protected, 18 range, 19 track longer than a revolution, 20 cancelled, 21 media changed,
  22 session lock, 23 speed mismatch.

## Procedures

### Add a per-write option (new flags2 bit)

1. Describe field, order and refusal reasons in `docs/USB_Protokoll.md` first.
2. Extend `UFI_V2_WRITE_FLAGS2_MASK` in `firmware/include/ufi_capabilities.h`.
3. Parse it in `cmd_write()` (`ufi_v2.c`) behind the existing flags2 fields; reject unsupported
   combinations with `refuse()` before `ufi_write_prepare_compact()`. The final `len != at` check must
   still hold.
4. Add the field to `write_context_t`, a setter in `ufi_write.c`, its prototype in `ufi_firmware.h`, the
   reset in `ufi_write_prepare()`, and the call in `cmd_write()` after a successful prepare.
5. Decide where it acts. If it changes the signal, it has to be refused on the paths that cannot honour
   it (Apple, GW, classic or extended) inside `ufi_write_start()` before `pack_deltas()`.
6. Tests: a case in `write_tests()` of `test_regressions.py` (gate stays off on refusal), stubs for the
   new setter in the harnesses that extract `cmd_write` (`test_quality_runtime.py`), a native test when
   pure code changed.
7. Host: `software/ufi_host/ufi_host/protocol2.py` (`write_track()`, result fields), `cli.py` option,
   a test under `software/ufi_host/tests/`.
8. Add a row to `docs/Abnahme_Schreibpfad.md` describing the measurement that would qualify it.

### Change codec, stream or waveform arithmetic

1. Keep the module pure: no `ufi_firmware.h`, no HAL, no globals. These files are compiled unchanged
   into the host tests (`TESTS` in `firmware/tests/run_tests.py`) and are in the `PURE` list of
   `firmware/tools/check_static.py`.
2. Write the failing case in the matching test first: `test_write_codec.c`, `test_write_stream.c`
   (lengths 1 to 200 000, late interrupts, underrun), `test_write_wave.c`, `test_write_verify.c`,
   `test_raw_verify.c`.
3. If a limit moves (`WRITE_MIN_DELTA`, `WSTREAM_HALF`, `WAVE_HALF`), check the users: `ufi_write.c`
   ring size and DMA length, `ufi_drive_port_report()`, the host `write_limits()` and the docs.

### Add a command that can write

1. Route it through `ufi_write_prepare*()` / `ufi_write_local*()`; do not drive WGATE from a new place.
2. At acceptance refuse with `ufi_board_write_locked() || ufi_drive_write_protected()`.
3. Add the command to `locked_write()` in `ufi_v2.c`; the session lock and the LAN refusal
   (`lan_refusal()`) both depend on that list.
4. Add a regression that proves the gate stays off for lock, write protect, cancel and media change.

## Examples

```bash
# pure write modules only (name filter)
python firmware/tests/run_tests.py write
python firmware/tests/run_tests.py raw_verify

# control-flow regressions with extracted production functions
python firmware/tests/test_regressions.py        # regression_write among others
python firmware/tests/test_write_safety.py       # ERASE / PATTERN with timed faults
python firmware/tests/test_mac_write_guard.py

# host side
python -m pytest software/ufi_host/tests/test_guarded_write.py software/ufi_host/tests/test_readback.py -q

# done criterion: the whole offline gate, then the ARM build without warnings
python firmware/tests/check_all.py
cmake -S firmware -B firmware/build -G Ninja && cmake --build firmware/build
```

Refusal cases in `write_tests()` look like this (`gate` is the WGATE double of the harness):

```c
prepare(); locked = true; assert(ufi_write_start() == UFI_ERR_WRITE_PROT && !gate); locked = false;
/* session write lock engaged between acceptance and start: status 22, nothing written */
prepare(); soft_locked = true; assert(ufi_write_start() == UFI_ERR_SOFT_LOCK && !gate && g_write.state == WRITE_ERROR);
```

A change is done when `check_all.py` ends with every group `passed`, the new behaviour has its own
assertion that fails without the change, and no invariant above lost its test.

## Prerequisites

- clang or gcc on PATH and `pip install -r requirements-test.txt` for the native tests and pytest.
- arm-none-eabi-gcc and STM32Cube H7 V1.11.0 for the cross build.
- Read `docs/Abnahme_Schreibpfad.md` and the WRITE sections of `docs/USB_Protokoll.md` before touching
  request parsing.

## Limitations

- Nothing here is confirmed on hardware. The register doubles "cannot establish the physical gate
  latency" (`test_write_safety.py`); capability bits `hardware_qualified` stay 0 until the acceptance
  plan has been measured. Do not describe a write feature as reliable in docs or commit messages.
- Every path that raises WGATE or WRREQ must call `ufi_board_write_enable(true)` directly before it
  (`ufi_write_start()`, `ufi_erase_track_ms()`, `ufi_write_pattern()`); on board v1.0 the gate signal is
  swallowed otherwise and the operation reports success without writing. `test_write_safety.py` counts a
  gate raised with the firmware lock closed. Not yet confirmed on a board with a drive.
- Docs versus code: section 4 of `docs/Abnahme_Schreibpfad.md` names status 10 for an underrun, the code
  and section 1 give status 6. The plan is titled for board v0.7 and speaks of the WRITE-LOCK jumper;
  v1.0 has the front switch plus WLOCK_FW. `board.h` calls the gate "3-input OR U14" in one comment and
  NOR U31 (ECO A11f) in `bus_out_inverted()`; `ufi_selftest.c` `t_wgate_lock()` follows U31.
- `firmware/tests/README.md` lists only part of the native tests; `run_tests.py` is authoritative.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| `cannot extract production function ufi_write.c:<name>` | function renamed, made one-line, or closing brace not in column 0 | restore the shape or update the name list in the harness |
| Harness fails to compile with an undeclared function | an extracted function calls a new helper | add a double for it in that harness (`test_regressions.py`, `test_write_safety.py`, `test_quality_runtime.py`) |
| WRITE ends with status 18 | a period outside 440..65536 ticks after scale and precompensation (classic) or below 220 ticks (extended) | fix the data, send the clamp flag, or use the extended backend for long gaps |
| WRITE ends with status 19 | track longer than one revolution, index cut the write | shorten or scale the track; never extend past the index |
| WRITE ends with status 6, `write_underruns` counts up | ring refill came too late in `ufi_write_dma_irq()` | look for long interrupt-off sections or a slow `ring_fill()`; do not mask the check |
| Status 22 after a reconnect or mode switch | session lock survives everything except an MCU reset | reset the board; do not add an unlock |
| Status 12 on v1.0 with the switch closed | `PIN_WREN_SENSE` reads high (front cable off) or the drive reports write protect | check the cable and `ufi write-lock` output |
| Classic WRITE with verify reports level 2 instead of 3 | no decodable reference (`verify_ref.n == 0`) or more than `VERIFY_MAX_SECTORS` | expected for GCR and protected tracks; use the raw check, not a relaxed match |
| Pattern test shows wrong pulse width after an extended write | timer left in up-count / preload | set DIR, OC1PE and CCR1 as `ufi_write_pattern()` does |
