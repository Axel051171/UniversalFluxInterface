"""Compare exact old/new primitives on the host; not a Cortex-M7 speed claim."""
import argparse
import json
import platform
import subprocess
from run_tests import BUILD, FW, compiler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--levels', nargs='+', choices=('2', '3', 's'), default=('2', '3', 's'))
    args = parser.parse_args()
    output = BUILD/'cpu_benchmark'
    output.mkdir(parents=True, exist_ok=True)
    cc = compiler()
    reports = []
    for level in args.levels:
        binary = output/f'mfm_O{level}{".exe" if platform.system() == "Windows" else ""}'
        subprocess.run([cc, '-std=c11', f'-O{level}', '-Wall', '-Wextra', '-Werror',
                        '-DUFI_BENCHMARK', '-I', str(FW/'include'),
                        str(FW/'tests/test_mfm_fastpaths.c'), '-o', str(binary)], check=True)
        result = subprocess.run([str(binary)], capture_output=True, text=True, check=True, timeout=180)
        report = json.loads(result.stdout)
        report.update(optimization=f'-O{level}', compiler=cc)
        reports.append(report)
        print(json.dumps(report), flush=True)
    path = output/'mfm_fastpaths.json'
    path.write_text(json.dumps(dict(schema=1, host=platform.platform(),
                                   scope='Host microbenchmark; no MCU throughput or interrupt timing claim',
                                   profiles=reports), indent=2)+'\n', encoding='utf-8')
    print(f'Report: {path}')


if __name__ == '__main__':
    main()
