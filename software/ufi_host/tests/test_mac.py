"""Macintosh 400K/800K GCR and FDHD helpers on the host (ufi_host.mac): symbol payload,
interleave, track encoder/decoder round trip, raw and DiskCopy 4.2 images; MAC 0x29 and the
read-mac / write-mac commands against a byte-level fake of a v1.0 board with a Mac drive."""
import random
import struct

import pytest
from test_bringup import FakeV10
from test_protocol2 import decode_write
from ufi_host import cli, mac
from ufi_host import protocol2 as P2


def test_zones_and_geometry():
    assert [mac.sectors_per_track(t) for t in (0, 15, 16, 31, 32, 48, 64, 79)] == [12, 12, 11, 11, 10, 9, 8, 8]
    assert mac.blocks(1) * 512 == 409600 and mac.blocks(2) * 512 == 819200
    assert mac.block_index(0, 0, 0, 2) == 0 and mac.block_index(0, 1, 0, 2) == 12
    assert mac.block_index(1, 0, 3, 2) == 27 and mac.block_index(79, 1, 7, 2) == 1599
    assert mac.block_index(79, 0, 7, 1) == 799


def test_interleave():
    assert mac.interleave(12) == [0, 6, 1, 7, 2, 8, 3, 9, 4, 10, 5, 11]
    assert mac.interleave(8) == [0, 4, 1, 5, 2, 6, 3, 7]
    for n in range(8, 13):
        assert sorted(mac.interleave(n)) == list(range(n))


def test_payload_round_trip_and_checksum():
    rng = random.Random(7)
    for _ in range(20):
        data = bytes(rng.randrange(256) for _ in range(524))
        sym = mac.encode_payload(data)
        assert len(sym) == 703 and all(0 <= s < 64 for s in sym)
        out, ok = mac.decode_payload(sym)
        assert ok and out == data
        bad = list(sym)
        bad[rng.randrange(699)] ^= 1
        assert not mac.decode_payload(bad)[1]
    with pytest.raises(ValueError):
        mac.encode_payload(bytes(512))


def _sectors(track, side, seed=1):
    rng = random.Random(seed * 1000 + track * 2 + side)
    return {s: bytes(rng.randrange(256) for _ in range(524)) for s in range(mac.sectors_per_track(track))}


@pytest.mark.parametrize("track,side", [(0, 0), (15, 1), (16, 0), (40, 1), (55, 0), (79, 1)])
def test_track_round_trip(track, side):
    secs = _sectors(track, side)
    deltas = mac.encode_track(track, side, secs, mac.FORMAT_800K)
    rev_ticks = 60 / mac.ZONE_RPM[mac.zone(track)] * mac.FLUX_CLOCK_HZ
    assert sum(deltas) <= rev_ticks * 0.97                     # fits one revolution at the zone speed
    assert min(deltas) == mac.CELL_TICKS and max(deltas) == 3 * mac.CELL_TICKS
    for scale, jitter in ((1.0, 0), (1.03, 25), (0.97, 25)):  # drive speed and bit cell drift
        rng = random.Random(track)
        noisy = [max(1, round(d * scale) + rng.randint(-jitter, jitter)) for d in deltas]
        dec = mac.decode_nibbles(mac.nibbles(noisy))
        best = dec.best()
        assert not dec.data_errors and not dec.header_errors
        assert {k[2] for k in best} == set(secs)
        for s, data in secs.items():
            got = best[(track, side, s)]
            assert got.ok and got.data == data and got.fmt == mac.FORMAT_800K


def _stream(cells):
    deltas, run = [], 0
    for c in mac.SYNC * 20 + cells + mac.SYNC * 4:
        run += 1
        if c:
            deltas.append(run * mac.CELL_TICKS)
            run = 0
    return mac.nibbles(deltas)


def test_decoder_flags_damage():
    secs = _sectors(3, 0)
    stream = _stream(mac.encode_sector(3, 0, 5, secs[5], mac.FORMAT_800K))
    dec = mac.decode_nibbles(stream)
    assert [s.sector for s in dec.sectors] == [5] and dec.sectors[0].ok and dec.sectors[0].data == secs[5]
    hdr = stream.index(mac.ADDR_MARK)
    data = stream.index(mac.DATA_MARK)
    damaged = bytearray(stream)
    damaged[hdr + 4] = mac.apple.SYMBOLS[6]                      # header sector number: checksum fails
    dec = mac.decode_nibbles(bytes(damaged))
    assert not dec.sectors and dec.header_errors == 1
    damaged = bytearray(stream)
    damaged[data + 100] = mac.apple.SYMBOLS[(mac.apple.DECODE[damaged[data + 100]] + 1) & 63]   # data symbol
    dec = mac.decode_nibbles(bytes(damaged))
    assert len(dec.sectors) == 1 and not dec.sectors[0].ok and dec.sectors[0].flags & mac.SF_DATA_CHECKSUM
    damaged = bytearray(stream)
    damaged[data + 3] = mac.apple.SYMBOLS[4]                     # data field of another sector
    dec = mac.decode_nibbles(bytes(damaged))
    assert dec.sectors[0].flags & mac.SF_SECTOR_MISMATCH and not dec.sectors[0].ok
    damaged = bytearray(stream)
    damaged[data + 3 + 704 + 1] = 0xab                           # trailer DE AB
    dec = mac.decode_nibbles(bytes(damaged))
    assert dec.sectors[0].flags == mac.SF_TRAILER


def test_encode_refuses_bad_tracks():
    with pytest.raises(ValueError):
        mac.encode_track(0, 0, {0: bytes(524)}, mac.FORMAT_800K)


def test_dc42_checksum():
    assert mac.dc42_checksum(b"") == 0
    assert mac.dc42_checksum(b"\x00\x01\x00\x02") == 0x40000001
    assert mac.dc42_checksum(b"\x00\x01\x00\x02\x07") == 0x40000001   # odd tail byte ignored


def test_images_round_trip(tmp_path):
    img = mac.GcrImage(2)
    dec = mac.TrackDecode()
    secs = _sectors(20, 1)
    for s, data in secs.items():
        dec.sectors.append(mac.Sector(20, 1, s, mac.FORMAT_800K, data, s != 4))
    assert img.add_track(20, 1, dec) == 11 and img.weak == [(20, 1, 4)]
    assert img.add_track(21, 0, mac.TrackDecode()) == 0 and len(img.missing) == 11
    assert img.sector(20, 1, 9) == secs[9]
    raw, dc = tmp_path / "d.img", tmp_path / "d.dc42"
    img.write(str(raw))
    img.write(str(dc), name="Test")
    assert raw.stat().st_size == 819200 and dc.stat().st_size == 84 + 819200 + 1600 * 12
    head = dc.read_bytes()[:84]
    assert head[0] == 4 and head[1:5] == b"Test" and head[80:84] == b"\x01\x22\x01\x00"
    back = mac.GcrImage.load(str(dc))
    assert back.sides == 2 and back.sector(20, 1, 9) == secs[9]
    back = mac.GcrImage.load(str(raw))
    assert back.sector(20, 1, 9)[12:] == secs[9][12:] and back.sector(20, 1, 9)[:12] == bytes(12)
    corrupt = bytearray(dc.read_bytes())
    corrupt[84 + 5] ^= 1
    (tmp_path / "bad.dc42").write_bytes(bytes(corrupt))
    with pytest.raises(ValueError):
        mac.GcrImage.load(str(tmp_path / "bad.dc42"))
    (tmp_path / "odd.img").write_bytes(bytes(1000))
    with pytest.raises(ValueError):
        mac.GcrImage.load(str(tmp_path / "odd.img"))


def test_mfm_image():
    img = mac.MfmImage()
    sectors = {(5, 1, r, 2): (0xfb, bytes([r]) * 512) for r in range(1, 19) if r != 7}
    assert img.add_track(5, 1, sectors) == 17 and img.missing == [(5, 1, 7)]
    off = ((5 * 2 + 1) * 18 + 2) * 512
    assert img.data[off:off + 512] == bytes([3]) * 512 and len(img.data) == 1474560


# ---- MAC 0x29 and the CLI against a v1.0 board fake ------------------------------------------

def _samples(deltas):
    out, t = [], 0
    for d in deltas:
        t += d
        out.append(t)
    return out


class FakeMac(FakeV10):
    """FakeV10 with a Macintosh drive on J23: MAC 0x29 answers the 15-byte status block (op 3 with the
    tachometer tail at the zone speed), SEEK/SIDE/MOTOR/RECAL move the model, READ returns the GCR track
    under the head as two no-index slices, WRITE stores the received intervals as that track."""

    def __init__(self, kind=2, **kw):
        super().__init__(**kw)
        self.lock = False
        self.kind, self.disk, self.wprot = kind, True, False
        self.track = self.side = 0
        self.mac_motor = False
        self.flux: dict[tuple[int, int], list[int]] = {}
        self.mac_ops: list[bytes] = []
        self.pending_write = None

    def status_block(self) -> bytes:
        flags = (1 if self.disk else 0) | (2 if self.wprot else 0) | (4 if self.mac_motor else 0) | \
            (8 if self.mac_motor and self.disk else 0) | (16 if self.track == 0 else 0)
        zone = mac.zone(self.track)
        rpm = 100 * mac.ZONE_RPM[zone]
        return struct.pack("<BBBBHBB", 1, self.kind, flags, 2 | (4 if self.mac_motor else 0), 0x5A5A, self.track,
                           self.side) + bytes([zone]) + struct.pack("<HHH", rpm, rpm if self.mac_motor else 0, 0)

    def _handle(self, f: P2.Frame):
        if f.type == P2.T_DATA:
            super()._handle(f)
            if self.pending_write and self.write_req is None:      # END sent: the track is on the disk
                self.flux[self.pending_write] = decode_write(bytes(self.write_code))
                self.pending_write = None
            return
        if f.type != P2.T_REQUEST:
            return super()._handle(f)
        cmd, a = f.cmd, f.payload
        if cmd == P2.MAC:
            self.requests.append(f)
            self.mac_ops.append(bytes(a))
            op = a[0] if a else 0
            if self.drive != 11:
                self._frame(P2.T_RESPONSE, cmd, f.seq, P2.NO_DRIVE)
                return
            tail = b""
            if op == 2:
                self.disk, self.mac_motor = False, False
            elif op == 3:
                self.mac_motor = True
                p = round(60 / mac.ZONE_RPM[mac.zone(self.track)] * 275_000_000)
                tail = bytes([0]) + struct.pack("<III", p, p, p)
            self._frame(P2.T_RESPONSE, cmd, f.seq, 0, self.status_block() + tail)
            return
        if cmd == P2.SEEK:
            self.track = a[0]
        elif cmd == P2.SIDE:
            self.side = a[0]
        elif cmd == P2.MOTOR:
            self.mac_motor = bool(a[0])
        elif cmd == P2.RECAL:
            self.track = 0
        elif cmd == P2.READ:
            self.track, self.side = a[0], a[1]
            d = self.flux.get((a[0], a[1]))
            self.revs = [(sum(d) + 5000, _samples(d))] * 2 if d else [(55_000_000, [1000, 2000])] * 2
        elif cmd == P2.WRITE:
            self.track, self.side = a[0], a[1]
            if self.wprot:
                self.requests.append(f)
                self._frame(P2.T_RESPONSE, cmd, f.seq, P2.WRITE_PROTECTED)
                return
            self.pending_write = (a[0], a[1])
        super()._handle(f)


def _disk(fake, sides, seed=3, tracks=range(mac.TRACKS)):
    img = mac.GcrImage(sides)
    fmt = mac.FORMAT_800K if sides == 2 else mac.FORMAT_400K
    for t in tracks:
        for s in range(sides):
            secs = _sectors(t, s, seed)
            for r, data in secs.items():
                img.put(t, s, r, data)
            fake.flux[(t, s)] = mac.encode_track(t, s, secs, fmt)
    return img


def test_device_mac_ops():
    fake = FakeMac(kind=3)
    dev = P2.Device2(fake)
    dev.select("mac")
    st = dev.mac_status()
    assert st.kind_name == "FDHD" and st.disk and not st.write_protected and st.track == 0 and st.zone == 0
    assert st.target_rpm == 394 and st.sense == 0x5A5A and "FDHD drive" in str(st)
    fake.track = 40
    t = dev.mac_tach(2)
    assert t.verdict == 0 and t.tach_rpm == 472 and t.periods[1] == round(60 / 472 * 275_000_000)
    assert dev.mac_command(4).kind == 3 and fake.mac_ops[-1] == bytes([1, 4])
    assert dev.mac_pwm(250).kind == 3 and fake.mac_ops[-1] == bytes([5, 250, 0])
    assert dev.mac_pwm_tune().kind == 3 and fake.mac_ops[-1] == bytes([4, 0xFF])
    assert not dev.mac_eject().disk and fake.mac_ops[-1] == bytes([2])
    for bad in (lambda: dev.mac_command(16), lambda: dev.mac_pwm(1001), lambda: dev.mac_pwm_tune(5),
                lambda: dev.mac_tach(51)):
        with pytest.raises(ValueError):
            bad()
    dev.select("a")
    with pytest.raises(P2.DeviceError) as e:
        dev.mac_status()
    assert e.value.status == P2.NO_DRIVE
    with pytest.raises(ValueError):
        P2.MacStatus.unpack(bytes([2]) + bytes(14))


def _run(monkeypatch, fake, argv):
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    return cli.main(argv)


def test_read_mac_800k_dc42(monkeypatch, tmp_path, capsys):
    fake = FakeMac(kind=2)
    img = _disk(fake, 2, tracks=(0, 17, 33, 50, 79))         # the other tracks are unformatted
    out = tmp_path / "disk.dc42"
    assert _run(monkeypatch, fake, ["read-mac", "-o", str(out), "--speed", "--retries", "1", "-r", "1"]) == 1
    text = capsys.readouterr().out
    assert "track  0.0: 12/12 sectors" in text and "track 79.1:  8/8 sectors" in text
    assert "zone 4: 590.0 rpm (zone speed 590)" in text and "missing 1500" in text
    back = mac.GcrImage.load(str(out))
    for t, s, r in ((0, 0, 0), (17, 1, 10), (33, 0, 9), (50, 1, 8), (79, 1, 7)):
        assert back.sector(t, s, r) == img.sector(t, s, r)
    assert back.sector(41, 1, 0) == bytes(524)
    reads = [q for q in fake.requests if q.cmd == P2.READ]
    assert all(q.payload[3] & P2.READ_NO_INDEX for q in reads)
    assert sum(1 for q in reads if q.payload[:2] == bytes([41, 1])) == 2          # one retry
    assert fake.drive == 11 and not fake.mac_motor                                  # motor off at the end


def test_read_mac_400k_raw(monkeypatch, tmp_path):
    fake = FakeMac(kind=1)
    img = _disk(fake, 1, tracks=range(3))
    out = tmp_path / "disk.img"
    assert _run(monkeypatch, fake, ["read-mac", "-o", str(out), "--retries", "0", "--tracks", "3", "-r", "1"]) == 0
    assert out.read_bytes() == bytes(img.data) and out.stat().st_size == 409600
    assert not any(q.cmd == P2.READ and q.payload[1] for q in fake.requests)       # one head only
    with pytest.raises(SystemExit):
        _run(monkeypatch, FakeMac(kind=1), ["read-mac", "-o", str(out), "--sides", "2"])
    with pytest.raises(SystemExit):
        _run(monkeypatch, FakeMac(kind=0), ["read-mac", "-o", str(out)])
    with pytest.raises(SystemExit):
        _run(monkeypatch, FakeMac(kind=2), ["read-mac", "-o", str(out), "--mfm"])     # needs a SuperDrive


def test_write_mac_verify_and_protection(monkeypatch, tmp_path, capsys):
    src = mac.GcrImage(2)
    for t in range(mac.TRACKS):
        for s in range(2):
            for r, data in _sectors(t, s, 9).items():
                src.put(t, s, r, data)
    path = tmp_path / "src.dc42"
    src.write(str(path))
    fake = FakeMac(kind=2)
    assert _run(monkeypatch, fake, ["write-mac", str(path), "--tracks", "3", "--verify"]) == 0
    text = capsys.readouterr().out
    assert "track  2.1: 12 sectors written, verify ok" in text and "0 sectors failed" in text
    writes = [q for q in fake.requests if q.cmd == P2.WRITE]
    assert len(writes) == 6 and all(not q.payload[2] & 0x20 for q in writes)     # classic path, no extended timing
    for (t, s), deltas in fake.flux.items():
        best = mac.decode_nibbles(mac.nibbles(deltas)).best()
        assert all(best[(t, s, r)].data == src.sector(t, s, r) for r in range(12))
    fake = FakeMac(kind=2)
    fake.wprot = True
    with pytest.raises(SystemExit):
        _run(monkeypatch, fake, ["write-mac", str(path), "--track", "0"])
    assert not any(q.cmd == P2.WRITE for q in fake.requests)
    fake = FakeMac(kind=1)
    with pytest.raises(SystemExit):
        _run(monkeypatch, fake, ["write-mac", str(path), "--track", "0"])         # 800K image, 400K drive
