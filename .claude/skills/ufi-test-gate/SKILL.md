---
name: ufi-test-gate
description: >
  Offline test gate of the UFI firmware that runs on a PC without the board: structure of
  firmware/tests (native C tests in run_tests.py, function-extraction regressions built on
  test_regressions.py, pinned corpus tests, host pytest suite, generated 6502 drive code check),
  the single entry point check_all.py with its offline_gate.json report, the environment switches
  UFI_SANITIZE / UFI_ANALYZE / UFI_COVERAGE / UFI_TEST_BUILD / CC, coverage.py, and how CI calls
  all of it. Use when a firmware or host change needs its tests run, when a native C test, a Python
  runtime or regression test or a corpus test has to be added, when a gate group fails, times out
  or is reported as skipped, or when coverage numbers are requested. Trigger phrases: "run the
  tests", "offline gate", "check_all", "add a native test", "regression test for this fix",
  "coverage", "Tests laufen lassen", "Testgate", "nativen Test hinzufuegen", "Regressionstest
  schreiben", "Korpustest", "Abdeckung messen", "Gate ist rot". DO NOT use for: LibFuzzer targets,
  generated driver stubs and check_static.py (ufi-fuzz-static-analysis), the ARM cross build,
  memory and stack budgets or flashing (ufi-firmware-build-flash), tests on the real board
  (ufi-bringup-diagnostics), protocol semantics of the commands under test (ufi-usb-protocol).
version: "1.0.0"
metadata:
  author: "Axel"
  tags: ["ufi", "firmware", "testing", "regression", "coverage"]
---

# UFI Offline Test Gate

## Purpose

Describes how the hardware-free software gate in `firmware/tests` is built, how to run all or part
of it, and how to add a test of each kind so that it really runs in the gate and in CI. The gate
compiles the production sources from `firmware/src` on the host; it never tests a model of them.

## Invariants and traps

- **A new test file is not run until it is registered.** Native C tests need an entry in `TESTS`
  in `firmware/tests/run_tests.py`; Python test scripts need their stem in the `independent` tuple
  in `firmware/tests/check_all.py`. Nothing is discovered by glob. (`benchmark_mfm_fastpaths.py`
  is deliberately outside the gate.)
- **Each gate group has a hard 180 s limit** (`execute()` in `check_all.py`, exit code 124). The
  whole of `run_tests.py` is one group and already needs about 95 s unsanitized on the owner's
  Windows machine. A slow new native test can push it over; measure before adding loops.
- **Skipped is not passed.** `check_all.py` marks a group `skipped` when its output contains
  `SKIP ` or `NOT RUN:`. The exit code stays 0, but the last line then reads "Full corpus release
  gate NOT satisfied". Never print those two strings from an ordinary test. A release claim needs
  `--require-corpora` with zero skipped.
- **No test may claim hardware behaviour.** The closing line always ends with "Hardware
  qualification remains open"; the doubles model control flow, not STM32 timing.
- **Extraction regressions depend on source layout.** `function(file, name)` in
  `test_regressions.py` cuts a production function out of `firmware/src/<file>` with a regex: the
  definition must start in column 0, the signature must not contain `;`, `{` or `}`, and the body
  ends at the first `}` in column 0. Renaming or re-indenting a production function breaks the
  test with "cannot extract production function" - fix the test call, do not paste a copy.
- **Native C tests see no HAL.** `run_tests.py` passes only `-I firmware/include -I firmware/tests`.
  A source that includes `ufi_firmware.h` / `stm32h7xx_hal.h` cannot be listed in `TESTS`; split the
  HAL-free logic into its own unit (pattern: `ufi_sense_calc.c` next to `ufi_sense.c`,
  `ufi_drive_extra.c`, `ufi_lan_cfg.c`) or use an extraction regression.
- **Warnings are errors** (`-std=c11 -Wall -Wextra -Werror`) for test and production sources alike,
  and CI builds with clang on Linux plus ASan/UBSan plus the analyzer. Clang on Windows accepts
  constructs Linux refuses (commit d10f44b: `static` definition after a non-static declaration), so
  a green local Windows gate is not proof; see Troubleshooting.
- **Parallel groups build in separate directories.** `check_all.py` sets `UFI_TEST_BUILD` to
  `firmware/tests/build/parallel/<script>` per group; every script must take its output directory
  from `run_tests.BUILD`, never from a literal path. Corpus fixtures stay in `firmware/tests/build/`.
- `firmware/tests/README.md` lists only a part of the native tests; `TESTS` in `run_tests.py` is
  the authoritative list.

## Structure

| Layer | Files | What runs |
|---|---|---|
| Native whole-unit tests | `test_*.c`, `check.h`, fixtures `*_fixture.h`, `w5500_fake.h`; table `TESTS` in `run_tests.py` | Test program + listed `firmware/src` units, built `-O1 -g`, executed; exit code 0 = pass |
| Extraction regressions | `test_regressions.py` (helpers `function`, `typedef`, `run`, `BASE`, `ERRORS`) and the scripts importing them: `test_*_runtime.py`, `test_gw.py`, `test_protocol_faults.py`, `test_native_requests.py`, `test_write_safety.py`, `test_psram_init.py`, ... | A generated C file in the build directory: doubles + production functions cut out verbatim + `main()` with `assert` |
| Corpus tests | `test_flux_corpus.py`, `test_gcr_corpus.py`, `test_hfe_v3_reference.py` (pinned downloads), `test_mac_corpus.py`, `test_hfe_corpus.py` (local archives via `--archive`), decoders `*_corpus_decoder.c`, `hfe_v3_reference.c`, manifests in `reference/` | Production decoders against external captures or an independent encoder |
| Host suite | `software/ufi_host/tests` (pytest) | Host package against fakes, no device |
| Drive code | `firmware/tools/asm6502.py`, `iec_nib.s`, `test_iec_nib.py` | Assembles the 1541 code, compares with `firmware/include/iec_nib.h` byte for byte, runs it in py65 |

Order inside `check_all.py`: the `independent` groups in a thread pool (`--jobs`, 1..8, default
min(4, CPUs)); a failure there stops the gate after all groups have reported. Then sequentially:
five corpus scripts, pytest, the drive code steps. Every result goes to
`firmware/tests/build/offline_gate.json` (`python`, `compiler`, `require_corpora`, `jobs`, and per
check `command`, `status` passed/failed/skipped, `seconds`, `output`, `returncode`), written at exit
even when the gate fails.

Environment switches (read in `run_tests.py`, honoured by every native build through
`extra_cflags()`):

| Variable | Effect |
|---|---|
| `CC` | Compiler; default clang, fallback gcc, cc |
| `UFI_SANITIZE=1` | `-fsanitize=address,undefined`; regression binaries get 120 s instead of 15 s |
| `UFI_ANALYZE=1` | `test_regressions.run()` first runs `clang --analyze -analyzer-werror` on each generated file |
| `UFI_COVERAGE=1` | clang source coverage instrumentation (set by `coverage.py`) |
| `UFI_TEST_BUILD` | Output directory instead of `firmware/tests/build` |

## Procedures

### Add a native C test for a HAL-free module

1. Write `firmware/tests/test_<topic>.c`. Include the production header, `check.h`, and end
   `main()` with `CHECK_SUMMARY("<topic>")`; use `CHECK(cond, "format", ...)` so that a failure
   prints file, line and values and the remaining checks still run. Plain `assert` is also in use
   (`test_sense.c`); builds have no `NDEBUG`.
2. Register it: `"test_<topic>": ["src/ufi_<module>.c", ...]` in `TESTS`. List every unit the
   module links against. `ufi_protection.c` and `ufi_metrics.c` are appended automatically
   whenever `src/ufi_quality.c` is listed.
3. Network code: use the register-level double `w5500_fake.h` (`fake_init`, `fake_dev`,
   `fake_udp_rx`, ...) instead of a new mock.
4. Run `python firmware/tests/run_tests.py <topic>` and once with `UFI_SANITIZE=1`.
5. Add a row to `firmware/tests/README.md` (German, table "Programm / Firmware-Quelle / Inhalt").

### Add a regression for HAL-bound control code

1. Create `firmware/tests/test_<topic>_runtime.py` (or add a function to an existing script):
   `from test_regressions import BASE, ERRORS, FW, typedef, function, run`.
2. Build the C text: `BASE + ERRORS` (stdint/stdbool/assert, `ufi_metrics.h`, `ufi_profile.h`,
   precision doubles, the real `ufi_error_t`), `typedef(FW/'include/<header>', '<type>')` for
   enums or structs needed verbatim, then `static` doubles for every callee and global, then
   `function('<file>.c', '<name>')` for each production function, then a `main()` with asserts
   and one closing `puts`.
3. Call `run('regression_<topic>', code, sources)`; `sources` is a tuple of whole units to link
   (`'src/ufi_mfm.c'`). To test a whole HAL-bound file, define its include guard and the HAL
   surface in the code and `#include "../src/<file>.c"` (pattern: `test_psram_init.py`).
4. Add the script stem to `independent` in `check_all.py`.
5. Run it directly, then with `CC=clang UFI_SANITIZE=1 UFI_ANALYZE=1`. The doubles themselves
   must be analyzer-clean (commit d10f44b: an unbounded loop over a 3-element test array was
   reported as a garbage read).
6. A bug fix gets its regression in the same change; name the fixed behaviour in the `puts` line.

### Add a corpus test

1. Pin the source: commit hash and SHA-256 of every file in a manifest under
   `firmware/tests/reference/` (fields as in `flux_corpus.json`: repository, commit, path, files,
   provenance, scope, license). Fixtures are downloaded to the ignored build directory and never
   committed; upstream code is never linked into firmware.
2. The script must support `--download` (fetch, verify digest, store), a plain run (missing
   fixture = error) and `--if-present` (print a line containing `NOT RUN:` or `SKIP ` and return).
   Verify the digest on every run, not only after download.
3. Convert to ticks in Python, decode with a small C program that links the production decoder
   (`corpus_decoder.c` + `src/ufi_mfm.c`), built with `*extra_cflags()`.
4. Add a `run("firmware/tests/<script>.py", *corpus_options)` line in `check_all.py`, the script
   to the list in `coverage.py`, and the `--download` call to the "Pinned external capture corpus"
   step of `.github/workflows/firmware.yml` (only when the fixture is freely downloadable).

## Examples

```bash
python -m pip install -r requirements-test.txt       # pytest, pyserial, py65 (pinned)
python firmware/tests/check_all.py                   # whole gate, corpora optional
python firmware/tests/check_all.py --require-corpora --jobs 4   # release gate, nothing may skip
python firmware/tests/run_tests.py codec lan         # native tests whose name contains a word
python firmware/tests/test_motor_runtime.py          # one regression script
python firmware/tests/test_flux_corpus.py --download # fetch + verify + run the pinned capture
python -m pytest software/ufi_host/tests -q -p no:cacheprovider
CC=clang UFI_SANITIZE=1 UFI_ANALYZE=1 python firmware/tests/check_all.py   # as CI runs it
python firmware/tests/coverage.py --no-fuzz          # coverage.txt, coverage.json, cov/html/
```

Skeleton of an extraction regression (shortened from `test_motor_runtime.py`):

```python
from test_regressions import BASE, ERRORS, FW, typedef, function, run

def main():
    code = BASE + ERRORS + typedef(FW/'include/ufi_firmware.h', 'drive_type_t') + r'''
static uint32_t now; static bool cancelled; static unsigned waits;
static uint32_t HAL_GetTick(void){return now;}
static bool ufi_delay_ms(unsigned ms){waits++;now+=cancelled?1:ms;return !cancelled;}
int ufi_drive_motor(bool on);
''' + function('ufi_drive.c', 'ufi_drive_motor') + r'''
int main(void){
    assert(!ufi_drive_motor(true) && waits==1 && now==500);
    cancelled=true;
    assert(ufi_drive_motor(true)==UFI_ERR_CANCELLED);
    puts("motor: spin-up and cancellation passed"); return 0;
}'''
    run('regression_motor_runtime', code)

if __name__ == '__main__': main()
```

Done criterion for a firmware or host change: `python firmware/tests/check_all.py` exits 0 and
prints "All configured checks passed." (or only corpus groups skipped), and the same command with
`CC=clang UFI_SANITIZE=1 UFI_ANALYZE=1` is green before pushing. CI job `offline-tests` in
`.github/workflows/firmware.yml` (ubuntu-24.04, Python 3.10) runs `check_static.py`, downloads the
three pinned corpora, runs `check_all.py` with those three variables, then `run_fuzz.py --seconds
30`. CI does not pass `--require-corpora`: the Mac and HxC corpora need local archives and are
skipped there.

## Coverage

`python firmware/tests/coverage.py [--require-corpora] [--no-fuzz]` needs clang with
`llvm-profdata` and `llvm-cov` beside it. It reruns `run_tests.py`, the five corpus scripts and
(without `--no-fuzz`) the corpus replays of the fuzz targets v2, gw, sd, drive with
`UFI_COVERAGE=1`, then writes `coverage.txt`, `coverage.json` (per file plus `not_measured`) and
`cov/html/` under `firmware/tests/build/`. Extraction regressions are deliberately not counted:
llvm-cov attributes them to the generated file, not to `firmware/src`. A module that appears under
`not_measured` has no whole-unit native build; that is a statement about measurement, not proof
of missing tests. There is no coverage threshold in the gate or in CI.

## Prerequisites

- Python 3.10 or newer with `requirements-test.txt` installed in the interpreter that starts
  `check_all.py` (it reuses `sys.executable` for every group and for pytest).
- clang (preferred) or gcc on PATH; coverage and `UFI_ANALYZE` need clang.
- Network access only for `--download`; everything else is offline.
- Run from any directory; the scripts resolve the repository root themselves.

## Limitations

- No timing, DMA, cache, USB PHY or analog behaviour is exercised; doubles return what the test
  scripts tell them.
- Extraction regressions test single functions with hand-written doubles; a changed callee contract
  is only caught if the double is updated with it.
- The corpus covers IBM MFM 360 KiB, a cc1541-generated 1541 GCR disk, one HFEv3 image and
  optional local MOOF/HxC archives; no real-media FM, Amiga or protection captures are claimed.
- Sanitizer support depends on the host toolchain; the reference environment for sanitized runs is
  Linux (CI or WSL) with clang.

## Troubleshooting

| Problem | Cause | Fix |
|---|---|---|
| pytest group fails in well under a second with "No module named pytest" | Gate started with an interpreter that lacks `requirements-test.txt` | Install the requirements into that interpreter or start the gate from the virtual environment |
| New test never appears in the gate output | Not registered in `TESTS` (C) or `independent` (Python) | Add the entry; check `offline_gate.json` for the command |
| "cannot extract production function file:name" | Production function renamed, made multi-line with braces in the signature, or not starting in column 0 | Adjust the `function()` call or restore the layout; never copy the function into the test |
| Group ends with "Timed out after 180 seconds." | Group limit in `check_all.execute()`; often `run_tests.py` under ASan on a slow machine | Lower `--jobs`, shrink the new test's loops, or split work into a separate registered script |
| `TimeoutExpired` from a regression binary after 15 s | Limit in `test_regressions.run()` for unsanitized runs | Reduce iterations; the 120 s limit applies only with `UFI_SANITIZE` |
| Local gate green, CI red with "static declaration follows non-static" or an analyzer report | Windows clang is more permissive; CI adds UBSan and the analyzer | Reproduce under WSL: `CC=clang UFI_SANITIZE=1 UFI_ANALYZE=1 python firmware/tests/check_all.py` |
| "generated drive code differs from firmware/include/iec_nib.h" | `firmware/tools/iec_nib.s` edited without regenerating the header | `python firmware/tools/asm6502.py firmware/tools/iec_nib.s firmware/include/iec_nib.h`, commit both |
| Gate exits 0 but reports "Full corpus release gate NOT satisfied" | One or more corpus fixtures absent | Run the three `--download` scripts; supply the Mac and HxC archives with `--archive`; use `--require-corpora` |
| "fixture digest mismatch" / "upstream digest mismatch" | Fixture in the build directory damaged or upstream changed | Delete the file under `firmware/tests/build/` and download again; never edit the pinned hash to make it pass |
| `llvm-cov not found next to ...` | clang without LLVM tools, or gcc selected | Install the matching LLVM tools; `llvm_tool()` also accepts versioned names such as `llvm-cov-18` |
| Undefined reference when linking a native test | A dependent unit is missing from the `TESTS` entry | Add the `src/...c` file; if it pulls the HAL, test through an extraction regression instead |
