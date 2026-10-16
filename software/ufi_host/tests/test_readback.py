from types import SimpleNamespace
import struct
import pytest
from ufi_host import readback, protocol2 as P2
from test_protocol2 import FakeV2


def crc(data):
    value=0xffff
    for byte in data:
        value^=byte<<8
        for _ in range(8):value=((value<<1)^0x1021 if value&0x8000 else value<<1)&0xffff
    return value


def ibm_track(payload, damage_crc=False, jitter=False):
    """Independent encoder: missing-clock A1 marks, ID/data CRC and normal MFM."""
    bits=[];previous=0
    def raw(word):bits.extend((word>>i)&1 for i in range(15,-1,-1))
    def byte(value):
        nonlocal previous
        for i in range(7,-1,-1):
            b=(value>>i)&1;bits.extend((int(not(previous or b)),b));previous=b
    def field(data,bad=False):
        nonlocal previous
        for _ in range(12):byte(0)
        for _ in range(3):raw(0x4489)
        previous=1
        check=crc(b'\xa1'*3+data)^(1 if bad else 0)
        for b in data+check.to_bytes(2,'big'):byte(b)
        for _ in range(24):byte(0x4e)
    for _ in range(40):byte(0x4e)
    field(bytes([0xfe,0,0,1,(len(payload)//128).bit_length()-1]))
    field(b'\xfb'+payload,damage_crc)
    # 500 kbit/s -> 275 native ticks per MFM cell.
    positions=[(i+1)*275 for i,b in enumerate(bits) if b]
    if jitter:positions=[p+((i%3)-1)*12 for i,p in enumerate(positions)]
    deltas=[];prev=0
    for p in positions:deltas.append(p-prev);prev=p
    return deltas,positions


def capture(positions,status=0):
    return SimpleNamespace(status=status,revolutions=[SimpleNamespace(samples=positions)])


def test_sector_readback_jitter_crc_and_changed_data():
    payload=bytes(range(256))*2
    deltas,positions=ibm_track(payload)
    rate,ref=readback.reference(deltas)
    assert rate==500000 and ref[(0,0,1,2)]==(0xfb,payload)
    assert readback.verify_capture(ref,rate,capture(ibm_track(payload,jitter=True)[1]))==1
    with pytest.raises(ValueError,match='missing'):
        readback.verify_capture(ref,rate,capture(ibm_track(payload,damage_crc=True)[1]))
    with pytest.raises(ValueError,match='mismatch'):
        readback.verify_capture(ref,rate,capture(ibm_track(bytes(512))[1]))
    with pytest.raises(ValueError,match='incomplete'):
        readback.verify_capture(ref,rate,capture(positions,status=10))
    with pytest.raises(ValueError,match='no disk was written'):
        readback.reference([344]*100)


@pytest.mark.parametrize('size',[128,256,512,1024,2048,4096,8192])
def test_all_supported_sector_sizes(size):
    payload=bytes(i%251 for i in range(size))
    deltas,positions=ibm_track(payload)
    rate,ref=readback.reference(deltas)
    assert next(iter(ref.values()))[1]==payload
    assert readback.verify_capture(ref,rate,capture(positions))==1


def test_extended_write_automatically_reads_back(monkeypatch):
    deltas,positions=ibm_track(bytes(range(256))*2)
    wire=FakeV2();dev=P2.Device2(wire)
    monkeypatch.setattr(dev,'describe',lambda:SimpleNamespace(flags=2,store_words=100000))
    reads=[]
    def read(*args,**kw):reads.append((args,kw));return capture(positions)
    monkeypatch.setattr(dev,'read_track',read)
    result=dev.write_track(0,0,deltas,extended=True,verify=True)
    assert result.host_verified_sectors==1 and result.verify_level==3
    assert reads[0][0]==(0,0,2)
    req=next(f for f in wire.requests if f.cmd==P2.WRITE)
    assert req.payload[2]==0x20  # no unsupported firmware VERIFY flag


def test_unsupported_verification_has_no_write_side_effect(monkeypatch):
    wire=FakeV2();dev=P2.Device2(wire)
    monkeypatch.setattr(dev,'describe',lambda:SimpleNamespace(flags=2,store_words=100000))
    with pytest.raises(ValueError,match='no disk was written'):
        dev.write_track(0,0,[344]*100,extended=True,verify=True)
    assert not any(f.cmd==P2.WRITE for f in wire.requests)


def test_diagnostics_schema(monkeypatch):
    dev=P2.Device2(FakeV2())
    payload=bytes([1,1,2,0])+struct.pack('<15I',*range(15))
    monkeypatch.setattr(dev,'request',lambda cmd:payload)
    info=dev.diagnostics()
    assert info['psram'] and info['owner']==2 and info['native_idle_timeout_ms']==14
    payload=payload[:-1]
    with pytest.raises(ValueError):dev.diagnostics()
