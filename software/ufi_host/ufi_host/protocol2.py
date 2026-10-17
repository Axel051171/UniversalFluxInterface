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
import secrets
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
    _timing_unpack,
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
SELFTEST = 0x07                         # firmware 1.14: board self-test
WRITE_LIMITS = 0x08                     # firmware 1.15: effective write backend limits
DESCRIBE = 0x09                         # firmware 1.18: clocks, transports and limits
RESOURCE = 0x0A
JOB = 0x0B
GUARDED_WRITE = 0x27
FEATURES = 0x0C
DIAGNOSTICS = 0x0D
ANALYSIS_JOB = 0x0F                     # firmware 1.18: sub-operations (op u8 first), 4.10-4.23
DESCRIBE_FMT = struct.Struct("<BBHIIIIHHIII")
WRITE_LIMITS_FMT = struct.Struct("<BBIIII")
SELFTEST_ITEMS = {
    1: ("timer", "TIM2 ticks per ms (275000 expected)"),
    2: ("usb_sof", "crystal vs. USB frames, ppm (skip = not enumerated)"),
    3: ("psram", "flux store bytes; fresh pattern test (fail value = offset)"),
    4: ("sd", "SD NAND capacity MB"),
    5: ("rail_5v", "FDD 5 V current mA (fail = overcurrent trip, skip = off)"),
    6: ("rail_12v", "FDD 12 V current mA"),
    7: ("board_id", "BOARD_ID divider mV (warn = no known revision)"),
    8: ("usbc_cc", "USB-C advertised mA (warn = no CC, USB-A cable)"),
    9: ("fdd_lines", "drive inputs low with nothing selected: bit0 INDEX bit1 TRK0 bit2 WPROT bit3 DSKCHG bit4 READY bit5 RDATA"),
    10: ("iec_loop", "IEC transceivers: bit0-3 ATN/CLK/DATA/SRQ not asserted, bit4-7 not released"),
    11: ("index_loop", "index simulation J9 pin 6 -> INDEX (skip = no wire)"),
    12: ("last_reset", "fault record at start-up (1 HardFault 2 MemManage 3 BusFault 4 UsageFault)"),
    13: ("stack_free", "stack bytes never used since start-up (warn < 2 KB, fail < 512)"),
    14: ("log", "SELFTEST.LOG written (bytes)"),
}
SELFTEST_STATUS = {0: "ok", 1: "FAIL", 2: "skip", 3: "warn"}
SELECT, MOTOR, SEEK, RECAL, SIDE, TIMING_CMD, LINES = 0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16
CHECK_DISK, PROBE_TRACKS, SEEK_TEST, AMIGA_ID = 0x17, 0x18, 0x19, 0x1A
DIAG_RPM, DRIVE_SCAN, INDEX_SIM = 0x1B, 0x1C, 0x1D          # firmware 1.13
SET_RPM = 0x1E                                              # firmware 1.14: 3-mode drives
STEP_SCHEDULE = 0x1F                                        # firmware 1.14: head steps during READ/WRITE
RPM_LINES = {0: "none", 1: "DENSITY (pin 2)", 2: "DRATE (pin 6, JP1)"}
READ, ABORT, WRITE, ERASE, PATTERN = 0x20, 0x21, 0x22, 0x23, 0x24
ANALYZE = 0x25                          # firmware 1.14: track analysis
FORMAT = 0x26                           # firmware 1.14: the firmware encodes and writes a track
FORMAT_REQ = struct.Struct("<BBBHBBB")  # track, side, flags, rate_kbps, spt, gap3, fill
FORMAT_VERIFY, FORMAT_AMIGA, FORMAT_HEAD_SWAP = 0x01, 0x02, 0x04
ANALYZE_FMT = struct.Struct("<BBHH4HIIIBBIQ")
ENCODINGS = {0: "unknown", 1: "FM", 2: "MFM", 3: "GCR"}
IEC_RESET, IEC_SEND, IEC_RECV = 0x30, 0x31, 0x32
IEC_MEM, IEC_NIB = 0x33, 0x34           # firmware 1.14: drive memory, 1541 raw GCR chunks
IEC_BLOCK = 0x35                        # firmware 1.16: U1/U2 block read/write through the drive's DOS
IEC_TARGET, IEC_READ_STATUS = 0x36, 0x37 # firmware 1.18, capability bit 15
NIB_REUPLOAD, NIB_REVERSE = 0x01, 0x02  # IEC_NIB flags
POWER, USB_POWER, SD_INFO = 0x40, 0x41, 0x42
DIR, READ_FILE, WRITE_FILE, DELETE, CFG_RELOAD = 0x50, 0x51, 0x52, 0x53, 0x54
DUMP_START, DUMP_STATUS, DUMP_ABORT, COPY_START = 0x60, 0x61, 0x62, 0x63
WRITE_START = 0x64                      # firmware 1.16: write an image from the SD NAND onto the disk
DUMP_FORMATS = {"scp": 0, "img": 1, "adf": 2, "hfe": 3}   # DUMP_START byte 5 (firmware 1.16; hfe 1.17)
BAD_FRAME_CMD = 0xFF                    # response to a request with CRC/length error

# status codes (3.3)
OK, BUSY, NO_DRIVE, SEEK_FAILED, NO_INDEX, TIMEOUT, DMA_ERROR, USB_ERROR = range(8)
IEC_NO_DEVICE, IEC_NO_ACK, OVERFLOW, UNSUPPORTED, WRITE_PROTECTED, STORAGE = range(8, 14)
FRAME_ERROR, UNKNOWN_COMMAND, BAD_ARGS, NOT_FOUND = range(14, 18)
NOT_REPRESENTABLE, TRACK_LONG, CANCELLED, MEDIA_CHANGED = range(18, 22)   # 1.15 / 1.15 / 1.18 / 1.18
SOFT_WRITE_LOCK = 22                                                        # 1.18: session write lock (op 37)
STATUS_TEXT = {
    1: "busy (dump running / operation active)", 2: "no drive selected / drive missing",
    3: "seek failed (track 0)", 4: "no index pulse / aborted", 5: "timeout",
    6: "DMA / verify error", 7: "USB error", 8: "IEC: no device", 9: "IEC: no acknowledge",
    10: "buffer full / overflow", 11: "not supported (on this board)", 12: "write protected",
    13: "storage (SD NAND / file system) error", 14: "frame error (CRC/length)",
    15: "unknown command", 16: "invalid arguments", 17: "file not found",
    18: "flux interval not representable (1.6-238 us); write with clamp=True to force",
    19: "track longer than one revolution: the index ended the write",
    20: "operation cancelled",
    21: "media changed; prepare the operation again",
    22: "session write lock engaged (op 37); only an MCU reset clears it",
}

# INFO caps bits
CAPS = ("PSRAM ok", "SD NAND ready", "IEC", "Amiga", "Amiga2 / JP3 capable", "Shugart bus",
        "Apple port", "sync sensor enabled (apple_sync=1)", "Greaseweazle mode available",
        "file access", "events", "drive diagnostics + index simulation",
        "FORMAT (firmware encodes tracks)", "SELFTEST", "WRITE_LIMITS query", "addressed IEC + DOS read status", "DESCRIBE query", "integrity jobs", "resource ownership", "feature state query", "runtime diagnostics")
CAP_PSRAM, CAP_SD, CAP_IEC, CAP_AMIGA, CAP_AMIGA2, CAP_SHUGART, CAP_APPLE, CAP_SYNC, \
    CAP_GW, CAP_FILES, CAP_EVENTS, CAP_DIAG, CAP_FORMAT, CAP_SELFTEST, CAP_WRITE_LIMITS, CAP_IEC_ADDRESSED, CAP_DESCRIBE, CAP_JOBS, CAP_RESOURCES, CAP_FEATURES, CAP_DIAGNOSTICS = \
    (1 << i for i in range(len(CAPS)))

# events (3.5); EVENTS mask bit = code - 0x80
EV_DISK_CHANGED, EV_BUTTON, EV_PROGRESS, EV_POWER_OFF, EV_MODE_SWITCH = 0x80, 0x81, 0x82, 0x83, 0x84
EVENT_NAMES = {EV_DISK_CHANGED: "disk changed", EV_BUTTON: "button",
               EV_PROGRESS: "dump/copy progress", EV_POWER_OFF: "drive supply off (overcurrent)",
               EV_MODE_SWITCH: "mode switch moved"}
EVENTS_ALL = sum(1 << (c - 0x80) for c in EVENT_NAMES)

USB_MODES = {"flux": 0, "sd": 1, "floppy": 2, "gw": 3, "composite": 4}
READ_NO_INDEX, WRITE_VERIFY = 0x01, 0x01
HARD_SECTORS = 0x02                     # READ/WRITE flag: sector-hole count follows (1.14)
QUARTER_TRACKS = 0x04                   # READ/WRITE flag: track field = track * 4 + quarter (Apple, 1.14)
TIME_SCALE = 0x08                       # WRITE flag: scale u16 in 1/1000 follows (1.14)
WRITE_CLAMP = 0x10                      # WRITE flag: clamp out-of-range periods instead of status 18 (1.15)
VERIFY_LEVELS = {0: "none", 1: "write completed", 2: "read-back plausible (count only)",
                 3: "content verified", 4: "unverifiable"}
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
    flags: int                          # bit0 WRITE-LOCK jumper, bit1/2 overcurrent 5/12 V, bit3 SD, bit4/5 buttons, bit6 safe mode, bit7 session lock (1.18)
    i5_ma: int
    i12_ma: int
    board_id_mv: int

    @property
    def session_locked(self) -> bool:
        """Session write lock engaged (op 37, firmware 1.18); cleared only by an MCU reset."""
        return bool(self.flags & 0x80)

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
class WriteLimits:
    """WRITE_LIMITS reply (firmware 1.15): what the write backend accepts."""
    schema: int
    flags: int                  # bit0 content verify, bit1 long tracks stream, bit2 clamp flag
    sample_hz: int
    max_flux: int               # flux_count limit (flux store)
    min_ticks: int
    max_ticks: int

    @classmethod
    def unpack(cls, b: bytes) -> "WriteLimits":
        return cls(*WRITE_LIMITS_FMT.unpack_from(b))


# ANALYSIS_JOB op 10 (additional drive times, 4.11) and op 18 (operating profile, 4.15)
EXTRA_FIELDS = ("density_us", "motor_step_ms", "write_settle_us", "gate_lead_us", "gate_tail_us", "reserved")
EXTRA = struct.Struct("<6H")
PROFILE_FMT = struct.Struct("<B22s12sBBHB")     # schema, timing, extra, rpm_line, rpm_360_low, rpm, stored
TIMING_PRESETS = {"standard": 1, "konservativ": 2}     # op 18 with [18, id] (firmware 1.18)

# ANALYSIS_JOB op 33: Apple II 5.25-inch GCR decoder options (format 7, 5.7)
APPLE_GCR_FIELDS = ("addr_prologue", "data_prologue", "addr_epilogue", "data_epilogue", "flags")
APPLE_GCR_FLAGS = {"keep_bad_addr_checksum": 0x01, "ignore_epilogues": 0x02, "any_track": 0x04,
                   "data_without_checksum": 0x08, "data_without_header": 0x10, "five_and_three": 0x20}
APPLE_STD_TABLE = bytes([
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff])
APPLE_GCR_SECTOR_FLAGS = {0x20: "addr checksum", 0x40: "data checksum", 0x80: "epilogue"}
# ANALYSIS_JOB op 34: tolerance flags of the IBM MFM/FM, HP, Amiga, Commodore and Macintosh analysis
# decoders (5.8); same bit values as the Apple flags.  SECTOR_FLAGS names the quality report's
# per-sector flag byte for every format.
TOLERANCE_FLAGS = {"keep_bad_header": 0x01, "ignore_epilogues": 0x02, "any_track": 0x04,
                   "data_without_checksum": 0x08, "data_without_header": 0x10}
SECTOR_FLAGS = {0x01: "conflicting content", 0x02: "deleted data mark", 0x04: "HP defect mark",
                0x08: "no header", 0x10: "coding error", 0x20: "header checksum", 0x40: "data checksum",
                0x80: "epilogue"}
TOLERATED_BITS = 0xE8          # a sector counted as read with one of these was accepted by tolerance only


def flag_names(value: int, table: dict) -> list[str]:
    """Names of the set bits; `table` maps name -> bit (TOLERANCE_FLAGS) or bit -> name (SECTOR_FLAGS)."""
    if table and isinstance(next(iter(table)), int):
        return [n for v, n in table.items() if value & v]
    return [n for n, v in table.items() if value & v]


def parse_flags(spec, table: dict) -> int:
    """'a+b' names, an int, or a numeric string -> flag value; unknown names raise ValueError."""
    if isinstance(spec, int):
        return spec
    spec = spec.strip()
    if not spec or spec in ("0", "-", "none"):
        return 0
    try:
        return int(spec, 0)
    except ValueError:
        pass
    names = [n.strip() for n in spec.split("+") if n.strip()]
    unknown = [n for n in names if n not in table]
    if unknown:
        raise ValueError(f"unknown flag(s): {', '.join(unknown)}; known: {', '.join(table)}")
    return sum(table[n] for n in names)


def _apple_unpack(b: bytes) -> dict:
    if len(b) < 13 or b[0] != 1:
        raise DeviceError("Apple GCR options: unexpected reply")
    flags = b[11] | b[12] << 8
    return {"addr_prologue": b[1:4].hex(), "data_prologue": b[4:7].hex(), "addr_epilogue": b[7:9].hex(),
            "data_epilogue": b[9:11].hex(), "flags": flags,
            "flag_names": [n for n, v in APPLE_GCR_FLAGS.items() if flags & v]}


def _apple_pack(cur: dict) -> bytes:
    flags = cur["flags"]
    if isinstance(flags, str):
        flags = sum(APPLE_GCR_FLAGS[n.strip()] for n in flags.split("+") if n.strip()) if flags else 0
    marks = b"".join(bytes.fromhex(cur[f]) for f in APPLE_GCR_FIELDS[:4])
    if len(marks) != 10:
        raise ValueError("Apple GCR marks: prologues are 3 bytes, epilogues 2 bytes (hex)")
    return marks + struct.pack("<H", int(flags))


@dataclass
class OperatingProfile:
    """Effective drive settings (ANALYSIS_JOB op 18, firmware 1.18): DRIVE_TIMING fields,
    the additional drive times of op 10, the RPM line and whether a per-drive profile is
    stored.  `pack()` yields the 40-byte request body for storing (stored byte = 0)."""
    timing: dict[str, int]
    extra: dict[str, int]
    rpm_line: int               # 0 none, 1 DENSITY, 2 DRATE
    rpm_360_low: int
    rpm: int                    # 0 = not set, else 300/360
    stored: bool

    @classmethod
    def unpack(cls, b: bytes) -> "OperatingProfile":
        schema, t, e, line, low, rpm, stored = PROFILE_FMT.unpack_from(b)
        if schema != 1:
            raise DeviceError(f"operating profile schema {schema} unsupported")
        return cls(dict(zip(TIMING_FIELDS, _timing_unpack(t), strict=True)), dict(zip(EXTRA_FIELDS, EXTRA.unpack(e), strict=True)),
                   line, low, rpm, bool(stored))

    def pack(self) -> bytes:
        return PROFILE_FMT.pack(1, TIMING.pack(*(self.timing[f] for f in TIMING_FIELDS)),
                                EXTRA.pack(*(self.extra[f] for f in EXTRA_FIELDS)),
                                self.rpm_line, self.rpm_360_low, self.rpm, 0)


GEN_BLOCK = struct.Struct("<BIIII")     # END generation block (4.5, 1.18): schema 1, config, media start, media end, capture id


def generation_block(payload: bytes, offset: int) -> dict | None:
    """Decode the generation block firmware 1.18 appends to READ/WRITE END payloads; None
    when the frame is too short (older firmware) or carries an unknown schema."""
    if len(payload) < offset + GEN_BLOCK.size:
        return None
    schema, config, start, end, capture = GEN_BLOCK.unpack_from(payload, offset)
    if schema != 1:
        return None
    return {"config_generation": config, "media_generation_start": start,
            "media_generation_end": end, "capture_id": capture}


@dataclass
class WriteResult:
    """END frame of a WRITE (firmware 1.15; older firmware: level 0, clamped 0).
    generations: the 1.18 generation block (config, media at start and end, capture id 0)."""
    verify_level: int = 0
    clamped: int = 0
    host_verified_sectors: int = 0
    host_verified_format: str = "IBM MFM"
    readback_quality: dict | None = None
    generations: dict | None = None

    @property
    def verify_text(self) -> str:
        if self.host_verified_sectors:
            return f"host: {self.host_verified_sectors} checksum-valid {self.host_verified_format} sectors compared byte-for-byte"
        return VERIFY_LEVELS.get(self.verify_level, str(self.verify_level))

    @classmethod
    def unpack(cls, b: bytes) -> "WriteResult":
        if len(b) < 5:
            return cls()
        return cls(b[0], struct.unpack_from("<I", b, 1)[0], generations=generation_block(b, 5))


@dataclass
class DumpStatus:
    state: int                          # 6 = done, 7 = error, 8 copy, 9 IEC dump, 10-12 write from SD
    error: int                          # 1 storage, 2 drive, 3 full/write, 4 aborted, 5 format/image not recognised, 6 write-protected, 7 file not found
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


@dataclass(frozen=True)
class ProtocolDescription:
    schema: int
    transports: int
    flags: int
    caps: int
    native_hz: int
    gw_hz: int
    store_words: int
    max_payload: int
    max_indexes: int
    max_gw_duration_ticks: int
    min_gw_write_ticks: int
    max_gw_write_ticks: int

    @classmethod
    def unpack(cls, data: bytes) -> "ProtocolDescription":
        if len(data) < DESCRIBE_FMT.size or data[0] != 1:
            raise ValueError("unsupported or truncated DESCRIBE reply")
        return cls(*DESCRIBE_FMT.unpack_from(data))


class DeviceError(P.DeviceError):
    """Non-zero v2 status; subclass of the v1 error so callers catch both."""

    def __init__(self, command: int, status: int):
        self.command, self.status = command, status
        RuntimeError.__init__(self, f"command 0x{command:02X}: {STATUS_TEXT.get(status, f'error {status}')}")


# -- device --------------------------------------------------------------------
class Device2:
    """UFI v2 command layer over any byte stream (pyserial port or a test double)."""

    proto = 2

    _limits: "WriteLimits | None" = None
    _limits_checked = False

    def __init__(self, stream: Stream, timeout: float = 5.0):
        self.s = stream
        self.timeout = timeout
        self.parser = FrameParser()
        self.frames: deque[Frame] = deque()
        self.events: deque[Event] = deque()
        self.max_payload = MAX_PAYLOAD
        self._seq = 0
        self._iec_addressed: bool | None = None
        self.integrity = False
        self.extended = False

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
        data = encode_frame(ftype, cmd, seq, 0, payload)
        offset = 0
        while offset < len(data):
            written = self.s.write(data[offset:])
            if not isinstance(written, int) or not 0 < written <= len(data) - offset:
                raise TimeoutError("serial write made no valid progress")
            offset += written

    def read_frame(self, timeout: float | None = None) -> Frame:
        """Next non-event frame; events are queued in self.events."""
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while True:
            if time.monotonic() > deadline:
                raise TimeoutError("no v2 frame from device")
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
        deadline = time.monotonic() + (timeout if timeout is not None else self.timeout)
        while True:
            remaining = deadline - time.monotonic()
            if remaining <= 0:
                raise TimeoutError("no matching v2 response from device")
            f = self.read_frame(remaining)
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

    def describe(self) -> ProtocolDescription:
        return ProtocolDescription.unpack(self.request(DESCRIBE))

    def diagnostics(self) -> dict:
        data=self.request(DIAGNOSTICS)
        if len(data)!=64 or data[0]!=1:
            raise ValueError("unsupported or truncated DIAGNOSTICS reply")
        names=("sample_hz","store_words","pending_words","peak_words","capture_overruns",
               "capture_dma_errors","write_dma_errors","write_underruns","native_rx_pauses",
               "native_tx_timeouts","lease_releases","heap_bytes","axi_free_bytes",
               "stack_space_bytes","native_idle_timeout_ms")
        return dict(schema=data[0],psram=bool(data[1]&1),owner=data[2],
                    **dict(zip(names,struct.unpack_from('<15I',data,4), strict=True)))

    def features(self) -> list[dict]:
        data = self.request(FEATURES)
        if len(data) < 2 or data[0] != 1 or len(data) != 2 + 8*data[1]:
            raise ValueError("invalid FEATURES reply")
        result = []
        for pos in range(2, len(data), 8):
            ident, flags, owner, limit = struct.unpack_from("<HBBI", data, pos)
            result.append(dict(id=ident, supported=bool(flags&1), configured=bool(flags&2),
                               available=bool(flags&4), hardware_qualified=bool(flags&8),
                               owner=owner, limit=limit))
        return result

    def resource(self, acquire: bool | None = None) -> int:
        reply = self.request(RESOURCE, b"" if acquire is None else bytes([int(acquire)]))
        if len(reply) != 1:
            raise ValueError("invalid RESOURCE reply")
        return reply[0]

    def job_status(self) -> dict:
        data = self.request(JOB)
        if len(data) != 24 or data[0] != 1:
            raise ValueError("invalid job status")
        schema, kind, state, status, job_id, packets, size, crc = struct.unpack("<BBBBQIII", data)
        return dict(id=job_id, kind=kind, state=state, status=status,
                    packets=packets, bytes=size, crc=crc)

    def close(self):
        try:
            if hasattr(self.s, "cancel"):
                self.resource(False)
        finally:
            if hasattr(self.s, "close"):
                self.s.close()

    def _job_open(self, kind):
        from .jobs import JobStream
        job = JobStream(secrets.randbits(64) or 1)
        self.request(JOB, struct.pack("<BBQ", 1, kind, job.id))
        return job

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
        cur = dict(zip(TIMING_FIELDS, _timing_unpack(self.request(TIMING_CMD)), strict=True))
        if changes:
            unknown = set(changes) - set(TIMING_FIELDS)
            if unknown:
                raise ValueError(f"unknown timing field(s): {', '.join(sorted(unknown))}")
            cur.update(changes)
            new = self.request(TIMING_CMD, TIMING.pack(*(cur[f] for f in TIMING_FIELDS)))
            cur = dict(zip(TIMING_FIELDS, _timing_unpack(new), strict=True))
        return cur

    # -- drive settings beyond TIMING (firmware 1.18, ANALYSIS_JOB ops 10 and 18) ----------
    def analysis_op(self, op: int, payload: bytes = b"", timeout: float = 2.0) -> bytes:
        """One ANALYSIS_JOB sub-operation; the op byte leads the payload."""
        return self.request(ANALYSIS_JOB, bytes([op]) + payload, timeout=timeout)

    def extra_timing(self, **changes: int) -> dict[str, int]:
        """Read the additional drive times (op 10); keyword arguments (EXTRA_FIELDS) change them."""
        cur = dict(zip(EXTRA_FIELDS, EXTRA.unpack_from(self.analysis_op(10), 1), strict=True))
        if changes:
            unknown = set(changes) - set(EXTRA_FIELDS)
            if unknown:
                raise ValueError(f"unknown extra timing field(s): {', '.join(sorted(unknown))}")
            cur.update(changes)
            new = self.analysis_op(10, bytes([1]) + EXTRA.pack(*(cur[f] for f in EXTRA_FIELDS)))
            cur = dict(zip(EXTRA_FIELDS, EXTRA.unpack_from(new, 1), strict=True))
        return cur

    def operating_profile(self) -> OperatingProfile:
        """Effective drive settings of the selected drive (op 18)."""
        return OperatingProfile.unpack(self.analysis_op(18))

    def operating_profile_store(self, profile: OperatingProfile | None = None) -> OperatingProfile:
        """Validate, apply and store a profile for the selected drive (op 18, 40 bytes); without
        an argument the effective settings are stored so a later SELECT restores them."""
        body = (profile or self.operating_profile()).pack()
        return OperatingProfile.unpack(self.analysis_op(18, body))

    def apple_gcr_options(self, **changes) -> dict:
        """Apple II 5.25-inch GCR decoder options (format 7, op 33): marks as hex strings
        (addr_prologue, data_prologue, addr_epilogue, data_epilogue) and `flags`
        (APPLE_GCR_FLAGS names or an int).  Keyword arguments change them."""
        cur = _apple_unpack(self.analysis_op(33))
        if changes:
            unknown = set(changes) - set(APPLE_GCR_FIELDS)
            if unknown:
                raise ValueError(f"unknown Apple GCR option(s): {', '.join(sorted(unknown))}")
            cur.update(changes)
            body = bytes([1]) + _apple_pack(cur)
            cur = _apple_unpack(self.analysis_op(33, body))
        return cur

    def apple_gcr_stats(self) -> dict:
        """Apple II nibble statistics of the last format 7 analysis (op 35, firmware 1.18):
        {'revs': [{nibbles, sync_nibbles, address_fields, data_fields, orphan_headers}, ...],
         'sectors': [{volume, sync_before_addr, header_to_data, sync_before_data, flags, flag_names, seen}] x 16}.
        An empty 'revs' list means no format 7 analysis ran since the last reset."""
        b = self.analysis_op(35)
        if len(b) < 178 or b[0] not in (1, 2):
            raise DeviceError("Apple GCR statistics: unexpected reply")
        revs = []
        for r in range(b[1]):
            v = struct.unpack_from("<5H", b, 2 + 10 * r)
            revs.append(dict(zip(("nibbles", "sync_nibbles", "address_fields", "data_fields", "orphan_headers"), v, strict=True)))
            if b[0] >= 2 and len(b) >= 274:       # schema 2: trailing-zero histogram of the self-sync nibbles
                revs[-1]["trail"] = list(struct.unpack_from("<3H", b, 178 + 6 * r))
        sectors = []
        for s in range(16):
            v = struct.unpack_from("<6B", b, 82 + 6 * s)
            d = dict(zip(("volume", "sync_before_addr", "header_to_data", "sync_before_data", "flags", "seen"), v, strict=True))
            d["flag_names"] = flag_names(d["flags"], SECTOR_FLAGS)
            if b[0] >= 2 and len(b) >= 274:
                d["position"] = struct.unpack_from("<H", b, 242 + 2 * s)[0]
            sectors.append(d)
        out = {"revs": revs, "sectors": sectors}
        if b[0] >= 2 and len(b) >= 274:           # D5 AA xx prologue variants that match neither configured prologue
            out["variants"] = [{"third": b[226 + 4 * k], "count": struct.unpack_from("<H", b, 228 + 4 * k)[0]}
                               for k in range(4) if struct.unpack_from("<H", b, 228 + 4 * k)[0]]
        return out

    def apple_gcr_table(self, table: bytes | str | None = None) -> bytes:
        """6-and-2 symbol table of the format 7 decoder (op 36, firmware 1.18): 64 symbols >= 0x80,
        all distinct.  None reads, 'standard' restores APPLE_STD_TABLE, else 64 bytes or 128 hex digits."""
        if table is not None:
            if table == "standard":
                table = APPLE_STD_TABLE
            elif isinstance(table, str):
                table = bytes.fromhex(table)
            if len(table) != 64 or len(set(table)) != 64 or min(table) < 0x80:
                raise ValueError("Apple GCR table: 64 distinct symbols with bit 7 set")
            reply = self.analysis_op(36, bytes([1]) + bytes(table))
        else:
            reply = self.analysis_op(36)
        if len(reply) < 65 or reply[0] != 1:
            raise DeviceError("Apple GCR table: unexpected reply")
        return bytes(reply[1:65])

    def decoder_tolerance(self, flags=None) -> dict:
        """Tolerance flags of the analysis decoders for formats 1-6 (op 34, firmware 1.18).
        flags: None reads, else an int or TOLERANCE_FLAGS names joined with '+'.  Returns
        {'flags': int, 'flag_names': [...]}.  The flags stay set until changed or reboot."""
        if flags is not None:
            value = parse_flags(flags, TOLERANCE_FLAGS)
            if value & ~0x1F:
                raise ValueError("tolerance flags: bits 0-4 only")
            reply = self.analysis_op(34, bytes([1, value & 0xFF, value >> 8]))
        else:
            reply = self.analysis_op(34)
        if len(reply) < 3 or reply[0] != 1:
            raise DeviceError("decoder tolerance: unexpected reply")
        value = reply[1] | reply[2] << 8
        return {"flags": value, "flag_names": flag_names(value, TOLERANCE_FLAGS)}

    def write_lock(self, engage: bool = False) -> dict:
        """Session write lock (op 37, firmware 1.18).  engage=True engages it; there is no
        disengage over USB, only an MCU reset clears it.  Returns {'session': bool,
        'jumper': bool, 'locked': bool}.  While engaged the firmware refuses WRITE,
        GUARDED_WRITE, ERASE, PATTERN, FORMAT, SD-to-disk starts and IEC drive writes with
        status 22; reads and SD file writes stay allowed."""
        reply = self.analysis_op(37, b"\x01" if engage else b"")
        if len(reply) < 2 or reply[0] != 1:
            raise DeviceError("write lock: unexpected reply")
        return {"session": bool(reply[1] & 1), "jumper": bool(reply[1] & 2), "locked": bool(reply[1] & 3)}

    def timing_preset(self, preset: int | str) -> OperatingProfile:
        """Apply a named timing set (op 18 with [18, id]): 'standard' or 'konservativ'.  Only the
        movement/settle times change; the stored profile is untouched (use operating_profile_store)."""
        pid = TIMING_PRESETS[preset.lower()] if isinstance(preset, str) else int(preset)
        return OperatingProfile.unpack(self.analysis_op(18, bytes([pid])))

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
    def _iec_request(self, payload: bytes, device: int = 0) -> bytes:
        if device and not 8 <= device <= 30:
            raise ValueError("IEC device must be 8..30 (0 selects UFI.CFG)")
        if device:
            if self._iec_addressed is None:
                self._iec_addressed = bool(self.caps() & CAP_IEC_ADDRESSED)
            if not self._iec_addressed:
                raise RuntimeError("addressed IEC commands require firmware 1.18; refusing to use another device")
            return self.request(IEC_TARGET, bytes([1, device]) + payload, timeout=10.0)
        return self.request(IEC_MEM, payload, timeout=10.0)

    def iec_mem_write(self, addr: int, data: bytes, device: int = 0) -> None:
        """M-W: up to 32 bytes into the drive's RAM."""
        if not 1 <= len(data) <= 32:
            raise ValueError("M-W accepts 1..32 bytes")
        self._iec_request(bytes([0]) + struct.pack("<H", addr) + bytes(data), device)

    def iec_mem_read(self, addr: int, n: int, device: int = 0) -> bytes:
        """M-R: n (1-255) bytes from the drive's RAM."""
        if not 1 <= n <= 255:
            raise ValueError("M-R accepts 1..255 bytes")
        data = self._iec_request(bytes([1]) + struct.pack("<H", addr) + bytes([n]), device)
        if len(data) != n:
            raise ValueError("short IEC memory response")
        return data

    def iec_mem_exec(self, addr: int, device: int = 0) -> None:
        self._iec_request(bytes([2]) + struct.pack("<H", addr), device)

    def iec_command(self, cmd: bytes | str, device: int = 0) -> None:
        """DOS command on channel 15, e.g. b'I0'."""
        if isinstance(cmd, str):
            cmd = cmd.encode("ascii")
        if not 1 <= len(cmd) <= 40:
            raise ValueError("DOS command must be 1..40 bytes")
        self._iec_request(bytes([3, 0, 0]) + cmd, device)

    def iec_status(self, device: int = 0) -> str:
        """DOS status line of channel 15 (firmware 1.16), e.g. '00, OK,00,00'."""
        return self._iec_request(bytes([4, 0, 0]), device).decode("latin-1").rstrip("\r\n")

    def iec_block_result(self, track: int, sector: int, device: int = 0) -> tuple[int, bytes]:
        """Return DOS status and data separately; never mask a media read error."""
        if device and not 8 <= device <= 30:
            raise ValueError("IEC device must be 8..30")
        if self._iec_addressed is None:
            self._iec_addressed = bool(self.caps() & CAP_IEC_ADDRESSED)
        if self._iec_addressed:
            p = self.request(IEC_READ_STATUS, bytes([1, device, track, sector]), timeout=10.0)
            if len(p) < 4 or p[0] != 1 or p[2:4] != bytes([track, sector]) or len(p) != (260 if p[1] == 0 else 4):
                raise ValueError("invalid structured IEC block response")
            return p[1], bytes(p[4:])
        # Old firmware does not check DOS after U1. Check it explicitly before accepting data.
        if device:
            raise RuntimeError("addressed IEC status requires firmware 1.18")
        data = bytes(self.request(IEC_BLOCK, bytes([0, track, sector, device]), timeout=5.0))
        status = self.iec_status(device)
        if len(status) < 3 or not status[:2].isdigit() or status[2] != ',':
            raise ValueError("invalid DOS status")
        code = int(status[:2])
        if code == 0 and len(data) != 256:
            raise ValueError("short IEC block response")
        return code, data if code == 0 else b""

    def iec_block_read(self, track: int, sector: int, device: int = 0) -> bytes:
        """One 256-byte block via U1 (firmware 1.16); device 0 = UFI.CFG iec_device."""
        code, data = self.iec_block_result(track, sector, device)
        if code:
            raise RuntimeError(f"IEC DOS {code:02d} at track {track}, sector {sector}")
        return data

    def iec_block_write(self, track: int, sector: int, data: bytes, device: int = 0) -> int:
        """Write one 256-byte block via B-P + U2 (firmware 1.16).  Returns the DOS error
        code the drive reported afterwards (0 ok, 26 write protect on, 2x read/write errors);
        a DeviceError means the bus transaction itself failed."""
        if len(data) != 256:
            raise ValueError("a Commodore block is 256 bytes")
        p = self.request(IEC_BLOCK, bytes([1, track, sector, device]) + bytes(data), timeout=5.0)
        return p[0] if p else 255

    def iec_nib(self, halftrack: int, sync: int = 0, mode: int = 0, flags: int = 0,
                device: int = 0) -> NibChunk:
        """One 512-byte raw GCR chunk from the 1541 drive code: mode 0 at sync index `sync`
        after the track origin, 1 raw, 2 step only.  flags: NIB_REUPLOAD, NIB_REVERSE."""
        p = self.request(IEC_NIB, bytes([halftrack, sync, mode, flags, device]), timeout=12.0)
        status, synclen, maxlen, ht = struct.unpack_from("<BBHB", p)
        return NibChunk(status, synclen, maxlen, ht, bytes(p[5:5 + 512]))

    def analyze(self, track: int, side: int, revs: int = 3, quarter: int | None = None) -> dict:
        """Track analysis (firmware 1.14): rpm, interval peaks, encoding guess, bit cells,
        weak-bit windows, MFM sectors and the index-to-first-sector time.  quarter 0-3 (Apple
        drives, firmware 1.18) analyses the quarter track: side byte = side | 0x80 | quarter << 4."""
        if quarter is not None and not 0 <= quarter <= 3:
            raise ValueError("quarter must be 0..3")
        side_byte = side | (0x80 | quarter << 4 if quarter is not None else 0)
        p = self.request(ANALYZE, bytes([track, side_byte, revs]), timeout=8.0)
        v = ANALYZE_FMT.unpack_from(p)
        return dict(revs=v[0], encoding=ENCODINGS.get(v[1], str(v[1])), bitcell_ns=v[2],
                    rpm=v[3] / 100, peaks_ns=[x for x in v[4:8] if x], index_ticks=v[8],
                    transitions=v[9], bitcells=v[10], sectors=v[11], weak_windows=v[12],
                    first_sector_us=v[13], weak_mask=v[14])

    def format_track(self, track: int, side: int, rate_kbps: int, spt: int, gap3: int = 0,
                     fill: int = 0xF6, verify: bool = False, amiga: bool = False,
                     head_id_swap: bool = False) -> tuple[int, int | None]:
        """Firmware 1.14: lay out and write one IBM MFM (or Amiga, spt 11; HD 22 from 1.16) track with
        512-byte sectors 1..spt filled with `fill`.  Returns (flux transitions written,
        sectors read back with good CRCs or None without verify)."""
        flags = (FORMAT_VERIFY if verify else 0) | (FORMAT_AMIGA if amiga else 0) | \
                (FORMAT_HEAD_SWAP if head_id_swap else 0)
        p = self.request(FORMAT, FORMAT_REQ.pack(track, side, flags, rate_kbps, spt, gap3, fill & 0xFF),
                         timeout=10.0)
        count, good = struct.unpack_from("<IB", p)
        return count, (None if good == 0xFF else good)

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
                   quarter: int = 0, frame_sink=None, integrity: bool | None = None) -> Capture:
        """Streamed capture: DATA frames while the disk turns, END frame with the revolutions.
        hard_sectors: sector holes of a hard-sectored disk (10/16/32); only the index hole counts.
        quarter: Apple Disk II quarter-track offset 0-3 (track 17 + 2 = track 17.5)."""
        revolutions = max(1, min(revolutions, MAX_READ_REVS))
        flags = (READ_NO_INDEX if no_index else 0) | (HARD_SECTORS if hard_sectors else 0)
        if quarter:
            flags |= QUARTER_TRACKS
            track = track * 4 + quarter
        job = self._job_open(1) if (self.integrity if integrity is None else integrity) else None
        seq = self._next_seq()
        crc_errors = self.parser.crc_errors
        self.send(T_REQUEST, READ, seq, READ_REQ.pack(track, side, revolutions, flags, period_ms)
                  + (bytes([hard_sectors]) if hard_sectors else b""))
        f = self._wait(READ, seq, (T_RESPONSE,), 5.0)
        if frame_sink is not None:
            frame_sink(f.encode())
        if f.status:
            raise DeviceError(READ, f.status)
        data = bytearray()
        while True:
            f = self._wait(READ, seq, (T_DATA, T_END), 2.0 + 0.4 * revolutions)
            if frame_sink is not None:
                frame_sink(f.encode())
            if f.type == T_DATA:
                data += job.decode(f.payload) if job else f.payload
                continue
            if f.status and f.status != OVERFLOW:
                raise DeviceError(READ, f.status)
            if self.parser.crc_errors != crc_errors:
                raise ValueError("CRC error during flux capture; refusing a potentially incomplete stream")
            rflags = 0x01 | (0x02 if f.status == OVERFLOW else 0)
            revs = decode_stream(bytes(data), track, side, rflags)
            end = job.finish(f.payload) if job else f.payload
            if len(end) < 1 or end[0] > len(revs) or end[0] > revolutions:
                raise ValueError("invalid capture END count")
            if f.status == 0 and end[0] != revolutions:
                raise ValueError("successful capture returned fewer revolutions than requested")
            revs = revs[:end[0]]
            return Capture(revs, f.status, generation_block(end, 1))

    def write_limits(self) -> WriteLimits:
        """Effective limits of the write backend (firmware 1.15, caps bit 14)."""
        return WriteLimits.unpack(self.request(WRITE_LIMITS))

    def configuration(self, expected: int | None = None) -> dict:
        from .capabilities import decode_configuration
        payload = bytes([23]) + (struct.pack('<I', expected) if expected is not None else b'')
        return decode_configuration(self.request(0x0f, payload))

    def capability_matrix(self) -> dict:
        from .capabilities import decode_capabilities
        return decode_capabilities(self.request(0x0f, bytes([22])))

    def _preflight(self, deltas: list[int], scale: float, clamp: bool) -> None:
        """Refuse locally what firmware 1.15 would refuse (status 10/18); older firmware
        without the query is left alone."""
        if not self._limits_checked:
            self._limits_checked = True
            if self.caps() & CAP_WRITE_LIMITS:
                self._limits = self.write_limits()
        lim = self._limits
        if lim is None:
            return
        if len(deltas) < 3 or len(deltas) > lim.max_flux:
            raise ValueError(f"{len(deltas)} flux intervals: the firmware takes 3..{lim.max_flux}")
        if clamp:
            return
        for i, d in enumerate(deltas):
            s = round(d * scale)
            if s < lim.min_ticks or s > lim.max_ticks:
                raise ValueError(f"interval {i} = {d} ticks ({s} after scaling) outside "
                                 f"{lim.min_ticks}..{lim.max_ticks} ({lim.min_ticks / lim.sample_hz * 1e6:.1f}"
                                 f"-{lim.max_ticks / lim.sample_hz * 1e6:.0f} us); use clamp=True to force")

    def write_track(self, track: int, side: int, deltas: list[int], verify: bool = False,
                    hard_sectors: int = 0, quarter: int = 0, scale: float = 1.0,
                    clamp: bool = False, integrity: bool | None = None, extended: bool | None = None,
                    index_ticks: int | None = None, firmware_verify: bool = False,
                    expected_config: int | None = None, expected_media: int | None = None) -> WriteResult:
        """deltas: flux intervals in 275 MHz ticks (first one measured from the index).
        scale: the firmware multiplies every interval (0.5..2.0; firmware 1.14), e.g.
        rpm_source / rpm_target when writing a 360 rpm image on a 300 rpm drive.
        clamp: tolerate intervals outside 1.6-238 us (firmware 1.15 refuses them with
        status 18 otherwise); the result counts the clamped ones.
        firmware_verify selects native extended readback instead of host verify;
        index_ticks then supplies the source revolution period for reference decoding."""
        extended = self.extended if extended is None else extended
        guarded = expected_config is not None or expected_media is not None
        if guarded and (expected_config is None or expected_media is None or
                        not 1 <= expected_config <= 0xffffffff or not 1 <= expected_media <= 0xffffffff):
            raise ValueError('guarded write requires nonzero u32 configuration and media generations')
        expected = None
        if firmware_verify and (not extended or verify):
            raise ValueError("firmware_verify requires extended writing and excludes host verify")
        if firmware_verify and index_ticks is not None and not 1 <= index_ticks <= 0xffffffff:
            raise ValueError("source index period outside u32 range")
        if extended:
            if clamp or scale != 1 or quarter:
                raise ValueError("extended writes require host timing and separate readback verification")
            limits = self.describe()
            if not limits.flags & 2:
                raise ValueError("firmware does not advertise the extended write backend")
            if len(deltas) < 3 or len(deltas) > limits.store_words:
                raise ValueError("extended write exceeds flux store capacity")
            if any(d < 220 or d > 0xffffffff for d in deltas):
                raise ValueError("extended interval outside 220..4294967295 ticks")
            if verify:
                from .readback import reference
                rate,expected=reference(deltas,index_ticks=index_ticks)  # refuse unsupported verification before writing
                if hasattr(expected, 'track') and expected.track != track * 2 + side:
                    raise ValueError('Amiga source track does not match destination; no disk was written')
        else:
            self._preflight(deltas, scale, clamp)
        code = encode_flux(deltas)
        flags = (WRITE_VERIFY if verify and not extended else 0) | (HARD_SECTORS if hard_sectors else 0)
        if firmware_verify:
            flags |= WRITE_VERIFY
        if extended:
            flags |= 0x20
        if quarter:
            flags |= QUARTER_TRACKS
            track = track * 4 + quarter
        extra = bytes([hard_sectors]) if hard_sectors else b""
        per_mille = round(scale * 1000)
        if per_mille != 1000:
            if not 500 <= per_mille <= 2000:
                raise ValueError("scale must be between 0.5 and 2.0")
            flags |= TIME_SCALE
            extra += struct.pack("<H", per_mille)
        if clamp:
            flags |= WRITE_CLAMP
        if firmware_verify and index_ticks is not None:
            flags |= 0x40
            extra += struct.pack("<I", index_ticks)
        payload = WRITE_REQ.pack(track, side, flags, len(deltas), len(code)) + extra
        command = GUARDED_WRITE if guarded else WRITE
        if guarded:
            payload = struct.pack('<II', expected_config, expected_media) + payload
        job = self._job_open(2) if (self.integrity if integrity is None else integrity) else None
        seq = self._next_seq()
        self.send(T_REQUEST, command, seq, payload)
        f = self._wait(command, seq, (T_RESPONSE,), 5.0)
        if f.status:
            if job:
                try:
                    self.abort()
                except Exception:
                    log.warning('failed to close rejected write job', exc_info=True)
            raise DeviceError(command, f.status)
        chunk_size = self.max_payload - (16 if job else 0)
        for off in range(0, len(code), chunk_size):
            chunk = code[off:off + chunk_size]
            self.send(T_DATA, WRITE, seq, job.encode(chunk) if job else chunk)
        if job:
            self.request(JOB, struct.pack("<BI", 2, job.crc))
        f = self._wait(WRITE, seq, (T_END,), max(10.0,sum(deltas)/P.FLUX_CLOCK_HZ+3.0) if extended else 10.0)
        if f.status:
            raise DeviceError(WRITE, f.status)
        result=WriteResult.unpack(job.finish(f.payload) if job else f.payload)
        if expected is not None:
            from .readback import verify_capture
            capture=self.read_track(track,side,2,hard_sectors=hard_sectors,integrity=integrity)
            quality = {}
            result.host_verified_sectors=verify_capture(expected,rate,capture,quality=quality)
            result.host_verified_format=getattr(expected,'format','IBM MFM')
            result.readback_quality=quality or None
            result.verify_level=3
        return result

    def erase(self, track: int, side: int) -> None:
        self.request(ERASE, bytes([track, side]), timeout=3.0)

    def pattern(self, track: int, side: int, interval_ns: int, duration_ms: int) -> None:
        self.request(PATTERN, struct.pack("<BBHH", track, side, interval_ns, duration_ms),
                     timeout=3.0 + duration_ms / 1000)

    def abort(self) -> None:
        if hasattr(self.s, "cancel"):
            self.s.cancel()
        self.request(ABORT)

    def rpm(self) -> int:
        """Spindle speed from one revolution on the current track (motor must run)."""
        st = self.status()
        cap = self.read_track(st.track, st.side, 1)
        return round(cap.revolutions[0].rpm) if cap.revolutions else 0

    def selftest(self) -> list[tuple[int, str, int, int]]:
        """Firmware 1.14: board self-test -> [(id, name, status, value)]; status 0 ok,
        1 fail, 2 skipped, 3 warning.  Also writes SELFTEST.LOG on the SD NAND."""
        p = self.request(SELFTEST, timeout=8.0)
        out = []
        for i in range(p[0]):
            id_, st, val = struct.unpack_from("<BBI", p, 1 + 6 * i)
            out.append((id_, SELFTEST_ITEMS.get(id_, (f"item{id_}", ""))[0], st, val))
        return out

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
                   revs: int = 3, fmt: str | None = None) -> None:
        """fmt (firmware 1.16): 'scp' (flux), 'img' or 'adf' (decoded sectors); None = UFI.CFG."""
        if drive is None and fmt is None:
            self.request(DUMP_START)
        else:
            d = DRIVES[drive or "a"] if isinstance(drive, str) or drive is None else drive
            tail = b"" if fmt is None else bytes([DUMP_FORMATS[fmt]])
            self.request(DUMP_START, bytes([d, tracks, sides, revs]) + tail)

    def dump_status(self) -> DumpStatus:
        return DumpStatus.unpack(self.request(DUMP_STATUS))

    def dump_abort(self) -> None:
        self.request(DUMP_ABORT)

    def copy_start(self) -> None:
        self.request(COPY_START)

    def sd_write_start(self, name: str = "") -> None:
        """Firmware 1.16: write NAME (8.3, on the SD NAND; SCP/IMG/ADF/D81, D64/D71 via iec)
        onto the stand-alone drive; '' = UFI.CFG write_file.  Progress via dump_status()."""
        self.request(WRITE_START, name.upper().encode("ascii"))


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
    if port == "usb" or (port and port.startswith("usb:")):
        from .usb_bulk import open_bulk
        return open_bulk(port[4:] if port.startswith("usb:") else None)
    return P.open_serial(port)


def connect(port: str | None = None, timeout: float = 5.0) -> Device2 | P.Device:
    ser = open_serial(port)
    channel_reset(ser)
    if hasattr(ser, "cancel"):
        dev = Device2(ser, timeout)
        try:
            if not dev.ping(secrets.token_bytes(16), timeout=timeout):
                raise TimeoutError("native bulk interface did not answer UFI v2")
            dev.device_info()
        except Exception:
            ser.close()
            raise
        return dev
    return connect_stream(ser, timeout)
