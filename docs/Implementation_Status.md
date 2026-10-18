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
See [USB protocol section 5.2](USB_Protokoll.md#52-firmware-detailanalyse-acht-flag-bits-118-dev)
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

Wire details and host integration sequence: [USB protocol section 6.1](USB_Protokoll.md#61-analysejobs-discovery-und-prüfabdeckung-info-bit24-2026-10-14).
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
The complete layouts and restrictions are in USB_Protokoll.md section 4.2.

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
Greaseweazle commands remain unchanged. See USB_Protokoll.md section 4.3.

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
See USB_Protokoll.md section 5.4 and Floppy_Emu_Review.md for exact semantics
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
Hardware qualification remains open. See USB_Protokoll.md 5.5 and HxC_Gotek_Review.md.

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
  dropped on size/CRC/schema mismatch (protocol 4.4). Presets and single TIMING/Op 10
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
- Long fuzz runs, 301 s per target, no finding: drive 2.2 M, SD 29.5 k, v2 5.6 M,
  Greaseweazle 10.2 M, parsers 13.3 M executions. Coverage with the new target:
  95.9 % lines / 85.9 % branches over 21 modules (was 93.9 / 82.4 over 20);
  `ufi_dump.c` 93.0 / 80.4 (was 87.9 / 75.2) with `search_track` and `ds_sector` fully,
  `probe_double_step` and `recapture` partly reached; `ufi_drive.c` 97.5 / 90.6, the
  profile file code and the presets fully. 35 of 56 modules remain unmeasured (HAL-bound).
- Format 7, Apple II 5.25-inch 16-sector 6-and-2 GCR (`apple_cell` in `ufi_mfm.c`):
  D5 AA 96 address field with 4-and-4 volume/track/sector/XOR checksum and DE AA,
  D5 AA AD data field with 342 nibbles, running XOR, checksum nibble and DE AA; 256-byte
  sectors reported in physical order as `{track, 0, sector, 1}`, volume in `disk_id`.
  Explicit selection only (no auto-detection candidate, so report layouts stay).
  Native test `test_apple_gcr.py` with a synthetic track generator (`apple_fixture.h`):
  16 sectors with data compare, header/data/epilogue faults, 6 % speed error.
  Prompted by the Floppy Emu Apple II teardown; nothing was taken from the binary.
- Format 7 decoder options (ANALYSIS_JOB op 33, 12 bytes: address/data prologue,
  address/data epilogue, flags): keep headers with a bad 4-and-4 checksum, ignore
  epilogues, no track/sector limit, accept data with a wrong checksum nibble, accept data
  fields without a header. Per-sector diagnostic bits 0x20/0x40/0x80 (header checksum,
  data checksum, epilogue) travel into the quality report; the error counters keep
  counting under tolerant flags. Host `ufi apple-gcr` / `apple_gcr_options()`. Native
  test covers protection marks (`D4 AA 96` / `D5 AA B5` / `DE AB`), each flag and the
  validation; pytest covers the wrapper.
- Host `ufi read-dsk` (`ufi_host/apple.py`): reads each track with READ, decodes the
  flux on the host with the same 6-and-2 rules, marks and flags as format 7 (nibble
  latch on the first 1 bit, 4 us cells through the shared PLL), merges revolutions
  (checksum-clean sector wins) and writes `.dsk` (DOS 3.3 order) or `.po` (ProDOS
  order, chosen by extension or `--order`); missing and fault-tolerated sectors are
  listed. Tests: synthetic tracks, both orders, faults, tolerance flags, protection
  marks, 6 % speed error, revolution merge.
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

## Decoder tolerance flags for every format — 2026-10-17

The Apple II tolerance flags (op 33) now exist for the IBM MFM, FM, HP MMFM, Amiga,
Commodore and Macintosh decoders as `mfm_decoder_t.tolerance`, set through
ANALYSIS_JOB op 34 (`[34]` reads, `[34, 1, flags u16]` sets, same bit values: keep bad
header, ignore epilogues, any track, data without checksum, data without header). Only
the analysis decoders take the flags (`quality_begin`); dump, write verify and format
detection stay strict. Every decoder now sets the same per-sector finding bits in the
quality report flag byte: 0x08 data field accepted without a valid header (id 255/255),
0x20 header CRC/checksum failed, 0x40 data CRC/checksum failed, 0x80 epilogue mismatch
(Macintosh and Apple). The error counters keep counting under tolerance, and the quality
block carries the op 34 and op 33 flags that were in effect at offsets 60 and 62, so a
report can later be classified as strict or tolerant. Headerless data fields take the
size code of the last ID field seen (valid or not), 512 bytes otherwise. New native test
`test_tolerance.py` builds one faulty track per format with its own generators (IBM MFM
and Amiga written for the test, Commodore from the GCR tables, FM/HP and Macintosh from
the existing fixtures) and checks every flag, the finding bits and the counters; op 34 is
covered in `test_analysis_job.py`, the report fields in `test_quality.c`. Host:
`Device2.decoder_tolerance()`, `ufi tolerance [FLAGS|reset]`, `SECTOR_FLAGS` names for the
report flag byte, FakeV2 answers op 34. Protocol doc 5.8, op table, sector entry and
quality header rows. Requested by the UFT integration session: active flags in every
report (done, offsets 60/62) and a "tolerance changed the outcome" marker (derivable:
sector in `seen` with one of bits 3/5/6/7, documented as the provenance rule in 5.8).

## SuperCard Pro updater and firmware container taken apart — 2026-10-17

`docs/REA_SuperCard_Pro_Analyse_2026-10-17.md`: the VB6 updater (Ghidra, VB6 prologue
scan for the event handlers) decrypts `SCP.fwu` with a key stream made from the file's
own filler bytes (4 bytes per plaintext byte, accumulator 0x55), the plaintext is an
8-byte `FW` header plus a 64 KiB dsPIC33FJ256GP210A image in the bootloader's wire form
(low word rotated by 3, high word = rotl6(raw low) XOR opcode << 6; erased `FFFFFF` =
`C03FFFFF`). Decoded and analysed with Ghidra's dsPIC33F language: bootloader commands
`SI`/`RF`/`WF`/`EX`/`R!` with 512-instruction pages, flux capture by Input Capture 5 on a
40 MHz timer (25 ns, one interrupt per transition, 0x0000 overflow cells written by the
Timer 3 overflow interrupt), index by change notification with a 32-bit timer, flux
write by Output Compare 8 with a 750 ns minimum cell and 150 µs chunking, external byte
SRAM on GPIO. Nothing was copied; the document compares the method with UFI's DMA path.

## Apple II nibble statistics — 2026-10-17

The format 7 decoder now frames nibbles in its search state (a nibble starts with its
first 1 bit, an FF followed by trailing zero cells is a 10-bit self-sync nibble) and
keeps `apple_stats_t` (178 bytes, ANALYSIS_JOB op 35): per revolution the number of
nibbles, self-sync nibbles, address prologues, accepted data prologues and orphan
headers (valid header without a data field before the next header or 4000 cells); per
physical sector the volume, the sync run before the address prologue, the header-to-data
distance, the sync run before the data prologue, the finding flags of the last data field
and the revolution mask. Counted only by the profile-0 decoder, reset by `quality_init`
with format 7, revolution set by `quality_revolution`. Native test with the synthetic
track (383 nibbles and 20 syncs per sector, orphan header fixture `apl_header_only`),
op 35 in `test_analysis_job.py`; host `apple_gcr_stats()`, `ufi apple-stats`, FakeV2 op
35 with a settable report; protocol doc 5.7 and op table.

## ANALYZE on quarter tracks — 2026-10-17

ANALYZE 0x25 and the analysis job start (op 1, byte 25) take a position byte in the side
field: `side | 0x80 | quarter << 4` selects an Apple quarter track (bit 7 = quarter mode,
quarter 0-3 in bits 4-5; other bits or side values above 1 are refused, non-Apple drives
get BAD_ARGS). The track byte stays the whole track. The capture context keeps the position
(`g_capture.current_quarter`), the quality block reports it at offset 19 (previously
reserved), sector identities stay the track numbers read from the disk. New
`ufi_analyze_track_q()`, `quality_options_t.side/position`, `quality_t.position`,
`ufi_capture_start_q()` from the job. Tests: option parsing in `test_quality.c`, the job
harness stubs the Apple query and the quarter-aware capture start; host `analyze(...,
quarter=)` and `ufi analyze -q`, FakeV2 validates the side byte.

## 13-sector 5-and-3, loadable 6-and-2 table, nibble statistics schema 2 — 2026-10-17

Format 7 reads DOS 3.2 13-sector data fields with op 33 flag 0x20 (410 nibbles from the
fixed 32-symbol alphabet, low-bit block of 154 then high-bit block of 256, byte 255 split,
XOR chain and checksum nibble as in 6-and-2; layout taken from the documented format and
checked against an independent encoder in the test fixture). Op 36 reads or loads the
6-and-2 symbol table (64 distinct symbols with bit 7, standard table restores; reverse
lookup table rebuilt on load, Macintosh decoder untouched). The nibble statistics report
grew to schema 2 (274 bytes, first 178 unchanged): per revolution the trailing-zero
histogram of self-sync nibbles (1, 2, 3 or more zero cells, the latter being timing bits),
the four most frequent D5 AA xx third nibbles that are neither configured prologue
(finds foreign marks without guessing), and per sector the cell position of the address
prologue since the revolution start. Native tests: 13-sector track with a data checksum
fault, rotated alphabet breaking the standard track, positions, trailing histogram and
variant counts; op 36 and the 274-byte op 35 in `test_analysis_job.py`. Host:
`apple_gcr_table()`, `ufi apple-table`, flag name `five_and_three`, schema 2 fields in
`apple_gcr_stats()` and `ufi apple-stats`; FakeV2 ops 35/36. Protocol doc 5.7 and op table.

## Session write lock — 2026-10-17

First of the UFT forensic wishes (P1.10). ANALYSIS_JOB op 37 reads (`[37]` -> schema 1,
state bit0 session lock, bit1 WRITE-LOCK jumper) or engages (`[37, 1]`) a write lock that
lives in RAM and ends only with an MCU reset; there is no disengage over USB, every other
payload is BAD_ARGS, and USB personality switches, protocol resets and ABORT keep it.
New status 22 `SOFT_WRITE_LOCK` so a tool can tell the session lock from the disk tab or
jumper (12). The v2 dispatcher refuses at acceptance, before any resource is taken: WRITE,
GUARDED_WRITE, ERASE, PATTERN, FORMAT, COPY_START, WRITE_START, IEC_BLOCK op 1, IEC_MEM /
IEC_TARGET ops 0/2/3; a WRITE accepted before the lock ends with END status 22 at the
start check. `ufi_board_write_locked()` now includes the lock, so GW-mode writes (GW
write-protect ack), the MSC floppy mode and the standalone button write are blocked like
the jumper; `board_status.flags` bit7 shows the lock, bit0 stays the jumper
(`ufi_board_jumper_locked()`). Native tests: op 37 in `test_analysis_job.py`, the board
state and the exhaustive refusal predicate in `test_native_requests.py`, the start check
in the write regression. Host: `write_lock(engage=)`, `ufi write-lock [engage]`,
`BoardStatus.session_locked`, status text 22; FakeV2 models op 37, bit7 and the
refusals. Protocol doc 4.6, status table, op table, board_status flags.

## Generation block in READ/WRITE END frames — 2026-10-17

UFT wish P1.3. Behind the existing END payload of READ 0x20 (`revs`) and WRITE 0x22
(`verify_level`, `clamped`; also after a GUARDED_WRITE start) the firmware appends 17
bytes: schema 1, configuration generation, the media generation the operation was bound
to at its start, the media generation at the END frame, and the capture id op 8 / the
quality report use (0 for WRITE). READ END is 18 bytes, WRITE END 22; old parsers keep
reading the front. The capture start keeps its bound token (`ufi_capture_media_token()`),
the write path exposes its own (`ufi_write_media_token()`), `ufi_v2_generations()` packs
the block and the job trailer buffer grew to hold it; the READ end after ABORT now goes
through `ufi_usb_send_read_done()` so it carries the block too. Native test of the block
layout in `test_native_requests.py`. Host: `generation_block()`, `Capture.generations`,
`WriteResult.generations`, READ END accepts >= 1 byte; FakeV2 appends the block with
`config_gen`/`media_gen`/`capture_id` attributes. Protocol doc 4.5 (new subsection),
command table rows 0x20/0x22, READ and WRITE bullets in 3.3.

## Media-bound READ start — 2026-10-17

UFT wish P1.4, same pattern as GUARDED_WRITE: GUARDED_READ 0x28 carries `expected_config,
expected_media` (u32 each, nonzero) in front of the unchanged READ payload; a stale
configuration answers 16, another or no medium 21, and nothing is sent before acceptance.
After the capture start the medium is checked again and a change seen in between aborts
the capture with 21, so a medium swapped between check and start never becomes the
baseline. The reply carries 0x28, DATA and END stay 0x20 with the same sequence, the END
carries the generation block. Discovery: op 25 is schema 2 with 20 bytes, bytes 16..19
name read start 0x28, read data/end 0x20, prefix 8, reserved 0; schema 1 parsers keep the
first 16 bytes. Native tests: guarded start rejections, prefix-only request, READ flag
check behind the prefix, seek failure, abort on a change after the start
(`test_native_requests.py`), op 25 schema 2 (`test_analysis_job.py`). Host:
`read_track(expected_config=, expected_media=)`, `guarded_starts()` (schema 1 and 2);
new `GuardedDevice(FakeV2)` with `config_gen`, `media_gen`, `bump_media()`, op 25,
0x27/0x28 checks and the accepted-start counter, for UFT's bridge. Protocol doc 4.5
(discovery schema 2, GUARDED_READ), command table row 0x28.

## Erase the remainder of the track — 2026-10-17

UFT wish P1.11. The WRITE flags byte is full (bit 7 = EXPECT_SET), so the option travels
as one optional trailing `flags2` byte behind every flag-driven field, announced by
WRITE_LIMITS flags bit 3; other flags2 bits are BAD_ARGS. With bit 0 the write path does
not drop WGATE after the last interval: WDATA goes idle (the ERASE 0x23 signal), the timer
and DMA stop, and the next index pulse finishes the write; the END payload carries
`erased_ticks` u32 (flux-timer ticks from the last interval to the index, 0 without the
option or when the index cut the track, status 19 as before) behind the generation block,
26 bytes in all. Apple Disk II drives have no index on that path and answer NOT_IMPL; the
GW path never sets the option; an abort during the tail releases the gate like any other.
Native tests: the gate stays on through `end_of_data()`, the index closes it and reports
the tail, abort during the tail, early index still the cut-track error (write regression);
flags2 parsing behind every optional field, two trailing bytes, unknown bits and the
Apple refusal (`test_quality_runtime.py` write flags). Host: `write_track(erase_rest=)`
checks WRITE_LIMITS bit 3 first, `WriteResult.erased_ticks`, `ufi write --erase-rest`,
`ufi write-limits` names the bit; FakeV2 parses flags2, answers bit 3 for 1.18 and reports
a tail. Protocol doc: command table rows 0x08 and 0x22 (bit 7 and flags2 now listed), 3.3
bullet, 4.5 END sizes.

## FakeV2 models DESCRIBE and the firmware read-back — 2026-10-17

Last of the UFT bridge wishes; no protocol change. The reference double answers DESCRIBE
(schema 1, flags 3 = firmware read-back and extended backend, store 2^21 words) so a
host can take the extended write path against it without monkeypatching, and a WRITE
with flags 0x21 or 0x61 models the firmware read-back of 5.2: fewer than
`VERIFY_MIN_INTERVALS` (1000) intervals is a source without reference sectors and ends
with status 16 before WGATE, longer sources end with verify level 3; the generation block
and `erased_ticks` follow in both cases. Host test `test_firmware_verify_modelled`.
UFT's bridge instantiates `GuardedDevice` for the media-bound starts and may lower the
threshold per run.

## Flag masks in one answer — 2026-10-17

First item of UFT's second list. ANALYSIS_JOB op 38 returns 16 bytes with every request
mask the parsers accept: READ flags, WRITE flags, WRITE flags2, analysis flags, highest
format hint, op 33 Apple mask, op 34 tolerance mask, QUALITY feature mask, diagnostic
flags mask and the analysis position byte. The READ/WRITE/flags2/analysis/position masks
moved into `ufi_capabilities.h` and the parsers check against those same constants, so the
answer cannot drift from the code; the other values are the existing module masks.
Read-only, never BUSY, payload after the op byte is BAD_ARGS. Native test in
`test_analysis_job.py`; host `flag_masks()`, `ufi masks`, FakeV2 op 38 with a test that
the host flag-name tables fit the masks. Protocol doc 4.7 and op table.

## Reason of the last refusal — 2026-10-17

Second item of UFT's list. The reply frame stays as it is; the v2 layer keeps the last
refusal instead: the READ/GUARDED_READ/WRITE/GUARDED_WRITE parsers and the ANALYZE option
check note a field code and a detail while they refuse (`refuse()`), `request()` clears
the note before parsing and stamps command, sequence and status when a 16 or 21 goes out,
and ANALYSIS_JOB op 39 returns the 6 bytes (schema, cmd, seq, status, field, detail; cmd 0
= none since reset). Thirteen field codes shared by the commands (length with the required
minimum, unknown flag bits, quarter without Apple, period, sectors, scale, index_ticks,
expected set, flags2 bits, configuration and media generation, analysis options as a
whole). Native tests check the notes for the READ shapes, the guarded prefix, every WRITE
optional field, flags2 and the EXPECT_SET fields, plus op 39 itself. Host
`last_refusal()` with field names, `ufi refusal`; FakeV2 and GuardedDevice keep a note for
their own length/flags2/generation refusals. Protocol doc 4.8 and op table.

## Per-sector verify report — 2026-10-17

Third item of UFT's list. The read-back matcher (`verify_match_t`) now keeps a state per
reference sector (missing / ok / CRC error / different content; ok wins across the three
decoder profiles, then different, then CRC error), and the quality runtime writes the
report of the last verified WRITE: schema, reference format, count, flags (overflow,
conflicting duplicates, level 3 reached, read-back missing), the track byte as sent and
the side, then c, h, r, n, state per sector; ANALYSIS_JOB op 40 returns it (6 + 5 n bytes,
max 326). The header with track/side is written when a WRITE with the verify bit is
accepted (flags bit3 set until the matcher fills the rest), a WRITE without verify leaves
the report alone. Native tests: every sector ok / every sector different / every sector
missing in the verify unit test, header and the single changed sector in the quality
runtime, op 40 and the header in the analysis job harness, the begin call in the write
flags harness. Host `verify_report()` with names, `ufi verify-report` (lists state != ok as
CHRN + reason); FakeV2 derives the report from its 0x21 model. Protocol doc 4.9, op table.

## Speed check before writing — 2026-10-17

Fourth item of UFT's list. ANALYSIS_JOB op 41 reads or sets a tolerance in per mille of
the source index period (0 = off, max 500, BUSY while an operation runs) and reports the
last measurement (measured and source index ticks). With a tolerance, every native WRITE
that carries the source period (flags bit 6, which requires the 0x21 path) measures the
drive's own index period with the FORMAT measurement (`ufi_diag_rpm`, two revolutions)
after the data arrived and before seek and WGATE; outside the tolerance the write ends
with new status 23 `SPEED_MISMATCH` and nothing is written. No bit 6, GW mode, Apple
drives without sync sensor or a failed measurement skip the check. Native tests: no
measurement without tolerance or source, within/outside tolerance both directions through
the service path, failed measurement proceeds, GW path never measures (write regression);
op 41 range, busy and reply layout (analysis job harness). Host `speed_check()`,
`ufi speed-check [PERMILLE|off]`, status text 23; FakeV2 op 41 with its own `index_ticks`
and status 23. Protocol doc 4.10, status and op tables.

## Weak windows as counts — 2026-10-17

Fifth item of UFT's list. The weak-window loop of ANALYZE moved into its own function
(`weak_windows()`, now unit-testable) and keeps the max - min transition count per 1/64
revolution (capped at 255) beside the unchanged threshold-3 mask; ANALYSIS_JOB op 42
returns 68 bytes: schema, revolutions compared (0 = nothing to compare), track, side/
position byte as sent, 64 counts. Belongs to the last ANALYZE 0x25 only. Native test with
three synthetic revolutions (raw differences 1/2/6, only the 6 reaches the mask, header,
single revolution, cancel) in `test_native_requests.py`, op 42 in the analysis job
harness. Host `weak_windows()`, `ufi weak [--min N]` with a heat strip; FakeV2 op 42
derived from its ANALYZE answer (mask 0x11). Protocol doc 4.11 and op table.

## WRITE start offset after the index — 2026-10-17

Sixth item of UFT's list. flags2 bit1 announces `start_offset_ticks` u32 behind the flags2
byte (1..275 000 000; 0, out of range or missing bytes -> 16 with op 39 field 14; Apple
without sync sensor -> 11); WRITE_LIMITS flags bit4 announces the option, the op 38 flags2
mask is 0x03. The index handler no longer opens the gate itself when an offset is set: it
arms TIM2 compare channel 4 at index + offset, the flux-timer ISR dispatches the compare to
`ufi_write_delayed_start()`, which opens the gate and starts the write timer
(`start_now()`, shared with the immediate start and the Apple sensor start). Further index
pulses while waiting are ignored, ABORT clears the pending compare, the cut-track stop and
the erase remainder work from the delayed start. WRITE END grows to 30 bytes with
`start_offset_applied` u32 = measured gate-open time minus index time (compare latency
included), 0 without the option. Native tests: arm/ignore/compare/late-compare/abort in the
write regression, parsing of flags2 bit1 with range, missing bytes, Apple and trailing
bytes in the write flags harness. Host `write_track(start_offset=)` with the WRITE_LIMITS
check, `WriteResult.start_offset_applied`, `ufi write --start-offset`; FakeV2 parses the
field and echoes it plus 7 ticks. Protocol doc rows 0x08/0x22, 3.3 bullet, 4.5 END sizes,
4.7/4.8 tables; acceptance plan rows (gate edge against the index on the analyser, long
wait, abort).

## Extended analysis flags without DETAIL — decided against, 2026-10-17

Seventh item of UFT's list, checked and left as it is. ANALYZE runs the quality pass only
with a nonzero flag byte, and the TIMING/GAPS/GLITCHES/POSITIONS/SYNC/WINDOWS counters are
computed inside that pass, interleaved with the sector decoder DETAIL stands for. A
"without DETAIL" variant would not be cheaper by dropping a flag; it would be a second pass
mode with the decoder switched off that reports only the QX block, saving the sector decode
and the 1824-byte QD report, not the capture. Kept as a possible later feature ("light
extended pass"), to be specified separately if UFT wants it. Open from the same list, both
pending the owner's word: bit 6 (source index period) on a WRITE without firmware verify,
and the light extended pass.

## Source index period without firmware verify, FakeV2 ops 22/23 — 2026-10-17

Eighth item of UFT's list, approved by the owner together with a FakeV2 request. WRITE
flags bit 6 (`index_ticks`) now needs only the extended path (bit 5, `0x60`); before it
needed verify as well (`0x21`). Nothing else moved: with verify the period still feeds the
reference decoder, without verify it serves the speed check of op 41 alone, and with
tolerance 0 it is accepted and ignored. Bit 7 (EXPECT_SET) keeps its `0x21` rule. The parser
refuses bit 6 without bit 5 as before (status 16, op 39 field 8 reason 1). WRITE_LIMITS
flags gained bit 5 (`0x3F`) so a host can tell the relaxed parser from the earlier
1.18-dev builds; `write_track()` sends `index_ticks` with every extended write when bit 5 is
announced and drops it silently on older firmware when no firmware verify was asked for.
Native write-flags harness: `0x60` accepted with the period and without verify, `0x41`
refused. Host: `test_speed_check` covers `0x60` both inside and outside the tolerance and the
fallback without bit 5.

FakeV2 (`software/ufi_host/tests/test_protocol2.py`) answers ANALYSIS_JOB op 22 with the
224-byte capability matrix a v0.7 board with a flux drive reports (head 1, 6, 32, 32, the
generation, flags 3, mask 63, 275 MHz, drive id, op numbers 23..32, six rows with
hardware_qualified 0) and op 23 / op 23 + expected generation with the 24-byte configuration
report, PROFILE_CHANGED (4) only against a stale expectation. Its configuration generation
moves on SELECT and on accepted op 10 / op 18 settings, as the firmware's does, so UFT can
provoke PROFILE_CHANGED and the generation block against the fake. Both reports decode with
`capabilities.decode_capabilities/decode_configuration`; test
`test_capability_matrix_and_configuration_against_fake`. Host-side only, no protocol change.
UFT's list is closed with this; nothing is parked.

## UFT's third list, items 6 and 5: documentation gaps, USB serial — 2026-10-18

UFT sent a third list of seven suggestions (owner asked for it); the owner released it in the
order documentation first. Item 6, read and confirmed: the op table in 6.1 skipped ops 20-32
(they were described only in 1.3, 4.5, 5.5, 5.6, 6.2 and 6.3), the `capabilities.py` docstring
named ops 22-24 while the module also builds op 26, and the matrix table in 1.3 listed format
7 (Apple II GCR) although schema 1 carries exactly six rows. All three corrected: rows 20-32
added to the table with request lengths and section references, docstring names 22, 23, 24
and 26, the matrix row says schema 1 has no Apple row (a schema 2 with that row is the next
item). Item 5, USB serial across personalities: checked in the code, one serial string
descriptor fed from the MCU unique ID serves every PID 4F54-4F58, so the serial does not
change over a mode switch; written into the USB section and into the acceptance plan as a
bench check (read the descriptor in each mode). Documentation and a docstring only, no
protocol or firmware change. Remaining from the list: 7 (host recognition fetcher), 1+3
(matrix schema 2 with Apple row and format-independent raw-flux/IEC entries), 4 (which bench
measurement sets which qualification bit, as a table); 2 (configuration change as an event)
declined, the END generation block already carries the configuration generation for a local
comparison.

## Host reference client for analysis jobs and the recognition block — 2026-10-18

Item 7 of UFT's third list. `recognition.py` decoded result section 15 but nothing in the host
fetched it: there was no client for ANALYSIS_JOB ops 1-4 at all. Added to `protocol2.py`:
`AnalysisStatus` (the 64-byte status of op 1/2/4 as a dataclass with `done` and `state_name`),
`analysis_start()` (op 1: tool-chosen job id, JOB_* flags, attempts, budget, expected sector
mask with C/H/N, the appended ANALYZE request always with DETAIL as the firmware requires),
`analysis_status()`, `analysis_abort()`, `analysis_wait()` (op 2 polling), `analysis_section()`
(op 3 in pages of up to 1024 bytes, each page's schema/section/job/generation/offset/total and
payload CRC32 checked, ValueError on a mismatch), `recognition_discovery()` (op 21) and
`recognition()` (section 15 decoded, plus `flag_names`). `recognition.py` gained the flag,
format and decision name tables. CLI `ufi recognize TRACK SIDE [--expect MASK --chn C H N
--attempts N --budget MS --retry]` runs one job and prints status, format, flags, causes and
the masks. FakeV2 models ops 1/2/3/4/21: a started job ends at once with sectors
1..verify_sectors seen, missing expected sectors give status 6 and cause missing_sectors,
op 3 serves sections 2 and 15 with the real header and CRC (a test switch corrupts the CRC to
prove the host check), foreign job ids and generations are refused. Test
`test_analysis_job_client_and_recognition`; protocol doc 5.6 and README name the reference
API. No protocol or firmware change. Remaining from the list: 1+3 (matrix schema 2), 4
(qualification table).

## Capability matrix schema 2: raw flux, Apple II GCR and IEC rows — 2026-10-18

Items 1 and 3 of UFT's third list. Op 22 answered schema 1 with six format rows, so a tool
that wanted a format-independent answer for raw flux capture or raw-flux write had to AND
all six rows, and Apple II GCR (format 7) and the IEC block path had no row at all. Schema 2
keeps the 32-byte head and the 32-byte row layout, sets byte 0 = 2 and byte 1 = 9 rows
(320 bytes), rows in ascending id order with the id in byte 0: 0 raw flux (implemented 5 =
capture and raw write, eligible 5 with a flux port and no line conflict, offline 5, both
backends, rate mask 0, the usual limits), 1..6 unchanged, 7 Apple II GCR (implemented 7, no
content verify since the verify reference has no Apple format, eligible 7 only with the Apple
port selected else 2, offline 2 via the MOOF corpus, classic backend only, mask 0x0001), 8 IEC
block path (implemented 13 = read blocks, write blocks, read-back verify, eligible 13 only
with an IEC drive selected, offline 13, no backends, prerequisites 5, everything else 0). Port
report drives the admission: flux bit, density bit (Apple has none), drive id without the
flux bit = IEC. hardware_qualified stays 0 everywhere; the bench table (item 4) says which
measurement will set which bit. Native test rewritten for the nine rows and the five port
situations (Shugart, Apple, line conflict, nothing selected, IEC, busy); analysis job harness
expects 320 bytes. Host decode_capabilities() reads schema 1 and 2 from the head, finds rows
by id, returns `rows` (format rows 1..7), `raw_flux` and `iec`; FakeV2 answers schema 2;
tests for both schemas. UFT (Sitzung 1) agreed to the layout beforehand and adapts its
parser to both schemas once the hash is known. Protocol doc 1.3 (tables, row paragraph,
masks), op table 6.1; README. Remaining from the list: item 4 (qualification table).

## Which bench measurement sets which qualification bit — 2026-10-18

Item 4 of UFT's third list, documentation only. `hardware_qualified` is 0 in every matrix
row and the head flags CAPABILITY_UNCONFIRMED / WRITE_MODE_UNQUALIFIED are always set,
because nothing was measured on the device yet. The acceptance plan gained section 4: per
row and path bit (raw flux capture / raw write, format rows capture / decoder / raw write /
content verify, Apple raw write, IEC read / write / verify, the two head flags) the
measurement from sections 1-3 that qualifies it and the pass criterion, with UFT's `.ufec`
transport capture or the `ufi` log plus raw file as the evidence to archive. The firmware
side (storing set bits in UFI.CFG and showing them in op 22) is explicitly not built; the
table fixes the procedure so a later bit means something. What a measurement does not
prove is listed as well (decoder rates are not write rates, loopback is not magnetic
content, one format or drive does not vouch for another). The old backlog section moved
to 5. This closes UFT's third list: items 6, 5, 7, 1+3, 4 done, item 2 declined (the END
generation block already carries the configuration generation).

## Provenance block, ANALYSIS_JOB op 43 — 2026-10-18

First item of the owner's fourth list (what the firmware can do for UFT's modules: forensic
provenance first). `uft_fundus_provenance` needs board, firmware, drive setting, generations
and the capture identity in one record; the pieces existed spread over INFO, STATUS, op 18,
op 23, the END generation block and the capture context. Op 43 returns them as one 96-byte
schema 1 block: board revision and BOARD_ID mV, MCU unique id / device id / silicon revision,
firmware major.minor with git revision text and dirty flag and __DATE__, flux clock,
configuration generation, current media generation, capture id and the media generation it
was bound to, the last index period the flux timer saw, the last op 41 measurement, selected
drive with rpm setting and profile source, uptime, plus lock and apple_sync flags. Pure query,
answered during dump and jobs. The SHA-256 of the flashed image stays with the host (it has the
file). `ufi_provenance_block()` lives in ufi_v2.c next to the INFO builder; the analysis job
harness stubs it and checks op 43 size and refusal of a payload. Host `provenance()`,
`ufi provenance [--json]` (JSON meant to be stored next to the raw file), FakeV2 op 43, test.
Protocol doc 4.12 and op table, README. Open from the same list: READ flag bit 3 to append the
block to the READ END (asked by the owner as "flag-driven"), re-seek flag for multi-reads,
half-track positioning for Shugart, weak-bit position list, precomp profile per write.

## Provenance block bound to a capture: READ flag bit 3 — 2026-10-18

The owner asked whether the provenance block can be flag-driven. READ 0x20 and GUARDED_READ
0x28 gained flags bit 3 (PROVENANCE, op 38 READ mask 0x0F): the END payload then carries the
96-byte op 43 block behind the generation block (114 bytes in all; 18 without the bit), so the
raw data and the device state it came from arrive in one transaction and the capture id in the
block is the one of the capture that just ended. The flag is remembered per accepted READ and
cleared by the next one; error-status and ABORT ENDs carry the block too when they carry
payload, since the request fixes the layout. Native READ harness: bit 3 accepted and recorded,
bit 4 refused with op 39 field 2 = 16, a plain READ clears the request. Host:
`read_track(provenance=True)` -> `Capture.provenance`, `decode_provenance()` shared with
`provenance()`, `READ_PROVENANCE`, `ufi read --provenance`; FakeV2 honours the bit on READ and
GUARDED_READ, refuses bits above 0x0F, reports 0x0F in op 38; test with the 114-byte END, the
plain END, the guarded path and the CLI line. Protocol doc: command row 0x20, 4.5 END sizes,
4.7 mask row, 4.12 paragraph. AXI reserve moves by 8 bytes (END buffer), still above the
32 KiB mark. Open from the fourth list: re-seek flag for multi-reads, half-track positioning
for Shugart, weak-bit position list, precomp profile per write.

## Independent head positioning between analysis attempts: job flag RESEEK — 2026-10-18

Second item of the owner's fourth list. UFT's recovery fusion and OTDR modules want physically
independent reads of a track; retries of an analysis job so far captured again at the head
position the first seek had settled on, which only adds revolutions of the same reading.
ANALYSIS_JOB op 1 gained job flag RESEEK 0x40 (mask 0x7F, op 0 byte 4 now 127): before every
retry capture (attempt 2 and later) the job seeks one track outward (inward at track 0, same
quarter) and the capture's own seek brings the head back, so the position is settled afresh.
The first attempt is untouched; a failed re-seek ends the job like a failed capture start
(status 3, CAUSE_POSITION in that attempt's assessment and history). Multitrack templates carry
the flag unchanged. Native harness: no re-seek on attempt 1, one per retry with the outward
track and quarter, none without the flag, re-seek failure path, bit 7 refused, op 0 mask 127.
Host JOB_RESEEK / JOB_FLAG_MASK, `ufi recognize --reseek`; FakeV2 accepts 0x40 and counts one
re-seek per retry in `reseeks`; test. Protocol doc: op 1 flag table, op 0 row. Open from the
list: half-track positioning for Shugart, weak-bit position list, precomp profile per write.

## Half-track positioning on Shugart drives — 2026-10-18

Third item of the owner's fourth list (protection analysis wants positions between tracks).
A PC stepper has no position between its steps, so a half track exists only where the profile
double-steps (48-tpi medium in a 96-tpi drive, `double_step` = 1): logical track n at physical
2n, the half track at 2n+1. The existing quarter form is reused instead of a new flag: READ /
WRITE flags bit 2 with track = 4n + q, ANALYZE and op 1 side byte 0x80 | q << 4, SEEK with the
quarter byte. On a non-Apple drive the form is accepted only for q = 2 and only with the
double-step profile; `ufi_drive_quarter_ok()` holds that rule, `ufi_drive_seek_q()` steps to
physical 2n+1 through the Shugart step sequence factored out as `seek_physical()`, and the
parsers refuse everything else with 16 and op 39 field 3 reason 2 ("half track needs the
double-step profile", documented verbatim at UFT's request; reason 1 stays for an invalid
quarter on the Apple port). Reports keep the quarter form as sent. Native harnesses: READ
(reason 2, 17.5 accepted with the profile, 17.25 refused), WRITE flags (same), analysis job op
1 check, media runtime seek extraction. Host docstrings and the quarter help name the rule;
no wire change for Apple, nothing moves for a tool that never sends the quarter form on
Shugart. Acceptance plan row with the step count at the logic analyser. Open from the list:
weak-bit position list, precomp profile per write.

## Fine weak-bit positions, ANALYSIS_JOB op 44 — 2026-10-18

Fourth item of the owner's fourth list. Op 42 counts per 1/64 revolution, too coarse to place
unstable transitions for UFT's protection and weak-bit work. ANALYZE now additionally sweeps
every revolution once with a running window cursor and keeps, per 1/1024 revolution, the
minimum and maximum transition count over the revolutions (2 KiB in DTCM, which keeps the AXI
reserve; counts capped at 255; samples past the index dropped as in op 42). Op 44 lists on
request the windows whose max-min reaches a threshold (default 2, `[44, t]` for 1..255, 0
refused), at most 64 entries with a truncation flag, window ascending, each with the
difference and the minimum: 10 + 4n bytes. Pure query of the last ANALYZE, empty with one
revolution. Native weak harness: fine pass on the synthetic revolutions (differences only
inside the coarse windows that were made to differ, window 17 sums to its 0/3/6 gain,
thresholds 7 and 0 list nothing, single revolution, cancel); analysis job harness checks the
default and explicit threshold and the refusal. Host `weak_positions(threshold)`,
`ufi weak --positions [--threshold N]`, FakeV2 op 44 with three fixed windows after an
ANALYZE, test. Protocol doc 4.13 and op table. Also in this commit: the ANALYZE quarter
check now uses `ufi_drive_quarter_ok()` like READ/WRITE (the Shugart half track of a34aed1
was still refused there). Open from the list: precomp profile per write.

## Write precompensation per write and per zone — 2026-10-18

Last item of the owner's fourth list. Precompensation came from TIMING `precomp_ns`
(0xFFFF auto by data rate, 0 off, else ns) and a fixed track rule inside the codec (< 40
none, 40..60 half, > 60 full) that counted the logical track as sent; under double_step
that was the wrong radius, so the rule now runs on the physical track (logical n at 2n).
The selection moved from the codec into `ufi_write_precomp_select()` in ufi_write.c and the
codec got `precomp_fixed` (amount as given, no rule). Two new ways, classic path only
(extended intervals are host-timed; flags2 bit 2 with bit 5 -> 11): per write, WRITE/GUARDED_
WRITE flags2 bit 2 followed by precomp_ns u16 (0 = off on any track, 1..2000 as given;
WRITE_LIMITS bit 6 announces it, flags2 mask 0x07, op 39 field 15 reasons 1 missing / 2
range), which lets a host that compensated itself send 0 and avoid the double shift UFT
pointed out; and a session-local zone profile through op 45 ([45] reads, [45, 0] clears,
[45, 1, n, n x (from_physical_track u8, ns u16)] sets 1..4 ascending zones, the first at 0,
ns <= 2000; set is BUSY during an operation, read never). The 19-byte reply names the source
of the last write (0 TIMING/auto with the rule, 1 profile, 2 per-write override), the zones,
the last applied ns and the last logical and physical track, so a write report can say
"Board: off / profile / auto" literally. Precedence: override, zone, TIMING/auto. Native
tests: write-flags harness (bit 2 values, missing bytes, 2001, extended -> 11, all three
flags2 fields together, bit 3 refused), analysis job harness (op 45 read/set/length/zone
refusal/clear/busy), codec check for the fixed amount on every track and 0 = off above 60,
evidence runtime stub. Host write_track(precomp_ns=), precomp_profile(), ufi write
--precomp NS, ufi precomp [--zones FROM:NS,... | --clear]; FakeV2 flags2 bit 2 and op 45;
tests. Protocol doc 4.14, rows 0x08/0x22, 4.7 mask, op 39 field 15, op table. That closes
the owner's fourth list: provenance block, READ provenance flag, RESEEK, Shugart half track,
fine weak positions, precompensation.

## Provenance block: drive supply state in the reserve — 2026-10-18

Owner's fifth list (further firmware support for UFT), item 3 taken first because it is the
cheapest: the twelve reserve bytes of the op 43 block (also in the READ END with flag bit 3)
now carry the drive supply state at the time of the record: 5 V and 12 V currents in mA,
the `power` byte (which supplies are on) and the board_status flags (jumper, overcurrent, SD,
buttons, safe mode, session lock). A forensic record can then tell a capture under a sagging
supply or after an overcurrent trip from a clean one. No MCU temperature: the internal sensor
channel is not configured on this board, documented as such; six bytes stay reserved. Layout
and size unchanged (96 bytes, schema 1), so UFT's archive packaging of op 43 keeps working and
gains fields. Host decode_provenance() exposes i5_ma, i12_ma, power, board_flags; `ufi
provenance` prints a supplies line; FakeV2 fills the fields; test. Item 1 of that list (stream
the flux of a finished analysis job) turned out deeper than estimated: the v2 DATA path
encodes live from the ring store with absolute timestamps, a finished job capture sits in
per-revolution arrays and needs its own v2 encoder, one to two days; parked with that note.
Remaining from the list in the agreed order: 2 (READ progress event per revolution), 5 (track
survey job), 6 + 7 (weak-bit writing with long gaps), 4 and 8 after the first bench run.

## 8-inch timing preset and bus drive strength — 2026-10-18

Prompted by a Greaseweazle community thread the owner forwarded (BASF 6102 on a PC
controller: lost steps at a 3 ms step rate, STEP only reaching 4 V against the 220/330 ohm
termination). Our default and preset 1 also step at 3 ms, so an 8-inch drive on the UFI
would have shown the same picture. Op 18 gained timing preset 3 "8 Zoll" (8 ms step, 20 ms
settle and direction change, 1 ms side settle, 1 s spin-up, 20 ms select settle, 100 ms from
motor/select to the first step for the head load that the 50-to-34 adapter ties to MOTOR or
SELECT, since the 34-pin bus has no HLD line). Host `timing_preset("8zoll")`, `ufi profile
8zoll`, FakeV2 preset 3, harness and host tests. Documented alongside: the v0.7 drives the
Shugart bus with 74LS07 open-collector stages (40 mA sink), which covers the roughly 23 mA an
8-inch termination needs, unlike many super-I/O controllers; 360 rpm and the FM/MFM rates
follow the 1.2 MB settings; hard-sectored 32-hole media use READ/WRITE bit 1. Acceptance
plan row: step timing at the drive and the low level against the termination, with the
standard preset as the failing counter-example. No change for existing drives.
