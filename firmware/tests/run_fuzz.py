"""Bounded coverage-guided tests of production code with libFuzzer.  Corpus and crash
artifacts stay in build/.

Targets:
  parsers  HFEv3, HFE header, fragmented compact write codec, raw-track profiles,
           job and resource transitions (fuzz_parsers.c)
  v2       the whole UFI v2 dispatcher behind generated driver stubs (fuzz_v2.c,
           fuzz_stubs.py, hal_stub/)

usage: python run_fuzz.py [--target parsers|v2|all] [--seconds N] [--replay]
--replay only executes the stored corpus once (used by coverage.py).
"""
import argparse
import os
from pathlib import Path
import subprocess
import sys

from run_tests import BUILD, FW, compiler, extra_cflags
import fuzz_stubs

HAL_FREE = ('ufi_job', 'ufi_resources', 'ufi_cancel', 'ufi_capabilities', 'ufi_media',
            'ufi_write_codec', 'ufi_quality')
COMMON = ['-std=c11', '-O1', '-g', '-fno-omit-frame-pointer', '-D_CRT_SECURE_NO_WARNINGS',
          '-I', str(FW / 'include'), '-I', str(FW / 'tests')]
EXE = '.exe' if sys.platform == 'win32' else ''


def bits(values):
    return bytes(int(f'{v:08b}'[::-1], 2) for v in values)


def record(flags, cmd, seq, payload=b''):
    return bytes([flags, cmd, seq, len(payload) & 255, len(payload) >> 8]) + payload


V2_SEEDS = (
    b'\x00' + record(4, 0x00, 1, b'ping') + record(4, 0x01, 2) + record(4, 0x02, 3) + record(4, 0x09, 4),
    b'\x00' + record(4, 0x0F, 1, b'\x00') + record(4, 0x0F, 2, b'\x14') + record(4, 0x0C, 3) + record(4, 0x0D, 4),
    b'\x00' + record(4, 0x20, 1, bytes([0, 0, 2, 0, 0, 0])) + record(0x08, 0, 0) + record(4, 0x0E, 2),
    b'\x00' + record(4, 0x22, 1, bytes([0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0])) + record(5, 0x22, 1, b'\xf2\x48\xf1\x80\xf1\x80')
    + record(0x18, 0, 0),
    b'\x00' + record(4, 0x50, 1, b'\x00\x00') + record(4, 0x51, 2, b'\x00\x00\x00\x00\x10\x00UFI.CFG')
    + record(4, 0x33, 3, b'\x01\x00\x03\x05UI') + record(4, 0x36, 4, b'\x01\x08\x01\x00\x03\x05'),
    b'\x00' + record(4, 0x60, 1, b'\x01\x50\x02\x03\x04') + record(4, 0x61, 2) + record(4, 0x62, 3)
    + record(0x24, 0x82, 0, b'\x01\x02') + record(4, 0x06, 4, b'\xff\xff\x00\x00'),
    b'\x01\x55\xaa\x01\x00\x07\x00\x00\x00\x12\x34\x55\xaa',
    # integrity job around a READ: open (kind 1, id 0x1234), READ, end, status
    b'\x00' + record(4, 0x0B, 1, bytes([1, 1, 0x34, 0x12, 0, 0, 0, 0, 0, 0])) + record(4, 0x20, 2, bytes([5, 1, 3, 0, 0, 0]))
    + record(0x0C, 0, 0) + record(4, 0x0B, 3),
    # integrity job around a WRITE: open (kind 2), WRITE, DATA with 16-byte job header, commit, end
    b'\x00' + record(4, 0x0B, 1, bytes([1, 2, 0x78, 0x56, 0, 0, 0, 0, 0, 0])) + record(4, 0x22, 2, bytes([0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0]))
    + record(5, 0x22, 2, bytes([0x78, 0x56, 0, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0]) + b'\xf2\x48\xf1\x80\xf1\x80')
    + record(4, 0x0B, 3, bytes([2, 0, 0, 0, 0])) + record(0x1C, 0, 0),
    # guarded write after one configuration change (generation 1), IEC block write with 256 data bytes, clear comms
    b'\x00' + record(4, 0x10, 9, b'\x01') + record(4, 0x27, 1, bytes([1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 6, 0, 0, 0]))
    + record(4, 0x35, 2, bytes([1, 18, 0, 8]) + bytes(256)) + record(0x40, 0, 0) + record(4, 0x00, 3),
    b'\x02' + record(4, 0x10, 1, b'\x01') + record(4, 0x20, 2, bytes([0, 0, 1, 0, 0, 0])) + record(4, 0x0A, 3, b'\x01'),
)
PARSER_SEEDS = (bits([0xf2, 72, 0x80, 0xf1, 0xf2, 36, 0x40, 0xf3, 7, 1]),
                bits([0xf2, 72, 0xf4, 0xf3, 5, 0xf4]), bits([0xf2, 72, 0xf1, 0x80, 0xf1, 0x80]))


def fuzzer_env():
    env = os.environ.copy()
    if sys.platform == 'win32':
        resource = subprocess.check_output([compiler(), '-print-resource-dir'], text=True).strip()
        env['PATH'] = str(Path(resource) / 'lib/windows') + os.pathsep + env.get('PATH', '')
    return env


def sanitizer_flags():
    sanitize = 'fuzzer,address' if sys.platform == 'win32' else 'fuzzer,address,undefined'
    return [f'-fsanitize={sanitize}'] + [f for f in extra_cflags() if not f.startswith('-fsanitize')]


def build_parsers():
    exe = BUILD / ('fuzz_parsers' + EXE)
    subprocess.run([compiler(), *COMMON, *sanitizer_flags(), str(FW / 'tests/fuzz_parsers.c'),
                    *[str(FW / 'src' / f) for f in ('ufi_hfe_v3.c', 'ufi_write_codec.c', 'ufi_image.c',
                                                      'ufi_raw_verify.c', 'ufi_job.c', 'ufi_resources.c')],
                    '-o', str(exe)], check=True)
    return exe


def build_v2():
    objdir = BUILD / 'fuzz_v2_obj'
    objdir.mkdir(parents=True, exist_ok=True)
    flags = [*COMMON, *sanitizer_flags(), '-I', str(FW / 'tests/hal_stub'), '-D__packed=__attribute__((packed))']
    objects, harness = [], objdir / 'fuzz_v2.o'
    for name in ('ufi_v2', *HAL_FREE):
        o = objdir / (name + '.o')
        subprocess.run([compiler(), *flags, '-w', '-c', str(FW / 'src' / (name + '.c')), '-o', str(o)], check=True)
        objects.append(o)
    subprocess.run([compiler(), *flags, '-Wall', '-Wextra', '-Werror', '-c', str(FW / 'tests/fuzz_v2.c'),
                    '-o', str(harness)], check=True)
    stubs = objdir / 'fuzz_v2_stubs.c'
    extra = {'g_capture': ('ufi_firmware.h', 'capture_context_t g_capture;')}
    extra.update({s: (None, f'uint32_t {s};') for s in ('_estack', '_edtcm', '__heap_start', '__heap_end')})
    stubbed, skipped = fuzz_stubs.generate(objects, stubs, harness_objects=[harness], extra=extra)
    libc = {'memcpy', 'memmove', 'memset', 'memcmp', 'strlen', 'strncpy', 'strcmp', 'strncmp', 'snprintf',
            'strchr', 'strrchr', 'strstr', 'strcpy', 'strcat', 'strncat', 'abs', 'rand', 'malloc', 'free',
            '__chkstk', '__stack_chk_fail', '__stack_chk_guard', 'assert', '_assert', '__assert_fail',
            '_wassert', 'abort', 'puts', 'printf', 'fprintf', 'vsnprintf', '__security_cookie',
            '__security_check_cookie', '__GSHandlerCheck', '__C_specific_handler'}
    unknown = [s for s in skipped if s not in libc and not s.startswith(('__asan', '__ubsan', '__sanitizer', '__sancov', '___'))]
    if unknown:
        raise SystemExit('fuzz_v2: no prototype found for ' + ', '.join(unknown))
    print(f'fuzz_v2: {len(stubbed)} generated stubs, {len(HAL_FREE)} real modules behind ufi_v2.c')
    exe = BUILD / ('fuzz_v2' + EXE)
    subprocess.run([compiler(), *flags, '-std=c23', '-w', str(stubs), str(harness), *map(str, objects),
                    '-o', str(exe)], check=True)
    return exe


TARGETS = {'parsers': (build_parsers, PARSER_SEEDS, 'fuzz_corpus', 32767),
           'v2': (build_v2, V2_SEEDS, 'fuzz_v2_corpus', 8192)}


def run_target(name, seconds, replay):
    build, seeds, corpus_name, max_len = TARGETS[name]
    corpus = BUILD / corpus_name
    corpus.mkdir(parents=True, exist_ok=True)
    for i, seed in enumerate(seeds):
        (corpus / f'seed{i}').write_bytes(seed)
    exe = build()
    log = BUILD / f'{name}_fuzz_result.log'
    args = [str(exe), str(corpus), f'-max_len={max_len}', '-timeout=5', '-seed=12345',
            f'-artifact_prefix={BUILD}/{name}-']
    args += ['-runs=0'] if replay else [f'-max_total_time={seconds}']
    with log.open('w') as out:
        result = subprocess.run(args, stdout=out, stderr=out, timeout=seconds + 120, env=fuzzer_env())
    print('\n'.join(log.read_text(errors='replace').splitlines()[-12:]))
    result.check_returncode()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--target', choices=('parsers', 'v2', 'all'), default='all')
    parser.add_argument('--seconds', type=int, default=30)
    parser.add_argument('--replay', action='store_true', help='execute the stored corpus once, no mutation')
    args = parser.parse_args()
    if not 1 <= args.seconds <= 3600:
        parser.error('seconds must be 1..3600')
    for name in (('parsers', 'v2') if args.target == 'all' else (args.target,)):
        run_target(name, args.seconds, args.replay)


if __name__ == '__main__':
    main()
