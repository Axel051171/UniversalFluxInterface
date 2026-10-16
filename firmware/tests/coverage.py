"""Source coverage of the firmware code that native tests compile as whole translation
units: run_tests.py, the corpus decoders and the v2 dispatcher fuzzer replay.

Needs clang with llvm-profdata / llvm-cov beside it.  The regression programs that
extract single production functions are deliberately not measured: llvm-cov attributes
them to the generated file, not to firmware/src.  Output: firmware/tests/build/coverage.txt
(llvm-cov table), coverage.json (per file), cov/html/ (browsable).

usage: python firmware/tests/coverage.py [--require-corpora] [--no-fuzz]
"""
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path

from run_tests import BUILD, FW, compiler, llvm_tool

ROOT = FW.parent
COV = BUILD / 'cov'
EXE = '.exe' if sys.platform == 'win32' else ''


def main(argv):
    if 'clang' not in Path(compiler()).name:
        sys.exit('coverage needs clang (set CC=clang)')
    if COV.exists():
        shutil.rmtree(COV)
    COV.mkdir(parents=True)
    env = dict(os.environ, UFI_COVERAGE='1', LLVM_PROFILE_FILE=str(COV / '%m-%p.profraw'))
    started = time.time()
    corpus = [] if '--require-corpora' in argv else ['--if-present']
    scripts = [['run_tests.py']]
    scripts += [[s, *corpus] for s in ('test_flux_corpus.py', 'test_gcr_corpus.py', 'test_mac_corpus.py',
                                        'test_hfe_corpus.py', 'test_hfe_v3_reference.py')]
    if '--no-fuzz' not in argv:
        scripts += [['run_fuzz.py', '--target', t, '--replay'] for t in ('v2', 'gw')]
    for script in scripts:
        subprocess.run([sys.executable, str(FW / 'tests' / script[0]), *script[1:]],
                       cwd=ROOT, env=env, check=True, timeout=900)

    objects = sorted(p for p in BUILD.iterdir()
                     if p.is_file() and p.suffix == EXE and p.stat().st_mtime >= started
                     and os.access(p, os.X_OK))
    profiles = sorted(COV.glob('*.profraw'))
    if not objects or not profiles:
        sys.exit('no instrumented programs ran')
    profdata = COV / 'all.profdata'
    subprocess.run([llvm_tool('llvm-profdata'), 'merge', '-sparse', *map(str, profiles), '-o', str(profdata)],
                   check=True)
    obj_args = [str(objects[0])] + [a for o in objects[1:] for a in ('-object', str(o))]
    common = [*obj_args, f'-instr-profile={profdata}', '-ignore-filename-regex=(tests|hal_stub)[\\\\/]|\\.h$']
    table = subprocess.run([llvm_tool('llvm-cov'), 'report', *common], check=True,
                           capture_output=True, text=True).stdout
    (BUILD / 'coverage.txt').write_text(table, encoding='utf-8')
    export = json.loads(subprocess.run([llvm_tool('llvm-cov'), 'export', '-summary-only', *common],
                                       check=True, capture_output=True, text=True).stdout)
    subprocess.run([llvm_tool('llvm-cov'), 'show', '-format=html', f'-output-dir={COV / "html"}',
                    '-show-branches=count', *common], check=True, capture_output=True)

    per_file = {}
    for f in export['data'][0]['files']:
        name = Path(f['filename']).name
        s = f['summary']
        per_file[name] = {k: dict(count=s[k]['count'], covered=s[k]['covered'],
                                  percent=round(s[k]['percent'], 1))
                          for k in ('lines', 'regions', 'branches', 'functions') if k in s}
    never = sorted(p.name for p in (FW / 'src').glob('*.c') if p.name not in per_file)
    report = dict(programs=[o.name for o in objects], files=per_file, not_measured=never,
                  total={k: dict(count=v['count'], covered=v['covered'], percent=round(v['percent'], 1))
                         for k, v in export['data'][0]['totals'].items()})
    (BUILD / 'coverage.json').write_text(json.dumps(report, indent=2), encoding='utf-8')

    print(f'{"file":28s} {"lines":>14s} {"branches":>14s}')
    for name, s in sorted(per_file.items(), key=lambda kv: kv[1]['lines']['percent']):
        ln, br = s['lines'], s.get('branches', dict(covered=0, count=0, percent=0.0))
        print(f'{name:28s} {ln["covered"]:5d}/{ln["count"]:<5d} {ln["percent"]:5.1f}% '
              f'{br["covered"]:5d}/{br["count"]:<5d} {br["percent"]:5.1f}%')
    t = report['total']
    print(f'{"TOTAL":28s} {t["lines"]["covered"]:5d}/{t["lines"]["count"]:<5d} {t["lines"]["percent"]:5.1f}% '
          f'{t["branches"]["covered"]:5d}/{t["branches"]["count"]:<5d} {t["branches"]["percent"]:5.1f}%')
    print(f'{len(never)} of {len(list((FW / "src").glob("*.c")))} firmware/src files are not measured (no whole-unit '
          'native build; the function-extraction regressions do not count): ' + ', '.join(never))
    print(f'details: {BUILD / "coverage.txt"}, {BUILD / "coverage.json"}, {COV / "html" / "index.html"}')
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
