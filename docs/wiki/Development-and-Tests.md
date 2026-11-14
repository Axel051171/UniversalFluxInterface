# Development and tests

**Sprache / Language:** [Deutsch](Entwicklung-und-Tests) | [English](Development-and-Tests)

Software is checked on the host without a drive and also built for the STM32. Hardware timing and real electrical signals require separate measurements.

For custom host applications, start with the [USB developer guide](USB-Protocol). The [reference guide](USB-Protocol-Reference) indexes the complete German specification included in the wiki. Hex vectors and a PING/INFO example provide a starting point without drive commands.

## Repository

| Path | Contents |
|---|---|
| `firmware/src`, `firmware/include` | STM32 application and interfaces |
| `firmware/tests` | Native C tests, control-flow checks, models, corpora and fuzz harnesses |
| `firmware/tools` | Static analysis, memory/stack checks, 6502 assembler and utilities |
| `software/ufi_host` | Python host and tests |
| `kicad/UFI_Headless` | Schematic, layout and manufacturing files |
| `docs` | Protocol, evidence and measurement plans |
| `docs/wiki` | Publishable wiki pages |

## Offline gate

From the project root in a suitable Python environment:

```console
python -m pip install -r requirements-test.txt
python firmware/tests/check_all.py
```

The gate builds native programs from production code, runs firmware control-flow checks and host tests, and checks the 6502 drive code. It writes `offline_gate.json` to the test build directory.

Some checks extract production functions unchanged and substitute driver doubles. They test the relevant control flow, not a cycle-accurate STM32 simulation. Complement host fakes with tests across module boundaries.

## External corpora

```console
python firmware/tests/test_flux_corpus.py --download
python firmware/tests/test_gcr_corpus.py --download
python firmware/tests/test_hfe_v3_reference.py --download
```

Other corpora require suitable images supplied locally. `check_all.py --require-corpora` treats missing corpora as errors. Download revisions and checksums are pinned in the respective scripts.

## Static analysis and fuzzing

```console
python firmware/tools/check_static.py
python firmware/tests/run_fuzz.py --seconds 30
```

Portable analysis covers HAL-free modules and some HAL-dependent modules with stub headers. After an actual ARM build, this variant extends coverage:

```console
python firmware/tools/check_static.py --compile-commands firmware/build/compile_commands.json
```

Fuzzing uses sanitizers on suitable platforms. Short runs are smoke tests; longer runs and retained failure inputs improve coverage. Account for platform-dependent sanitizer availability when interpreting results.

## Memory and stack

The firmware build checks memory regions, individual stack frames, call chains and interrupt nesting. Indirect calls and assumed sizes of external functions limit the static call-chain model. Supplement it with runtime stack high-water measurements.

## Reviewing changes

For protocol changes, update firmware, host, masks, event buffers, ownership checks and the [protocol description](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md) together. Changes to the write path additionally require a matching measurement case in the qualification plan.

Sources: [test overview](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tests/README.md), [CI](https://github.com/Axel051171/UniversalFluxInterface/blob/main/.github/workflows/firmware.yml), [call-graph check](https://github.com/Axel051171/UniversalFluxInterface/blob/main/firmware/tools/check_callgraph.py).
