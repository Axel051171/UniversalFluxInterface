"""Firmware stop-and-wait analysis plans (native 0x0f operations 27..29).

Persist each result before acknowledge(). A checkpoint is only a reconnect hint:
session identity and physical media must be established independently by caller.
"""
import hashlib
import json
import os
from pathlib import Path
import struct

def create_plan(plan_id, config, media, first, last, sides, start_payload):
    if any(not 0 < x <= 0xffffffff for x in (plan_id, config, media)):
        raise ValueError('plan and session tokens must be nonzero u32')
    if not 0 <= first <= last <= 83 or sides not in (1,2):
        raise ValueError('invalid track range')
    if not 28 <= len(start_payload) <= 61 or start_payload[0] != 1:
        raise ValueError('invalid START template')
    return struct.pack('<BIIIBBB',27,plan_id,config,media,first,last,sides)+start_payload

def decode_status(data):
    if len(data)!=32 or data[0]!=1 or data[1]>5 or data[3]!=29 or any(data[29:]):
        raise ValueError('invalid multitrack status')
    plan_id,config,media,child,cursor,total=struct.unpack_from('<4I2H',data,4)
    if cursor>total or total>168 or (plan_id and (data[26] not in (1,2) or
       not 0 <= data[24] <= data[25] <= 83 or total!=(data[25]-data[24]+1)*data[26])):
        raise ValueError('invalid multitrack progress')
    return dict(state=data[1],error=data[2],plan_id=plan_id,config=config,media=media,
                child=child,cursor=cursor,total=total,first=data[24],last=data[25],
                sides=data[26],track=data[27],side=data[28])

def control(plan_id,cursor,action=0):
    if not 0 < plan_id <= 0xffffffff or not 0 <= cursor <= 168 or action not in (0,1,2):
        raise ValueError('invalid plan control')
    return struct.pack('<BIHB',29,plan_id,cursor,action)

def save_checkpoint(path, status, result_digest):
    """Atomic host checkpoint after saving result, before ACK. SHA256 is integrity only."""
    if status['state']!=3 or len(result_digest)!=64 or any(c not in '0123456789abcdef' for c in result_digest):
        raise ValueError('checkpoint requires completed result and SHA256')
    record=dict(schema=1,status=status,result_sha256=result_digest)
    payload=json.dumps(record,sort_keys=True,separators=(',',':')).encode()
    wrapper=json.dumps(dict(payload=record,sha256=hashlib.sha256(payload).hexdigest()),sort_keys=True)
    path=Path(path);temporary=path.with_name(path.name+'.tmp')
    with temporary.open('w',encoding='utf-8') as out:
        out.write(wrapper);out.flush();os.fsync(out.fileno())
    os.replace(temporary,path)

def load_checkpoint(path):
    wrapper=json.loads(Path(path).read_text(encoding='utf-8'))
    record=wrapper['payload'];payload=json.dumps(record,sort_keys=True,separators=(',',':')).encode()
    if record.get('schema')!=1 or hashlib.sha256(payload).hexdigest()!=wrapper['sha256']:
        raise ValueError('invalid checkpoint integrity or schema')
    return record
