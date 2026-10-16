"""Pinned external capture corpus; optional download, deterministic offline replay.

python firmware/tests/test_flux_corpus.py --download
python firmware/tests/test_flux_corpus.py              # requires local fixtures
--if-present is used by the offline gate and explicitly reports absent fixtures.
"""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import urllib.request

from run_tests import FW, BUILD, compiler

sys.path.insert(0, str(FW.parent / 'software/ufi_host'))
from ufi_host.scp import read_scp, scp_to_ticks


def main():
    manifest = json.loads((FW/'tests/reference/flux_corpus.json').read_text())
    BUILD.mkdir(exist_ok=True)
    for name, digest in manifest['files'].items():
        path = BUILD/name
        if '--download' in sys.argv and not path.exists():
            url = f"https://raw.githubusercontent.com/dbalsom/fluxfox/{manifest['commit']}/{manifest['path']}{name}"
            blob = urllib.request.urlopen(url, timeout=60).read()
            if hashlib.sha256(blob).hexdigest() != digest:
                raise ValueError(f'upstream digest mismatch: {name}')
            path.write_bytes(blob)
        if not path.exists():
            if '--if-present' in sys.argv:
                print('External capture corpus NOT RUN: download pinned fixtures with --download.')
                return
            raise FileNotFoundError(f'{path}: run with --download once')
        if hashlib.sha256(path.read_bytes()).hexdigest() != digest:
            raise ValueError(f'fixture digest mismatch: {name}')
    _, tracks = read_scp(str(BUILD/'sector_test_360k.scp'))
    binary = BUILD/'corpus_ticks.bin'
    with binary.open('wb') as f:
        f.write(struct.pack('<I', len(tracks)))
        for track, revolutions in sorted(tracks.items()):
            # SCP stores transition-to-transition intervals continuously over the
            # index boundary: do not insert a second artificial index gap.
            cells = [c for rev in revolutions for c in rev.cells]
            deltas = scp_to_ticks(cells)
            f.write(struct.pack('<II', track, len(deltas)))
            f.write(struct.pack(f'<{len(deltas)}I', *deltas))
    exe = BUILD/('corpus_decoder.exe' if os.name == 'nt' else 'corpus_decoder')
    flags = ['-fsanitize=address,undefined','-fno-omit-frame-pointer'] if os.environ.get('UFI_SANITIZE') else []
    subprocess.run([compiler(), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
                    *flags,
                    '-D_CRT_SECURE_NO_WARNINGS', '-I', str(FW/'include'),
                    str(FW/'tests/corpus_decoder.c'), str(FW/'src/ufi_mfm.c'),
                    '-o', str(exe)], check=True)
    result = subprocess.run([str(exe), str(BUILD/'sector_test_360k.img'), str(binary)],
                            check=True, capture_output=True, text=True, timeout=60)
    print(result.stdout, end='')
    (BUILD/'corpus_result.json').write_text(json.dumps({'manifest': manifest, 'result': result.stdout.strip()}, indent=2))


if __name__ == '__main__':
    main()
