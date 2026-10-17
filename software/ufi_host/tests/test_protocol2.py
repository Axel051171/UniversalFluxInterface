"""UFI v2 tests against a fake device implementing docs/USB_Protokoll.md section 3 on byte level."""
from __future__ import annotations

import struct

import pytest
from test_protocol import FakeDevice, encode_stream

from ufi_host import cli
from ufi_host import protocol as P
from ufi_host import protocol2 as P2


def test_describe_schema_and_forward_extension():
    payload = P2.DESCRIBE_FMT.pack(1, 3, 0, 1 << 16, 275000000, 68750000,
                                  38224, 1024, 200, 0x3fffffff, 110, 100000)
    assert len(payload) == 36
    result = P2.ProtocolDescription.unpack(payload + b"future")
    assert result.native_hz == 4 * result.gw_hz
    assert result.transports == 3 and result.flags == 0
    assert result.min_gw_write_ticks == 110
    for truncated in (b"", payload[:35], bytes([2]) + payload[1:]):
        with pytest.raises(ValueError):
            P2.ProtocolDescription.unpack(truncated)


def test_describe_device_request():
    class Transport:
        def write(self, data):
            request = list(P2.FrameParser().feed(data))[0]
            assert request.cmd == P2.DESCRIBE and request.payload == b""
            payload = P2.DESCRIBE_FMT.pack(1, 3, 0, 65536, 275000000, 68750000,
                                          38224, 1024, 200, 0x3fffffff, 110, 100000)
            self.response = P2.encode_frame(P2.T_RESPONSE, request.cmd, request.seq, 0, payload)
            return len(data)

        def read(self, size):
            data, self.response = self.response[:size], self.response[size:]
            return data

    assert P2.Device2(Transport()).describe().store_words == 38224


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

    Speaks firmware 1.18-dev on a v0.7 board by default (INFO caps as the firmware
    reports them with PSRAM, SD and Apple port present, sync sensor off); pass
    firmware=(1, 15), caps=0x7FFF for the pre-addressed-IEC behaviour.  `read_status`
    ends every READ with that END status (OVERFLOW, NO_INDEX, CANCELLED, MEDIA_CHANGED);
    CANCELLED and MEDIA_CHANGED send no data frames, as the firmware's cancel path
    and media-generation check stop the capture.  `dos_errors` maps (track, sector)
    to the DOS code IEC_READ_STATUS reports instead of data.
    """

    CAPS_1_18 = 0xFFFFFF7F

    def __init__(self, revolutions=None, files=None, read_status=0, data_chunk=1000,
                 firmware=(1, 18), caps=None):
        self.rx = P2.FrameParser()
        self.out = bytearray()
        self.revs = revolutions or []
        self.files = dict(files or {})
        self.read_status = read_status
        self.data_chunk = data_chunk
        self.firmware = firmware
        self.caps = self.CAPS_1_18 if caps is None else caps
        self.dos_errors: dict[tuple[int, int], int] = {}
        self.requests: list[P2.Frame] = []
        self.blocks: dict[tuple[int, int], bytes] = {}
        self.write_req = None           # (seq, flux_count, byte_count, flags)
        self.write_code = bytearray()
        self.write_frames = 0
        self.events_mask = 0
        self.timing = P.TIMING.pack(3, 3000, 15000, 0, 200, 500, 10000, 30, 0, 0xFFFF, 0)
        self.extra = P2.EXTRA.pack(0, 0, 20000, 0, 0, 0)
        self.profile_stored = False
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

    @staticmethod
    def _iec_mem(a, r):
        """IEC_MEM payload: 0 M-W (<= 32 bytes), 1 M-R n, 2 M-E, 3 DOS command (1-40 bytes), 4 DOS status."""
        if len(a) < 3 or a[0] > 4 or (a[0] == 0 and not 1 <= len(a) - 3 <= 32) \
                or (a[0] == 1 and (len(a) != 4 or a[3] == 0)) or (a[0] == 3 and not 1 <= len(a) - 3 <= 40):
            r(P2.BAD_ARGS)
        elif a[0] == 4:
            r(0, b"00, OK,00,00\r")
        elif a[0] == 1:
            r(0, bytes(a[3]))
        else:
            r()

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
                self._frame(P2.T_END, P2.WRITE, f.seq, 0, struct.pack("<BI", 2 if self.write_req[3] & 1 else 1, 3 if self.write_req[3] & P2.WRITE_CLAMP else 0))
                self.write_req = None
            return
        assert f.type == P2.T_REQUEST and f.status == 0
        self.requests.append(f)
        cmd, seq, a = f.cmd, f.seq, f.payload
        r = lambda status=0, payload=b"": self._frame(P2.T_RESPONSE, cmd, seq, status, payload)
        if cmd == P2.PING:
            r(0, a)
        elif cmd == P2.INFO:
            text = f"UFI Flux Engine {self.firmware[0]}.{self.firmware[1]}-dev (abc1234, 2026-10-17)"
            r(0, P2.INFO_HDR.pack(2, *self.firmware, 7, self.caps, 275_000_000, 8 << 20, 4096)
              + text.encode() + b"\0PSRAM ok\0")
        elif cmd == P2.STATUS:
            r(0, bytes([1, 1, 0, 1, 0, 1, 5, 0]) + P2.BOARD.pack(3, 8, 120, 0, 2200))
        elif cmd == P2.TIMING_CMD:
            if a:
                self.timing = a[:P2.TIMING.size]
            r(0, self.timing)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 10:          # additional drive times (4.1)
            if len(a) == 14:
                if a[1] != 1:
                    r(P2.BAD_ARGS)
                    return
                self.extra = a[2:14]
            r(0, bytes([1]) + self.extra)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 33:          # Apple II GCR options (5.7)
            if len(a) == 14:
                if a[1] != 1 or (a[12] | a[13] << 8) & ~0x1F or any(b < 0x80 for b in a[2:12]):
                    r(P2.BAD_ARGS)
                    return
                self.apple = bytes(a[2:14])
            r(0, bytes([1]) + getattr(self, "apple", bytes.fromhex("d5aa96d5aaaddeaadeaa0000")))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 34:          # decoder tolerance flags, formats 1-6 (5.8)
            if len(a) == 4:
                if a[1] != 1 or (a[2] | a[3] << 8) & ~0x1F:
                    r(P2.BAD_ARGS)
                    return
                self.tolerance = a[2] | a[3] << 8
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            t = getattr(self, "tolerance", 0)
            r(0, bytes([1, t & 0xFF, t >> 8]))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 18:          # operating profile / presets (4.4)
            if len(a) == 2:                                   # [18, id]: movement/settle times only
                presets = {1: ((3000, 15000, 15000, 200, 500, 10000), (0, 0)),
                           2: ((8000, 40000, 38000, 1000, 800, 60000), (60000, 200))}
                if a[1] not in presets:
                    r(P2.BAD_ARGS)
                    return
                t = list(P2.TIMING.unpack(self.timing))
                t[1:7] = presets[a[1]][0]
                self.timing = P2.TIMING.pack(*t)
                e = list(P2.EXTRA.unpack(self.extra))
                e[0:2] = presets[a[1]][1]
                self.extra = P2.EXTRA.pack(*e)
            elif len(a) == 41:
                if a[1] != 1 or a[40] != 0:
                    r(P2.BAD_ARGS)
                    return
                self.timing, self.extra, self.profile_stored = a[2:24], a[24:36], True
            r(0, P2.PROFILE_FMT.pack(1, self.timing, self.extra, 0, 1, 0, int(self.profile_stored)))
        elif cmd == P2.IEC_MEM:                              # op, addr u16, data / n
            self._iec_mem(a, r)
        elif cmd == P2.IEC_TARGET:                           # 1.18: addressed IEC_MEM (3.4)
            if not (self.caps & P2.CAP_IEC_ADDRESSED):
                r(P2.UNKNOWN_COMMAND)
            elif len(a) < 2 or a[0] != 1 or not (a[1] == 0 or 8 <= a[1] <= 30):
                r(P2.BAD_ARGS)
            else:
                self._iec_mem(a[2:], r)
        elif cmd == P2.IEC_READ_STATUS:                      # 1.18: U1 plus DOS channel (3.4)
            if not (self.caps & P2.CAP_IEC_ADDRESSED):
                r(P2.UNKNOWN_COMMAND)
            elif len(a) != 4 or a[0] != 1 or not (a[1] == 0 or 8 <= a[1] <= 30):
                r(P2.BAD_ARGS)
            else:
                track, sector = a[2], a[3]
                dos = self.dos_errors.get((track, sector), 0 if 1 <= track <= 35 and sector < 21 else 66)
                data = self.blocks.get((track, sector), bytes([track, sector]) * 128) if dos == 0 else b""
                r(0, bytes([1, dos, track, sector]) + data)
        elif cmd == P2.IEC_BLOCK:
            if a[0] == 0:
                r(0, self.blocks.get((a[1], a[2]), bytes([a[1], a[2]]) * 128))
            elif a[1] == 36:                  # track 36: write protect
                r(0, b"\x1a")
            else:
                self.blocks[(a[1], a[2])] = bytes(a[4:])
                r(0, b"\x00")
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
        elif cmd == P2.WRITE_LIMITS:
            r(P2.BAD_ARGS if a else 0, b"" if a else P2.WRITE_LIMITS_FMT.pack(1, 7, 275_000_000, 1 << 21, 440, 65536))
        elif cmd == P2.SELFTEST:
            r(0, bytes([3]) + struct.pack("<BBI", 1, 0, 275000) + struct.pack("<BBI", 2, 0, 0xFFFFFFF6)
              + struct.pack("<BBI", 14, 1, 0))
        elif cmd == P2.FORMAT:
            if len(a) != 8:
                r(P2.BAD_ARGS)
            else:
                q = P2.FORMAT_REQ.unpack(a)
                r(0, struct.pack("<IB", q[4] * 1000, q[4] if q[2] & 1 else 0xFF))
        elif cmd == P2.ANALYZE:
            r(0, P2.ANALYZE_FMT.pack(a[2] or 3, 2, 2000, 30012, 4000, 6000, 8000, 0, 55_000_000,
                                     50_000, 100_000, 18, 2, 1234, 0x11))
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
            if self.read_status in (P2.CANCELLED, P2.MEDIA_CHANGED):
                self._frame(P2.T_END, cmd, seq, self.read_status)
                return
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
                     P2.IEC_SEND, P2.USB_MODE, P2.DUMP_START, P2.DUMP_ABORT, P2.COPY_START,
                     P2.WRITE_START):
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
    assert lines[0] == "UFI v2 protocol, firmware 1.18, board v0.7"
    assert "PSRAM ok" in lines
    i = dev.device_info()
    assert i.sample_hz == 275_000_000 and i.max_payload == 4096 and i.caps == 0xFFFFFF7F
    assert [on for _, on in i.caps_list()] == [k != 7 for k in range(len(P2.CAPS))]
    assert i.caps & P2.CAP_IEC_ADDRESSED and i.caps & (1 << 27)
    st = dev.status()
    assert st.drive == 1 and st.motor_on and st.track0 and st.track == 5
    assert st.board.flags == 8 and st.board.board_id_mv == 2200
    old = P2.Device2(FakeV2(firmware=(1, 15), caps=0x7FFF))
    assert old.info()[0] == "UFI v2 protocol, firmware 1.15, board v0.7"
    assert not old.device_info().caps_list()[15][1]  # firmware 1.15 has no addressed status API


def test_iec_addressed_status_and_read_end_states():
    fake = FakeV2()
    dev = P2.Device2(fake)
    assert dev.iec_status(9) == "00, OK,00,00"
    assert fake.requests[-1].cmd == P2.IEC_TARGET and fake.requests[-1].payload == bytes([1, 9, 4, 0, 0])
    dev.iec_command("I0")
    assert fake.requests[-1].cmd == P2.IEC_MEM           # device 0 keeps the unaddressed command
    assert dev.iec_mem_read(0x300, 4, 8) == bytes(4)
    with pytest.raises(ValueError):
        dev.iec_status(31)
    # block reads go through IEC_READ_STATUS; the DOS code arrives in the payload, not the frame status
    dos, data = dev.iec_block_result(18, 0)
    assert (dos, data) == (0, bytes([18, 0]) * 128)
    assert fake.requests[-1].cmd == P2.IEC_READ_STATUS and fake.requests[-1].payload == bytes([1, 0, 18, 0])
    fake.dos_errors[(18, 1)] = 23
    assert dev.iec_block_result(18, 1, 9) == (23, b"")
    assert dev.iec_block_result(40, 0) == (66, b"")
    with pytest.raises(RuntimeError, match="DOS 23"):
        dev.iec_block_read(18, 1, 9)
    # firmware 1.15: U1 over IEC_BLOCK, then the error channel; addressed devices refused
    old = FakeV2(firmware=(1, 15), caps=0x7FFF)
    dev = P2.Device2(old)
    assert dev.iec_block_result(18, 0) == (0, bytes([18, 0]) * 128)
    assert [f.cmd for f in old.requests[-2:]] == [P2.IEC_BLOCK, P2.IEC_MEM]
    with pytest.raises(RuntimeError):
        dev.iec_block_result(18, 0, 9)
    with pytest.raises(P2.DeviceError, match="unknown command"):
        dev.request(P2.IEC_READ_STATUS, bytes([1, 0, 18, 0]))
    # READ ended by the cancel path or by a media change: no data, named error
    for status, text in ((P2.CANCELLED, "cancelled"), (P2.MEDIA_CHANGED, "media changed")):
        with pytest.raises(P2.DeviceError, match=text) as e:
            P2.Device2(FakeV2(REVS, read_status=status)).read_track(0, 0, 1)
        assert e.value.status == status


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
    fake.revs = [(10000, [1000, 2000])]
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
    r = dev.analyze(5, 1, 4)
    assert fake.requests[-1].payload == bytes([5, 1, 4]) and r["revs"] == 4
    assert r["encoding"] == "MFM" and r["peaks_ns"] == [4000, 6000, 8000] and r["rpm"] == 300.12
    assert r["sectors"] == 18 and r["weak_windows"] == 2 and r["weak_mask"] == 0x11
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
    dev._limits_checked = True              # encoder test: no preflight
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
    assert cli.main(["analyze", "5", "1"]) == 0
    out = capsys.readouterr().out
    assert "MFM, bit cell 2000 ns" in out and "MFM sectors: 18" in out and "weak-bit windows: 2/64" in out
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


def test_write_track_time_scale_flag_and_u16():
    import pytest
    fake = FakeV2()
    dev = P2.Device2(fake)
    dev.write_track(4, 0, [1000, 1000, 1000], scale=1.2)
    req = [r for r in fake.requests if r.cmd == P2.WRITE][-1].payload
    assert req[2] & P2.TIME_SCALE and req[11:13] == struct.pack("<H", 1200)
    dev.write_track(4, 0, [1000, 1000, 1000], hard_sectors=10, scale=0.8333)
    req = [r for r in fake.requests if r.cmd == P2.WRITE][-1].payload
    assert req[11] == 10 and req[12:14] == struct.pack("<H", 833)
    dev.write_track(4, 0, [1000, 1000, 1000], scale=1.0)
    req = [r for r in fake.requests if r.cmd == P2.WRITE][-1].payload
    assert not (req[2] & P2.TIME_SCALE) and len(req) == 11
    with pytest.raises(ValueError):
        dev.write_track(4, 0, [1000, 1000, 1000], scale=3.0)


def test_format_track_request_and_reply():
    fake = FakeV2()
    dev = P2.Device2(fake)
    assert dev.format_track(3, 1, 500, 18, verify=True) == (18000, 18)
    assert fake.requests[-1].payload == P2.FORMAT_REQ.pack(3, 1, P2.FORMAT_VERIFY, 500, 18, 0, 0xF6)
    assert dev.format_track(0, 0, 250, 11, amiga=True, fill=0) == (11000, None)
    assert fake.requests[-1].payload[2] == P2.FORMAT_AMIGA and fake.requests[-1].payload[-1] == 0
    assert dev.format_track(0, 0, 250, 10, head_id_swap=True)[0] == 10000
    assert fake.requests[-1].payload[2] == P2.FORMAT_HEAD_SWAP


def test_selftest_items():
    fake = FakeV2()
    dev = P2.Device2(fake)
    items = dev.selftest()
    assert items == [(1, "timer", 0, 275000), (2, "usb_sof", 0, 0xFFFFFFF6), (14, "log", 1, 0)]
    assert fake.requests[-1].cmd == P2.SELFTEST and fake.requests[-1].payload == b""


def test_write_track_result_and_clamp_flag():
    fake = FakeV2()
    dev = P2.Device2(fake)
    r = dev.write_track(4, 0, [1000, 1000, 1000])
    assert (r.verify_level, r.clamped, r.verify_text) == (1, 0, "write completed")
    r = dev.write_track(4, 0, [1000, 1000, 1000], verify=True, clamp=True)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert req[2] & P2.WRITE_CLAMP and r.verify_level == 2 and r.clamped == 3
    assert "count only" in r.verify_text
    assert P2.WriteResult.unpack(b"") == P2.WriteResult(0, 0)
    assert "18" not in P2.STATUS_TEXT or "not representable" in P2.STATUS_TEXT[18]


def test_write_limits_and_preflight():
    import pytest
    fake = FakeV2()
    dev = P2.Device2(fake)
    lim = dev.write_limits()
    assert (lim.schema, lim.flags, lim.sample_hz, lim.max_flux, lim.min_ticks, lim.max_ticks) == \
        (1, 7, 275_000_000, 1 << 21, 440, 65536)
    n_before = len([q for q in fake.requests if q.cmd == P2.WRITE])
    with pytest.raises(ValueError):
        dev.write_track(1, 0, [1000, 439, 1000])
    with pytest.raises(ValueError):
        dev.write_track(1, 0, [1000, 65537, 1000])
    with pytest.raises(ValueError):
        dev.write_track(1, 0, [1000, 800, 1000], scale=0.5)
    with pytest.raises(ValueError):
        dev.write_track(1, 0, [1000, 1000])
    assert len([q for q in fake.requests if q.cmd == P2.WRITE]) == n_before, "no WRITE went out"
    dev.write_track(1, 0, [1000, 439, 1000], clamp=True)
    dev.write_track(1, 0, [1000, 800, 1000], scale=0.5, clamp=True)
    assert len([q for q in fake.requests if q.cmd == P2.WRITE]) == n_before + 2
    assert P2.STATUS_TEXT[19].startswith("track longer")


def test_extra_timing_profile_and_presets():
    import pytest
    fake = FakeV2()
    dev = P2.Device2(fake)
    extra = dev.extra_timing()
    assert extra["write_settle_us"] == 20000 and extra["density_us"] == 0
    assert dev.extra_timing(density_us=1500)["density_us"] == 1500
    with pytest.raises(ValueError):
        dev.extra_timing(bogus=1)
    prof = dev.operating_profile()
    assert prof.timing["step_rate_us"] == 3000 and prof.extra["density_us"] == 1500 and not prof.stored
    # conservative preset: KryoFlux-derived movement/settle times, everything else untouched
    prof = dev.timing_preset("konservativ")
    assert (prof.timing["step_rate_us"], prof.timing["settle_us"], prof.timing["dir_change_us"],
            prof.timing["side_settle_us"], prof.timing["spinup_ms"], prof.timing["select_settle_us"]) == \
        (8000, 40000, 38000, 1000, 800, 60000)
    assert prof.extra["density_us"] == 60000 and prof.extra["motor_step_ms"] == 200
    assert prof.timing["precomp_ns"] == 0xFFFF and prof.extra["write_settle_us"] == 20000 and not prof.stored
    assert dev.timing()["step_rate_us"] == 8000, "TIMING reports the preset values"
    assert fake.requests[-2].payload == bytes([18, 2])
    with pytest.raises(KeyError):
        dev.timing_preset("turbo")
    with pytest.raises(P2.DeviceError):
        dev.timing_preset(7)
    # store: the effective settings go out as a 40-byte profile with the stored byte cleared
    stored = dev.operating_profile_store()
    assert stored.stored and fake.requests[-1].payload[0] == 18 and len(fake.requests[-1].payload) == 41
    assert fake.requests[-1].payload[40] == 0 and stored.timing["dir_change_us"] == 38000
    back = dev.timing_preset("standard")
    assert back.timing["dir_change_us"] == 15000 and back.stored


def test_decoder_tolerance(monkeypatch, capsys):
    import pytest
    fake = FakeV2()
    dev = P2.Device2(fake)
    assert dev.decoder_tolerance() == {"flags": 0, "flag_names": []}
    assert fake.requests[-1].payload == bytes([34])
    t = dev.decoder_tolerance("keep_bad_header+data_without_header")
    assert t["flags"] == 0x11 and t["flag_names"] == ["keep_bad_header", "data_without_header"]
    assert fake.requests[-1].payload == bytes([34, 1, 0x11, 0])
    assert dev.decoder_tolerance("0x1f")["flags"] == 0x1F and dev.decoder_tolerance(0)["flags"] == 0
    with pytest.raises(ValueError):
        dev.decoder_tolerance("keep_bad_header+bogus")
    with pytest.raises(ValueError):
        dev.decoder_tolerance(0x20)
    assert P2.flag_names(0xE8, P2.SECTOR_FLAGS) == ["no header", "header checksum", "data checksum", "epilogue"]
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["tolerance", "any_track"]) == 0 and "0x04 any_track" in capsys.readouterr().out
    assert cli.main(["tolerance"]) == 0 and "0x04 any_track" in capsys.readouterr().out
    assert cli.main(["tolerance", "reset"]) == 0 and "strict" in capsys.readouterr().out


def test_apple_gcr_options():
    import pytest
    fake = FakeV2()
    dev = P2.Device2(fake)
    o = dev.apple_gcr_options()
    assert o["addr_prologue"] == "d5aa96" and o["data_epilogue"] == "deaa" and o["flags"] == 0 and o["flag_names"] == []
    o = dev.apple_gcr_options(addr_prologue="d4aa96", flags="keep_bad_addr_checksum+any_track")
    assert o["addr_prologue"] == "d4aa96" and o["flags"] == 0x05 and o["flag_names"] == ["keep_bad_addr_checksum", "any_track"]
    assert fake.requests[-1].payload == bytes([33, 1]) + bytes.fromhex("d4aa96d5aaaddeaadeaa0500")
    with pytest.raises(ValueError):
        dev.apple_gcr_options(bogus=1)
    with pytest.raises(ValueError):
        dev.apple_gcr_options(addr_epilogue="de")
    with pytest.raises(P2.DeviceError):
        dev.apple_gcr_options(flags=0x40)
    assert dev.apple_gcr_options(flags=0)["flags"] == 0


def test_dump_start_format():
    fake = FakeV2()
    dev = P2.Device2(fake, timeout=0.5)
    dev.dump_start()
    assert fake.requests[-1].payload == b""
    dev.dump_start("a", 80, 2, 3)
    assert fake.requests[-1].payload == bytes([1, 80, 2, 3])
    dev.dump_start("b", 40, 1, 2, fmt="adf")
    assert fake.requests[-1].payload == bytes([2, 40, 1, 2, 2])
    dev.dump_start(fmt="img")
    assert fake.requests[-1].payload == bytes([1, 80, 2, 3, 1])
    dev.dump_start(fmt="hfe")
    assert fake.requests[-1].payload == bytes([1, 80, 2, 3, 3])


def test_iec_blocks():
    from ufi_host import cbm
    fake = FakeV2()
    dev = P2.Device2(fake, timeout=0.5)
    assert dev.iec_status() == "00, OK,00,00"
    assert dev.iec_block_read(18, 1) == bytes([18, 1]) * 128
    # 1.18: one structured request carries U1 and the DOS code (legacy pair tested elsewhere)
    assert fake.requests[-1].cmd == P2.IEC_READ_STATUS and fake.requests[-1].payload == bytes([1, 0, 18, 1])
    blk = bytes(range(256))
    assert dev.iec_block_write(17, 3, blk, device=9) == 0
    assert fake.requests[-1].payload == bytes([1, 17, 3, 9]) + blk
    assert dev.iec_block_read(17, 3) == blk
    assert dev.iec_block_write(36, 0, blk) == 26
    with pytest.raises(ValueError):
        dev.iec_block_write(1, 0, b"short")
    # geometry
    assert cbm.geometry(174848) == ("d64", 35) and cbm.geometry(196608) == ("d64", 40)
    assert cbm.geometry(349696) == ("d71", 70) and cbm.geometry(819200) == ("d81", 80)
    assert cbm.geometry(174848 + 683) == ("d64", 35)
    assert cbm.image_size("d64", 35) == 174848 and cbm.image_size("d71", 70) == 349696
    assert cbm.image_size("d81", 80) == 819200 and sum(1 for _ in cbm.blocks("d64", 40)) == 768
    assert [cbm.sectors_per_track("d64", t) for t in (1, 17, 18, 24, 25, 30, 31, 35)] == [21, 21, 19, 19, 18, 18, 17, 17]
    assert cbm.sectors_per_track("d71", 36) == 21 and cbm.kind_for("1571", 35) == ("d64", 35)
    with pytest.raises(ValueError):
        cbm.geometry(1000)


def test_sd_write_start():
    fake = FakeV2()
    dev = P2.Device2(fake, timeout=0.5)
    dev.sd_write_start("game.adf")
    assert fake.requests[-1].cmd == P2.WRITE_START and fake.requests[-1].payload == b"GAME.ADF"
    dev.sd_write_start()
    assert fake.requests[-1].payload == b""
