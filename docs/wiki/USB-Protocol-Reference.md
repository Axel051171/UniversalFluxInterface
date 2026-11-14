# Complete USB protocol reference: reader's guide

**Sprache / Language:** [Deutsch](USB-Protokoll-Referenz) | [English](USB-Protocol-Reference)

The **complete detailed specification is currently in German**. It is included in this wiki, not just linked to an external file. Choose **Deutsch** above to open the full reference. The [English developer guide](USB-Protocol) explains framing, CRC, session setup, INFO, READ/WRITE workflows, flux decoding, errors and verified byte examples in English. This page helps locate additional specifications.

The full German page is generated from [docs/USB_Protokoll.md](https://github.com/Axel051171/UniversalFluxInterface/blob/main/docs/USB_Protokoll.md). Wiki validation detects a stale copy. Edit the source and regenerate rather than maintaining two independent specifications. The German page has a clickable table of contents.

| Reference section | Contents |
|---|---|
| 1.1–1.2 | USB identities, native CDC and composite endpoints |
| 1.3–1.4 | Capability matrix, generations, resource ownership and feature queries |
| 1.5–1.7 | v1.0 board peripherals, LAN/authentication and Macintosh control |
| 2.1–2.2 | Byte framing, CRC, flux code and exact index counting |
| 2.3–2.6 | Events, timebase, reset/cancel, deadlines and integrity jobs |
| 3.1–3.4 | Complete command payloads, status codes, drive IDs and IEC addressing |
| 3.5–3.6 | Track-reading example and flag interactions |
| 4.1–4.5 | Capture evidence, drive/media profiles and guarded operations |
| 4.6–4.10 | Session write lock, flag masks, refusal reasons, sector verification and speed checks |
| 4.11–4.17 | Weak regions, provenance, precompensation, surveys and raw verification |
| 4.18–4.23 | Sector reports, stored capture replay, variants, timing and protection hints |
| 4.24–4.27 | READ context, lead-in and later corrections |
| 5 | Detailed analysis, quality reports, decoders, retries and tolerance |
| 6 | Asynchronous analysis jobs, paged results and multitrack plans |
| 7–8 | SD/standalone writing and Commodore IEC |
| 9 | Test evidence and outstanding hardware qualification |
| 10 | Separate Greaseweazle protocol and 68.75 MHz timebase |
| Appendix A | Historical section-number mapping |

Command bytes are hexadecimal where prefixed with `0x`; `ANALYSIS_JOB` sub-operation numbers are generally decimal. Check each field's namespace: identical flag bits in READ, WRITE, analysis and job options have different meanings.

This is a development protocol, not a blanket guarantee for every function or drive. Use capability/schema discovery and consult [project status](Project-Status). Native UFI ticks, Greaseweazle ticks and SCP export units are distinct. Preserve the native capture when timing resolution matters.

Continue: [developing a tool](USB-Protocol) · [UnifiedFloppyTool integration](UnifiedFloppyTool-EN) · [development and tests](Development-and-Tests).
