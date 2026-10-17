# UFI implementation and evidence

Target: the existing STM32H723 headless board, without physical hardware testing.
Software results do not establish electrical, pulse timing or media reliability.

## Delivered in 1.18-dev (2026-10-13)

- A: one offline gate (`python firmware/tests/check_all.py`), pinned Python test
  dependencies, generated 6502 header consistency check, CI host/native/6502 job.
  CI requests ASan/UBSan; the GitHub runner has not been executed in this session.
- B: armed and cleared write INDEX interrupts; main-loop error reporting rather
  than blocking CDC from USB IRQ; bounded GET_INFO strings; v1 argument lengths;
  conservative exclusion of conflicting drive/IEC/file/power operations.
  Both MSC personalities poll USB in the main loop. Failed floppy writeback keeps
  dirty cache and the current personality. The physical switch retries once per
  second; mode notification occurs only after exit preconditions succeed.
- Capture DMA now reserves a complete fallback chunk inside the backing store.
  Previously 256 scratch words could receive up to 32768 DMA words. Usable fallback
  capture capacity is now 38224 words; with 8 MiB PSRAM it is 2064384 words.
  Buffered revolution finalisation is capped at the actual descriptor capacity.
- C foundation: addressed IEC memory/DOS commands and structured DOS read status;
  corrected full-disk geometry defaults for 1571; explicit 1541/C/II profiles;
  checkpointed D64/D71/D81 reads with per-sector attempt history and SHA-256.
  Transport failures and DOS media errors are distinct. Failed sector writes now
  produce a nonzero CLI exit status; source error maps are checked before writing.
- GCR: correct byte offsets when overlap sync runs differ in length, rejection of
  short ambiguous wrap-around, DOS cleanup after read failures, append-only fragment
  evidence from `iec-nib`. G64 refuses missing tracks, raw-only fragments and
  oversized tracks instead of silently manufacturing/truncating a track.
- D foundation: native `.ufi` SQLite archive commits CRC-validated v2 frames before
  decoding, then stores full-resolution uint32 timestamps and metadata. SHA-256
  integrity checking works offline. Failed/interrupted attempts remain identifiable.
- SCP: host bounds/checksum/resolution checks and third-party creator flag; host
  and standalone writer refuse exact 65536-unit transition gaps that the existing
  encoding cannot represent. The SD writer rejects unsupported resolution, cell
  width and extended headers; checks the checksum, file bounds and timestamps;
  avoids multiplication overflow in its input chunk size and refuses truncation.
- Host transport: completes partial serial writes, bounds waits even with stale
  frames, rejects CRC errors during flux capture, truncated stream tokens, zero
  intervals, invalid index order and incomplete successful END counts.

## Eight improvements implemented, 2026-10-13

1. Extended flux writer: TIM3 PWM2/DMA burst, 220..UINT32_MAX native tick intervals,
   silent segments for long gaps, corrected first falling edge and silent tail.
   GW uses it automatically, native WRITE opts in with bit5. Short ASTABLE periods
   are representable. Total duration is bounded to about 58 seconds. Extended
   native writes use host timing without firmware precompensation, scaling,
   clamping, Apple support or firmware-integrated verification. The host now
   automates separate IBM-MFM and Amiga-DD/HD sector readback when --extended --verify is selected.
2. Composite mode4: GW CDC interfaces0/1 plus native v2 Bulk interface2, separate
   endpoints, Microsoft OS1 WinUSB descriptors, serial-number selection and
   PyUSB transport. Development PID1209:4F58 requires allocation review before
   publication. No physical enumeration has been tested. Legacy modes remain.
3. Resource ownership: shared drive/timer/DMA group, native/GW exclusion, explicit
   acquire/release and disconnect cleanup. Conservative feature-state query
   reports implementation/configuration/readiness separately. Native ownership now
   expires after 30 seconds of idle time; active work is protected.
4. Cancellation and bounded waits: native EP0 cancel, interface-local ClearComms,
   disconnect cleanup, upload idle timeout and bounded physical write duration.
   IEC waits and precise delays cooperate with cancellation; this is not yet a
   completely nonblocking scheduler. Existing progress plus job counters remain.
5. Native integrity jobs: 64-bit ID, DATA packet number/byte offset, CRC32 over the
   whole encoded stream, explicit verified WRITE COMMIT, checked END proof and
   queryable last job. No persistent flux resume or exactly-once media guarantee.
6. Extended 64-bit TIM2 timebase with pending-overflow handling, full-u32 GW
   ticks/linger, SPACE during long silent reads and microsecond pre/post delays.
7. IEC ROM-family identification and optional ROM SHA256; unknown ROMs stay
   unknown. Resumable write journals validate source/device/geometry and every
   previously verified sector before proceeding. Each new sector is read back
   byte-for-byte, retries are bounded, write protection is not retried.
8. Additional software evidence: real parser random-input fuzzing (100000 bytes),
   composite descriptors, actual clock-extension function with register doubles,
   pure writer/job/resource modules and independent host wire oracle. Tests cover
   malformed/duplicate/missing job packets, commit CRC and IEC resume/media change.

GW command/codec tests use official host functions pinned to commit
`26690f89967d519e0106ab9566019a026b920bb4`. This is not a complete GW conformance
suite. Exact wire layouts and constraints are in USB_Protokoll.md section4.

## Reliability follow-up: priorities 1–4

- Composite native replies now use two owned buffers and a nonblocking send queue.
  Parser backpressure preserves reply order relative to streams/events. IN timeout
  deactivates/reopens only the native endpoint and requires interface-local recovery;
  the host also clears endpoint83 halt/toggle state. Legacy UFI CDC remains synchronous.
- IEC_RESET in v2 uses 20-ms reset and 500-ms boot states; cancellation releases
  the bus. Legacy reset remains a synchronous wrapper. Readiness delays feed the
  watchdog and accept cancellation; timing-sensitive IEC byte operations still run
  synchronously. This is not a full asynchronous IEC controller.
- Native idle ownership expires after 30 seconds; active jobs, physical operations,
  pending responses and received commands protect ownership. Incomplete WRITE
  uploads time out after 10 seconds even without integrity jobs. Status polls do
  not renew the upload-progress deadline.
- --extended --verify decodes CRC-valid IBM MFM source sectors before writing,
  reads two revolutions afterwards and compares marks and bytes. No supported
  sectors means refusal before WRITE. Invalid CRC, missing sectors, conflicting
  duplicates and modified data do not produce a successful content result.
  Amiga-DD/HD now has complete-track host verification including labels; GCR/protection structures are not claimed as verified.
- DIAGNOSTICS0D / INFO bit20 / ufi diagnostics expose usable store, sampled backlog
  and peak, read/write DMA faults, underruns, USB timeouts/pauses, lease releases
  and static memory reserves. Counters are software observations, not bus tracing.
- New regression doubles execute actual USB queue and IEC reset functions. Host
  readback uses an independent test encoder with CRC faults, changed data, jitter
  and sector sizes128..8192. Endpoint re-enumeration still requires real OS testing.

## Evidence and limits

Local environment: Windows, Python 3.10 virtual environment, native Clang,
ARM GNU 14.2.1, STM32CubeH7 v1.11.0. Release build succeeds; project sources produce
no compiler warnings. The clean vendor build reports the existing FatFs
`f_setlabel` misleading-indentation warning and CDC/MSC class initializer
warnings after enabling user string descriptors; project sources compile cleanly.

Clang static analysis of `ufi_image.c`, `ufi_write_codec.c`,
`ufi_write_stream.c`, `ufi_verify.c`, `ufi_mfm.c`, `ufi_write_wave.c`,
`ufi_job.c` and `ufi_resources.c` completed with no diagnostics.

Validated software checks: 489 original native assertions (304 codec, 38 verify,
90 stream, 57 image), 29 independent Amiga checks, 291 quality/flag checks, plus wave/job/resource assertion suites; seventeen regression
executables (USB/cache, IEC, write/index, DMA allocation, four GW regressions,
parser fuzzing, composite descriptors, clock64, heap bounds, PSRAM selection, USB reply queue, IEC reset states, quality runtime, native WRITE flags); 141 host tests; 23/23 py65 checks. Generated drive code is 622 bytes
and matches the checked-in header. Native tests compile production codec modules;
control regressions extract selected production functions and use register/bus
doubles. py65 executes the real 6502 binary against a simplified drive model.
Neither is a cycle-accurate STM32 or complete Commodore drive simulation.

Build footprint: FLASH 160200 bytes, DTCM 106464 bytes, AXI SRAM 289288 bytes
(88.28% of 320 KiB), SRAM2 8192 bytes. CDC packet buffers now match the physical
64-byte Full-Speed packet size. The two 8 KiB CPU RX rings now live in DTCM;
their capacities are unchanged. Newlib retains an 8 KiB heap with a bounded
`_sbrk`. Linker/ELF gates enforce 32 KiB AXI and 16 KiB stack reserves.
U11 remains the preferred 8 MiB flux buffer; the 224 KiB fallback is preserved.
See [Speicherarchitektur.md](Speicherarchitektur.md) for the memory audit and tests.
Local `firmware/build/ufi_firmware.bin` SHA-256 at validation:
`0770a1c3984cc92005cbbeb8c545efa205b0f4c856a6f49a294a309d00ec908f`.
The native archive's synchronous per-frame disk commits prioritize evidence
durability; sustained throughput must still be benchmarked. The archive contains
accepted protocol frames, not rejected/corrupt serial bytes. Hashes detect file
changes; they are not authentication or proof of an accurate physical capture.

## Remaining qualification and further work

1. Actual IEC line transitions coupled to independent VICE true-drive models and
   legally supplied versioned ROM fixtures. Current ROM signatures and py65/bus
   doubles do not prove 1541/C/II, 1571 or 1581 interoperability.
2. OS enumeration, WinUSB binding, sustained Full-Speed throughput and reconnect
   races on real hardware. Verify PID allocation before distribution.
3. STM32H723 waveform measurements, especially TIM3 DMA burst through the reserved
   middle register slot, preload sequencing, 400-ns pulse polarity, DMA/IRQ latency
   at short ASTABLE periods and contention under worst-case load. Mathematical
   waveform tests are not evidence that electrical deadlines are met.
4. Independent real-media SCP/HFE/G64 interoperability corpora and broader
   state-machine fuzzing. Amiga per-revolution scoring and bounded adaptive PLL
   decoding now exist with synthetic offline evidence. The current parser corpus is deterministic.
5. Pin CI toolchain/action revisions and execute remote ASan/UBSan. Remote CI was
   not run in this session. Replace remaining synchronous waits as needed after
   measuring latency. File transfers do not yet have atomic upload/commit jobs.
6. Complete the physical measurements below before a hardware-qualified release.

All eight requested areas now have implementations and offline evidence, with
these explicit limits. No bug-free or best-device release claim is justified.

## Reproduce and use

```text
python -m venv build/venv
build/venv/Scripts/python -m pip install -r requirements-test.txt
build/venv/Scripts/python firmware/tests/check_all.py
cmake --build firmware/build
```

Configure CMake with the local STM32CubeH7 v1.11.0 path as described in the
firmware README when no build directory exists. Install the host package or set
`PYTHONPATH=software/ufi_host` for these CLI examples:

```text
ufi iec-read disk.d71 --type 1571 --device 9 --retries 2
ufi iec-read disk.d71 --type 1571 --device 9 --resume
ufi read-disk -o disk.ufi --tracks 80 --sides 2 -r 3
ufi archive-check disk.ufi
ufi mode composite
ufi --port usb features
ufi --port usb --integrity read 0 0 -r 3
ufi iec-identify --device 8 --fingerprint
ufi iec-write disk.d64 --checkpoint disk.write.json --retries 2
ufi iec-write disk.d64 --checkpoint disk.write.json --resume
```

Resume requires the same physical disk. The checkpoint validates saved sectors
and geometry/address, not an immutable disk identity. A job uses `.partial` plus
`.ufi-job.json`; only all-successful jobs acquire the requested image filename.
Existing images/jobs are not overwritten. Native archives are new files only.
For `iec-nib`, `<output>.fragments.jsonl` is retained even if G64 export is refused.
IEC GCR chunks are not physical flux timestamps.

## Reference sources

- ST: https://www.st.com/en/microcontrollers-microprocessors/stm32h723-733/documentation.html
  (DS13313, RM0468, ES0491; check against the actual silicon revision).
- ST cache/DMA: https://www.st.com/resource/en/application_note/an4839-level-1-cache-on-stm32f7-series-and-stm32h7-series-stmicroelectronics.pdf
- OpenCBM: https://opencbm.trikaliotis.net/opencbm-23.html
- Nibtools: https://raw.githubusercontent.com/OpenCBM/nibtools/master/readme.txt
- VICE: https://vice-emu.sourceforge.io/vice_1.html
- SCP: https://www.cbmstuff.com/downloads/scp/scp_image_specs.txt
- HFE: https://hxc2001.com/floppy_drive_emulator/HFE-file-format.html

## Baseline

Firmware 1.17: ARM Release build passes; 485 native checks pass. Python test
dependencies were absent from the selected interpreter. Existing local KiCad
project edits and `claude.txt` are unrelated and must remain untouched.

## Required hardware evidence (not available)

Signal polarity/levels, write pulse timing, IRQ latency, cache/DMA bus contention,
SD/PSRAM stability, IEC electrical timing, real drive compatibility and media
readback must be measured on the assembled board. No hardware-qualified release
is claimed by this software work.

## Amiga improvements (2026-10-13)

Implemented review items 1-4: extended host DD/HD readback, 16-byte label
comparison on host and firmware, bounded three-profile PLL analysis with
per-revolution conflicts/errors and histogram JSON, leading track gap and
startup-cell bounds, independent synthetic host/native vectors. No external
GPL code was incorporated. Offline SCP analysis never connects to USB.
SCP/HFE writes supply source revolution duration for index-split sectors.
See [Amiga_Prueflesen.md](Amiga_Prueflesen.md) for commands, evidence levels
and limitations. Full offline gate and ARM build passed; physical hardware
qualification remains open.

## On-device quality preprocessing and verification (2026-10-13)

The firmware now implements native ANALYZE DETAIL/ADAPTIVE flags, independent
of the Python tool. IBM-MFM and Amiga decoding use up to three PLL profiles;
unknown formats retain raw interval/revolution statistics. A versioned 1824-byte
quality block records CRC32 variants, errors, revolution masks, histogram,
index duration, transition counts and largest gaps. It is returned with detailed
ANALYZE and can be fetched using QUALITY 0x0E. INFO capability bit 21 advertises it.
Extended WRITE now accepts VERIFY, builds a u32 source reference before WGATE,
and runs readback inside firmware. Optional source index duration supports split
sectors. Cancellation, capture overflow, invalid timestamps and unsupported
sources are tested. Classic unknown-format count plausibility remains level 2,
not content verification. The Python host retains its previous separate verification
path; other tools can use the native firmware path directly.

Full offline gate: 141 host tests, 291 quality/flag assertions, 17 regression
executables and 23/23 drive-code checks passed. ARM build passed; AXI reserve
38392 bytes, stack address space 24608 bytes. No hardware test performed.
See [USB protocol section 4.9](USB_Protokoll.md#49-firmware-detailanalyse-zwei-flag-bits-118-dev)
for byte layouts, flags, evidence limits and timeout requirements.


## Extended on-device analysis flags (2026-10-13)

Implemented TIMING (0x04), GAPS (0x08), GLITCHES (0x10), POSITIONS (0x20),
SYNC (0x40) and WINDOWS (0x80); each requires DETAIL. INFO bit 22 advertises
these flags. The optional versioned 33-byte request configures thresholds,
raw-cell pattern, at most 48 shared events, at most four intervals per raw
window, and a detail budget of at most 10 seconds. A 1920-byte QX block is
appended only for the new flags; legacy request/reply sizes remain unchanged.
No heap, capture copy or raw-data modification. The extra persistent RAM cost
is 1936 bytes; U11 capture capacity is unchanged.

Statistics retain coverage and saturation indicators; event overflow is counted.
Patterns use fixed raw-cell quantization and restart at index boundaries.
Sector positions delimit decoder fields in the standard IBM/Amiga profile,
not exact physical splice boundaries. Raw windows end at the triggering edge.
Unknown formats still provide timing, gap, glitch, pattern and window evidence.

ARM build and the full offline gate passed. The final seven added arithmetic,
32-bit pattern and index-boundary checks also passed in the targeted quality
suite (291 checks total). Runtime tests exercise combined flags, position
records, raw-window contents, report bounds, event truncation and deadline/
cancellation reporting. Hardware timing and real media remain unqualified.

## FM/MMFM decoding and native diagnostics (2026-10-14)

Added independent IBM FM and HP 9895 MMFM decoders to the bounded shared PLL.
Native ANALYZE supports format IDs 3/4 and automatic detection; the extended
WRITE verification path also decodes these formats. Uploaded raw flux remains
the write source; no HP-IB bus driver or new sector encoder is claimed.
HP ECC address marks are recognised, but ECC payloads are not decoded.

INFO bit 23 advertises a 37-byte version-2 ANALYZE parameter block with a new
16-bit diagnostic_flags field: CODING_ERRORS=1, ADDRESS_MARKS=2, PLL_MARGIN=4.
A 304-byte QD block records per-revolution/profile counters and completion masks.
The maximum native ANALYZE payload is 4088 bytes (compile-time bound <=4096).
CRC and coding errors are separate. PLL margin is digital residual evidence,
not an analogue signal measurement. Greaseweazle commands and framing are unchanged.

Full offline gate passed: native suites, Greaseweazle/control regressions,
141 host tests, and 23/23 drive-code checks. Final targeted suites additionally
passed 49 independent FM/MMFM checks (including index-split source, unsupported
ECC and oversized sectors), 291 quality checks, and runtime/parser regressions.
ARM build passed: FLASH 160200 bytes; AXI free 38392 bytes; stack address space
24608 bytes. Hardware qualification and real-media test vectors remain open.
See [USB protocol](USB_Protokoll.md) and [HP9895 review](HP9895_Review.md).

## Analysis jobs and explicit verification coverage (2026-10-14)

Implemented the eight follow-up improvements in firmware and offline tooling:

1. Native 0x0F analysis jobs: capture/analyze/status/cancel states, cooperative
   decoder slices (256 edges, 2-ms target checked every 32), bounded total time.
   Mechanical seek/motor preparation still uses the existing synchronous driver.
2. Schema-1 discovery: formats, rates, verification support, schemas, flags,
   page/event/retry limits, parameter bounds and selected-selftest capabilities.
3. Result paging: job ID, generation, section, offset, length and per-page CRC32.
   Stale quality generations are rejected. No additional full-report RAM buffer.
4. Pinned independent FluxFox SCP corpus with SHA-256 manifest, optional download,
   deterministic replay through production ufi_mfm.c and comparison against IMG.
   All 80 tracks / 720 sectors match, with 2160 valid observations and no data-CRC
   failures. Upstream metadata names Greaseweazle 1.18. This covers IBM MFM only;
   provenance is recorded without claiming certification of the capture chain.
5. Commodore GCR analysis: four density rates, header/data XOR checksums, invalid
   symbol detection, disk-ID-aware content conflicts and index-crossing decoding.
   Raw on-disk track/sector IDs are retained. No GCR content-verified write yet.
6. Same-track rereads: RETRY_MISSING=1, RETRY_ERRORS=2, EXPECT_SET=4; up to eight
   attempts / 60 seconds, fixed 40-byte history per attempt. Latest attempt stays
   authoritative; observations from separate attempts are not silently merged.
7. WRITE bit7 EXPECT_SET: exact declared CHRN mask checked against the reference
   before WGATE; inventory query distinguishes decoded-source versus declared-set
   coverage. Existing END status and verification level remain authoritative.
8. Selected bring-up tests: explicit mask, separate permission bit for PSRAM
   pattern writes and driven IEC/index loopbacks. SD information is read-only.
   No automatic log or disk writes; disk-write tests stay separate commands.

Wire details and host integration sequence: [USB protocol section 4.10](USB_Protokoll.md#410-analysejobs-discovery-und-prüfabdeckung-info-bit24-2026-10-14).
Greaseweazle framing and commands remain unchanged. The new features use native
UFI commands and INFO bit24; no per-disk-format flag proliferation.

Validation: complete offline gate including native/control/Greaseweazle tests,
analysis-job protocol tests, independent capture replay, 141 host tests and
23/23 drive-code checks. ARM build: FLASH 166588 bytes, AXI free 37856 bytes,
stack address space 24576 bytes, reserved heap 8192 bytes. Firmware binary SHA-256:
`9dc17a3f627f08c3e1ff6434dbd030b8f441c07df35658ca0f3552ea2d5716d6`.
CI includes pinned corpus download and sanitizer execution; CI has not been run
in this session. No physical board tests; electrical correctness, sustained U11
bandwidth and actual disk writing still require hardware qualification.

## Capture evidence and drive timing extension — 2026-10-14

Implemented native INFO bit25 and ANALYSIS_JOB ops 7/8/10. Job flag bits
RAW_INDEX=0x0008 and TRANSFER_TRACE=0x0010 retain bounded, format-independent
capture evidence; every native capture through the standard capture entrypoint
also freezes a 160-byte context. Job report sections 4–6 expose the same data.
Generation checks reject evidence replaced by a later capture. No foreign
firmware code was imported. Greaseweazle wire commands remain unchanged.

Extra drive timing parameters cover line changes, motor-to-first-step delay,
pre-write settling and native extended 34-pin write gate margins. Nonzero gate
margins are rejected for classic native/Apple writes before WGATE. Existing
Greaseweazle pre/post parameters remain separate. All new settings are volatile.

Record storage uses 4992 bytes of CPU-only SRAM1, without reducing U11 capacity
or relaxing the 32-KiB AXI reserve. Latest ARM build: FLASH 169960 bytes,
AXI free 37592 bytes, stack address space 24576 bytes, heap 8192 bytes.
Binary SHA-256:
`76b0ada2940cbcf9858185cf47f205ef6045b9fdab4e5d218b73e0dde473dbf5`.

Validation: full offline gate passed, including 141 host tests, 23 drive-code
checks and the independent 80-track corpus. Additional production-path timing
tests passed for motor delay/wrap, line ordering and silent gate margins.
Raw-index and runtime tests cover filter reasons, bounds/losses, immutable
context, coalesced USB stalls and stale capture IDs. Hardware qualification
remains open; a trace reports observations and does not establish causation.

## Retry failure and capture stop fixes — 2026-10-15

A new analysis attempt now clears the previous attempt's report generation,
sector set and capture ID. Failures before capture starts are recorded in the
attempt history, including initial seek failure and failure during a retry.
Earlier successful observations remain in their own history entry only.

Direct capture stop now freezes evidence before stopping DMA and changing
capture state to idle, preserving the final progress sample on abort/transport
stop. Repeated stop leaves the retained report unchanged. No new flags or wire
layouts were needed.

Validation: analysis-job and evidence runtime regression suites passed,
including failed retry after a decoded track and repeated stop. ARM build:
FLASH 169968 bytes; AXI free 37592 bytes; SRAM1 evidence 4992 bytes.
Binary SHA-256:
`4feadfc4a7b80722c4b409b84ca7f1675b5fb47f5f6de45d23f1c17c76c09735`.
No hardware tests performed.

## GCR verification, revolution comparison and recovery policies — 2026-10-15

Implemented the recommended first work package (items 1, 2, 8 and 3):

- Extended native WRITE now builds and checks GCR content references in all four
  CBM density zones. The hash includes 256 data bytes and both header disk-ID
  bytes. Source coverage/EXPECT_SET and the existing verify levels are reused.
- Optional decoder-independent revolution comparison: monotonically match
  index-relative timestamps with configured tolerance after linear period
  normalisation. Report missing/extra transitions in 128 windows and a bitmap
  of affected revolutions. No automatic phase alignment or weak-bit verdict.
- Separate recovery configuration: default legacy retry policy, consecutive
  matching-content confirmation, or stop on stalled recovery progress. No new
  job flags; REV_COMPARE=1 belongs to a separate report mask. Configuration is
  snapshotted per job. INFO bit26, native ops 11/12 and report sections 7/8.
- Independent GCR test corpus uses pinned MIT-licensed cc1541 source solely as
  a host-side encoder. All 35 tracks / 683 sectors match the D64 payload, with
  1366 valid observations plus reference/readback checks. These are external
  synthetic vectors, not physical GCR captures. The upstream encoder uses a
  literal ASCII disk-ID in G64 headers independently of PETSCII BAM contents;
  tests check that actual header identity explicitly.

Provenance is pinned in `firmware/tests/reference/gcr_corpus.json`. CI downloads
and verifies the encoder hash and runs the corpus; offline checks explicitly
report a missing fixture instead of claiming coverage. The existing physical
SCP corpus still covers IBM MFM. Real-media Amiga/GCR/FM/HP/protection corpora
remain a validation gap; synthetic coverage must not be labelled hardware proof.

The new comparison is cooperative and uses SRAM1, without another full-track
copy. ARM build: FLASH 172260 bytes; AXI free 37040 bytes; SRAM1 6560 bytes;
stack address space 24576 bytes, heap 8192 bytes. SHA-256:
`5ea3eebaa24f25bbf6d42f1afe87dd1e2e841e92c838709fb1464593556c8672`.

Remaining later work from the eight-item list: general flux-level WRITE
verification, automatic hard-sector classification, retained capture replay
until host acknowledgement, and expanded drive-health diagnostics. None is
advertised by the new capability. No hardware qualification performed.

Final validation passed: complete offline gate including comparator bounds,
timing normalisation, cooperative cancellation, recovery policies, GCR content
changes/disk-ID changes/checksum failure/index crossing, both external corpora,
141 host tests and 23/23 6502 drive-code checks. ARM build passed without new
warnings. CI configuration was updated but remote CI was not executed here.

## Drive position and media state — 2026-10-15

Implemented independent UFI state handling inspired by the floppy_bios review;
no upstream BIOS code was imported. Unknown positions now require calibration,
and an unexpected early Track-0 signal fails a seek instead of reporting success
at the wrong cylinder. Failed calibration and safety/protocol resets invalidate
position. Apple positions are explicitly marked as estimated.

INFO bit27 exposes native ops 13–16: status, per-drive mechanical profile,
separate media description, and manual media-change notification. Five status
bits describe position validity, configured change-signal support, last observed
change level, media-profile presence and estimated position. No additional
analysis flags or Greaseweazle wire commands/status codes were introduced.
The complete layouts and restrictions are in USB_Protokoll.md section 4.13.

A rising change indication invalidates the media profile and advances a
boot-local generation. Native capture, analysis jobs and WRITE reject observed
generation/drive changes with status 21; WRITE releases WGATE when aborted.
Job and capture reports retain their starting media context. These are polling
guards, not physical write protection; unsupported signals and a held change
latch cannot prove that the disk stayed unchanged. Multi-track operations do
not acquire a transaction-wide media guarantee.

Validation: the complete offline gate passed, including 141 host tests,
23/23 6502 checks and both external corpora. Focused media, analysis-job and
control/write regression tests passed again after the final refinements.
Simulated cases include early/stuck/missing Track-0, bounded calibration,
profile limits, held/reasserted change signals and WRITE abort before/during
writing. ARM build passed. Hardware qualification remains pending.

Current build: FLASH 174832 bytes; AXI 291080 bytes used, 36600 bytes free;
SRAM1 6560 bytes; stack address space 24576 bytes; heap 8192 bytes. Binary
SHA-256: `084778e3f1c987cdab825dcbd1da40efecef2677631b4163f237b4614ae7675f`.

## Optional operating-profile checks — 2026-10-16

Analysis job flag CHECK_PROFILE (0x0020, discoverable through op 0) produces a
retained 32-byte section 11. It compares capture-time drive/media metadata and
effective stepping against decoded format/rate and physical index periods.
Independent result bits report RPM_MISMATCH, PROFILE_CONFLICT and
INSUFFICIENT_EVIDENCE, with a separate reason mask. Synthetic index, timed
slices and configured hard-sector modes cannot establish spindle speed.
GCR zone rates are not compared against a global media rate. This is a
non-mutating check: no automatic RPM/density changes, retry or write operation.
Greaseweazle commands remain unchanged. See USB_Protokoll.md section 4.14.

Validation passed: native operating-profile matrix, job protocol/retention and
simulated-index rejection, 43 write-verification checks including 300 kbit/s
at 360 RPM and logical-versus-physical cylinder IDs, and Greaseweazle regression
tests. ARM build and memory budget passed. No hardware tests performed; the
complete offline gate from the preceding entry was not rerun for this change.

Current build: FLASH 175736 bytes; AXI 291112 bytes used, 36568 bytes free;
SRAM1 6560 bytes. Added persistent AXI usage: 32 bytes; U11 capacity unchanged.
SHA-256: `1939e942668291cee0cc27083c106f5f2e9406acacbe4a31346fa6fb23c2fb37`.

## Port conflicts, operating profiles and ED coverage — 2026-10-16

INFO bit28 exposes native op 17 (24-byte port report) and op 18 (40-byte
operating settings). Explicitly saved per-drive profiles contain timing,
extra timing, speed-select line/polarity and target RPM. Native SELECT restores
them, using the target select-settle timing. Unconfigured drives retain the
legacy shared-settings behavior; GW/internal selection does not restore native
profiles. Individual setters do not silently overwrite saved profiles.

Native raw line changes and speed/timing/profile setters now reject known
DRATE/DS3 and DENSITY/TG43/speed conflicts before GPIO changes. Direct DENSITY
writes cannot override an assigned speed/TG43 line. GW SET_PIN reports its
existing ACK_BAD_PIN on rejection. Port reports describe wiring assumptions,
not detected cables/jumpers/drives, and include actual normal-write interval
limits. CHECK_PROFILE gains result bits LINE_CONFLICT=8 and
UNSUPPORTED_CONFIGURATION=16; reasons use bits 16–18. No new job flag.
Section 12 retains the port report before the last capture attempt.

ED tests exposed the existing normal-write minimum of 440 ticks (1.6 us),
which rejects the approximately 1 us intervals required by 1 Mbit/s ED.
This limit was preserved. Full synthetic 36-sector decoding/content comparison,
last-sector corruption, truncation, encoder-capacity failure and rejection by
the normal write packer are covered. Clamp is not an ED implementation.
No physical ED read/write support is claimed by these offline tests.

ARM build: FLASH 177632 bytes; AXI 291616 bytes used, 36064 bytes free;
SRAM1 6560 bytes; stack address space 24576 bytes; heap 8192 bytes.
SHA-256: `9ae503cbcd8d90021b85af03cdb1fd4e2cd70826b7e9f8139cb6894da602a27e`.

Final validation: complete offline gate passed, including production profile
guards/switching, 50/50 write-verification checks, native protocol/runtime and
Greaseweazle regressions, both independent corpora, 141 host tests and 23/23
6502 drive-code checks. ARM build passed without warnings. Hardware qualification
remains open; nothing was flashed or pushed.

## Macintosh GCR and explicit observation semantics — 2026-10-16

Implemented format 6 for Macintosh 400K/800K 3.5-inch GCR, with bounded
header/data association, legal-symbol and epilogue checks, repeated sector ID,
header XOR and data checksum. The existing decoder buffer is reused. Content
fingerprints preserve 12 tags, 512 payload bytes and the header format byte.
Extended native WRITE source references/readback also recognize this format;
there is no Macintosh sector encoder or new drive emulator.

Native timing and five fixed-300-RPM zone rates are supported. Auto detection
tries native timing and the requested cylinder's fixed-speed zone; explicit
rates remain available. INFO bit29 / op 19 describe the extension without
overwriting the five legacy descriptor entries. Candidate count is 24 and
section 3 is 48 bytes. CHECK_PROFILE uses existing flag 0x0020, with new
ZONE_MISMATCH=32 and ALIGNMENT_UNCERTAIN=64 result bits. Media profile byte 7
can explicitly select the fixed-300-RPM Mac zone model. No implicit switch of
drive speed or assumption of native Mac hardware is made.

Section 13 separates inconsistent valid content, decoding errors, missing
sectors and missing evidence. Comparison header fields distinguish missing
and additional edges and explicitly mark alignment as unverified. Cross-track
phase measurement and weak-bit writing are not implemented or advertised.
See USB_Protokoll.md section 4.16 and Floppy_Emu_Review.md for exact semantics
and provenance; no femu.bin code was executed or imported.

Validation: complete offline gate passed, including synchronous/cooperative
Mac autodetection across all five zones and native timing, checksum/tag tests,
both existing external corpora, 141 host tests and 23/23 6502 checks. The local
BMOW MOOF fixtures additionally pass 240 tracks / 2400 sectors with valid
header/data checksums and consistent repeated bitstreams. They are not physical
captures. The added inner-zone reference test passed separately. Final ARM
build passed without warnings; no hardware qualification performed.

Current build: FLASH 180084 bytes; AXI 291624 bytes used, 36056 bytes free;
SRAM1 6560 bytes, stack address space 24576 bytes, heap 8192 bytes.
SHA-256: `f5917f429987159c1a9919b89b7bc03f231ee110e79f81766059f3aabbe15e33`.

## HxC follow-up: local evidence and HFEv3 — 2026-10-16

Implemented 32 bounded firmware timing windows with independent cell estimation,
sample/diversity/spread gates, relative rate-change candidates and separate valid
content-change versus decoder-error observations. Existing comparison windows
join missing/extra edges by generation. INFO bit30, discovery op20 and paged
section14 expose the 1312-byte report; existing TIMING/WINDOWS/REV_COMPARE
selections suffice. No Greaseweazle wire change or physical weak-bit claim.

Host HFEv3 import preserves variable cell timing, index positions and RAND
metadata. Deterministic tracks use Extended WRITE, with optional firmware
verification and SOURCE_INDEX. Unknown opcodes, truncated commands and lossy
v1 export are refused. RAND physical writing, multiple-index mapping, v3 export
and MCU v3 file parsing remain unimplemented, explicitly rejected where needed.

Optional hash-pinned local HxC corpus: 536 sides, 5000 valid sectors, 16
explicitly expected undecoded outer sides. Sector IDs, checksums and repeated
content pass the production decoder. This is synthesized flux from HFE v1,
not a hardware or independent payload-image comparison.

Validation: full offline gate passed (147 host tests, all native/runtime/GW/IEC
tests, SCP/GCR/MOOF/HFE corpora, 23/23 6502 checks). Additional targeted tests
for local checksum/content distinction and the final HFEv3/parser cases passed.
Final ARM build warning-free; FLASH 181820 bytes, AXI 292904 used / 34776 free,
SRAM1 6560, heap 8192, stack address space 24576 bytes.
SHA-256: `4d14ccde8ba6b058ece25b1332351eef319ba0e75379681638a4e9715c17f451`.
Hardware qualification remains open. See USB_Protokoll.md 4.17 and HxC_Gotek_Review.md.

## Recognition and attempt causes — 2026-10-16

Native INFO bit31 / discovery op21 expose sections15 (64-byte recognition)
and16 (20-byte assessment per attempt). Explicit sector-set evidence and
repeated content confirmation are distinct; a single track never confirms a
disk-wide geometry. Profile provenance, ambiguity and incomplete evidence
remain visible. Auto-observed media now requires a valid sector observation.

CHECK_PROFILE adds header C/H mismatch result128 / reason bit20 for matching
declared non-CBM profiles. Explicit expected C/H/N/ID sets also cover CBM's
numbering conventions. No inferred mechanical recovery, no Greaseweazle change.
Per-attempt causes distinguish missing sectors, decoder/content evidence,
transport, buffer, position, index, media changes, timeouts and cancellation;
retained decisions explain retry/confirmation/stagnation/limit stops.

Validation: complete offline gate passed, 150 host tests and 23/23 6502 checks;
targeted recognition/job tests repeated after final refinements. Warning-free
ARM build: FLASH183868, AXI293080 used /34600 free; SRAM1 6560, heap8192,
stack address space24576. No hardware qualification or upstream BIOS code import.
SHA-256: `ed37a5c1b981afb33840ae2d3cd022519168335465fa80fc14465c09e1872b10`.

## Capability matrix and configuration epochs — 2026-10-16

Native analysis operations22/23/24 expose a 224-byte family/backend matrix,
session-local conservative configuration epoch and compare-before-analysis-start.
No new INFO or request flags; six result flags distinguish unqualified hardware,
unqualified writes, stale configuration, missing flux port, busy engine and line
conflicts. Raw writing is distinct from sector encoding; decoder rate support is
not a physical write qualification. Hardware masks remain zero. Existing native
analysis start and Greaseweazle wire contracts are unchanged.

Epoch updates cover accepted drive/timing/extra/profile/RPM/density changes,
including A-B-A switches and repeated settings. Invalid drive numbers are rejected
before touching GPIO. Configuration and media epochs remain separate; a guarded
analysis start does not constitute a guarded write or multi-track transaction.
Host parsers and payload wrapper are in ufi_host.capabilities.

Validation: complete offline gate passed (150 existing host tests, all native,
runtime, corpus and 23/23 6502 checks). Final expanded drive/media/job tests and
two additional host tests passed separately. ARM build warning-free:
FLASH184744, AXI293080 used /34600 free; SRAM1 6560, heap8192, stack space24576.
No hardware test, no imported GLaDISK code.
SHA-256: `870bb34b4c370e217a80dae74f200ee561931faebf76ffa278e2a3ab9ccdf9a6`.

## Offline hardening and media-bound starts — 2026-10-16

Native0x27 wraps WRITE with expected configuration/media tokens; response0x27,
DATA/END0x22. Discovery is analysis-op25. Analysis-op26 similarly checks both
tokens. Both paths recheck media after preparation/start. Existing stream,
integrity and Greaseweazle wire formats are preserved. Matrix bytes23/24
advertise the new operations and eligibility now accounts for a GW owner.

Native READ rejects incomplete fields, unknown flags, trailing bytes and excessive
indexless periods; failed starts restore the prior hard-sector setting. WRITE
commits that setting only after successful preparation. Compact size validation
precedes preparation, preserving retained verification evidence on malformed input.
Host parameters are packed before opening an integrity job; a rejected write
closes its own job. No new request flags or persistent RAM allocations.

Full offline gate passed:161 host tests, native/runtime/GW and external corpus
tests,23/23 6502 checks. Final media-start and host refinements were tested
separately (including one additional host test). Ten pure production modules
pass Clang static analysis; reproducible tool and CI step added. Remote CI was
not run. Source review and remaining work: Offline_Review_2026-10-16.md.

Final warning-free ARM build: FLASH184968, AXI293080 used /34600 free,
SRAM1 6560, heap8192, stack space24576. SHA-256:
`19ad64901d2fe82c474cd7125d68fdeb82b4cfb1ac3157fdb67004e36a7750f7`.

## Dispatcher fuzzing and coverage measurement — 2026-10-17

`firmware/tests/run_fuzz.py --target v2` compiles `ufi_v2.c` as a whole on the host
(`tests/hal_stub/` supplies the HAL declarations) and links the real job, resource,
cancel, capability, media, write-codec and quality modules. Every other call out of
the dispatcher goes to a stub that `fuzz_stubs.py` generates from the header
prototypes: error codes, flags, states and counts come from the fuzz input, pointers
point at a zeroed buffer, structs are zero. The harness feeds CRC-correct request and
DATA frames, raw byte streams, operation ends, events and comms clears, lets
Greaseweazle own the drive in some inputs, and checks every emitted frame (magic,
length field, CRC) besides ASan and the libFuzzer timeout. Runs: 2.1 M executions in
61 s without a finding. Two early reports were contract violations of the generic
stubs (a report length above the given capacity, an oversized internal end payload)
and were corrected in the harness; the firmware was not changed. No firmware
behaviour changed in this step, so the protocol document is unchanged.

## Timing presets from the KryoFlux reconstruction — 2026-10-17

The Ghidra pass over the KryoFlux 3.00 image (`REA_Firmware_Analyse_2026-10-17.md`)
recovered the firmware's drive timing defaults. UFI already had every corresponding
parameter (TIMING 0x15, additional drive times op 10, stored per-drive profiles op 18),
so the adoption is a named preset rather than new fields:

- `ANALYSIS_JOB [18, id]` applies a timing set and answers with the effective 40-byte
  profile: 1 = factory, 2 = conservative (step 8 ms, head settle 40 ms, 38 ms on direction
  reversal, 1 ms side select, 800 ms spin-up, 60 ms drive select, 200 ms motor-to-step,
  density settle capped at 60 ms). Only movement/settle times change; the stored profile,
  step pulse, precompensation, TG43, write settle, WGATE margins and the RPM line stay.
- Factory default `dir_change_us` is now 15 ms (was 0): a head that reverses direction
  without settling mis-tracks on older mechanisms; the preset keeps the cost at one seek.
- Host: `ufi extra` (op 10), `ufi profile [standard|konservativ|save]` (op 18), wrappers
  `extra_timing()`, `operating_profile()`, `operating_profile_store()`, `timing_preset()`.
- Not adopted: KryoFlux's text replies and control-request protocol, the bypass line
  (no hardware), the 86-step recalibration (UFI's three-phase TRK00 search is stricter).

Checks: native `test_analysis_job.py` (preset dispatch, invalid ids), pytest
(`test_extra_timing_profile_and_presets`), the offline gate, ARM build and static analysis.

## Stored profiles on SD, drive-module fuzzing, SD fuzzer reach — 2026-10-17

- Stored operating profiles persist in `UFIPROF.BIN` (header, 12 x 40 bytes, CRC-32):
  written by every successful Op 18 store, read after UFI.CFG, shape-checked per slot,
  dropped on size/CRC/schema mismatch (protocol 4.15). Presets and single TIMING/Op 10
  changes do not touch the file.
- New fuzz target `run_fuzz.py --target drive`: `ufi_drive.c` as a whole with the real
  media, capability, extra-timing and CRC modules; HAL GPIO, time and the SD file API are
  harness doubles. The harness models one head per drive type (STEP edges in the DIR
  direction, TRK00 from the model with rare injected faults) and checks that a successful
  seek or recalibrate leaves module and model on the same physical track, that stored
  profiles round-trip through the file double, and that the port report keeps its schema.
  First finding, fixed: `ufi_drive_apple_step()` on a non-Apple drive ran the Apple
  recalibration and marked that drive's head position valid without moving it; it now
  returns NOT_IMPL (NO_DRIVE for NONE/IEC). The other reports were harness-model errors
  (per-drive positions, positions kept across inputs, status sampling TRK00 live, a
  sensor lie surviving into the next operation) and were corrected in the harness.
  After the fix: 2.2 M executions in 301 s without a finding (839 edges of 1360).
- SD fuzzer: the `double_step=2` presets never enabled the auto probe (the key takes
  `auto`); fixed in harness and seed. Synthesised tracks now stamp cylinder track/2 for
  some inputs and carry a few damaged spots, `ufi_flux_capture_start/stop` and the drive
  timing are modelled, so `probe_double_step`, `ds_sector`, `recapture` and
  `search_track` are reachable.
- Host: all `zip()` calls carry an explicit `strict=` (ruff B905), the two deliberately
  truncating pairings in `hfe.py` say so; one unused loop variable and one late-binding
  lambda in `cli.py` fixed; ruff `--select F,B` is clean for the package.

`firmware/tests/coverage.py` measures line and branch coverage (clang, llvm-cov) of
every native whole-unit build: the runner, the five corpora and the v2 fuzzer corpus
replay. First measurement: 96.1 % lines, 83.2 % branches over 18 modules; `ufi_v2.c`
99.2 % / 95.9 %, `ufi_media.c` 52.9 % branches, `ufi_cancel.c` 20 % lines (delays are
not exercised). 38 of 56 modules are not measured because they run only through the
function-extraction regressions or need hardware; llvm-cov ignores `#line`, so those
programs cannot be attributed to the source files. Coverage states which lines ran,
not that their results were checked; the generated stubs do not model driver timing
or state. Output: `firmware/tests/build/coverage.txt`, `coverage.json`, `cov/html/`.

Second step the same day: `run_fuzz.py --target gw` fuzzes the whole Greaseweazle
protocol module (`ufi_gw.c`, 33 generated stubs, real resource and cancel modules)
with the raw CDC byte stream, oracle-chosen chunking, read completions, comms clears
and a byte budget that bounds SOURCE_BYTES; 2.2 M executions in 46 s without a
finding. Coverage with both fuzzers: 96.4 % lines / 84.3 % branches over 19 modules,
`ufi_gw.c` 97.4 % / 92.1 %. `firmware/tools/check_static.py` now analyses 33 modules
instead of 12: the 21 additional HAL-bound modules compile against `tests/hal_stub/`
(DWT, TIM, SCB, USB CDC declarations only); 16 modules that need the real HAL, USB
library or FatFs headers remain outside (`NEEDS_REAL_HAL`). No findings.

Third step: `run_fuzz.py --target sd` fuzzes the stand-alone SD module (`ufi_dump.c`,
2200 lines, 43 generated stubs, real image/MFM/HFE/verify/quality modules) through an
in-memory FatFs (`tests/hal_stub/ff.h`, implemented in `fuzz_sd.c`): read files return
the fuzz input zero padded to the size the image kind needs, write files are bounded
sinks, rare FatFs errors (open, read, short read, write, disk full, seek, mount) come
from the oracle, `f_mkfs` asserts. Captures are synthesised from the input, half of
them as valid IBM-MFM tracks encoded by the firmware's own encoder so the quality
decoders, image writers and double-step logic see real sectors. One input runs one SD
write (SCP, HFE v1/v3, ADF, IMG, D64, D81), the UFI.CFG parser plus the host file API,
or a dump/copy to SD in a fuzzed format, each after a UFI.CFG preset so the module's
static configuration is known. 13 k executions in 121 s without a finding;
`ufi_dump.c` 87.9 % lines / 75.2 % branches, all native builds 93.9 % / 82.4 %. Four
harness mistakes surfaced along the way (file model stricter than FatFs: a failed
`f_open` left the handle open, a failed `f_close` closed it, re-opening a handle was
refused; a sector buffer too small for size code 3) and were corrected in the harness;
the firmware is unchanged. `check_static.py` now covers `ufi_dump.c` as well (34
modules). Not reached yet: the double-step probe and track search (need a recognised
format on track 0 plus a weak later track).
