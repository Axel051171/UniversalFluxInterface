"""Host-side Apple II 5.25-inch GCR decoding and .dsk/.po assembly."""
from types import SimpleNamespace

import pytest

from ufi_host import apple

CELL = 1100     # 4 us at 275 MHz


class Track:
    """Test-only 6-and-2 encoder (inverse of the decoder, written from the layout docs)."""

    def __init__(self, marks=None):
        self.bits = []
        self.m = marks or apple.Options()

    def byte(self, v, trailing_zeros=0):
        self.bits += [(v >> b) & 1 for b in range(7, -1, -1)] + [0] * trailing_zeros

    def sync(self, n):
        for _ in range(n):
            self.byte(0xff, 2)

    def ff44(self, v):
        self.byte((v >> 1) | 0xaa)
        self.byte(v | 0xaa)

    def sector(self, volume, track, sector, data, faults=0):
        self.sync(10)
        for b in self.m.addr_prologue:
            self.byte(b)
        for v in (volume, track, sector, volume ^ track ^ sector ^ (1 if faults & 1 else 0)):
            self.ff44(v)
        for b in self.m.addr_epilogue:
            self.byte(b)
        self.byte(0xeb)
        self.sync(6)
        for b in self.m.data_prologue:
            self.byte(b)
        x = [0] * 342
        for i in range(86):
            parts = []
            for j in (i, i + 86, i + 172):
                a = data[j] & 3 if j < 256 else 0
                parts.append(((a & 1) << 1) | (a >> 1))
            x[i] = parts[0] | (parts[1] << 2) | (parts[2] << 4)
        for i in range(256):
            x[86 + i] = data[i] >> 2
        prev = 0
        for v in x:
            self.byte(apple.SYMBOLS[v ^ prev])
            prev = v
        self.byte(apple.SYMBOLS[(prev ^ (1 if faults & 2 else 0)) & 63])
        self.byte(self.m.data_epilogue[0])
        self.byte(self.m.data_epilogue[1] ^ (1 if faults & 4 else 0))
        self.byte(0xeb)
        self.sync(4)

    def deltas(self, speed=1.0):
        out, run = [], 0
        for b in self.bits:
            run += 1
            if b:
                out.append(int(round(run * CELL * speed)))
                run = 0
        return out


def sector_data(s):
    return bytes((s * 37 + i * 11 + (i >> 3)) & 0xff for i in range(256))


def test_decode_clean_track_and_orders():
    t = Track()
    for s in range(16):
        t.sector(254, 17, s, sector_data(s))
    dec = apple.decode_nibbles(apple.nibbles(t.deltas()))
    assert len(dec.sectors) == 16 and all(s.ok for s in dec.sectors)
    assert [s.sector for s in dec.sectors] == list(range(16)) and dec.sectors[3].track == 17 and dec.sectors[3].volume == 254
    assert dec.sectors[5].data == sector_data(5) and not dec.header_errors and not dec.data_errors
    img = apple.DiskImage(35, "dos33")
    assert img.add_track(17, dec) == 16 and not img.missing and img.volumes == {254}
    # DOS 3.3 order: physical sector 1 lands in logical slot 7, ProDOS: slot 8
    assert img.data[(17 * 16 + 7) * 256:(17 * 16 + 8) * 256] == sector_data(1)
    po = apple.DiskImage(35, "prodos")
    po.add_track(17, dec)
    assert po.data[(17 * 16 + 8) * 256:(17 * 16 + 9) * 256] == sector_data(1)
    assert len(img.data) == 143360 and apple.order_for_name("x.po", None) == "prodos" and apple.order_for_name("x.dsk", None) == "dos33"


def test_faults_flags_and_marks():
    t = Track()
    t.sector(254, 3, 0, sector_data(0), 1)
    t.sector(254, 3, 1, sector_data(1), 2)
    t.sector(254, 3, 2, sector_data(2), 4)
    t.sector(254, 3, 3, sector_data(3), 0)
    deltas = t.deltas()
    dec = apple.decode_nibbles(apple.nibbles(deltas))
    assert [s.sector for s in dec.sectors] == [1, 2, 3] and [s.ok for s in dec.sectors] == [False, False, True]
    assert dec.sectors[0].flags & apple.SF_DATA_CHECKSUM and dec.sectors[1].flags & apple.SF_EPILOGUE
    assert dec.header_errors == 1 and dec.data_errors == 2
    tolerant = apple.Options(flags=apple.KEEP_BAD_ADDR_CHECKSUM | apple.IGNORE_EPILOGUES | apple.DATA_WITHOUT_CHECKSUM)
    dec = apple.decode_nibbles(apple.nibbles(deltas), tolerant)
    assert [s.sector for s in dec.sectors] == [0, 1, 2, 3] and all(s.ok for s in dec.sectors)
    assert dec.sectors[0].flags & apple.SF_ADDR_CHECKSUM and dec.sectors[0].data == sector_data(0)
    img = apple.DiskImage(35, "dos33")
    assert img.add_track(3, dec) == 4 and len(img.missing) == 12 and not img.weak
    # protection marks decode only with matching options; 6 % slow drive still decodes
    marks = apple.Options(b"\xd4\xaa\x96", b"\xd5\xaa\xb5", b"\xde\xab", b"\xde\xaa")
    t = Track(marks)
    for s in range(16):
        t.sector(1, 5, s, sector_data(s))
    assert not apple.decode_nibbles(apple.nibbles(t.deltas())).sectors
    dec = apple.decode_nibbles(apple.nibbles(t.deltas(1.06)), marks)
    assert len(dec.sectors) == 16 and all(s.ok for s in dec.sectors) and dec.sectors[9].data == sector_data(9)
    # data without header and any-track limits
    t = Track()
    t.sector(1, 45, 20, sector_data(0))
    assert not apple.decode_nibbles(apple.nibbles(t.deltas())).sectors
    dec = apple.decode_nibbles(apple.nibbles(t.deltas()), apple.Options(flags=apple.ANY_TRACK))
    assert dec.sectors and dec.sectors[0].track == 45 and dec.sectors[0].sector == 20
    t = Track()
    t.sector(1, 3, 0, sector_data(0), 1)
    dec = apple.decode_nibbles(apple.nibbles(t.deltas()), apple.Options(flags=apple.DATA_WITHOUT_HEADER))
    assert dec.sectors and dec.sectors[0].sector == 255 and dec.sectors[0].ok
    with pytest.raises(ValueError):
        apple.Options(addr_prologue=b"\x55\xaa\x96").check()
    with pytest.raises(ValueError):
        apple.Options(flags=0x40).check()


def test_decode_capture_merges_revolutions():
    t = Track()
    for s in range(16):
        t.sector(254, 0, s, sector_data(s), 2 if s == 4 else 0)
    bad = t.deltas()
    t = Track()
    for s in range(16):
        t.sector(254, 0, s, sector_data(s))
    good = t.deltas()

    def rev(deltas):
        ts, acc = [], 0
        for d in deltas:
            acc += d
            ts.append(acc)
        return SimpleNamespace(samples=ts, index_ticks=acc + 1000)

    cap = SimpleNamespace(revolutions=[rev(bad), rev(good)])
    dec = apple.decode_capture(cap)
    img = apple.DiskImage(35)
    assert img.add_track(0, dec) == 16 and not img.weak and dec.data_errors == 1
    assert img.data[(0 * 16 + apple.DOS33_ORDER[4]) * 256:(0 * 16 + apple.DOS33_ORDER[4] + 1) * 256] == sector_data(4)
