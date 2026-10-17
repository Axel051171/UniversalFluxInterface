"""Apple II 5.25-inch 16-sector 6-and-2 GCR on the host: decode flux captures into
256-byte sectors and write .dsk (DOS 3.3 order) or .po (ProDOS order) images.

Mirrors the firmware's format 7 decoder (docs/USB_Protokoll.md 5.7): same marks,
tolerance flags and per-sector findings, independently implemented from the documented
track layout. Bit cells are 4 us (250 kbit/s raw), a nibble starts with its first 1 bit.
"""
from __future__ import annotations

from dataclasses import dataclass, field

from .hfe import Pll

CELL_RATE = 125_000                 # Pll(rate) models MFM cells of 1/(2*rate): 4 us
SYMBOLS = bytes([
    0x96, 0x97, 0x9a, 0x9b, 0x9d, 0x9e, 0x9f, 0xa6, 0xa7, 0xab, 0xac, 0xad, 0xae, 0xaf, 0xb2, 0xb3,
    0xb4, 0xb5, 0xb6, 0xb7, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf, 0xcb, 0xcd, 0xce, 0xcf, 0xd3,
    0xd6, 0xd7, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf, 0xe5, 0xe6, 0xe7, 0xe9, 0xea, 0xeb, 0xec,
    0xed, 0xee, 0xef, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff])
DECODE = {s: i for i, s in enumerate(SYMBOLS)}
# physical sector -> logical sector of the image (Floppy Emu tables, Beneath Apple DOS)
DOS33_ORDER = (0, 7, 14, 6, 13, 5, 12, 4, 11, 3, 10, 2, 9, 1, 8, 15)
PRODOS_ORDER = (0, 8, 1, 9, 2, 10, 3, 11, 4, 12, 5, 13, 6, 14, 7, 15)
ORDERS = {"dos33": DOS33_ORDER, "prodos": PRODOS_ORDER, "physical": tuple(range(16))}

# tolerance flags, same values as the firmware (ANALYSIS_JOB op 33)
KEEP_BAD_ADDR_CHECKSUM, IGNORE_EPILOGUES, ANY_TRACK, DATA_WITHOUT_CHECKSUM, DATA_WITHOUT_HEADER = 1, 2, 4, 8, 16
SF_ADDR_CHECKSUM, SF_DATA_CHECKSUM, SF_EPILOGUE = 0x20, 0x40, 0x80


@dataclass
class Options:
    addr_prologue: bytes = b"\xd5\xaa\x96"
    data_prologue: bytes = b"\xd5\xaa\xad"
    addr_epilogue: bytes = b"\xde\xaa"
    data_epilogue: bytes = b"\xde\xaa"
    flags: int = 0

    def check(self) -> "Options":
        if len(self.addr_prologue) != 3 or len(self.data_prologue) != 3 or len(self.addr_epilogue) != 2 \
                or len(self.data_epilogue) != 2:
            raise ValueError("Apple GCR marks: prologues are 3 bytes, epilogues 2 bytes")
        if any(b < 0x80 for b in self.addr_prologue + self.data_prologue + self.addr_epilogue + self.data_epilogue):
            raise ValueError("Apple GCR marks must have bit 7 set")
        if self.addr_prologue == self.data_prologue or self.flags & ~0x1F:
            raise ValueError("Apple GCR options: prologues must differ, unknown flag bits")
        return self


@dataclass
class Sector:
    track: int
    sector: int                     # physical, 255 = data field without header
    volume: int
    data: bytes
    ok: bool
    flags: int = 0                  # SF_* findings


@dataclass
class TrackDecode:
    sectors: list[Sector] = field(default_factory=list)
    header_errors: int = 0
    data_errors: int = 0
    code_errors: int = 0
    nibbles: int = 0

    def best(self) -> dict[int, Sector]:
        """One sector per physical number, a checksum-clean one wins."""
        out: dict[int, Sector] = {}
        for s in self.sectors:
            if s.sector == 255:
                continue
            if s.sector not in out or (s.ok and not out[s.sector].ok):
                out[s.sector] = s
        return out


def nibbles(deltas: list[int]) -> bytes:
    """Flux intervals (275 MHz ticks) -> GCR nibble stream, latching on the first 1 bit."""
    pll = Pll(CELL_RATE)
    out = bytearray()
    byte = 0
    for delta in deltas:
        if not isinstance(delta, int) or not 0 < delta <= 0xffffffff:
            raise ValueError("invalid flux interval")
        count = min(pll.cells(delta), 64)
        for _ in range(count - 1):          # zero cells
            if byte:
                byte = (byte << 1) & 0xff
                if byte & 0x80:
                    out.append(byte)
                    byte = 0
        byte = ((byte << 1) | 1) & 0xff     # the transition
        if byte & 0x80:
            out.append(byte)
            byte = 0
    return bytes(out)


def _payload(x: bytes) -> bytes:
    data = bytearray(256)
    for i in range(256):
        pair = (x[i % 86] >> (2 * (i // 86))) & 3
        data[i] = (x[86 + i] << 2) | ((pair & 1) << 1) | (pair >> 1)
    return bytes(data)


def decode_nibbles(stream: bytes, options: Options | None = None) -> TrackDecode:
    o = (options or Options()).check()
    r = TrackDecode(nibbles=len(stream))
    i = 0
    n = len(stream)
    id_ok = False
    track = sector = volume = 0
    id_flags = 0
    id_pos = -10**9
    while i + 3 <= n:
        mark = stream[i:i + 3]
        if mark == o.addr_prologue and i + 13 <= n:
            field_ = stream[i + 3:i + 13]
            v = [((field_[2 * k] << 1) | 1) & field_[2 * k + 1] for k in range(4)]
            flags = 0
            ending = bytes(field_[8:10]) == o.addr_epilogue
            if not ending:
                flags |= SF_EPILOGUE
            sum_ok = v[3] == (v[0] ^ v[1] ^ v[2])
            if not sum_ok:
                flags |= SF_ADDR_CHECKSUM
            limits = v[2] < 64 if o.flags & ANY_TRACK else (v[1] < 40 and v[2] < 16)
            id_ok = (ending or bool(o.flags & IGNORE_EPILOGUES)) and limits and \
                (sum_ok or bool(o.flags & KEEP_BAD_ADDR_CHECKSUM))
            if not sum_ok or not ending:
                r.header_errors += 1
            if id_ok:
                volume, track, sector, id_flags, id_pos = v[0], v[1], v[2], flags, i
            i += 13
            continue
        if mark == o.data_prologue and ((id_ok and i - id_pos < 600) or o.flags & DATA_WITHOUT_HEADER) \
                and i + 3 + 345 <= n:
            body = stream[i + 3:i + 3 + 343]
            x = bytearray(343)
            prev = 0
            bad = False
            for k, nib in enumerate(body):
                sym = DECODE.get(nib)
                if sym is None:
                    bad = True
                    break
                x[k] = sym ^ prev
                prev = x[k]
            if bad:
                r.code_errors += 1
                id_ok = False
                i += 3
                continue
            flags = id_flags if id_ok else 0
            ending = bytes(stream[i + 346:i + 348]) == o.data_epilogue
            if not ending:
                flags |= SF_EPILOGUE
            checksum = x[342] == 0
            if not checksum:
                flags |= SF_DATA_CHECKSUM
            ok = (ending or bool(o.flags & IGNORE_EPILOGUES)) and (checksum or bool(o.flags & DATA_WITHOUT_CHECKSUM))
            if not checksum or not ending:
                r.data_errors += 1
            r.sectors.append(Sector(track if id_ok else 255, sector if id_ok else 255, volume if id_ok else 0,
                                    _payload(bytes(x)), ok, flags))
            id_ok = False
            i += 348
            continue
        i += 1
    return r


def deltas_of(rev) -> list[int]:
    previous = 0
    out = []
    for timestamp in rev.samples:
        if timestamp <= previous:
            raise ValueError("invalid capture timestamps")
        out.append(timestamp - previous)
        previous = timestamp
    return out


def decode_capture(capture, options: Options | None = None) -> TrackDecode:
    """All revolutions of a READ capture, merged."""
    total = TrackDecode()
    for rev in capture.revolutions:
        part = decode_nibbles(nibbles(deltas_of(rev)), options)
        total.sectors.extend(part.sectors)
        total.header_errors += part.header_errors
        total.data_errors += part.data_errors
        total.code_errors += part.code_errors
        total.nibbles += part.nibbles
    return total


class DiskImage:
    """35 (or more) tracks of 16 sectors; missing sectors stay zero and are listed."""

    def __init__(self, tracks: int = 35, order: str = "dos33"):
        if order not in ORDERS:
            raise ValueError("order must be dos33, prodos or physical")
        self.tracks = tracks
        self.order = ORDERS[order]
        self.data = bytearray(tracks * 16 * 256)
        self.missing: list[tuple[int, int]] = []
        self.weak: list[tuple[int, int]] = []
        self.volumes: set[int] = set()

    def add_track(self, track: int, decoded: TrackDecode, *, expect_track: bool = True) -> int:
        best = decoded.best()
        good = 0
        for phys in range(16):
            s = best.get(phys)
            if s is None or (expect_track and s.track != track):
                self.missing.append((track, phys))
                continue
            if not s.ok:
                self.weak.append((track, phys))
            self.volumes.add(s.volume)
            logical = self.order[phys]
            off = (track * 16 + logical) * 256
            self.data[off:off + 256] = s.data
            good += 1
        return good

    def write(self, path: str) -> None:
        with open(path, "wb") as f:
            f.write(self.data)


def order_for_name(path: str, requested: str | None) -> str:
    if requested:
        return requested
    return "prodos" if path.lower().endswith(".po") else "dos33"
