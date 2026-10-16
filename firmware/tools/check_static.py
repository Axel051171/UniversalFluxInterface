"""Clang path-sensitive analysis of HAL-independent production firmware modules."""
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ('ufi_capabilities', 'ufi_media', 'ufi_write_codec', 'ufi_write_wave',
           'ufi_compare', 'ufi_job', 'ufi_quality', 'ufi_mfm', 'ufi_verify', 'ufi_image', 'ufi_hfe_v3', 'ufi_raw_verify')


def main():
    compiler = shutil.which('clang')
    if not compiler:
        raise SystemExit('Static analysis requires clang on PATH')
    subprocess.run([compiler, '--analyze', '-Xanalyzer', '-analyzer-output=text',
                    '-Xanalyzer', '-analyzer-werror', '-I', str(ROOT/'firmware/include'),
                    *[str(ROOT/'firmware/src'/f'{name}.c') for name in SOURCES]],
                   cwd=ROOT, check=True, timeout=180)
    print(f'Static analysis passed: {len(SOURCES)} production modules; no hardware timing proof.')


if __name__ == '__main__':
    main()
