# Developing a tool: USB protocol

**Sprache / Language:** [Deutsch](USB-Protokoll) | [English](USB-Protocol)

This guide is for custom host applications, GUIs and scripts. Use the **native UFI v2 protocol**. The [reference guide](USB-Protocol-Reference) indexes the complete German specification, including command tables and specialist functions. Version covered: firmware 1.18-dev, 14 November 2026. Implementation and hardware evidence are distinct; see [project status](Project-Status).

## Choose the transport

| Mode | VID:PID | Interface for your application |
|---|---|---|
| Native UFI | `1209:4F54` | USB CDC serial byte stream; simplest starting point |
| Composite | `1209:4F58` | Interface 2, vendor class, bulk OUT `0x03`, IN `0x83`; UFI v2 only |
| Greaseweazle | `1209:4F57` | Separate protocol; do not send native UFI v2 frames |
| SD / USB-Floppy | `1209:4F55` / `1209:4F56` | Mass storage; no normal UFI CDC port |

Serial baud rate does not determine USB transfer speed. **Setting 10000 baud resets the CDC channel**; then restore a value such as 115200. A channel reset can interrupt existing work: close other clients first. Composite has separate EP0 reset/cancel requests; do not reconfigure the entire USB device. The development PID `4F58` and actual operating-system support need separate consideration. See reference sections 1.1, 1.2 and 2.5.

A USB mode change causes re-enumeration. The MCU serial number helps identify the same board afterward. The reviewed [UnifiedFloppyTool](UnifiedFloppyTool-EN) board integration uses native CDC. For network clients, see [LAN](LAN-EN).

## Starting a session

1. Select and open a specific port; distinguish multiple devices by serial number.
2. Reset your transport channel and discard stale input.
3. Send `PING` with a fresh random identifier and check the identical reply.
4. Read `INFO`: evaluate protocol version, board revision, timebase and payload limit.
5. According to advertised capabilities, query `DESCRIBE`, `FEATURES`, `WRITE_LIMITS` and `0x0F` discovery/mask operations. Do not assume extensions are present.
6. Keep one request outstanding; dispatch events separately and follow streaming operations through their matching `END`.

A successful `PING` establishes communication, not drive qualification. INFO bits, current port availability, supported flags and hardware qualification are separate statements. Configuration setters can change generations: obtain tokens for guarded operations after configuring the device.

## Frame layout

All multibyte values are **little endian**. Frame size is `10 + payload_length` bytes, at most 4106 bytes.

| Offset | Length | Field |
|---:|---:|---|
| 0 | 2 | Sync `55 AA` |
| 2 | 1 | Type: 1 request, 2 response, 3 event, 4 data, 5 end |
| 3 | 1 | Command `cmd` |
| 4 | 1 | Sequence `seq` |
| 5 | 1 | Status; 0 in requests |
| 6 | 2 | Payload length, 0 through 4096 |
| 8 | variable | Payload |
| 8 + length | 2 | CRC-16/CCITT-FALSE over offset 2 through the last payload byte |

CRC: polynomial `0x1021`, initial value `0xFFFF`, no reflection and no final XOR. The CRC is also stored little endian. ASCII `123456789` produces `0x29B1`.

Hosts use sequence numbers 1 through 255, then restart at 1. Response, DATA and END belong to the request; events have their own sequences. A frame-error response can use `cmd=0xFF, seq=0, status=14` and belongs to the outstanding request.

### Verified hex vectors

The final two bytes are the calculated CRC, not placeholders.

```text
PING request, seq=1, ASCII UFI2:
55 AA 01 00 01 00 04 00 55 46 49 32 38 08
PING response, seq=1:
55 AA 02 00 01 00 04 00 55 46 49 32 F7 B9
SELECT A, seq=2:
55 AA 01 10 02 00 01 00 01 B9 30
READ track=0, side=0, revs=3, flags=0, period_ms=0, seq=3:
55 AA 01 20 03 00 06 00 00 00 03 00 00 00 2E 87
```

## Implementing parser and transport

- `read()` may return one byte, half a frame or several frames. USB packets and API calls do not define protocol boundaries.
- An incremental parser retains a trailing `55` when `AA` arrives later. Validate type, length limit and CRC before delivering payloads.
- Inside a validated frame, `55 AA` is ordinary content. Search again for sync only after rejecting a frame candidate.
- `write()` can also accept only part of a buffer. Continue until the complete frame is sent; handle lack of progress and absolute deadlines.
- Remove events from the response path. Match response/DATA/END by type, command and sequence; stale frames must not complete a new job.
- Treat a flux operation with transfer losses as incomplete. A subsequent valid CRC does not restore missing data.
- Reject or deliberately handle unknown schemas; check minimum lengths before reading fields. Use documented schema/size fields for extensible replies.

The existing [Python parser](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/protocol2.py) is an executable reference for framing and matching. `FrameParser.feed()` accepts arbitrarily split byte chunks. Set deadlines for the expected operation rather than extending them indefinitely after every received byte.

## INFO and key commands

The fixed INFO payload is 18 bytes, followed by optional ASCII text. Parse the binary fields; do not infer versions or capabilities from display text.

| INFO offset | Type | Field |
|---:|---|---|
| 0 | u8 | Protocol version, expected 2 |
| 1, 2 | u8 each | Firmware major/minor |
| 3 | u8 | Board revision: 10 for v1.0, 5/6/7 for v0.x, 0 unknown |
| 4 | u32 | Capability bits |
| 8 | u32 | Native timebase `sample_hz`, currently 275000000 |
| 12 | u32 | Flux storage bytes, not SD capacity |
| 16 | u16 | Maximum payload size |
| 18 | bytes | Optional ASCII display text |

| Code | Function | Starting point |
|---|---|---|
| `00` / `01` / `02` | PING / INFO / STATUS | Communication and state |
| `06` | EVENTS | Set mask; current firmware also supports a query without changing it |
| `08` / `09` / `0C` | WRITE_LIMITS / DESCRIBE / FEATURES | Limits, clocks, transport and capabilities |
| `0A` / `0B` | RESOURCE / JOB | Ownership and optional transfer integrity |
| `0F` | ANALYSIS_JOB with sub-operation | Discovery, masks, generations, analysis and reports |
| `10` / `11` / `12` / `13` / `14` | SELECT / MOTOR / SEEK / RECAL / SIDE | Drive control |
| `20` / `21` / `22` | READ / ABORT / WRITE | Flux operations |
| `27` / `28` | GUARDED_WRITE / GUARDED_READ | Start with configuration and media generations |
| `30` through `37` | IEC family | Commodore bus, sectors and DOS status |
| `40` through `49` | Board/network functions | Power, storage, sensors, LAN and AUTH |
| `50` through `54` / `60` through `64` | Files / standalone | SD files, configuration, dumps, copying and writing |

Codes in this table are hexadecimal; ranges do not mean every number is assigned. Complete payloads are in reference chapter 3. `0x0F` sub-operations are mostly listed in **decimal** there: for example, op 38 is byte `0x26`, not `0x38`.

## Reading a track

After selecting the appropriate drive, starting its motor and recalibrating, send READ. The usual base payload is `<BBBBH>`: `track`, `side`, `revs`, `flags`, `period_ms` (6 bytes). Optional fields follow according to flags. Do not borrow flag bits from WRITE or ANALYZE.

| READ flag | Meaning |
|---|---|
| Bit 0 | No index; use time slices |
| Bit 1 | Hard-sector option; additional sector-count byte |
| Bit 2 | Track byte encodes drive-specific quarter/half-track selection |
| Bit 3 | Provenance block in END |
| Bit 4 | Per-revolution progress events; also enable EVENTS mask bit 5 |
| Bit 5 | Read context in END |
| Bit 6 | Pre-index lead-in; requires a suitable index and PSRAM |
| Bit 7 | Additional trailing `flags2` byte; its bit 0 enables per-READ double stepping on supporting firmware |

Query masks through `0x0F [38]` before using extensions. A successful start returns RESPONSE `cmd=0x20`, followed by DATA and finally END with the same sequence. Acceptance is not capture success. END reports at least the complete revolution count. Current firmware adds the 17-byte generation block; requested provenance (96 bytes) and read context (132 bytes) follow in that order. An active integrity job prefixes its completion proof. See reference sections 2.6, 4.5, 4.12 and 4.24.

**GUARDED_READ:** acceptance replies on `0x28`, but DATA and END still use **`0x20`** with the same sequence. GUARDED_WRITE similarly replies on `0x27`, then streams and ends on **`0x22`**. A parser waiting only for the original command otherwise stalls.

### Decoding flux

| Code | Meaning |
|---|---|
| `01..EF` | Flux interval in native ticks |
| `F0..FC b` | Interval `240 + ((code - 0xF0) << 8) + b` |
| `FD u32` | Index at `last_flux_timestamp + u32`; not a flux transition |
| `FE u32` | u32 flux interval |
| `FF 01 u32` / `FF 02 u32` | Requested lead-in / index 0; only with READ bit 6 |

An index does not change the timestamp of the last flux transition. The next flux interval still starts at that transition, not at the index marker. Codewords can cross DATA-frame boundaries: preserve an incomplete `FD`, `FE` or `FF` code between payloads.

Without lead-in, capture starts at the first index without an initial `FD`. Each completed revolution ends with one `FD`; N successfully delivered revolutions produce N markers, not N+1. Time slices without a physical index source are not measured angular references. Accumulate time in at least 64 bits and convert for display/export using the reported `sample_hz`.

## Writing and reconnecting

Before writing, check capabilities, permitted flag masks, backend limits and write locks. The WRITE request carries flux interval count and **encoded** stream length as separate fields. DATA must not contain READ index markers. Integrity jobs add 16-byte DATA headers, which do not count toward flux bytes. Physical writing does not begin before JOB COMMIT in that workflow.

A lost ACK or END **does not justify blindly retrying**. Writing may already have happened. Query JOB if available, save the outcome and reassess state. There is no persistent exactly-once guarantee after reset. Status 19 means index cut short a track that was already being written; it does not mean nothing was written.

Verification levels are evidence classes: 0 no confirmation, 1 output completed, 2 plausible read-back, 3 sector content verified, 4 unverifiable. A higher number does not automatically mean stronger evidence. See [writing and verification](Writing-and-Verification).

## Events and errors

EVENTS mask bit = event code minus `0x80`. Events are advisory and can be lost. Current firmware reports capacity through EVENTS (currently three events, up to 12 payload bytes each) and cumulative loss counters. Compare before and after; older firmware may return an empty reply or status 16 for query-only requests. Use RESPONSE/END for authoritative operation status.

Common statuses: 1 BUSY, 3 SEEK_FAILED, 4 NO_INDEX/older abort path, 6 DMA/verification, 10 OVERFLOW, 11 UNSUPPORTED, 12 WRITE_PROTECTED, 14 FRAME_ERROR, 16 BAD_ARGS, 18 NOT_REPRESENTABLE, 19 TRACK_LONG, 20 CANCELLED, 21 MEDIA_CHANGED, 22 session write lock, 23 SPEED_MISMATCH. LAN adds 24–26. These are UFI statuses, not operating-system errors.

## Small Python example

After [installing the Python host](Installation-EN), use the [PING/INFO example](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/wiki-tools/examples/ufi_v2_probe.py). It reuses the existing parser and sends no drive or media commands. Its command-line entry point requires an explicit native CDC port and resets that channel before probing.

```console
python docs/wiki-tools/examples/ufi_v2_probe.py --port COM5
```

The script lives in the main repository; substitute your actual port. The core for a custom application is:

```python
from ufi_host.protocol2 import Device2

device = Device2(prepared_byte_stream, timeout=2.0)
if not device.ping(b"my-tool-probe"):
    raise RuntimeError("PING echo mismatch")
info = device.device_info()
print(info.proto, info.sample_hz, info.max_payload)
```

`prepared_byte_stream` means an already opened and prepared transport implementing `read(n)` and `write(data)`; it is not a directly executable placeholder. The complete script handles port checking, preparation and closing. Other languages can independently implement the frame layout, vectors and state sequence.

## Testing your client

Before hardware tests, cover every byte split, multiple frames per read, sync inside payloads, invalid CRC/length, interleaved events, unrelated sequences, rejected requests, error ENDs, truncated flux codes and lost ENDs. Then use test media and recording. Existing [host tests](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/tests/test_protocol2.py) provide a byte-level double; [firmware simulation tests](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tests/test_sim_host.py) add checks against executed firmware code. Neither replaces electrical measurements.

Sources: [protocol source](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md), [firmware dispatcher](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_v2.c), [flux encoder](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/src/ufi_stream.c), [Python host](https://github.com/Axel051171/UniversalFluxInterface/blob/main/software/ufi_host/ufi_host/protocol2.py).
