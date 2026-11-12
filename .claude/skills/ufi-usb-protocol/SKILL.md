---
name: ufi-usb-protocol
description: >
  Native UFI USB protocol of the STM32H723 firmware (v2 dispatcher in firmware/src/ufi_v2.c plus the legacy
  v1 commands in ufi_usb.c): frame layout and CRC, one-reply-per-request rule, READ/WRITE DATA and END frames,
  events, status and error codes, the dispatcher gates (session write lock, LAN rules, drive ownership, busy
  lists), INFO caps, FEATURES, flag masks and the capability matrix, and the full touchpoint list for adding
  or changing a command so firmware, tests, fuzz target, Python host and docs/USB_Protokoll.md stay in step.
  Use when adding or changing a v2 command, a reply layout, a request flag, a status code, an event or a
  capability bit, when a host tool reports a frame error, status 1/15/16, a timeout or a stale frame, or when
  reviewing a protocol change. Trigger phrases: "add a v2 command", "new opcode", "change the reply payload",
  "status code", "frame error", "capability bit", "FEATURES", "neuen Befehl hinzufuegen", "USB-Protokoll
  aendern", "Statuscode ergaenzen", "Rahmenfehler", "Antwort-Nutzdaten erweitern", "Befehlstabelle".
  DO NOT use for: Greaseweazle protocol, USB mass storage, composite descriptors, mode switch and UFI.CFG
  (ufi-usb-modes-gw-msc); TCP transport, LAN.CFG and lan_key (ufi-lan-w5500); flux capture and stream code
  internals (ufi-flux-capture-timers); write backends and write guards (ufi-write-path-safety); running or
  extending the test gate itself (ufi-test-gate); fuzz harness internals (ufi-fuzz-static-analysis).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "usb", "protocol", "host"]
---

# UFI USB Protocol (v2 dispatcher)

## Purpose

Describes how the native UFI protocol is framed, gated and answered in `firmware/src/ufi_v2.c`, and which
files must change together when a command, flag, status code, event or capability is added. The binding
specification is `docs/USB_Protokoll.md` (German, sections 1 to 9); the Python host in
`software/ufi_host/ufi_host/protocol2.py` and the fake device in its tests mirror it byte for byte.

## Where the protocol lives

| Piece | File / symbol |
|---|---|
| Frame, dispatcher, events, ownership | `firmware/src/ufi_v2.c`: `request()`, `check_frame()`, `send()`, `ufi_v2_end()`, `ufi_v2_event()` |
| Opcodes | `enum { V2_PING ... }` in `ufi_v2.c` (private); `UFI_V2_READ` 0x20, `UFI_V2_WRITE` 0x22, frame types and event codes in `firmware/include/ufi_firmware.h` |
| Status codes | `ufi_error_t` in `firmware/include/ufi_fixes.h`; wire status = minus the error code (`st()`) |
| ANALYSIS_JOB 0x0F sub-operations | `ufi_analysis_command()` in `firmware/src/ufi_analysis_job.c` (first payload byte = op) |
| Transport hand-off | `ufi_usb_receive_callback()` / `ufi_usb_line_coding()` in `ufi_usb.c`, `ufi_native_tx_*` in `ufi_composite.c`, `ufi_v2_lan_frame()` for TCP |
| Legacy v1 | `ufi_usb_process_command()` in `ufi_usb.c` (one command per USB packet, 64-byte buffer, reply header + up to 64 bytes) |
| Flag masks, capability matrix, config generation | `firmware/include/ufi_capabilities.h`, `firmware/src/ufi_capabilities.c` |
| Integrity jobs, cancel flag | `ufi_job.c` (`UFI_JOB_HEADER` 16 bytes), `ufi_cancel.c` |
| Host | `protocol2.py` (`Device2`, `FrameParser`, constants), `protocol.py` (v1), `capabilities.py`, `cli.py` |

## Wire rules

Frame, both directions, little endian: `55 AA type cmd seq status len16 payload[len] crc16`.
`UFI_V2_HDR` = 8, `UFI_V2_MAX_PAYLOAD` = 4096, `UFI_V2_FRAME_MAX` = 4106. CRC-16/CCITT-FALSE
(poly 0x1021, init 0xFFFF) over `type` up to the last payload byte. Types: 1 request, 2 reply, 3 event,
4 data, 5 end.

- Exactly one reply per request, carrying the request's `cmd` and `seq`. READ and WRITE reply at once and
  close later with an END frame (`ufi_v2_end()`), READ with DATA frames in between, WRITE with DATA frames
  from the host. GUARDED_READ 0x28 replies on 0x28, its DATA and END use 0x20 with the same `seq`.
- `seq` 1..255 from the host; 0 is reserved for the frame-error reply `cmd=0xFF, seq=0, status=14`
  (`frame_error()`), sent for a bad CRC or `len` above 4096; the parser then resynchronises on the next
  `55 AA` (`resync()`).
- Session latch: in the UFI flux personality a USB packet that starts with `55 AA` switches to v2
  (`ufi_v2_rx()` sets `on`); v1 stays active until then. SET_LINE_CODING 10000 baud calls
  `ufi_v2_clear_comms()`: back to v1, running READ/WRITE aborted. In the composite personality the native
  bulk interface speaks v2 only and is reset by vendor EP0 requests (`bRequest` 0x21 reset, 0x22 cancel).
- Events (0x80..0x85) are sent only after EVENTS 0x06 set the mask, only while the transmitter is idle, from
  a queue of `EVQ_SIZE` 4 with at most `EVQ_PAYLOAD` 8 bytes each; a full queue drops silently.
- Timeouts in `ufi_v2_service()`: WRITE upload without DATA for 10 s ends with END status 5; an open
  integrity job idle for 10 s fails; the USB drive owner is released after `OWNER_IDLE_MS` 30 s.

## Dispatcher gates (order in `request()`)

A new command passes these checks before its `case` runs. Each is a hand-maintained list; a command that
is missing from a list gets the default, which is the strict one except for `readonly`.

| Order | Check | Default for an unlisted command | Refusal |
|---|---|---|---|
| 1 | `ufi_board_soft_locked() && locked_write(cmd, p, len)` | not a disk write | 22 |
| 2 | LAN origin: `lan_refusal()` (BOOTLOADER, USB_MODE, reading LAN.CFG; writes without `lan_write`) | allowed | 24 / 12 |
| 3 | `readonly` expression, else `ufi_v2_claim(origin)` | **not** read-only: claims the drive for this transport | 1 |
| 4 | composite personality: `ufi_resources_acquire(UFI_OWNER_NATIVE)` for non-read-only | acquires the lease | 1 |
| 5 | `allowed_during_dump(cmd)` while a stand-alone dump runs | refused | 1 |
| 6 | `uses_drive(cmd)` while READ/WRITE or `ufi_operation_busy()` | refused | 1 |
| 7 | `switch (cmd)`; `default` | | 15 |
| 8 | after the case: pending cancel overrides the result | | 20 |

Status 16 and 21 are recorded for ANALYSIS_JOB op 39 (`refusal[]`); a parser names the failing field with
`refuse(REFUSE_*, detail)` before returning `UFI_ERR_BAD_ARGS`.

## Invariants and traps

- **Never transmit directly.** All output of `ufi_v2.c` goes through `send()` or `ufi_native_tx_*`, which
  route to CDC, composite bulk or LAN by `ufi_v2_route()`. Calling `ufi_usb_tx_blocking()` or
  `CDC_Transmit_HS()` from a command breaks composite and LAN sessions.
- **One static reply buffer.** `out = &tx[UFI_V2_HDR]`, `n` = payload length. The request payload `p` points
  into `fb`, a different buffer, and may be overwritten in place. `send()` turns an impossible length into
  status 10 with an empty payload instead of overrunning; a reply whose size depends on a provider needs a
  `_Static_assert` like the one for `QUALITY_MAX_WIRE`.
- **RESET, BOOTLOADER and USB_MODE send their own reply and `return`** because the transport goes away;
  they flush with `ufi_native_tx_flush(100)` first. Every other case must fall through to the single
  `send()` at the end of `request()`.
- **`ufi_native_tx_blocking()` does not block in composite**: it copies into one of two queued slots and
  returns `UFI_ERR_BUSY` when both are taken. The parse loop in `ufi_v2_service()` only runs while
  `ufi_native_tx_room()`; do not emit more than one frame per request from inside a case.
- **IRQ context:** `ufi_v2_rx()` and `ufi_v2_clear_comms()` run in the USB interrupt and only touch the
  ring (`rx`, 8192 bytes, OUT endpoint paused below 1024 free) and flags. `request()` runs in the main
  loop; blocking drive calls there must stay cancellable (`ufi_delay_ms()`, `ufi_cancel_requested()`).
- **`rx` and `link` live in NOLOAD DTCM sections** and are not zeroed by start-up code; `ufi_v2_init()`
  clears `link`. New state that must start at zero belongs in ordinary `static` storage or in that init.
- **ANALYSIS_JOB ops are read-only unless listed.** The `readonly` expression names the mutating op numbers
  of 0x0F explicitly. A new op that changes state and is not added there runs without claiming the drive,
  so the other transport can use it while a session owns the drive.
- **A new disk- or drive-changing command belongs in `locked_write()`.** That one list feeds both the
  session write lock (status 22) and the LAN write rule; leaving it out bypasses both.
- **INFO `caps` is full.** `caps()` sets bits 20 to 31 unconditionally (the host fake uses
  `CAPS_1_18 = 0xFFFFFF7F`). New features are announced through FEATURES 0x0C: `FEATURE_COUNT`, `enabled[]`
  and `limits[]` in `ufi_v2.c`, `FEATURE_NAMES` in `protocol2.py`.
- **Request flag masks are data.** `UFI_V2_READ_FLAG_MASK`, `UFI_V2_WRITE_FLAG_MASK`,
  `UFI_V2_WRITE_FLAGS2_MASK` in `ufi_capabilities.h` are what the parsers accept and what op 38 reports;
  a new bit needs the mask, the parser and section 3.6/4.7 of the doc.
- **Replies grow by appending.** New replies start with a schema byte (`out[0] = 1`); existing ones are
  extended at the end so older parsers keep reading the prefix (END generation block
  `UFI_V2_GEN_BLOCK` 17 bytes, provenance, read context). Do not reorder or resize existing fields.
- **Refuse before side effects.** `test_native_requests.py` runs `cmd_read` over all 256 flag values and
  every length and asserts that a refused request touched neither the capture start nor the hard-sector
  setting; a failed start must roll settings back (see `ufi_flux_hard_sectors(previous[4])`).
- **Tests extract functions by regex.** `function('ufi_v2.c', 'cmd_read')` in
  `firmware/tests/test_regressions.py` matches from the signature to the first `}` in column 0. Renaming an
  extracted function or indenting its closing brace breaks the extraction; a new helper call inside it
  fails to compile until the test's C preamble defines a double. Several tests also carry hand-copied opcode enums
  (`test_native_requests.py`, `test_lan_runtime.py`).
- **Settings that alter behaviour must call `ufi_config_changed()`**; GUARDED_READ/GUARDED_WRITE compare
  `ufi_config_generation()` and refuse stale requests with 16.
- **v1 and v2 share completion paths.** `ufi_usb_send_read_done()` and `ufi_usb_send_event()` first offer
  the result to `ufi_v2_end()`, which returns false when no v2 operation is open; then the v1 message goes
  out. A WRITE verify timeout is reported as 6, not 5.

## Status codes

Wire status is `-ret` of `ufi_error_t`: 0 OK, 1 busy, 2 no drive, 3 seek, 4 no index/aborted, 5 timeout,
6 DMA/verify, 7 USB, 8/9 IEC, 10 buffer full, 11 not supported, 12 write protected, 13 storage, 14 frame,
15 unknown command, 16 bad arguments, 17 not found, 18 interval range, 19 track long, 20 cancelled,
21 media changed, 22 session write lock, 23 speed mismatch, 24 LAN denied, 25/26 LAN auth.
Adding one touches `ufi_fixes.h` (keep the `typedef enum { ... } ufi_error_t;` shape, tests extract it),
`STATUS_TEXT` and the named constant in `protocol2.py`, and table 3.2 of the doc. The Greaseweazle side
maps unknown errors to `ACK_FLUX_OVERFLOW` in `ack_of()` (`ufi_gw.c`).

## Procedure: add a v2 command

1. Pick the code. 0x00–0x0F and 0x10–0x1F are fully assigned; free ranges are 0x2A–0x2F, 0x38–0x3F,
   0x4A–0x4F, 0x55–0x5F, 0x65 upward. Analysis or evidence functions usually become a new ANALYSIS_JOB op
   instead (`ufi_analysis_command()`), which needs no new opcode.
2. `ufi_v2.c`: add `V2_NAME` to the enum and a `case` in `request()`. Check `len` exactly, set `ret` to a
   `UFI_ERR_*`, write the reply into `out`, set `n`. No transmit, no early `return`.
3. Classify the command in every gate list that applies: `readonly`, `allowed_during_dump()`,
   `uses_drive()`, `locked_write()`, `lan_refusal()`.
4. Declare any new driver function in a header under `firmware/include`. `firmware/tests/fuzz_stubs.py`
   generates the fuzz stubs from header prototypes; a missing prototype is a link error in the v2 target.
5. Tests: a regression in `firmware/tests/test_native_requests.py` or `test_protocol_faults.py` that
   extracts the new parser function; a seed in `V2_SEEDS` of `firmware/tests/run_fuzz.py`
   (`record(4, code, seq, payload)`); if the case calls a provider with an `(out, capacity)` contract, a
   bounded double in `firmware/tests/fuzz_v2.c` like `ufi_quality_report`, otherwise the generated stub
   returns an arbitrary length and the fuzzer reports a false overflow.
6. Host: constant, `struct.Struct` and a `Device2` method in `protocol2.py`; subcommand in `cli.py`; a
   branch in `FakeV2._handle` and a test in `software/ufi_host/tests/test_protocol2.py`.
7. Docs: row in table 3.1 of `docs/USB_Protokoll.md`, details in 3.3 or an own subsection with the
   firmware version tag (currently `1.18-dev`, `UFI_FW_VERSION` in `firmware/CMakeLists.txt`); command line
   in `software/ufi_host/README.md`; an entry with the offline evidence in `docs/Implementation_Status.md`.
8. Run the gate (see Examples). Done = `check_all.py` ends with every group passed, the v2 fuzz target
   runs clean, `check_static.py` passes, and the ARM build has no new warning.

## Procedure: extend a reply, add a flag, add an event

- **Reply field:** append behind the existing bytes, keep the schema byte, update `n`, the host
  `struct.Struct` (accept the old shorter length), `FakeV2`, and the doc row. For END payloads also check
  the buffer sizes in `ufi_v2_end()` and `ufi_usb_send_read_done()` / `ufi_usb_send_event()`.
- **Request flag:** extend the mask in `ufi_capabilities.h`, parse the optional field in flag order
  (`cmd_write` keeps a running offset `at` and ends with `len != at` → 16), add a `REFUSE_*` detail, add the
  host constant, extend the exhaustive loops in `test_native_requests.py`, document in 3.6 and 4.7.
- **Event:** add the code to the enum in `ufi_firmware.h` (0x80..0x9F fit the 32-bit mask), call
  `ufi_v2_event()` from main-loop code, payload at most 8 bytes; host `EV_*` and `EVENT_NAMES` (which
  defines `EVENTS_ALL`); doc table 2.3.

## Examples

A complete read-only query with a schema byte, as the dispatcher writes them (`V2_WRITE_LIMITS`):

```c
case V2_WRITE_LIMITS: {
    if (len != 0) { ret = UFI_ERR_BAD_ARGS; break; }
    uint32_t words = 0;
    ufi_flux_store(&words);
    out[0] = 1;                     /* schema */
    out[1] = 0xFF;
    put32(&out[2], FLUX_TIMER_FREQ);
    put32(&out[6], words);
    put32(&out[10], WRITE_MIN_DELTA);
    put32(&out[14], WRITE_MAX_DELTA);
    n = 18u;
    break;
}
```

Checks after a protocol change, from the repository root:

```bash
pip install -r requirements-test.txt
python firmware/tests/test_native_requests.py      # request parsers, lock list
python firmware/tests/test_protocol_faults.py      # frame parser, reply bounds, diagnostics
python firmware/tests/test_lan_runtime.py          # LAN rules and drive ownership from ufi_v2.c
PYTHONPATH=software/ufi_host python -m pytest software/ufi_host/tests/test_protocol2.py -q -p no:cacheprovider
python firmware/tests/run_fuzz.py --target v2 --seconds 30
python firmware/tools/check_static.py
python firmware/tests/check_all.py                 # full offline gate, report in firmware/tests/build/offline_gate.json
cmake -S firmware -B firmware/build -G Ninja && cmake --build firmware/build
```

## Prerequisites

- clang (libFuzzer and `llvm-nm` for `run_fuzz.py`, analyzer for `check_static.py`) or gcc for the native
  regressions; Python with `requirements-test.txt` (pytest, pyserial, py65).
- `PYTHONPATH=software/ufi_host` when running host tests outside `check_all.py` (the gate sets it itself).
- arm-none-eabi-gcc and STM32Cube H7 V1.11.0 only for the cross build.
- `docs/USB_Protokoll.md` sections 2 and 3 (German) at hand.

## Limitations

- Nothing here is qualified on hardware (`check_all.py` prints that hardware qualification remains open);
  transport timings (1 s IN timeout, 100 ms flush) are code facts, not measurements.
- ANALYSIS_JOB sub-operations (the doc numbers them up to op 56) are only outlined; their payloads are in
  doc sections 4 to 6 and `ufi_analysis_job.c`.
- The doc is authoritative for payload layouts; where it and the code differ, the code wins and the doc
  must be corrected in the same change.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| `RuntimeError: cannot extract production function ufi_v2.c:NAME` | function renamed, signature spans a `{`/`;`, or closing brace not in column 0 | restore the shape or update the name in the test that calls `function()` |
| `run_fuzz.py --target v2` ends with an unresolved symbol | new driver call has no prototype in `firmware/include` (or `tests/hal_stub`) | add the prototype; stubs are generated from headers |
| Fuzzer reports a buffer overflow in a new case right after a stubbed provider | generated stub returned a length above the capacity argument | write a bounded double in `fuzz_v2.c` |
| Host gets status 1 for a query while a dump or READ runs | command missing from `allowed_during_dump()` or counted by `uses_drive()` | add it to the list if it touches neither drive nor file system |
| New write command ignores the session lock or LAN `lan_write=0` | not in `locked_write()` | add it; `test_native_requests.py` and `test_lan_runtime.py` carry copies of the enum |
| Host reports `frame error` (status 14, cmd 0xFF, seq 0) | CRC over the wrong range or `len` above 4096 | CRC covers bytes 2..7+len; split payloads at `info.max_payload` |
| Host times out, device silent, composite mode | both reply slots full or `tx_fault` after a 1 s IN timeout | host resets the channel (EP0 0x21 plus CLEAR_FEATURE on 0x83, `BulkStream.reset_input_buffer()`) |
| Event never arrives | mask bit not set, queue full, payload above 8 bytes, or `ufi_v2_event()` called before the session is on | check `ev_mask`, `EVQ_PAYLOAD`, `EVQ_SIZE` |
| Linux CI fails with "static declaration follows non-static" in a regression | a test double declared `static` clashes with a prototype from an included firmware header | define the double without `static` (as for `ufi_config_generation`) |
