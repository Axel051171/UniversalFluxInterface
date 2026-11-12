---
name: ufi-usb-modes-gw-msc
description: >
  USB personalities of the UFI firmware other than the native protocol, and the switch between them:
  Greaseweazle-compatible protocol (firmware/src/ufi_gw.c), USB mass storage for the SD NAND and the USB
  floppy drive (ufi_msc.c, ufi_floppy.c), the composite device with Greaseweazle CDC plus native bulk
  interface (ufi_composite.c), descriptors and PIDs (usbd_desc.c, usbd_conf.c), operating-mode selection by
  switch, button B, USB_MODE and UFI.CFG protocol= (ufi_mode.c, ufi_usb_set_mode, parse_line in ufi_dump.c),
  and the drive-resource lease shared by both protocols. Use when changing a Greaseweazle command or its flux
  code, adding a USB mode or PID, touching re-enumeration, endpoint or FIFO layout, MSC storage callbacks,
  the mode switch logic or a UFI.CFG key, or when gw tools, the USB drive or the composite interface do not
  show up or stall. Trigger phrases: "gw read fails", "gw info does not find the board", "USB floppy mode",
  "SD drive mode", "composite device", "add UFI.CFG key", "mode switch", "re-enumeration", "Greaseweazle-Modus",
  "USB-Floppy geht nicht", "Betriebsart umschalten", "UFI.CFG Schluessel ergaenzen", "Geraet meldet sich
  nicht neu an", "SD-Laufwerk erscheint nicht".
  DO NOT use for: native v2 frames, opcodes, status codes and capabilities (ufi-usb-protocol); stand-alone
  dump, copy and SD image writing beyond the config parser, or write guards (ufi-write-path-safety); capture
  timers and the Greaseweazle read stream encoder in ufi_stream.c (ufi-flux-capture-timers); drive bus
  signals and step timing (ufi-drive-interfaces); switch and header pin assignment (ufi-board-pins);
  LAN.CFG and TCP (ufi-lan-w5500); fuzz harness internals (ufi-fuzz-static-analysis).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "usb", "greaseweazle", "mass-storage"]
---

# UFI USB Modes: Greaseweazle, Mass Storage, Composite, Mode Switch

## Purpose

Explains the five USB personalities of the board, how the firmware moves between them, and what must stay
consistent when one of them changes. It covers the Greaseweazle-compatible command set, both mass-storage
backends, the composite wrapper and the UFI.CFG keys that select the personality.

## The five personalities

`enum { UFI_USB_FLUX, UFI_USB_SD, UFI_USB_FLOPPY, UFI_USB_GW, UFI_USB_COMPOSITE }` in
`firmware/include/ufi_firmware.h`; the number is also the USB_MODE 0x05 argument and the index into
`pids[]` and `names[]` in `usbd_desc.c`.

| Mode | PID (VID 1209) | Product string | Class registered in `ufi_usb_set_mode()` | Selected by |
|---|---|---|---|---|
| 0 flux | 4F54 | UFI Flux Engine | `USBD_CDC` | switch middle, `protocol=ufi` (or absent) |
| 1 SD drive | 4F55 | UFI Flux Storage | `USBD_MSC` + `ufi_msc_fops` | switch side EXP_IO2, button B 2 s, USB_MODE 1 |
| 2 USB floppy | 4F56 | UFI USB Floppy | `USBD_MSC` + `ufi_floppy_fops` | switch side EXP_IO1, USB_MODE 2 |
| 3 Greaseweazle | 4F57 | UFI Flux Engine (gw-compat) | `USBD_CDC` | switch middle, `protocol=gw` |
| 4 composite | 4F58 | UFI Native + Flux (gw-compat) | `UFI_Composite` | switch middle, `protocol=composite`, USB_MODE 4 |

ACT blinks mode + 1 times after a change (`show_mode()`): 1 flux, 2 SD, 3 floppy, 4 Greaseweazle,
5 composite. The header comment of `ufi_mode.c` lists 2 as floppy and 3 as SD; the code and
`docs/USB_Protokoll.md` section 1.1 say otherwise.

## Invariants and traps

- **Start-up order** (`ufi_main.c`): `ufi_v2_init()`, `ufi_usb_init()` (always enumerates as mode 0),
  `ufi_config_load()`, then `ufi_mode_init()`. A board with `protocol=gw` therefore appears first as 4F54
  and re-enumerates as 4F57. Without a mountable SD NAND `ufi_config_load()` returns early and the board
  stays in the UFI flux mode.
- **One way in:** every change goes through `ufi_usb_set_mode()`. It refuses with busy while
  `ufi_operation_busy()` (and a dump for the MSC modes), flushes the floppy cache first, sends event 0x84
  with `ufi_v2_event_now()`, resets v2, stops USB for 200 ms, calls `ufi_resources_reset()`, swaps
  descriptors with `usbd_desc_set_mode()` and registers the class. New modes must extend the range checks
  there, in `V2_USB_MODE` (`ufi_v2.c`), in `usbd_desc_set_mode()` (arrays and `mode <= 4u`) and
  `USB_MODES` in `software/ufi_host/ufi_host/protocol2.py`.
- **A failed floppy write-back keeps the mode.** `ufi_floppy_end()` returns the flush error and keeps the
  dirty cache (the only copy of the data, ERR LED on); `ufi_usb_set_mode()` then returns that error and
  `ufi_mode_service()` retries once per second (`pending`, `retry_at`). Do not add a path that drops the
  cache to force the switch. `regression_control` in `test_regressions.py` pins this.
- **MSC modes are polled.** `ufi_usb_set_mode()` disables `OTG_HS_IRQn` for modes 1 and 2 and
  `ufi_usb_poll()` runs `HAL_PCD_IRQHandler()` from the main loop, so the storage callbacks may block on
  SD or drive access. Consequence: no CDC and no command channel exists in these modes (leave by switch,
  button B or reset), and long blocking work elsewhere in the main loop starves USB.
- **The live device descriptor is `USBD_HS_DeviceDesc` in `usbd_desc.c`** (registered through `HS_Desc`).
  `USBD_DeviceDesc` and `UFI_VID/UFI_PID` in `ufi_usb.c` are not what the host sees.
- **Each mode has its own PID on purpose** so a host does not reuse a driver binding of another mode; the
  serial number (MCU UID) is the same in all modes and is how the host finds the board again.
- **`protocol=` parsing** (`parse_line()` in `firmware/src/ufi_dump.c`, not `ufi_sd.c`): keys are matched
  with `strncmp` at the line start (blanks skipped), lower case, no blank around `=`; values go through
  `word_is()` (case-insensitive, must end at blank, `#` or line end). Only the first 1023 bytes of the
  file are read. `protocol_gw` / `protocol_composite` are assigned only when a `protocol=` line exists, so
  deleting the line and sending CFG_RELOAD keeps the previous personality until reset.
- **CFG_RELOAD 0x54 can re-enumerate.** `read_switch()` returns `ufi_usb_flux_mode()` for the middle
  position, so a changed `protocol=` looks like a switch movement to `ufi_mode_service()` and is applied
  after the 50 ms debounce once nothing is busy.
- **Both switch inputs high is a wiring fault** (`read_switch()` returns 0xFF) and is ignored.
- **Button B** (`ufi_mode_button_b()`) acts only while the switch is in the middle and toggles
  SD drive ↔ flux personality; from another mode with the switch in the middle it selects the SD drive.
- **Greaseweazle replies are capped:** `reply()` sends `[cmd, ack]` plus at most 32 bytes; a command is
  `[cmd, len, args]` with `len` 2..64 (`cbuf`), any other length byte answers `ACK_BAD_COMMAND` and
  resynchronises. Output uses `ufi_usb_tx_blocking()` on the CDC interface in modes 3 and 4.
- **Greaseweazle clock is 275 MHz / 4:** `GW_SAMPLE_FREQ` 68.75 MHz, `GW_TICK_SHIFT` 2. The write decoder
  (`w_put()`) rejects 0 and values above `UINT32_MAX >> GW_TICK_SHIFT` instead of clamping.
- **Greaseweazle writes:** firmware precompensation is switched off around `ufi_write_local_gw()` and on
  again afterwards; the host's last interval is sacrificial (`w_count - 1`); fewer than 4 intervals, a
  pending SPACE or a partial code end as `ACK_FLUX_UNDERFLOW`; hard-sectored writes and index opcodes in a
  write stream are refused; `ufi_board_write_locked() || ufi_drive_write_protected()` gives `ACK_WRPROT`
  (the session write lock is part of `ufi_board_write_locked()`).
- **Entering Greaseweazle mode changes drive settings:** `ufi_gw_begin()` forces `double_step = 0`, clears
  hard-sector mode and loads the power-on delays (watchdog 10000 ms). A quiet host with a drive selected
  loses motor and selection after the watchdog; `watchdog = 0` disables it.
- **Only PC and Shugart units exist in this protocol** (`unit_drive()`): IBM PC units 0/1, Shugart 0..3.
  Amiga, IEC, Apple and Mac drives need the UFI protocol. Cylinders 0..`GW_MAX_CYL` (83); UPDATE,
  SWITCH_FW_MODE, TEST_MODE and GET_INDEX_TIMES answer `ACK_BAD_COMMAND`.
- **Composite lease** (`ufi_resources.c`): drive, timers and flux store belong to `UFI_OWNER_NATIVE` or
  `UFI_OWNER_GW`. Every Greaseweazle command except GET_INFO and GET_FLUX_STATUS acquires the lease or
  answers `ACK_BAD_COMMAND`; DESELECT or the watchdog releases it. Native non-read-only requests acquire
  it or answer status 1; RESOURCE 0x0A with payload 0 or 30 s idle releases it. A channel reset of one
  side must not cancel the other: `ufi_gw_clear_comms()` and `ufi_v2_clear_comms()` check the owner first.
- **Composite layout is asserted byte for byte:** `descriptors()` builds 98 bytes, 3 interfaces, 5
  endpoints, 1 IAD, WinUSB compat ID and the interface GUID for interface 2 only;
  `regression_composite_descriptors` in `test_protocol_faults.py` checks these numbers. Native bulk is
  0x03/0x83 with 64-byte packets; `usbd_conf.c` gives TX FIFO 3 0x100 words, total 736 of 1024.
  USB is full speed on the internal PHY, DMA off.
- **A stand-alone dump owns the drive in every mode:** Greaseweazle commands answer `ACK_BAD_COMMAND`
  while `ufi_dump_active()`, and `ufi_dump_start_fmt()` refuses while a mass-storage mode is active, so
  host and firmware never use the FAT volume at the same time.
- **USB floppy uses the stand-alone drive** (`FLOPPY_DRIVE` = UFI.CFG `drive=`), 512-byte sectors only;
  ED 2.88M and Atari 11-sector disks are read-only (`fmt_t.writable`).

## Procedure: change or add a Greaseweazle command

1. Compare with `cdc_acm_protocol.h` of the Greaseweazle firmware (public domain); this file is an
   independent implementation, keep it that way and do not paste GPL host code.
2. Edit `process_command()` in `firmware/src/ufi_gw.c`: validate `clen` exactly, return an `ACK_*`, send
   data with `reply()` (32 bytes) or, for streams, with `ufi_usb_tx_blocking()` while feeding
   `UFI_WATCHDOG_FEED()` and checking `ufi_cancel_requested()` as `do_source()` does. Raise `CMD_MAX` only
   when the new highest command is implemented; it is reported in GET_INFO.
3. Map firmware errors through `ack_of()`; unknown errors become `ACK_FLUX_OVERFLOW`.
4. Tests: `firmware/tests/test_gw.py` extracts `write_finish`, the write decoder and the stream encoder
   and compares them with `firmware/tests/reference/greaseweazle_encoder.py`; add the new function to the
   matching group or a new `run('regression_gw_...')`. Add a seed to `GW_SEEDS` in
   `firmware/tests/run_fuzz.py` using `gw(cmd, *args)`.
5. Declare new driver calls in `firmware/include` (stubs for `fuzz_gw.c` are generated from prototypes).
6. Docs: `docs/USB_Protokoll.md` section 10 (10.2 command table, 10.3 flux code).
7. Done when `python firmware/tests/test_gw.py`, `python firmware/tests/run_fuzz.py --target gw` and
   `python firmware/tests/check_all.py` pass.

## Procedure: add a UFI.CFG key

1. `parse_line()` in `ufi_dump.c`: add an `else if (!strncmp(k, "key=", N))` branch with the exact length;
   numbers come from `num`, words from `word_is()`. Clamp or reject out-of-range values there.
2. Reset the backing static to its default at the top of the `f_open` branch in `ufi_config_load()` if a
   removed line should fall back on reload (see the list starting with `index_sim = 0`).
3. Add the key with a comment to `cfg_default[]`; it is written with CRLF when UFI.CFG is missing.
4. Expose it through an accessor declared in `ufi_firmware.h` (`ufi_config_*`), and call
   `ufi_config_changed()` where the value alters guarded behaviour.
5. Add the key to a `presets[]` entry in `firmware/tests/fuzz_sd.c` (operation 6 parses the fuzz input as
   UFI.CFG) and run `python firmware/tests/run_fuzz.py --target sd`.
6. Document the key in the `UFI.CFG` bullet of section 3.3 of `docs/USB_Protokoll.md` and in
   `software/ufi_host/README.md` (`ufi cfg key=value` writes the file and sends CFG_RELOAD).

## Procedure: change descriptors, endpoints or a mode

Touch together: `usbd_desc.c` (PID, product string, class bytes), `descriptors()` and the endpoint
constants in `ufi_composite.c`, FIFO sizes in `usbd_conf.c`, the numbers in
`regression_composite_descriptors`, table 1.1/1.2 of the doc, and the host matching in
`software/ufi_host/ufi_host/usb_bulk.py` (finds 1209:4F58 and claims interface 2). Product string 3 must keep the substring `gw-compat`; the gw
tools find the board by it.

## Examples

Checks for this area, from the repository root:

```bash
python firmware/tests/test_gw.py                   # gw decoder, encoder, write finish, read deadline
python firmware/tests/test_protocol_faults.py      # composite descriptor layout
python firmware/tests/test_regressions.py          # regression_control: floppy end, ufi_usb_poll
python firmware/tests/run_fuzz.py --target gw --seconds 30
python firmware/tests/run_fuzz.py --target sd --seconds 30
python firmware/tests/check_all.py                 # full offline gate
python firmware/tools/check_static.py              # ufi_gw, ufi_mode, ufi_dump against hal_stub
```

Host side, as documented in `software/ufi_host/README.md`:

```bash
ufi cfg protocol=gw            # edit UFI.CFG on the SD NAND, then CFG_RELOAD
ufi mode flux|sd|floppy|gw|composite
ufi --port usb describe        # native bulk interface of the composite device
```

Greaseweazle command framing as used in the fuzz seeds (`run_fuzz.py`):

```python
def gw(cmd, *args):
    return bytes([cmd, 2 + len(args), *args])
# SET_BUS_TYPE IBM PC, SELECT unit 0, MOTOR on, SEEK 5
gw(14, 1) + gw(12, 0) + gw(6, 0, 1) + gw(2, 5)
```

## Prerequisites

- clang for the regressions and libFuzzer targets; Python with `requirements-test.txt`.
- For the ARM-only files (`ufi_usb.c`, `ufi_composite.c`, `ufi_msc.c`, `ufi_sd.c` are in `NEEDS_REAL_HAL`)
  a cross build and `python firmware/tools/check_static.py --compile-commands
  firmware/build/compile_commands.json`.
- `docs/USB_Protokoll.md` sections 1.1, 1.2, 1.4, 2.5 and 10; ST USB device library class sources from
  STM32Cube H7 V1.11.0.

## Limitations

- No personality has been verified on hardware; the doc states that Windows/Linux/macOS enumeration of the
  composite device and full Greaseweazle behavioural compatibility are unproven.
- PID 1209:4F58 is a development identity that needs an allocation check before publication.
- `ufi_mode.c`, `usbd_desc.c`, `usbd_conf.c` and the MSC callbacks have no native regression; only the
  cross build, static analysis and bench tests cover them.
- The Greaseweazle read path (capture limits, stream encoder) lives in `ufi_flux.c` and `ufi_stream.c` and
  is outside this skill.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| `gw info` does not find the board | mode is not 3 or 4, or the product string lost `gw-compat` | check `UFI.CFG protocol=`, ACT blink count (4 or 5), `names[]` in `usbd_desc.c` |
| `protocol=gw` in UFI.CFG has no effect | blank after `=`, upper-case key, line beyond byte 1023, SD not mounted, or switch not in the middle | fix the line; the switch positions override `protocol=` |
| Board shows up as 4F54 briefly, then again as 4F57 | start-up enumerates as flux before `ufi_mode_init()` applies the mode | expected; host tools must wait for the second enumeration |
| Every gw command answers ack 1 in composite mode | the native side holds the lease, or a dump is running | native host sends RESOURCE 0 (`ufi --port usb release`) or waits 30 s |
| Native requests answer status 1 in composite mode | Greaseweazle holds the lease | `gw` DESELECT, or wait for its watchdog (default 10 s) |
| gw write ends with ack 5 | stream stalled longer than the watchdog, fewer than 4 intervals, or an interval out of range | host clears comms (10000 baud) and resends; after a watchdog stall `transport_fault` drops input until then |
| Mode does not change after moving the switch | capture, write or dump running (`busy()`), or floppy write-back failed (ERR LED) | finish or abort the operation; fix write protection, the retry runs every second |
| Device unreachable by `ufi` after USB_MODE 1 or 2 | mass-storage modes have no CDC interface | switch, button B 2 s or reset |
| Host reports I/O errors on the USB floppy | no decodable 512-byte format on track 0, or `ufi_floppy_ready()` not OK (no disk) | use the flux mode for other formats; check `drive=` in UFI.CFG |
| `regression_composite_descriptors` fails after a descriptor edit | config length, interface or endpoint count changed | update the test numbers together with doc 1.2 and the FIFO budget in `usbd_conf.c` |
| `cannot extract production function ufi_gw.c:NAME` | extracted function renamed or closing brace not in column 0 | restore the shape or update `test_gw.py` |
