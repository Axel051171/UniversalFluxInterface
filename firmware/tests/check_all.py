"""Single offline software gate (install requirements-test.txt first).

Cross-build separately with CMake and the pinned STM32CubeH7 v1.11.0 package.
UFI_SANITIZE=1 enables native ASan/UBSan on supported hosts.
"""
import os
import atexit
import json
import time
from pathlib import Path
import subprocess
import sys
import argparse
from concurrent.futures import ThreadPoolExecutor

ROOT = Path(__file__).resolve().parents[2]
PY = sys.executable
RESULTS = []
REQUIRE_CORPORA = '--require-corpora' in sys.argv
BUILD=Path(os.environ.get('UFI_TEST_BUILD',str(ROOT/'firmware/tests/build')))
JOBS=1


def save_report():
    path = BUILD/'offline_gate.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(python=sys.version,compiler=os.environ.get('CC','clang'),
        require_corpora=REQUIRE_CORPORA,jobs=JOBS,checks=RESULTS),indent=2),encoding='utf-8')


def execute(args,isolated=False):
    started=time.monotonic()
    env=os.environ.copy()
    if isolated:
        target=BUILD/'parallel'/Path(args[0]).stem;target.mkdir(parents=True,exist_ok=True)
        env['UFI_TEST_BUILD']=str(target)
    try:
        result=subprocess.run([PY,*args],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                              text=True,timeout=180,env=env)
    except subprocess.TimeoutExpired as error:
        output=error.stdout or ''
        if isinstance(output,bytes):output=output.decode(errors='replace')
        result=subprocess.CompletedProcess(args,124,output+'\nTimed out after 180 seconds.\n')
    skipped='SKIP ' in result.stdout or 'NOT RUN:' in result.stdout
    return dict(command=list(args),status='failed' if result.returncode else 'skipped' if skipped else 'passed',
                seconds=round(time.monotonic()-started,3),output=result.stdout,returncode=result.returncode)


def publish(result):
    RESULTS.append(result);print(result['output'],end='',flush=True)


def run(*args):
    result=execute(args);publish(result)
    if result['returncode']:raise SystemExit(result['returncode'])


if __name__ == "__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--require-corpora',action='store_true')
    parser.add_argument('--jobs',type=int,default=min(4,os.cpu_count() or 1))
    args=parser.parse_args()
    if not 1<=args.jobs<=8:parser.error('--jobs must be between 1 and 8')
    JOBS=args.jobs
    atexit.register(save_report)
    os.environ["PYTHONPATH"] = str(ROOT / "software/ufi_host")
    independent=('run_tests','test_regressions','test_write_safety','test_gw','test_protocol_faults',
        'test_native_requests','test_sd_runtime','test_sd_export','test_memory','test_runtime',
        'test_quality_runtime','test_analysis_job','test_precision_runtime','test_capture_archive',
        'benchmark_quality','test_apple_gcr','test_tolerance','test_evidence_runtime','test_media_runtime',
        'test_drive_profiles','test_stream_runtime','test_motor_runtime','test_capture_integrity')
    started=time.monotonic()
    with ThreadPoolExecutor(max_workers=JOBS) as pool:
        pending=[pool.submit(execute,(f'firmware/tests/{name}.py',),True) for name in independent]
        for future in pending:publish(future.result())
    print(f'Independent groups: {len(independent)}, workers: {JOBS}, wall time: {time.monotonic()-started:.2f} s',flush=True)
    if any(result['returncode'] for result in RESULTS):raise SystemExit(1)
    corpus_options=() if REQUIRE_CORPORA else ('--if-present',)
    run("firmware/tests/test_flux_corpus.py", *corpus_options)
    run("firmware/tests/test_gcr_corpus.py", *corpus_options)
    run("firmware/tests/test_mac_corpus.py", *corpus_options)
    run("firmware/tests/test_hfe_corpus.py", *corpus_options)
    run("firmware/tests/test_hfe_v3_reference.py", *corpus_options)
    run("-m", "pytest", "software/ufi_host/tests", "-q", "-p", "no:cacheprovider")
    build = BUILD
    run("firmware/tools/asm6502.py", "firmware/tools/iec_nib.s", str(build / "iec_nib.h"))
    if (build / "iec_nib.h").read_text() != (ROOT / "firmware/include/iec_nib.h").read_text():
        raise SystemExit("generated drive code differs from firmware/include/iec_nib.h")
    run("firmware/tools/asm6502.py", "firmware/tools/iec_nib.s", str(build / "iec_nib.bin"))
    run("firmware/tools/test_iec_nib.py", str(build / "iec_nib.bin"))
    skipped=sum(r['status']=='skipped' for r in RESULTS)
    print(f"Offline software checks finished; {skipped} skipped. "
          + ("Full corpus release gate NOT satisfied. " if skipped else "All configured checks passed. ")
          + "Hardware qualification remains open.")
