import struct
import pytest
from ufi_host import protocol2 as P2
from test_protocol2 import FakeV2, GuardedDevice   # GuardedDevice: op 25, 0x27/0x28 with config_gen 7 / media_gen 42


def test_guarded_write_uses_existing_stream():
    fake = GuardedDevice()
    result = P2.Device2(fake).write_track(0, 0, [1000]*3, expected_config=7, expected_media=42)
    assert result.verify_level == 1 and fake.write_frames
    request = [f for f in fake.requests if f.cmd == P2.GUARDED_WRITE][0]
    assert request.payload[:8] == struct.pack('<II', 7, 42)


@pytest.mark.parametrize('config,media', [(6, 42), (7, 41)])
def test_stale_write_sends_no_data(config, media):
    fake = GuardedDevice()
    with pytest.raises(P2.DeviceError):
        P2.Device2(fake).write_track(0, 0, [1000]*3, expected_config=config, expected_media=media)
    assert not fake.write_frames and fake.write_req is None


@pytest.mark.parametrize('config,media', [(None, 42), (7, None), (0, 42), (7, 0), (1<<32, 42)])
def test_invalid_tokens_rejected_before_io(config, media):
    fake = GuardedDevice()
    with pytest.raises(ValueError):
        P2.Device2(fake).write_track(0, 0, [1000]*3, expected_config=config, expected_media=media)
    assert not fake.requests


def test_local_parameter_error_does_not_open_integrity_job():
    dev = P2.Device2(FakeV2())
    opened = []
    dev._job_open = lambda kind: opened.append(kind)
    with pytest.raises(struct.error):
        dev.write_track(256, 0, [1000]*3, integrity=True)
    assert not opened


def test_rejected_guard_closes_own_integrity_job():
    dev = P2.Device2(GuardedDevice())
    closed = []
    dev._job_open = lambda kind: object()
    dev.abort = lambda: closed.append(True)
    with pytest.raises(P2.DeviceError):
        dev.write_track(0, 0, [1000]*3, integrity=True, expected_config=6, expected_media=42)
    assert closed == [True]
