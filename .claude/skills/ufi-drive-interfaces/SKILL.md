---
name: ufi-drive-interfaces
description: >
  Drive control of the UFI firmware for its four bus families: 34-pin PC and Shugart, Amiga DB23,
  Apple Disk II and Macintosh 3.5 inch (Sony registers, variable-speed PWM for 400K drives), and
  Commodore IEC with the generated 6502 drive code for the 1541. Covers drive ids, select, motor,
  step, seek and recalibrate with TRK00, head position validity, side select, DENSITY / DRATE / TG43
  and the 300/360 rpm line, disk change and media generation, operating profiles and timing presets,
  and the tests and the fuzzer that guard them. Use when changing ufi_drive.c, ufi_drive_extra.c,
  ufi_media.c, ufi_mac.c, ufi_mac_pwm.c, ufi_iec.c, ufi_nib.c or firmware/tools/iec_nib.s, when
  adding a drive type, a timing field or a preset, when a seek, motor, media or profile regression
  fails, or when iec_nib.h differs from the assembler output. Trigger phrases: "drive select", "seek",
  "recalibrate", "TRK00", "step rate", "double step", "motor spin-up", "disk change", "drive profile",
  "density line", "Amiga DF1", "Disk II", "Mac 400K PWM", "1541 drive code", "Laufwerk anwählen",
  "Spur anfahren", "Laufwerksprofil", "Schrittzeit", "Diskettenwechsel", "Laufwerkscode neu erzeugen".
  DO NOT use for: opening WGATE, write waveform and verify (ufi-write-path-safety); flux capture, index
  timing and timer allocation (ufi-flux-capture-timers); v2 commands that carry the drive requests
  (ufi-usb-protocol); Greaseweazle and USB floppy mode behaviour (ufi-usb-modes-gw-msc); pin and
  connector assignment (ufi-board-pins); bench bring-up of a drive (ufi-bringup-diagnostics); running
  the gate or the fuzzers as such (ufi-test-gate, ufi-fuzz-static-analysis).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "drive-control", "iec", "floppy-bus"]
---

# UFI Drive Interfaces

## Purpose

Explains how the firmware addresses and moves every supported drive, which state it keeps about head
position and medium, and which lines are shared between functions. It names the rules that keep a
drive from being stepped blindly or a shared line from being driven for two purposes, and the
touchpoints of the usual changes. Drive timings are defaults from documentation, not bench results.

## Drive ids are wire values

`drive_type_t` in `firmware/include/ufi_firmware.h`; the host names are `DRIVES` in
`software/ufi_host/ufi_host/protocol.py`. Never renumber; append before `DRIVE_TYPE_COUNT`.

| id | enum | host name | Select / motor |
|---|---|---|---|
| 1 | `DRIVE_SHUGART_A` | a | `PIN_FDD_DRV_SEL_A`, motor `PIN_FDD_MOTOR_A` (PC cable) |
| 2 | `DRIVE_SHUGART_B` | b | `PIN_FDD_DRV_SEL_B`, motor `PIN_FDD_MOTOR_B` |
| 3 | `DRIVE_APPLE_II` | apple | Disk II on J14, `/ENABLE` = spindle and stepper power |
| 4 | `DRIVE_AMIGA` | amiga | J7 SEL1B = `DRV_SEL_B`, motor latched from MTRXD = `MOTOR_B` |
| 5 | `DRIVE_IEC` | iec | no select, the 1541 runs its own motor; most drive calls return `UFI_ERR_NO_DRIVE` |
| 6-9 | `DRIVE_SHUGART_DS0..3` | ds0..ds3 | straight cable: select on `MOTOR_A`, `DRV_SEL_B`, `DRV_SEL_A`, `DRATE` (DS3 needs JP1); one shared MOTOR ON = `MOTOR_B` |
| 10 | `DRIVE_AMIGA2` | amiga2 | J7 SEL2B = `DRV_SEL_A`, needs JP3 |
| 11 | `DRIVE_APPLE2` | apple2 / mac | board v0.7: second Disk II (J15 enable). Board v1.0: Macintosh 3.5 inch on J23 |

Id 11 changes meaning with the board: `is_mac()` is `DRIVE_APPLE2 && board_fdd_mosfet`
(`board_fdd_mosfet` = board v1.0). `is_apple()` is true for both 3 and 11, so every Apple branch has to
ask `is_mac()` first.

## Invariants

1. **Bus lines only through `bus_out()` / `bus_in()`** (`firmware/include/board.h`): `true` = asserted =
   low on the cable. The electrical polarity differs per board (LS07 on v0.x, MOSFET gates on v1.0,
   WGATE and the IEC port keep the old polarity; `board_pin()` also remaps `MOTOR_B` and the LED).
   A direct `HAL_GPIO_WritePin()` on an FDD or IEC pin is wrong on one of the boards. The Apple / Mac
   pins (`PIN_APL_*`, `PIN_MAC_*`) are plain GPIO with explicit levels.
2. **No step without a known position.** `media[drive].position_valid` says whether `current_track`
   can be trusted. `seek_physical()` recalibrates first when it is false, fails with
   `UFI_ERR_SEEK_FAIL` and clears the flag when TRK00 shows up early or the sensor disagrees with the
   target at the end. Only a path that proved the position by sensor may set it true (fuzz_drive found
   `ufi_drive_apple_step()` marking a non-Apple drive valid). `ufi_drive_safe_state()` and profile
   changes clear it.
3. **Recalibrate is three phases**: out to TRK00 (100 steps at most), 6 steps in where TRK00 must
   release, out again (10 steps at most). A stuck or missing sensor is an error, never "track 0".
4. **`current_track` counts physical tracks.** With `g_timing.double_step` logical n is physical 2n;
   limits compare against `media[].drive[1]` (highest physical track). A Shugart half track exists only
   as quarter 2 with double step (`ufi_drive_quarter_ok()`).
5. **Shared lines are checked in `line_conflicts()`**: `DRATE` is also the DS3 select, so it cannot be
   the rpm line on ds0..ds3; `DENSITY` cannot be both rpm line and TG43; Apple, Mac, IEC and "none"
   have neither line. `ufi_drive_operating_profile()` validates the whole 40-byte profile before it
   changes a setting or a physical line; keep that order.
6. **Every setter calls `ufi_config_changed()`.** The configuration generation guards GUARDED_READ /
   GUARDED_WRITE; a new setting that skips it lets a stale host token pass.
7. **The media generation is the disk identity.** `ufi_drive_media_token()` samples the change line
   only while the motor runs; a rising edge in `media_observe()` gives a new generation and clears the
   media profile. `ufi_drive_step()` samples it before STEP, because stepping re-arms the DSKCHG latch.
   Token 0 (no drive, IEC) never matches.
8. **Motor on blocks until spin-up is over and is cancellable.** `ufi_drive_motor(true)` returns at once
   when the motor already runs; a cancel during `ufi_delay_ms()` switches the motor off again and
   returns `UFI_ERR_CANCELLED`. `motor_step_wait()` enforces `drive_extra_t.motor_step_ms` before the
   first step.
9. **`ufi_drive_safe_state()` is the single "hands off" call** (USB lost, abort, mode change): motors
   off including latched Amiga drives, WGATE and /WRREQ released, STEP released, select none. A new
   output line has to be released there too.
10. **`firmware/include/iec_nib.h` is generated.** The gate assembles `firmware/tools/iec_nib.s` and
    compares the result with the committed header byte for byte.

## Bus family notes

**PC / Shugart (J6).** Pin 34 is DSKCHG for ids 1-2 and READY for ds0..ds3 on the same input
`PIN_FDD_DKCHG`; the Shugart ids therefore have no change line (`ufi_drive_disk_changed()` is false).
All four ds drives spin together, so `ufi_drive_motor()` updates all four status entries. The 300/360 rpm
line comes from `UFI.CFG` (`rpm_line=none|density|drate`, `rpm_360=low|high`) through
`ufi_drive_rpm_config()`; `ufi_drive_set_rpm()` returns `UFI_ERR_NOT_IMPL` without a configured line.
TG43 (`drive_timing_t.tg43`) drives DENSITY from physical track 43 after each seek (`tg43_apply()`).

**Amiga (J7).** The drive latches MTRXD on the falling edge of its select line. `amiga_motor()` and
`ufi_drive_select()` therefore pulse select with the drive's own stored motor state on MTRXD, so that
re-selecting DF1 / DF2 does not stop a running drive. `ufi_drive_amiga_id()` shifts 32 id bits out on
/RDY and leaves the motor off. On the headless board J7 pin n equals DB23 pin n; the wiring table in
`docs/Amiga_DB23_Adapter_Cable.md` belongs to the older J4 adapter.

**Apple Disk II.** Four-phase stepper kept in quarter tracks (`apl_qt[]`), no track-0 sensor:
recalibrate runs 80 half steps against the stop. Phases are released between seeks; a head left on an
odd quarter loses its position when `/ENABLE` goes high (`apl_release()`). One head, 40 tracks
(`APL_MAX_TRACK` 39), no index unless `ufi_drive_sync_index()` (config `apple_sync`).

**Macintosh 3.5 inch (v1.0, J23).** `firmware/src/ufi_mac.c` is HAL-free and talks only through
`mac_io_t`; `ufi_drive.c` supplies the callbacks, `ufi_mac_pwm.c` is the TIM1_CH1 glue (22.25 kHz).
CA0-CA2 and LSTRB share the Disk II phase pins. Rules:
- Status registers replace the read data on RD, so `mac_status_flags()` reads them only while
  `!ufi_operation_busy()` and otherwise answers from the last complete read.
- `mac_probe()` reads twice; unstable, all-equal or DRVIN-high results are `MAC_KIND_NONE`, and "none"
  counts as write protected and "no disk".
- Whole tracks only, 80 tracks, side 1 refused on a 400K drive.
- The speed PWM is handed out only for `MAC_KIND_400K` (`mac_io_get()`); 800K and FDHD drives drive
  J23 pin 20 themselves, and JP5 is open by default. The duty per zone is found by closed loop against
  the tachometer (`mac_pwm_tune()`), there is no duty table. Zones: 16 tracks each, 394 / 429 / 472 /
  525 / 590 rpm.
- `MAC_ERR_*` must equal the `ufi_error_t` numbers (`_Static_assert` in `ufi_drive.c`).

**Commodore IEC.** UFI is always the controller (`firmware/src/ufi_iec.c`, timing constants
`IEC_T_*`). Raw GCR reading uploads the routine from `iec_nib.h` with M-W in 32-byte pieces to `$0310`
and starts it with M-E; it runs with interrupts off, so `ufi_iec_wait_ready()` polls LISTEN until the
drive answers again. Layout: parameters and results `$0300-$030F`, code up to `$05FF`, data buffer
`$0600-$07FF`. `NIB_PARAM`, `NIB_RESULT`, `NIB_BUF` in `ufi_nib.c` and the dictionary `P` in
`firmware/tools/test_iec_nib.py` repeat these addresses. A bus reset calls `ufi_nib_invalidate()`.

## Profiles and timing

- `drive_timing_t` is the wire format: 11 little-endian u16 (22 bytes, `__packed`); a shorter
  DRIVE_TIMING payload overwrites only the leading fields. `drive_extra_t` adds 6 u16 with
  `reserved == 0` enforced by `drive_extra_valid()`.
- Defaults `DEFAULT_TIMING_INIT`: step 3 ms, settle 15 ms, direction change 15 ms, spin-up 500 ms,
  select settle 10 ms, motor off after 30 s idle (`ufi_board.c` service).
- Operating profile = 40 bytes per drive id (schema, timing, extra, rpm line, polarity, rpm, stored
  flag), kept in `operating_profiles[]` and in `UFIPROF.BIN` on the SD NAND with a CRC-32.
  `PROFILE_FILE_SIZE` depends on `DRIVE_TYPE_COUNT`: a new drive id makes existing files unreadable and
  they are dropped without a message.
- `ufi_drive_select_native()` applies the stored profile and restores the previous timing when select
  fails; plain `ufi_drive_select()` does not touch timing. Host SELECT uses the native variant.
- Presets (`ufi_drive_operating_preset()`): 1 factory, 2 conservative, 3 8-inch. The header comment in
  `ufi_media.h` still names only 1 and 2; the code is right. Host: `ufi profile standard|konservativ|8zoll`.
- Name trap: `firmware/include/ufi_profile.h` is the cycle-count profiler, not drive profiles. Drive
  and media profile declarations live in `ufi_media.h`, the code in `ufi_drive.c` and `ufi_media.c`.

## Procedures

### Add a timing field or a preset

1. A new u16 goes into `drive_extra_t` in place of `reserved` (keeps the 40-byte profile) or, if the
   wire size may grow, at the end of `drive_timing_t`; then fix every `memcpy(..., 22)` / `12` and the
   offsets in `ufi_drive_operating_profile()`, `profile_shape_ok()` and `ufi_drive_select_native()`.
2. Set the default in `DEFAULT_TIMING_INIT` / `DEFAULT_EXTRA_INIT`, the limit in `drive_extra_valid()`,
   and the value in each branch of `ufi_drive_operating_preset()`.
3. Host: `protocol2.py` (profile pack / unpack, `TIMING_PRESETS`), `cli.py`; document in
   `docs/USB_Protokoll.md`.
4. Tests: `firmware/tests/test_drive_profiles.py` (rejection must stay atomic), `test_evidence.c` when
   `drive_extra_valid()` changed, a seed or operation in `firmware/tests/fuzz_drive.c`.

### Add a drive type or bus family

1. Append the enum value; add the host name in `protocol.py` and the `UFI.CFG` name table in
   `ufi_dump.c`.
2. In `ufi_drive.c`: `is_*()` helper, `media_init()` arguments in `ufi_drive_init()` (highest track,
   heads, change line), the `switch` in `ufi_drive_select()` and `ufi_drive_motor()`, then
   `ufi_drive_step()` / seek / recalibrate, `ufi_drive_select_side()`, the status readers
   (`ufi_drive_at_track0()`, `ufi_drive_write_protected()`, `ufi_drive_disk_changed()`,
   `ufi_drive_ready()`), `line_conflicts()`, `ufi_drive_port_report()`, `ufi_drive_profile_set()` and
   `ufi_drive_safe_state()`.
3. Keep hardware-independent logic in its own HAL-free file with a callback struct like `mac_io_t` and
   test it natively (pattern: `test_mac_drive.c`, entry in `TESTS` of `firmware/tests/run_tests.py`,
   `PURE` list in `firmware/tools/check_static.py`).
4. Check the other users of drive ids: `ufi_dump.c`, `ufi_diag.c`, `ufi_floppy.c`.
5. Extend `fuzz_drive.c` (head model) and the regression scripts; decide the write-protect semantics
   and pin them like `test_mac_write_guard.py`.

### Change the 1541 drive code

1. Edit `firmware/tools/iec_nib.s` only. The code must end at or below `$05FF`.
2. Regenerate the header; the output file name decides the symbol names (`iec_nib_code`,
   `IEC_NIB_ORG`, `IEC_NIB_LEN`).
3. Run the py65 model test, then the whole gate.

## Examples

```bash
# regenerate and test the 6502 drive code (py65 from requirements-test.txt)
python firmware/tools/asm6502.py firmware/tools/iec_nib.s firmware/include/iec_nib.h
python firmware/tools/asm6502.py firmware/tools/iec_nib.s firmware/tests/build/iec_nib.bin
python firmware/tools/test_iec_nib.py firmware/tests/build/iec_nib.bin

# drive regressions (production functions extracted from ufi_drive.c)
python firmware/tests/test_drive_profiles.py
python firmware/tests/test_media_runtime.py
python firmware/tests/test_motor_runtime.py
python firmware/tests/test_mac_write_guard.py

# native module tests
python firmware/tests/run_tests.py mac_drive
python firmware/tests/run_tests.py media_check

# drive fuzzer against the head model with TRK00 sensor (clang with libFuzzer)
python firmware/tests/run_fuzz.py --target drive --seconds 30
python firmware/tests/check_all.py
```

Access pattern for a bus line and a status input:

```c
bus_out(&PIN_FDD_DIR, direction > 0);     /* asserted = step in */
bus_out(&PIN_FDD_STEP, true);
delay_us(STEP_PULSE_US);
bus_out(&PIN_FDD_STEP, false);
if (bus_in(&PIN_FDD_TRACK0)) { /* head on track 0 */ }
```

A change is done when `check_all.py` passes, the drive fuzzer runs clean for the chosen time, and a
new behaviour has an assertion in one of the regression scripts or native tests.

## Prerequisites

- clang or gcc on PATH, `pip install -r requirements-test.txt` (pytest, pyserial, py65).
- clang with libFuzzer and `llvm-nm` for `run_fuzz.py`.
- `docs/USB_Protokoll.md` for the requests that reach these functions (SELECT, TIMING, op 18 profile,
  MAC 0x29), `kicad/UFI_Headless/README.md` for jumpers JP1, JP3, JP5.

## Limitations

- No drive family has been confirmed on hardware; Mac register timings in `ufi_mac.h` are
  "conservative choices" awaiting a real drive, IEC timing follows the published bus description.
- `delay_us()` in `ufi_drive.c` is a DWT busy wait and `ufi_drive_set_rpm()` uses `HAL_Delay()`; neither
  reacts to a cancel. Only the motor spin-up and the IEC waits use the cancellable delays.
- The regression scripts are control-flow tests with GPIO doubles, "not a cycle-accurate STM32
  simulation"; step pulse widths and settle times are not measured by them.
- `docs/ADF_Drive_Review.md` compares a GPLv3 project; nothing from it may be copied into this
  MIT-licensed firmware. `docs/Amiga_DB23_Adapter_Cable.md` says SEL2B is held inactive on the board,
  while the code uses it for `DRIVE_AMIGA2` through JP3.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| Gate stops with `generated drive code differs from firmware/include/iec_nib.h` | `iec_nib.s` edited without regenerating, or the header edited by hand | run `asm6502.py` with the header as output and commit both |
| `cannot extract production function ufi_drive.c:<name>` | extracted function renamed, or its closing brace is not in column 0 | restore the shape or update the name tuple in `test_drive_profiles.py`, `test_media_runtime.py`, `test_motor_runtime.py`, `test_mac_write_guard.py` |
| Regression harness no longer compiles | an extracted function calls a new helper or reads a new global | add the double to that harness |
| Seek returns status 3 although the drive stepped | TRK00 asserted before the target or state differs at the end; position invalidated | check the sensor and the `double_step` setting; do not relax the comparison in `seek_physical()` |
| Stored profiles are gone after a firmware update | `DRIVE_TYPE_COUNT` changed, `UFIPROF.BIN` has the wrong size | store the profiles again (`ufi profile save`) |
| `ufi_drive_set_rpm()` answers status 16 on ds0..ds3 | `rpm_line=drate` collides with the DS3 select | use the DENSITY line or ids a / b |
| Amiga drive stops when the other one is selected | select edge presented a wrong MTRXD level | keep the motor-state write before the select edge in `ufi_drive_select()` |
| Mac drive reports write protected with an unprotected disk | probe returned `MAC_KIND_NONE` (no power on the rails, drive not answering) | switch the drive supply on before SELECT, read MAC op 0 status |
| MAC op 4 answers status 11 | JP5 open: PWM does not reach J23 pin 20 (`ufi_mac_pwm_loopback()`) | close JP5 only for a 400K drive |
| Disk change never seen on ds0..ds3 or Disk II | no change line on those ids (`media_init(..., change=false)`) | expected; the host reports the change, which ends in `ufi_drive_media_notify()` |
