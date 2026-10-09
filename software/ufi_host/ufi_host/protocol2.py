"""UFI v2 USB protocol (docs/USB_Protokoll.md, section 3).

Every message in both directions is a frame:

    55 AA | type u8 | cmd u8 | seq u8 | status u8 | len u16 | payload | crc u16

crc = CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF) over bytes 2 .. 7+len.  Frames are
independent of USB packet borders; the receiver hunts for 55 AA, checks the CRC and
drops bytes up to the next 55 AA on error.  One request -> exactly one response; READ
and WRITE then exchange DATA frames (type 4) and finish with an END frame (type 5).
Events (type 3) arrive only after EVENTS and never inside a frame.

The first bytes 55 AA switch a UFI-Flux session from v1 to v2 (until channel reset =
10000 baud, or re-enumeration).  `connect` falls back to the v1 `protocol.Device`
when the device does not answer a v2 PING (firmware < 1.12).
"""
from __future__ import annotations

import logging
import struct
import sys
import time
from collections import deque
from collections.abc import Callable, Iterator
from dataclasses import dataclass, field

from . import protocol as P
from .protocol import (
    AMIGA_IDS,
    DRIVES,
    TIMING,
    TIMING_FIELDS,
    Capture,
    DriveStatus,
    Stream,
    decode_stream,
)

log = logging.getLogger(__name__)

SYNC = b"\x55\xAA"
HEADER = struct.Struct("<BBBBH")        # type, cmd, seq, status, len (after SYNC)
FRAME_OVERHEAD = 2 + HEADER.size + 2
MAX_PAYLOAD = 4096
PROTO_VERSION = 2
RESET_BAUD = 10000                      # channel reset (Greaseweazle convention)

# frame types
T_REQUEST, T_RESPONSE, T_EVENT, T_DATA, T_END = 1, 2, 3, 4, 5

# command codes (3.2)
PING, INFO, STATUS, RESET, BOOTLOADER, USB_MODE, EVENTS = 0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06
SELECT, MOTOR, SEEK, RECAL, SIDE, TIMING_CMD, LINES = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16
CHECK_DISK, PROBE_TRACKS, SEEK_TEST, AMIGA_ID = 0x17, 0x18, 0x19, 0x1A
DIAG_RPM, DRIVE_SCAN, INDEX_SIM = 0x1B, 0x1C, 0x1D          # firmware 1.13
SET_RPM = 0x1E                                              # firmware 1.14: 3-mode drives
STEP_SCHEDULE = 0x1F                                        # firmware 1.14: head steps during READ/WRITE
RPM_LINES = {0: "none", 1: "DENSITY (pin 2)", 2: "DRATE (pin 6, JP1)"}
READ, ABORT, WRITE, ERASE, PATTERN = 0x20, 0x21, 0x22, 0x23, 0x24
IEC_RESET, IEC_SEND, IEC_RECV = 0x30, 0x31, 0x32
IEC_MEM, IEC_NIB = 0x33, 0x34           # firmware 1.14: drive memory, 1541 raw GCR chunks
NIB_REUPLOAD, NIB_REVERSE = 0x01, 0x02  # IEC_NIB flags
POWER, USB_POWER, SD_INFO = 0x40, 0x41, 0x42
DIR, READ_FILE, WRITE_FILE, DELETE, CFG_RELOAD = 0x50, 0x51, 0x52, 0x53, 0x54
DUMP_START, DUMP_STATUS, DUMP_ABORT, COPY_START = 0x60, 0x61, 0x62, 0x63
BAD_FRAME_CMD = 0xFF                    # response to a request with CRC/length error

# status codes (3.3)
OK, BUSY, NO_DRIVE, SEEK_FAILED, NO_INDEX, TIMEOUT, DMA_ERROR, USB_ERROR = range(8)
IEC_NO_DEVICE, IEC_NO_ACK, OVERFLOW, UNSUPPORTED, WRITE_PROTECTED, STORAGE = range(8, 14)
FRAME_ERROR, UNKNOWN_COMMAND, BAD_ARGS, NOT_FOUND = range(14, 18)
STATUS_TEXT = {
    1: "busy (dump running / operation active)", 2: "no drive selected / drive missing",
    3: "seek failed (track 0)", 4: "no index pulse / aborted", 5: "timeout",
    6: "DMA / verify error", 7: "USB error", 8: "IEC: no device", 9: "IEC: no acknowledge",
    10: "buffer full / overflow", 11: "not supported (on this board)", 12: "write protected",
    13: "storage (SD NAND / file system) error", 14: "frame error (CRC/length)",
    15: "unknown command", 16: "invalid arguments", 17: "file not found",
}

# INFO caps bits
CAPS = ("PSRAM ok", "SD NAND ready", "IEC", "Amiga", "Amiga2 / JP3 capable", "Shugart bus",
        "Apple port", "sync sensor enabled (apple_sync=1)", "Greaseweazle mode available",
        "file access", "events", "drive diagnostics + index simulation")
CAP_PSRAM, CAP_SD, CAP_IEC, CAP_AMIGA, CAP_AMIGA2, CAP_SHUGART, CAP_APPLE, CAP_SYNC, \
    CAP_GW, CAP_FILES, CAP_EVENTS, CAP_DIAG = (1 << i for i in range(len(CAPS)))

# events (3.5); EVENTS mask bit = code - 0x80
EV_DISK_CHANGED, EV_BUTTON, EV_PROGRESS, EV_POWER_OFF, EV_MODE_SWITCH = 0x80, 0x81, 0x82, 0x83, 0x84
EVENT_NAMES = {EV_DISK_CHANGED: "disk changed", EV_BUTTON: "button",
               EV_PROGRESS: "dump/copy progress", EV_POWER_OFF: "drive supply off (overcurrent)",
               EV_MODE_SWITCH: "mode switch moved"}
EVENTS_ALL = sum(1 << (c - 0x80) for c in EVENT_NAMES)

USB_MODES = {"flux": 0, "sd": 1, "floppy": 2, "gw": 3}
READ_NO_INDEX, WRITE_VERIFY = 0x01, 0x01
HARD_SECTORS = 0x02                     # READ/WRITE flag: sector-hole count follows (1.14)
QUARTER_TRACKS = 0x04                   # READ/WRITE flag: track field = track * 4 + quarter (Apple, 1.14)
FILE_CREATE = 0x01
ATTR_DIR = 0x10
MAX_READ_REVS = 200                     # firmware ring buffer (v1 READ_TRACK)

INFO_HDR = struct.Struct("<BBBBIIIH")   # proto, fw_major, fw_minor, board_rev, caps, sample_hz, store, max_payload
DIAG_RPM_REPLY = struct.Struct("<BIIIIH")   # revs, period min/avg/max, pulse width (ticks), rpm*100
DIAG_TRACK0, DIAG_INDEX, DIAG_WPROT = 1, 2, 4   # DRIVE_SCAN flags
INDEX_SIM_PIN, INDEX_SIM_INTERNAL = 1, 2        # INDEX_SIM mode bits
INDEX_SIM_MODES = {1: "J9 pin 6", 2: "internal", 3: "J9 pin 6 + internal"}


@dataclass
class NibChunk:
    status: int                 # 0 ok, 1 no sync / no bytes, 2 origin not found, 3 raw
    synclen: int                # ~11 us units
    maxlen: int                 # longest non-sync stretch in bytes
    halftrack: int              # drive's position counter
    data: bytes                 # 512 raw GCR bytes (syncs as 0xFF runs)


@dataclass
class RpmResult:
    revolutions: int
    period_min_us: float
    period_avg_us: float
    period_max_us: float
    pulse_us: float
    rpm: float

    def __str__(self) -> str:
        jitter = (self.period_max_us - self.period_min_us) / 2
        return (f"{self.rpm:.2f} rpm  ({self.period_avg_us / 1000:.3f} ms per revolution, "
                f"+/-{jitter:.0f} us over {self.revolutions} revs, index pulse {self.pulse_us:.0f} us)")
BOARD = struct.Struct("<BBHHH")         # power, flags, i5_mA, i12_mA, board_id_mV (v1 0x1B)
STATUS_HDR = struct.Struct("<8B")
DUMP = struct.Struct("<BBBBHBB")
READ_REQ = struct.Struct("<BBBBH")
WRITE_REQ = struct.Struct("<BBBII")
SD = struct.Struct("<BBBBI")
DIR_ENTRY = struct.Struct("<IBB")


# -- framing -------------------------------------------------------------------
def crc16(data: bytes, crc: int = 0xFFFF) -> int:
    """CRC-16/CCITT-FALSE (poly 0x1021, init 0xFFFF, no reflection, no final xor)."""
    for b in data:
        crc ^= b << 8
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if crc & 0x8000 else (crc << 1) & 0xFFFF
    return crc


@dataclass
class Frame:
    type: int
    cmd: int
    seq: int
    status: int = 0
    payload: bytes = b""

    def encode(self) -> bytes:
        return encode_frame(self.type, self.cmd, self.seq, self.status, self.payload)


def encode_frame(ftype: int, cmd: int, seq: int, status: int = 0, payload: bytes = b"") -> bytes:
    if len(payload) > MAX_PAYLOAD:
        raise ValueError(f"payload {len(payload)} > {MAX_PAYLOAD} bytes")
    body = HEADER.pack(ftype, cmd, seq & 0xFF, status, len(payload)) + bytes(payload)
    return SYNC + body + struct.pack("<H", crc16(body))


class FrameParser:
    """Incremental receiver: feed() arbitrary byte chunks, get complete, CRC-checked frames.

    Garbage before 55 AA is skipped; a frame with bad CRC or an impossible length is
    dropped by advancing one byte and hunting for the next 55 AA.
    """

    def __init__(self, max_payload: int = MAX_PAYLOAD):
        self.buf = bytearray()
        self.max_payload = max_payload
        self.crc_errors = 0
        self.skipped = 0

    def feed(self, data: bytes) -> list[Frame]:
        self.buf += data
        frames = []
        while True:
            i = self.buf.find(SYNC)
            if i < 0:
                keep = 1 if self.buf[-1:] == SYNC[:1] else 0     # 55 may start the next sync
                self.skipped += len(self.buf) - keep
                del self.buf[:len(self.buf) - keep]
                return frames
            if i:
                self.skipped += i
                del self.buf[:i]
            if len(self.buf) < 2 + HEADER.size:
                return frames
            ftype, cmd, seq, status, n = HEADER.unpack_from(self.buf, 2)
            if n > self.max_payload or not 1 <= ftype <= 5:
                self.crc_errors += 1
                self.skipped += 1
                del self.buf[:1]
                continue
            total = FRAME_OVERHEAD + n
            if len(self.buf) < total:
                return frames
            (crc,) = struct.unpack_from("<H", self.buf, total - 2)
            if crc != crc16(self.buf[2:total - 2]):
                self.crc_errors += 1
                self.skipped += 1
                del self.buf[:1]
                continue
            frames.append(Frame(ftype, cmd, seq, status, bytes(self.buf[2 + HEADER.size:total - 2])))
            del self.buf[:total]


# -- flux code (3.4) -----------------------------------------------------------
def encode_flux(deltas: list[int]) -> bytes:
    """Write deltas (275 MHz ticks) -> stream code without FD index marks."""
    out = bytearray()
    for d in deltas:
        d = max(1, d)                   # 00 is reserved; two edges cannot coincide
        if d <= 0xEF:
            out.append(d)
        elif d <= 240 + 0xCFF:          # F0..FC b: up to 3567
            d -= 240
            out += bytes((0xF0 + (d >> 8), d & 0xFF))
        else:
            out += b"\xFE" + struct.pack("<I", d)
    return bytes(out)


# -- payload helpers -----------------------------------------------------------
@dataclass
class BoardStatus:
    power: int                          # bit0 FDD_5V, bit1 FDD_12V
    flags: int                          # bit0 WRITE-LOCK, bit1/2 overcurrent 5/12 V, bit3 SD, bit4/5 buttons
    i5_ma: int
    i12_ma: int
    board_id_mv: int

    @classmethod
    def unpack(cls, b: bytes) -> BoardStatus:
        return cls(*BOARD.unpack(b[:BOARD.size]))


@dataclass
class DriveStatus2(DriveStatus):
    board: BoardStatus | None = None


@dataclass
class DeviceInfo:
    proto: int
    fw_major: int
    fw_minor: int
    board_rev: int                      # 5/6/7, 0 = unknown
    caps: int
    sample_hz: int
    store_bytes: int
    max_payload: int
    text: list[str] = field(default_factory=list)

    @classmethod
    def unpack(cls, b: bytes) -> DeviceInfo:
        v = INFO_HDR.unpack_from(b)
        raw = b[INFO_HDR.size:].replace(b"\r", b"").replace(b"\n", b"\0")
        return cls(*v, [s.decode("ascii", "replace") for s in raw.split(b"\0") if s.strip()])

    def caps_list(self) -> list[tuple[str, bool]]:
        return [(name, bool(self.caps & (1 << i))) for i, name in enumerate(CAPS)]


@dataclass
class DumpStatus:
    state: int                          # 6 = done, 7 = error
    error: int                          # 1 storage, 2 drive, 3 full, 4 aborted
    track: int
    side: int
    file_no: int
    fs_result: int
    storage_ready: int

    @classmethod
    def unpack(cls, b: bytes) -> DumpStatus:
        return cls(*DUMP.unpack_from(b))


@dataclass
class DirEntry:
    name: str
    size: int
    attr: int

    @property
    def is_dir(self) -> bool:
        return bool(self.attr & ATTR_DIR)


@dataclass
class Event:
    code: int
    seq: int
    payload: bytes

    @property
    def name(self) -> str:
        return EVENT_NAMES.get(self.code, f"event 0x{self.code:02X}")

    def describe(self) -> str:
        p = self.payload
        if self.code == EV_DISK_CHANGED and p:
            drive = {v: k for k, v in DRIVES.items()}.get(p[0], str(p[0]))
            return f"{self.name}: drive {drive}"
        if self.code == EV_BUTTON and len(p) >= 2:
            return f"{self.name} {'AB'[p[0] & 1]}" + (" long" if p[1] else " short")
        if self.code == EV_PROGRESS and len(p) >= DUMP.size:
            return f"{self.name}: {DumpStatus.unpack(p)}"
        if self.code == EV_POWER_OFF and len(p) >= BOARD.size:
            return f"{self.name}: {BoardStatus.unpack(p)}"
        if self.code == EV_MODE_SWITCH and p:
            mode = {v: k for k, v in USB_MODES.items()}.get(p[0], str(p[0]))
            return f"{self.name}: {mode}"
        return f"{self.name}: {p.hex(' ')}"


def pack_dir(start: int, path: str) -> bytes:
    return struct.pack("<H", start) + _name(path)


def unpack_dir(b: bytes) -> tuple[list[DirEntry], bool]:
    count, more = b[0], b[1]
    entries, i = [], 2
    for _ in range(count):
        size, attr, nlen = DIR_ENTRY.unpack_from(b, i)
        i += DIR_ENTRY.size
        entries.append(DirEntry(b[i:i + nlen].decode("ascii", "replace"), size, attr))
        i += nlen
    return entries, bool(more)


def pack_read_file(offset: int, length: int, name: str) -> bytes:
    return struct.pack("<IH", offset, length) + _name(name)


def pack_write_file(offset: int, name: str, data: bytes, create: bool) -> bytes:
    n = _name(name)
    if len(n) > 255:
        raise ValueError("file name longer than 255 bytes")
    return struct.pack("<IBB", offset, FILE_CREATE if create else 0, len(n)) + n + data


def _name(name: str) -> bytes:
    return name.encode("ascii")


class DeviceError(P.DeviceError):
    """Non-zero v2 status; subclass of the v1 error so callers catch both."""

    def __init__(self, command: int, status: int):
        self.command, self.status = command, status
        RuntimeError.__init__(self, f"command 0x{command:02X}: {STATUS_TEXT.get(status, f'error {status}')}")


# -- device --------------------------------------------------------------------
class Device2:
    """UFI v2 command layer over any byte stream (pyserial port or a test double)."""

    proto = 2

    def __init__(self, stream: Stream, timeout: float = 5.0):
        self.s = stream
        self.timeout = timeout
        self.parser = FrameParser()
        self.frames: deque[Frame] = deque()
        self.events: deque[Event] = deque()
        self.max_payload = MAX_PAYLOAD
        self._seq = 0

    # -- framing ------------------------------------------------------------
    def _next_seq(self) -> int:
        self._seq = self._seq % 255 + 1             # 1..255; seq 0 marks frame-error replies
        return self._seq

    def _read_chunk(self) -> bytes:
        # pyserial: read what is buffered, else block (port timeout) for the first byte only
        waiting = getattr(self.s, "in_waiting", None)
        if waiting is None:
            return self.s.read(FRAME_OVERHEAD + self.max_payload)
        return self.s.read(waiting or 1)

    def send(self, ftype: int, cmd: int, seq: int, payload: bytes = b"") -> None:
        self.s.write(encode_frame(ftype, cmd, seq, 0, payload))

    def read_frame(self, timeout: float | None = None) -> Frame:
        """Next non-event frame; events are queued in self.events."""
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while True:
            while self.frames:
                f = self.frames.popleft()
                if f.type == T_EVENT:
                    self.events.append(Event(f.cmd, f.seq, f.payload))
                else:
                    return f
            chunk = self._read_chunk()
            if chunk:
                self.frames.extend(self.parser.feed(chunk))
            elif time.monotonic() > deadline:
                raise TimeoutError("no v2 frame from device")

    def _wait(self, cmd: int, seq: int, types: tuple[int, ...], timeout: float | None) -> Frame:
        while True:
            f = self.read_frame(timeout)
            if f.type == T_RESPONSE and f.cmd == BAD_FRAME_CMD and f.seq == 0:
                raise DeviceError(cmd, f.status or FRAME_ERROR)
            if f.seq == seq and f.cmd == cmd and f.type in types:
                return f
            log.debug("stale frame dropped: %s", f)

    def request(self, cmd: int, payload: bytes = b"", timeout: float | None = None) -> bytes:
        seq = self._next_seq()
        self.send(T_REQUEST, cmd, seq, payload)
        f = self._wait(cmd, seq, (T_RESPONSE,), timeout)
        if f.status:
            raise DeviceError(cmd, f.status)
        return f.payload

    # -- system -------------------------------------------------------------
    def ping(self, data: bytes = b"UFI2", timeout: float | None = None) -> bool:
        return self.request(PING, data, timeout) == data

    def device_info(self) -> DeviceInfo:
        info = DeviceInfo.unpack(self.request(INFO))
        if info.max_payload:
            self.max_payload = min(info.max_payload, MAX_PAYLOAD)
        return info

    def info(self) -> list[str]:
        i = self.device_info()
        rev = f"v0.{i.board_rev}" if i.board_rev else "unknown"
        return [f"UFI v{i.proto} protocol, firmware {i.fw_major}.{i.fw_minor}, board {rev}",
                *i.text]

    def caps(self) -> int:
        return self.device_info().caps

    def status(self) -> DriveStatus2:
        b = self.request(STATUS)
        v = STATUS_HDR.unpack_from(b)
        board = BoardStatus.unpack(b[8:]) if len(b) >= 8 + BOARD.size else None
        return DriveStatus2(v[0], *(bool(x) for x in v[1:6]), v[6], v[7], 0, board)

    def reset(self) -> None:
        self.request(RESET)

    def bootloader(self) -> None:
        self.request(BOOTLOADER)

    def usb_mode(self, mode: int | str) -> None:
        self.request(USB_MODE, bytes([USB_MODES[mode] if isinstance(mode, str) else mode]))

    def enable_events(self, mask: int = EVENTS_ALL) -> None:
        self.request(EVENTS, struct.pack("<I", mask))

    def poll_event(self, timeout: float = 0.0) -> Event | None:
        """Return a queued or newly received event, None after `timeout` seconds."""
        deadline = time.monotonic() + timeout
        # events that arrived in the same read as a response are parked in self.frames
        for f in [f for f in self.frames if f.type == T_EVENT]:
            self.frames.remove(f)
            self.events.append(Event(f.cmd, f.seq, f.payload))
        while not self.events:
            chunk = self._read_chunk()
            if chunk:
                for f in self.parser.feed(chunk):
                    if f.type == T_EVENT:
                        self.events.append(Event(f.cmd, f.seq, f.payload))
                    else:
                        log.debug("unsolicited frame dropped: %s", f)
            elif time.monotonic() >= deadline:
                return None
        return self.events.popleft()

    def iter_events(self, poll: float = 0.5) -> Iterator[Event]:
        while True:
            ev = self.poll_event(poll)
            if ev is not None:
                yield ev

    # -- drive --------------------------------------------------------------
    def select(self, drive: str | int) -> None:
        self.request(SELECT, bytes([DRIVES[drive] if isinstance(drive, str) else drive]))

    def motor(self, on: bool) -> None:
        self.request(MOTOR, bytes([int(on)]), timeout=3.0)

    def seek(self, track: int, quarter: int = 0) -> None:
        """quarter 1-3: Apple Disk II quarter-track offset (firmware 1.14)."""
        if quarter:
            self.request(SEEK, bytes([track, quarter]), timeout=5.0)
            return
        self.request(SEEK, bytes([track]), timeout=3.0)

    def recalibrate(self) -> None:
        self.request(RECAL, timeout=3.0)

    def side(self, side: int) -> None:
        self.request(SIDE, bytes([side]))

    def timing(self, **changes: int) -> dict[str, int]:
        """Read drive timings; keyword arguments (TIMING_FIELDS) change them."""
        cur = dict(zip(TIMING_FIELDS, TIMING.unpack(self.request(TIMING_CMD)[:TIMING.size])))
        if changes:
            unknown = set(changes) - set(TIMING_FIELDS)
            if unknown:
                raise ValueError(f"unknown timing field(s): {', '.join(sorted(unknown))}")
            cur.update(changes)
            new = self.request(TIMING_CMD, TIMING.pack(*(cur[f] for f in TIMING_FIELDS)))
            cur = dict(zip(TIMING_FIELDS, TIMING.unpack(new[:TIMING.size])))
        return cur

    def lines(self, density: int, drate: int) -> None:
        self.request(LINES, bytes([density, drate]))

    def check_disk(self) -> tuple[bool, bool]:
        """-> (disk changed since last check, disk present)."""
        changed, present = self.request(CHECK_DISK, timeout=3.0)[:2]
        return bool(changed), bool(present)

    def probe_tracks(self) -> int:
        return self.request(PROBE_TRACKS, timeout=10.0)[0]

    def seek_test(self, a: int, b: int, cycles: int) -> None:
        self.request(SEEK_TEST, bytes([a, b, cycles]), timeout=5.0 + cycles)

    def amiga_id(self) -> tuple[int, str]:
        (v,) = struct.unpack("<I", self.request(AMIGA_ID)[:4])
        return v, AMIGA_IDS.get(v, "unknown")

    # -- diagnostics (firmware 1.13) ------------------------------------------
    def diag_rpm(self, revolutions: int = 5) -> RpmResult:
        """Revolution time of the selected drive from its index pulses (motor is started)."""
        p = self.request(DIAG_RPM, bytes([max(1, min(revolutions, 50))]), timeout=3.0 + revolutions)
        revs, pmin, pavg, pmax, pulse, rpm100 = DIAG_RPM_REPLY.unpack_from(p)
        us = 1e6 / P.FLUX_CLOCK_HZ
        return RpmResult(revs, pmin * us, pavg * us, pmax * us, pulse * us, rpm100 / 100)

    def drive_scan(self, shugart_bus: bool = False) -> list[tuple[str, int]]:
        """Probe a/b/amiga/amiga2 (or ds0-ds3): [(drive, DIAG_* flags)].  Moves the heads."""
        p = self.request(DRIVE_SCAN, bytes([1 if shugart_bus else 0]), timeout=20.0)
        names = {v: k for k, v in P.DRIVES.items()}
        return [(names.get(p[i], str(p[i])), p[i + 1]) for i in range(0, len(p) - 1, 2)]

    def index_sim(self, rpm: int | None = None, pulse_us: int = 0,
                  mode: int = INDEX_SIM_PIN) -> tuple[int, int]:
        """Index simulation: rpm 300/360 on, 0 off, None = query.  mode: INDEX_SIM_PIN (J9 pin 6)
        and/or INDEX_SIM_INTERNAL (replaces the drive's INDEX line).  -> (rpm, mode)."""
        payload = b"" if rpm is None else struct.pack("<HHB", rpm, pulse_us, mode)
        rpm_now, mode_now = struct.unpack("<HB", self.request(INDEX_SIM, payload)[:3])
        return rpm_now, mode_now

    # -- IEC drive memory / 1541 raw reads (firmware 1.14) ------------------------
    def iec_mem_write(self, addr: int, data: bytes) -> None:
        """M-W: up to 32 bytes into the drive's RAM."""
        self.request(IEC_MEM, bytes([0]) + struct.pack("<H", addr) + bytes(data), timeout=5.0)

    def iec_mem_read(self, addr: int, n: int) -> bytes:
        """M-R: n (1-255) bytes from the drive's RAM."""
        return self.request(IEC_MEM, bytes([1]) + struct.pack("<H", addr) + bytes([n]), timeout=5.0)[:n]

    def iec_mem_exec(self, addr: int) -> None:
        self.request(IEC_MEM, bytes([2]) + struct.pack("<H", addr), timeout=5.0)

    def iec_command(self, cmd: bytes | str) -> None:
        """DOS command on channel 15, e.g. b'I0'."""
        if isinstance(cmd, str):
            cmd = cmd.encode("ascii")
        self.request(IEC_MEM, bytes([3, 0, 0]) + cmd, timeout=5.0)

    def iec_nib(self, halftrack: int, sync: int = 0, mode: int = 0, flags: int = 0,
                device: int = 0) -> NibChunk:
        """One 512-byte raw GCR chunk from the 1541 drive code: mode 0 at sync index `sync`
        after the track origin, 1 raw, 2 step only.  flags: NIB_REUPLOAD, NIB_REVERSE."""
        p = self.request(IEC_NIB, bytes([halftrack, sync, mode, flags, device]), timeout=12.0)
        status, synclen, maxlen, ht = struct.unpack_from("<BBHB", p)
        return NibChunk(status, synclen, maxlen, ht, bytes(p[5:5 + 512]))

    def step_schedule(self, steps: list[tuple[int, int]], quarter: bool = False) -> None:
        """Head steps during the next READ/WRITE (firmware 1.14): [(ms after the start index,
        position)], position in tracks or, with quarter=True, in quarter tracks (track*4+q).
        One-shot; up to 32 entries in time order."""
        if len(steps) > 32:
            raise ValueError("at most 32 steps")
        body = b"".join(struct.pack("<HB", ms, pos) for ms, pos in steps)
        self.request(STEP_SCHEDULE, bytes([1 if quarter else 0, len(steps)]) + body)

    def set_rpm(self, rpm: int | None = None) -> tuple[int, int]:
        """3-mode drive speed select (firmware 1.14): 300/360, None = query.  -> (rpm, line);
        line per UFI.CFG rpm_line (0 = none: setting raises UNSUPPORTED)."""
        payload = b"" if rpm is None else struct.pack("<H", rpm)
        rpm_now, line = struct.unpack("<HB", self.request(SET_RPM, payload, timeout=3.0)[:3])
        return rpm_now, line

    # -- flux ---------------------------------------------------------------
    def read_track(self, track: int, side: int, revolutions: int = 3,
                   no_index: bool = False, period_ms: int = 0, hard_sectors: int = 0,
                   quarter: int = 0) -> Capture:
        """Streamed capture: DATA frames while the disk turns, END frame with the revolutions.
        hard_sectors: sector holes of a hard-sectored disk (10/16/32); only the index hole counts.
        quarter: Apple Disk II quarter-track offset 0-3 (track 17 + 2 = track 17.5)."""
        revolutions = max(1, min(revolutions, MAX_READ_REVS))
        flags = (READ_NO_INDEX if no_index else 0) | (HARD_SECTORS if hard_sectors else 0)
        if quarter:
            flags |= QUARTER_TRACKS
            track = track * 4 + quarter
        seq = self._next_seq()
        self.send(T_REQUEST, READ, seq, READ_REQ.pack(track, side, revolutions, flags, period_ms)
                  + (bytes([hard_sectors]) if hard_sectors else b""))
        f = self._wait(READ, seq, (T_RESPONSE,), 5.0)
        if f.status:
            raise DeviceError(READ, f.status)
        data = bytearray()
        while True:
            f = self._wait(READ, seq, (T_DATA, T_END), 2.0 + 0.4 * revolutions)
            if f.type == T_DATA:
                data += f.payload
                continue
            if f.status and f.status != OVERFLOW:
                raise DeviceError(READ, f.status)
            rflags = 0x01 | (0x02 if f.status == OVERFLOW else 0)
            revs = decode_stream(bytes(data), track, side, rflags)
            if f.payload:
                revs = revs[:f.payload[0]]
            return Capture(revs, f.status)

    def write_track(self, track: int, side: int, deltas: list[int], verify: bool = False,
                    hard_sectors: int = 0, quarter: int = 0) -> None:
        """deltas: flux intervals in 275 MHz ticks (first one measured from the index)."""
        code = encode_flux(deltas)
        seq = self._next_seq()
        flags = (WRITE_VERIFY if verify else 0) | (HARD_SECTORS if hard_sectors else 0)
        if quarter:
            flags |= QUARTER_TRACKS
            track = track * 4 + quarter
        self.send(T_REQUEST, WRITE, seq, WRITE_REQ.pack(track, side, flags, len(deltas), len(code))
                  + (bytes([hard_sectors]) if hard_sectors else b""))
        f = self._wait(WRITE, seq, (T_RESPONSE,), 5.0)
        if f.status:
            raise DeviceError(WRITE, f.status)
        for off in range(0, len(code), self.max_payload):
            self.send(T_DATA, WRITE, seq, code[off:off + self.max_payload])
        f = self._wait(WRITE, seq, (T_END,), 10.0)
        if f.status:
            raise DeviceError(WRITE, f.status)

    def erase(self, track: int, side: int) -> None:
        self.request(ERASE, bytes([track, side]), timeout=3.0)

    def pattern(self, track: int, side: int, interval_ns: int, duration_ms: int) -> None:
        self.request(PATTERN, struct.pack("<BBHH", track, side, interval_ns, duration_ms),
                     timeout=3.0 + duration_ms / 1000)

    def abort(self) -> None:
        self.request(ABORT)

    def rpm(self) -> int:
        """Spindle speed from one revolution on the current track (motor must run)."""
        st = self.status()
        cap = self.read_track(st.track, st.side, 1)
        return round(cap.revolutions[0].rpm) if cap.revolutions else 0

    def selftest(self) -> int:
        raise DeviceError(0xD0, UNSUPPORTED)    # v1 debug command; v2 reports PSRAM in INFO

    # -- IEC ----------------------------------------------------------------
    def iec_reset(self) -> None:
        self.request(IEC_RESET, timeout=3.0)

    def iec_send(self, byte: int, eoi: bool = False) -> None:
        self.request(IEC_SEND, bytes([byte, int(eoi)]))

    def iec_receive(self) -> tuple[int, bool]:
        b, eoi = self.request(IEC_RECV, timeout=1.0)[:2]
        return b, bool(eoi)

    # -- board --------------------------------------------------------------
    def power(self, mask: int | None = None) -> BoardStatus:
        """Board status; `mask` (bit0 FDD_5V, bit1 FDD_12V) switches the drive supplies."""
        return BoardStatus.unpack(self.request(POWER, b"" if mask is None else bytes([mask])))

    board_status = power

    def usb_power(self) -> tuple[int, int, int]:
        return struct.unpack("<3H", self.request(USB_POWER)[:6])

    def sd_info(self) -> tuple[int, int, int, int, int]:
        """-> (present, status, card_type, bus_width, capacity_mb)."""
        return SD.unpack_from(self.request(SD_INFO))

    # -- files --------------------------------------------------------------
    def list_dir(self, path: str = "") -> list[DirEntry]:
        out: list[DirEntry] = []
        while True:
            entries, more = unpack_dir(self.request(DIR, pack_dir(len(out), path)))
            out += entries
            if not more or not entries:
                return out

    def read_file(self, name: str, progress: Callable[[int], None] | None = None) -> bytes:
        data = bytearray()
        while True:
            n = self.max_payload
            chunk = self.request(READ_FILE, pack_read_file(len(data), n, name))
            data += chunk
            if progress:
                progress(len(data))
            if len(chunk) < n:
                return bytes(data)

    def write_file(self, name: str, data: bytes, progress: Callable[[int], None] | None = None) -> None:
        step = self.max_payload - 6 - len(_name(name))
        off = 0
        while True:
            chunk = data[off:off + step]
            (written,) = struct.unpack("<H", self.request(
                WRITE_FILE, pack_write_file(off, name, chunk, create=off == 0), timeout=10.0)[:2])
            if chunk and written == 0:
                raise DeviceError(WRITE_FILE, STORAGE)
            off += written
            if progress:
                progress(off)
            if off >= len(data):
                return

    def delete(self, name: str) -> None:
        self.request(DELETE, _name(name))

    def cfg_reload(self) -> None:
        self.request(CFG_RELOAD)

    # -- standalone ---------------------------------------------------------
    def dump_start(self, drive: str | int | None = None, tracks: int = 80, sides: int = 2,
                   revs: int = 3) -> None:
        if drive is None:
            self.request(DUMP_START)
        else:
            d = DRIVES[drive] if isinstance(drive, str) else drive
            self.request(DUMP_START, bytes([d, tracks, sides, revs]))

    def dump_status(self) -> DumpStatus:
        return DumpStatus.unpack(self.request(DUMP_STATUS))

    def dump_abort(self) -> None:
        self.request(DUMP_ABORT)

    def copy_start(self) -> None:
        self.request(COPY_START)


# -- connection ----------------------------------------------------------------
def _hint(msg: str) -> None:
    print(msg, file=sys.stderr)


def channel_reset(port) -> None:
    """10000 baud = reset the CDC channel: v2 session ends, half frames are dropped."""
    if hasattr(port, "baudrate"):
        old = port.baudrate if port.baudrate != RESET_BAUD else 115200
        port.baudrate = RESET_BAUD
        port.baudrate = old
    if hasattr(port, "reset_input_buffer"):
        port.reset_input_buffer()


def connect_stream(stream: Stream, timeout: float = 5.0, ping_timeout: float = 0.5,
                   notify: Callable[[str], None] = _hint) -> Device2 | P.Device:
    """Talk v2 if the device answers PING, else fall back to the v1 protocol."""
    dev = Device2(stream, timeout)
    try:
        if dev.ping(timeout=ping_timeout):
            try:
                dev.device_info()           # learns max_payload
            except (P.DeviceError, TimeoutError):
                pass
            return dev
    except (P.DeviceError, TimeoutError):
        pass
    # v1 firmware answered 0x55 as an unknown command (or not at all): drop that
    if hasattr(stream, "reset_input_buffer"):
        stream.reset_input_buffer()
    else:
        while stream.read(4096):
            pass
    notify("note: device does not speak UFI v2 (old firmware) - using v1, "
           "please update the firmware (ufi bootloader)")
    return P.Device(stream, timeout)


def open_serial(port: str | None = None):
    return P.open_serial(port)


def connect(port: str | None = None, timeout: float = 5.0) -> Device2 | P.Device:
    ser = open_serial(port)
    channel_reset(ser)
    return connect_stream(ser, timeout)
