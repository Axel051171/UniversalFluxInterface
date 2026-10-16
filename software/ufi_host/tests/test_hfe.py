"""HFE images: the cell arithmetic mirrors firmware/src/ufi_image.c (same constants as
firmware/tests/test_image.c), file round trip, rate guess, read-disk to .hfe."""
from __future__ import annotations

import argparse
import random
import struct
import pytest

from ufi_host import cli
from ufi_host import hfe as H
from ufi_host import protocol2 as P2
from ufi_host.protocol import FLUX_CLOCK_HZ


def test_hfe_rejects_unsupported_and_truncated_input(tmp_path):
    path = tmp_path / "bad.hfe"
    blob = bytearray(1536)
    blob[:8] = b"HXCPICFE"
    blob[9:12] = bytes([1, 2, 0])
    struct.pack_into("<H", blob, 12, 253)
    struct.pack_into("<H", blob, 18, 1)
    struct.pack_into("<HH", blob, 512, 2, 512)
    path.write_bytes(blob)
    assert H.read_hfe(path).rate_bps == 253000
    for cut in (8, 511, 513, 1024, 1535):
        path.write_bytes(blob[:cut])
        with pytest.raises(ValueError):
            H.read_hfe(path)
    for offset, value in ((8, 1), (8, 255), (12, 0), (18, 0), (512, 1), (514, 1)):
        damaged = bytearray(blob)
        damaged[offset] = value
        path.write_bytes(damaged)
        with pytest.raises(ValueError):
            H.read_hfe(path)


def test_fit_cell16_matches_firmware_constants():
    assert H.fit_cell16(250_000) == (550 << 16, 1000)
    assert H.fit_cell16(300_000)[0] == 30037333
    assert H.fit_cell16(250_000, 100_000, 55_000_000) == (((550 << 16) * 985 + 500) // 1000, 985)
    assert H.fit_cell16(250_000, 100_000, 50_000_000)[1] == 970
    assert H.fit_cell16(250_000, 50_000, 55_000_000)[1] == 1030
    assert H.fit_cell16(250_000, 99_000, 55_000_000)[1] == 995


def test_bits_to_deltas_lsb_first_and_no_drift():
    assert H.bits_to_deltas(bytes([0x12, 0x80]), 550 << 16) == [1100, 1650, 11 * 550]
    cells = bytes([0x80]) * 15000                      # 120000 cells at 300 kbit/s = one revolution
    d = H.bits_to_deltas(cells, H.fit_cell16(300_000)[0])
    assert len(d) == 15000 and abs(sum(d) - 55_000_000) <= 1


def _pattern(n: int, seed: int) -> tuple[bytes, int]:
    rng = random.Random(seed)
    bits = bytearray()
    pos = 0
    runs = []
    for _ in range(n):
        k = rng.choice((2, 3, 4))
        pos += k
        runs.append(k)
    nbytes = (pos + 7) // 8
    bits = bytearray(nbytes)
    pos = 0
    for k in runs:
        pos += k
        bits[(pos - 1) >> 3] |= 1 << ((pos - 1) & 7)
    return bytes(bits), pos


def test_pll_round_trip_with_jitter_and_drift():
    cells, ncells = _pattern(9000, 7)
    deltas = H.bits_to_deltas(cells, 550 << 16)
    assert len(deltas) == 9000
    rng = random.Random(99)
    samples, t = [], 0
    for d in deltas:
        t += d + d * rng.randint(-4, 4) // 100     # +-4 % on every interval
        samples.append(t)
    nbytes = (ncells + 7) // 8
    assert H.flux_to_cells(samples, 250_000, nbytes) == cells
    slow = [t * 105 // 100 for t in samples]
    assert H.flux_to_cells(slow, 250_000, nbytes) == cells
    # a longer revolution is cut, a shorter one padded with zeros
    assert H.flux_to_cells(samples, 250_000, nbytes - 10) == cells[:nbytes - 10]
    assert H.flux_to_cells(samples, 250_000, nbytes + 10) == cells + bytes(10)


def test_pll_matches_c_division_for_negative_phase():
    pll = H.Pll(250_000)
    assert pll.cells(50 * 550) == 50 and pll.period == pll.nominal   # long gap: no adaptation
    assert pll.cells(1050) == 2                                       # short: phase goes negative
    assert pll.phase < 0
    assert pll.cells(1100) == 2


def test_guess_rate():
    cells, _ = _pattern(3000, 3)
    for rate in H.RATES[:3]:
        d = H.bits_to_deltas(cells, H.fit_cell16(rate)[0])
        samples, t = [], 0
        for x in d:
            t += x
            samples.append(t)
        assert H.guess_rate(samples) == rate


def test_file_round_trip(tmp_path):
    img = H.HfeImage(500_000, H.ENC_AMIGA_MFM, rpm=300, double_step=True)
    img.tracks[(0, 0)] = bytes(range(256)) * 3        # 768 bytes = 3 blocks
    img.tracks[(0, 1)] = b"\x55" * 700                # shorter side 1: padded to 768
    img.tracks[(1, 0)] = b"\xaa" * 10
    img.tracks[(1, 1)] = b"\x0f" * 20                 # longer side 1: cut to 10
    p = tmp_path / "t.hfe"
    H.write_hfe(str(p), img)
    blob = p.read_bytes()
    assert blob[:8] == b"HXCPICFE" and blob[9] == 2 and blob[10] == 2 and blob[11] == 1
    assert blob[12:14] == (500).to_bytes(2, "little") and blob[16] == H.IF_AMIGA_HD and blob[21] == 0
    assert blob[512:516] == bytes([2, 0, 0x00, 0x06])             # block 2, length 1536
    assert blob[516:520] == bytes([5, 0, 20, 0])                  # block 5, length 20
    assert len(blob) == 512 * 6
    back = H.read_hfe(str(p))
    assert back.rate_bps == 500_000 and back.encoding == 1 and back.rpm == 300 and back.double_step
    assert back.tracks[(0, 0)] == img.tracks[(0, 0)]
    assert back.tracks[(0, 1)] == b"\x55" * 700 + bytes(68)
    assert back.tracks[(1, 0)] == b"\xaa" * 10 and back.tracks[(1, 1)] == b"\x0f" * 10


def test_read_disk_to_hfe(tmp_path):
    from test_protocol2 import REVS, FakeV2
    fake = FakeV2(REVS)
    dev = P2.Device2(fake, timeout=0.5)
    out = tmp_path / "d.hfe"
    a = argparse.Namespace(drive="a", tracks=1, sides=2, revs=1, hard_sectors=0, output=str(out),
                           bitrate=250_000, encoding="mfm")
    cli.cmd_read_disk(dev, a)
    img = H.read_hfe(str(out))
    assert img.rate_bps == 250_000 and sorted(img.tracks) == [(0, 0), (0, 1)]
    assert len(img.tracks[(0, 0)]) == len(img.tracks[(0, 1)]) > 0
    assert 200 < img.rpm < 400 and 60.0 * FLUX_CLOCK_HZ / img.rpm > 0
