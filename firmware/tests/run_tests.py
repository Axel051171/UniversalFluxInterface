"""Build and run the native firmware tests on the host.

The tests compile the real firmware sources that have no HAL dependency (the write
codec, the MFM codec) together with a small test program each; what runs is the
firmware code, not a model of it.

    python firmware/tests/run_tests.py            # all
    python firmware/tests/run_tests.py codec      # one (substring of the test name)

Needs clang or gcc on the PATH.  Objects and executables go to firmware/tests/build/.
"""
import pathlib
import shutil
import subprocess
import sys

HERE = pathlib.Path(__file__).resolve().parent
FW = HERE.parent
BUILD = HERE / "build"

# test program -> firmware sources it exercises
TESTS = {
    "test_write_codec": ["src/ufi_write_codec.c"],
    "test_write_verify": ["src/ufi_verify.c", "src/ufi_mfm.c", "src/ufi_write_codec.c"],
    "test_write_stream": ["src/ufi_write_stream.c"],
    "test_image": ["src/ufi_image.c"],
}


def compiler():
    for cc in ("clang", "gcc", "cc"):
        if shutil.which(cc):
            return cc
    sys.exit("no C compiler (clang or gcc) on the PATH")


def main(argv):
    cc = compiler()
    BUILD.mkdir(exist_ok=True)
    exe = ".exe" if sys.platform == "win32" else ""
    failed = []
    for name, sources in TESTS.items():
        if argv and not any(a in name for a in argv):
            continue
        out = BUILD / (name + exe)
        cmd = [cc, "-std=c11", "-O1", "-g", "-Wall", "-Wextra", "-Werror", "-D_CRT_SECURE_NO_WARNINGS",
               "-I", str(FW / "include"), "-I", str(HERE),
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
