"""1541 raw track stitching (ufi_host.nib) against a modelled drive-code chunk source."""
import random
import re
import struct

import pytest

from ufi_host import nib
from ufi_host import protocol2 as P2

GCR_ENCODE = {v: k for k, v in nib.GCR_DECODE.items()}


def gcr_encode(b: bytes) -> bytes:
    out = bytearray()
    for i in range(0, len(b), 4):
        q = b[i:i + 4]
        bits = 0
        for byte in q:
            bits = (bits << 5) | GCR_ENCODE[byte >> 4]
            bits = (bits << 5) | GCR_ENCODE[byte & 15]
        out += bits.to_bytes(5, "big")
    return bytes(out)


def make_track(track: int, sectors: int, tail: int = 90) -> bytes:
    t = bytearray()
    for s in range(sectors):
        hdr = bytes([0x08, s ^ track ^ 0x41 ^ 0x42, s, track, 0x42, 0x41, 0x0F, 0x0F])
        t += b"\xff" * 5 + gcr_encode(hdr) + b"\x55" * 9
        data = bytes(((s * 7 + i) & 0xFF) for i in range(256))
        chk = 0
        for v in data:
            chk ^= v
        t += b"\xff" * 5 + gcr_encode(bytes([0x07]) + data + bytes([chk, 0, 0])) + b"\x55" * 8
    t += b"\x55" * tail
    return bytes(t)


class FakeDrive:
    """What the firmware IEC_NIB / IEC_MEM path returns for a modelled disk."""

    def __init__(self, tracks: dict[int, bytes], head: int = 36, reversed_stepper=False):
        self.tracks = tracks
        self.head = head                # real half track position
        self.cur = 0                    # drive code counter ($0308), 0 = do not move
        self.reversed = reversed_stepper
        self.commands = []
        self.calls = 0

    def iec_mem_write(self, addr, data):
        if addr == 0x0308:
            self.cur = data[0]

    def iec_command(self, cmd):
        self.commands.append(bytes(cmd))

    def _move(self, target, flags):
        if self.cur:
            delta = target - self.cur
            if self.reversed != bool(flags & P2.NIB_REVERSE):
                delta = -delta
            self.head = max(2, min(86, self.head + delta))
            self.cur = target

    def _chunks(self, t):
        ends = [m.end() for m in re.finditer(rb"\xff+", t)]
        if not ends:
            return None
        # origin: the sync after the longest stretch between two syncs (circular)
        starts = [m.start() for m in re.finditer(rb"\xff+", t)]
        gaps = [(starts[(i + 1) % len(starts)] - ends[i]) % len(t) for i in range(len(ends))]
        k = gaps.index(max(gaps))
        origin = (k + 1) % len(ends)
        return [ends[(origin + i) % len(ends)] for i in range(len(ends))]

    def iec_nib(self, halftrack, sync=0, mode=0, flags=0, device=0):
        self.calls += 1
        self._move(halftrack, flags)
        t = self.tracks.get(self.head)
        if mode == 2:
            return P2.NibChunk(0, 0, 0, self.cur, bytes(512))
        if t is None:                   # unformatted: noise, no syncs
            r = random.Random(self.head)
            noise = bytes(r.choice(range(255)) for _ in range(512))
            return P2.NibChunk(3 if mode == 1 else 1, 0, 0, self.cur, noise)
        ends = self._chunks(t)
        pos = ends[sync % len(ends)] if mode == 0 else random.Random(self.calls).randrange(len(t))
        data = bytes((t * 3)[pos:pos + 512])
        return P2.NibChunk(3 if mode == 1 else 0, 12, max(len(t) // 20, 300), self.cur, data)


TRACKS = {36: make_track(18, 19), 38: make_track(19, 19), 34: make_track(17, 21),
          2: make_track(1, 21), 4: make_track(2, 21), 70: make_track(35, 17)}


def expected(t: bytes) -> bytes:
    return t[5:] + t[:5]                # starts after the origin sync, ends with it


def test_gcr_roundtrip_and_header():
    hdr = bytes([0x08, 1, 2, 18, 0x42, 0x41, 0x0F, 0x0F])
    assert nib.gcr_decode(gcr_encode(hdr)) == hdr
    assert nib.header_track(gcr_encode(hdr) + b"\x55" * 9) == 18
    assert nib.header_track(b"\x55" * 10) is None


def test_stitch_formatted_track():
    d = FakeDrive(TRACKS)
    rd = nib.NibReader(d, log=lambda *_: None)
    rd.set_cur(36)
    r = rd.read_halftrack(36)
    assert r.data is not None and r.note == ""
    assert r.data == expected(TRACKS[36])
    assert 15 <= r.chunks <= 30


def test_calibrate_reads_position_and_direction():
    d = FakeDrive(TRACKS, head=36)
    rd = nib.NibReader(d, log=lambda *_: None)
    rd.calibrate()
    assert rd.cur == 38 and d.head == 38 and rd.flags == 0


def test_calibrate_detects_reversed_stepper():
    d = FakeDrive(TRACKS, head=36, reversed_stepper=True)
    rd = nib.NibReader(d, log=lambda *_: None)
    rd.calibrate()
    assert rd.flags & P2.NIB_REVERSE and rd.cur == d.head == 34
    r = rd.read_halftrack(2)
    assert d.head == 2 and r.data == expected(TRACKS[2])


def test_unformatted_halftrack_gives_raw_chunks():
    d = FakeDrive(TRACKS)
    rd = nib.NibReader(d, log=lambda *_: None)
    rd.set_cur(36)
    r = rd.read_halftrack(37)
    assert r.data is None and r.note == "no sync" and len(r.raw) == 24


def test_read_disk_and_g64(tmp_path):
    d = FakeDrive(TRACKS, head=36)
    rd = nib.NibReader(d, log=lambda *_: None)
    rd.calibrate()
    res = rd.read_disk(tracks=2)
    assert [r.halftrack for r in res] == [2, 4] and all(r.data for r in res)
    assert d.commands[-1] == b"I0"
    out = tmp_path / "d.g64"
    nib.write_g64(str(out), res)
    b = out.read_bytes()
    assert b[:8] == b"GCR-1541" and b[9] == 84 and struct.unpack_from("<H", b, 10)[0] == 7928
    assert len(b) == 12 + 84 * 8 + 2 * (2 + 7928)
    off = struct.unpack_from("<I", b, 12)[0]
    n = struct.unpack_from("<H", b, off)[0]
    assert b[off + 2:off + 2 + n] == expected(TRACKS[2])
    assert struct.unpack_from("<I", b, 12 + 84 * 4)[0] == 3          # speed zone track 1
    assert struct.unpack_from("<I", b, 12 + 4 * 4)[0] == 0           # half track 6 missing
