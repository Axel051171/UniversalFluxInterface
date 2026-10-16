"""Optional local HxC fixtures: no execution or redistribution of supplied binaries."""
import argparse
import hashlib
import shutil
import struct
import subprocess
import sys
import zipfile
from pathlib import Path
from run_tests import BUILD, FW, compiler

sys.path.insert(0, str(FW.parent / 'software/ufi_host'))
from ufi_host.hfe import read_hfe

SHA = 'fab679db2536e257f274b0dd1dd44e5cc69005e64e6ea7433dd3b0d096320052'

def main():
    p = argparse.ArgumentParser()
    p.add_argument('--archive', type=Path)
    p.add_argument('--if-present', action='store_true')
    args = p.parse_args()
    cache = BUILD / 'hfe_corpus' / 'HXCFEUSB_Gotek_HxC_firmware.zip'
    source = args.archive or cache
    if not source.exists() and args.if_present:
        print('SKIP HFE corpus: local HxC archive absent'); return
    assert hashlib.sha256(source.read_bytes()).hexdigest() == SHA
    cache.parent.mkdir(parents=True, exist_ok=True)
    if source.resolve() != cache.resolve():
        shutil.copyfile(source, cache)
    records = []
    with zipfile.ZipFile(cache) as z:
        for name in z.namelist():
            if not name.lower().endswith('.hfe'): continue
            fixture = cache.parent / 'input.hfe'
            fixture.write_bytes(z.read(name))
            img = read_hfe(fixture)
            assert not img.v3_tracks and img.encoding in (0, 1)
            for (c, h), cells in sorted(img.tracks.items()):
                records.append(struct.pack('<5I', c, h, img.encoding+1, img.rate_bps, len(cells))+cells)
    assert len(records) == 536
    vectors = cache.parent / 'tracks.bin'
    vectors.write_bytes(struct.pack('<I', len(records))+b''.join(records))
    exe = BUILD / 'hfe_corpus_decoder.exe'
    subprocess.run([compiler(), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
        '-D_CRT_SECURE_NO_WARNINGS', '-I', str(FW/'include'), str(FW/'tests/hfe_corpus_decoder.c'),
        *[str(FW/'src'/s) for s in ('ufi_mfm.c', 'ufi_verify.c', 'ufi_quality.c')],
        '-o', str(exe)], check=True)
    subprocess.run([str(exe), str(vectors)], check=True, timeout=90)

if __name__ == '__main__': main()
