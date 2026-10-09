"""UFI v2 tests against a fake device implementing docs/USB_Protokoll.md section 3 on byte level."""
from __future__ import annotations

import struct

import pytest
from test_protocol import FakeDevice, encode_stream

from ufi_host import cli
from ufi_host import protocol as P
from ufi_host import protocol2 as P2


def decode_write(code: bytes) -> list[int]:
    """Independent decoder for WRITE data (stream code without FD)."""
    out, i = [], 0
    while i < len(code):
        b = code[i]
        if 1 <= b <= 0xEF:
            out.append(b)
            i += 1
        elif 0xF0 <= b <= 0xFC:
            out.append(240 + ((b - 0xF0) << 8) + code[i + 1])
            i += 2
        elif b == 0xFE:
            out.append(struct.unpack_from("<I", code, i + 1)[0])
            i += 5
        else:
            raise AssertionError(f"bad write byte 0x{b:02X}")
    return out


class FakeV2:
    """Byte-level double of the v2 firmware: parses request frames, answers with frames.

    read() hands out odd-sized pieces so frames straddle chunk borders; `garbage` is
    injected before every reply frame, `corrupt_next` breaks the CRC of an extra
    (bogus) frame placed before the next reply.
    """

    def __init__(self, revolutions=None, files=None, read_status=0, data_chunk=1000):
        self.rx = P2.FrameParser()
        self.out = bytearray()
        self.revs = revolutions or []
        self.files = dict(files or {})
        self.read_status = read_status
        self.data_chunk = data_chunk
        self.requests: list[P2.Frame] = []
        self.write_req = None           # (seq, flux_count, byte_count, flags)
        self.write_code = bytearray()
        self.write_frames = 0
        self.events_mask = 0
        self.timing = P.TIMING.pack(3, 3000, 15000, 0, 200, 500, 10000, 30, 0, 0xFFFF)
        self.index_sim = 0
        self.index_sim_mode = 0
        self.rpm_set = 0
        self.garbage = b""
        self.corrupt_next = False
        self.pending_events: list[tuple[int, bytes]] = []
        self.reloads = 0
        self._chunks = [1, 7, 64, 3, 513, 2]
        self._ci = 0
        self._evseq = 0

    # -- device -> host -------------------------------------------------------
    def _frame(self, ftype, cmd, seq, status=0, payload=b""):
        if self.garbage:
            self.out += self.garbage
        if self.corrupt_next:
            bad = bytearray(P2.encode_frame(P2.T_EVENT, 0x80, 99, 0, b"\x01"))
            bad[-1] ^= 0xFF
            self.out += bad
            self.corrupt_next = False
        self.out += P2.encode_frame(ftype, cmd, seq, status, payload)

    def read(self, n: int) -> bytes:
        k = min(n, self._chunks[self._ci % len(self._chunks)])
        self._ci += 1
        chunk, self.out = bytes(self.out[:k]), self.out[k:]
        return chunk

    def emit_event(self, code, payload):
        if self.events_mask & (1 << (code - 0x80)):
            self._evseq = (self._evseq + 1) & 0xFF
            self._frame(P2.T_EVENT, code, self._evseq, 0, payload)

    # -- host -> device -------------------------------------------------------
    def write(self, data: bytes):
        before = self.rx.crc_errors
        for f in self.rx.feed(data):
            self._handle(f)
        if self.rx.crc_errors > before:
            self._frame(P2.T_RESPONSE, P2.BAD_FRAME_CMD, 0, P2.FRAME_ERROR)
        return len(data)

    def _handle(self, f: P2.Frame):
        if f.type == P2.T_DATA and self.write_req and f.seq == self.write_req[0]:
            self.write_frames += 1
            self.write_code += f.payload
            if len(self.write_code) >= self.write_req[2]:
                self._frame(P2.T_END, P2.WRITE, f.seq)
                self.write_req = None
            return
        assert f.type == P2.T_REQUEST and f.status == 0
        self.requests.append(f)
        cmd, seq, a = f.cmd, f.seq, f.payload
        r = lambda status=0, payload=b"": self._frame(P2.T_RESPONSE, cmd, seq, status, payload)
        if cmd == P2.PING:
            r(0, a)
        elif cmd == P2.INFO:
            r(0, P2.INFO_HDR.pack(2, 1, 14, 7, 0xFFF, 275_000_000, 8 << 20, 4096)
              + b"UFI Flux Engine 1.14 (abc1234, 2026-10-09)\0PSRAM ok\0")
        elif cmd == P2.STATUS:
            r(0, bytes([1, 1, 0, 1, 0, 1, 5, 0]) + P2.BOARD.pack(3, 8, 120, 0, 2200))
        elif cmd == P2.TIMING_CMD:
            if a:
                self.timing = a[:20]
            r(0, self.timing)
        elif cmd == P2.CHECK_DISK:
            r(0, b"\x01\x01")
        elif cmd == P2.AMIGA_ID:
            r(0, struct.pack("<I", 0xAAAAAAAA))
        elif cmd == P2.DIAG_RPM:
            r(0, P2.DIAG_RPM_REPLY.pack(a[0], 54_900_000, 55_000_000, 55_100_000, 1_100_000, 30000))
        elif cmd == P2.DRIVE_SCAN:
            r(0, bytes([6, 3, 7, 0, 8, 0, 9, 0]) if a and a[0] & 1 else bytes([1, 7, 2, 1, 4, 0, 10, 0]))
        elif cmd == P2.INDEX_SIM:
            if len(a) >= 2:
                rpm = struct.unpack_from("<H", a)[0]
                if rpm and not 200 <= rpm <= 400:
                    r(P2.BAD_ARGS, struct.pack("<HB", self.index_sim, self.index_sim_mode))
                    return
                self.index_sim = rpm
                self.index_sim_mode = (a[4] or 1) if rpm and len(a) >= 5 else (1 if rpm else 0)
            r(0, struct.pack("<HB", self.index_sim, self.index_sim_mode))
        elif cmd == P2.STEP_SCHEDULE:
            r(0 if len(a) == 2 + 3 * a[1] else P2.BAD_ARGS)
        elif cmd == P2.SET_RPM:
            if len(a) >= 2 and struct.unpack_from("<H", a)[0]:
                rpm = struct.unpack_from("<H", a)[0]
                if rpm not in (300, 360):
                    r(P2.BAD_ARGS, struct.pack("<HB", self.rpm_set, 1))
                    return
                self.rpm_set = rpm
            r(0, struct.pack("<HB", self.rpm_set, 1))
        elif cmd == P2.USB_POWER:
            r(0, struct.pack("<3H", 0, 1350, 3000))
        elif cmd == P2.IEC_RECV:
            r(0, b"\x42\x01")
        elif cmd == P2.POWER:
            r(0, P2.BOARD.pack(a[0] if a else 3, 8, 100, 50, 2200))
        elif cmd == P2.EVENTS:
            self.events_mask = struct.unpack("<I", a)[0]
            r()
            for code, payload in self.pending_events:
                self.emit_event(code, payload)
        elif cmd == P2.READ:
            _track, _side, revs, _flags, _period = P2.READ_REQ.unpack_from(a)
            r()
            data = encode_stream(self.revs[:revs])
            for k in range(0, len(data), self.data_chunk):
                self._frame(P2.T_DATA, cmd, seq, 0, data[k:k + self.data_chunk])
            self._frame(P2.T_END, cmd, seq, self.read_status, bytes([min(revs, len(self.revs))]))
        elif cmd == P2.WRITE:
            self.write_req = (seq, *struct.unpack_from("<II", a, 3), a[2])
            self.write_code = bytearray()
            r()
        elif cmd == P2.ERASE:
            r(P2.WRITE_PROTECTED)
        elif cmd == P2.DIR:
            start = struct.unpack_from("<H", a)[0]
            names = sorted(self.files)
            page = names[start:start + 2]                  # small pages exercise `more`
            body = bytes([len(page), int(start + 2 < len(names))])
            for nm in page:
                body += P2.DIR_ENTRY.pack(len(self.files[nm]), 0x20, len(nm)) + nm.encode()
            r(0, body)
        elif cmd == P2.READ_FILE:
            off, n = struct.unpack_from("<IH", a)
            name = a[6:].decode()
            if name not in self.files:
                r(P2.NOT_FOUND)
            else:
                r(0, self.files[name][off:off + n])
        elif cmd == P2.WRITE_FILE:
            off, flags, nlen = struct.unpack_from("<IBB", a)
            name, data = a[6:6 + nlen].decode(), a[6 + nlen:]
            cur = bytearray(b"" if flags & 1 else self.files.get(name, b""))
            cur[off:off + len(data)] = data
            self.files[name] = bytes(cur)
            r(0, struct.pack("<H", len(data)))
        elif cmd == P2.DELETE:
            name = a.decode()
            if self.files.pop(name, None) is None:
                r(P2.NOT_FOUND)
            else:
                r()
        elif cmd == P2.CFG_RELOAD:
            self.reloads += 1
            r()
        elif cmd == P2.DUMP_STATUS:
            r(0, P2.DUMP.pack(3, 0, 12, 1, 7, 0, 1))
        elif cmd in (P2.SELECT, P2.MOTOR, P2.SEEK, P2.RECAL, P2.SIDE, P2.ABORT, P2.IEC_RESET,
                     P2.IEC_SEND, P2.USB_MODE, P2.DUMP_START, P2.DUMP_ABORT, P2.COPY_START):
            r()
        else:
            r(P2.UNKNOWN_COMMAND)


REVS = [(55_000_000, [1100, 2200, 3300, 3300 + 4000, 3300 + 900_000]),
        (55_000_100, [1000, 2000, 2000 + 0xEF + 7])]


# -- framing --------------------------------------------------------------------
def test_crc_is_ccitt_false():
    assert P2.crc16(b"123456789") == 0x29B1


def test_frame_layout_matches_spec_example():
    f = P2.encode_frame(P2.T_REQUEST, P2.SELECT, 1, 0, b"\x01")
    assert f[:9] == bytes.fromhex("55 AA 01 10 01 00 01 00 01")
    assert struct.unpack("<H", f[9:])[0] == P2.crc16(f[2:9])
    f = P2.encode_frame(P2.T_REQUEST, P2.READ, 3, 0, P2.READ_REQ.pack(0, 0, 3, 0, 0))
    assert f[:14] == bytes.fromhex("55 AA 01 20 03 00 06 00 00 00 03 00 00 00")


def test_parser_resyncs_after_garbage_and_crc_errors():
    good1 = P2.encode_frame(P2.T_RESPONSE, 0x10, 1)
    bad = bytearray(P2.encode_frame(P2.T_RESPONSE, 0x11, 2, 0, b"\x55\xAA\x55"))
    bad[9] ^= 0x01
    good2 = P2.encode_frame(P2.T_DATA, 0x20, 3, 0, b"\x55\xAA" * 40)
    stream = b"\x00\x55\x13" + good1 + b"\x55" + bytes(bad) + b"\xAA\x55" + good2
    parser = P2.FrameParser()
    frames = []
    for i in range(len(stream)):                     # one byte at a time
        frames += parser.feed(stream[i:i + 1])
    assert [(f.cmd, f.seq) for f in frames] == [(0x10, 1), (0x20, 3)]
    assert frames[1].payload == b"\x55\xAA" * 40
    assert parser.crc_errors >= 1


def test_parser_rejects_oversized_length():
    hdr = b"\x55\xAA" + P2.HEADER.pack(2, 0, 1, 0, 5000)
    good = P2.encode_frame(P2.T_RESPONSE, 0, 1, 0, b"ok")
    frames = P2.FrameParser().feed(hdr + good)
    assert len(frames) == 1 and frames[0].payload == b"ok"


def test_request_survives_garbage_and_corrupt_frame():
    fake = FakeV2()
    fake.garbage = b"\x00\x55\x11"
    fake.corrupt_next = True
    dev = P2.Device2(fake, timeout=0.5)
    assert dev.ping(b"hello")
    assert dev.parser.crc_errors >= 1 and not dev.events


def test_bad_request_frame_gets_frame_error():
    fake = FakeV2()
    dev = P2.Device2(fake, timeout=0.5)
    orig = fake.write

    def corrupting_write(data):
        b = bytearray(data)
        b[-1] ^= 0xFF
        return orig(bytes(b))

    fake.write = corrupting_write
    with pytest.raises(P2.DeviceError, match="frame error") as e:
        dev.select("a")
    assert e.value.status == P2.FRAME_ERROR
    assert isinstance(e.value, P.DeviceError)


# -- commands -------------------------------------------------------------------
def test_info_caps_status():
    dev = P2.Device2(FakeV2())
    lines = dev.info()
    assert lines[0] == "UFI v2 protocol, firmware 1.14, board v0.7"
    assert "PSRAM ok" in lines
    i = dev.device_info()
    assert i.sample_hz == 275_000_000 and i.max_payload == 4096 and i.caps == 0xFFF
    assert all(on for _, on in i.caps_list())
    st = dev.status()
    assert st.drive == 1 and st.motor_on and st.track0 and st.track == 5
    assert st.board.flags == 8 and st.board.board_id_mv == 2200


def test_diagnostics():
    fake = FakeV2()
    dev = P2.Device2(fake)
    r = dev.diag_rpm(7)
    assert r.revolutions == 7 and abs(r.rpm - 300.0) < 1e-9
    assert abs(r.period_avg_us - 200_000) < 1e-6 and abs(r.pulse_us - 4000) < 1e-6
    assert "300.00 rpm" in str(r) and "7 revs" in str(r)
    assert fake.requests[-1].payload == b"\x07"
    assert dev.drive_scan() == [("a", 7), ("b", 1), ("amiga", 0), ("amiga2", 0)]
    assert dev.drive_scan(shugart_bus=True)[0] == ("ds0", 3)
    assert fake.requests[-1].payload == b"\x01"
    assert dev.index_sim() == (0, 0)
    assert dev.index_sim(300) == (300, 1) and fake.requests[-1].payload == struct.pack("<HHB", 300, 0, 1)
    assert dev.index_sim(360, 1500, P2.INDEX_SIM_INTERNAL) == (360, 2)
    with pytest.raises(P2.DeviceError):
        dev.index_sim(1000)
    assert dev.index_sim() == (360, 2) and dev.index_sim(0) == (0, 0)
    dev.read_track(1, 0, 1, hard_sectors=16)
    assert fake.requests[-1].payload == P2.READ_REQ.pack(1, 0, 1, P2.HARD_SECTORS, 0) + b"\x10"
    dev.write_track(1, 0, [1000, 1000, 1000], hard_sectors=10)
    req = [r for r in fake.requests if r.cmd == P2.WRITE][-1].payload
    assert req[2] == P2.HARD_SECTORS and req[-1] == 10 and len(req) == 12
    dev.seek(17, 2)
    assert fake.requests[-1].cmd == P2.SEEK and fake.requests[-1].payload == bytes([17, 2])
    dev.seek(17)
    assert fake.requests[-1].payload == bytes([17])
    dev.read_track(17, 0, 1, quarter=1)
    assert fake.requests[-1].payload == P2.READ_REQ.pack(17 * 4 + 1, 0, 1, P2.QUARTER_TRACKS, 0)
    dev.write_track(17, 0, [1000, 1000, 1000], quarter=3)
    req = [r for r in fake.requests if r.cmd == P2.WRITE][-1].payload
    assert req[0] == 17 * 4 + 3 and req[2] == P2.QUARTER_TRACKS
    dev.step_schedule([(50, 17 * 4 + 1), (100, 17 * 4 + 2)], quarter=True)
    assert fake.requests[-1].cmd == P2.STEP_SCHEDULE
    assert fake.requests[-1].payload == bytes([1, 2]) + struct.pack("<HBHB", 50, 69, 100, 70)
    with pytest.raises(ValueError):
        dev.step_schedule([(0, 0)] * 33)
    from ufi_host.cli import _parse_steps
    assert _parse_steps("50:17.1,100:17.2") == ([(50, 69), (100, 70)], True)
    assert _parse_steps("30:40,60:41") == ([(30, 40), (60, 41)], False)
    assert dev.set_rpm() == (0, 1)
    assert dev.set_rpm(360) == (360, 1) and fake.requests[-1].payload == struct.pack("<H", 360)
    with pytest.raises(P2.DeviceError):
        dev.set_rpm(333)


def test_drive_commands_and_payloads():
    fake = FakeV2()
    dev = P2.Device2(fake)
    dev.select("amiga2")
    dev.motor(True)
    dev.seek(40)
    dev.side(1)
    dev.iec_send(0x28, True)
    assert [(f.cmd, f.payload) for f in fake.requests] == [
        (P2.SELECT, b"\x0a"), (P2.MOTOR, b"\x01"), (P2.SEEK, bytes([40])), (P2.SIDE, b"\x01"),
        (P2.IEC_SEND, b"\x28\x01")]
    assert [f.seq for f in fake.requests] == [1, 2, 3, 4, 5]
    assert dev.check_disk() == (True, True)
    assert dev.amiga_id() == (0xAAAAAAAA, '3.5" HD (HD media)')
    assert dev.usb_power() == (0, 1350, 3000)
    assert dev.iec_receive() == (0x42, True)
    assert dev.power(1).power == 1
    t = dev.timing(step_rate_us=6000)
    assert t["step_rate_us"] == 6000 and t["precomp_ns"] == 0xFFFF
    with pytest.raises(P2.DeviceError, match="write protected"):
        dev.erase(0, 0)


def test_seq_wraps_and_skips_zero():
    dev = P2.Device2(FakeV2())
    dev._seq = 254
    assert [dev._next_seq() for _ in range(3)] == [255, 1, 2]


# -- flux -----------------------------------------------------------------------
def test_read_track_data_frames_to_revolutions():
    fake = FakeV2(REVS, data_chunk=7)
    cap = P2.Device2(fake).read_track(5, 1, 2)
    assert [(r.index_ticks, r.samples) for r in cap.revolutions] == REVS
    assert cap.revolutions[0].track == 5 and cap.revolutions[1].side == 1
    assert fake.requests[-1].payload == P2.READ_REQ.pack(5, 1, 2, 0, 0)


def test_read_no_index_flags_and_overflow():
    fake = FakeV2(REVS[:1], read_status=P2.OVERFLOW)
    cap = P2.Device2(fake).read_track(0, 0, 3, no_index=True, period_ms=300)
    assert fake.requests[-1].payload == P2.READ_REQ.pack(0, 0, 3, 1, 300)
    assert len(cap.revolutions) == 1 and cap.status == 10 and cap.revolutions[0].overflow


def test_read_end_error_raises():
    with pytest.raises(P2.DeviceError, match="no index"):
        P2.Device2(FakeV2([], read_status=P2.NO_INDEX)).read_track(0, 0, 1)


def test_read_to_scp_equals_v1_path(tmp_path):
    revs = [(55_000_000, list(range(700, 55_000_000 - 700, 7700))),
            (55_000_123, list(range(900, 55_000_000 - 900, 7700)))]
    v1 = P.Device(FakeDevice(revs)).read_track(2, 1, 2)
    v2 = P2.Device2(FakeV2(revs, data_chunk=4096)).read_track(2, 1, 2)
    p1, p2 = tmp_path / "v1.scp", tmp_path / "v2.scp"
    for cap, path in ((v1, p1), (v2, p2)):
        cli.write_scp(str(path), {5: [cli._to_scp(r) for r in cap.revolutions]}, 2)
    assert p1.read_bytes() == p2.read_bytes()


def test_write_track_sends_data_frames_and_waits_for_end():
    fake = FakeV2()
    dev = P2.Device2(fake)
    deltas = [1100] * 3000 + [300, 3567, 3568, 100_000, 0xEF, 0xF0]
    dev.write_track(3, 1, deltas, verify=True)
    req = fake.requests[-1]
    code = P2.encode_flux(deltas)
    assert req.cmd == P2.WRITE and req.payload == P2.WRITE_REQ.pack(3, 1, 1, len(deltas), len(code))
    assert decode_write(bytes(fake.write_code)) == deltas
    assert fake.write_frames == -(-len(code) // 4096) > 1


# -- files ----------------------------------------------------------------------
def test_dir_paginates():
    files = {f"DUMP000{i}.SCP": bytes(i) for i in range(5)}
    entries = P2.Device2(FakeV2(files=files)).list_dir()
    assert [e.name for e in entries] == sorted(files) and entries[3].size == 3
    assert not entries[0].is_dir


def test_file_read_write_delete():
    blob = bytes(range(256)) * 40                     # 10240 bytes: 3 READ_FILE chunks
    fake = FakeV2(files={"DUMP0001.SCP": blob})
    dev = P2.Device2(fake)
    assert dev.read_file("DUMP0001.SCP") == blob
    exact = bytes(8192)                               # multiple of 4096: ends on an empty chunk
    dev.write_file("X.BIN", exact)
    assert fake.files["X.BIN"] == exact
    dev.write_file("X.BIN", b"short")                 # first chunk truncates
    assert fake.files["X.BIN"] == b"short"
    dev.write_file("E.BIN", b"")
    assert fake.files["E.BIN"] == b""
    dev.delete("X.BIN")
    assert "X.BIN" not in fake.files
    with pytest.raises(P2.DeviceError, match="file not found"):
        dev.read_file("NOPE.TXT")


def test_cfg_edit_keeps_comments_and_line_endings():
    text = "# UFI config\r\ndrive=a\r\ntracks = 80\r\n"
    out = cli.edit_cfg(text, {"tracks": "82", "protocol": "gw"})
    assert out == "# UFI config\r\ndrive=a\r\ntracks=82\r\nprotocol=gw\r\n"


def test_cli_cfg_writes_back_and_reloads(monkeypatch, capsys):
    fake = FakeV2(files={"UFI.CFG": b"drive=a\nrevs=3\n"})
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["cfg", "revs=5", "sides=1"]) == 0
    assert fake.files["UFI.CFG"] == b"drive=a\nrevs=5\nsides=1\n" and fake.reloads == 1
    assert cli.main(["cfg"]) == 0
    assert "revs=5" in capsys.readouterr().out


def test_cli_get_and_caps(monkeypatch, tmp_path, capsys):
    fake = FakeV2(files={"DUMP0001.SCP": b"SCP" + bytes(5000)})
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    out = tmp_path / "d.scp"
    assert cli.main(["get", "DUMP0001.SCP", "-o", str(out)]) == 0
    assert out.read_bytes() == fake.files["DUMP0001.SCP"]
    assert cli.main(["caps"]) == 0
    text = capsys.readouterr().out
    assert "board v0.7" in text and "file access" in text


def test_cli_diagnostics(monkeypatch, capsys):
    fake = FakeV2()
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["rpm", "3"]) == 0
    assert "300.00 rpm" in capsys.readouterr().out and fake.requests[-1].cmd == P2.DIAG_RPM
    assert cli.main(["scan"]) == 0
    out = capsys.readouterr().out
    assert "a        yes      spins yes" in out and "amiga    -" in out
    assert cli.main(["index-sim", "360"]) == 0
    assert "360 rpm, J9 pin 6" in capsys.readouterr().out
    assert cli.main(["index-sim", "300", "--internal", "--no-pin"]) == 0
    assert "300 rpm, internal" in capsys.readouterr().out and fake.requests[-1].payload[4] == 2
    assert cli.main(["index-sim", "off"]) == 0
    assert "off" in capsys.readouterr().out
    assert cli.main(["rpm-select", "360"]) == 0
    assert "DENSITY" in capsys.readouterr().out and fake.rpm_set == 360
    assert cli.main(["rpm-select"]) == 0
    assert "360 rpm selected" in capsys.readouterr().out


# -- events ---------------------------------------------------------------------
def test_events_are_queued_and_decoded():
    fake = FakeV2()
    fake.pending_events = [(P2.EV_BUTTON, b"\x01\x01"), (P2.EV_DISK_CHANGED, b"\x01")]
    dev = P2.Device2(fake)
    dev.enable_events()
    assert fake.events_mask == P2.EVENTS_ALL == 0x1F
    assert dev.ping()                                 # events arriving around a response
    e1, e2 = dev.poll_event(0.1), dev.poll_event(0.1)
    assert e1.describe() == "button B long" and e2.describe() == "disk changed: drive a"
    assert dev.poll_event(0.0) is None


# -- fallback -------------------------------------------------------------------
def test_v1_firmware_falls_back_with_hint():
    hints = []
    dev = P2.connect_stream(FakeDevice(), ping_timeout=0.05, notify=hints.append)
    assert type(dev) is P.Device and "update the firmware" in hints[0]
    assert dev.info() == ["UFI Flux Engine v1.1", "UFI Headless", "STM32H723"]


def test_v2_firmware_is_detected():
    dev = P2.connect_stream(FakeV2(), notify=pytest.fail)
    assert isinstance(dev, P2.Device2) and dev.max_payload == 4096


def test_cli_v2_only_command_on_v1(monkeypatch, capsys):
    monkeypatch.setattr(P2, "open_serial", lambda port=None: FakeDevice())
    monkeypatch.setattr(P2, "connect_stream",
                        lambda s, timeout=5.0: P.Device(s, timeout))
    assert cli.main(["files"]) == 1
    assert "needs UFI v2" in capsys.readouterr().err
