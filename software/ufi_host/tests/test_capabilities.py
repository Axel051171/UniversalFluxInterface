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
    p[0] = 2                                    # schema 2 with the same six rows still decodes (count from the head)
    assert decode_capabilities(p)['schema'] == 2 and decode_capabilities(p)['rows'][0]['rates'] == row['rates']
    p[0] = 3
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


def test_capability_matrix_schema_2_rows():
    """Schema 2 (1.18-dev): row count from the head, rows found by their id byte, raw flux and IEC
    rows exposed separately, Apple II GCR as format row 7; schema 1 still decodes; sizes that do
    not match the head are refused."""
    p = bytearray(320)
    p[:4] = bytes((2, 9, 32, 32))
    struct.pack_into('<4I', p, 4, 11, 3, 63, 275000000)
    for rid in range(9):
        p[32 + 32 * rid] = rid
    p[32:40] = bytes((0, 5, 5, 5, 0, 3, 3, 7))
    struct.pack_into('<6I', p, 40, 0, 440, 65536, 220, 0xffffffff, 3)
    p[64:72] = bytes((1, 15, 15, 10, 0, 3, 3, 7))
    struct.pack_into('<6I', p, 72, 15, 440, 65536, 220, 0xffffffff, 3)
    p[256:264] = bytes((7, 7, 7, 2, 0, 1, 1, 7))
    struct.pack_into('<6I', p, 264, 1, 440, 65536, 0, 0, 3)
    p[288:296] = bytes((8, 13, 13, 13, 0, 0, 0, 5))
    report = decode_capabilities(p)
    assert report['schema'] == 2 and report['generation'] == 11
    assert [r['format'] for r in report['rows']] == [1, 2, 3, 4, 5, 6, 7]
    assert report['raw_flux']['implemented'] == 5 and report['raw_flux']['rates'] == [] and report['raw_flux']['extended_limits'] == (220, 0xffffffff)
    assert report['iec']['eligible'] == 13 and report['iec']['prerequisites'] == 5 and report['iec']['classic_limits'] == (0, 0)
    assert report['rows'][6]['rates'] == [250000] and report['rows'][6]['backends'] == 1
    with pytest.raises(ValueError):
        decode_capabilities(p[:-32])
    p[1] = 8
    with pytest.raises(ValueError):
        decode_capabilities(p)
    p[0], p[1] = 1, 9
    with pytest.raises(ValueError):
        decode_capabilities(p)
