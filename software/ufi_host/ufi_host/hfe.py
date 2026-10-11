"""HxC Floppy Emulator HFE v1 images (firmware 1.17 writes and reads them as well).

Layout: 512-byte header ("HXCPICFE", revision 0, tracks, sides, encoding, bit rate kbit/s,
rpm, interface mode, track table block, write allowed, single step), track table in block
1 (per cylinder: offset in 512-byte blocks, length in bytes for both sides), track data in
512-byte blocks holding 256 bytes of side 0 followed by 256 bytes of side 1.  One bit per
MFM cell, LSB of every byte first, exactly one revolution per track.

The flux <-> cell conversions are the integer algorithms of firmware/src/ufi_image.c, so a
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

    def __init__(self, rate_bps: int):
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
        self.period = min(self.pmax, max(self.pmin, self.period + _cdiv(err, n * 16)))
        self.phase = _cdiv(err, 2)
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
    deltas = sorted(b - a for a, b in zip(samples, samples[1:]) if b > a)
    if not deltas:
        return 250_000
    two = deltas[len(deltas) // 10]
    return min(RATES, key=lambda r: abs(FLUX_CLOCK_HZ / r - two))


# ---- file -------------------------------------------------------------------------------

def write_hfe(path: str, img: HfeImage) -> None:
    """Side 1 of a cylinder is cut or zero-padded to the length of side 0 (as the firmware
    does); missing tracks get an empty entry."""
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
    if blob[:8] != b"HXCPICFE":
        raise ValueError("not an HFE file")
    tracks, sides, enc = blob[9], blob[10], blob[11]
    kbps, rpm = struct.unpack_from("<HH", blob, 12)
    lut_block, = struct.unpack_from("<H", blob, 18)
    img = HfeImage(kbps * 1000, enc, rpm, blob[21] == 0)
    if sides not in (1, 2) or not 1 <= tracks <= 128:
        raise ValueError(f"unusable HFE header: {tracks} tracks, {sides} sides")
    for c in range(tracks):
        block, length = struct.unpack_from("<HH", blob, lut_block * BLOCK + 4 * c)
        if length < 2:
            continue
        n = length // 2
        for s in range(sides):
            data = bytearray()
            k = 0
            while len(data) < n:
                off = (block + k) * BLOCK + 256 * s
                data += blob[off:off + min(256, n - len(data))]
                k += 1
            img.tracks[(c, s)] = bytes(data)
    return img
