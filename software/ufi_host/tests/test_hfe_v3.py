import argparse
import struct
from types import SimpleNamespace
import pytest
from ufi_host import hfe as H, cli, protocol2 as P2
from test_protocol2 import FakeV2


def raw(values):
    return bytes(int(f'{v:08b}'[::-1], 2) for v in values)


def test_timing_skip_index_rotation_and_wrap():
    track = H.parse_hfe_v3(raw([0xf2, 72, 0x80, 0xf1, 0xf2, 36, 0x40, 0xf3, 7, 1]))
    assert track.indexes == [8] and len(track.bits) == 17
    deltas, duration = track.flux()
    assert deltas == [550, 1925, 550] and duration == 6875
    wrapped = H.parse_hfe_v3(raw([0x80, 0xf2, 72, 0x80]))
    assert wrapped.periods == bytes([72]*16)
    assert wrapped.flux() == ([550, 4400], 8800)


def test_weak_metadata_and_malformed_opcodes():
    track = H.parse_hfe_v3(raw([0xf2, 72, 0xf4, 0xf3, 5, 0xf4]))
    assert track.weak == [(0, 11)] and len(track.bits) == 11
    with pytest.raises(ValueError, match='weak-write'):
        track.flux()
    for data in ([0xf2], [0xf2, 0], [0xf3, 1], [0xf3, 0, 1],
                 [0xf3, 7, 0x80], [0xf5], [0x80], [0xf2, 72]):
        with pytest.raises(ValueError): H.parse_hfe_v3(raw(data))
    multi = H.parse_hfe_v3(raw([0xf2, 72, 0xf1, 0x80, 0xf1, 0x80]))
    with pytest.raises(ValueError, match='multiple index'):
        multi.flux()


def test_v3_file_and_no_lossy_export(tmp_path):
    path = tmp_path/'v3.hfe'
    blob = bytearray(1536);blob[:8] = b'HXCHFEV3';blob[9:12] = bytes([1,1,0])
    struct.pack_into('<H', blob, 12, 250);struct.pack_into('<H', blob, 18, 1)
    data = raw([0xf2,72,0xf1,0x80,0x40,0x20])
    struct.pack_into('<HH', blob, 512, 2, len(data)*2);blob[1024:1024+len(data)] = data
    path.write_bytes(blob);img = H.read_hfe(path)
    assert img.v3_tracks[0,0].flux() == ([550,4950,4950],13200)
    with pytest.raises(ValueError, match='metadata'):
        H.write_hfe(tmp_path/'lossy.hfe', img)


def test_cli_preserves_v3_timing_and_preflights_all_tracks(monkeypatch):
    img = H.HfeImage(250000)
    img.v3_tracks[0,0] = H.parse_hfe_v3(raw([0xf2,72,0x80,0x40,0x20]))
    img.tracks[0,0] = b''
    dev = P2.Device2(None);calls = []
    monkeypatch.setattr(dev, 'describe', lambda: SimpleNamespace(flags=2,store_words=100000))
    monkeypatch.setattr(dev, 'write_track', lambda *a, **kw: calls.append((a,kw)))
    monkeypatch.setattr(cli, '_write_text', lambda *a: 'ok')
    args = argparse.Namespace(scale=1.0, clamp=False, hard_sectors=0, verify=True, quarter=0)
    cli._hfe_write_tracks(dev,args,img)
    assert calls[0][0][2] == [550,4950,4950]
    assert calls[0][1]['extended'] and calls[0][1]['firmware_verify']
    calls.clear();img.v3_tracks[1,0] = H.parse_hfe_v3(raw([0xf2,72,0xf4]))
    with pytest.raises(ValueError):cli._hfe_write_tracks(dev,args,img)
    assert not calls
    img.v3_tracks[1,0] = H.parse_hfe_v3(raw([0xf2,1,0x80,0x40,0x20]))
    with pytest.raises(ValueError, match='limits'):cli._hfe_write_tracks(dev,args,img)
    assert not calls


def test_firmware_verify_source_index_on_wire(monkeypatch):
    wire = FakeV2();dev = P2.Device2(wire)
    monkeypatch.setattr(dev,'describe',lambda:SimpleNamespace(flags=2,store_words=100000))
    dev.write_track(0,0,[550,4950,4950],extended=True,firmware_verify=True,index_ticks=13200)
    req = next(f for f in wire.requests if f.cmd == P2.WRITE)
    assert req.payload[2] == 0x61 and req.payload[-4:] == struct.pack('<I',13200)


def test_regions_decode_and_bounds():
    from ufi_host.regions import decode_regions, CONTENT_VARIATION, ALIGNMENT_UNCERTAIN
    report = bytearray(1312);report[:3] = bytes([1,32,40]);report[14] = 2
    struct.pack_into('<I',report,4,42)
    struct.pack_into('<I',report,32,CONTENT_VARIATION|ALIGNMENT_UNCERTAIN)
    decoded = decode_regions(report)
    assert decoded['generation'] == 42 and decoded['compared_revolutions'] == 2
    assert decoded['windows'][0]['flags'] == 72
    for bad in (report[:-1], report+b'\x00', bytes(1312)):
        with pytest.raises(ValueError):decode_regions(bad)


def test_export_all_literals_and_metadata(tmp_path):
    bits = bytes((v >> shift) & 1 for v in range(256) for shift in range(7,-1,-1))
    track = H.HfeV3Track(bits, bytes([72])*len(bits), [], [0, 7, len(bits)])
    assert H.parse_hfe_v3(H.encode_hfe_v3(track)) == track
    weak = H.parse_hfe_v3(raw([0xf2,72,0xf4,0xf3,5,0xf4,0xf1,0xf2,36,0x80]))
    assert H.parse_hfe_v3(H.encode_hfe_v3(weak)) == weak
    img = H.HfeImage(250000)
    img.v3_tracks = {(0,0):track, (0,1):weak}
    path = tmp_path/'export.hfe'
    H.write_hfe_v3(path,img)
    assert H.read_hfe(path).v3_tracks == img.v3_tracks


def test_invalid_export_preserves_destination(tmp_path):
    path = tmp_path/'existing.hfe';path.write_bytes(b'original')
    img = H.HfeImage(250000)
    img.v3_tracks[0,0] = H.HfeV3Track(b'\1',b'\0',[],[])
    with pytest.raises(ValueError):H.write_hfe_v3(path,img)
    assert path.read_bytes() == b'original'
