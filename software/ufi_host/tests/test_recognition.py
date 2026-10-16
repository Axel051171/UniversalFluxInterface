import struct
import pytest
from ufi_host.recognition import decode_recognition, decode_attempt_assessments


def test_recognition_masks_and_failed_retry_are_distinct():
    report = bytearray(64);report[:4] = bytes([1,2,1,2])
    struct.pack_into('<4I',report,4,0x24f,0,11,0)
    struct.pack_into('<QQ',report,20,0xffe,0xffe)
    struct.pack_into('<II',report,52,42,100)
    report[51]=1;report[60:64]=bytes([2,2,2,0])
    decoded=decode_recognition(report)
    assert decoded['expected']==0xffe and decoded['confirmations']==2 and decoded['generation']==42
    assert decoded['expected_set_supplied'] and not decoded['cause_names']
    rows=struct.pack('<4I4B',10,1,11,0,2,1,0,0)+struct.pack('<4I4B',136,128|8192,0,0,1,0,0,0)
    before,after=decode_attempt_assessments(rows)
    assert before['cause_names']==['missing_sectors'] and before['decision']==1
    assert after['cause_names']==['position','incomplete_evidence'] and not after['valid_sector_records']


def test_invalid_reports_are_not_partial_success():
    for bad in (b'', bytes(63), bytes(64), bytes(65)):
        with pytest.raises(ValueError):decode_recognition(bad)
    for bad in (b'', bytes(19), bytes(21), bytes(180)):
        with pytest.raises(ValueError):decode_attempt_assessments(bad)
