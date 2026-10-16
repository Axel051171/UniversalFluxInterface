import hashlib
import json
import struct
import pytest
from ufi_host.multitrack import create_plan,decode_status,control,save_checkpoint,load_checkpoint

def test_plan_wire_and_bounds():
    start=bytes([1])+bytes(32)
    assert create_plan(42,3,4,0,79,2,start)==struct.pack('<BIIIBBB',27,42,3,4,0,79,2)+start
    assert control(42,159,1)==struct.pack('<BIHB',29,42,159,1)
    for args in ((0,3,4,0,79,2),(42,3,4,80,79,2),(42,3,4,0,84,2),(42,3,4,0,79,3)):
        with pytest.raises(ValueError):create_plan(*args,start)

def test_status_checkpoint_integrity(tmp_path):
    raw=bytearray(32);raw[:4]=bytes([1,3,0,29])
    struct.pack_into('<4I2H',raw,4,42,3,4,43,1,160);raw[24:29]=bytes([0,79,2,0,1])
    status=decode_status(raw);assert status['cursor']==1 and status['child']==43
    path=tmp_path/'checkpoint.json'
    save_checkpoint(path,status,hashlib.sha256(b'report').hexdigest())
    assert load_checkpoint(path)['status']==status
    bad=json.loads(path.read_text());bad['payload']['status']['cursor']=2;path.write_text(json.dumps(bad))
    with pytest.raises(ValueError):load_checkpoint(path)
    raw[20]=161
    with pytest.raises(ValueError):decode_status(raw)
