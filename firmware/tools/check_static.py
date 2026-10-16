"""Clang path-sensitive analysis of the production firmware modules.

Two groups: HAL-independent modules exactly as the firmware builds them, and every
other ufi_*.c module compiled against firmware/tests/hal_stub (HAL declarations only).
Modules that need the real HAL / USB / FatFs headers are listed in NEEDS_REAL_HAL and
stay outside this check.  Nothing here proves hardware timing.
"""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SRC = ROOT / 'firmware/src'
PURE = ('ufi_capabilities', 'ufi_media', 'ufi_write_codec', 'ufi_write_wave',
        'ufi_compare', 'ufi_job', 'ufi_quality', 'ufi_mfm', 'ufi_verify', 'ufi_image', 'ufi_hfe_v3', 'ufi_raw_verify')
NEEDS_REAL_HAL = {'ufi_board', 'ufi_clock', 'ufi_composite', 'ufi_crash', 'ufi_debug', 'ufi_diag', 'ufi_dump',
                  'ufi_flux', 'ufi_main', 'ufi_msc', 'ufi_power', 'ufi_psram', 'ufi_sd', 'ufi_selftest',
                  'ufi_usb', 'ufi_write'}
STUB_FLAGS = ['-I', str(ROOT / 'firmware/tests/hal_stub'), '-D__packed=__attribute__((packed))',
              '-DUFI_FW_VERSION="analysis"', '-DUFI_GIT_REV="analysis"']


def analyze(compiler, sources, extra=()):
    subprocess.run([compiler, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                    '-Xanalyzer', '-analyzer-werror', '-I', str(ROOT / 'firmware/include'), *extra,
                    *[str(SRC / f'{name}.c') for name in sources]],
                   cwd=ROOT, check=True, timeout=600)


def main():
    compiler = shutil.which('clang')
    if not compiler:
        raise SystemExit('Static analysis requires clang on PATH')
    stubbed = sorted(p.stem for p in SRC.glob('ufi_*.c') if p.stem not in PURE and p.stem not in NEEDS_REAL_HAL)
    analyze(compiler, PURE)
    analyze(compiler, stubbed, STUB_FLAGS)
    print(f'Static analysis passed: {len(PURE)} HAL-independent modules, {len(stubbed)} modules against '
          f'the HAL stubs, {len(NEEDS_REAL_HAL)} not covered; no hardware timing proof.')


if __name__ == '__main__':
    main()
