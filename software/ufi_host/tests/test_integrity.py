"""Adversarial host integrity checks; no real drive compatibility is implied."""
import hashlib
import json
import sqlite3
import struct

import pytest

from ufi_host import cbm, protocol as P, protocol2 as P2
from ufi_host.archive import NativeArchive, verify_archive
from ufi_host.iec_job import read_image
from ufi_host.scp import ScpRevolution, _encode_cells, read_scp, write_scp
from test_protocol2 import FakeV2


class AddressedDrive(P2.Device2):
    def __init__(self, code=0):
        super().__init__(None)
        self._iec_addressed = True
        self.calls = []
        self.code = code

    def request(self, cmd, payload=b"", **kwargs):
        self.calls.append((cmd, payload))
        if cmd == P2.IEC_READ_STATUS:
            return bytes([1, self.code]) + payload[2:4] + (bytes(range(256)) if self.code == 0 else b"")
        return b"00, OK,00,00"


def test_addressed_iec_never_uses_default():
    dev = AddressedDrive()
    dev.iec_command("I0", 9)
    dev.iec_status(9)
    assert dev.calls == [(P2.IEC_TARGET, b"\x01\x09\x03\x00\x00I0"),
                         (P2.IEC_TARGET, b"\x01\x09\x04\x00\x00")]
    assert dev.iec_block_result(18, 0, 9) == (0, bytes(range(256)))
    assert dev.calls[-1] == (P2.IEC_READ_STATUS, bytes([1, 9, 18, 0]))
    dev.code = 23
    assert dev.iec_block_result(18, 0, 9) == (23, b"")
    with pytest.raises(RuntimeError, match="DOS 23"):
        dev.iec_block_read(18, 0, 9)


def test_old_iec_explicit_address_fails_before_command():
    dev = AddressedDrive()
    dev._iec_addressed = False
    with pytest.raises(RuntimeError):
        dev.iec_command("I0", 9)
    with pytest.raises(RuntimeError):
        dev.iec_block_result(1, 0, 9)
    assert not dev.calls


def test_partial_serial_writes_are_completed():
    class ShortWrite(FakeV2):
        def write(self, data):
            return super().write(data[:3])
    assert P2.Device2(ShortWrite()).ping()
    class StalledWrite(FakeV2):
        def write(self, data):
            return 0
    with pytest.raises(TimeoutError, match="write"):
        P2.Device2(StalledWrite()).ping()


def test_corrupt_frame_during_flux_is_not_silently_accepted():
    fake = FakeV2([(10000, [500, 1000])])
    fake.corrupt_next = True
    with pytest.raises(ValueError, match="CRC"):
        P2.Device2(fake).read_track(0, 0, 1)


@pytest.mark.parametrize("data", [b"\xf0", b"\xfd", b"\xfe\x01\x02", b"\xfe\0\0\0\0", b"\xfd\0\0\0\0"])
def test_malformed_flux_stream(data):
    with pytest.raises(ValueError):
        P.decode_stream(data)


def test_success_end_cannot_hide_missing_revolutions():
    with pytest.raises(ValueError, match="fewer"):
        P2.Device2(FakeV2([(10000, [1000])])).read_track(0, 0, 2)


@pytest.mark.parametrize("model,tracks,expected", [("1541", 0, ("d64", 35)),
    ("1541C", 40, ("d64", 40)), ("1541-II", 0, ("d64", 35)),
    ("1571", 0, ("d71", 70)), ("1571", 35, ("d64", 35)), ("1581", 0, ("d81", 80))])
def test_geometry(model, tracks, expected):
    assert cbm.kind_for(model, tracks) == expected


def test_invalid_geometry():
    with pytest.raises(ValueError):
        cbm.kind_for("1581", 35)


def test_checkpoint_retries_resume_and_tamper(tmp_path, monkeypatch):
    monkeypatch.setattr(cbm, "blocks", lambda *_: iter([(1, 0), (1, 1)]))
    class Drive:
        calls = []
        bad = True
        def iec_block_result(self, t, s, device):
            self.calls.append((t, s, device))
            return (23, b"") if self.bad and s == 1 else (0, bytes([s + 1]) * 256)
    dev = Drive()
    target = tmp_path / "disk.d64"
    state = read_image(dev, str(target), "d64", 35, 9, retries=1)
    assert not state["complete"] and not target.exists()
    assert state["sectors"]["1/1"]["attempts"] == [23, 23]
    partial = tmp_path / "disk.d64.partial"
    original = partial.read_bytes()
    partial.write_bytes(b"X" + original[1:])
    with pytest.raises(ValueError, match="changed"):
        read_image(dev, str(target), "d64", 35, 9, resume=True)
    partial.write_bytes(original)
    dev.bad = False
    dev.calls = []
    state = read_image(dev, str(target), "d64", 35, 9, resume=True)
    assert state["complete"] and dev.calls == [(1, 1, 9)]
    assert target.read_bytes() == b"\x01" * 256 + b"\x02" * 256
    assert not partial.exists()
    with pytest.raises(FileExistsError):
        read_image(dev, str(target), "d64", 35, 9)
    assert read_image(dev, str(target), "d64", 35, 9, resume=True)["complete"]


@pytest.mark.parametrize("interval", [0, -1, 65536, 131072])
def test_scp_does_not_emit_fake_transition(interval):
    with pytest.raises(ValueError):
        _encode_cells([interval])


def test_scp_corruption_resolution_and_bounds(tmp_path):
    path = tmp_path / "disk.scp"
    write_scp(str(path), {0: [ScpRevolution(8000000, [10, 70000])]}, 1)
    original = path.read_bytes()
    assert original[8] & 0x80
    def check_blob(blob):
        path.write_bytes(blob)
        return read_scp(str(path))
    blob = bytearray(original)
    blob[11] = 1  # resolution affects cells, not the fixed-25ns index time
    assert check_blob(blob)[1][0][0] == ScpRevolution(8000000, [20, 140000])
    blob[-1] ^= 1
    with pytest.raises(ValueError, match="checksum"):
        check_blob(blob)
    blob = bytearray(original)
    struct.pack_into("<I", blob, 16, 0xFFFFFFF0)
    struct.pack_into("<I", blob, 12, sum(blob[16:]) & 0xFFFFFFFF)
    with pytest.raises(ValueError, match="header"):
        check_blob(blob)
    with pytest.raises(ValueError):
        check_blob(b"SCP")


def test_native_archive_preserves_ticks_and_partial_frames(tmp_path):
    path = tmp_path / "disk.ufi"
    archive = NativeArchive(str(path), dict(sample_hz=275000000))
    attempt = archive.begin(2, 1, 1)
    dev = P2.Device2(FakeV2(revolutions=[(10000, [447, 1101, 2199])]))
    cap = dev.read_track(2, 1, 1, frame_sink=lambda b: archive.frame(attempt, b))
    archive.finish(attempt, cap)
    incomplete = archive.begin(3, 0, 1)
    archive.frame(incomplete, b"saved before disconnect")
    archive.fail(incomplete, "disconnected")
    archive.close()
    report = verify_archive(str(path))
    assert report["captures"] == 2 and report["unfinished"] == 1 and report["failed"] == 1
    with sqlite3.connect(path) as db:
        raw, sha = db.execute("SELECT samples,sha256 FROM revolutions").fetchone()
        assert struct.unpack("<3I", raw) == (447, 1101, 2199)
        assert hashlib.sha256(raw).hexdigest() == sha
        assert db.execute("SELECT status,error FROM captures WHERE id=?", (incomplete,)).fetchone() == (None, "disconnected")
        frames = db.execute("SELECT data FROM frames WHERE capture=? ORDER BY ordinal", (attempt,)).fetchall()
        parser = P2.FrameParser()
        decoded = [frame for (raw,) in frames for frame in parser.feed(raw)]
        assert decoded[0].type == P2.T_RESPONSE and decoded[-1].type == P2.T_END
        assert json.loads(db.execute("SELECT json FROM metadata").fetchone()[0])["sample_hz"] == 275000000
        db.execute("UPDATE revolutions SET samples=?", (b"bad",))
    with pytest.raises(ValueError, match="checksum"):
        verify_archive(str(path))
