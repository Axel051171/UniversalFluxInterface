"""Clang path-sensitive analysis of the production firmware modules.

Two groups: HAL-independent modules exactly as the firmware builds them, and every
other ufi_*.c module compiled against firmware/tests/hal_stub (HAL declarations only).
Modules that need the real HAL / USB / FatFs headers are listed in NEEDS_REAL_HAL and
stay outside the portable check. --compile-commands uses the actual ARM headers
and build definitions to cover every project C translation unit, including USB
and FatFs glue. Nothing here proves hardware timing.
"""
from pathlib import Path
import argparse
import json
import os
import shlex
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'firmware/src'
PURE = ('ufi_capabilities', 'ufi_media', 'ufi_write_codec', 'ufi_write_wave',
        'ufi_compare', 'ufi_job', 'ufi_quality', 'ufi_protection', 'ufi_mfm', 'ufi_verify', 'ufi_image', 'ufi_hfe_v3', 'ufi_raw_verify',
        'ufi_sha256', 'ufi_w5500', 'ufi_lan_cfg', 'ufi_net')
NEEDS_REAL_HAL = {'ufi_board', 'ufi_clock', 'ufi_composite', 'ufi_crash', 'ufi_debug', 'ufi_diag',
                  'ufi_flux', 'ufi_main', 'ufi_msc', 'ufi_power', 'ufi_psram', 'ufi_sd', 'ufi_selftest',
                  'ufi_sense', 'ufi_sync', 'ufi_usb', 'ufi_write',
                  'ufi_lan'}   # sense/sync: HAL I2C, RCC, TIM8 (board v1.0, A10); lan: HAL SPI3 (W5500 glue)
STUB_FLAGS = ['-I', str(ROOT / 'firmware/tests/hal_stub'), '-D__packed=__attribute__((packed))',
              '-DUFI_FW_VERSION="analysis"', '-DUFI_GIT_REV="analysis"']


def analyze(compiler, sources, extra=()):
    subprocess.run([compiler, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                    '-Xanalyzer', '-analyzer-werror', '-I', str(ROOT / 'firmware/include'), *extra,
                    *[str(SRC / f'{name}.c') for name in sources]],
                   cwd=ROOT, check=True, timeout=600)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compile-commands',type=Path)
    args=parser.parse_args()
    compiler = shutil.which('clang')
    if not compiler:
        raise SystemExit('Static analysis requires clang on PATH')
    if args.compile_commands:
        gcc=shutil.which('arm-none-eabi-gcc')
        if not gcc:raise SystemExit('ARM header analysis requires arm-none-eabi-gcc on PATH')
        newlib=Path(gcc).resolve().parent.parent/'arm-none-eabi/include'
        if not (newlib/'stdlib.h').is_file():
            search=subprocess.run([gcc,'-E','-x','c','-','-v'],input='',capture_output=True,text=True,check=True,timeout=30)
            # only the indented lines of the "#include <...> search starts here" list are paths;
            # the "Configured with: ..." line is longer than a file name may be (ENAMETOOLONG on Linux)
            def has_stdlib(line):
                try:
                    return (Path(line.strip())/'stdlib.h').is_file()
                except OSError:
                    return False
            newlib=next((Path(line.strip()) for line in search.stderr.splitlines()
                         if line.startswith(' ') and len(line)<1024 and has_stdlib(line)),newlib)
        if not (newlib/'stdlib.h').is_file():raise SystemExit(f'Newlib headers missing: {newlib}')
        entries=json.loads(args.compile_commands.read_text(encoding='utf-8'))
        checked=0
        for entry in entries:
            source=Path(entry['file']).resolve()
            if source.parent!=SRC.resolve() or source.suffix!='.c':continue
            command=entry.get('arguments')
            if command is None:
                command=shlex.split(entry['command'],posix=os.name!='nt')
            command=[arg[1:-1] if arg.startswith('"') and arg.endswith('"') else arg for arg in command]
            flags=[arg.replace('\\"','"') for arg in command if arg.startswith(('-I','-D'))]
            subprocess.run([compiler,'--target=arm-none-eabi','-mcpu=cortex-m7','-mthumb',
                '-mfpu=fpv5-d16','-mfloat-abi=hard','-isystem',str(newlib),*flags,
                '--analyze','-Xanalyzer','-analyzer-output=text','-Xanalyzer','-analyzer-werror',
                '-o',os.devnull,str(source)],cwd=entry['directory'],check=True,timeout=180)
            checked+=1
        expected=len(list(SRC.glob('*.c')))
        if checked!=expected:raise SystemExit(f'Incomplete ARM analysis: {checked}/{expected} project modules')
        print(f'Static ARM analysis passed: all {checked} project modules with actual Cube/USB/FatFs headers; '
              'vendor implementations and physical timing are not covered.')
        return
    stubbed = sorted(p.stem for p in SRC.glob('ufi_*.c') if p.stem not in PURE and p.stem not in NEEDS_REAL_HAL)
    analyze(compiler, PURE)
    analyze(compiler, stubbed, STUB_FLAGS)
    print(f'Static analysis passed: {len(PURE)} HAL-independent modules, {len(stubbed)} modules against '
          f'the HAL stubs, {len(NEEDS_REAL_HAL)} not covered; no hardware timing proof.')


if __name__ == '__main__':
    main()
