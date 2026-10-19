"""UFI v2 tests against a fake device implementing docs/USB_Protokoll.md section 3 on byte level."""
from __future__ import annotations

import struct
import zlib

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
    to the DOS code IEC_READ_STATUS reports instead of data.  DESCRIBE is answered
    (flags 3: firmware read-back and extended backend).  A WRITE with flags 0x21/0x61
    (extended + verify) models the firmware read-back: fewer than VERIFY_MIN_INTERVALS
    intervals is a source without reference sectors (END status 16, nothing written),
    otherwise the END reports verify level 3.
    """

    CAPS_1_18 = 0xFFFFFF7F
    VERIFY_MIN_INTERVALS = 1000     # 0x21 path: shorter sources end with 16 before WGATE, longer ones verify (level 3)

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
        self.soft_locked = False        # session write lock (op 37): engage-only, status 22 on disk/drive writes
        self.drive = 1                  # selected drive id (op 22 byte 20); SELECT replaces it
        self.job = None                 # analysis job (ops 1-4): started jobs finish at once, sectors 1..verify_sectors seen
        self.page_crc_error = 0         # xor into every op 3 page CRC (tests the host's page check)
        self.config_gen = 7             # generation block in READ/WRITE END (4.5): configuration generation; SELECT and accepted op 10/18 settings bump it
        self.media_gen = 42             # ... media generation (start and end are equal unless bumped mid-run)
        self.capture_id = 0x1234        # ... capture id reported by READ END (0 for WRITE)
        self.refusal = (0, 0, 0, 0, 0)  # op 39 (4.8): cmd, seq, status, field, detail of the last 16/21 refusal
        self.verify_report = bytes([1, 0, 0, 0, 0, 0])   # op 40 (4.9): header until a verified WRITE ran
        self.verify_sectors = 9         # reference sectors the 0x21 model reports (c = track, h = side, r = 1.., n = 2)
        self.speed_tolerance = 0        # op 41 (4.10): per mille, 0 = off
        self.index_ticks = 55_000_000   # the fake drive's own index period (300 rpm)
        self.speed_last = (0, 0)        # last check: measured, source
        self.last_start_offset = 0      # flags2 bit1 of the last accepted WRITE; END echoes it + 7 ticks "measured"
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

    def _gen_block(self, capture_id):
        """Generation block firmware 1.18 appends to READ/WRITE END (4.5): schema 1, config, media start, media end, capture id."""
        return P2.GEN_BLOCK.pack(1, self.config_gen, self.media_gen, self.media_gen, capture_id)

    def _provenance_block(self) -> bytes:
        """96-byte provenance block (4.12): v0.7 board at 2200 mV, a fixed MCU id, dirty build abc1234 of
        2026-10-17, the fake's generations, capture id, index period, speed check and drive."""
        flags = 1 | (4 if self.soft_locked else 0) | (16 if self.capture_id else 0)      # dirty build, no jumper
        block = bytes([1, 96, 7, flags]) + struct.pack("<IIIHHH", 0x00310028, 0x31355117, 0x20323633, 0x483, 0x1003, 2200)
        block += bytes(self.firmware) + b"abc1234".ljust(12, b"\0") + b"Oct 17 2026".ljust(12, b"\0")
        block += struct.pack("<7I", 275_000_000, self.config_gen, self.media_gen, self.capture_id, self.media_gen,
                             self.index_ticks, self.speed_last[0])
        return block + bytes([self.drive, 0]) + struct.pack("<HI", 0, 123_456) + struct.pack("<HHBB", 120, 0, 3, 8) + bytes(6)

    def _capability_matrix(self) -> bytes:
        """Op 22 as ufi_capability_matrix() builds it on a v0.7 board with a flux drive selected (4.3):
        head 1, 6, 32, 32; generation; flags 3 (CAPABILITY_UNCONFIRMED | WRITE_MODE_UNQUALIFIED);
        mask 63; 275 MHz; drive id and the op numbers 23..32; six rows (formats 1-6) with
        implemented/eligible 15, offline_tested 10, hardware_qualified 0, both raw backends,
        prerequisites 7, the format's rate mask and the classic/extended interval limits."""
        head = bytes([2, 9, 32, 32]) + struct.pack("<4I", self.config_gen, 3, 63, 275_000_000)
        head += bytes([self.drive, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 0])
        rows = bytes([0, 5, 5, 5, 0, 3, 3, 7]) + struct.pack("<6I", 0, 440, 65536, 220, 0xFFFFFFFF, 3)    # raw flux
        rows += b"".join(bytes([f, 15, 15, 10, 0, 3, 3, 7]) + struct.pack("<6I", rate, 440, 65536, 220, 0xFFFFFFFF, 3)
                         for f, rate in enumerate((0x0F, 0x0F, 0x37, 0x04, 0x1C1, 0x3E04), 1))
        rows += bytes([7, 7, 2, 2, 0, 1, 0, 7]) + struct.pack("<6I", 1, 440, 65536, 0, 0, 3)             # Apple II GCR, decoder only here
        rows += bytes([8, 13, 0, 13, 0, 0, 0, 5]) + struct.pack("<6I", 0, 0, 0, 0, 0, 3)                  # IEC block path, no IEC drive
        return head + rows

    def _job_status(self) -> bytes:
        """64-byte analysis job status (6.1): one attempt, 123 ms, the job's masks and C/H/N, capture id."""
        j = self.job
        return struct.pack("<BBBBIIIQQQBBBxIBBBBBBBBII", 1, j["state"], j["status"], 1, j["id"], 123, j["generation"],
                           j["expected"], j["seen"], j["expected"] & ~j["seen"], 0, 0, 0, 0, j["attempts"], j["flags"],
                           j["c"], j["h"], j["n"], 0, 0, 0, j["budget"], self.capture_id)

    def _recognition_block(self) -> bytes:
        """Result section 15 (5.6) of the fake's job: IBM MFM observed, geometry unconfirmed, the explicit
        set confirmed when every expected sector was seen, otherwise cause missing_sectors."""
        j = self.job
        missing = j["expected"] & ~j["seen"]
        flags = 1 | 2 | 8 | (256 if j["flags"] & 4 else 0) | (4 if j["flags"] & 4 and not missing else 0)
        profile = bytearray(12)
        profile[6] = 2                                            # media profile observed
        return (bytes([1, 1, 0, 0]) + struct.pack("<4I", flags, 1 if missing else 0, self.verify_sectors, 0)
                + struct.pack("<QQ", j["expected"], j["seen"]) + bytes(profile)
                + bytes([j["c"], j["h"], j["n"], 1 if j["flags"] & 4 else 0])
                + struct.pack("<II", j["generation"], self.capture_id) + bytes([1, 2, 0, j["status"]]))

    @staticmethod
    def _locked_write(cmd, a):
        """Requests the session write lock refuses (4.6): disk writes, SD-to-disk starts and IEC drive writes."""
        if cmd in (P2.WRITE, P2.GUARDED_WRITE, P2.ERASE, P2.PATTERN, P2.FORMAT, P2.COPY_START, P2.WRITE_START):
            return True
        if cmd == P2.IEC_BLOCK:
            return len(a) >= 1 and a[0] == 1
        if cmd == P2.IEC_MEM:
            return len(a) >= 1 and a[0] in (0, 2, 3)
        return cmd == P2.IEC_TARGET and len(a) >= 3 and a[2] in (0, 2, 3)

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
                _seq, count, _size, flags, flags2, track, side, source = self.write_req
                erased = 0x1234567 if flags2 & 1 else 0            # flags2 bit0: an erased tail up to the index
                status, level = 0, 2 if flags & 1 else 1
                if self.speed_tolerance and source:                # op 41: drive period against the source period
                    self.speed_last = (self.index_ticks, source)
                    if abs(self.index_ticks - source) > source * self.speed_tolerance // 1000:
                        self._frame(P2.T_END, P2.WRITE, f.seq, P2.SPEED_MISMATCH,
                                    struct.pack("<BI", 0, 0) + self._gen_block(0) + struct.pack("<I", 0))
                        self.write_req = None
                        return
                if flags & 1:                                      # op 40: a verified WRITE opens a new report
                    self.verify_report = bytes([1, 0, 0, 8, track, side])
                if flags & 0x21 == 0x21:
                    # firmware read-back (5.2): the firmware decodes the source before WGATE; a
                    # source without reference sectors ends with 16, a verified one with level 3
                    status, level = (P2.BAD_ARGS, 0) if count < self.VERIFY_MIN_INTERVALS else (0, 3)
                    if level == 3:
                        self.verify_report = bytes([1, 1, self.verify_sectors, 4, track, side]) + b"".join(
                            bytes([track, side, r, 2, 1]) for r in range(1, self.verify_sectors + 1))
                applied = self.last_start_offset + 7 if self.last_start_offset else 0   # measured: compare latency included
                weak_applied, weak_seed = 0, 0
                if getattr(self, "last_weak", None) is not None:                 # 4.16: replay the board's pass on the received data
                    weak_seed = self.last_weak[1] or 0xC0FFEE42                  # seed 0: the board picks one
                    weak_applied = P2.apply_weak(decode_write(bytes(self.write_code)), self.last_weak[0], weak_seed)[1]
                if getattr(self, "last_raw_check", False):                      # 4.17: fingerprint verdict, track 7 deviates
                    count = len(decode_write(bytes(self.write_code)))
                    ok = track != 7
                    opts = getattr(self, "raw_options", (0, 30))
                    self.raw_report = (bytes([1, 64, 1 if ok else 2, 1]) + struct.pack("<IIIII", source, self.index_ticks, count, count, 0 if ok else 900)
                                       + struct.pack("<H", opts[1]) + bytes(6) + bytes(256))
                    level = 2 if ok else 1
                    if not ok and opts[0] & 2:
                        status = 6
                self._frame(P2.T_END, P2.WRITE, f.seq, status, struct.pack("<BI", level, 3 if flags & P2.WRITE_CLAMP else 0)
                            + self._gen_block(0) + struct.pack("<IIII", erased, applied, weak_applied, weak_seed))
                self.write_req = None
            return
        assert f.type == P2.T_REQUEST and f.status == 0
        self.requests.append(f)
        cmd, seq, a = f.cmd, f.seq, f.payload
        r = lambda status=0, payload=b"": self._frame(P2.T_RESPONSE, cmd, seq, status, payload)
        if self.soft_locked and self._locked_write(cmd, a):
            r(P2.SOFT_WRITE_LOCK)
            return
        if cmd == P2.PING:
            r(0, a)
        elif cmd == P2.INFO:
            text = f"UFI Flux Engine {self.firmware[0]}.{self.firmware[1]}-dev (abc1234, 2026-10-17)"
            r(0, P2.INFO_HDR.pack(2, *self.firmware, 7, self.caps, 275_000_000, 8 << 20, 4096)
              + text.encode() + b"\0PSRAM ok\0")
        elif cmd == P2.STATUS:
            r(0, bytes([1, 1, 0, 1, 0, 1, 5, 0]) + P2.BOARD.pack(3, 8 | (0x80 if self.soft_locked else 0), 120, 0, 2200))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 41:          # speed check before writing (4.10)
            if len(a) == 4 and a[1] == 1 and struct.unpack_from("<H", a, 2)[0] <= 500:
                self.speed_tolerance = struct.unpack_from("<H", a, 2)[0]
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            r(0, struct.pack("<BHII", 1, self.speed_tolerance, *self.speed_last))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 1:           # analysis job start (6.1): capture and analysis end at once
            if len(a) < 28 or not a[27] & 1:                  # DETAIL required on the appended ANALYZE request
                r(P2.BAD_ARGS)
                return
            job_id, flags, attempts, budget, expected, c, h, n, res = struct.unpack_from("<IHBIQBBBB", a, 1)
            if not job_id or flags & ~0x7F or not 1 <= attempts <= 8 or not 1 <= budget <= 60000 or res or n > 3 \
                    or (flags & 1 and not flags & 4) or bool(flags & 4) != bool(expected):
                r(P2.BAD_ARGS)
                return
            seen = sum(1 << s for s in range(1, self.verify_sectors + 1))
            missing = expected & ~seen
            if missing and flags & 1 and attempts > 1 and flags & 0x40:      # RESEEK: one re-seek per retry attempt
                self.reseeks = getattr(self, "reseeks", 0) + attempts - 1
            self.job = dict(id=job_id, flags=flags, attempts=attempts, budget=budget, expected=expected, c=c, h=h, n=n,
                            track=a[24], side=a[25], revs=a[26] or 3, seen=seen, generation=1,
                            status=6 if missing else 0, state=4 if missing else 3)    # 6: expected sectors missing
            r(0, self._job_status())
        elif cmd == P2.ANALYSIS_JOB and a[0] in (2, 4):      # job status / abort: the tool's job id, nothing else
            if len(a) != 5 or not self.job or struct.unpack_from("<I", a, 1)[0] != self.job["id"]:
                r(P2.BAD_ARGS)
                return
            if a[0] == 4 and self.job["state"] < 3:
                self.job.update(state=4, status=20)
            r(0, self._job_status())
        elif cmd == P2.ANALYSIS_JOB and a[0] == 3:           # result pages with header and CRC32 (6.1, op 3)
            if len(a) != 16 or not self.job:
                r(P2.BAD_ARGS)
                return
            job_id, gen, section, offset, count = struct.unpack_from("<IIBIH", a, 1)
            body = {2: self._job_status, 15: self._recognition_block}.get(section)
            if job_id != self.job["id"] or gen != self.job["generation"] or not 1 <= count <= 1024 or body is None:
                r(P2.BAD_ARGS)
                return
            data = body()
            if offset > len(data):
                r(P2.BAD_ARGS)
                return
            page = data[offset:offset + count]
            r(0, struct.pack("<BBHIIIII", 1, section, 0, job_id, gen, offset, len(data),
                             zlib.crc32(page) ^ self.page_crc_error) + page)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 21:          # recognition discovery (5.6)
            r(P2.BAD_ARGS if len(a) != 1 else 0, b"" if len(a) != 1 else
              bytes([1, 15, 16, 20]) + struct.pack("<5I", 64, 1023, 32767, 8, 128))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 22:          # capability matrix (4.3), 224 bytes
            r(P2.BAD_ARGS if len(a) != 1 else 0, b"" if len(a) != 1 else self._capability_matrix())
        elif cmd == P2.ANALYSIS_JOB and a[0] == 23:          # configuration report (4.3): [23] or [23, expected u32]
            if len(a) not in (1, 5):
                r(P2.BAD_ARGS)
                return
            expected = struct.unpack_from("<I", a, 1)[0] if len(a) == 5 else 0
            changed = 4 if len(a) == 5 and expected != self.config_gen else 0        # PROFILE_CHANGED
            r(0, bytes([1, 24, 1, 0]) + struct.pack("<5I", self.config_gen, expected, changed, self.media_gen, 4))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 40:          # per-sector verify report (4.9)
            r(P2.BAD_ARGS if len(a) != 1 else 0, b"" if len(a) != 1 else self.verify_report)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 39:          # last refusal (4.8)
            r(P2.BAD_ARGS if len(a) != 1 else 0, b"" if len(a) != 1 else bytes([1, *self.refusal]))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 38:          # flag masks the parsers accept (4.7)
            r(P2.BAD_ARGS if len(a) != 1 else 0, b"" if len(a) != 1 else
              bytes([1, 0x1F, 0xFF, 0x1F, 0xFF, 7]) + struct.pack("<HHHH", 0x3F, 0x1F, 0x07, 0x18) + bytes([0xB1, 0]))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 31:          # raw fingerprint options (6.3): [31] reads, [31, 1, flags, tol u16] sets
            opts = getattr(self, "raw_options", (0, 30))
            if len(a) == 5 and a[1] == 1:
                flags, tol = a[2], struct.unpack_from("<H", a, 3)[0]
                if flags not in (0, 1, 3) or not 1 <= tol <= 1000:
                    r(P2.BAD_ARGS)
                    return
                opts = self.raw_options = (flags, tol)
                self.config_gen += 1
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            r(0, bytes([1, opts[0]]) + struct.pack("<H", opts[1]) + bytes([3, 32, 64, 0]))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 32:          # last raw fingerprint report (6.3), 288 bytes or NOT_FOUND
            rep = getattr(self, "raw_report", None)
            if len(a) != 1:
                r(P2.BAD_ARGS)
            elif rep is None:
                r(17)
            else:
                r(0, rep)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 37:          # session write lock (4.6): [37] reads, [37, 1] engages
            if len(a) == 2 and a[1] == 1:
                self.soft_locked = True
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            r(0, bytes([1, 1 if self.soft_locked else 0]))
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
                self.config_gen += 1                          # accepted settings change the configuration generation (4.3)
            r(0, bytes([1]) + self.extra)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 33:          # Apple II GCR options (5.7)
            if len(a) == 14:
                if a[1] != 1 or (a[12] | a[13] << 8) & ~0x3F or any(b < 0x80 for b in a[2:12]):
                    r(P2.BAD_ARGS)
                    return
                self.apple = bytes(a[2:14])
            r(0, bytes([1]) + getattr(self, "apple", bytes.fromhex("d5aa96d5aaaddeaadeaa0000")))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 35:          # Apple II nibble statistics (5.7)
            if len(a) != 1:
                r(P2.BAD_ARGS)
                return
            st = getattr(self, "apple_stats", None)
            if st is None:                                    # no format 7 analysis yet (schema 2, 274 bytes)
                r(0, bytes([2, 0]) + bytes(272))
            else:
                r(0, st)
        elif cmd == P2.ANALYSIS_JOB and a[0] == 36:          # Apple II 6-and-2 symbol table (5.7)
            if len(a) == 66:
                if a[1] != 1 or len(set(a[2:])) != 64 or min(a[2:]) < 0x80:
                    r(P2.BAD_ARGS)
                    return
                self.apple_table = bytes(a[2:])
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            r(0, bytes([1]) + getattr(self, "apple_table", P2.APPLE_STD_TABLE))
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
                           2: ((8000, 40000, 38000, 1000, 800, 60000), (60000, 200)),
                           3: ((8000, 20000, 20000, 1000, 1000, 20000), (0, 100))}       # 8 inch (1.18)
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
            if len(a) > 1:
                self.config_gen += 1                          # accepted settings change the configuration generation (4.3)
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
        elif cmd == P2.DESCRIBE:                             # 1.18: transport/clock limits; flags bit0 firmware read-back, bit1 extended backend
            r(P2.BAD_ARGS if a else 0, b"" if a else P2.DESCRIBE_FMT.pack(1, 3, 3, 65536, 275_000_000, 68_750_000,
                                                                          1 << 21, 1024, 200, 0x3fffffff, 110, 100000))
        elif cmd == P2.WRITE_LIMITS:
            r(P2.BAD_ARGS if a else 0, b"" if a else P2.WRITE_LIMITS_FMT.pack(1, 255 if self.firmware >= (1, 18) else 7, 275_000_000, 1 << 21, 440, 65536))
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
            if a[1] & 0x4E or (not a[1] & 0x80 and a[1] > 1):    # side byte: side, or 0x80 | quarter << 4 (1.18)
                r(P2.BAD_ARGS)
                return
            self.last_analyze = (a[0], a[1], a[2] or 3)      # op 42 header: track, side byte, revs
            r(0, P2.ANALYZE_FMT.pack(a[2] or 3, 2, 2000, 30012, 4000, 6000, 8000, 0, 55_000_000,
                                     50_000, 100_000, 18, 2, 1234, 0x11))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 43:          # provenance block (4.12), 96 bytes
            if len(a) != 1:
                r(P2.BAD_ARGS)
                return
            r(0, self._provenance_block())
        elif cmd == P2.ANALYSIS_JOB and a[0] == 46:          # track survey (4.15): [46] reads, [46, 1, first, last, sides, flags] starts, [46, 0] aborts
            sv = getattr(self, "survey", None)
            if len(a) == 6 and a[1] == 1:
                if a[2] > a[3] or a[3] > 83 or a[4] not in (1, 2, 3) or a[5] & ~1:
                    r(P2.BAD_ARGS)
                    return
                if sv and sv["state"] == 1:
                    r(P2.BUSY)
                    return
                sides = [0, 1] if a[4] == 3 else [a[4] - 1]
                steps = [(t, s) for t in range(a[2], a[3] + 1) for s in sides]
                sv = self.survey = dict(state=1, error=0, first=a[2], last=a[3], sides=a[4], flags=a[5], cursor=0,
                                        total=len(steps), steps=steps, config=self.config_gen, media=self.media_gen, polls=0)
            elif len(a) == 2 and a[1] == 0:
                if sv and sv["state"] == 1:
                    sv["state"], sv["error"] = 3, 20
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            elif sv and sv["state"] == 1:                   # every read finishes half of the remaining steps
                sv["polls"] += 1
                sv["cursor"] = min(sv["total"], sv["cursor"] + max(1, sv["total"] // 2))
                if sv["cursor"] == sv["total"]:
                    sv["state"] = 2
            if not sv:
                r(0, bytes([1, 28, 0, 0, 0, 0, 0, 0, 20, 0]) + bytes(18))
                return
            body = bytearray()
            for t, s in sv["steps"][:sv["cursor"]]:
                if (t, s) == (2, 1):
                    body += struct.pack("<BBBBHBBIIHH", t, s, 4, 0, 0, 0, 0, 0, 0, 0, 0)       # NO_INDEX on one step
                else:
                    body += struct.pack("<BBBBHBBIIHH", t, s, 0, 2, 4050, 9 if sv["flags"] & 1 else 0, 3,
                                        55_000_000, 50_000 + t, 6050, 8050)
            r(0, bytes([1, 28, sv["state"], sv["error"], sv["first"], sv["last"], sv["sides"], sv["flags"], 20, 0])
              + struct.pack("<HHHIII", sv["cursor"], sv["total"], 0, 1500 * sv["cursor"], sv["config"], sv["media"]) + bytes(body))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 45:          # precomp profile (4.14): [45] reads, [45, 0] clears, [45, 1, n, zones] sets
            zones = getattr(self, "precomp_zones", [])
            if len(a) == 2 and a[1] == 0:
                zones = []
            elif len(a) >= 3 and a[1] == 1 and 1 <= a[2] <= 4 and len(a) == 3 + 3 * a[2]:
                new = [struct.unpack_from("<BH", a, 3 + 3 * i) for i in range(a[2])]
                if new[0][0] != 0 or any(b[0] <= x[0] for x, b in zip(new, new[1:], strict=False)) or any(ns > 2000 for _, ns in new):
                    r(P2.BAD_ARGS)
                    return
                zones = new
            elif len(a) != 1:
                r(P2.BAD_ARGS)
                return
            self.precomp_zones = zones
            last = getattr(self, "last_precomp", None)
            track = getattr(self, "last_write_track", 0)
            source = 2 if last is not None else 1 if zones else 0
            ns = last if last is not None else next((ns for f, ns in reversed(zones) if track >= f), 0) if zones else (125 if track > 60 else 62 if track >= 40 else 0)
            body = bytes([1, source, len(zones)]) + b"".join(struct.pack("<BH", f, n) for f, n in zones)
            r(0, body.ljust(15, b"\0") + struct.pack("<HBB", ns, track, track))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 44:          # weak positions of the last ANALYZE (4.13): [44] or [44, threshold]
            if len(a) > 2 or (len(a) == 2 and a[1] == 0):
                r(P2.BAD_ARGS)
                return
            threshold = a[1] if len(a) == 2 else 2
            track, side_byte, revs = getattr(self, "last_analyze", (0, 0, 0))
            fine = {72: (3, 10), 77: (2, 11), 290: (5, 9)} if revs >= 2 else {}        # window: (diff, min)
            hits = [(w, d, m) for w, (d, m) in sorted(fine.items()) if d >= threshold]
            r(0, struct.pack("<BBBBHBBH", 1, revs, track, side_byte, 1024, threshold, 0, len(hits))
              + b"".join(struct.pack("<HBB", w, d, m) for w, d, m in hits))
        elif cmd == P2.ANALYSIS_JOB and a[0] == 42:          # weak windows of the last ANALYZE (4.11)
            if len(a) != 1:
                r(P2.BAD_ARGS)
                return
            track, side_byte, revs = getattr(self, "last_analyze", (0, 0, 0))
            counts = bytearray(64)
            if revs >= 2:
                counts[0], counts[4], counts[9] = 3, 5, 1          # weak_mask 0x11 = windows 0 and 4 at threshold 3
            r(0, bytes([1, revs if revs >= 2 else 0, track, side_byte]) + bytes(counts))
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
            _track, _side, revs, flags, _period = P2.READ_REQ.unpack_from(a)
            if flags & ~0x1F:                                 # op 38 READ mask: bit3 provenance in END (4.12), bit4 progress events (2.3)
                self.refusal = (cmd, seq, P2.BAD_ARGS, 2, flags & ~0x1F)
                r(P2.BAD_ARGS)
                return
            r()
            if self.read_status in (P2.CANCELLED, P2.MEDIA_CHANGED):
                self._frame(P2.T_END, cmd, seq, self.read_status)
                return
            data = encode_stream(self.revs[:revs])
            for k in range(0, len(data), self.data_chunk):
                self._frame(P2.T_DATA, cmd, seq, 0, data[k:k + self.data_chunk])
            if flags & 0x10:                                  # bit4: one progress event per revolution (sent before the END)
                for n, rev in enumerate(self.revs[:revs], 1):
                    self.emit_event(0x85, struct.pack("<BII", n, rev[0] if isinstance(rev, tuple) else rev.index_ticks, 200 * n))
            self._frame(P2.T_END, cmd, seq, self.read_status, bytes([min(revs, len(self.revs))]) + self._gen_block(self.capture_id)
                        + (self._provenance_block() if flags & 0x08 else b""))
        elif cmd == P2.WRITE:
            flags = a[2]                    # optional fields in flag order, then an optional flags2 byte (1.18)
            if flags & 0x40 and not flags & 0x20:                 # bit6 needs extended timing (0x60; 0x61 before 1.18-dev)
                self.refusal = (cmd, seq, P2.BAD_ARGS, 8, 1)
                r(P2.BAD_ARGS)
                return
            want = 11 + (1 if flags & 0x02 else 0) + (2 if flags & 0x08 else 0) + (4 if flags & 0x40 else 0) + (12 if flags & 0x80 else 0)
            flags2 = a[want] if len(a) > want else 0
            start_offset = struct.unpack_from("<I", a, want + 1)[0] if flags2 & 2 and len(a) >= want + 5 else 0
            at2 = want + (5 if flags2 & 2 else 1)                                 # after the flags2 byte and the offset
            precomp = struct.unpack_from("<H", a, at2)[0] if flags2 & 4 and len(a) >= at2 + 2 else None
            at3 = at2 + (2 if flags2 & 4 else 0)                                   # flags2 bit3 (4.16): jitter u16 + seed u32
            weak = struct.unpack_from("<HI", a, at3) if flags2 & 8 and len(a) >= at3 + 6 else None
            expect = (at3 + (6 if flags2 & 8 else 0)) if len(a) > want else want
            bad_weak = weak is not None and (weak[0] > 8192 or not flags & 0x20 or flags & 1)
            bad_raw = bool(flags2 & 0x10) and (not flags & 0x40 or flags & 1)          # flags2 bit4 (4.17): needs index_ticks, not with verify
            if flags2 & ~0x1F or len(a) != expect or (flags2 & 2 and not 1 <= start_offset <= 275_000_000) \
                    or (precomp is not None and precomp > 2000) or bad_weak or bad_raw:
                self.refusal = (cmd, seq, P2.BAD_ARGS) + ((10, flags2 & ~0x1F) if flags2 & ~0x1F else
                                                          (17, 1) if bad_raw and not flags & 0x40 else
                                                          (17, 2) if bad_raw else
                                                          (16, 1) if flags2 & 8 and weak is None else
                                                          (16, 2) if bad_weak and weak[0] > 8192 else
                                                          (16, 3) if bad_weak and not flags & 0x20 else
                                                          (16, 4) if bad_weak else
                                                          (15, 2) if precomp is not None and precomp > 2000 else
                                                          (14, 2) if flags2 & 2 and len(a) == expect else
                                                          (15, 1) if flags2 & 4 and len(a) < expect else (1, expect))
                r(P2.BAD_ARGS)
                return
            if precomp is not None and flags & 0x20:
                r(11)                                                          # NOT_IMPL: extended intervals are host-timed
                return
            self.last_weak = weak                                              # (jitter, seed) or None
            self.last_raw_check = bool(flags2 & 0x10)
            self.last_precomp = precomp                                        # None: profile / TIMING decides
            self.last_write_track = a[0]
            self.last_start_offset = start_offset
            at = 11 + (1 if flags & 0x02 else 0) + (2 if flags & 0x08 else 0)
            source = struct.unpack_from("<I", a, at)[0] if flags & 0x40 else 0   # bit6: source index period
            self.write_req = (seq, *struct.unpack_from("<II", a, 3), flags, flags2, a[0], a[1], source)
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
        elif cmd == P2.SELECT:                                # selection changes the configuration generation (4.3)
            self.drive = a[0] if a else 0
            self.config_gen += 1
            r()
        elif cmd in (P2.MOTOR, P2.SEEK, P2.RECAL, P2.SIDE, P2.ABORT, P2.IEC_RESET,
                     P2.IEC_SEND, P2.USB_MODE, P2.DUMP_START, P2.DUMP_ABORT, P2.COPY_START,
                     P2.WRITE_START):
            r()
        else:
            r(P2.UNKNOWN_COMMAND)


class GuardedDevice(FakeV2):
    """FakeV2 plus the media-bound starts of 4.5: op 25 discovery (schema 2, 20 bytes),
    GUARDED_WRITE 0x27 and GUARDED_READ 0x28 check `config_gen` / `media_gen` (16 stale
    configuration, 21 other medium) and then behave like WRITE / READ with DATA and END on
    0x22 / 0x20.  bump_media() simulates a disk change between requests (a bridge calls it
    after the N-th accepted guarded start); `guarded_starts` counts the accepted ones.
    Same constructor arguments as FakeV2."""

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self.guarded_starts = 0

    def bump_media(self):
        self.media_gen += 1

    def _handle(self, f: P2.Frame):
        if f.type == P2.T_REQUEST and f.cmd == P2.ANALYSIS_JOB and f.payload == bytes([25]):
            self.requests.append(f)
            self._frame(P2.T_RESPONSE, f.cmd, f.seq, 0,
                        bytes([2, 0x27, 0x22, 8]) + struct.pack("<III", 3, self.config_gen, self.media_gen)
                        + bytes([0x28, 0x20, 8, 0]))
            return
        if f.type == P2.T_REQUEST and f.cmd in (P2.GUARDED_WRITE, P2.GUARDED_READ):
            inner = P2.WRITE if f.cmd == P2.GUARDED_WRITE else P2.READ
            if self.soft_locked and inner == P2.WRITE:
                self.requests.append(f)
                self._frame(P2.T_RESPONSE, f.cmd, f.seq, P2.SOFT_WRITE_LOCK)
                return
            if len(f.payload) < 8:
                self.requests.append(f)
                self.refusal = (f.cmd, f.seq, P2.BAD_ARGS, 1, 11)
                self._frame(P2.T_RESPONSE, f.cmd, f.seq, P2.BAD_ARGS)
                return
            config, media = struct.unpack_from("<II", f.payload)
            if config != self.config_gen:
                self.requests.append(f)
                self.refusal = (f.cmd, f.seq, P2.BAD_ARGS, 11, 1)
                self._frame(P2.T_RESPONSE, f.cmd, f.seq, P2.BAD_ARGS)
                return
            if media != self.media_gen:
                self.requests.append(f)
                self.refusal = (f.cmd, f.seq, P2.MEDIA_CHANGED, 12, 1)
                self._frame(P2.T_RESPONSE, f.cmd, f.seq, P2.MEDIA_CHANGED)
                return
            self.guarded_starts += 1
            # accepted: the plain handler runs, its response is re-labelled with the guarded command
            mark = len(self.out)
            super()._handle(P2.Frame(f.type, inner, f.seq, f.status, f.payload[8:]))
            self.requests[-1] = f                            # log the guarded request, not the inner one
            reply = P2.FrameParser().feed(bytes(self.out[mark:]))
            self.out = self.out[:mark]
            for r in reply:
                if r.type == P2.T_RESPONSE and r.cmd == inner:
                    r = P2.Frame(r.type, f.cmd, r.seq, r.status, r.payload)
                self.out += P2.encode_frame(r.type, r.cmd, r.seq, r.status, r.payload)
            return
        super()._handle(f)


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
    assert dev.analyze(17, 0, 2, quarter=3)["revs"] == 2 and fake.requests[-1].payload == bytes([17, 0xB0, 2])
    assert dev.analyze(17, 1, 2, quarter=0)["revs"] == 2 and fake.requests[-1].payload == bytes([17, 0x81, 2])
    with pytest.raises(ValueError):
        dev.analyze(17, 0, 2, quarter=4)
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
    assert fake.events_mask == P2.EVENTS_ALL == 0x3F
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
        (1, 255, 275_000_000, 1 << 21, 440, 65536)
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
    prof = dev.timing_preset("8zoll")                                            # 8-inch preset (1.18)
    assert (prof.timing["step_rate_us"], prof.timing["settle_us"], prof.timing["spinup_ms"], prof.extra["motor_step_ms"]) == \
        (8000, 20000, 1000, 100) and fake.requests[-1].payload == bytes([18, 3])
    dev.timing_preset("konservativ")                                             # back to the values the store check expects
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


def test_session_write_lock(monkeypatch, capsys):
    """Op 37: engage-only session lock; writes refused with status 22, reads and SD files allowed."""
    import pytest
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.write_lock() == {"session": False, "jumper": False, "locked": False}
    assert fake.requests[-1].payload == bytes([37]) and not dev.status().board.session_locked
    assert dev.write_lock(engage=True)["session"] and fake.requests[-1].payload == bytes([37, 1])
    assert dev.write_lock()["locked"] and dev.status().board.session_locked
    deltas = [1100] * 2000
    for call in (lambda: dev.write_track(3, 1, deltas), lambda: dev.erase(3, 1),
                 lambda: dev.pattern(3, 1, 4000, 100), lambda: dev.iec_block_write(18, 0, b"\0" * 256)):
        with pytest.raises(P2.DeviceError) as e:
            call()
        assert e.value.status == P2.SOFT_WRITE_LOCK and "session write lock" in str(e.value)
    assert fake.write_req is None, "the WRITE was refused before any DATA frame"
    assert len(dev.read_track(0, 0, 2).revolutions) == 2    # reads stay allowed
    assert dev.iec_block_read(18, 0) and fake.requests[-1].cmd == P2.IEC_READ_STATUS
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["write-lock"]) == 0 and "engaged" in capsys.readouterr().out
    fake2 = FakeV2()
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake2)
    assert cli.main(["write-lock"]) == 0 and "session lock off" in capsys.readouterr().out
    assert cli.main(["write-lock", "engage"]) == 0 and fake2.soft_locked and "engaged" in capsys.readouterr().out


def test_raw_check(monkeypatch, capsys, tmp_path):
    """flags2 bit4 (4.17) and op 31/32 against the fake: options read/set/refused, no report before a write,
    write_track(raw_check=True) sends flags2 0x10 behind the index period, the END carries level 2 on a match
    and level 1 (status 0) on a deviation unless REQUIRE_TIMING_MATCH turns it into status 6, the report
    decodes, local and board-side refusals (field 17), and the CLI commands."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.raw_verify_report() is None
    o = dev.raw_verify_options()
    assert (o["flags"], o["tolerance_permille"], o["supported_mask"], o["report_op"], o["windows"]) == (0, 30, 3, 32, 64)
    gen = fake.config_gen
    assert dev.raw_verify_options(tolerance_permille=50)["tolerance_permille"] == 50 and fake.requests[-1].payload == bytes([31, 1, 0, 50, 0])
    assert dev.raw_verify_options(flags=P2.REQUIRE_TIMING_MATCH | P2.RAW_TIMING_PROFILE)["flags"] == 3 and fake.config_gen == gen + 2
    for bad in (dict(flags=4), dict(tolerance_permille=0), dict(tolerance_permille=1001)):
        with pytest.raises(ValueError):
            dev.raw_verify_options(**bad)
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([31, 1, 2, 30, 0]))                      # REQUIRE without PROFILE: board refuses
    dev.raw_verify_options(flags=0, tolerance_permille=30)
    deltas = [1100] * 3000
    r = dev.write_track(5, 0, deltas, extended=True, index_ticks=55_000_000, raw_check=True)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert req[2] == 0x60 and req[15] == 0x10 and len(req) == 16 and r.verify_level == 2
    rep = dev.raw_verify_report()
    assert (rep["verdict_name"], rep["lossy"], rep["source_period"], rep["source_count"], rep["distance"], rep["tolerance_permille"], len(rep["windows"])) == \
        ("plausible", True, 55_000_000, 3000, 0, 30, 64)
    r = dev.write_track(7, 0, deltas, extended=True, index_ticks=55_000_000, raw_check=True)       # fake: track 7 deviates
    assert r.verify_level == 1 and dev.raw_verify_report()["verdict"] == 2
    dev.raw_verify_options(flags=3)
    with pytest.raises(P2.DeviceError) as err:
        dev.write_track(7, 0, deltas, extended=True, index_ticks=55_000_000, raw_check=True)
    assert err.value.status == 6
    dev.raw_verify_options(flags=0)
    r = dev.write_track(5, 0, P2.mark_weak(deltas, [(100_000, 120_000)]), extended=True, index_ticks=55_000_000,
                        raw_check=True, weak_jitter=100)                                           # with weak markers
    assert r.verify_level == 2 and r.weak_applied > 0 and [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload[15] == 0x18
    for kw in (dict(extended=True), dict(extended=False, index_ticks=None), dict(extended=True, index_ticks=55_000_000, verify=True),
               dict(extended=True, index_ticks=55_000_000, firmware_verify=True)):
        with pytest.raises(ValueError):
            dev.write_track(5, 0, deltas, raw_check=True, **kw)
    for flags, detail in ((0x20, 1), (0x61, 2)):
        with pytest.raises(P2.DeviceError):
            dev.request(P2.WRITE, P2.WRITE_REQ.pack(5, 0, flags, 3, 3) + (struct.pack("<I", 1) if flags & 0x40 else b"") + bytes([0x10]))
        lr = dev.last_refusal()
        assert (lr["field"], lr["field_name"], lr["detail"]) == (17, "raw check", detail)
    cap = P2.Device2(FakeV2(REVS)).read_track(2, 1, 2)
    path = tmp_path / "t.scp"
    cli.write_scp(str(path), {10: [cli._to_scp(rv) for rv in cap.revolutions]}, 2)
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["--extended", "write", str(path), "5", "0", "--raw-check"]) == 0
    out = capsys.readouterr().out
    assert "raw fingerprint: plausible" in out and "64 windows" in out
    assert cli.main(["raw-check", "--tolerance", "40", "--require"]) == 0
    out = capsys.readouterr().out
    assert "tolerance 40" in out and "required" in out and "plausible" in out


def test_weak_markers(monkeypatch, capsys, tmp_path):
    """flags2 bit3 (4.16) against the fake: mark_weak() sets the markers inside absolute windows, apply_weak()
    replays the board's pass (length kept, 220-tick floor, drops shorten the list), write_track(weak_jitter=)
    sends jitter u16 + seed u32 behind flags2 and the END returns the count and the seed (host's or board's),
    local refusals, the firmware's field 16 reasons, old END frames, and ufi write --weak-window."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.write_limits().flags & 128
    deltas = [1100] * 3000
    marked = P2.mark_weak(deltas, [(100_000, 120_000)])
    assert [i for i, d in enumerate(marked) if d & P2.WEAK_JITTER] == list(range(90, 109))   # transitions at 1100 * (i + 1)
    marked = P2.mark_weak(marked, [(2_000_000, 2_010_000), (3_299_000, 3_300_001)], drop=True)
    assert marked[-1] == 1100 and marked[2999 - 1] & P2.WEAK_DROP == 0            # the last interval is never marked
    assert sum(1 for d in marked if d & P2.WEAK_DROP) == 9
    local, applied = P2.apply_weak(marked, 300, 0x12345678)
    assert sum(local) == sum(deltas) and len(local) <= 3000 and min(local) >= 220 and applied >= 3000 - len(local) > 0
    assert local[:90] == [1100] * 90 and local[-1] == 1100 and P2.apply_weak(marked, 300, 0x12345678) == (local, applied)
    assert P2.apply_weak(deltas, 300, 1) == (deltas, 0) and P2.apply_weak(marked, 0, 0) == P2.apply_weak(marked, 0, 0x9E3779B9)
    r = dev.write_track(5, 0, marked, extended=True, index_ticks=55_000_000, weak_jitter=300, weak_seed=0x12345678)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert req[2] == 0x60 and req[15] == 0x08 and req[16:22] == struct.pack("<HI", 300, 0x12345678) and len(req) == 22
    assert decode_write(bytes(fake.write_code)) == marked                      # markers travel inside the u32 intervals
    assert (r.weak_applied, r.weak_seed, r.start_offset_applied, r.erased_ticks) == (applied, 0x12345678, 0, 0)
    r = dev.write_track(5, 0, marked, extended=True, weak_jitter=0)               # board seed, drop markers only
    assert r.weak_seed == 0xC0FFEE42 and r.weak_applied == P2.apply_weak(marked, 0, 0xC0FFEE42)[1] > 0
    assert dev.write_track(5, 0, deltas, extended=True).weak_seed == 0           # no markers: nothing reported
    for kw in (dict(weak_jitter=8193), dict(weak_jitter=5, verify=True), dict(weak_jitter=5, firmware_verify=True),
               dict(weak_jitter=5, extended=False), dict(weak_jitter=5, weak_seed=1 << 32), dict(weak_jitter=-1)):
        with pytest.raises(ValueError):
            dev.write_track(5, 0, marked, **{**dict(extended=True), **kw})
    with pytest.raises(ValueError):
        dev.write_track(5, 0, [1100, P2.WEAK_VALUE + 1, 1100], extended=True, weak_jitter=5)   # value beyond 30 bits
    with pytest.raises(ValueError):
        P2.Device2(FakeV2(revolutions=REVS, firmware=(1, 15), caps=0x7FFF)).write_track(5, 0, marked, extended=True, weak_jitter=5)
    head = P2.WRITE_REQ.pack(5, 0, 0x20, 3, 3)
    for tail, detail in ((bytes([8, 5, 0]), 1), (bytes([8]) + struct.pack("<HI", 8193, 0), 2),
                         (bytes([8]) + struct.pack("<HI", 5, 0), 3), (bytes([8]) + struct.pack("<HI", 5, 0), 4)):
        payload = (P2.WRITE_REQ.pack(5, 0, 0x00 if detail == 3 else 0x21 if detail == 4 else 0x20, 3, 3)) + tail
        with pytest.raises(P2.DeviceError):
            dev.request(P2.WRITE, payload)
        lr = dev.last_refusal()
        assert (lr["field"], lr["field_name"], lr["detail"]) == (16, "weak markers", detail)
    assert head  # the plain head is accepted elsewhere
    end = struct.pack("<BI", 0, 0) + P2.GEN_BLOCK.pack(1, 7, 42, 42, 0) + struct.pack("<IIII", 0, 0, 7, 0xABCD)
    assert (P2.WriteResult.unpack(end).weak_applied, P2.WriteResult.unpack(end).weak_seed) == (7, 0xABCD)
    assert P2.WriteResult.unpack(end[:30]).weak_seed == 0 and P2.WriteResult.unpack(end[:30]).start_offset_applied == 0
    cap = P2.Device2(FakeV2(REVS)).read_track(2, 1, 2)
    path = tmp_path / "t.scp"
    cli.write_scp(str(path), {10: [cli._to_scp(rv) for rv in cap.revolutions]}, 2)
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["--extended", "write", str(path), "5", "0", "--weak-jitter", "300", "--weak-seed", "0x12345678",
                     "--weak-window", "100000:120000,200000:210000", "--weak-drop-window", "300000:310000"]) == 0
    out = capsys.readouterr().out
    assert "written" in out and "transitions randomized (seed 0x12345678)" in out and fake.last_weak == (300, 0x12345678)
    with pytest.raises(SystemExit) as stop:                                        # windows need --weak-jitter
        cli.main(["--extended", "write", str(path), "5", "0", "--weak-window", "1:2"])
    assert "--weak-jitter" in str(stop.value)


def test_start_offset():
    """flags2 bit1 + u32: announced by WRITE_LIMITS bit 4, range checked locally, END reports the measured offset."""
    import pytest
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    deltas = [1100] * 2000
    r = dev.write_track(3, 1, deltas, start_offset=10_000)
    req = fake.requests[-1].payload
    assert req[11] == 2 and req[12:16] == struct.pack("<I", 10_000) and len(req) == 16
    assert r.start_offset_applied == 10_007 and r.erased_ticks == 0
    r = dev.write_track(3, 1, deltas, start_offset=20_000, erase_rest=True)
    assert fake.requests[-1].payload[11] == 3 and r.start_offset_applied == 20_007 and r.erased_ticks == 0x1234567
    assert dev.write_track(3, 1, deltas).start_offset_applied == 0
    for bad in (0, 275_000_001):
        with pytest.raises(ValueError):
            dev.write_track(3, 1, deltas, start_offset=bad)
    with pytest.raises(ValueError):
        P2.Device2(FakeV2(revolutions=REVS, firmware=(1, 15), caps=0x7FFF)).write_track(3, 1, deltas, start_offset=5)
    dev.send(P2.T_REQUEST, P2.WRITE, 78, P2.WRITE_REQ.pack(0, 0, 0, 3, 4) + b"\x02")          # bit1 without the u32
    assert dev._wait(P2.WRITE, 78, (P2.T_RESPONSE,), 1.0).status == P2.BAD_ARGS
    assert dev.last_refusal()["field_name"] == "length"
    end = struct.pack("<BI", 1, 0) + P2.GEN_BLOCK.pack(1, 7, 42, 42, 0) + struct.pack("<II", 5, 9)
    assert (P2.WriteResult.unpack(end).erased_ticks, P2.WriteResult.unpack(end).start_offset_applied) == (5, 9)
    assert P2.WriteResult.unpack(end[:26]).start_offset_applied == 0


def test_weak_windows(monkeypatch, capsys):
    """Op 42: raw max-min counts of the last ANALYZE with its track and side byte; revs 0 before any analyze."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    ww = dev.weak_windows()
    assert ww["revs"] == 0 and len(ww["counts"]) == 64 and not any(ww["counts"]) and fake.requests[-1].payload == bytes([42])
    a = dev.analyze(17, 1, revs=4)
    assert a["weak_mask"] == 0x11
    ww = dev.weak_windows()
    assert (ww["revs"], ww["track"], ww["side_byte"]) == (4, 17, 1)
    assert ww["counts"][0] == 3 and ww["counts"][4] == 5 and ww["counts"][9] == 1 and sum(ww["counts"]) == 9
    assert [w for w, c in enumerate(ww["counts"]) if c >= 3] == [0, 4]        # the host threshold reproduces the mask
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["weak", "--min", "2"]) == 0
    out = capsys.readouterr().out
    assert "track 17 side byte 0x01, 4 revs" in out and "w0=3, w4=5" in out and "w9" not in out
    monkeypatch.setattr(P2, "open_serial", lambda port=None: FakeV2())
    assert cli.main(["weak"]) == 0 and "no weak-window data" in capsys.readouterr().out


def test_speed_check(monkeypatch, capsys):
    """Op 41: tolerance read/set, writes with a source index period end with 23 outside it, nothing without bit6."""
    import pytest
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.speed_check() == {"tolerance_permille": 0, "measured_ticks": 0, "source_ticks": 0, "measured_rpm": 0.0, "source_rpm": 0.0}
    assert dev.speed_check(20)["tolerance_permille"] == 20 and fake.requests[-1].payload == bytes([41, 1, 20, 0])
    with pytest.raises(ValueError):
        dev.speed_check(501)
    deltas = [1100] * 2000
    r = dev.write_track(3, 1, deltas, extended=True, firmware_verify=True, index_ticks=55_400_000)   # +0.7 %
    assert r.verify_level == 3
    with pytest.raises(P2.DeviceError) as e:
        dev.write_track(3, 1, deltas, extended=True, firmware_verify=True, index_ticks=57_000_000)   # -3.5 %
    assert e.value.status == P2.SPEED_MISMATCH and "speed" in str(e.value)
    sc = dev.speed_check()
    assert (sc["measured_ticks"], sc["source_ticks"]) == (55_000_000, 57_000_000) and sc["measured_rpm"] == 300.0
    assert dev.write_track(3, 1, deltas, extended=True).verify_level == 1          # no bit6: no check
    assert dev.write_track(3, 1, deltas, extended=True, index_ticks=55_400_000).verify_level == 1   # 1.18-dev: bit6 without verify (0x60)
    with pytest.raises(P2.DeviceError) as e:                                        # ... checks too
        dev.write_track(3, 1, deltas, extended=True, index_ticks=57_000_000)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert e.value.status == P2.SPEED_MISMATCH and req[2] == 0x60 and req[11:15] == struct.pack("<I", 57_000_000)
    fake.firmware = (1, 17)                                                          # no WRITE_LIMITS bit 5: the host drops the period
    r = dev.write_track(3, 1, deltas, extended=True, index_ticks=57_000_000)
    assert r.verify_level == 1 and [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload[2] == 0x20
    fake.firmware = (1, 18)
    assert dev.speed_check(0)["tolerance_permille"] == 0
    assert dev.write_track(3, 1, deltas, extended=True, firmware_verify=True, index_ticks=57_000_000).verify_level == 3
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["speed-check", "25"]) == 0 and "+/-2.5 %" in capsys.readouterr().out
    assert cli.main(["speed-check"]) == 0 and "300.00 rpm vs source 289.47 rpm" in capsys.readouterr().out
    assert cli.main(["speed-check", "off"]) == 0 and "speed check off" in capsys.readouterr().out


def test_verify_report(monkeypatch, capsys):
    """Op 40: header only until a verified WRITE ran; the 0x21 model lists every reference sector, a WRITE
    without verify leaves the report alone, a short source leaves a header with read-back missing."""
    import pytest
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    vr = dev.verify_report()
    assert vr["format"] == 0 and vr["count"] == 0 and vr["sectors"] == [] and not vr["readback_missing"]
    assert fake.requests[-1].payload == bytes([40])
    dev.write_track(5, 1, [1100] * 2000, extended=True, firmware_verify=True)
    vr = dev.verify_report()
    assert (vr["format_name"], vr["count"], vr["content_verified"], vr["track"], vr["side"]) == ("IBM MFM", 9, True, 5, 1)
    assert all(s["state_name"] == "ok" and s["c"] == 5 and s["h"] == 1 for s in vr["sectors"]) and vr["sectors"][8]["r"] == 9
    dev.write_track(6, 0, [1100] * 2000)                                   # no verify: report untouched
    assert dev.verify_report()["track"] == 5
    with pytest.raises(P2.DeviceError):
        dev.write_track(7, 0, [1100] * 10, extended=True, firmware_verify=True)
    vr = dev.verify_report()
    assert vr["track"] == 7 and vr["format"] == 0 and vr["readback_missing"] and vr["count"] == 0
    fake.verify_report = bytes([1, 2, 2, 0, 3, 1]) + bytes([3, 1, 1, 2, 3]) + bytes([3, 1, 2, 2, 0])
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["verify-report"]) == 0
    out = capsys.readouterr().out
    assert "track 3.1: Amiga, 2 reference sectors, not verified" in out
    assert "C3 H1 R1 N2: different content" in out and "C3 H1 R2 N2: missing" in out


def test_last_refusal(monkeypatch, capsys):
    """Op 39: the fake keeps cmd/seq/status/field/detail of its last 16/21 refusal, the host names the field."""
    import pytest
    fake = GuardedDevice(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.last_refusal() == {"command": 0, "seq": 0, "status": 0, "field": 0, "field_name": "unknown", "detail": 0}
    with pytest.raises(P2.DeviceError):
        dev.read_track(0, 0, 1, expected_config=6, expected_media=42)
    seq = fake.requests[-2].seq if fake.requests[-1].cmd == P2.ANALYSIS_JOB else fake.requests[-1].seq
    rf = dev.last_refusal()
    assert (rf["command"], rf["seq"], rf["status"], rf["field_name"], rf["detail"]) == (P2.GUARDED_READ, seq, P2.BAD_ARGS, "configuration generation", 1)
    fake.bump_media()
    with pytest.raises(P2.DeviceError):
        dev.write_track(3, 1, [1100] * 2000, expected_config=7, expected_media=42)
    rf = dev.last_refusal()
    assert (rf["command"], rf["status"], rf["field"], rf["field_name"]) == (P2.GUARDED_WRITE, P2.MEDIA_CHANGED, 12, "media generation")
    dev.send(P2.T_REQUEST, P2.WRITE, 77, P2.WRITE_REQ.pack(0, 0, 0, 3, 4) + b"\x20")   # flags2 bit5: unknown
    assert dev._wait(P2.WRITE, 77, (P2.T_RESPONSE,), 1.0).status == P2.BAD_ARGS
    rf = dev.last_refusal()
    assert (rf["command"], rf["seq"], rf["field_name"], rf["detail"]) == (P2.WRITE, 77, "flags2", 32)
    assert len(dev.read_track(0, 0, 1).revolutions) == 1 and dev.last_refusal()["seq"] == 77   # an accepted request keeps the note
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["refusal"]) == 0 and "flags2, detail 32" in capsys.readouterr().out
    monkeypatch.setattr(P2, "open_serial", lambda port=None: FakeV2())
    assert cli.main(["refusal"]) == 0 and "no refusal" in capsys.readouterr().out


def test_flag_masks(monkeypatch, capsys):
    """Op 38: one 16-byte answer with every request mask the parsers accept."""
    fake = FakeV2()
    dev = P2.Device2(fake)
    assert dev.flag_masks() == {"read_flags": 0x1F, "write_flags": 0xFF, "write_flags2": 0x1F, "analyze_flags": 0xFF,
                                "format_hint_max": 7, "apple_gcr_flags": 0x3F, "tolerance_flags": 0x1F,
                                "quality_features": 0x07, "diagnostic_flags": 0x18, "position_byte": 0xB1}
    assert fake.requests[-1].payload == bytes([38])
    assert dev.flag_masks()["apple_gcr_flags"] == max(P2.APPLE_GCR_FLAGS.values()) * 2 - 1    # every host name fits the mask
    assert dev.flag_masks()["tolerance_flags"] == sum(P2.TOLERANCE_FLAGS.values())
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["masks"]) == 0
    out = capsys.readouterr().out
    assert "write_flags2       0x001f" in out and "format_hint_max    7" in out


def test_end_generation_block():
    """4.5: READ/WRITE END carry schema 1, config, media start/end and capture id behind the old payload;
    old one-byte / five-byte END frames still parse (generations None)."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    cap = dev.read_track(0, 0, 2)
    assert len(cap.revolutions) == 2 and cap.generations == {
        "config_generation": 7, "media_generation_start": 42, "media_generation_end": 42, "capture_id": 0x1234}
    fake.media_gen = 43                                      # a later capture binds to the new medium
    assert dev.read_track(0, 0, 1).generations["media_generation_start"] == 43
    r = dev.write_track(3, 1, [1100] * 2000)
    assert r.verify_level == 1 and r.generations == {
        "config_generation": 7, "media_generation_start": 43, "media_generation_end": 43, "capture_id": 0}
    assert P2.WriteResult.unpack(struct.pack("<BI", 1, 0)).generations is None          # firmware 1.15 END
    assert P2.generation_block(bytes([3]) + P2.GEN_BLOCK.pack(2, 1, 1, 1, 0), 1) is None  # unknown schema
    assert P2.generation_block(b"\x03", 1) is None
    raw = bytes([2]) + P2.GEN_BLOCK.pack(1, 7, 42, 42, 9)
    assert P2.generation_block(raw, 1)["capture_id"] == 9


def test_erase_remainder(monkeypatch, capsys):
    """flags2 bit0 (1.18): announced by WRITE_LIMITS bit 3, sent as one trailing byte, END reports erased_ticks."""
    import pytest
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    deltas = [1100] * 2000
    r = dev.write_track(3, 1, deltas)
    assert fake.requests[-1].payload == P2.WRITE_REQ.pack(3, 1, 0, 2000, len(P2.encode_flux(deltas))) and r.erased_ticks == 0
    r = dev.write_track(3, 1, deltas, erase_rest=True, clamp=True)
    req = fake.requests[-1].payload
    assert req[-1] == 1 and len(req) == 12 and req[2] & P2.WRITE_CLAMP and r.erased_ticks == 0x1234567 and r.generations
    assert dev.write_limits().flags & 8
    old = FakeV2(revolutions=REVS, firmware=(1, 15), caps=0x7FFF)   # WRITE_LIMITS flags 7: no bit 3
    with pytest.raises(ValueError):
        P2.Device2(old).write_track(3, 1, deltas, erase_rest=True)
    assert not any(q.cmd == P2.WRITE for q in old.requests), "nothing sent to a firmware without the option"
    assert P2.WriteResult.unpack(struct.pack("<BI", 1, 0) + P2.GEN_BLOCK.pack(1, 7, 42, 42, 0)).erased_ticks == 0
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["write-limits"]) == 0 and "erase remainder yes" in capsys.readouterr().out


def test_firmware_verify_modelled():
    """0x21/0x61 against the fake: DESCRIBE answered, a short source ends with 16 before WGATE,
    a long one verifies (level 3); the generation block and erased_ticks follow in both cases."""
    import pytest
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.describe().flags & 3 == 3 and fake.requests[-1].cmd == P2.DESCRIBE
    long_source = [1100] * 2000
    r = dev.write_track(3, 1, long_source, extended=True, firmware_verify=True)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert req[2] == 0x21 and r.verify_level == 3 and r.generations and r.erased_ticks == 0
    r = dev.write_track(3, 1, long_source, extended=True, firmware_verify=True, index_ticks=55_000_000)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert req[2] == 0x61 and req[11:15] == struct.pack("<I", 55_000_000) and r.verify_level == 3
    with pytest.raises(P2.DeviceError) as e:
        dev.write_track(3, 1, [1100] * 999, extended=True, firmware_verify=True)
    assert e.value.status == P2.BAD_ARGS and e.value.command == P2.WRITE and fake.write_req is None
    r = dev.write_track(3, 1, [1100] * 999, extended=True)              # no verify: level 1 as before
    assert r.verify_level == 1
    fake.VERIFY_MIN_INTERVALS = 10                                        # the bridge may lower the threshold
    assert dev.write_track(3, 1, [1100] * 999, extended=True, firmware_verify=True).verify_level == 3


def test_guarded_read_and_discovery():
    """4.5: op 25 schema 2 names 0x27/0x22 and 0x28/0x20; GUARDED_READ refuses a stale configuration (16)
    or another medium (21) before any DATA frame, accepts with the reply on 0x28 and DATA/END on 0x20."""
    import pytest
    fake = GuardedDevice(revolutions=REVS)
    dev = P2.Device2(fake)
    starts = dev.guarded_starts()
    assert starts == {"schema": 2, "write_start": 0x27, "write_data": 0x22, "prefix": 8, "check_mask": 3,
                      "config_generation": 7, "media_generation": 42, "read_start": 0x28, "read_data": 0x20}
    cap = dev.read_track(0, 0, 2, expected_config=starts["config_generation"], expected_media=starts["media_generation"])
    req = fake.requests[-1]
    assert req.cmd == P2.GUARDED_READ and req.payload[:8] == struct.pack("<II", 7, 42) and req.payload[8:] == P2.READ_REQ.pack(0, 0, 2, 0, 0)
    assert len(cap.revolutions) == 2 and cap.generations["media_generation_start"] == 42 and fake.guarded_starts == 1
    with pytest.raises(P2.DeviceError) as e:
        dev.read_track(0, 0, 2, expected_config=6, expected_media=42)
    assert e.value.status == P2.BAD_ARGS and e.value.command == P2.GUARDED_READ
    fake.bump_media()                                        # the bridge's media change between requests
    with pytest.raises(P2.DeviceError) as e:
        dev.read_track(0, 0, 2, expected_config=7, expected_media=42)
    assert e.value.status == P2.MEDIA_CHANGED and fake.guarded_starts == 1
    assert len(dev.read_track(0, 0, 1, expected_config=7, expected_media=43).revolutions) == 1
    with pytest.raises(ValueError):
        dev.read_track(0, 0, 1, expected_config=7)          # both generations or none
    r = dev.write_track(3, 1, [1100] * 2000, expected_config=7, expected_media=43)
    assert r.verify_level == 1 and r.generations["media_generation_start"] == 43 and fake.guarded_starts == 3
    with pytest.raises(P2.DeviceError) as e:
        dev.write_track(3, 1, [1100] * 2000, expected_config=7, expected_media=42)
    assert e.value.status == P2.MEDIA_CHANGED and e.value.command == P2.GUARDED_WRITE
    with pytest.raises(P2.DeviceError):
        P2.Device2(FakeV2(revolutions=REVS)).guarded_starts()   # the plain fake has no op 25
    assert len(dev.read_track(0, 0, 1).revolutions) == 1        # an unguarded READ still works on the guarded fake


def test_apple_gcr_stats(monkeypatch, capsys):
    import pytest
    fake = FakeV2()
    dev = P2.Device2(fake)
    assert dev.apple_gcr_stats() == {"revs": [], "variants": [], "sectors": [dict(volume=0, sync_before_addr=0, header_to_data=0,
                                     sync_before_data=0, flags=0, seen=0, flag_names=[], position=0)] * 16}
    assert fake.requests[-1].payload == bytes([35])
    rev0 = struct.pack("<5H", 6128, 319, 17, 16, 1)
    sec = bytearray(96)
    sec[0:6] = bytes([254, 10, 11, 6, 0, 1])
    sec[54:60] = bytes([254, 14, 11, 6, 0x40, 3])
    fake.apple_stats = bytes([1, 1]) + rev0 + bytes(70) + bytes(sec)
    st = dev.apple_gcr_stats()
    assert st["revs"] == [dict(nibbles=6128, sync_nibbles=319, address_fields=17, data_fields=16, orphan_headers=1)]
    assert st["sectors"][0]["volume"] == 254 and st["sectors"][9]["flag_names"] == ["data checksum"] and st["sectors"][9]["seen"] == 3
    assert "variants" not in st and "position" not in st["sectors"][0]
    # schema 2: trailing-zero histogram, prologue variants, sector positions
    tail = struct.pack("<3H", 0, 319, 2) + bytes(42) + struct.pack("<BBH", 0xB5, 0, 16) + bytes(12) + struct.pack("<16H", *range(100, 100 + 16 * 3104, 3104))
    fake.apple_stats = bytes([2, 1]) + rev0 + bytes(70) + bytes(sec) + tail
    st = dev.apple_gcr_stats()
    assert st["revs"][0]["trail"] == [0, 319, 2] and st["variants"] == [{"third": 0xB5, "count": 16}]
    assert st["sectors"][1]["position"] == 3204
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["apple-stats"]) == 0 and "D5 AA xx: b5 x16" in capsys.readouterr().out
    # loadable 6-and-2 table (op 36)
    assert dev.apple_gcr_table() == P2.APPLE_STD_TABLE and fake.requests[-1].payload == bytes([36])
    rot = P2.APPLE_STD_TABLE[1:] + P2.APPLE_STD_TABLE[:1]
    assert dev.apple_gcr_table(rot.hex()) == rot and fake.requests[-1].payload == bytes([36, 1]) + rot
    assert dev.apple_gcr_table("standard") == P2.APPLE_STD_TABLE
    with pytest.raises(ValueError):
        dev.apple_gcr_table(bytes(64))
    assert dev.apple_gcr_options(flags="five_and_three")["flag_names"] == ["five_and_three"]
    assert cli.main(["apple-table"]) == 0 and "standard" in capsys.readouterr().out
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["apple-stats"]) == 0
    out = capsys.readouterr().out
    assert "orphan headers 1" in out and "  9 254         14        11          6  0011     28036  data checksum" in out


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


def test_capability_matrix_and_configuration_against_fake():
    """Ops 22/23 on FakeV2 (4.3): the matrix decodes like a v0.7 board (flags 3, mask 63, six rows with
    hardware_qualified 0), the configuration report carries the generation, PROFILE_CHANGED (4) only against
    a stale expectation, and SELECT / accepted op 10 / op 18 settings move the generation."""
    import pytest
    from ufi_host.capabilities import decode_capabilities, decode_configuration
    fake = FakeV2()
    dev = P2.Device2(fake)
    m = dev.capability_matrix()
    assert (m["generation"], m["flags"], m["supported_flags"], m["timer_hz"], m["drive"]) == (7, 3, 63, 275_000_000, 1)
    assert (m["write_guard_discovery"], m["media_guarded_analysis"], m["multitrack_create"]) == (25, 26, 27)
    assert m["schema"] == 2 and [r["format"] for r in m["rows"]] == [1, 2, 3, 4, 5, 6, 7]
    assert all(r["hardware_qualified"] == 0 and r["flags"] == 3 and r["classic_limits"] == (440, 65536) for r in m["rows"])
    assert all(r["implemented"] == 15 and r["backends"] == 3 and r["extended_limits"] == (220, 0xFFFFFFFF) for r in m["rows"][:6])
    assert m["rows"][0]["rate_mask"] == 0x0F and m["rows"][5]["rate_mask"] == 0x3E04
    apple, raw, iec = m["rows"][6], m["raw_flux"], m["iec"]
    assert (apple["implemented"], apple["eligible"], apple["backends"], apple["rates"]) == (7, 2, 1, [250000])
    assert (raw["format"], raw["implemented"], raw["eligible"], raw["rate_mask"], raw["extended_limits"]) == (0, 5, 5, 0, (220, 0xFFFFFFFF))
    assert (iec["format"], iec["implemented"], iec["eligible"], iec["prerequisites"], iec["classic_limits"]) == (8, 13, 0, 5, (0, 0))
    assert len(fake.requests[-1].payload) == 1 and decode_capabilities(dev.request(0x0F, bytes([22]))) == m
    c = dev.configuration()
    assert c == {"generation": 7, "expected": 0, "flags": 0, "media_generation": 42, "supported_flags": 4}
    assert dev.configuration(7)["flags"] == 0 and dev.configuration(6) == {
        "generation": 7, "expected": 6, "flags": 4, "media_generation": 42, "supported_flags": 4}
    assert decode_configuration(dev.request(0x0F, bytes([23, 7, 0, 0, 0])))["flags"] == 0
    dev.select(2)                                            # selection bumps the generation
    assert dev.configuration(7)["flags"] == 4 and dev.configuration()["generation"] == 8 and dev.capability_matrix()["drive"] == 2
    dev.request(0x0F, bytes([10, 1]) + bytes(12))            # accepted extra times
    assert dev.configuration()["generation"] == 9
    dev.request(0x0F, bytes([10]))                           # a plain read leaves it alone
    assert dev.configuration()["generation"] == 9
    dev.request(0x0F, bytes([18, 1]))                        # preset accepted
    assert dev.configuration()["generation"] == 10 and dev.configuration(9)["flags"] == 4
    assert dev.capability_matrix()["generation"] == 10
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([22, 0]))
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([23, 1, 2]))


def test_analysis_job_client_and_recognition(monkeypatch, capsys):
    """Ops 1/2/3/4/21 against the fake: the start returns the 64-byte status, pages are checked (header,
    CRC32), the recognition block (section 15) decodes with flag names, missing expected sectors end with
    status 6 and the cause, a foreign job id or generation is refused, and the CLI prints the summary."""
    fake = FakeV2()
    dev = P2.Device2(fake)
    d = dev.recognition_discovery()
    assert (d["section"], d["history_section"], d["history_entry_size"], d["report_size"], d["flag_mask"],
            d["cause_mask"], d["max_attempts"], d["profile_check_flag"]) == (15, 16, 20, 64, 1023, 32767, 8, 128)
    st = dev.analysis_start(0x1001, 5, 1, 3, expected=0x3FE, c=5, h=1, n=2)
    req = fake.requests[-1].payload
    assert req[0] == 1 and struct.unpack_from("<IHBIQBBBB", req, 1) == (0x1001, 4, 1, 10000, 0x3FE, 5, 1, 2, 0)
    assert req[24:28] == bytes([5, 1, 3, 1]) and len(req) == 28
    assert st.done and st.state_name == "done" and st.status == 0 and (st.expected, st.seen, st.missing) == (0x3FE, 0x3FE, 0)
    assert st.job_id == 0x1001 and st.generation == 1 and st.capture_id == 0x1234 and st.flags == 4
    assert dev.analysis_status(0x1001).job_id == 0x1001 and dev.analysis_wait(0x1001).generation == 1
    assert dev.analysis_section(0x1001, 1, 2) == fake._job_status()
    rec = dev.recognition(0x1001)
    assert rec["flags"] == 1 | 2 | 4 | 8 | 256 and rec["flag_names"] == [
        "PROFILE_PRESENT", "FORMAT_OBSERVED", "SECTOR_SET_CONFIRMED", "GEOMETRY_UNCONFIRMED", "POSITION_CHECKED"]
    assert rec["expected"] == rec["seen"] == 0x3FE and rec["expected_set_supplied"] and rec["generation"] == 1
    assert not rec["cause_names"] and rec["format"] == 1 and rec["valid_sector_records"] == 9
    # small pages: every header checked; a wrong CRC or an unknown generation is refused before decoding
    assert dev.analysis_section(0x1001, 1, 15, page=7) == fake._recognition_block()
    pages = [q for q in fake.requests if q.cmd == P2.ANALYSIS_JOB and q.payload[0] == 3][-10:]
    assert [struct.unpack_from("<I", q.payload, 10)[0] for q in pages] == [0, 7, 14, 21, 28, 35, 42, 49, 56, 63]
    fake.page_crc_error = 1
    with pytest.raises(ValueError):
        dev.analysis_section(0x1001, 1, 15)
    fake.page_crc_error = 0
    with pytest.raises(P2.DeviceError):
        dev.analysis_section(0x1001, 2, 15)
    with pytest.raises(P2.DeviceError):
        dev.analysis_status(0x1002)
    with pytest.raises(ValueError):
        dev.analysis_start(0, 5, 1)
    with pytest.raises(ValueError):
        dev.analysis_start(7, 5, 1, flags=P2.JOB_RETRY_MISSING)
    with pytest.raises(ValueError):
        dev.analysis_start(7, 5, 1, max_attempts=9)
    # missing expected sectors: status 6, cause missing_sectors, no SECTOR_SET_CONFIRMED
    st = dev.analysis_start(0x1003, 5, 1, expected=0xFFE, c=5, h=1, n=2)
    assert st.state == 4 and st.status == 6 and st.missing == 0xC00 and st.done
    rec = dev.recognition(0x1003, st)
    assert rec["cause_names"] == ["missing_sectors"] and not rec["flags"] & 4 and rec["status"] == 6
    assert dev.analysis_abort(0x1003).state == 4
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["recognize", "5", "1", "--expect", "0x3fe"]) == 0
    out = capsys.readouterr().out
    assert "done, status 0 (ok)" in out and "SECTOR_SET_CONFIRMED" in out and "expected 0x3fe seen 0x3fe" in out
    assert "format IBM MFM" in out and "decision no retry" in out


def test_provenance_block(monkeypatch, capsys):
    """Op 43 against the fake: 96 bytes decode into the identity, build, generation and drive fields,
    the session lock and a SELECT show up, a payload after the op byte is refused, and the CLI prints
    both the text form and one JSON object."""
    import json
    fake = FakeV2()
    dev = P2.Device2(fake)
    pv = dev.provenance()
    assert fake.requests[-1].payload == bytes([43])
    assert (pv["board_rev"], pv["board_id_mv"], pv["mcu_device_id"], pv["mcu_revision"]) == (7, 2200, 0x483, 0x1003)
    assert pv["mcu_uid"] == "203236333135511700310028"
    assert (pv["firmware"], pv["git_rev"], pv["git_dirty"], pv["build_date"]) == ((1, 18), "abc1234", True, "Oct 17 2026")
    assert (pv["flux_hz"], pv["config_generation"], pv["media_generation"], pv["capture_id"]) == (275_000_000, 7, 42, 0x1234)
    assert pv["capture_present"] and pv["capture_media_generation"] == 42 and pv["index_ticks"] == 55_000_000
    assert (pv["drive"], pv["rpm_set"], pv["profile_stored"], pv["uptime_ms"], pv["speed_measured_ticks"]) == (1, 0, False, 123_456, 0)
    assert not pv["jumper_locked"] and not pv["session_locked"] and not pv["apple_sync"]
    assert (pv["i5_ma"], pv["i12_ma"], pv["power"], pv["board_flags"]) == (120, 0, 3, 8)
    dev.select(2)
    dev.request(0x0F, bytes([37, 1]))                        # engage the session write lock
    pv = dev.provenance()
    assert pv["drive"] == 2 and pv["config_generation"] == 8 and pv["session_locked"]
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([43, 1]))
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["provenance"]) == 0
    out = capsys.readouterr().out
    assert "board v0.7 (2200 mV)" in out and "rev abc1234+ built Oct 17 2026" in out and "capture id 4660" in out
    assert "drive 2" in out and "write lock session" in out and "last index period 55000000 ticks = 300.00 rpm" in out
    assert cli.main(["provenance", "--json"]) == 0
    assert json.loads(capsys.readouterr().out)["mcu_uid"] == "203236333135511700310028"


def test_read_provenance_flag(monkeypatch, capsys):
    """READ flag bit 3 (4.12): the request carries 0x08, the END grows to 114 bytes with the provenance
    block behind the generation block, Capture.provenance decodes it (same capture id as the generation
    block), a READ without the flag keeps the 18-byte END and None, an unknown bit 4 is refused with op 39
    field 2, GUARDED_READ passes the flag through, and ufi read --provenance prints the line."""
    fake = GuardedDevice(revolutions=REVS)
    dev = P2.Device2(fake)
    cap = dev.read_track(0, 0, 2, provenance=True)
    req = [q for q in fake.requests if q.cmd == P2.READ][-1]
    assert req.payload[3] & 0x08 and len(cap.revolutions) == 2
    pv = cap.provenance                                      # decoded only from exactly 96 bytes behind the 18-byte END head
    assert pv["capture_id"] == cap.generations["capture_id"] == 0x1234 and pv["config_generation"] == 7
    assert pv["mcu_uid"] == "203236333135511700310028" and pv["git_rev"] == "abc1234" and pv["board_rev"] == 7
    plain = dev.read_track(0, 0, 2)
    assert plain.provenance is None and plain.generations["capture_id"] == 0x1234
    g = dev.read_track(0, 0, 2, expected_config=7, expected_media=42, provenance=True)
    assert g.provenance["capture_id"] == 0x1234 and fake.requests[-1].cmd == P2.GUARDED_READ
    with pytest.raises(P2.DeviceError) as e:
        dev.request(P2.READ, P2.READ_REQ.pack(0, 0, 2, 0x20, 0))
    assert e.value.status == P2.BAD_ARGS and dev.last_refusal()["field"] == 2 and dev.last_refusal()["detail"] == 0x20
    assert dev.flag_masks()["read_flags"] == 0x1F
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["read", "0", "0", "-r", "2", "--provenance"]) == 0
    out = capsys.readouterr().out
    assert "provenance: board v0.7, MCU 203236333135511700310028, firmware 1.18 rev abc1234+" in out and "capture id 4660" in out


def test_analysis_job_reseek_flag(monkeypatch, capsys):
    """RESEEK 0x40 (6.1): accepted by the host and the fake, sent in the op 1 flags word, counted once per retry
    by the fake; bit 7 is still refused locally; ufi recognize --reseek sets it."""
    fake = FakeV2()
    dev = P2.Device2(fake)
    st = dev.analysis_start(0x2001, 5, 1, flags=P2.JOB_RETRY_MISSING | P2.JOB_RESEEK, max_attempts=3,
                            expected=0xFFE, c=5, h=1, n=2)
    assert struct.unpack_from("<H", fake.requests[-1].payload, 5)[0] == 1 | 4 | 64 and st.flags == 1 | 4 | 64
    assert st.status == 6 and fake.reseeks == 2                     # two retries after the first attempt
    dev.analysis_start(0x2002, 5, 1, flags=P2.JOB_RETRY_MISSING, max_attempts=3, expected=0xFFE, c=5, h=1, n=2)
    assert fake.reseeks == 2                                        # without the flag nothing moves
    with pytest.raises(ValueError):
        dev.analysis_start(0x2003, 5, 1, flags=0x80)
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["recognize", "5", "1", "--expect", "0xffe", "--retry", "--attempts", "2", "--reseek"]) == 0
    assert struct.unpack_from("<H", [q for q in fake.requests if q.cmd == P2.ANALYSIS_JOB and q.payload[0] == 1][-1].payload, 5)[0] \
        == 1 | 2 | 4 | 64
    assert "missing_sectors" in capsys.readouterr().out and fake.reseeks == 3


def test_track_survey(monkeypatch, capsys):
    """Op 46 against the fake: the empty header before a start, argument checks local and on the board,
    the start reply, polling until done with the entries decoded (encoding, peaks, rpm, sectors only with
    the flag, the NO_INDEX step), side masks, abort, and ufi survey printing the table."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    st = dev.track_survey_status()
    assert (st["state_name"], st["cursor"], st["total"], st["entry_size"], st["entries"]) == ("none", 0, 0, 20, [])
    for bad in ((3, 2, 3), (0, 84, 3), (0, 1, 0), (0, 1, 4)):
        with pytest.raises(ValueError):
            dev.track_survey(*bad)
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([46, 1, 0, 1, 3, 2]))
    st = dev.track_survey(0, 3, 3, sectors=True)
    assert fake.requests[-1].payload == bytes([46, 1, 0, 3, 3, 1])
    assert (st["state_name"], st["cursor"], st["total"], st["config_generation"], st["media_token"]) == ("running", 0, 8, 7, 42)
    with pytest.raises(P2.DeviceError):
        dev.track_survey(0, 1)                       # BUSY while running
    seen = []
    st = dev.track_survey_wait(poll=0, progress=lambda s: seen.append(s["cursor"]))
    assert seen == [4, 8] and st["state_name"] == "done" and not st["error"] and st["elapsed_ms"] == 12000
    assert [(e["track"], e["side"]) for e in st["entries"]] == [(t, s) for t in range(4) for s in (0, 1)]
    e = st["entries"][0]
    assert (e["encoding_name"], e["peak_ns"], e["peak2_ns"], e["peak3_ns"], e["sectors"], e["peaks"]) == ("MFM", 4050, 6050, 8050, 9, 3)
    assert e["index_ticks"] == 55_000_000 and abs(e["rpm"] - 300.0) < 0.01 and e["transitions"] == 50_000
    bad = st["entries"][5]
    assert (bad["track"], bad["side"], bad["status"], bad["encoding"], bad["index_ticks"]) == (2, 1, 4, 0, 0) and "index" in bad["status_text"]
    st = dev.track_survey(5, 6, 2)                   # side 1 only, no sector pass
    st = dev.track_survey_wait(poll=0)
    assert [(e["track"], e["side"], e["sectors"]) for e in st["entries"]] == [(5, 1, 0), (6, 1, 0)]
    dev.track_survey(0, 9, 1)
    st = dev.track_survey_abort()
    assert (st["state_name"], st["error"], st["cursor"]) == ("failed", 20, 0) and fake.requests[-1].payload == bytes([46, 0])
    assert dev.track_survey_abort()["error"] == 20     # idempotent
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    monkeypatch.setattr(P2.time, "sleep", lambda s: None)
    assert cli.main(["survey", "0", "3", "--sectors"]) == 0
    out = capsys.readouterr().out
    assert "survey done: tracks 0-3, sides mask 3, 8/8 entries" in out
    assert "t00 s0  MFM     peaks 4.05/6.05/8.05 us   300.0 rpm   50000 transitions  9 sectors" in out
    assert "t02 s1  error 4 (no index pulse / aborted)" in out
    assert cli.main(["survey", "--status"]) == 0 and "8/8 entries" in capsys.readouterr().out
    assert cli.main(["survey", "--abort"]) == 0 and "survey done" in capsys.readouterr().out     # nothing running: table unchanged


def test_weak_positions(monkeypatch, capsys):
    """Op 44 against the fake: default threshold 2 lists the two windows at or above it, threshold 5 one,
    threshold 7 none, the request carries the threshold only when it differs from 2, 0 and 256 are refused
    locally, no data before an ANALYZE with two revolutions, and ufi weak --positions prints the list."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    assert dev.weak_positions()["entries"] == [] and fake.requests[-1].payload == bytes([44])
    dev.analyze(7, 1, 3)
    wp = dev.weak_positions()
    assert (wp["revs"], wp["track"], wp["side_byte"], wp["windows"], wp["threshold"], wp["truncated"]) == (3, 7, 1, 1024, 2, False)
    assert wp["entries"] == [dict(window=72, diff=3, min=10), dict(window=77, diff=2, min=11), dict(window=290, diff=5, min=9)]
    assert [e["window"] for e in dev.weak_positions(3)["entries"]] == [72, 290] and fake.requests[-1].payload == bytes([44, 3])
    assert dev.weak_positions(5)["entries"] == [dict(window=290, diff=5, min=9)] and dev.weak_positions(7)["entries"] == []
    for bad in (0, 256):
        with pytest.raises(ValueError):
            dev.weak_positions(bad)
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([44, 0]))
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["weak", "--positions", "--threshold", "3"]) == 0
    out = capsys.readouterr().out
    assert "2 fine windows (1/1024 revolution) with max-min >= 3: 72:10+3, 290:9+5" in out


def test_precomp_per_write_and_profile(monkeypatch, capsys):
    """flags2 bit 2 and op 45 against the fake: write_track(precomp_ns=) sends the u16 behind the flags2 byte
    (0 = off), the fake records it and reports source 2, a zone profile is set/read/cleared with source 1/0,
    local checks refuse 2001, the extended path and bad zones, WRITE_LIMITS announces bit 6, and the CLI prints
    the profile."""
    fake = FakeV2()
    dev = P2.Device2(fake)
    assert dev.write_limits().flags & 64
    deltas = [1100] * 2000
    r = dev.write_track(70, 0, deltas, precomp_ns=0)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert r.clamped == 0 and req[11] == 0x04 and req[12:14] == struct.pack("<H", 0) and fake.last_precomp == 0
    pp = dev.precomp_profile()
    assert (pp["source"], pp["zones"], pp["last_ns"], pp["last_track"], pp["last_physical"]) == (2, [], 0, 70, 70)
    dev.write_track(70, 0, deltas, precomp_ns=125, erase_rest=True)
    req = [q for q in fake.requests if q.cmd == P2.WRITE][-1].payload
    assert req[11] == 0x05 and req[12:14] == struct.pack("<H", 125) and dev.precomp_profile()["last_ns"] == 125
    for bad in dict(precomp_ns=2001), dict(precomp_ns=-1), dict(precomp_ns=125, extended=True):
        with pytest.raises(ValueError):
            dev.write_track(70, 0, deltas, **bad)
    dev.write_track(70, 0, deltas)                                           # no override: fake applies its rule
    assert fake.last_precomp is None and dev.precomp_profile()["source"] == 0 and dev.precomp_profile()["last_ns"] == 125
    pp = dev.precomp_profile([(0, 0), (40, 62), (80, 125)])
    assert fake.requests[-1].payload == bytes([45, 1, 3, 0, 0, 0, 40, 62, 0, 80, 125, 0])
    assert pp["zones"] == [(0, 0), (40, 62), (80, 125)] and pp["source"] == 1 and pp["last_ns"] == 62
    for bad in ([], [(1, 10)], [(0, 10), (0, 20)], [(0, 2001)], [(0, 1)] * 5):
        with pytest.raises(ValueError):
            dev.precomp_profile(bad)
    with pytest.raises(P2.DeviceError):
        dev.request(0x0F, bytes([45, 1, 1, 5, 0, 0]))                       # first zone must start at 0
    assert dev.precomp_profile(clear=True)["zones"] == [] and fake.requests[-1].payload == bytes([45, 0])
    assert dev.flag_masks()["write_flags2"] == 0x1F
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    assert cli.main(["precomp", "--zones", "0:125,40:62"]) == 0
    out = capsys.readouterr().out
    assert "from physical track 0: 125 ns, from physical track 40: 62 ns" in out and "from zone profile" in out
    assert cli.main(["precomp", "--clear"]) == 0 and "none (TIMING/auto" in capsys.readouterr().out


def test_read_progress_events():
    """READ flag bit 4 (2.3): with the event mask enabled the fake sends event 0x85 per revolution, read_track
    hands revolution, index period and elapsed time to the callback and drains the events; without the
    callback the flag stays clear; bit 5 is refused; the mask reports 0x1F."""
    fake = FakeV2(revolutions=REVS)
    dev = P2.Device2(fake)
    dev.enable_events()
    assert fake.events_mask & (1 << 5)
    seen = []
    cap = dev.read_track(0, 0, 2, progress=lambda rev, ticks, ms: seen.append((rev, ticks, ms)))
    req = [q for q in fake.requests if q.cmd == P2.READ][-1].payload
    assert req[3] & 0x10 and len(cap.revolutions) == 2
    assert seen == [(1, REVS[0][0], 200), (2, REVS[1][0], 400)] and not [e for e in dev.events if e.code == 0x85]
    dev.read_track(0, 0, 2)
    assert not [q for q in fake.requests if q.cmd == P2.READ][-1].payload[3] & 0x10
    with pytest.raises(P2.DeviceError):
        dev.request(P2.READ, P2.READ_REQ.pack(0, 0, 2, 0x20, 0))
    assert dev.last_refusal()["detail"] == 0x20 and P2.EVENT_NAMES[P2.EV_READ_PROGRESS] == "read progress"
