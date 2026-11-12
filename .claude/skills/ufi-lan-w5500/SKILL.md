---
name: ufi-lan-w5500
description: >
  Ethernet side of the UFI firmware on board v1.0: the register-level W5500 driver over SPI3
  (ufi_w5500.c), the board glue (ufi_lan.c), the own DHCP client with link-local fallback and ARP
  probe (ufi_net.c), the UFI v2 session over TCP with the lan_key HMAC challenge
  (ufi_lan_session.c), LAN.CFG parsing (ufi_lan_cfg.c), NET op 0x48, NET.TXT and UDP discovery
  (ufi_lan_status.c), the LAN rules and drive ownership inside ufi_v2.c, and the host side
  (TcpStream, connect_tcp, discover in protocol2.py). Use when changing any of these files or their
  native tests and fuzz target, when adding a LAN.CFG key, a NET status field, a W5500 socket or a
  v2 command that must behave differently over LAN, and when a board gets no address, no link, or
  drops its session. Trigger phrases: "W5500", "LAN session", "LAN.CFG", "DHCP", "link-local",
  "ufi discover", "ufi net", "lan_key", "lan_write", "Netzwerk", "LAN-Sitzung", "keine IP-Adresse",
  "Laufwerksbesitz USB/LAN", "LAN-Schlüssel", "Ethernet geht nicht". DO NOT use for: v2 frame
  format and command table (ufi-usb-protocol), USB personalities and Greaseweazle lease
  (ufi-usb-modes-gw-msc), write lock hardware and write guards (ufi-write-path-safety), SPI3 and
  W5500 pin assignment (ufi-board-pins), fuzz infrastructure (ufi-fuzz-static-analysis), gate
  mechanics (ufi-test-gate), bench bring-up report (ufi-bringup-diagnostics).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "lan", "w5500", "network"]
---

# UFI LAN: W5500, address, TCP session

## Purpose

Explains how the LAN port is layered, which rules keep it from harming a disk or leaking the key, and
which files move together when the LAN changes. The whole stack is written in this repo from the W5500
datasheet and the RFCs (no WIZnet ioLibrary, no lwIP) and has only been exercised against the native
register fake, never on a cable.

## Layers

| Stage | File | HAL | Role |
|---|---|---|---|
| L1 | `firmware/src/ufi_w5500.c`, `include/ufi_w5500.h` | no | SPI frames, registers, socket commands, buffer pointers, MAC from the UID |
| L2 | `ufi_lan_cfg.c` | no | LAN.CFG text → `lan_cfg_t` |
| L3 | `ufi_net.c` | no | DHCP, link-local, fixed address; state machine `net_service(n, d, now, session_busy)` |
| L4 | `ufi_lan_session.c` | no | TCP server on socket 0, frame reassembly, AUTH 0x49 |
| L5 | `ufi_lan_status.c` | no | `lan_status_pack()` (NET 0x48), `lan_status_text()` (NET.TXT), `lan_disc_service()` |
| glue | `ufi_lan.c`, `include/ufi_lan.h` | yes | SPI3, reset line, INTn polling, timing, callbacks into `ufi_v2.c` |
| rules | `ufi_v2.c` | yes | `lan_refusal()`, `ufi_v2_claim()`, `ufi_v2_route()`, `ufi_v2_lan_frame()`, `ufi_v2_lan_closed()` |

The five HAL-free files are in `PURE` of `firmware/tools/check_static.py` and are compiled natively by
the tests and the fuzz target. Time enters as a `now` argument and hardware through the `w5500_t`
callbacks `select` / `xfer`. Including `ufi_firmware.h` or calling `HAL_GetTick()` there breaks the
native build.

## Invariants and traps

- **Everything is polled from the main loop.** `ufi_lan_service()` reads `PIN_LAN_INT` as a GPIO; there
  is no EXTI handler. PHY every 500 ms, address every 10 ms or when INTn is low, session every pass
  while INTn is low and at least every 2 ms while connected. No LAN code may run in an ISR.
- **Reset ownership.** `board_gpio_init()` parks the chip in reset; `ufi_lan_init()` runs after
  `ufi_sd_init()` and `ufi_config_load()`. `lan_start()` pulses RSTn, calls `w5500_reset()` (VERSIONR
  must read `W5500_VERSION` 0x04) and `w5500_setup()`. SHAR resets to zero, so the MAC is written and
  read back after every reset; a mismatch means `LAN_STATE_NO_CHIP` and the chip goes back into reset.
  On `ufi_board_rev() < 10` the pins are never driven (`LAN_STATE_NO_HW`).
- **Buffer split is fixed at 8/2/2/1 KB** for sockets `W5500_SOCK_SESSION` 0, `W5500_SOCK_DHCP` 1,
  `W5500_SOCK_DISCOVER` 2, `W5500_SOCK_PROBE` 3 (`kb[]` in `w5500_setup()`); the chip has 16 KB per
  direction. `SIMR` is 0x0F, so only sockets 0-3 raise INTn. `test_w5500.c` pins the split.
- **16-bit chip-updated registers** (`Sn_TX_FSR`, `Sn_RX_RSR`) are read until two reads agree
  (`read16_stable()`). Socket buffer addresses are the raw 16-bit pointers; the chip wraps them, so no
  split at the buffer end.
- **One SEND at a time.** `lan_session_send()` returns `LAN_TX_BUSY` until SEND_OK or TIMEOUT of the
  previous SEND (`tx_settle()`), and writes a frame only when it fits completely, so frames never
  interleave. `lan_disc_service()` follows the same rule with `*inflight`.
- **A request is consumed only when its reply has room.** `next_frame()` peeks (`w5500_rx_peek()`),
  checks header, length ≤ `LAN_V2_MAX_PAYLOAD` and CRC, and calls `w5500_rx_skip()` only then;
  `cb->can_dispatch` demands a free `UFI_V2_FRAME_MAX` in the TX buffer. Otherwise TCP holds the data.
- **Frames are the USB frames.** `_Static_assert(LAN_V2_FRAME_MAX == UFI_V2_FRAME_MAX)` in `ufi_lan.c`.
  Bad length or CRC answers cmd 0xFF, seq 0, status 14 and resyncs one byte later, as the USB parser.
- **One TCP connection.** While ESTABLISHED no socket listens; a second client gets a reset. Keep-alive
  `LAN_KEEPALIVE_5S` 2 (10 s). A blocking reply that cannot be sent for `LAN_TX_TIMEOUT_MS` 2000 sets
  `sess.drop`; the close happens in the next service, not inside the request.
- **Session end = safe state.** Link loss, address change (`net.changes`), chip restart, peer close and
  `drop` all reach `ufi_v2_lan_closed()`: if LAN owned the drive, `cancel_ops()` aborts read/write/jobs
  and calls `ufi_drive_safe_state()`. Any new way to end a session must go through
  `lan_session_close()` so `cb->closed` fires.
- **Drive ownership** (`ufi_v2_claim()`): the first transport with a non-read-only request owns the
  drive. LAN may claim only while USB mode is `UFI_USB_FLUX` or `UFI_USB_COMPOSITE` and Greaseweazle
  does not hold the resources. A USB owner is released by channel reset, RESOURCE release, or
  `OWNER_IDLE_MS` 30 s without requests while nothing runs. The non-owner gets status 1 (busy).
- **What LAN may not do** (`lan_refusal()`): BOOTLOADER and USB_MODE → `UFI_ERR_LAN_DENIED` (24);
  READ_FILE of LAN.CFG in any spelling (`is_lan_cfg()`) → 24; everything `locked_write()` lists plus
  WRITE_FILE, DELETE and IEC_SEND → `UFI_ERR_WRITE_PROT` (12) unless `ufi_lan_write_allowed()`, which
  needs `lan_write=1` **and** the hardware write lock open (`!ufi_board_jumper_locked()`).
- **lan_key never leaves the device.** NET reports only `LAN_F_KEY`. Before AUTH succeeds every other
  request gets status 25 and DATA frames are dropped; one answer per challenge; `LAN_AUTH_TRIES` 3 wrong
  answers drop the connection. Over USB op 0x49 answers `UFI_ERR_NOT_IMPL`. A key longer than
  `LAN_KEY_MAX` 64 is cut, so authentication fails instead of switching off. Frames after AUTH are not
  encrypted.
- **DHCP always sets the BROADCAST flag**: until bound, SIPR is 0.0.0.0 and the W5500 drops unicast
  replies. The ARP probe is a one-byte UDP send from socket 3 with SIPR and SUBR 0.0.0.0 and GAR =
  candidate; SEND_OK = address in use, TIMEOUT = free. `w5500_retry()` is changed for the probe and
  must be restored to 2000 / 8 afterwards.
- **`session_busy` blocks voluntary address changes** (link-local → DHCP retry every 60 s); expiry and
  conflicts still change the address and thereby close the session.
- **Memory.** `lan_t` sits in `.sram2` (NOLOAD, cleared by `ufi_lan_init()`), next to the 8 KB write
  ring in the 16 KB SRAM2 bank; AXI SRAM `.bss` is frozen by the gate. `lan.buf` is both the LAN.CFG
  read buffer and the session frame buffer, which is why CFG_RELOAD only sets `lan.reload` and
  `load_cfg()` runs in the next service pass, after the reply went out.
- **Config change restarts the chip.** `ufi_lan_service()` compares old and new `lan_cfg_t` with
  `memcmp`; `lan_cfg_parse()` starts from `lan_cfg_defaults()` (memset), keep it that way or padding
  bytes cause spurious restarts.
- **Discovery port is fixed** at UDP `LAN_DISCOVER_PORT` 55100 independent of `port=`; the reply is a
  broadcast when the asker is not on the device's subnet.

## Procedure: add a LAN.CFG key

1. `include/ufi_lan_cfg.h`: field in `lan_cfg_t`. `ufi_lan_cfg.c`: default in `lan_cfg_defaults()`, a
   commented line in `lan_cfg_default_text` (CRLF, total below `LAN_CFG_MAX_TEXT` 1023), a `word_is(k,
   kn, "key")` branch in the parser; a bad value keeps the default and increments `errors`.
2. `firmware/tests/test_lan_cfg.c`: good, bad and boundary values. The test asserts that the default
   text parses to exactly the defaults.
3. `firmware/tests/fuzz_lan.c` `fuzz_cfg()`: assert the bound of the new field; add the key to
   `LAN_SEEDS` in `firmware/tests/run_fuzz.py`.
4. Consume it in `ufi_lan.c` (`lan_start()` runs again on any change).
5. Table of keys in `docs/USB_Protokoll.md` section 1.6. Unknown keys are ignored by older firmware,
   so no migration is needed.

## Procedure: add a NET status field or discovery field

1. `lan_status_t` in `include/ufi_lan_status.h`; fill it in `ufi_lan_status()` (`ufi_lan.c`).
2. `lan_status_pack()`: schema is 1 and the host name is the variable tail from offset 43, so a new
   fixed field needs a new schema value or a place after the name; adjust `LAN_STATUS_MAX`. Same
   reasoning for `lan_disc_reply()` and `LAN_DISC_REPLY_MAX`. `NET_PKT_MAX` must stay ≥ both
   (`_Static_assert` in `ufi_lan.c`).
3. `lan_status_text()` for NET.TXT within `LAN_TEXT_MAX` 400.
4. Tests: `firmware/tests/test_lan_status.c`; host `NetStatus.unpack` / `DiscoverReply.unpack` in
   `software/ufi_host/ufi_host/protocol2.py` and `software/ufi_host/tests/test_lan.py` (`FakeLan`).
5. Offset table in `docs/USB_Protokoll.md` 1.6; `_lan()` in `bringup.py` if the verdict changes.

## Procedure: add or change a v2 command with respect to LAN

1. Decide in `ufi_v2.c`: read-only (the `readonly` expression in `request()`, else the command claims
   the drive), `uses_drive()`, `allowed_during_dump()`, and whether it writes (`locked_write()` or the
   explicit list in `lan_refusal()`).
2. `firmware/tests/test_lan_runtime.py` extracts `lan_refusal`, `ufi_v2_claim` and friends verbatim
   and carries its own copy of the command numbers in an enum; extend both.
3. Replies must go through `ufi_native_tx_*` (routed by `ufi_v2_route()` in `ufi_composite.c`), never
   straight to a USB endpoint.
4. `firmware/tests/fuzz_v2.c` drives LAN frames with mode bit2; generated stubs cover `ufi_lan_*`.
5. Host status texts (`protocol2.py`, 24/25/26) and `docs/USB_Protokoll.md` 1.6 "Was über LAN nicht geht".

## Examples

```bash
# native tests of the area (name filter is a substring)
python firmware/tests/run_tests.py lan        # test_lan_cfg, test_lan_session, test_lan_status
python firmware/tests/run_tests.py w5500
python firmware/tests/run_tests.py net
python firmware/tests/test_lan_runtime.py     # LAN rules extracted from ufi_v2.c
python firmware/tests/run_fuzz.py --target lan --seconds 30
python firmware/tools/check_static.py
PYTHONPATH=software/ufi_host python -m pytest software/ufi_host/tests/test_lan.py -q -p no:cacheprovider

# done criterion
python firmware/tests/check_all.py

# host
ufi discover --timeout 1
ufi net
ufi --host 192.168.1.50 --lan-port 55100 --lan-key SECRET info
```

Driver use against the fake, as in `firmware/tests/test_lan_session.c`:

```c
static w5500_fake_t fake;
fake_init(&fake);
const w5500_t d = fake_dev(&fake);              /* select/xfer decode real SPI frames */
lan_session_init(&s, 55100, key, key_len, frame);
lan_session_service(&s, &d, now, &cb);          /* DOWN -> LISTEN */
fake_tcp_accept(&fake, W5500_SOCK_SESSION, peer_ip, 40000);
lan_session_service(&s, &d, now, &cb);          /* -> CONNECTED, cb.connected */
```

## Prerequisites

- clang or gcc on the PATH for `run_tests.py`; clang with libFuzzer for `run_fuzz.py`;
  `pip install -r requirements-test.txt` for the host tests.
- Board v1.0 (BOARD_ID revision 10) with U23 and the SD NAND U15 for LAN.CFG and NET.TXT; without a
  file system the defaults apply and nothing is written.
- Reference text: `docs/USB_Protokoll.md` section 1.6 (matches the code as of this writing); test
  overview in `firmware/tests/README.md`.

## Limitations

- Not run on hardware: the bench run of 2026-11-11 had no LAN cable; link, DHCP, the ARP probe with
  sender 0.0.0.0 and the session are proven only against `firmware/tests/w5500_fake.h`.
- `README.md` still says the LAN firmware is missing and the W5500 stays in reset; the code implements
  L1-L5. Trust the code.
- `ufi_board_service()` ends capture and write 300 ms after a previously configured USB connection goes
  away only when `ufi_v2_owner()` is not `UFI_ROUTE_LAN` (`test_board_safety_runtime.py`). A LAN session
  that ends while USB is still gone gets no second safe-state call from that path.
- SPI failures only increment `lan.spi_errors`; no op reports the counter.
- No gratuitous ARP, no DNS client (the `dns` value is stored and reported only), no IPv6, no TLS.
- `ufi_lan.c` itself has no native test (`NEEDS_REAL_HAL`); timing constants there are unproven.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| `ufi net` state 1 "W5500 not detected" | VERSIONR ≠ 0x04, MR.RST stuck, or SHAR read-back mismatch | check U23 soldering, SPI3 pins, RSTn; `bringup.py` step `lan` reports VERSIONR |
| State 2 | `lan=off` in LAN.CFG | set `lan=on`, send CFG_RELOAD (0x54) |
| State 3, no link | PHY bit `W5500_PHY_LNK` clear | cable or switch; polled every 500 ms |
| State 4 for 20 s, then 169.254.x.y | no DHCP offer after three DISCOVERs | expected fallback; DHCP is retried every 60 s while no session is connected |
| Address flips, session drops | `IR.CONFLICT` or lease expiry bumped `net.changes` | fix the duplicate address; NET flags bit7 shows a conflict was seen |
| Every request answers status 25 | `lan_key` set, AUTH not done | host `--lan-key`; `Device2.auth()` |
| Status 26, then disconnect | wrong key (or key cut at 64 characters) | three tries per connection; shorten the key |
| Write over LAN answers status 12 | `lan_write=0` or the write lock switch is closed | both conditions of `ufi_lan_write_allowed()` |
| Non-read-only requests answer status 1 | the other transport owns the drive, or USB is in SD/floppy/GW mode | end the USB session (RESOURCE release, 30 s idle) or switch USB mode |
| Status 24 | BOOTLOADER, USB_MODE or READ_FILE LAN.CFG over LAN | USB only, by design |
| Second client gets a TCP reset | one session at a time | close the first connection |
| Native test fails after adding state to a HAL-free file | file now includes HAL or firmware headers | keep the five L1-L5 files free of `ufi_firmware.h` |
| `test_w5500` fails on buffer sizes | `kb[]` split changed | sum per direction must stay ≤ 16 KB; update the test and the `UDP_RX_BUF` constant in `ufi_net.c` |
| `ufi discover` finds nothing | different segment, or reply broadcast filtered | same L2 segment; UDP 55100 open |
