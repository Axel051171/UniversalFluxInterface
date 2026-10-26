"""Macintosh 3.5-inch disks on the host: 400K/800K GCR decoding and encoding, FDHD 1.44 MB
MFM assembly, raw (.img/.dsk) and DiskCopy 4.2 images.

The GCR side mirrors the firmware's format 6 decoder (docs/USB_Protokoll.md 5.4): address field
D5 AA 96 + track, sector, side/track-high, format, XOR checksum + DE AA; data field D5 AA AD +
sector + 699 six-and-two symbols for 524 bytes (12 tag + 512 data) + four checksum symbols +
DE AA, with the three running checksums of the documented layout.  The 6-and-2 symbol table is
the Apple II one.  Independently implemented from the documented on-disk layout.

On a Mac drive the bit cell is 2 us on every track: the drive turns faster on the outer zones
(tracks 0-15 / 16-31 / 32-47 / 48-63 / 64-79: 394 / 429 / 472 / 525 / 590 rpm, 12..8 sectors).
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from . import apple
from .protocol import FLUX_CLOCK_HZ

CELL_RATE = 250_000                 # Pll(rate) models MFM cells of 1/(2*rate): 2 us GCR cells
CELL_TICKS = FLUX_CLOCK_HZ // 500_000   # 550 ticks = 2 us at 275 MHz
TRACKS = 80
ZONE_RPM = (394, 429, 472, 525, 590)
ADDR_MARK = b"\xd5\xaa\x96"
DATA_MARK = b"\xd5\xaa\xad"
TRAILER = b"\xde\xaa"
FORMAT_400K, FORMAT_800K = 0x02, 0x22   # header format byte: bit 5 two sides, interleave 2
KIND_NAMES = {0: "none", 1: "400K", 2: "800K", 3: "FDHD"}
SF_HEADER_CHECKSUM, SF_DATA_CHECKSUM, SF_TRAILER, SF_SECTOR_MISMATCH = 1, 2, 4, 8


def zone(track: int) -> int:
    return min(track // 16, 4)


def sectors_per_track(track: int) -> int:
    return 12 - zone(track)


def interleave(n: int, factor: int = 2) -> list[int]:
    """Physical slot -> logical sector for an n-sector track (2:1: 0 6 1 7 2 8 ... for 12)."""
    slots: list[int | None] = [None] * n
    pos = 0
    for s in range(n):
        while slots[pos] is not None:
            pos = (pos + 1) % n
        slots[pos] = s
        pos = (pos + factor) % n
    return [s for s in slots if s is not None]


def blocks(sides: int) -> int:
    return sides * sum(sectors_per_track(t) for t in range(TRACKS))


def block_index(track: int, side: int, sector: int, sides: int) -> int:
    """Logical block of (track, side, sector): track-major, then side, then sector."""
    before = sum(sectors_per_track(t) for t in range(track)) * sides
    return before + side * sectors_per_track(track) + sector


@dataclass
class Sector:
    track: int
    side: int
    sector: int
    fmt: int
    data: bytes                     # 524 bytes: 12 tags + 512 data
    ok: bool
    flags: int = 0


@dataclass
class TrackDecode:
    sectors: list[Sector] = field(default_factory=list)
    header_errors: int = 0
    data_errors: int = 0
    code_errors: int = 0
    nibbles: int = 0

    def best(self) -> dict[tuple[int, int, int], Sector]:
        """One sector per (track, side, sector), a checksum-clean one wins."""
        out: dict[tuple[int, int, int], Sector] = {}
        for s in self.sectors:
            key = (s.track, s.side, s.sector)
            if key not in out or (s.ok and not out[key].ok):
                out[key] = s
        return out


def nibbles(deltas: list[int], rate: int = CELL_RATE) -> bytes:
    return apple.nibbles(deltas, rate)


def _symbols(raw: bytes) -> list[int] | None:
    out = []
    for b in raw:
        v = apple.DECODE.get(b)
        if v is None:
            return None
        out.append(v)
    return out


def decode_payload(sym: list[int]) -> tuple[bytes, bool]:
    """699 data symbols + 4 checksum symbols -> 524 bytes, checksum ok."""
    a = b = c = 0
    out = bytearray()
    i = 0
    while len(out) < 524:
        hi, x, y = sym[i], sym[i + 1], sym[i + 2]
        last = len(out) == 522
        z = 0 if last else sym[i + 3]
        i += 3 if last else 4
        a = (a & 0xff) << 1
        if a & 0x100:
            a += 1
        v = (x | ((hi << 2) & 0xc0)) ^ (a & 0xff)
        c += v
        if a & 0x100:
            c += 1
            a &= 0xff
        out.append(v)
        v = (y | ((hi << 4) & 0xc0)) ^ (c & 0xff)
        b += v
        if c > 0xff:
            b += 1
            c &= 0xff
        out.append(v)
        if not last:
            v = (z | ((hi << 6) & 0xc0)) ^ (b & 0xff)
            a += v
            if b > 0xff:
                a += 1
                b &= 0xff
            out.append(v)
    high = ((a >> 6) & 3) | ((b >> 4) & 12) | ((c >> 2) & 48)
    ok = sym[i:i + 4] == [high, c & 63, b & 63, a & 63]
    return bytes(out), ok


def encode_payload(data: bytes) -> list[int]:
    """524 bytes -> 699 data symbols + 4 checksum symbols (inverse of decode_payload)."""
    if len(data) != 524:
        raise ValueError("a Macintosh sector is 524 bytes (12 tags + 512 data)")
    a = b = c = 0
    at = 0
    out: list[int] = []
    while at < 524:
        a = (a & 0xff) << 1
        if a & 0x100:
            a += 1
        x = data[at] ^ (a & 0xff)
        c += data[at]
        at += 1
        if a & 0x100:
            c += 1
            a &= 0xff
        y = data[at] ^ (c & 0xff)
        b += data[at]
        at += 1
        if c > 0xff:
            b += 1
            c &= 0xff
        last = at == 524
        z = 0
        if not last:
            z = data[at] ^ (b & 0xff)
            a += data[at]
            at += 1
            if b > 0xff:
                a += 1
                b &= 0xff
        out.append(((x >> 2) & 48) | ((y >> 4) & 12) | (z >> 6))
        out += [x & 63, y & 63] + ([] if last else [z & 63])
    out += [((a >> 6) & 3) | ((b >> 4) & 12) | ((c >> 2) & 48), c & 63, b & 63, a & 63]
    return out


def decode_nibbles(stream: bytes) -> TrackDecode:
    r = TrackDecode(nibbles=len(stream))
    n = len(stream)
    i = 0
    header: tuple[int, int, int, int] | None = None
    header_flags = 0
    header_at = -10**9
    while i + 3 <= n:
        mark = stream[i:i + 3]
        if mark == ADDR_MARK and i + 10 <= n:
            sym = _symbols(stream[i + 3:i + 8])
            header = None
            if sym is None:
                r.code_errors += 1
                i += 3
                continue
            flags = 0
            if stream[i + 8:i + 10] != TRAILER:
                flags |= SF_TRAILER
            if sym[0] ^ sym[1] ^ sym[2] ^ sym[3] ^ sym[4]:
                flags |= SF_HEADER_CHECKSUM
            track = sym[0] | ((sym[2] & 1) << 6)
            if flags or track >= TRACKS or sym[2] & 0x1e:
                r.header_errors += 1
            else:
                header, header_flags, header_at = (track, sym[2] >> 5, sym[1], sym[3]), flags, i
            i += 10
            continue
        if mark == DATA_MARK and header is not None and i - header_at < 200 and i + 3 + 704 + 2 <= n:
            sym = _symbols(stream[i + 3:i + 3 + 704])
            if sym is None:
                r.code_errors += 1
                header = None
                i += 3
                continue
            data, checksum = decode_payload(sym[1:])
            flags = header_flags
            if not checksum:
                flags |= SF_DATA_CHECKSUM
            if stream[i + 707:i + 709] != TRAILER:
                flags |= SF_TRAILER
            if sym[0] != header[2]:
                flags |= SF_SECTOR_MISMATCH
            ok = not flags
            if not ok:
                r.data_errors += 1
            r.sectors.append(Sector(header[0], header[1], header[2], header[3], data, ok, flags))
            header = None
            i += 709
            continue
        i += 1
    return r


def decode_capture(capture, rate: int = CELL_RATE) -> TrackDecode:
    """All revolutions (no-index slices) of a READ capture, merged."""
    total = TrackDecode()
    for rev in capture.revolutions:
        part = decode_nibbles(nibbles(apple.deltas_of(rev), rate))
        total.sectors.extend(part.sectors)
        total.header_errors += part.header_errors
        total.data_errors += part.data_errors
        total.code_errors += part.code_errors
        total.nibbles += part.nibbles
    return total


# ---- encoding ------------------------------------------------------------------------------

def _bits(byte: int) -> list[int]:
    return [(byte >> k) & 1 for k in range(7, -1, -1)]


SYNC = [1] * 8 + [0, 0]             # 10-cell self-sync group: FF and two zero cells


def encode_sector(track: int, side: int, sector: int, data: bytes, fmt: int, gap: int = 6) -> list[int]:
    """Cells of one sector: header field, `gap` sync groups, data field (no leading sync)."""
    hdr = [track & 63, sector, (side << 5) | (track >> 6), fmt & 63]
    hdr.append(hdr[0] ^ hdr[1] ^ hdr[2] ^ hdr[3])
    cells: list[int] = []
    for b in ADDR_MARK + bytes(apple.SYMBOLS[v] for v in hdr) + TRAILER:
        cells += _bits(b)
    cells += SYNC * gap
    for b in DATA_MARK + bytes(apple.SYMBOLS[v] for v in [sector] + encode_payload(data)) + TRAILER:
        cells += _bits(b)
    return cells


def encode_track(track: int, side: int, sectors: dict[int, bytes], fmt: int, *,
                 fill: float = 0.96, cell_ticks: int = CELL_TICKS) -> list[int]:
    """Flux intervals (275 MHz ticks) of a whole track in 2:1 interleave, sync groups spread so
    the track takes `fill` of one revolution at the zone speed; the write starts anywhere
    (no index), the gap behind the last sector absorbs speed tolerance."""
    n = sectors_per_track(track)
    if sorted(sectors) != list(range(n)):
        raise ValueError(f"track {track}: sectors 0..{n - 1} required")
    bodies = [encode_sector(track, side, s, sectors[s], fmt) for s in interleave(n)]
    rev_cells = int(60 / ZONE_RPM[zone(track)] * FLUX_CLOCK_HZ / cell_ticks)
    room = int(rev_cells * fill) - sum(len(b) for b in bodies)
    groups = room // len(SYNC)
    if groups < 6 * (n + 1):
        raise ValueError(f"track {track}: {n} sectors do not fit into one revolution")
    per = groups // (n + 1)
    cells = SYNC * (per + groups % (n + 1))
    for body in bodies:
        cells += body + SYNC * per
    deltas: list[int] = []
    run = 0
    for c in cells:
        run += 1
        if c:
            deltas.append(run * cell_ticks)
            run = 0
    return deltas


# ---- images --------------------------------------------------------------------------------

def dc42_checksum(data: bytes) -> int:
    """DiskCopy 4.2 checksum: add each big-endian 16-bit word, rotate the 32-bit sum right by one."""
    s = 0
    for (w,) in struct.iter_unpack(">H", data[:len(data) & ~1]):
        s = (s + w) & 0xffffffff
        s = ((s >> 1) | (s << 31)) & 0xffffffff
    return s


DC42_FORMAT = {1: (0, 0x12), 2: (1, 0x22)}      # sides -> (disk format, format byte)


def write_dc42(path: str, data: bytes, tags: bytes, sides: int, name: str = "UFI") -> None:
    disk, fmt = DC42_FORMAT[sides]
    label = name.encode("mac_roman", "replace")[:63]
    header = bytes([len(label)]) + label.ljust(63, b"\0")
    header += struct.pack(">IIIIBBH", len(data), len(tags), dc42_checksum(data),
                          dc42_checksum(tags[12:]) if tags else 0, disk, fmt, 0x0100)
    with open(path, "wb") as f:
        f.write(header + data + tags)


def read_dc42(raw: bytes) -> tuple[bytes, bytes, int]:
    """-> (data, tags, sides); checksums are checked."""
    if len(raw) < 84 or raw[82:84] != b"\x01\x00":
        raise ValueError("not a DiskCopy 4.2 image")
    dsize, tsize, dsum, tsum, disk, _fmt = struct.unpack_from(">IIIIBB", raw, 64)
    data, tags = raw[84:84 + dsize], raw[84 + dsize:84 + dsize + tsize]
    if len(data) != dsize or len(tags) != tsize:
        raise ValueError("DiskCopy 4.2 image is truncated")
    if dc42_checksum(data) != dsum or (tags and dc42_checksum(tags[12:]) != tsum):
        raise ValueError("DiskCopy 4.2 checksum mismatch")
    if disk not in (0, 1) or dsize != 512 * blocks(disk + 1):
        raise ValueError("only 400K/800K GCR DiskCopy images are supported")
    return data, tags, disk + 1


def is_dc42_name(path: str) -> bool:
    return path.lower().endswith((".dc42", ".image", ".dc", ".diskcopy"))


class GcrImage:
    """400K (one side) or 800K (two sides): 512 data + 12 tag bytes per block."""

    def __init__(self, sides: int):
        if sides not in (1, 2):
            raise ValueError("sides must be 1 (400K) or 2 (800K)")
        self.sides = sides
        self.data = bytearray(512 * blocks(sides))
        self.tags = bytearray(12 * blocks(sides))
        self.missing: list[tuple[int, int, int]] = []
        self.weak: list[tuple[int, int, int]] = []
        self.formats: set[int] = set()

    def add_track(self, track: int, side: int, decoded: TrackDecode) -> int:
        best = decoded.best()
        good = 0
        for s in range(sectors_per_track(track)):
            sec = best.get((track, side, s))
            if sec is None:
                self.missing.append((track, side, s))
                continue
            if not sec.ok:
                self.weak.append((track, side, s))
            self.formats.add(sec.fmt)
            self.put(track, side, s, sec.data)
            good += 1
        return good

    def put(self, track: int, side: int, sector: int, data524: bytes) -> None:
        b = block_index(track, side, sector, self.sides)
        self.tags[12 * b:12 * b + 12] = data524[:12]
        self.data[512 * b:512 * b + 512] = data524[12:524]

    def sector(self, track: int, side: int, sector: int) -> bytes:
        b = block_index(track, side, sector, self.sides)
        return bytes(self.tags[12 * b:12 * b + 12] + self.data[512 * b:512 * b + 512])

    def write(self, path: str, name: str = "UFI") -> None:
        if is_dc42_name(path):
            write_dc42(path, bytes(self.data), bytes(self.tags), self.sides, name)
        else:
            with open(path, "wb") as f:
                f.write(self.data)

    @classmethod
    def load(cls, path: str) -> GcrImage:
        with open(path, "rb") as f:
            raw = f.read()
        if is_dc42_name(path):
            data, tags, sides = read_dc42(raw)
        else:
            sides = {512 * blocks(1): 1, 512 * blocks(2): 2}.get(len(raw))
            if sides is None:
                raise ValueError(f"{path}: {len(raw)} bytes is neither 400K nor 800K")
            data, tags = raw, b""
        img = cls(sides)
        img.data[:] = data
        if tags:
            img.tags[:] = tags
        return img


MFM_HD_SECTORS = 18


class MfmImage:
    """FDHD 1.44 MB: 80 tracks, 2 sides, 18 IBM MFM sectors of 512 bytes (500 kbit/s)."""

    def __init__(self):
        self.data = bytearray(TRACKS * 2 * MFM_HD_SECTORS * 512)
        self.missing: list[tuple[int, int, int]] = []

    def add_track(self, track: int, side: int, sectors: dict) -> int:
        good = 0
        for r in range(1, MFM_HD_SECTORS + 1):
            got = sectors.get((track, side, r, 2))
            if got is None:
                self.missing.append((track, side, r))
                continue
            off = ((track * 2 + side) * MFM_HD_SECTORS + r - 1) * 512
            self.data[off:off + 512] = got[1]
            good += 1
        return good

    def write(self, path: str) -> None:
        with open(path, "wb") as f:
            f.write(self.data)
