"""Local user-supplied BMOW MOOFs; no firmware binary execution or redistribution."""
import argparse
import hashlib
import shutil
import struct
import subprocess
import zipfile
import zlib
from pathlib import Path
from run_tests import BUILD, FW, compiler

SHA = 'd5fac3b5bfd4b2ad8caa7ac69a6a179632d3d0bf44582fa0455a6e9e5584a94f'
IMAGES = {'Blank 400K.moof': ('b941df385d8a7dbc99f975dcc81944dcf481847fa019eb4d44749e3a3c5abd0e', 1),
          'Blank 800K.moof': ('98a834764a2c2089e70709ebf52e934486116f4f56fbb8fcf1c484391c473a80', 2)}

def tracks(data, heads):
    assert data[:8] == b'MOOF\xff\n\r\n'
    crc, = struct.unpack_from('<I', data, 8)
    assert not crc or crc == zlib.crc32(data[12:])
    chunks = {}; at = 12
    while at < len(data):
        assert at + 8 <= len(data)
        kind, size = struct.unpack_from('<4sI', data, at)
        assert at + 8 + size <= len(data) and kind not in chunks
        chunks[kind] = (at + 8, size); at += 8 + size
    info, size = chunks[b'INFO']; assert size >= 60
    assert data[info] == 1 and data[info+1] == heads and data[info+4] == 16
    assert data[info+40:info+44] == bytes(4)  # bitstreams only in these fixtures
    tmap, size = chunks[b'TMAP']; assert size == 160
    start, size = chunks[b'TRKS']; assert size >= 1280
    for c in range(80):
        for h in range(2):
            entry = data[tmap+c*2+h]
            if h >= heads:
                assert entry == 255; continue
            assert entry < 160
            block, blocks, bits = struct.unpack_from('<HHI', data, start+entry*8)
            assert bits and (bits+7)//8 <= blocks*512
            assert block*512 >= start+1280 and (block+blocks)*512 <= start+size
            yield struct.pack('<III', c, h, bits) + data[block*512:block*512+(bits+7)//8]

def main():
    p=argparse.ArgumentParser();p.add_argument('--archive',type=Path);p.add_argument('--if-present',action='store_true');args=p.parse_args()
    cache=BUILD/'mac_corpus'/'femu-231209M.zip'; source=args.archive or cache
    if not source.exists() and args.if_present:
        print('SKIP Macintosh MOOF corpus: local femu-231209M.zip absent');return
    assert hashlib.sha256(source.read_bytes()).hexdigest()==SHA
    cache.parent.mkdir(parents=True,exist_ok=True)
    if source.resolve()!=cache.resolve():shutil.copyfile(source,cache)
    records=[]
    with zipfile.ZipFile(cache) as z:
        for name,(sha,heads) in IMAGES.items():
            data=z.read('femu-231209M/examples/'+name)
            assert hashlib.sha256(data).hexdigest()==sha
            records.extend(tracks(data,heads))
    vectors=cache.parent/'tracks.bin';vectors.write_bytes(struct.pack('<I',len(records))+b''.join(records))
    exe=BUILD/'mac_corpus_decoder.exe'
    subprocess.run([compiler(),'-std=c11','-O2','-Wall','-Wextra','-Werror','-D_CRT_SECURE_NO_WARNINGS','-I',str(FW/'include'),
        str(FW/'tests/mac_corpus_decoder.c'),*[str(FW/'src'/s) for s in ('ufi_mfm.c','ufi_verify.c','ufi_quality.c')],'-o',str(exe)],check=True)
    subprocess.run([str(exe),str(vectors)],check=True,timeout=90)

if __name__=='__main__':main()
