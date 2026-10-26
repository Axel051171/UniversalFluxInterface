"""Independent synthetic vectors from the documented Amiga trackdisk layout.

No production encoder or ADF-Drive code is used. Payloads are synthetic/MIT.
"""
from types import SimpleNamespace
import json

import pytest

from ufi_host import amiga, readback, protocol2 as P2
from test_protocol2 import FakeV2


def vector(spt=11, rate=250000, *, track=0, labels=None, change=None,
           bad_header=None, bad_data=None, omit=None, duplicate=False,
           rotate=0, rpm=300, jitter=0, stretch=1.0):
    bits = []
    previous = 0
    def raw(word, width):
        nonlocal previous
        bits.extend((word >> b) & 1 for b in range(width-1, -1, -1))
        previous = word & 1
    def encode(block):
        nonlocal previous
        data = [(b >> k) & 1 for b in block for k in range(7, -1, -1)]
        for bit in data[::2] + data[1::2]:
            bits.extend((int(not(previous or bit)), bit))
            previous = bit
    def check(block):
        # XOR corresponding bits of the original longwords, then fold their
        # odd/even bits into the checksum's data-bit positions.
        columns = [0] * 32
        for i, byte in enumerate(block):
            for k in range(8):
                columns[(i % 4)*8+k] ^= (byte >> (7-k)) & 1
        value = 0
        for i in range(0, 32, 2):
            value = (value << 2) | (columns[i] ^ columns[i+1])
        return value
    raw(0xaaaaaaaa, 32)
    expected = {}
    order = list(range(spt)) + ([0] if duplicate else [])
    for seq, sector in enumerate(order):
        if sector == omit:
            continue
        payload = bytes((i * 17 + sector * 3) % 256 for i in range(512))
        if change == sector or (duplicate and seq == spt):
            # XOR-compensated edits: Amiga's XOR checksum cannot detect this,
            # but byte comparison must still reject the change.
            data = bytearray(payload)
            data[0] ^= 1
            data[4] ^= 1
            payload = bytes(data)
        label = labels if labels is not None else bytes(range(16))
        info = bytes([255, track, sector, spt-sector])
        raw(0xaaaaaaaa, 32)
        raw(0x44894489, 32)
        encode(info)
        encode(label)
        encode((check(info+label) ^ (1 if sector == bad_header else 0)).to_bytes(4, 'big'))
        encode((check(payload) ^ (1 if sector == bad_data else 0)).to_bytes(4, 'big'))
        encode(payload)
        expected[(track, sector)] = amiga.Sector(label, payload, spt-sector)
    target = max(len(bits)+64, round(60/rpm*2*rate))
    while len(bits) < target:
        raw(0xaaaa, 16)
    rotate %= len(bits)
    bits = bits[rotate:] + bits[:rotate]
    tick = 275000000 / (2*rate) * stretch
    duration = round(len(bits)*tick)
    samples = [round((i+1)*tick) for i, bit in enumerate(bits) if bit]
    if jitter:
        samples = [min(duration-1, t + ((i % 3)-1)*jitter) for i,t in enumerate(samples)]
    previous = 0
    deltas = []
    for sample in samples:
        deltas.append(sample-previous)
        previous = sample
    rev = SimpleNamespace(samples=samples, index_ticks=duration, flags=1)
    return deltas, rev, expected


def capture(*revs, status=0):
    return SimpleNamespace(revolutions=list(revs), status=status)


@pytest.mark.parametrize('spt,rate', [(11,250000), (22,500000), (22,250000)])
def test_reference_data_labels_and_verify(spt, rate):
    deltas, rev, expected = vector(spt, rate, rpm=150 if spt==22 and rate==250000 else 300)
    found_rate, ref = readback.reference(deltas)
    assert found_rate == rate and ref == expected
    assert ref.spt == spt
    report = {}
    assert readback.verify_capture(ref, rate, capture(rev, rev), quality=report) == spt
    assert report['complete']
    assert all(s['state']=='stable' for s in report['sectors'])


@pytest.mark.parametrize('kwargs', [{'labels': bytes(16)}, {'change': 5}])
def test_checksum_valid_label_or_payload_change_rejected(kwargs):
    deltas, _, _ = vector()
    rate, ref = readback.reference(deltas)
    _, rev, _ = vector(**kwargs)
    with pytest.raises(ValueError, match='mismatch'):
        readback.verify_capture(ref, rate, capture(rev))


@pytest.mark.parametrize('kwargs', [{'bad_header': 3}, {'bad_data': 3}, {'omit': 3}])
def test_incomplete_source_refused_and_missing_reported(kwargs):
    deltas, rev, _ = vector(**kwargs)
    with pytest.raises(ValueError, match='no disk was written'):
        readback.reference(deltas)
    report, _ = amiga.analyze_capture(capture(rev), 250000, 0)
    assert not report['complete']
    assert report['sectors'][3]['state'] == 'missing'
    if 'bad_data' in kwargs:
        assert report['sectors'][3]['data_checksum_error']


def test_conflicting_valid_duplicates_and_revolutions_never_vote():
    deltas, _rev, _ = vector(duplicate=True)
    with pytest.raises(ValueError, match='conflicting'):
        readback.reference(deltas)
    _, good, _ = vector()
    _, changed, _ = vector(change=0)
    report, decoded = amiga.analyze_capture(capture(good, good, changed), 250000, 0)
    assert report['conflicting_sectors'] == [[0,0]]
    assert (0,0) not in decoded and not report['complete']
    assert report['sectors'][0]['state'] == 'conflicting'


@pytest.mark.parametrize('spt,rate,rpm', [(11,250000,297), (11,300000,360), (22,500000,303)])
@pytest.mark.parametrize('rotate', [77, 3500, 8704*10+900])
def test_index_splice_jitter_and_rotation(spt, rate, rpm, rotate):
    _, rev, expected = vector(spt, rate, rotate=rotate, rpm=rpm, jitter=9)
    report, decoded = amiga.analyze_capture(capture(rev,rev,rev), rate, 0, spt)
    assert report['complete'] and decoded == expected


def test_source_split_at_index_requires_real_duration():
    deltas, rev, expected = vector(rotate=3500)
    with pytest.raises(ValueError, match='incomplete'):
        readback.reference(deltas)
    rate, ref = readback.reference(deltas,index_ticks=rev.index_ticks)
    assert rate==250000 and ref==expected
    with pytest.raises(ValueError, match='duration'):
        readback.reference(deltas,index_ticks=1)


def test_weak_track_evidence_bounded_attempts_and_raw_unchanged():
    _, good, _ = vector()
    _, bad, _ = vector(bad_data=4)
    original = bad.samples.copy()
    report, _ = amiga.analyze_capture(capture(bad, good), 250000, 0)
    assert report['complete'] and report['sectors'][4]['state']=='intermittent'
    assert len(report['revolutions'][0]['attempts'])==3
    assert sum(report['histogram']['counts'])==len(bad.samples)+len(good.samples)
    assert bad.samples==original
    assert 'not proof' in report['interpretation']
    single, _ = amiga.analyze_capture(capture(good), 250000, 0, adaptive=False)
    assert single['sectors'][0]['state']=='observed_once'
    assert len(single['revolutions'][0]['attempts'])==1


def test_alternative_pll_recovers_real_jitter_vector():
    _, rev, expected = vector()
    rev.samples = [min(rev.index_ticks-1,t+(((i*17)%7)-3)*190//3)
                   for i,t in enumerate(rev.samples)]
    standard, _ = amiga.analyze_capture(capture(rev),250000,0,adaptive=False)
    adaptive, decoded = amiga.analyze_capture(capture(rev),250000,0)
    assert not standard['complete']
    assert adaptive['complete'] and decoded==expected
    assert all(s['state']=='recovered' for s in adaptive['sectors'])


def test_overflow_wrong_track_and_invalid_timestamps():
    deltas, good, _ = vector()
    rate, ref = readback.reference(deltas)
    good.flags=3
    with pytest.raises(ValueError, match='overflow'):
        readback.verify_capture(ref, rate, capture(good))
    _, other, _ = vector(track=1)
    with pytest.raises(ValueError, match='unexpected'):
        readback.verify_capture(ref, rate, capture(other))
    other.samples[2]=other.samples[1]
    with pytest.raises(ValueError, match='timestamps'):
        amiga.analyze_capture(capture(other), rate, 0)


def test_extended_write_amiga_and_preflight(monkeypatch):
    deltas, rev, _ = vector()
    wire=FakeV2()
    dev=P2.Device2(wire)
    monkeypatch.setattr(dev, 'describe', lambda: SimpleNamespace(flags=2,store_words=200000))
    monkeypatch.setattr(dev, 'read_track', lambda *a, **kw: capture(rev,rev))
    result=dev.write_track(0,0,deltas,extended=True,verify=True)
    assert result.host_verified_sectors==11 and result.readback_quality['complete']
    assert 'Amiga' in result.verify_text and 'labels' in result.verify_text
    wire.requests.clear()
    with pytest.raises(ValueError, match='no disk was written'):
        dev.write_track(1,0,deltas,extended=True,verify=True)
    assert not any(f.cmd==P2.WRITE for f in wire.requests)


def test_quality_cli_offline_scp(tmp_path, monkeypatch):
    from ufi_host import cli, scp
    _, rev, _ = vector()
    image=tmp_path/'amiga.scp'
    report=tmp_path/'quality.json'
    scp.write_scp(str(image), {0:[scp.ScpRevolution(8000000,scp.ticks_to_scp(rev.samples))]*2}, 2)
    monkeypatch.setattr(P2, 'connect', lambda *a: pytest.fail('offline command must not connect'))
    assert cli.main(['amiga-quality',str(image),'-o',str(report)])==0
    assert json.loads(report.read_text())['complete']
    assert cli.main(['amiga-quality',str(image),'--track','1'])==1
