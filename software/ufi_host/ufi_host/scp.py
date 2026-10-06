"""SuperCard Pro (.scp) flux images.

Layout (SCP spec v2.x): 16-byte header ("SCP", version, disk type, revolutions, start/end
track, flags, cell width 0 = 16 bit, heads 0 = both, resolution 0 = 25 ns, u32 checksum
over everything from offset 0x10), 168 u32 track offsets, then per track "TRK" + track
number, one (index time, cell count, data offset) triple per revolution and the cell
times as u16 big endian (0 = add 65536).  SCP track number = cylinder * 2 + head.
Conversion from the 275 MHz UFI tick to 25 ns (40 MHz) is done on absolute times, so
rounding never accumulates.
"""
from __future__ import annotations

import struct
from dataclasses import dataclass

from .protocol import FLUX_CLOCK_HZ

SCP_HZ = 40_000_000
N_TRACKS = 168
FLAG_INDEX = 0x01
DISK_OTHER = 0x80


def ticks_to_scp(samples: list[int]) -> list[int]:
    """UFI flux timestamps (ticks since index) -> SCP cell times (25 ns units)."""
    cells, prev = [], 0
    for t in samples:
        now = (t * SCP_HZ + FLUX_CLOCK_HZ // 2) // FLUX_CLOCK_HZ
        if now > prev:
            cells.append(now - prev)
            prev = now
    return cells


def scp_to_ticks(cells: list[int]) -> list[int]:
    """SCP cell times -> UFI write deltas (275 MHz ticks), rounding on absolute time."""
    deltas, acc, prev = [], 0, 0
    for c in cells:
        acc += c
        now = (acc * FLUX_CLOCK_HZ + SCP_HZ // 2) // SCP_HZ
        deltas.append(now - prev)
        prev = now
    return deltas


@dataclass
class ScpRevolution:
    index_time: int                  # 25 ns units
    cells: list[int]


def _encode_cells(cells: list[int]) -> bytes:
    out = bytearray()
    for c in cells:
        while c > 0xFFFF:
            out += b"\x00\x00"
            c -= 0x10000
        out += struct.pack(">H", c)
    return bytes(out)


def write_scp(path: str, tracks: dict[int, list[ScpRevolution]], revolutions: int,
              heads: int = 0) -> None:
    """tracks: SCP track number -> revolutions (all tracks with the same count)."""
    nums = sorted(tracks)
    body = bytearray()
    offsets = [0] * N_TRACKS
    base = 0x10 + 4 * N_TRACKS
    for n in nums:
        revs = tracks[n][:revolutions]
        tdh = bytearray(b"TRK" + bytes([n]))
        data = bytearray()
        rev_off = 4 + 12 * len(revs)
        for r in revs:
            enc = _encode_cells(r.cells)
            tdh += struct.pack("<III", r.index_time, len(enc) // 2, rev_off + len(data))
            data += enc
        offsets[n] = base + len(body)
        body += tdh + data
    table = struct.pack(f"<{N_TRACKS}I", *offsets)
    tail = table + bytes(body)
    checksum = sum(tail) & 0xFFFFFFFF
    header = b"SCP" + bytes([0x22, DISK_OTHER, revolutions, nums[0] if nums else 0,
                             nums[-1] if nums else 0, FLAG_INDEX, 0, heads, 0])
    with open(path, "wb") as f:
        f.write(header + struct.pack("<I", checksum) + tail)


def read_scp(path: str) -> tuple[int, dict[int, list[ScpRevolution]]]:
    """-> (revolutions, track number -> revolutions)."""
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:3] != b"SCP":
        raise ValueError("not an SCP file")
    nrev, start, end, cellw = blob[5], blob[6], blob[7], blob[9]
    if cellw not in (0, 16):
        raise ValueError(f"unsupported cell width {cellw}")
    offsets = struct.unpack_from(f"<{N_TRACKS}I", blob, 0x10)
    tracks = {}
    for n in range(start, end + 1):
        off = offsets[n]
        if not off or blob[off:off + 3] != b"TRK":
            continue
        revs = []
        for r in range(nrev):
            idx, count, doff = struct.unpack_from("<III", blob, off + 4 + 12 * r)
            raw = struct.unpack_from(f">{count}H", blob, off + doff)
            cells, carry = [], 0
            for v in raw:
                if v == 0:
                    carry += 0x10000
                else:
                    cells.append(v + carry)
                    carry = 0
            revs.append(ScpRevolution(idx, cells))
        tracks[n] = revs
    return nrev, tracks
