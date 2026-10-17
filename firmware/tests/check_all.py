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

ROOT = Path(__file__).resolve().parents[2]
PY = sys.executable
RESULTS = []
REQUIRE_CORPORA = '--require-corpora' in sys.argv


def save_report():
    path = ROOT/'firmware/tests/build/offline_gate.json'
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(dict(python=sys.version,compiler=os.environ.get('CC','clang'),
        require_corpora=REQUIRE_CORPORA,checks=RESULTS),indent=2),encoding='utf-8')


atexit.register(save_report)


def run(*args):
    started=time.monotonic()
    result=subprocess.run([PY,*args],cwd=ROOT,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,
                          text=True,timeout=180)
    print(result.stdout,end='',flush=True)
    skipped='SKIP ' in result.stdout or 'NOT RUN:' in result.stdout
    RESULTS.append(dict(command=list(args),status='failed' if result.returncode else 'skipped' if skipped else 'passed',
                        seconds=round(time.monotonic()-started,3),output=result.stdout))
    result.check_returncode()


if __name__ == "__main__":
    os.environ["PYTHONPATH"] = str(ROOT / "software/ufi_host")
    run("firmware/tests/run_tests.py")
    run("firmware/tests/test_regressions.py")
    run("firmware/tests/test_gw.py")
    run("firmware/tests/test_protocol_faults.py")
    run("firmware/tests/test_native_requests.py")
    run("firmware/tests/test_sd_runtime.py")
    run("firmware/tests/test_sd_export.py")
    run("firmware/tests/test_memory.py")
    run("firmware/tests/test_runtime.py")
    run("firmware/tests/test_quality_runtime.py")
    run("firmware/tests/test_analysis_job.py")
    run("firmware/tests/test_apple_gcr.py")
    run("firmware/tests/test_tolerance.py")
    run("firmware/tests/test_evidence_runtime.py")
    run("firmware/tests/test_media_runtime.py")
    run("firmware/tests/test_drive_profiles.py")
    corpus_options=() if REQUIRE_CORPORA else ('--if-present',)
    run("firmware/tests/test_flux_corpus.py", *corpus_options)
    run("firmware/tests/test_gcr_corpus.py", *corpus_options)
    run("firmware/tests/test_mac_corpus.py", *corpus_options)
    run("firmware/tests/test_hfe_corpus.py", *corpus_options)
    run("firmware/tests/test_hfe_v3_reference.py", *corpus_options)
    run("-m", "pytest", "software/ufi_host/tests", "-q", "-p", "no:cacheprovider")
    build = ROOT / "firmware/tests/build"
    run("firmware/tools/asm6502.py", "firmware/tools/iec_nib.s", str(build / "iec_nib.h"))
    if (build / "iec_nib.h").read_text() != (ROOT / "firmware/include/iec_nib.h").read_text():
        raise SystemExit("generated drive code differs from firmware/include/iec_nib.h")
    run("firmware/tools/asm6502.py", "firmware/tools/iec_nib.s", str(build / "iec_nib.bin"))
    run("firmware/tools/test_iec_nib.py", str(build / "iec_nib.bin"))
    skipped=sum(r['status']=='skipped' for r in RESULTS)
    print(f"Offline software checks finished; {skipped} skipped. "
          + ("Full corpus release gate NOT satisfied. " if skipped else "All configured checks passed. ")
          + "Hardware qualification remains open.")
