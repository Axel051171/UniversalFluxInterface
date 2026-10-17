# Floppy Emu 231209M: Firmware review and UFI implementation

Reviewed 2026-10-16. Input: user-supplied `femu-231209M.zip`, SHA-256
`d5fac3b5bfd4b2ad8caa7ac69a6a179632d3d0bf44582fa0455a6e9e5584a94f`.
The archive contains a combined AVR/CPLD update, documentation and two MOOF
examples. No femu.bin code was executed, imported or reverse-engineered in this
review; the binary itself is taken apart in
[REA_Floppy_Emu_Analyse_2026-10-17.md](REA_Floppy_Emu_Analyse_2026-10-17.md).

## References

- [MOOF specification](https://applesaucefdc.com/moof-reference/): container
  structure, bit order, optional CRC, track mapping and bitstream timing.
- [MAME Apple 3.5-inch format description](https://github.com/mamedev/mame/blob/master/src/lib/formats/ap_dsk35.cpp):
  header/data layout, tags and sectors per zone. The file identifies BSD-3-Clause
  licensing and O. Galibert / R. Belmont as copyright holders. No source block copied.
- [Greaseweazle Macintosh codec](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/codec/macintosh/mac_gcr.py)
  and [checksum implementation](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/optimised/mac.c):
  reference for checksum operation and sector pairing. Greaseweazle's COPYING
  dedicates its software to the public domain; mac.c credits FluxEngine / David
  Given and MESS / Nathan Woods / R. Belmont. UFI implements a streaming state
  machine and an in-place decoder, without the reference implementation's arrays.
- [Greaseweazle Mac disk definitions](https://github.com/keirf/greaseweazle/blob/master/src/greaseweazle/data/diskdefs_mac.cfg):
  fixed-300-RPM zone clocks 2.63, 2.89, 3.20, 3.57 and 3.98 microseconds.
  Rates are rounded reciprocals: 380228, 346021, 312500, 280112, 251256 bit/s.

References were inspected on the review date; URLs identify upstream files,
not a vendored source dependency. The BMOW binary is not a build dependency.

## Implemented in firmware

Format 6 decodes Macintosh 400K/800K GCR headers, 524-byte sectors (12 tags +
512 payload) and checksums. It validates the repeated sector ID and DE/AA
epilogues, bounds header/data association, rejects illegal symbols, and retains
the header format byte in content fingerprints. It reuses the existing decoder
buffer and PLL. Normal Macintosh timing is 500000 channel bits/s; fixed-speed
captures may use the five zone rates. Auto-detection limits fixed-speed Mac
candidates to the requested logical cylinder's zone to avoid neighboring PLL
capture ranges producing artificial rate ambiguity. Explicit rates remain available.

Extended native WRITE reference/readback now recognizes Macintosh sectors and
includes tags and the format byte. This does not add a Macintosh sector encoder
or a drive emulator. Existing timing, range and source-coverage rules still apply.

CHECK_PROFILE gains optional fixed-300-RPM Mac zone checking. A separate
observation summary distinguishes missing sectors, inconsistent valid content,
decode errors and absent evidence. Revolution comparison explicitly reports
missing/extra edges and unverified alignment. No new job flag is required.
There is no cross-track alignment measurement: reports explicitly state that
the phase relation is unverified, rather than fabricating an angular reference.

## Test data and limitations

The user-supplied examples are copied only into ignored test build storage.
`test_mac_corpus.py --archive <path>` verifies archive/image SHA-256 values,
chunk boundaries, mapping, block ranges and bit lengths before feeding the real
firmware decoder. Both MOOF CRC fields are zero, meaning no file CRC was stored;
this is not a checksum failure. SHA-256 pins the exact supplied fixtures.

- Blank 400K.moof: SHA-256
  `b941df385d8a7dbc99f975dcc81944dcf481847fa019eb4d44749e3a3c5abd0e`.
- Blank 800K.moof: SHA-256
  `98a834764a2c2089e70709ebf52e934486116f4f56fbb8fcf1c484391c473a80`.

Together: 240 tracks / 2400 sectors. Firmware checks all sector checksums,
zone geometry and consistency across two repetitions of each supplied bitstream.
These are external bitstream fixtures, not physical flux captures. Synthetic
tests additionally exercise nonzero tags/payloads, checksum/header failures,
wrong repeated IDs, epilogue damage, content reference mismatch, zone detection,
and synchronous/cooperative analysis. Missing local fixtures produce an explicit
skip; CI does not silently claim that it ran this private local corpus.

No standalone Lisa/Twiggy or Apple II 5.25-inch decoder, MOOF filesystem reader
on the MCU, random weak-bit writer or cross-track synchronizer is advertised.
The tool handles image containers; firmware handles captured flux and bounded
sector evidence. Real-media validation and Apple drive electrical compatibility
remain hardware qualification tasks.
