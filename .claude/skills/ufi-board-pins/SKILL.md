---
name: ufi-board-pins
description: >
  Covers the pin map and board abstraction of the UFI firmware (STM32H723ZGT6, LQFP144): the pin table in
  firmware/include/board.h and firmware/src/board_headless.c, the polarity helpers bus_out, bus_in,
  bus_out_state, led_set, the revision switch board_fdd_mosfet with board_pin and bus_out_inverted, the BOARD_ID
  divider and ufi_board_rev, the start-up idle levels in board_gpio_init, the board services in ufi_board.c
  (drive rails, eFuse faults, write lock pins), the timer, ADC and GPIO allocation table docs/Timer_Zuteilung.md,
  and how a pin or peripheral change stays in parity with the KiCad schematic in kicad/UFI_Headless. Use when a
  pin is added, moved or repurposed, when a timer channel, ADC channel or alternate function is assigned, when a
  signal polarity or reset level is in question, when code must behave differently on board v0.x and v1.0, when
  a new board revision gets a BOARD_ID value, or when firmware pins are compared with the netlist. Trigger
  phrases: "pin map", "which pin is", "move a pin", "add a GPIO", "free pins", "alternate function", "board
  revision", "BOARD_ID", "active low or high", "pin swap", "Pinbelegung", "Pin tauschen", "neuer Pin",
  "welcher Timer ist frei", "Platinenrevision", "Schaltplan-Parität", "Polarität der Busleitung",
  "GPIO-Belegung". DO NOT use for: capture and write timer configuration (ufi-flux-capture-timers), drive
  protocol logic on the pins (ufi-drive-interfaces), write gate interlock behaviour (ufi-write-path-safety), PSRAM
  controller start-up (ufi-memory-psram), W5500 driver (ufi-lan-w5500), bench bring-up runs
  (ufi-bringup-diagnostics), building and flashing (ufi-firmware-build-flash). Schematic and layout edits follow
  the skills pcb-revision-stufen and kicad-schaltplan-review.
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "pinmap", "board-revision", "kicad"]
---

# UFI Board Pins and Board Abstraction

## Purpose

Explains where the firmware defines each MCU pin, how one binary serves the boards v0.5, v0.6, v0.7 and v1.0
with opposite output polarity, and which files have to change together when a pin or peripheral moves. The goal
is that firmware, allocation table and schematic never disagree, because a wrong idle level here asserts a
drive line (motor, step, write gate) at power-up.

## Invariants and traps

1. **One binary, four boards, decided at run time.** `ufi_board_rev()` (defined in `ufi_v2.c`, declared in
   `ufi_firmware.h`) reads `ADC_CH_BOARD_ID` (PA4) on every call and returns 5, 6, 7, 10 or 0 = unknown, using
   windows of +/-200 mV around `BOARD_ID_V05_MV` 1650, `BOARD_ID_V06_MV` 1055, `BOARD_ID_V07_MV` 595,
   `BOARD_ID_V10_MV` 2269. There is no compile-time board switch; `BOARD_HAS_APPLE` and `BOARD_HAS_PSRAM` are
   both 1 and the real check is at run time.
2. **Output polarity flips with the revision.** v0.x: all bus outputs drive SN74LS07 inputs, MCU low = line
   asserted. v1.0 (A11b): the Shugart/Amiga outputs PE7-PE15, PF11 (DRATE), PE5 (INDEX_SIM) and PA6 (WDATA)
   drive N-MOSFET gates, MCU high = line asserted. `board_gpio_init()` sets `board_fdd_mosfet =
   ufi_board_rev() >= 10u` before it touches an output. Exceptions on v1.0 that stay active low:
   the IEC outputs PD0-PD4 (LS07 U9 on every board) and PE14 `PIN_FDD_WGATE` (WGATE_n into the NOR gate U31,
   A11f). `bus_out_inverted()` encodes exactly this rule by port and pin.
3. **Never write a bus output with `HAL_GPIO_WritePin` in module code.** Use `bus_out(&PIN_x, true)` = assert,
   `bus_in(&PIN_x)` = line asserted on the cable, `bus_out_state()`, `led_set()`. Timer outputs follow the same
   flag: `ufi_write.c` and `ufi_diag.c` pick `TIM_OCPOLARITY_HIGH` when `board_fdd_mosfet` is set.
4. **Two pins differ between v0.x and v1.0 (B3a) and are translated, not redefined.** `PIN_FDD_MOTOR_B` stays
   PE8 and `PIN_LED_FDD` stays PG1 in the table; `board_pin()` maps them to PC13 and PC7 when `board_fdd_mosfet`
   is set. Only `bus_out()`, `bus_out_state()` and `led_set()` call `board_pin()`. Code that uses
   `PIN_FDD_MOTOR_B.port`/`.pin` directly drives the wrong pin on v1.0.
5. **Idle level before output mode.** `board_gpio_init()` latches the released level with `HAL_GPIO_WritePin`
   and only then calls `init_pins(..., GPIO_MODE_OUTPUT_PP, ...)`. Keep this order for every new output. The
   reset state of the board (100k gate pull-downs on v1.0, floating LS07 inputs on v0.x, R131 pull-up on
   WLOCK_FW, R28 pull-down on FDD5_EN) is the released state; firmware must match it, not fight it.
6. **Unknown revision drives no Shugart output.** If the BOARD_ID reading falls outside all windows (0),
   neither released level is safe (high turns the MOSFETs of a v1.0 board on, low asserts through the LS07 of
   v0.x), so `board_gpio_init()` leaves PE7-PE13, PE15, PF11, PA6 and PE5 as inputs and the board keeps the
   lines released; only WGATE_n (PE14, high on every board) is driven. The drive is unusable in that state.
   `test_board_safety_runtime.py` pins this per revision; a new output pin with revision-dependent polarity
   must use `fdd_mode` there too. A changed divider, ADC channel or window stays a safety change; the host
   shows it as `board unknown` and `bringup.py --expect-rev 10` fails on it.
7. **PE4 changes direction with the revision.** v0.5-v0.7: input, WRITE LOCK jumper. v1.0 (A9b): output
   WLOCK_FW, high = locked, low only while a write runs (`ufi_board_write_enable()`); the hardware lock is read
   on `PIN_WREN_SENSE` (PE6). `ufi_board_jumper_locked()` picks the right pin; do not read `PIN_WLOCK` elsewhere.
8. **`PIN_FLT5`/`PIN_FLT12` are low while the rail is off** (ENUV low counts as undervoltage). `sample_faults()`
   in `ufi_board.c` only evaluates them for rails in `power_mask`, and only for revision >= 10.
9. **TIM2 is full** (CH1 RDATA PA5, CH2 INDEX PA1, CH3 APL_RDDATA PA2, CH4 INDEX2 PB11, OC4 internally for the
   write start offset). TIM5 shares PA0-PA3 with TIM2 and is unusable. TIM3 = WDATA (CH1 PA6, CH2 PA7),
   TIM7 = flux ISR tick, TIM15 CH1 = INDEX_SIM (PE5), TIM8 CH1 = SYNC (PC6), TIM1 CH1 = Mac PWM (PA8).
10. **PC13 is a backup-domain pin** (about 3 mA, low speed): acceptable for the static Q6 gate, not for a fast
    or loaded signal.
11. **The pin table is duplicated in `firmware/tests/fuzz_drive.c`** (eleven `PIN_FDD_*` definitions and a drive
    model that interprets `GPIOE->ODR` with `board_fdd_mosfet = fuzz_bit()`). Moving a drive pin without
    updating that harness makes the fuzz target model a different board.
12. **No automated firmware-to-netlist check exists.** Parity was established by review
    (`docs/Schaltplan_Review_2026-10-24.md`: all pin table entries matched the net on the U5 pin). After a
    change the comparison has to be repeated by hand against a fresh netlist export.
13. **Do not run `kicad/UFI_Headless/scripts/build_schematic.py` on the v1.0 project.** The KiCad README still
    presents it as the generator ("Erzeugen / Prüfen"), but the script was last changed on 2026-10-07 and
    contains none of the v1.0 parts (U28, U31, J23), while the `*.kicad_sch` sheets were edited afterwards in
    ECO stages. It rewrites the sheets and `netlist_summary.txt` from the v0.7 state.

## Where pins are defined

| Place | Content |
|---|---|
| `firmware/include/board.h` | `extern const gpio_pin_t PIN_*` with the pin and its electrical meaning in the comment, AF macros (`FDD_RDATA_AF`, `INDEX_SIM_AF`, `LAN_SPI_AF`, ...), ADC channels (`ADC_CH_*`), `BOARD_ID_*_MV`, `PSRAM_*_PORT/_PIN`, polarity helpers |
| `firmware/src/board_headless.c` | the `PIN_*` definitions and `board_gpio_init()` (clocks, modes, idle levels) |
| `firmware/src/ufi_board.c` | rails (`ufi_board_power`, stagger 5 V then 12 V), overcurrent trip, eFuse faults, lock pins, `ufi_board_status()`, `ufi_board_has_apple()` |
| `firmware/src/ufi_power.c` | ADC1 pins as port literals: PA0/PA3 (CC1/CC2), PA4 (BOARD_ID), PC0/PC1 (rail currents); `ufi_adc_mv()` initialises the ADC on first use |
| `firmware/src/ufi_sense.c` | I2C1 PB6/PB7 and the rail voltage pins PC4/PC5 as port literals |
| `firmware/src/ufi_sd.c`, `usbd_conf.c` | SDMMC2 PD6/PD7/PG9-PG12, USB PA11/PA12 as port literals |
| `ufi_psram.c`, `ufi_lan.c`, `ufi_sync.c`, `ufi_mac_pwm.c`, `ufi_write.c`, `ufi_diag.c` | take over a pin from `board.h` and switch it to its alternate function when the driver starts |
| `firmware/tools/psram_probe.tcl` | PSRAM pins and GPIO register addresses hard-coded for OpenOCD |

Revision-dependent behaviour is spread as `ufi_board_rev() >= 10u` (or `< 10u`) checks in `ufi_board.c`,
`ufi_main.c`, `ufi_lan.c`, `ufi_selftest.c`, `ufi_sense.c`, `ufi_sync.c`, `ufi_v2.c`, and as `board_fdd_mosfet`
checks in `ufi_drive.c`, `ufi_dump.c`, `ufi_write.c`, `ufi_diag.c`. Search both when a rule changes.

## Documents and their standing

| Document | Standing |
|---|---|
| `docs/Timer_Zuteilung.md` | binding allocation table for timers, ADC channels and A11 GPIOs; its rule: enter a new assignment there before it reaches schematic or firmware, and change it in the same commit as `board.h`. Lists the free pins on v1.0: PE8, PG1, PG13, PC2_C, PC3_C, PB4, PC8, PA10 |
| `kicad/UFI_Headless/README.md`, section "GPIO-Belegung" | full signal/pin/function table for v1.0, kept current including B3a |
| `kicad/UFI_Headless/netlist_summary.txt` | written by `scripts/build_schematic.py`; last changed 2026-10-07 (v0.7). It has no v1.0 parts (U28, U31, J23) and still shows FDD_MOTOR_B on U5.59 (PE8). Not a v1.0 reference |
| `docs/Schaltplan_Review_2026-10-24.md` | review after stage A4, before B3a: names PG13 for GUARD_ERR12 (now PC9) |
| `docs/PCB_Revision_Empfehlungen_2026-10-24.md` | design decisions for v1.0; section 6 still lists ERR12 = PG13 and section 2 proposes INDEX_SIM on PE1 (built: PE5) |

Docs versus code, trust the code: `Timer_Zuteilung.md` marks TIM8, TIM1 and ADC INP4/INP8 as planned, but
`ufi_sync.c`, `ufi_mac_pwm.c` and `ufi_sense.c` use them; it names 2.4k for the PB5 divider where `board.h` says
2.2k (R156/R157). The KiCad README section "Signalpolarität" describes only the LS07 polarity of v0.x, and its
firmware notes say `BOARD_HAS_APPLE` is 0 while `board.h` defines 1.

## Procedures

### Add or move a pin

1. Check the candidate pin against the free list and the timer table in `docs/Timer_Zuteilung.md`, and the
   alternate function against the STM32H723ZG pin data. Enter the assignment in that file.
2. Schematic: stage-wise per skill `pcb-revision-stufen` (label on the U5 pin, no-connect on the freed pin; the
   scripted example is `kicad/UFI_Headless/scripts/eco/pinswap_b3a.py`). ERC and DRC with schematic parity must be
   0 before the firmware side counts as matching.
3. `board.h`: `extern const gpio_pin_t PIN_<NAME>;` with pin, timer/AF, the part it drives and the level meaning
   in the comment; AF or ADC macro if needed.
4. `board_headless.c`: definition, and in `board_gpio_init()` the port clock, the idle level, then the mode.
   An input driven by a buffer gets `GPIO_NOPULL`; an input that is unconnected on older boards gets a pull that
   yields the inactive level there (as `PIN_FLT5`/`PIN_FLT12` with `GPIO_PULLUP`).
5. If the pin exists only on some revisions, guard its use with `ufi_board_rev()`. If an existing signal moved,
   extend `board_pin()` and keep the old definition, and initialise the new pin under `if (board_fdd_mosfet)`
   as done for PC13/PC7.
6. If it is a bus output, decide its polarity in `bus_out_inverted()` and state the reset level (pull resistor)
   in the comment.
7. Update the duplicates: `firmware/tests/fuzz_drive.c` (drive pins), `firmware/tools/psram_probe.tcl` (PSRAM
   pins), the table "GPIO-Belegung" in `kicad/UFI_Headless/README.md`, and the port literals in
   `ufi_power.c`/`ufi_sense.c`/`ufi_sd.c`/`usbd_conf.c` if one of those pins moved.
8. Export a fresh netlist and compare every changed `PIN_*` with the net on the U5 pin (commands in Examples).
9. Build and run the gates listed under "Done criterion".

### Add a board revision

1. Choose a divider whose voltage is more than 400 mV away from 595, 1055, 1650 and 2269 mV; add
   `BOARD_ID_Vxx_MV` to `board.h` and a branch to `ufi_board_rev()` in `ufi_v2.c`.
2. `ufi_board_has_apple()` in `ufi_board.c` has its own window list (v0.7 and v1.0); extend it or the Apple port
   disappears from the capabilities (`CAP_APPLE` in `caps()`).
3. Review every `>= 10u`/`< 10u` and `board_fdd_mosfet` site listed above; `board_fdd_mosfet` currently means
   "v1.0" in three senses at once (MOSFET polarity, B3a pin swap, Mac port on drive id 11 in `ufi_drive.c`).
4. Host: `board_rev_text()` in `software/ufi_host/ufi_host/protocol2.py` formats any number as
   `v<rev // 10>.<rev % 10>`; extend `software/ufi_host/tests/test_board_rev.py`. `bringup.py` sets `ctx.v10`
   from `board_rev >= 10` and offers `--expect-rev N`; its fakes are in `software/ufi_host/tests/test_bringup.py`.
5. Shunt scale in `sample_rails()` (1 mV/mA from revision 10, 2 mV/mA before) and the INFO/provenance fields that
   carry `board_rev` and `board_id_mv` (see `ufi-usb-protocol`).

## Examples

```c
/* module code: logical levels only, both board generations handled inside */
bus_out(&PIN_FDD_MOTOR_B, true);              /* asserts PE8 low on v0.x, PC13 high on v1.0 */
if (bus_in(&PIN_FDD_TRACK0)) { /* head is on track 0 */ }
led_set(&PIN_LED_FDD, true);                  /* PG1 on v0.x, PC7 on v1.0 */

/* board_gpio_init(): level first, then direction */
HAL_GPIO_WritePin(PIN_FDD_PIN4.port, PIN_FDD_PIN4.pin, GPIO_PIN_RESET);
init_pins(PIN_FDD_PIN4.port, PIN_FDD_PIN4.pin, GPIO_MODE_OUTPUT_PP, GPIO_NOPULL, GPIO_SPEED_FREQ_LOW, 0);
```

```bash
# netlist of the drawn schematic for the manual pin comparison (KiCad 10 paths as in kicad/UFI_Headless/README.md)
CLI="C:/Program Files/KiCad/10.0/bin/kicad-cli.exe"
cd kicad/UFI_Headless
"$CLI" sch erc --severity-all -o erc.rpt UFI_Headless.kicad_sch
"$CLI" sch export netlist -o headless.net UFI_Headless.kicad_sch
"$CLI" pcb drc --severity-all --schematic-parity -o drc.rpt UFI_Headless.kicad_pcb

# firmware and host side
cmake --build firmware/build
python firmware/tests/check_all.py
python firmware/tests/run_fuzz.py --target drive --seconds 30
python -m ufi_host.bringup --port COMx --expect-rev 10 --report bringup.json   # on the board
```

**Done criterion**: `docs/Timer_Zuteilung.md`, `board.h`, `board_headless.c` and the KiCad README table name the
same pin in one commit; ERC 0, DRC with `--schematic-parity` 0; the ARM build has no warning; `check_all.py` is
green (it includes the host tests `test_board_rev.py` and `test_bringup.py`); `run_fuzz.py --target drive` is
green when a drive pin or polarity changed; on hardware `bringup.py --expect-rev` passes.

## Prerequisites

- The schematic PDF or KiCad project of the revision in question, and KiCad 10 `kicad-cli` for ERC, netlist
  and DRC.
- Knowledge of which board generation the change targets; bench boards of v0.x stay supported.
- For the gates: the toolchain from `ufi-firmware-build-flash`, clang and the Python requirements from
  `requirements-test.txt`.

## Limitations

- Native tests stub the revision (`ufi_board_rev()` returns 7 in `test_native_requests.py` and
  `test_evidence_runtime.py`); `board_gpio_init()` itself is covered by no native test, only by static analysis
  with the real ARM headers and by the bench run.
- `scripts/verify_netlist.py` compares against `netlist_summary.txt` and therefore reflects the v0.7 generator
  state, not the v1.0 schematic that was changed in ECO stages.
- Electrical facts in the `board.h` comments (pull values, part numbers) are documentation; nothing checks
  them against the BOM.
- The v1.0 first bench run (`docs/Bringup_2026-10-24.md` section 8) was USB-only without a drive; the MOSFET
  polarity path has no bench evidence with a drive in the docs.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| Motor B or the FDD LED dead on v1.0, fine on v0.7 | code used `PIN_FDD_MOTOR_B`/`PIN_LED_FDD` port and pin directly, bypassing `board_pin()` | go through `bus_out()`/`led_set()` |
| All drive lines asserted after power-up on v1.0 (or no drive reacts at all: BOARD_ID unknown, outputs left as inputs) | BOARD_ID read as v0.x, so `board_fdd_mosfet` is false | check the divider (10k/22k, 2.27 V on PA4), `ufi_adc_mv()` and the windows in `ufi_board_rev()`; host INFO shows the revision |
| Write gate asserts or never asserts on v1.0 after a polarity change | PE14 treated as MOSFET-polarity pin | keep the PE14 exception in `bus_out_inverted()` and the explicit `GPIO_PIN_SET` in `board_gpio_init()` |
| POWER events with the fault bits 2/3 set while the rails are off | FLT read without masking by `power_mask` | evaluate FLT only for enabled rails, as `sample_faults()` does |
| Write refused with write-protect status on v1.0 although the firmware lock is released | hardware lock: `PIN_WREN_SENSE` high (front switch open or front cable off) | close the switch; do not read PE4 for the lock state on v1.0 |
| A new capture input has no free TIM2 channel | TIM2 fully allocated, TIM5 blocked by PA0-PA3 | use another 275 MHz timer with an overflow counter, as SYNC does on TIM8 |
| `verify_netlist.py` reports many mismatches on the current schematic | it compares with the v0.7 `netlist_summary.txt` | compare `PIN_*` against the exported netlist by hand; do not regenerate the schematic with `build_schematic.py` |
| Fuzz target `drive` finds a new failure after a pin move | pin copy or polarity model in `fuzz_drive.c` not updated | update the `PIN_FDD_*` copies and the `board_fdd_mosfet` branches there |
| Host prints `board unknown` | `ufi_board_rev()` returned 0 | measure `board_id_mv` (BOARD_STATUS, bring-up report) and compare with the `BOARD_ID_*_MV` windows |
| Apple port missing in the capabilities on a new revision | `ufi_board_has_apple()` window list not extended | add the new `BOARD_ID_Vxx_MV` window there |
