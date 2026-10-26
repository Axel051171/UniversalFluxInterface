"""Host tool tests against a fake device that speaks the firmware's byte protocol."""
from __future__ import annotations

import struct

import pytest

from ufi_host import protocol as P
from ufi_host.scp import ScpRevolution, read_scp, scp_to_ticks, ticks_to_scp, write_scp


def encode_delta(d: int) -> bytes:
    """One flux interval of the READ_TRACK stream code (spec: firmware/src/ufi_stream.c)."""
    if 1 <= d <= 0xEF:
        return bytes([d])
    if 240 <= d <= 3567:
        return bytes([0xF0 + ((d - 240) >> 8), (d - 240) & 0xFF])
    return b"\xFE" + struct.pack("<I", d)


def encode_stream(revs, last: int = 0) -> bytes:
    """Independent encoder for the READ_TRACK stream (spec: firmware/src/ufi_stream.c).
    revs: [(index_ticks, samples relative to that revolution's index), ...]
    last: time of the previous transition (a lead-in's last one, 4.25), 0 = index 0"""
    out, t, start = bytearray(), last, 0
    for ticks, samples in revs:
        for s in samples:
            out += encode_delta(start + s - t)
            t = start + s
        start += ticks
        out += b"\xFD" + struct.pack("<I", start - t)
    return bytes(out)


class FakeDevice:
    """Byte-level double of firmware/src/ufi_usb.c (+ capture/write flow of ufi_main.c)."""

    def __init__(self, revolutions=None, read_done_status=0, fail=None):
        self.out = bytearray()
        self.revs = revolutions or []
        self.read_done_status = read_done_status
        self.fail = fail or {}          # command -> status
        self.write_expect = 0
        self.write_data = bytearray()
        self.write_cmd = 0
        self.commands = []

    def _reply(self, cmd, status=0, payload=b""):
        self.out += P.HDR.pack(cmd, status, len(payload)) + payload

    def write(self, data: bytes):
        if self.write_expect:            # OUT data of a write in progress
            self.write_data += data
            if len(self.write_data) >= self.write_expect:
                self.write_expect = 0
                self._reply(self.write_cmd)
            return len(data)
        cmd, args = data[0], data[1:]
        self.commands.append((cmd, bytes(args)))
        if cmd in self.fail:
            self._reply(cmd, self.fail[cmd])
        elif cmd == P.GET_INFO:
            self._reply(cmd, 0, b"UFI Flux Engine v1.1\0UFI Headless\0STM32H723\0")
        elif cmd == P.GET_STATUS:
            self._reply(cmd, 0, P.STATUS.pack(1, 1, 0, 1, 0, 1, 0, 0, 300))
        elif cmd == P.READ_TRACK:
            self._reply(cmd)
            data = encode_stream(self.revs[:args[2]])
            for k in range(0, len(data), 1000):    # message borders carry no meaning
                self._reply(P.EVT_FLUX_STREAM, 0, data[k:k + 1000])
            self._reply(P.EVT_READ_DONE, self.read_done_status, bytes([min(len(self.revs), args[2])]))
        elif cmd == P.READ_TRACK_RAW:
            self._reply(cmd)
            for i, (ticks, samples) in enumerate(self.revs[:args[2]]):
                self._reply(P.EVT_FLUX, 0, P.FLUX_HDR.pack(args[0], args[1], i, 1, ticks, len(samples)))
                self.out += struct.pack(f"<{len(samples)}I", *samples)
            self._reply(P.EVT_READ_DONE, self.read_done_status, bytes([min(len(self.revs), args[2])]))
        elif cmd in (P.WRITE_TRACK, P.WRITE_TRACK_VERIFY):
            self.write_cmd = cmd
            self.write_expect = 4 * struct.unpack("<I", args[2:6])[0]
            self.write_data = bytearray()
            self._reply(cmd)
        elif cmd == P.IEC_RECEIVE:
            self._reply(cmd, 0, bytes([0x42, 1]))
        elif cmd == P.CHECK_DISK:
            self._reply(cmd, 0, bytes([1, 1]))
        elif cmd == P.USB_POWER:
            self._reply(cmd, 0, struct.pack("<3H", 0, 1350, 3000))
        elif cmd == P.AMIGA_ID:
            self._reply(cmd, 0, struct.pack("<I", 0xAAAAAAAA))
        elif cmd == P.DRIVE_TIMING:
            if len(args) >= P.TIMING.size:
                self.timing = bytes(args[:P.TIMING.size])
            self._reply(cmd, 0, getattr(self, "timing", P.TIMING.pack(3, 3000, 15000, 0, 200, 500, 10000, 30, 0, 0xFFFF, 0)))
        else:
            self._reply(cmd)
        return len(data)

    def read(self, n: int) -> bytes:
        chunk, self.out = bytes(self.out[:n]), self.out[n:]
        return chunk


def test_command_framing_has_no_length_prefix():
    fake = FakeDevice()
    P.Device(fake).select("amiga")
    assert fake.commands == [(P.SELECT_DRIVE, bytes([4]))]


def test_info_and_status():
    dev = P.Device(FakeDevice())
    assert dev.info() == ["UFI Flux Engine v1.1", "UFI Headless", "STM32H723"]
    st = dev.status()
    assert st.drive == 1 and st.motor_on and st.track0 and st.ready and st.rpm == 300


def test_read_track_streams_revolutions_until_read_done():
    revs = [(55_000_000, [1100, 2200, 3300]), (55_000_100, [1000, 2000])]
    cap = P.Device(FakeDevice(revs)).read_track(5, 1, 2)
    assert [r.samples for r in cap.revolutions] == [[1100, 2200, 3300], [1000, 2000]]
    assert cap.revolutions[0].track == 5 and cap.revolutions[0].side == 1
    assert cap.revolutions[0].rpm == pytest.approx(300.0)
    assert cap.status == 0


def test_read_track_raw_fallback():
    revs = [(55_000_000, [1100, 2200, 3300]), (55_000_100, [1000, 2000])]
    fake = FakeDevice(revs)
    cap = P.Device(fake).read_track_raw(5, 1, 2)
    assert fake.commands[0][0] == P.READ_TRACK_RAW
    assert [r.samples for r in cap.revolutions] == [[1100, 2200, 3300], [1000, 2000]]


def test_stream_covers_every_code_and_halves_the_data():
    # 1-byte, 2-byte edges, long (> 3567) and a transition exactly at the index
    revs = [(1_000_000, [5, 0xEF + 5, 0xEF + 245, 4000, 4000 + 3567, 900_000]),
            (55_000_000, [0, 550, 1100, 1650])]
    data = encode_stream(revs)
    got = P.decode_stream(data, 2, 1)
    assert [(r.index_ticks, r.samples) for r in got] == revs
    hd = [(55_000_000, list(range(700, 55_000_000 - 700, 770)))]   # ~71k transitions, 2.8 us
    assert len(encode_stream(hd)) < 0.51 * 4 * len(hd[0][1])


def test_stream_rejects_reserved_bytes():
    with pytest.raises(ValueError):
        P.decode_stream(b"\x00")


def encode_lead_stream(ticks: int, lead: list[int], revs, end_offset: int | None = None) -> bytes:
    """Independent encoder of the pre-index lead-in (USB_Protokoll 4.25): 0xFF 0x01 u32 ticks,
    the lead-in deltas from -ticks, 0xFF 0x02 u32 (index 0 after the last transition), revolutions."""
    out, t = bytearray(b"\xFF\x01" + struct.pack("<I", ticks)), -ticks
    for s in lead:
        out += encode_delta(s - t)
        t = s
    out += b"\xFF\x02" + struct.pack("<I", -t if end_offset is None else end_offset)
    return bytes(out + encode_stream(revs, last=t))


def test_stream_lead_in_is_separate_from_the_revolutions():
    """4.25: the lead-in comes back on its own, the revolutions stay exactly as without it, also
    with a transition exactly on index 0 (revolution 0) and an empty lead-in."""
    revs = [(1_000_000, [0, 5, 4000, 900_000]), (900_000, [1, 2, 3])]
    for lead in ([-5_499_999, -100_000, -3567, -240, -1], [-12], []):
        data = encode_lead_stream(5_500_000, lead, revs)
        got, plain = P.decode_stream_lead(data, 2, 1)
        assert got == P.LeadIn(5_500_000, lead)
        assert [(r.index_ticks, r.samples) for r in plain] == revs == [(r.index_ticks, r.samples) for r in P.decode_stream(data)]
    old = encode_stream(revs[1:])                                 # no transition on index 0 without a lead-in
    assert P.decode_stream_lead(old) == (None, P.decode_stream(old))
    assert P.decode_stream_lead(b"\xFF\x01\0\0\0\0\xFF\x02\0\0\0\0")[0] == P.LeadIn(0, [])


def test_stream_lead_in_rejects_malformed_marks():
    ok = encode_lead_stream(1000, [-500], [(2000, [10])])
    bad = [ok[:5],                                               # truncated header
           b"\xFF\x02" + ok[2:],                                 # index 0 mark as first element
           encode_lead_stream(1000, [-500], [(2000, [10])], end_offset=499),   # index 0 not at time 0
           encode_lead_stream(1000, [-500], [(2000, [10])])[:-6 - 6],          # stream ends inside the lead-in
           b"\xFF\x01" + struct.pack("<I", 100) + b"\xFE" + struct.pack("<I", 100) + b"\xFF\x02\0\0\0\0",  # transition on index 0
           b"\xFF\x01" + struct.pack("<I", 100) + b"\xFD" + struct.pack("<I", 100),                     # FD inside
           ok + b"\xFF\x02\0\0\0\0",                              # second index 0 mark
           ok[:6] + b"\xFF\x01" + ok[2:6] + ok[6:],              # second header
           b"\xFF\x03" + ok[2:]]
    for data in bad:
        with pytest.raises(ValueError):
            P.decode_stream_lead(data)
        with pytest.raises(ValueError):
            P.decode_stream(data)


def test_stream_decoder_random_input_only_raises_value_error():
    """Bounded random-input pass over the stream decoder (lead-in marks included): every input is
    either decoded consistently or refused with ValueError, never another exception."""
    import random
    rng = random.Random(4250)
    alphabet = [0x00, 0x01, 0x7F, 0xEF, 0xF0, 0xFC, 0xFD, 0xFE, 0xFF, 0x02, 0x03]
    seeds = [encode_lead_stream(1000, [-999, -1], [(5000, [0, 4999])]), encode_stream([(5000, [1, 2])])]
    for n in range(3000):
        if n % 3:
            data = bytearray(rng.choice(seeds))
            for _ in range(rng.randint(1, 4)):
                if data:
                    data[rng.randrange(len(data))] = rng.choice(alphabet) if rng.random() < 0.7 else rng.randrange(256)
        else:
            data = bytearray(rng.choice(alphabet) for _ in range(rng.randint(0, 24)))
        try:
            lead, revs = P.decode_stream_lead(bytes(data))
        except ValueError:
            continue
        if lead is not None:
            assert all(-lead.ticks < s < 0 for s in lead.samples) and lead.samples == sorted(set(lead.samples))
        assert all(0 <= s < r.index_ticks for r in revs for s in r.samples)


def test_read_overflow_keeps_complete_revolutions():
    cap = P.Device(FakeDevice([(55_000_000, [10])], read_done_status=10)).read_track(0, 0, 3)
    assert len(cap.revolutions) == 1 and cap.status == 10


def test_read_error_raises_with_text():
    fake = FakeDevice([], read_done_status=6)
    with pytest.raises(P.DeviceError, match="DMA"):
        P.Device(fake).read_track(0, 0, 1)


def test_command_error_is_decoded():
    with pytest.raises(P.DeviceError, match="write protected"):
        P.Device(FakeDevice(fail={P.ERASE_TRACK: 12})).erase(1, 0)


def test_write_track_sends_header_then_deltas_and_waits_for_event():
    fake = FakeDevice()
    deltas = list(range(1000, 1100))
    P.Device(fake).write_track(3, 0, deltas, verify=True)
    assert fake.commands[0] == (P.WRITE_TRACK_VERIFY, bytes([3, 0]) + struct.pack("<I", 100))
    assert list(struct.unpack("<100I", fake.write_data)) == deltas


def test_iec_receive_returns_byte_and_eoi():
    assert P.Device(FakeDevice()).iec_receive() == (0x42, True)


def test_check_disk_and_amiga_id():
    dev = P.Device(FakeDevice())
    assert dev.check_disk() == (True, True)
    assert dev.amiga_id() == (0xAAAAAAAA, '3.5" HD (HD media)')
    assert dev.usb_power() == (0, 1350, 3000)


def test_timing_get_and_set():
    fake = FakeDevice()
    dev = P.Device(fake)
    assert dev.timing()["step_rate_us"] == 3000
    t = dev.timing(step_rate_us=6000, spinup_ms=750)
    assert t["step_rate_us"] == 6000 and t["spinup_ms"] == 750 and t["settle_us"] == 15000
    assert t["motor_off_s"] == 30 and t["precomp_ns"] == 0xFFFF
    assert fake.commands[-1][0] == P.DRIVE_TIMING and len(fake.commands[-1][1]) == 22
    with pytest.raises(ValueError):
        dev.timing(warp=1)


def test_timeout_when_device_is_silent():
    class Silent:
        def write(self, d):
            return len(d)

        def read(self, n):
            return b""

    with pytest.raises(TimeoutError):
        P.Device(Silent(), timeout=0.05).info()


def test_tick_conversion_does_not_drift():
    # 4 us MFM cells for a whole 200 ms revolution: 1100 ticks = 160 SCP units each
    samples = [1100 * (i + 1) for i in range(50_000)]
    cells = ticks_to_scp(samples)
    assert set(cells) == {160} and sum(cells) == 8_000_000
    back = scp_to_ticks(cells)
    assert sum(back) == samples[-1] and max(back) - min(back) <= 1


def test_scp_roundtrip(tmp_path):
    tracks = {0: [ScpRevolution(8_000_000, [160, 240, 70_000])],
              3: [ScpRevolution(8_000_100, [320, 160])]}
    path = tmp_path / "t.scp"
    write_scp(str(path), tracks, 1)
    raw = path.read_bytes()
    assert raw[:3] == b"SCP" and struct.unpack_from("<I", raw, 12)[0] == sum(raw[16:]) & 0xFFFFFFFF
    nrev, back = read_scp(str(path))
    assert nrev == 1
    assert back[0][0].cells == [160, 240, 70_000] and back[3][0].index_time == 8_000_100
