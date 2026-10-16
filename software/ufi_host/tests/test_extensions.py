import struct
import pytest
from ufi_host.jobs import JobStream, END
from ufi_host.iec_identity import identify
from ufi_host.iec_job import write_image
from ufi_host import cbm
from ufi_host.usb_bulk import BulkStream
from ufi_host import protocol2 as P2
from test_protocol2 import FakeV2, REVS
import zlib


class JobFirmware(FakeV2):
    """Independent wire oracle: no drive write before verified COMMIT."""
    def __init__(self):
        super().__init__(REVS, data_chunk=17)
        self.job_id=0; self.packets=0; self.total=0; self.checksum=0; self.committed=False

    def _frame(self, kind, cmd, seq, status, payload=b""):
        if self.job_id and cmd==P2.READ and kind in (P2.T_DATA,P2.T_END):
            prefix=struct.pack("<QII",self.job_id,self.packets,self.total)
            if kind==P2.T_DATA:
                self.packets+=1;self.total+=len(payload);self.checksum=zlib.crc32(payload,self.checksum)
            else:
                prefix+=struct.pack("<I",self.checksum)
            payload=prefix+payload
        super()._frame(kind,cmd,seq,status,payload)

    def _handle(self,f):
        if f.type==P2.T_REQUEST and f.cmd==P2.JOB:
            if f.payload[0]==1:
                _,self.kind,self.job_id=struct.unpack("<BBQ",f.payload)
                self.packets=self.total=self.checksum=0
            else:
                assert self.kind==2 and self.total==self.write_req[2]
                assert struct.unpack("<BI",f.payload)==(2,self.checksum)
                self.committed=True
            status=struct.pack("<BBBBQIII",1,self.kind,3 if self.committed else 1,0,
                               self.job_id,self.packets,self.total,self.checksum)
            self._frame(P2.T_RESPONSE,f.cmd,f.seq,0,status)
            if self.committed:
                proof=struct.pack("<QIII",self.job_id,self.packets,self.total,self.checksum)
                self._frame(P2.T_END,P2.WRITE,self.write_req[0],0,proof+struct.pack("<BI",1,0))
            return
        if f.type==P2.T_DATA and f.cmd==P2.WRITE:
            assert not self.committed
            assert struct.unpack_from("<QII",f.payload)==(self.job_id,self.packets,self.total)
            chunk=f.payload[16:]
            self.packets+=1;self.total+=len(chunk);self.checksum=zlib.crc32(chunk,self.checksum)
            self.write_code+=chunk
            return
        super()._handle(f)


def test_production_host_extended_read_and_commit():
    reader=P2.Device2(JobFirmware())
    result=reader.read_track(0,0,len(REVS),integrity=True)
    assert len(result.revolutions)==len(REVS)
    wire=JobFirmware(); writer=P2.Device2(wire);writer.max_payload=64
    writer.write_track(0,0,[1100]*5000,integrity=True)
    assert wire.committed and wire.packets>1


def test_job_loss_duplicate_corruption():
    tx, rx = JobStream(0x123456789abcdef), JobStream(0x123456789abcdef)
    first, second = tx.encode(b"abc"), tx.encode(b"def")
    with pytest.raises(ValueError): rx.decode(second)
    assert rx.decode(first) == b"abc"
    with pytest.raises(ValueError): rx.decode(first)
    wrong = bytearray(second); wrong[0] ^= 1
    with pytest.raises(ValueError): rx.decode(wrong)
    assert rx.decode(second) == b"def"
    proof = END.pack(tx.id, tx.packets, tx.offset, tx.crc)
    assert rx.finish(proof+b"result") == b"result"
    for bad in (proof[:-1], proof[:-1]+bytes([proof[-1]^1])):
        with pytest.raises(ValueError): rx.finish(bad)


def test_bulk_connect_nonce_and_failure_closes(monkeypatch):
    class Bulk(FakeV2):
        def cancel(self): pass
        def reset_input_buffer(self): pass
        def close(self): self.closed=True
    wire=Bulk();wire.closed=False
    monkeypatch.setattr(P2,"open_serial",lambda port:wire)
    seen=[]
    def ping(self,data,timeout):
        seen.append(data);return True
    monkeypatch.setattr(P2.Device2,"ping",ping)
    monkeypatch.setattr(P2.Device2,"device_info",lambda self:None)
    P2.connect("usb");P2.connect("usb")
    assert len(seen[0])==16 and seen[0]!=seen[1]
    monkeypatch.setattr(P2.Device2,"ping",lambda *a,**kw:False)
    with pytest.raises(TimeoutError):P2.connect("usb")
    assert wire.closed


def test_extended_requires_advertised_backend(monkeypatch):
    from types import SimpleNamespace
    dev=P2.Device2(FakeV2())
    monkeypatch.setattr(dev,"describe",lambda:SimpleNamespace(flags=0,store_words=999))
    with pytest.raises(ValueError,match="advertise"):
        dev.write_track(0,0,[344]*3,extended=True)


def test_cli_does_not_downgrade_integrity_to_v1(monkeypatch,capsys):
    from ufi_host import cli
    monkeypatch.setattr(cli.P2,"connect",lambda port:object())
    monkeypatch.setattr("sys.argv",["ufi","--integrity","info"])
    assert cli.main()==1
    assert "require UFI v2" in capsys.readouterr().err


@pytest.mark.parametrize("magic,extra,model", [(0xf00f,0,"1541-II"),(0xcd18,0,"1541C"),
    (0x02ac,0,"1571"),(0x01ba,0,"1581"),(0x01ba,0x4446,None),(0x1234,0,None),
    (0xaaaa,0xfe67,"1541")])
def test_rom_identity(magic, extra, model):
    class Drive:
        def iec_mem_read(self,address,n,device):
            assert n == 2 and device == 9
            return (magic if address == 0xff40 else extra).to_bytes(2,"little")
    assert identify(Drive(),9)["model"] == model


def test_verified_write_resume_rechecks_disk(tmp_path, monkeypatch):
    monkeypatch.setattr(cbm, "blocks", lambda kind,tracks: iter([(1,0),(1,1),(1,2)]))
    source = tmp_path/"source.d64"
    source.write_bytes(b"x"*174848)
    journal = tmp_path/"write.json"
    class Drive:
        def __init__(self): self.disk={}; self.writes=[]; self.interrupt=True
        def iec_block_write(self,t,s,data,device):
            if s==1 and self.interrupt: raise TimeoutError("USB disconnected")
            self.writes.append((t,s)); self.disk[t,s]=data; return 0
        def iec_block_result(self,t,s,device): return 0,self.disk[t,s]
    drive=Drive()
    with pytest.raises(TimeoutError): write_image(drive,str(source),str(journal))
    assert drive.writes == [(1,0)]
    drive.interrupt=False
    result=write_image(drive,str(source),str(journal),resume=True)
    assert result["complete"] and drive.writes==[(1,0),(1,1),(1,2)]
    drive.disk[1,0]=b"y"*256
    with pytest.raises(ValueError,match="disk"):
        write_image(drive,str(source),str(journal),resume=True)
    assert len(drive.writes)==3


def test_bulk_only_claims_native_interface():
    from types import SimpleNamespace
    class Interface(list): bInterfaceClass=0xff
    class USB:
        def __init__(self): self.actions=[]
        def get_active_configuration(self):
            return {(2,0): Interface([SimpleNamespace(bEndpointAddress=a) for a in (3,0x83)])}
        def ctrl_transfer(self,*args,**kwargs): self.actions.append(args)
        def clear_halt(self,ep): assert ep==0x83
        def read(self,ep,n,timeout): assert ep==0x83 and n%64==0; return b"abcdef"
        def write(self,ep,data,timeout): assert ep==3; return len(data)
    class Util:
        @staticmethod
        def claim_interface(d,i): assert i==2
        @staticmethod
        def release_interface(d,i): assert i==2
        @staticmethod
        def dispose_resources(d): pass
    d=USB(); stream=BulkStream(d,Util)
    assert stream.read(2)==b"ab" and stream.read(4)==b"cdef"
    assert stream.write(b"abc")==3
    stream.reset_input_buffer();stream.cancel();stream.close()
    assert [(a[0],a[1],a[3]) for a in d.actions]==[(0x41,0x21,2),(0x41,0x22,2)]
