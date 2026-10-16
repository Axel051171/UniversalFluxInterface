"""Reproducible independent GCR encoder corpus; --download once, then offline.
No upstream code is linked into firmware. G64->flux only unpacks recorded bits.
"""
import hashlib
import json
import os
import struct
import subprocess
import sys
import urllib.request
from run_tests import FW, BUILD, compiler


def main():
    manifest = json.loads((FW/'tests/reference/gcr_corpus.json').read_text())
    BUILD.mkdir(exist_ok=True)
    source = BUILD/'cc1541.c'
    if '--download' in sys.argv and not source.exists():
        url = f"https://raw.githubusercontent.com/TrantorHF/cc1541/{manifest['commit']}/cc1541.c"
        blob = urllib.request.urlopen(url, timeout=60).read()
        if hashlib.sha256(blob).hexdigest() != manifest['sha256']:
            raise ValueError('upstream source digest mismatch')
        source.write_bytes(blob)
    if not source.exists():
        if '--if-present' in sys.argv:
            print('External GCR encoder corpus NOT RUN: use test_gcr_corpus.py --download.')
            return
        raise FileNotFoundError('Download pinned cc1541 source with --download first.')
    if hashlib.sha256(source.read_bytes()).hexdigest() != manifest['sha256']:
        raise ValueError('encoder source digest mismatch')
    suffix = '.exe' if os.name == 'nt' else ''
    encoder = BUILD/('cc1541'+suffix)
    subprocess.run([compiler(), '-O2', str(source), '-o', str(encoder)], check=True, timeout=120)
    payload = BUILD/'gcr_payload.prg'
    payload.write_bytes(bytes((i*73+(i>>8)) & 255 for i in range(50000)))
    disk,g64 = BUILD/'external_gcr.d64', BUILD/'external_gcr.g64'
    # Only remove our generated pair, never a user image.
    disk.unlink(missing_ok=True)
    g64.unlink(missing_ok=True)
    # Upstream G64 headers use literal ASCII "2A"; BAM text is PETSCII-converted.
    subprocess.run([str(encoder), '-n', 'UFI CORPUS', '-i', '2A', '-f', 'VECTOR',
                    '-w', str(payload), '-g', str(g64), str(disk)], check=True,
                   stdout=subprocess.DEVNULL, timeout=30)
    blob = g64.read_bytes()
    if blob[:8] != b'GCR-1541' or blob[8] != 0:
        raise ValueError('unexpected G64 header')
    binary = BUILD/'external_gcr_ticks.bin'
    with binary.open('wb') as f:
        f.write(struct.pack('<I', 35))
        for track in range(35):
            offset = struct.unpack_from('<I', blob, 12+8*track)[0]
            speed = struct.unpack_from('<I', blob, 12+4*blob[9]+8*track)[0]
            if not offset or speed>3:
                raise ValueError('missing track or variable speed map')
            length = struct.unpack_from('<H', blob, offset)[0]
            bits = [bool(b & (128 >> k)) for b in blob[offset+2:offset+2+length] for k in range(8)]
            positions = [i for i, b in enumerate(bits) if b]
            rate = [250000,266667,285714,307692][speed]
            previous = positions[-1]-len(bits)
            deltas = []
            for pos in positions:
                deltas.append(round((pos-previous)*275000000/rate))
                previous = pos
            f.write(struct.pack('<III', track+1, rate, len(deltas)))
            f.write(struct.pack(f'<{len(deltas)}I', *deltas))
    exe = BUILD/('gcr_corpus_decoder'+suffix)
    flags = ['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('UFI_SANITIZE') else []
    subprocess.run([compiler(), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    '-D_CRT_SECURE_NO_WARNINGS', *flags, '-I', str(FW/'include'),
                    str(FW/'tests/gcr_corpus_decoder.c'), str(FW/'src/ufi_mfm.c'),
                    str(FW/'src/ufi_verify.c'), '-o', str(exe)], check=True, timeout=120)
    subprocess.run([str(exe), str(disk), str(binary)], check=True, timeout=60)


if __name__ == '__main__':
    main()
