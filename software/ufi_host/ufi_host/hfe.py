"""HxC HFE v1 images and v3 import with explicit timing/weak/index metadata.

Layout: 512-byte header ("HXCPICFE", revision 0, tracks, sides, encoding, bit rate kbit/s,
rpm, interface mode, track table block, write allowed, single step), track table in block
1 (per cylinder: offset in 512-byte blocks, length in bytes for both sides), track data in
512-byte blocks holding 256 bytes of side 0 followed by 256 bytes of side 1.  One bit per
MFM cell, LSB of every byte first, exactly one revolution per track.

The v1 flux <-> cell conversions are the integer algorithms of firmware/src/ufi_image.c, so a
dump made by the firmware (format=hfe) and ``ufi read-disk -o x.hfe`` from the same flux
are byte-identical, and ``ufi write-disk x.hfe`` sends the same deltas the firmware's
``sd-write`` would.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass, field

from .protocol import FLUX_CLOCK_HZ

BLOCK = 512
ENC_ISOIBM_MFM, ENC_AMIGA_MFM, ENC_ISOIBM_FM, ENC_UNKNOWN = 0, 1, 2, 0xFF
IF_IBMPC_DD, IF_IBMPC_HD, IF_AMIGA_DD, IF_AMIGA_HD, IF_IBMPC_ED = 0, 1, 4, 5, 8
RATES = (250_000, 300_000, 500_000, 1_000_000)
ENCODINGS = {"mfm": ENC_ISOIBM_MFM, "amiga": ENC_AMIGA_MFM, "fm": ENC_ISOIBM_FM, "unknown": ENC_UNKNOWN}


@dataclass
class HfeImage:
    rate_bps: int
    encoding: int = ENC_ISOIBM_MFM
    rpm: int = 0                            # 0 = unknown
    double_step: bool = False
    tracks: dict[tuple[int, int], bytes] = field(default_factory=dict)   # (cyl, side) -> cell bytes
    v3_tracks: dict[tuple[int, int], HfeV3Track] = field(default_factory=dict)

    @property
    def cylinders(self) -> int:
        return max((c for c, _ in self.tracks), default=-1) + 1

    @property
    def sides(self) -> int:
        return 2 if any(s == 1 for _, s in self.tracks) else 1


def interface_mode(rate_bps: int, encoding: int) -> int:
    if encoding == ENC_AMIGA_MFM:
        return IF_AMIGA_HD if rate_bps >= 500_000 else IF_AMIGA_DD
    return IF_IBMPC_ED if rate_bps >= 1_000_000 else IF_IBMPC_HD if rate_bps >= 500_000 else IF_IBMPC_DD


# ---- cells <-> flux (same arithmetic as the firmware) ------------------------------------

def fit_cell16(rate_bps: int, cells: int = 0, rev_ticks: int = 0) -> tuple[int, int]:
    """Cell length in 1/65536 tick and the applied factor in permille: nominal, or scaled so
    that `cells` cells fill 98.5 % of rev_ticks (limited to 0.97..1.03)."""
    nominal16 = (FLUX_CLOCK_HZ << 16) // (2 * rate_bps)
    pm = 1000
    if rev_ticks and cells:
        track = cells * FLUX_CLOCK_HZ // (2 * rate_bps)
        pm = min(1030, max(970, (rev_ticks * 985 + track // 2) // track))
    return (nominal16 * pm + 500) // 1000, pm


def bits_to_deltas(cells: bytes, cell16: int) -> list[int]:
    """Cell bytes (LSB first) -> write deltas in 275 MHz ticks, rounding on absolute time."""
    out, n, prev = [], 0, 0
    for b in cells:
        for k in range(8):
            n += 1
            if b & (1 << k):
                now = (n * cell16 + 32768) >> 16
                out.append(now - prev)
                prev = now
    return out


class Pll:
    """Clock recovery of the firmware's MFM decoder: 1/16 of the error into the period, half
    of the residual into the phase, +-12 %; gaps longer than 8 cells are kept without
    adaptation."""

    def __init__(self, rate_bps: int, *, period_divisor: int = 16, phase_divisor: int = 2):
        if period_divisor not in (8, 16, 32) or phase_divisor not in (2, 4):
            raise ValueError('unsupported PLL gain')
        self.period_divisor = period_divisor
        self.phase_divisor = phase_divisor
        self.nominal = (FLUX_CLOCK_HZ * 256) // (2 * rate_bps)
        self.period = self.nominal
        self.pmin = self.nominal - self.nominal // 8
        self.pmax = self.nominal + self.nominal // 8
        self.phase = 0

    def cells(self, ticks: int) -> int:
        t = ticks * 256 + self.phase
        n = max(1, _cdiv(t + self.period // 2, self.period))
        if n > 8:
            self.phase = 0
            return n
        err = t - n * self.period
        self.period = min(self.pmax, max(self.pmin, self.period + _cdiv(err, n * self.period_divisor)))
        self.phase = _cdiv(err, self.phase_divisor)
        return n


def _cdiv(a: int, b: int) -> int:
    """C integer division (truncating towards zero)."""
    q = abs(a) // abs(b)
    return q if (a >= 0) == (b > 0) else -q


def side_bytes(index_ticks: int, rate_bps: int) -> int:
    """Bytes per side for one revolution: cells at the nominal rate, rounded up to bytes."""
    cells = (index_ticks * 2 * rate_bps + FLUX_CLOCK_HZ // 2) // FLUX_CLOCK_HZ
    return (cells + 7) // 8


def flux_to_cells(samples: list[int], rate_bps: int, nbytes: int) -> bytes:
    """Flux timestamps (ticks since the index) -> nbytes of cells: PLL per interval, zeros for
    the gap before the index; a longer revolution is cut, a shorter one padded."""
    limit = nbytes * 8
    buf = bytearray(nbytes)
    pll = Pll(rate_bps)
    pos, prev = 0, 0
    for t in samples:
        n = pll.cells(t - prev)
        prev = t
        pos += n
        if pos > limit:
            break
        buf[(pos - 1) >> 3] |= 1 << ((pos - 1) & 7)
    return bytes(buf)


def guess_rate(samples: list[int]) -> int:
    """Data rate from the shortest common interval (= two cells): 10th percentile of the
    intervals snapped to 250/300/500/1000 kbit/s."""
    deltas = sorted(b - a for a, b in zip(samples, samples[1:], strict=False) if b > a)
    if not deltas:
        return 250_000
    two = deltas[len(deltas) // 10]
    return min(RATES, key=lambda r: abs(FLUX_CLOCK_HZ / r - two))


# ---- file -------------------------------------------------------------------------------

def write_hfe(path: str, img: HfeImage) -> None:
    """Side 1 of a cylinder is cut or zero-padded to the length of side 0 (as the firmware
    does); missing tracks get an empty entry."""
    if img.v3_tracks:
        raise ValueError("HFEv3 timing/weak metadata cannot be saved as HFEv1")
    cyls, sides = img.cylinders, img.sides
    if cyls < 1:
        raise ValueError("no tracks")
    hdr = bytearray(b"\xff" * BLOCK)
    hdr[0:8] = b"HXCPICFE"
    hdr[8] = 0
    hdr[9] = cyls
    hdr[10] = sides
    hdr[11] = img.encoding
    struct.pack_into("<HH", hdr, 12, (img.rate_bps + 500) // 1000, img.rpm)
    hdr[16] = interface_mode(img.rate_bps, img.encoding)
    hdr[17] = 1
    struct.pack_into("<H", hdr, 18, 1)
    hdr[20] = 0xFF
    hdr[21] = 0x00 if img.double_step else 0xFF
    lut = bytearray(b"\xff" * BLOCK)
    body = bytearray()
    block = 2
    for c in range(cyls):
        s0 = img.tracks.get((c, 0))
        if s0 is None:
            struct.pack_into("<HH", lut, 4 * c, 0, 0)
            continue
        n = len(s0)
        s1 = (img.tracks.get((c, 1)) or b"")[:n].ljust(n, b"\0")
        blocks = (n + 255) // 256
        for k in range(blocks):
            body += s0[k * 256:(k + 1) * 256].ljust(256, b"\0")
            body += s1[k * 256:(k + 1) * 256].ljust(256, b"\0")
        struct.pack_into("<HH", lut, 4 * c, block, 2 * n)
        block += blocks
    with open(path, "wb") as f:
        f.write(bytes(hdr) + bytes(lut) + bytes(body))


def read_hfe(path: str) -> HfeImage:
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:8] not in (b"HXCPICFE", b"HXCHFEV3"):
        raise ValueError("not an HFE file")
    if len(blob) < BLOCK:
        raise ValueError("truncated HFE header")
    if blob[8] != 0:
        raise ValueError("unsupported HFE revision")
    tracks, sides, enc = blob[9], blob[10], blob[11]
    kbps, rpm = struct.unpack_from("<HH", blob, 12)
    lut_block, = struct.unpack_from("<H", blob, 18)
    img = HfeImage(kbps * 1000, enc, rpm, blob[21] == 0)
    if sides not in (1, 2) or not 1 <= tracks <= 128:
        raise ValueError(f"unusable HFE header: {tracks} tracks, {sides} sides")
    if not 125 <= kbps <= 1000:
        raise ValueError("unusable HFE bitrate")
    table_start = lut_block * BLOCK
    table_end = table_start + 4 * tracks
    if lut_block < 1 or table_end > len(blob):
        raise ValueError("truncated or invalid HFE track table")
    data_start = ((table_end + BLOCK - 1) // BLOCK) * BLOCK
    for c in range(tracks):
        block, length = struct.unpack_from("<HH", blob, lut_block * BLOCK + 4 * c)
        if length < 2:
            continue
        if length % 2 or block * BLOCK < data_start:
            raise ValueError("invalid HFE track extent")
        n = length // 2
        for s in range(sides):
            data = bytearray()
            k = 0
            while len(data) < n:
                off = (block + k) * BLOCK + 256 * s
                end = off + min(256, n - len(data))
                if end > len(blob):
                    raise ValueError("truncated HFE track data")
                data += blob[off:end]
                k += 1
            img.tracks[(c, s)] = bytes(data)
            if blob[:8] == b"HXCHFEV3":
                img.v3_tracks[(c, s)] = parse_hfe_v3(bytes(data))
    return img


@dataclass
class HfeV3Track:
    """Decoded cells and explicit metadata; RAND is not a magnetic write recipe."""
    bits: bytes                   # one 0/1 byte per cell, stream order
    periods: bytes                # cell periods in 1/36 MHz units
    weak: list[tuple[int, int]]    # start cell, cell count
    indexes: list[int]             # cell offsets of every index opcode

    def flux(self) -> tuple[list[int], int]:
        if self.weak:
            raise ValueError("HFEv3 RAND areas require a physical weak-write policy; metadata preserved")
        if len(self.indexes) > 1:
            raise ValueError("HFEv3 multiple index marks require explicit hard-sector mapping")
        start = self.indexes[0] if self.indexes else 0
        elapsed = previous = 0
        deltas = []
        for k in range(len(self.bits)):
            i = (start + k) % len(self.bits)
            elapsed += self.periods[i]
            if self.bits[i]:
                now = (elapsed * FLUX_CLOCK_HZ + 18_000_000) // 36_000_000
                deltas.append(now - previous)
                previous = now
        duration = (elapsed * FLUX_CLOCK_HZ + 18_000_000) // 36_000_000
        if not deltas or duration > 0xffffffff:
            raise ValueError("HFEv3 empty or oversized flux track")
        return deltas, duration


def parse_hfe_v3(raw: bytes) -> HfeV3Track:
    """HFEv3 byte reversal, 36 MHz timing, SKIP and RAND semantics.

    Reference: Greaseweazle image/hfe.py (Keir Fraser, public domain).
    Strictly reject malformed commands, preserving index and weak metadata.
    """
    stream = bytes(int(f'{x:08b}'[::-1], 2) for x in raw)
    bits, periods = bytearray(), bytearray()
    weak, indexes = [], []
    at = rate = 0
    while at < len(stream):
        value = stream[at]; at += 1
        if value == 0xf0:
            continue
        if value == 0xf1:
            indexes.append(len(bits)); continue
        if value == 0xf2:
            if at == len(stream) or not stream[at]:
                raise ValueError("invalid HFEv3 bitrate command")
            rate = stream[at]; at += 1; continue
        skip = 0
        if value == 0xf3:
            if at + 2 > len(stream) or not 1 <= stream[at] <= 7:
                raise ValueError("invalid HFEv3 skip command")
            skip, value = stream[at:at+2]; at += 2
            if value != 0xf4 and value >= (1 << (8-skip)):
                raise ValueError("invalid HFEv3 skipped data bits")
        elif value >= 0xf5:
            raise ValueError("unknown HFEv3 opcode")
        count = 8-skip
        if value == 0xf4:
            if weak and sum(weak[-1]) == len(bits):
                weak[-1] = (weak[-1][0], weak[-1][1]+count)
            else:
                weak.append((len(bits), count))
            value = 0
        bits.extend((value >> k) & 1 for k in range(count-1, -1, -1))
        periods.extend([rate]*count)
    if not bits or not rate:
        raise ValueError("HFEv3 track has no cells or bitrate")
    for k in range(len(periods)):
        if periods[k]:
            break
        periods[k] = rate          # rate wraps from previous revolution
    return HfeV3Track(bytes(bits), bytes(periods), weak, indexes)


def encode_hfe_v3(track: HfeV3Track) -> bytes:
    """Encode explicit cells without quantising timings or losing weak/index metadata."""
    count = len(track.bits)
    if not count or len(track.periods) != count or any(b > 1 for b in track.bits) or 0 in track.periods:
        raise ValueError('invalid HFEv3 cells or periods')
    if track.indexes != sorted(track.indexes) or any(i < 0 or i > count for i in track.indexes):
        raise ValueError('invalid HFEv3 indexes')
    weak = bytearray(count)
    last = 0
    boundaries = {0, count, *track.indexes}
    for start, size in track.weak:
        if start < last or size <= 0 or start + size > count:
            raise ValueError('invalid HFEv3 weak ranges')
        if any(track.bits[start:start+size]):
            raise ValueError('weak cell placeholders must be zero')
        weak[start:start+size] = b'\1' * size
        boundaries.update((start, start+size));last = start+size
    boundaries.update(i for i in range(1, count) if track.periods[i] != track.periods[i-1])
    points = sorted(boundaries)
    out = bytearray();rate = 0
    indexes = {}
    for i in track.indexes:
        indexes[i] = indexes.get(i, 0) + 1
    for start, end in zip(points, points[1:], strict=False):
        out.extend([0xf1]*indexes.get(start, 0))
        if rate != track.periods[start]:
            rate = track.periods[start];out.extend((0xf2, rate))
        at = start
        while at < end:
            n = min(8, end-at)
            if weak[at]:
                value = 0xf4
            else:
                value = 0
                for b in track.bits[at:at+n]: value = (value << 1) | b
                if n == 8 and value >= 0xf0:
                    n = 7;value >>= 1
            if n < 8: out.extend((0xf3, 8-n))
            out.append(value);at += n
    out.extend([0xf1]*indexes.get(count, 0))
    if len(out) > 32767:
        raise ValueError('HFEv3 track exceeds 16-bit extent capacity')
    return bytes(int(f'{x:08b}'[::-1], 2) for x in out)


def write_hfe_v3(path: str, img: HfeImage) -> None:
    """Export explicit v3 tracks; side padding uses NOP, never extra bitcells."""
    keys = set(img.v3_tracks)
    if not keys or set(img.tracks) - keys or any(not 0 <= c < 128 or h not in (0, 1) for c, h in keys):
        raise ValueError('HFEv3 export needs explicit v3 metadata for every track')
    if not 125000 <= img.rate_bps <= 1000000 or not 0 <= img.rpm <= 65535:
        raise ValueError('invalid HFE header timing')
    cyls = max(c for c, _ in keys)+1;sides = max(h for _, h in keys)+1
    encoded = {key: encode_hfe_v3(t) for key, t in img.v3_tracks.items()}
    header = bytearray(b'\xff'*512);header[:8] = b'HXCHFEV3'
    header[8:12] = bytes((0, cyls, sides, img.encoding))
    struct.pack_into('<HH', header, 12, (img.rate_bps+500)//1000, img.rpm)
    header[16:22] = bytes((interface_mode(img.rate_bps,img.encoding), 1, 1, 0, 255, 0 if img.double_step else 255))
    lut = bytearray(512);body = bytearray();block = 2
    for c in range(cyls):
        if not any((c,h) in keys for h in range(sides)): continue
        if any((c,h) not in keys for h in range(sides)):
            raise ValueError('missing side in HFEv3 cylinder')
        n = max(len(encoded[c,h]) for h in range(sides))
        blocks = (n+255)//256
        if block+blocks > 65535: raise ValueError('HFEv3 image exceeds block address space')
        struct.pack_into('<HH',lut,c*4,block,n*2)
        for b in range(blocks):
            for h in range(2):
                data = encoded.get((c,h),b'')[b*256:(b+1)*256]
                body.extend(data.ljust(256,b'\x0f'))  # reversed NOP
        block += blocks
    with open(path,'wb') as output:
        output.write(header+lut+body)
