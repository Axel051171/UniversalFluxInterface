"""Host tool tests against a fake device that speaks the firmware's byte protocol."""
from __future__ import annotations

import struct

import pytest

from ufi_host import protocol as P
from ufi_host.scp import ScpRevolution, read_scp, scp_to_ticks, ticks_to_scp, write_scp


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
