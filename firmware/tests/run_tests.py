"""Build and run the native firmware tests on the host.

The tests compile the real firmware sources that have no HAL dependency (the write
codec, the MFM codec) together with a small test program each; what runs is the
firmware code, not a model of it.

    python firmware/tests/run_tests.py            # all
    python firmware/tests/run_tests.py codec      # one (substring of the test name)

Needs clang or gcc on the PATH.  Objects and executables go to firmware/tests/build/.
"""
import pathlib
import os
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
FW = HERE.parent
BUILD = pathlib.Path(os.environ.get('UFI_TEST_BUILD',str(HERE/'build')))

# test program -> firmware sources it exercises
TESTS = {
    "test_mfm_fastpaths": [],
    "test_cycle_profile": [],
    "test_metrics": ["src/ufi_metrics.c"],
    "test_fingerprint": ["src/ufi_verify.c","src/ufi_mfm.c"],
    "test_protection": ["src/ufi_mfm.c","src/ufi_verify.c","src/ufi_quality.c","src/ufi_protection.c","src/ufi_compare.c"],
    "test_sector_details": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_raw_verify": ["src/ufi_raw_verify.c"],
    "test_hfe_v3": ["src/ufi_hfe_v3.c"],
    "test_capabilities": ["src/ufi_capabilities.c"],
    "test_recognition": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_local": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_mac": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_media_check": ["src/ufi_media.c"],
    "test_compare": ["src/ufi_compare.c"],
    "test_evidence": ["src/ufi_evidence.c", "src/ufi_drive_extra.c"],
    "test_gcr": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_legacy": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_quality": ["src/ufi_mfm.c", "src/ufi_verify.c", "src/ufi_quality.c"],
    "test_amiga": ["src/ufi_mfm.c", "src/ufi_verify.c"],
    "test_job": ["src/ufi_job.c", "src/ufi_resources.c"],
    "test_write_wave": ["src/ufi_write_wave.c"],
    "test_write_codec": ["src/ufi_write_codec.c"],
    "test_write_verify": ["src/ufi_verify.c", "src/ufi_mfm.c", "src/ufi_write_codec.c"],
    "test_write_stream": ["src/ufi_write_stream.c"],
    "test_image": ["src/ufi_image.c"],
    "test_sense": ["src/ufi_sense_calc.c"],
}


def compiler():
    for cc in (os.environ.get("CC", "clang"), "gcc", "cc"):
        if shutil.which(cc):
            return cc
    sys.exit("no C compiler (clang or gcc) on the PATH")


def llvm_tool(name):
    """llvm-nm / llvm-profdata / llvm-cov from the same installation as the compiler."""
    cc = pathlib.Path(shutil.which(compiler()))
    beside = cc.with_name(name + cc.suffix)
    found = str(beside) if beside.exists() else shutil.which(name)
    if not found:
        sys.exit(f"{name} not found next to {cc}")
    return found


def extra_cflags():
    """Flags every native build honours: UFI_SANITIZE=1 (ASan/UBSan) and
    UFI_COVERAGE=1 (clang source coverage, profiles go where LLVM_PROFILE_FILE says)."""
    flags = []
    if os.environ.get("UFI_SANITIZE"):
        flags += ["-fsanitize=address,undefined", "-fno-omit-frame-pointer"]
    if os.environ.get("UFI_COVERAGE"):
        flags += ["-fprofile-instr-generate", "-fcoverage-mapping"]
    return flags


def main(argv):
    cc = compiler()
    BUILD.mkdir(parents=True,exist_ok=True)
    exe = ".exe" if sys.platform == "win32" else ""
    failed = []
    for name, sources in TESTS.items():
        if 'src/ufi_quality.c' in sources and 'src/ufi_protection.c' not in sources:
            sources=[*sources,'src/ufi_protection.c']
        if 'src/ufi_quality.c' in sources and 'src/ufi_metrics.c' not in sources:
            sources=[*sources,'src/ufi_metrics.c']
        if argv and not any(a in name for a in argv):
            continue
        out = BUILD / (name + exe)
        cmd = [cc, *extra_cflags(), "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror",
               "-D_CRT_SECURE_NO_WARNINGS", "-I", str(FW / "include"), "-I", str(HERE),
               str(HERE / (name + ".c"))] + [str(FW / s) for s in sources] + ["-o", str(out)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        if r.returncode:
            print(f"{name}: build failed\n{r.stdout}{r.stderr}")
            failed.append(name)
            continue
        r = subprocess.run([str(out)], capture_output=True, text=True)
        sys.stdout.write(r.stdout)
        if r.returncode:
            sys.stdout.write(r.stderr)
            failed.append(name)
    if failed:
        print("FAILED:", ", ".join(failed))
        return 1
    print("all native tests passed")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
