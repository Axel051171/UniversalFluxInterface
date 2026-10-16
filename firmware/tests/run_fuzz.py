"""Bounded coverage-guided production parser test. Corpus/crashes stay in build/."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
from run_tests import BUILD, FW, compiler

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--seconds', type=int, default=30)
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600: parser.error('seconds must be 1..3600')
    corpus = BUILD/'fuzz_corpus';corpus.mkdir(parents=True, exist_ok=True)
    for i, seed in enumerate(([0xf2,72,0x80,0xf1,0xf2,36,0x40,0xf3,7,1],
                              [0xf2,72,0xf4,0xf3,5,0xf4], [0xf2,72,0xf1,0x80,0xf1,0x80])):
        (corpus/f'seed{i}').write_bytes(bytes(int(f'{v:08b}'[::-1],2) for v in seed))
    exe = BUILD/('fuzz_parsers.exe' if sys.platform=='win32' else 'fuzz_parsers')
    sanitize = 'fuzzer,address' if sys.platform=='win32' else 'fuzzer,address,undefined'
    env = os.environ.copy()
    if sys.platform=='win32':
        resource = subprocess.check_output([compiler(),'-print-resource-dir'],text=True).strip()
        env['PATH'] = str(Path(resource)/'lib/windows') + os.pathsep + env.get('PATH','')
    subprocess.run([compiler(), '-std=c11', '-O1', '-g', f'-fsanitize={sanitize}',
                    '-fno-omit-frame-pointer', '-I', str(FW/'include'),
                    str(FW/'tests/fuzz_parsers.c'), str(FW/'src/ufi_hfe_v3.c'),
                    str(FW/'src/ufi_write_codec.c'), str(FW/'src/ufi_image.c'),
                    str(FW/'src/ufi_raw_verify.c'),
                    str(FW/'src/ufi_job.c'),str(FW/'src/ufi_resources.c'),
                    '-o', str(exe)], check=True)
    with (BUILD/'fuzz_result.log').open('w') as log:
        result = subprocess.run([str(exe),str(corpus),f'-max_total_time={args.seconds}',
                                 '-max_len=32767','-timeout=5','-seed=12345',
                                 f'-artifact_prefix={BUILD}/'],stdout=log,stderr=log,
                                timeout=args.seconds+45,env=env)
    print('\n'.join((BUILD/'fuzz_result.log').read_text().splitlines()[-15:]))
    result.check_returncode()

if __name__=='__main__':main()
