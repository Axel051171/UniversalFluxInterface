import struct
import pytest
from ufi_host.capabilities import decode_capabilities, decode_configuration, guarded_analysis


def test_capability_qualification_and_rates():
    p = bytearray(224)
    p[:4] = bytes((1, 6, 32, 32))
    struct.pack_into('<4I', p, 4, 9, 3, 63, 275000000)
    p[32:40] = bytes((1, 15, 15, 10, 0, 3, 3, 7))
    struct.pack_into('<6I', p, 40, 15, 440, 65536, 220, 0xffffffff, 3)
    report = decode_capabilities(p)
    assert report['generation'] == 9
    row = report['rows'][0]
    assert 1000000 in row['rates'] and row['hardware_qualified'] == 0
    assert row['classic_limits'] == (440, 65536)
    assert row['extended_limits'] == (220, 0xffffffff)
    with pytest.raises(ValueError):
        decode_capabilities(p[:-1])
    p[0] = 2
    with pytest.raises(ValueError):
        decode_capabilities(p)


def test_generation_guard():
    p = bytes((1, 24, 1, 0)) + struct.pack('<5I', 8, 7, 4, 12, 4)
    assert decode_configuration(p)['flags'] == 4
    start = bytes((1,)) + bytes(32)
    assert guarded_analysis(8, start) == bytes((24, 8, 0, 0, 0)) + start
    assert guarded_analysis(8, start, 12) == bytes((26,)) + struct.pack('<II', 8, 12) + start
    with pytest.raises(ValueError):
        guarded_analysis(8, start, 0)
    for generation in (0, -1, 0x100000000):
        with pytest.raises(ValueError):
            guarded_analysis(generation, start)
    with pytest.raises(ValueError):
        guarded_analysis(8, bytes(33))
    with pytest.raises(ValueError):
        decode_configuration(p[:-1])
