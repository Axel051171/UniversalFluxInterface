"""1541 raw GCR tracks over the serial bus (firmware 1.14, drive code firmware/tools/iec_nib.s).

The drive code delivers 512-byte chunks that start right after a chosen sync mark counted
from the track origin (the sync after the longest non-sync stretch = header sync of sector 0
on a formatted track).  Chunks are requested every second sync (~350 bytes apart), aligned
on the sync runs they contain and joined until the origin content comes round again.
Output: G64 (one entry per half track, 84 entries).
"""
from __future__ import annotations

import re
import struct
from dataclasses import dataclass, field

from . import protocol2 as P2

SYNC_RUN = re.compile(rb"\xff+")
G64_TRACKS = 84
G64_MAX = 7928
GCR_DECODE = {0x0A: 0, 0x0B: 1, 0x12: 2, 0x13: 3, 0x0E: 4, 0x0F: 5, 0x16: 6, 0x17: 7,
              0x09: 8, 0x19: 9, 0x1A: 10, 0x1B: 11, 0x0D: 12, 0x1D: 13, 0x1E: 14, 0x15: 15}


def gcr_decode(b: bytes) -> bytes | None:
    """5 GCR bytes -> 4 data bytes (None on an invalid code)."""
    out = bytearray()
    for i in range(0, len(b) - 4, 5):
        bits = int.from_bytes(b[i:i + 5], "big")
        nibbles = []
        for k in range(8):
            code = (bits >> (35 - 5 * k)) & 0x1F
            if code not in GCR_DECODE:
                return None
            nibbles.append(GCR_DECODE[code])
        out += bytes((nibbles[2 * j] << 4) | nibbles[2 * j + 1] for j in range(4))
    return bytes(out)


def header_track(data: bytes) -> int | None:
    """Track number from a sector header at the start of a chunk (GCR 0x08 block)."""
    d = gcr_decode(data[:10])
    if d and d[0] == 0x08:
        return d[3]
    return None


def collapse(b: bytes) -> bytes:
    """Sync runs come back with approximate lengths: compare with runs collapsed."""
    return SYNC_RUN.sub(b"\xff", b)


def speed_zone(track: int) -> int:
    return 3 if track <= 17 else 2 if track <= 24 else 1 if track <= 30 else 0


@dataclass
class TrackResult:
    halftrack: int
    data: bytes | None = None       # stitched track (starts after the origin sync)
    raw: list[bytes] = field(default_factory=list)      # unformatted: loose chunks
    chunks: int = 0
    note: str = ""


class NibReader:
    def __init__(self, dev: P2.Device2, device: int = 0, log=print):
        self.dev = dev
        self.device = device
        self.flags = 0
        self.log = log
        self.cur = 0                    # drive position counter (half tracks), 0 = unknown

    # -- drive position ---------------------------------------------------------
    def set_cur(self, halftrack: int) -> None:
        self.dev.iec_mem_write(0x0308, bytes([halftrack]))
        self.cur = halftrack

    def calibrate(self) -> None:
        """Learn the head position from a sector header without moving, then check the
        stepper direction with one step; falls back to a bump to track 1."""
        self.dev.iec_nib(2, 0, 2, self.flags | P2.NIB_REUPLOAD, self.device)   # upload, no move
        self.set_cur(0)
        c = self.dev.iec_nib(2, 0, 0, self.flags, self.device)
        t = header_track(c.data) if c.status == 0 else None
        if t is None or not 1 <= t <= 41:
            self.log("position unknown: bumping to track 1")
            self.set_cur(86)                                    # assume far in: 84 steps out
            self.dev.iec_nib(2, 0, 2, self.flags, self.device)
            self.set_cur(2)
            t = 1
        else:
            self.set_cur(2 * t)
        # direction: one track in must read track t + 1
        target = 2 * t + 2 if t < 35 else 2 * t - 2
        c = self.dev.iec_nib(target, 0, 0, self.flags, self.device)
        t2 = header_track(c.data) if c.status == 0 else None
        if t2 is not None and t2 == (t - 1 if target < 2 * t else t + 1):
            self.cur = target               # the drive counted the step itself
            return
        if t2 is not None and t2 == (t + 1 if target < 2 * t else t - 1):
            self.log("stepper direction reversed: using the other phase order")
            self.flags |= P2.NIB_REVERSE
            self.set_cur(2 * t2)
            return
        raise RuntimeError(f"calibration failed (track {t}, after one step: {t2})")

    # -- one track ------------------------------------------------------------------
    def read_halftrack(self, ht: int, raw_chunks: int = 24) -> TrackResult:
        res = TrackResult(ht)
        stream = bytearray()
        origin = b""
        s = 0
        retries = 0
        while s < 128:
            c = self.dev.iec_nib(ht, s, 0, self.flags, self.device)
            res.chunks += 1
            if c.status in (1, 2):
                res.note = "no sync" if c.status == 1 else "no origin"
                break
            if s == 0:
                stream = bytearray(c.data)
                origin = c.data[:48]
            else:
                ends = [m.end() for m in SYNC_RUN.finditer(stream)]
                if len(ends) < s:               # the stream does not reach sync s yet
                    s = max(1, len(ends))
                    continue
                pos = ends[s - 1]
                overlap = bytes(stream[pos:])
                if not collapse(c.data[:len(overlap) + 16]).startswith(collapse(overlap)):
                    retries += 1
                    if retries > 3:
                        res.note = "chunks do not line up"
                        break
                    continue
                stream += c.data[len(overlap):]
            idx = stream.find(origin, 64)
            if idx > 0:
                res.data = bytes(stream[:idx])  # ends with the origin sync run
                return res
            s += 2
            retries = 0
        if res.data is None and not res.note:
            res.note = "no wrap-around found"
        if res.note in ("no sync", "no origin"):
            for _ in range(raw_chunks):     # unformatted / protection: loose raw chunks
                c = self.dev.iec_nib(ht, 0, 1, self.flags, self.device)
                res.chunks += 1
                if c.status == 3:
                    res.raw.append(c.data)
        return res

    def read_disk(self, tracks: int = 35, halftracks: bool = False) -> list[TrackResult]:
        out = []
        step = 1 if halftracks else 2
        for ht in range(2, 2 * tracks + 1, step):
            r = self.read_halftrack(ht)
            n = len(r.data) if r.data else sum(len(x) for x in r.raw)
            self.log(f"track {ht / 2:4.1f}: {n:5d} bytes, {r.chunks:2d} chunks{(' - ' + r.note) if r.note else ''}")
            out.append(r)
        self.dev.iec_command(b"I0")         # restore the DOS BAM buffer
        return out


def write_g64(path: str, results: list[TrackResult]) -> None:
    by_ht = {r.halftrack: r for r in results}
    hdr = b"GCR-1541" + bytes([0, G64_TRACKS]) + struct.pack("<H", G64_MAX)
    offsets, speeds, body = [], [], bytearray()
    base = len(hdr) + 8 * G64_TRACKS
    for i in range(G64_TRACKS):
        ht = i + 2
        r = by_ht.get(ht)
        data = r.data if r and r.data else (b"".join(r.raw)[:G64_MAX] if r and r.raw else b"")
        if not data:
            offsets.append(0)
            speeds.append(0)
            continue
        data = data[:G64_MAX]
        offsets.append(base + len(body))
        speeds.append(speed_zone(ht // 2))
        body += struct.pack("<H", len(data)) + data + bytes(G64_MAX - len(data))
    with open(path, "wb") as f:
        f.write(hdr + b"".join(struct.pack("<I", o) for o in offsets)
                + b"".join(struct.pack("<I", v) for v in speeds) + body)
