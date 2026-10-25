"""Build deterministic quality fixtures and retain their machine-readable metrics."""
import json
import os
import subprocess
from run_tests import BUILD, FW, compiler, extra_cflags


def main():
    BUILD.mkdir(exist_ok=True)
    binary = BUILD / ('benchmark_quality.exe' if os.name == 'nt' else 'benchmark_quality')
    subprocess.run([compiler(), '-std=c11', '-O1', '-Wall', '-Wextra', '-Werror',
                    *extra_cflags(), '-I', str(FW/'include'),
                    str(FW/'tests/benchmark_quality.c'), str(FW/'src/ufi_compare.c'),
                    str(FW/'src/ufi_raw_verify.c'), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], check=True, text=True, capture_output=True)
    report = json.loads(result.stdout)
    path = BUILD/'quality_benchmark.json'
    path.write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(f"Quality benchmark: {len(report['cases'])} deterministic fixtures passed; metrics: {path}")


if __name__ == '__main__':
    main()
