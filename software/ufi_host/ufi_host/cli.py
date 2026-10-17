"""ufi - command line host tool for the UFI Flux Engine (UFI Headless board).

Talks UFI v2 (docs/USB_Protokoll.md, section 3); falls back to v1 for old firmware.

  ufi info | status | caps | selftest | rpm
  ufi select {a,b,amiga,iec,none,...} | motor {on,off} | seek N | recal | side {0,1}
  ufi read TRACK SIDE [-r REVS] [-o out.scp|out.hfe]  one track, summary, SCP or HFE
  ufi read-disk -o disk.scp|disk.hfe [--tracks 80] [--sides 2] [-r 3] [--drive a]
  ufi write disk.scp|disk.hfe TRACK SIDE [--verify]   rev 0 of that track
  ufi write-disk disk.scp|disk.hfe [--verify] [--drive a]
  ufi erase TRACK SIDE | abort
  ufi iec-reset | iec-send BYTE [--eoi] | iec-recv | iec-status
  ufi iec-read out.d64 [--type 1541 --tracks 35] | iec-write in.d64 [--verify] [--new NAME,ID]
  ufi files [PATH] | get NAME [-o out] | put LOCAL NAME | rm NAME       (v2, SD NAND)
  ufi cfg [KEY=VALUE ...]                             show / edit UFI.CFG (v2)
  ufi dump [--drive a --tracks 80 --sides 2 -r 3 --format img] | dump-status | dump-abort | copy | sd-write NAME
  ufi monitor [--drive a --track 0 --seconds 60]   # Drehzahl live, eine Zeile je Umdrehung
  ufi power [MASK] | events | mode {flux,sd,floppy,gw}
  ufi reset | bootloader
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import PurePosixPath

from . import protocol as P
from . import protocol2 as P2
from .hfe import (ENCODINGS, HfeImage, bits_to_deltas, fit_cell16, flux_to_cells, guess_rate, read_hfe,
                  side_bytes, write_hfe)
from .scp import ScpRevolution, read_scp, scp_to_ticks, ticks_to_scp, write_scp

CFG_NAME = "UFI.CFG"


def _to_scp(rev: P.Revolution) -> ScpRevolution:
    return ScpRevolution((rev.index_ticks * 40 + 137) // 275, ticks_to_scp(rev.samples))


def _kbps(v: str) -> int:
    return int(v) * 1000


def _hfe_image(revs: dict[tuple[int, int], list], rate: int | None, encoding: str) -> HfeImage:
    """Revolution 0 of every captured track as HFE cells (firmware 1.17 format=hfe does the
    same): bit rate from the first track unless given, side 1 cut or padded to side 0."""
    first = next((rl[0] for _, rl in sorted(revs.items()) if rl), None)
    if first is None:
        raise SystemExit("no revolutions captured")
    rate = rate or guess_rate(first.samples)
    img = HfeImage(rate, ENCODINGS[encoding])
    rpms = []
    for (c, s), rl in sorted(revs.items()):
        if not rl:
            continue
        ref = revs.get((c, 0)) or rl
        img.tracks[(c, s)] = flux_to_cells(rl[0].samples, rate, side_bytes(ref[0].index_ticks, rate))
        rpms.append(60.0 * P.FLUX_CLOCK_HZ / rl[0].index_ticks)
    img.rpm = int(round(sum(rpms) / len(rpms)))
    return img


def _hfe_write_tracks(dev, a, img: HfeImage, only: tuple[int, int] | None = None) -> None:
    """HFE tracks hold one nominal revolution: without --scale one revolution is measured
    first and every track's cells are scaled into 98.5 % of it (+-3 %, as the firmware's
    sd-write does); with --scale the cells stay nominal and the firmware scales."""
    if img.v3_tracks:
        if not isinstance(dev, P2.Device2):
            raise ValueError("HFEv3 writing requires the native UFI protocol")
        if a.scale != 1.0 or a.clamp or a.hard_sectors or getattr(a, 'quarter', 0):
            raise ValueError("HFEv3 requires original timing without scale, clamp or implicit hard-sector mapping")
        # Validate every selected track before the first drive operation.
        plans = {key: track.flux() for key, track in sorted(img.v3_tracks.items())
                 if only is None or key == only}
        limits = dev.describe()
        if not limits.flags & 2:
            raise ValueError("firmware does not advertise the extended write backend")
        for deltas, _duration in plans.values():
            if not 3 <= len(deltas) <= limits.store_words or any(d < 220 or d > 0xffffffff for d in deltas):
                raise ValueError("HFEv3 flux exceeds firmware interval or buffer limits")
        for (c, s), (deltas, duration) in plans.items():
            r = dev.write_track(c, s, deltas, False, quarter=getattr(a, "quarter", 0),
                                index_ticks=duration, extended=True, firmware_verify=a.verify)
            print(f"track {c:2d}.{s}: " + _write_text(r, a.verify), flush=True)
        return
    rev = 0
    if a.scale == 1.0:
        cap = dev.read_track(0, 0, 1)
        rev = cap.revolutions[0].index_ticks if cap.revolutions else 0
    for (c, s), cells in sorted(img.tracks.items()):
        if only and (c, s) != only:
            continue
        cell16, pm = fit_cell16(img.rate_bps, len(cells) * 8, rev)
        r = dev.write_track(c, s, bits_to_deltas(cells, cell16), a.verify, hard_sectors=a.hard_sectors,
                            quarter=getattr(a, "quarter", 0), scale=a.scale, clamp=a.clamp,
                            **({'index_ticks': (len(cells)*8*cell16+32768)>>16} if isinstance(dev,P2.Device2) else {}))
        print(f"track {c:2d}.{s}: " + _write_text(r, a.verify)
              + (f", cells scaled {pm / 1000:.3f}" if pm != 1000 else ""), flush=True)


def _summary(cap: P.Capture) -> str:
    lines = []
    for r in cap.revolutions:
        lines.append(f"  rev {r.index}: {len(r.samples):6d} flux, {r.rpm:6.1f} rpm"
                     + ("  OVERFLOW" if r.overflow else ""))
    if cap.status == 10:
        lines.append("  store full: fewer revolutions than requested")
    return "\n".join(lines)


def _prepare(dev, drive: str) -> None:
    dev.select(drive)
    dev.motor(True)
    dev.recalibrate()


def edit_cfg(text: str, changes: dict[str, str]) -> str:
    """Set KEY=VALUE lines in UFI.CFG text: replace the first active line, else append."""
    nl = "\r\n" if "\r\n" in text else "\n"
    lines = text.splitlines()
    for key, value in changes.items():
        pat = re.compile(rf"^\s*{re.escape(key)}\s*=", re.IGNORECASE)
        for i, line in enumerate(lines):
            if pat.match(line):
                lines[i] = f"{key}={value}"
                break
        else:
            lines.append(f"{key}={value}")
    return nl.join(lines) + nl


def _parse_steps(spec: str) -> tuple[list[tuple[int, int]], bool]:
    """'MS:TRACK[.Q],...' -> ([(ms, pos)], quarter_units); any '.Q' switches to quarter tracks."""
    quarter = any("." in part.split(":", 1)[1] for part in spec.split(","))
    steps = []
    for part in spec.split(","):
        ms, pos = part.split(":", 1)
        if quarter:
            t, _, q = pos.partition(".")
            steps.append((int(ms), int(t) * 4 + (int(q) if q else 0)))
        else:
            steps.append((int(ms), int(pos)))
    return steps, quarter


def cmd_read_disk(dev, a) -> None:
    if a.output.lower().endswith(".ufi"):
        return cmd_read_native(dev, a)
    _prepare(dev, a.drive)
    tracks, revs = {}, {}
    try:
        for cyl in range(a.tracks):
            for head in range(a.sides):
                cap = dev.read_track(cyl, head, a.revs, hard_sectors=a.hard_sectors)
                revs[(cyl, head)] = cap.revolutions
                tracks[cyl * 2 + head] = [_to_scp(r) for r in cap.revolutions]
                print(f"track {cyl:2d}.{head}: {len(cap.revolutions)} revs, "
                      f"{cap.revolutions[0].rpm if cap.revolutions else 0:5.1f} rpm", flush=True)
    finally:
        dev.motor(False)
    if a.output.lower().endswith(".hfe"):
        img = _hfe_image(revs, a.bitrate, a.encoding)
        write_hfe(a.output, img)
        print(f"wrote {a.output}: {len(img.tracks)} tracks, {img.rate_bps // 1000} kbit/s, {img.rpm} rpm")
        return
    nrev = min(len(v) for v in tracks.values())
    write_scp(a.output, tracks, nrev, heads=0 if a.sides == 2 else 1)
    print(f"wrote {a.output}: {len(tracks)} tracks x {nrev} revolutions")


def cmd_read_native(dev, a) -> None:
    from dataclasses import asdict
    from .archive import NativeArchive
    if not isinstance(dev, P2.Device2):
        raise ValueError("native frame archives require protocol v2")
    if not 1 <= a.tracks <= 84 or a.sides not in (1, 2):
        raise ValueError("invalid disk geometry")
    info = dev.device_info()
    archive = NativeArchive(a.output, dict(schema=1, device=asdict(info), drive=a.drive,
                                           hard_sectors=a.hard_sectors, sample_hz=info.sample_hz))
    try:
        _prepare(dev, a.drive)
        for cyl in range(a.tracks):
            for head in range(a.sides):
                attempt = archive.begin(cyl, head, a.revs)
                try:
                    cap = dev.read_track(cyl, head, a.revs, hard_sectors=a.hard_sectors,
                                         frame_sink=lambda b, att=attempt: archive.frame(att, b))
                    archive.finish(attempt, cap)
                except BaseException as exc:
                    archive.fail(attempt, str(exc) or type(exc).__name__)
                    raise
                print(f"track {cyl}.{head}: {len(cap.revolutions)} revolutions; status {cap.status}", flush=True)
    finally:
        archive.close()
        dev.motor(False)
    print(f"saved native ticks and validated frames to {a.output}")


def _write_text(r, verify: bool) -> str:
    """One line for a WRITE result: never 'verified' for a count-only read-back."""
    if r is None:                                   # v1 device
        return "written" + (" + verified" if verify else "")
    text = "written"
    if verify or r.verify_level >= 2:
        text += ", verify: " + r.verify_text
    if r.clamped:
        text += f", {r.clamped} intervals clamped into 1.6-238 us"
    return text


def cmd_write_disk(dev, a) -> None:
    if a.image.lower().endswith(".hfe"):
        img = read_hfe(a.image)
        _prepare(dev, a.drive)
        try:
            _hfe_write_tracks(dev, a, img)
        finally:
            dev.motor(False)
        return
    _, tracks = read_scp(a.image)
    _prepare(dev, a.drive)
    try:
        for n in sorted(tracks):
            r = dev.write_track(n // 2, n % 2, scp_to_ticks(tracks[n][0].cells), a.verify,
                                hard_sectors=a.hard_sectors, scale=a.scale, clamp=a.clamp,
                                **({'index_ticks': (tracks[n][0].index_time*P.FLUX_CLOCK_HZ+20000000)//40000000}
                                   if isinstance(dev,P2.Device2) else {}))
            print(f"track {n // 2:2d}.{n % 2}: " + _write_text(r, a.verify), flush=True)
    finally:
        dev.motor(False)


# ufi format: layouts the firmware can write (rate kbit/s, sectors, cylinders, heads, flags)
FORMATS = {
    "1440": (500, 18, 80, 2, ""), "720": (250, 9, 80, 2, ""), "1200": (500, 15, 80, 2, ""),
    "360": (300, 9, 40, 2, ""), "dmf": (500, 21, 80, 2, ""), "amiga": (250, 11, 80, 2, "amiga"),
    "amigahd": (500, 22, 80, 2, "amiga"),
    "1581": (250, 10, 80, 2, "swap"), "st720": (250, 9, 80, 2, ""), "st800": (250, 10, 80, 2, ""),
}


def cmd_format(dev: P2.Device2, a) -> None:
    rate, spt, cyls, heads, kind = FORMATS[a.layout]
    rate, spt = a.rate or rate, a.spt or spt
    cyls, heads = a.tracks or cyls, a.sides or heads
    if a.layout == "360":
        print("note: a 360K disk in a 1.2M drive needs two steps per track: ufi timing double_step=1")
    _prepare(dev, a.drive)
    bad = 0
    try:
        targets = [(a.track, a.side)] if a.track is not None else \
                  [(c, h) for c in range(cyls) for h in range(heads)]
        for c, h in targets:
            n, good = dev.format_track(c, h, rate, spt, a.gap3, a.fill, a.verify,
                                       amiga=(kind == "amiga"), head_id_swap=(kind == "swap"))
            ok = good is None or good == spt
            bad += 0 if ok else 1
            print(f"track {c:2d}.{h}: {n} transitions" +
                  ("" if good is None else f", verify {good}/{spt}" + ("" if ok else "  BAD")), flush=True)
    finally:
        dev.motor(False)
    if bad:
        raise SystemExit(f"{bad} track(s) failed verification")


def cmd_selftest(dev: P2.Device2) -> int:
    fails = 0
    for id_, name, st, val in dev.selftest():
        text = P2.SELFTEST_ITEMS.get(id_, (name, ""))[1]
        v = f"{val - (1 << 32) if id_ == 2 and val >= 1 << 31 else val}"
        if id_ in (9, 10):
            v = f"0x{val:02X}"
        print(f"{id_:2d} {name:<11} {P2.SELFTEST_STATUS.get(st, str(st)):<4} {v:>10}  {text}")
        fails += st == 1
    print("self-test: " + (f"{fails} FAILED" if fails else "no failures"))
    return fails


def cmd_caps(dev: P2.Device2) -> None:
    i = dev.device_info()
    print(f"protocol v{i.proto}, firmware {i.fw_major}.{i.fw_minor}, "
          f"board {f'v0.{i.board_rev}' if i.board_rev else 'unknown'}")
    print(f"sample clock {i.sample_hz} Hz, store {i.store_bytes} bytes, max payload {i.max_payload}")
    print(f"caps 0x{i.caps:08X}")
    for bit, (name, on) in enumerate(i.caps_list()):
        print(f"  bit{bit:<2} {'yes' if on else 'no ':3}  {name}")


def cmd_events(dev: P2.Device2) -> None:
    dev.enable_events(P2.EVENTS_ALL)
    print("events enabled - Ctrl+C to stop", flush=True)
    try:
        for ev in dev.iter_events():
            print(ev.describe(), flush=True)
    except KeyboardInterrupt:
        pass
    finally:
        dev.enable_events(0)


def _progress(total: int | None):
    def show(n: int) -> None:
        print(f"\r  {n} bytes" + (f" / {total}" if total is not None else ""), end="",
              file=sys.stderr, flush=True)
    return show


def _iec_check(dev: P2.Device2, what: str, device: int = 0) -> None:
    st = dev.iec_status(device)
    if not st.startswith(("00", "01", "73")):
        raise SystemExit(f"{what}: drive says {st!r}")


def cmd_iec_read(dev: P2.Device2, a) -> None:
    from . import cbm
    from .iec_job import read_image
    model = a.type
    if model == "auto":
        from .iec_identity import identify
        model = identify(dev, a.device)["model"]
        if model is None:
            raise ValueError("unknown drive ROM; select --type explicitly")
    kind, tracks = cbm.kind_for(model, a.tracks)
    dev.iec_command("I0", a.device)
    _iec_check(dev, "initialise", a.device)
    if kind == "d71":
        dev.iec_command("U0>M1", a.device)
        _iec_check(dev, "1571 mode", a.device)
    state = read_image(dev, a.output, kind, tracks, a.device, a.retries, a.resume)
    bad = [key for key, entry in state["sectors"].items() if entry["dos"] != 0]
    if bad:
        raise SystemExit(f"{len(bad)} unreadable blocks; preserved {a.output}.partial and .ufi-job.json; use --resume")
    print(f"wrote {a.output}: {kind.upper()} {tracks} tracks, all blocks read successfully")


def cmd_iec_write(dev: P2.Device2, a) -> None:
    from . import cbm
    if a.new and getattr(a, "resume", False):
        raise ValueError("--new cannot be combined with --resume")
    data = open(a.input, "rb").read()
    kind, tracks = cbm.geometry(len(data))
    image_bytes = cbm.image_size(kind, tracks)
    if any(code != 1 for code in data[image_bytes:]):
        raise ValueError("image error map contains unreadable sectors; refusing to write placeholder data")
    if a.new:
        print(f"formatting (N0:{a.new}) ...", flush=True)
        dev.iec_command("N0:" + a.new, a.device)
        _iec_check(dev, "format", a.device)
    dev.iec_command("I0", a.device)
    _iec_check(dev, "initialise", a.device)
    if kind == "d71":
        dev.iec_command("U0>M1", a.device)
    if getattr(a, "checkpoint", None) or getattr(a, "resume", False):
        from .iec_job import write_image
        result = write_image(dev, a.input, a.checkpoint or a.input + ".write-job.json",
                             a.device, a.retries, a.resume)
        print(f"verified IEC write complete: {result['complete']}")
        return
    errors = []
    off = 0
    for t, s in cbm.blocks(kind, tracks):
        blk = data[off:off + cbm.BLOCK]
        off += cbm.BLOCK
        code = dev.iec_block_write(t, s, blk, a.device)
        if code not in (0, 1):
            errors.append((t, s, code))
            if code == 26:
                raise SystemExit(f"track {t} sector {s}: write protect on")
        elif a.verify and dev.iec_block_read(t, s, a.device) != blk:
            errors.append((t, s, -1))
        if s == 0:
            print(f"track {t:2d}: {cbm.sectors_per_track(kind, t)} blocks", end="\r", flush=True)
    print(f"wrote {a.input} ({kind.upper()}, {tracks} tracks) with {len(errors)} error(s)")
    for t, s, code in errors[:20]:
        print(f"  track {t} sector {s}: " + ("verify mismatch" if code < 0 else f"DOS {code:02d} {cbm.DOS_ERRORS.get(code, '')}"))
    if errors:
        raise SystemExit(1)


def cmd_monitor(dev: P2.Device2, a) -> None:
    """Drive speed over time: READs of up to 50 revolutions back to back (one revolution is
    lost between two reads), one line per revolution with the deviation from the first ten.
    Ctrl-C stops; the motor is switched off afterwards."""
    import time
    _prepare(dev, a.drive)
    dev.seek(a.track)
    t0 = time.monotonic()
    n, base, lo, hi, total = 0, [], None, None, 0.0
    status = 0
    try:
        while a.seconds <= 0 or time.monotonic() - t0 < a.seconds:
            cap = dev.read_track(a.track, a.side, revolutions=a.revs)
            if not cap.revolutions and not cap.status:
                print("no revolutions in the read (no index pulse?)")
                break
            for rev in cap.revolutions:
                if rev.index_ticks <= 0:
                    continue
                rpm = 60.0 * P.FLUX_CLOCK_HZ / rev.index_ticks
                n += 1
                total += rpm
                lo = rpm if lo is None else min(lo, rpm)
                hi = rpm if hi is None else max(hi, rpm)
                if len(base) < 10:
                    base.append(rpm)
                ref = sum(base) / len(base)
                dev_pct = (rpm - ref) / ref * 100.0
                pos = max(0, min(40, int(round(20 + dev_pct / a.scale * 20))))
                bar = "".join("|" if i == 20 else ("*" if i == pos else "-") for i in range(41))
                print(f"{n:5d} {time.monotonic() - t0:7.1f}s {rpm:8.3f} rpm {dev_pct:+6.3f} % "
                      f"{len(rev.samples):7d} flux {bar}", flush=True)
            if cap.status:
                status = cap.status
                print(f"read ended with status {cap.status} ({P2.STATUS_TEXT.get(cap.status, '?')})")
                break
    except KeyboardInterrupt:
        pass
    finally:
        dev.motor(False)
    if n:
        print(f"{n} revolutions: min {lo:.3f}, avg {total / n:.3f}, max {hi:.3f} rpm, "
              f"spread {(hi - lo) / (total / n) * 100:.3f} %" + ("" if not status else f", status {status}"))


def run_v2(dev: P2.Device2, a) -> bool:
    """v2-only commands; returns False when a.cmd is not one of them."""
    if a.cmd == "caps":
        cmd_caps(dev)
    elif a.cmd == "files":
        for e in dev.list_dir(a.path):
            print(f"{'<DIR>' if e.is_dir else e.size:>10}  {e.name}")
    elif a.cmd == "get":
        data = dev.read_file(a.name, _progress(None))
        out = a.output or PurePosixPath(a.name).name
        with open(out, "wb") as f:
            f.write(data)
        print(f"\nwrote {out}: {len(data)} bytes")
    elif a.cmd == "put":
        with open(a.local, "rb") as f:
            data = f.read()
        dev.write_file(a.name, data, _progress(len(data)))
        print(f"\n{a.name}: {len(data)} bytes written")
    elif a.cmd == "rm":
        dev.delete(a.name)
    elif a.cmd == "cfg":
        text = dev.read_file(CFG_NAME).decode("latin-1")
        if not a.set:
            print(text, end="" if text.endswith("\n") else "\n")
            return True
        changes = {}
        for s in a.set:
            if "=" not in s:
                raise SystemExit(f"expected KEY=VALUE, got {s!r}")
            k, v = s.split("=", 1)
            changes[k.strip()] = v.strip()
        dev.write_file(CFG_NAME, edit_cfg(text, changes).encode("latin-1"))
        dev.cfg_reload()
        print(f"{CFG_NAME} updated and reloaded: " + ", ".join(f"{k}={v}" for k, v in changes.items()))
    elif a.cmd == "events":
        cmd_events(dev)
    elif a.cmd == "mode":
        dev.usb_mode(a.mode)
        print(f"switching to {a.mode} - the device re-enumerates")
    elif a.cmd == "dump":
        dev.dump_start(a.drive, a.tracks, a.sides, a.revs, a.format)
    elif a.cmd == "dump-status":
        print(dev.dump_status())
    elif a.cmd == "dump-abort":
        dev.dump_abort()
    elif a.cmd == "copy":
        dev.copy_start()
    elif a.cmd == "monitor":
        cmd_monitor(dev, a)
    elif a.cmd == "sd-write":
        dev.sd_write_start(a.name)
        print(f"writing {a.name.upper() or 'UFI.CFG write_file'} from the SD NAND; follow with ufi dump-status")
    elif a.cmd == "power":
        print(dev.power(a.mask))
    else:
        return False
    return True


V2_ONLY = {"diagnostics", "features", "job-status", "release", "iec-identify", "caps", "describe", "files", "get", "put", "rm", "cfg", "events", "mode", "dump", "dump-status", "scan", "index-sim", "rpm-select", "iec-nib", "analyze", "format", "write-limits",
           "dump-abort", "copy", "power", "iec-status", "iec-read", "iec-write", "sd-write", "monitor", "extra", "profile", "apple-gcr", "tolerance", "apple-stats", "apple-table",
           "write-lock", "masks", "refusal", "verify-report", "speed-check"}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="ufi", description="UFI Flux Engine host tool",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--port", help="serial port (default: auto-detect VID 1209 / PID 4F54)")
    ap.add_argument("--integrity", action="store_true", help="use job IDs, chunk offsets and checked write commit")
    ap.add_argument("--extended", action="store_true", help="use host-timed extended flux write intervals")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("archive-check", help="verify native archive hashes offline; report incomplete captures").add_argument("image")
    p = sub.add_parser("amiga-quality", help="offline Amiga sector/label and weak-track analysis of SCP flux")
    p.add_argument("image")
    p.add_argument("--track", type=int, default=0)
    p.add_argument("--side", type=int, choices=(0, 1), default=0)
    p.add_argument("--spt", type=int, choices=(11, 22), default=11)
    p.add_argument("--bitrate", type=_kbps, default=250000)
    p.add_argument("-o", "--output", help="JSON report (otherwise stdout)")
    sub.add_parser("rpm", help="spindle speed from the index pulses").add_argument(
        "revs", type=int, nargs="?", default=5, help="revolutions to average (default 5)")
    sub.add_parser("scan", help="which drives answer (moves the heads)").add_argument(
        "bus", nargs="?", choices=("pc", "ds"), default="pc", help="pc: a/b/amiga/amiga2, ds: Shugart bus ds0-ds3")
    sub.add_parser("write-limits", help="what the write backend accepts (firmware 1.15): clock, max intervals, min/max interval")
    sub.add_parser("diagnostics", help="buffer occupancy, DMA/USB errors and memory reserves")
    sub.add_parser("features", help="supported/configured/available features")
    sub.add_parser("job-status", help="last extended job state and integrity totals")
    sub.add_parser("release", help="release native ownership while idle")
    sub.add_parser("describe", help="protocol clocks, transport availability and limits (firmware 1.18)")
    p = sub.add_parser("format", help="format a disk or one track in the firmware (MFM/Amiga, firmware 1.14): "
                       "ufi format 1440 [--verify]; one track: --track T --side S")
    p.add_argument("layout", choices=list(FORMATS), help="1440 720 1200 360 dmf amiga amigahd 1581 st720 st800")
    p.add_argument("--track", type=int)
    p.add_argument("--side", type=int, default=0, choices=(0, 1))
    p.add_argument("--tracks", type=int, help="cylinders (default from the layout)")
    p.add_argument("--sides", type=int, choices=(1, 2))
    p.add_argument("--rate", type=int, choices=(250, 300, 500, 1000), help="override kbit/s")
    p.add_argument("--spt", type=int, help="override sectors per track")
    p.add_argument("--gap3", type=int, default=0, help="gap 3 bytes (0 = by layout)")
    p.add_argument("--fill", type=lambda x: int(x, 0), default=0xF6, help="data byte, default 0xF6")
    p.add_argument("--verify", action="store_true", help="read every track back and count good sectors")
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p = sub.add_parser("analyze", help="track analysis: rpm, encoding, bit cells, weak bits, sector timing (firmware 1.14)")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("-q", "--quarter", type=int, choices=(0, 1, 2, 3), help="Apple drives (firmware 1.18): analyse the quarter track TRACK + q/4")
    sub.add_parser("iec-status", help="DOS status line of the IEC drive (firmware 1.16)")
    p = sub.add_parser("iec-read", help="D64/D71/D81 from a 1541/1571/1581 over IEC via U1 block reads (firmware 1.16)")
    p.add_argument("output")
    p.add_argument("--type", choices=("auto", "1541", "1541-II", "1541C", "1571", "1581"), default="1541")
    p.add_argument("--tracks", type=int, default=0, help="default: full disk; 1541: 35/40; 1571: 70 or 35/40 first side; 1581: 80")
    p.add_argument("--retries", type=int, default=2, help="additional attempts per unreadable sector (0..20)")
    p.add_argument("--resume", action="store_true", help="resume checkpoint; keep the same physical disk inserted")
    p.add_argument("--device", type=int, default=0, help="IEC device number (default: UFI.CFG iec_device)")
    p = sub.add_parser("iec-write", help="D64/D71/D81 onto a formatted disk in a 1541/1571/1581 via U2 block writes (firmware 1.16)")
    p.add_argument("input")
    p.add_argument("--device", type=int, default=0, help="IEC device number (default: UFI.CFG iec_device)")
    p.add_argument("--verify", action="store_true", help="read every block back and compare")
    p.add_argument("--new", metavar="NAME,ID", help="format the disk first (N0:NAME,ID; about a minute on a 1541)")
    p.add_argument("--checkpoint", help="write journal path; enables mandatory readback verification")
    p.add_argument("--resume", action="store_true", help="resume verified IEC write journal on the same disk")
    p.add_argument("--retries", type=int, default=2)
    p = sub.add_parser("iec-identify", help="read ROM signatures; report unknown ROMs explicitly")
    p.add_argument("--device", type=int, default=0)
    p.add_argument("--fingerprint", action="store_true", help="also hash the full mapped ROM")
    p = sub.add_parser("iec-nib", help="1541 raw GCR tracks over IEC into a G64 (firmware 1.14, drive code upload)")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--tracks", type=int, default=35, choices=range(35, 43), metavar="35-42")
    p.add_argument("--halftracks", action="store_true", help="also read the half tracks")
    p.add_argument("--device", type=int, default=0, help="IEC device number (default: UFI.CFG iec_device)")
    sub.add_parser("rpm-select", help="3-mode drive: select 300/360 rpm via UFI.CFG rpm_line (firmware 1.14)").add_argument(
        "rpm", nargs="?", choices=("300", "360"), help="omit to query")
    p = sub.add_parser("index-sim", help="index pulses on J9 pin 6 for flippy disks (needs firmware 1.13)")
    p.add_argument("rpm", nargs="?", choices=("300", "360", "off"), help="omit to query")
    p.add_argument("--pulse", type=int, default=0, help="pulse width in us (default 2000)")
    p.add_argument("--internal", action="store_true",
                   help="pulses also replace the drive's INDEX line for reading/writing (no wire)")
    p.add_argument("--no-pin", action="store_true", help="internal only, J9 pin 6 stays an input")
    for name in ("info", "status", "selftest", "recal", "abort", "iec-reset", "iec-recv",
                 "reset", "bootloader", "check-disk", "amiga-id", "usb-power", "caps", "events",
                 "dump-status", "dump-abort", "copy"):
        sub.add_parser(name)
    sub.add_parser("timing", help="show/set drive timings, e.g. timing step_rate_us=6000; tg43=1 for 8-inch drives (firmware 1.16)").add_argument(
        "set", nargs="*", metavar="FIELD=VALUE")
    sub.add_parser("extra", help="show/set the additional drive times (firmware 1.18): density_us motor_step_ms write_settle_us gate_lead_us gate_tail_us").add_argument(
        "set", nargs="*", metavar="FIELD=VALUE")
    sub.add_parser("apple-gcr", help="Apple II 5.25-inch GCR decoder options (format 7, firmware 1.18): addr_prologue=d4aa96 data_prologue=d5aab5 "
                   "addr_epilogue=deab data_epilogue=deaa flags=keep_bad_addr_checksum+ignore_epilogues+any_track+data_without_checksum+data_without_header; "
                   "'apple-gcr reset' restores the defaults").add_argument("set", nargs="*", metavar="FIELD=VALUE")
    sub.add_parser("apple-stats", help="Apple II nibble statistics of the last format 7 analysis (firmware 1.18, op 35): nibbles, self-sync nibbles, "
                   "address/data fields and orphan headers per revolution; per sector volume, sync run lengths, header-to-data distance, findings")
    sub.add_parser("apple-table", help="6-and-2 symbol table of the format 7 decoder (firmware 1.18, op 36): no argument shows it, "
                   "'standard' restores the DOS 3.3 alphabet, 128 hex digits load a swapped alphabet").add_argument("table", nargs="?")
    sub.add_parser("tolerance", help="tolerance flags of the analysis decoders for IBM MFM/FM, HP, Amiga, Commodore and Macintosh (firmware 1.18, op 34): "
                   "keep_bad_header+ignore_epilogues+any_track+data_without_checksum+data_without_header, a number, or 'reset'; "
                   "no argument shows the current flags").add_argument("flags", nargs="?")
    sub.add_parser("masks", help="request flag masks this firmware accepts (firmware 1.18, op 38): READ/WRITE/flags2/analysis flags, "
                   "Apple GCR, tolerance, quality features, diagnostics, position byte")
    sub.add_parser("refusal", help="which request the firmware last refused with status 16/21 and which field caused it "
                   "(firmware 1.18, op 39); ask right after a refusal")
    sub.add_parser("verify-report", help="per-sector outcome of the last verified WRITE (firmware 1.18, op 40): CHRN and "
                   "missing / ok / CRC error / different content for every reference sector")
    sub.add_parser("speed-check", help="speed check before writing (firmware 1.18, op 41): no argument shows tolerance and last "
                   "measurement, PERMILLE (1-500) sets the allowed deviation of the drive from the source index period "
                   "(writes with index_ticks end with status 23 outside it), 'off' disables").add_argument("tolerance", nargs="?")
    sub.add_parser("write-lock", help="session write lock (firmware 1.18, op 37): no argument shows lock and jumper state, 'engage' locks every disk "
                   "and drive write (status 22) until the board is reset; there is no disengage").add_argument("action", nargs="?", choices=["engage"])
    sub.add_parser("profile", help="effective drive settings of the selected drive; 'profile standard|konservativ' applies a timing preset, "
                   "'profile save' stores the effective settings so SELECT restores them (firmware 1.18)").add_argument(
        "action", nargs="?", choices=("standard", "konservativ", "save"))
    sub.add_parser("select").add_argument("drive", choices=list(P.DRIVES))
    sub.add_parser("motor").add_argument("state", choices=("on", "off"))
    q_help = "Apple Disk II quarter-track offset 0-3 (2 = half track), firmware >= 1.14"
    p = sub.add_parser("seek")
    p.add_argument("track", type=int)
    p.add_argument("--quarter", type=int, default=0, choices=(0, 1, 2, 3), help=q_help)
    sub.add_parser("side").add_argument("side", type=int, choices=(0, 1))
    hs_help = "hard-sectored disk: number of sector holes (10/16/32), firmware >= 1.14"
    p = sub.add_parser("monitor", help="drive speed over time (one line per revolution) for adjusting a drive; Ctrl-C stops")
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p.add_argument("--track", type=int, default=0)
    p.add_argument("--side", type=int, default=0, choices=(0, 1))
    p.add_argument("--seconds", type=float, default=0, help="stop after this time (default: until Ctrl-C)")
    p.add_argument("--revs", type=int, default=50, help="revolutions per READ (default 50, max 200)")
    p.add_argument("--scale", type=float, default=1.0, help="full bar deflection in percent (default 1)")
    p = sub.add_parser("read")
    p.add_argument("--amiga-quality", metavar="JSON", help="save adaptive Amiga quality report; keep raw flux with -o capture.scp")
    p.add_argument("--amiga-spt", type=int, choices=(11, 22), default=11)
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("-o", "--output")
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p.add_argument("--quarter", type=int, default=0, choices=(0, 1, 2, 3), help=q_help)
    st_help = "head steps during the operation (firmware >= 1.14): MS:TRACK[.Q],... e.g. 50:17.1,100:17.2"
    br_help = ".hfe output: bit rate in kbit/s (250/300/500/1000); default: from the shortest flux intervals"
    en_help = ".hfe output: encoding field of the header (default mfm)"
    cl_help = ("force intervals outside 1.6-238 us into range (firmware >= 1.15 refuses them otherwise); "
               "the count of changed intervals is reported")
    sc_help = ("multiply every flux interval by FACTOR (0.5-2.0, firmware >= 1.14): rpm_source/rpm_target "
               "for a 360 rpm image on a 300 rpm drive (1.2), or the Mac zone factor")
    p.add_argument("--steps", metavar="SPEC", help=st_help)
    p.add_argument("--bitrate", type=_kbps, metavar="KBPS", help=br_help)
    p.add_argument("--encoding", choices=list(ENCODINGS), default="mfm", help=en_help)
    p = sub.add_parser("read-disk")
    p.add_argument("-o", "--output", required=True, help="disk.scp (flux, all revolutions) or disk.hfe (HxC bit cells, one revolution)")
    p.add_argument("--bitrate", type=_kbps, metavar="KBPS", help=br_help)
    p.add_argument("--encoding", choices=list(ENCODINGS), default="mfm", help=en_help)
    p.add_argument("--tracks", type=int, default=80)
    p.add_argument("--sides", type=int, default=2, choices=(1, 2))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p = sub.add_parser("read-dsk", help="Apple II 5.25-inch disk -> .dsk (DOS 3.3 order) or .po (ProDOS order): flux is read with READ and decoded on the host (6-and-2 GCR, same marks/flags as firmware format 7)")
    p.add_argument("-o", "--output", required=True, help="disk.dsk or disk.po")
    p.add_argument("--order", choices=("dos33", "prodos", "physical"), help="sector order in the file; default from the extension (.po = prodos, else dos33)")
    p.add_argument("--tracks", type=int, default=35)
    p.add_argument("-r", "--revs", type=int, default=2)
    p.add_argument("--drive", default="apple", choices=list(P.DRIVES))
    p.add_argument("--addr-prologue", default="d5aa96", metavar="HEX")
    p.add_argument("--data-prologue", default="d5aaad", metavar="HEX")
    p.add_argument("--addr-epilogue", default="deaa", metavar="HEX")
    p.add_argument("--data-epilogue", default="deaa", metavar="HEX")
    p.add_argument("--flags", default="", help="keep_bad_addr_checksum+ignore_epilogues+any_track+data_without_checksum+data_without_header")
    p = sub.add_parser("write")
    p.add_argument("image")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("--verify", action="store_true")
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p.add_argument("--quarter", type=int, default=0, choices=(0, 1, 2, 3), help=q_help)
    p.add_argument("--steps", metavar="SPEC", help=st_help)
    p.add_argument("--scale", type=float, default=1.0, metavar="FACTOR", help=sc_help)
    p.add_argument("--clamp", action="store_true", help=cl_help)
    p.add_argument("--erase-rest", action="store_true",
                   help="firmware >= 1.18: keep the gate on without transitions from the last interval to the next index, "
                        "so nothing of the old track survives behind the new data (reported as erased ticks)")
    p = sub.add_parser("write-disk")
    p.add_argument("image")
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p.add_argument("--scale", type=float, default=1.0, metavar="FACTOR", help=sc_help)
    p.add_argument("--clamp", action="store_true", help=cl_help)
    p.add_argument("--verify", action="store_true")
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p = sub.add_parser("erase")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p = sub.add_parser("iec-send")
    p.add_argument("byte", type=lambda s: int(s, 0))
    p.add_argument("--eoi", action="store_true")
    sub.add_parser("files", help="list a directory on the SD NAND").add_argument("path", nargs="?", default="")
    p = sub.add_parser("get", help="download a file from the SD NAND, e.g. DUMP0001.SCP")
    p.add_argument("name")
    p.add_argument("-o", "--output")
    p = sub.add_parser("put", help="upload a file to the SD NAND")
    p.add_argument("local")
    p.add_argument("name")
    sub.add_parser("rm", help="delete a file on the SD NAND").add_argument("name")
    sub.add_parser("cfg", help="show UFI.CFG, or set KEY=VALUE lines and reload").add_argument(
        "set", nargs="*", metavar="KEY=VALUE")
    sub.add_parser("mode", help="switch USB personality").add_argument("mode", choices=list(P2.USB_MODES))
    p = sub.add_parser("dump", help="standalone dump to DUMPnnnn.SCP on the SD NAND "
                                    "(--drive iec: 1541 to DUMPnnnn.D64, --tracks 35|40, firmware >= 1.14)")
    p.add_argument("--drive", choices=list(P.DRIVES))
    p.add_argument("--tracks", type=int, default=80)
    p.add_argument("--sides", type=int, default=2, choices=(1, 2))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("--format", choices=list(P2.DUMP_FORMATS),
                   help="scp = flux (default from UFI.CFG), img/adf = decoded sectors (firmware 1.16), hfe = HxC bit cells (1.17)")
    sub.add_parser("sd-write", help="write an image on the SD NAND onto the disk (firmware 1.16): SCP flux, HFE cells (1.17), "
                               "IMG/ADF/D81 sectors, D64/D71 via the IEC drive; report in WRITE.LOG").add_argument(
        "name", nargs="?", default="", help="8.3 file name; omit for UFI.CFG write_file")
    sub.add_parser("power", help="board status; MASK bit0 FDD_5V, bit1 FDD_12V").add_argument(
        "mask", nargs="?", type=lambda s: int(s, 0))
    a = ap.parse_args(argv)

    if a.cmd == "amiga-quality":
        import json
        from .amiga import analyze_capture
        try:
            _, tracks = read_scp(a.image)
            number = a.track * 2 + a.side
            if number not in tracks:
                raise ValueError('requested track is absent from SCP image')
            revs = []
            for seq, rev in enumerate(tracks[number]):
                samples, total = [], 0
                for delta in scp_to_ticks(rev.cells):
                    total += delta
                    samples.append(total)
                duration = (rev.index_time * P.FLUX_CLOCK_HZ + 20_000_000) // 40_000_000
                revs.append(P.Revolution(a.track, a.side, seq, 1, duration, samples))
            report, _ = analyze_capture(P.Capture(revs), a.bitrate, number, a.spt)
            text = json.dumps(report, indent=2) + '\n'
            if a.output:
                with open(a.output, 'x', encoding='utf-8') as output:
                    output.write(text)
            else:
                print(text, end='')
            return 0 if report['complete'] else 1
        except (ValueError, OSError) as exc:
            print(f'error: {exc}', file=sys.stderr)
            return 1

    if a.cmd == "archive-check":
        import json
        from .archive import verify_archive
        report = verify_archive(a.image)
        print(json.dumps(report, indent=2, sort_keys=True))
        return 1 if report["unfinished"] or report["failed"] else 0

    dev = P2.connect(a.port)
    if isinstance(dev, P2.Device2):
        dev.integrity = a.integrity
        dev.extended = a.extended
    try:
        if not isinstance(dev, P2.Device2) and (a.integrity or a.extended):
            print("error: --integrity and --extended require UFI v2 firmware", file=sys.stderr)
            return 1
        if isinstance(dev, P2.Device2):
            if run_v2(dev, a):
                return 0
        elif a.cmd in V2_ONLY:
            print(f"error: '{a.cmd}' needs UFI v2 firmware (>= 1.12)", file=sys.stderr)
            return 1
        if a.cmd == "info":
            print("\n".join(dev.info()))
        elif a.cmd == "status":
            print(dev.status())
        elif a.cmd == "selftest":
            if isinstance(dev, P2.Device2):     # v2: board self-test table (firmware 1.14)
                if cmd_selftest(dev):
                    raise SystemExit(1)
            else:
                r = dev.selftest()
                print(" ".join(f"{n}={'OK' if r & (1 << i) else 'FAIL'}"
                               for i, n in enumerate(("timer", "dma", "usb", "gpio"))))
        elif a.cmd == "rpm":
            if isinstance(dev, P2.Device2) and dev.caps() & P2.CAP_DIAG:
                print(dev.diag_rpm(a.revs))
            else:
                print(f"{dev.rpm()} rpm")
        elif a.cmd == "scan":
            print("drive    answers  disk  write-protect")
            for name, fl in dev.drive_scan(a.bus == "ds"):
                print(f"{name:8} {'yes' if fl & P2.DIAG_TRACK0 else '-':8} "
                      f"{'spins' if fl & P2.DIAG_INDEX else '-':5} {'yes' if fl & P2.DIAG_WPROT else '-'}")
        elif a.cmd == "diagnostics":
            import json
            print(json.dumps(dev.diagnostics(), indent=2))
        elif a.cmd == "features":
            print(dev.features())
        elif a.cmd == "job-status":
            print(dev.job_status())
        elif a.cmd == "release":
            print(dev.resource(False))
        elif a.cmd == "describe":
            print(dev.describe())
        elif a.cmd == "write-limits":
            lim = dev.write_limits()
            print(f"schema {lim.schema}, flags 0x{lim.flags:02X}"
                  f" (content verify {'yes' if lim.flags & 1 else 'no'}, long tracks {'yes' if lim.flags & 2 else 'no'},"
                  f" clamp flag {'yes' if lim.flags & 4 else 'no'}, erase remainder {'yes' if lim.flags & 8 else 'no'})")
            print(f"clock {lim.sample_hz} Hz, max {lim.max_flux} intervals per track")
            print(f"interval {lim.min_ticks}..{lim.max_ticks} ticks = "
                  f"{lim.min_ticks / lim.sample_hz * 1e6:.2f}..{lim.max_ticks / lim.sample_hz * 1e6:.2f} us")
        elif a.cmd == "format":
            cmd_format(dev, a)
        elif a.cmd == "analyze":
            r = dev.analyze(a.track, a.side, a.revs, a.quarter)
            pos = f"track {a.track}.{a.side}" + (f" quarter {a.quarter}" if a.quarter is not None else "")
            print(f"{pos}: {r['rpm']:.2f} rpm, {r['transitions']} transitions, "
                  f"{r['revs']} revs")
            print(f"  intervals: {', '.join(f'{p / 1000:.2f} us' for p in r['peaks_ns']) or '-'}"
                  f"  -> {r['encoding']}, bit cell {r['bitcell_ns']} ns, {r['bitcells']} bit cells per track")
            if r["sectors"]:
                print(f"  MFM sectors: {r['sectors']}, first ID {r['first_sector_us'] / 1000:.2f} ms after index")
            w = r["weak_windows"]
            print(f"  weak-bit windows: {w}/64" + (f"  (mask 0x{r['weak_mask']:016x})" if w else ""))
        elif a.cmd == "iec-nib":
            from .nib import NibReader, write_g64
            import json
            import hashlib
            from dataclasses import asdict
            # Preserve evidence before stitching, including unsuccessful/raw fragments.
            with open(a.output + ".fragments.jsonl", "x", encoding="utf-8") as fragments:
                def save_chunk(args, chunk):
                    record = asdict(chunk)
                    record["data"] = chunk.data.hex()
                    record["sha256"] = hashlib.sha256(chunk.data).hexdigest()
                    record["request"] = args
                    record["schema"] = 1
                    fragments.write(json.dumps(record) + "\n")
                    fragments.flush()
                rd = NibReader(dev, a.device, chunk_sink=save_chunk)
                try:
                    rd.calibrate()
                except BaseException:
                    dev.iec_command(b"I0", a.device)
                    raise
                res = rd.read_disk(a.tracks, a.halftracks)
            write_g64(a.output, res)
            good = sum(1 for r in res if r.data)
            print(f"wrote {a.output}: {good}/{len(res)} tracks stitched, "
                  f"{sum(1 for r in res if r.raw)} raw")
        elif a.cmd == "rpm-select":
            rpm, line = dev.set_rpm(None if a.rpm is None else int(a.rpm))
            print(f"speed select line: {P2.RPM_LINES.get(line, line)}, "
                  + (f"{rpm} rpm selected" if rpm else "not set"))
        elif a.cmd == "index-sim":
            mode = (0 if a.no_pin else P2.INDEX_SIM_PIN) | (P2.INDEX_SIM_INTERNAL if a.internal else 0)
            if a.rpm is not None and a.rpm != "off" and not mode:
                raise SystemExit("index-sim: --no-pin needs --internal")
            rpm, mode = dev.index_sim(None if a.rpm is None else (0 if a.rpm == "off" else int(a.rpm)),
                                      a.pulse, mode)
            print(f"index simulation {rpm} rpm, {P2.INDEX_SIM_MODES.get(mode, mode)}" if rpm
                  else "index simulation off")
        elif a.cmd == "select":
            dev.select(a.drive)
        elif a.cmd == "motor":
            dev.motor(a.state == "on")
        elif a.cmd == "seek":
            dev.seek(a.track, a.quarter)
        elif a.cmd == "recal":
            dev.recalibrate()
        elif a.cmd == "side":
            dev.side(a.side)
        elif a.cmd == "check-disk":
            changed, present = dev.check_disk()
            print(f"disk {'present' if present else 'MISSING'}" + (", changed" if changed else ""))
        elif a.cmd == "usb-power":
            cc1, cc2, ma = dev.usb_power()
            print(f"CC1 {cc1} mV, CC2 {cc2} mV -> " + (f"{ma} mA allowed" if ma else "no Type-C source"))
        elif a.cmd == "amiga-id":
            v, name = dev.amiga_id()
            print(f"0x{v:08X}  {name}")
        elif a.cmd == "timing":
            changes = {k: int(v, 0) for k, v in (s.split("=", 1) for s in a.set)}
            for k, v in dev.timing(**changes).items():
                print(f"{k:18} {v}")
        elif a.cmd == "extra":
            changes = {k: int(v, 0) for k, v in (s.split("=", 1) for s in a.set)}
            for k, v in dev.extra_timing(**changes).items():
                if k != "reserved":
                    print(f"{k:18} {v}")
        elif a.cmd == "apple-gcr":
            if a.set == ["reset"]:
                changes = {"addr_prologue": "d5aa96", "data_prologue": "d5aaad", "addr_epilogue": "deaa",
                           "data_epilogue": "deaa", "flags": 0}
            else:
                changes = {k: (int(v, 0) if k == "flags" and v.isdigit() else v) for k, v in (s.split("=", 1) for s in a.set)}
            opts = dev.apple_gcr_options(**changes)
            for k in P2.APPLE_GCR_FIELDS[:4]:
                print(f"{k:18} {opts[k]}")
            print(f"{'flags':18} 0x{opts['flags']:02x} {'+'.join(opts['flag_names']) or '-'}")
        elif a.cmd == "apple-stats":
            st = dev.apple_gcr_stats()
            if not st["revs"]:
                print("no format 7 analysis since the last reset")
            for i, r in enumerate(st["revs"]):
                trail = f"  trailing zeros 1/2/3+: {'/'.join(map(str, r['trail']))}" if "trail" in r else ""
                print(f"rev {i}: nibbles {r['nibbles']}  sync {r['sync_nibbles']}  address fields {r['address_fields']}  "
                      f"data fields {r['data_fields']}  orphan headers {r['orphan_headers']}{trail}")
            if st.get("variants"):
                print("prologue variants D5 AA xx: " + ", ".join(f"{v['third']:02x} x{v['count']}" for v in st["variants"]))
            print("sec vol  sync>addr  hdr>data  sync>data  seen  position  findings")
            for i, s in enumerate(st["sectors"]):
                if s["seen"] or s["volume"] or s["sync_before_addr"]:
                    print(f"{i:3} {s['volume']:3}  {s['sync_before_addr']:9}  {s['header_to_data']:8}  {s['sync_before_data']:9}  "
                          f"{s['seen']:04b}  {s.get('position', 0):8}  {'+'.join(s['flag_names']) or '-'}")
        elif a.cmd == "apple-table":
            t = dev.apple_gcr_table(a.table)
            print(f"table {'standard' if t == P2.APPLE_STD_TABLE else 'custom'}: {t.hex()}")
        elif a.cmd == "tolerance":
            t = dev.decoder_tolerance(0 if a.flags == "reset" else a.flags)
            print(f"tolerance 0x{t['flags']:02x} {'+'.join(t['flag_names']) or 'strict'}")
        elif a.cmd == "speed-check":
            sc = dev.speed_check(None if a.tolerance is None else 0 if a.tolerance == "off" else int(a.tolerance))
            tol = sc["tolerance_permille"]
            text = "off" if not tol else f"+/-{tol / 10:.1f} %"
            if sc["measured_ticks"]:
                text += f", last: drive {sc['measured_rpm']:.2f} rpm vs source {sc['source_rpm']:.2f} rpm"
            print(f"speed check {text}")
        elif a.cmd == "verify-report":
            vr = dev.verify_report()
            print(f"track {vr['track']}.{vr['side']}: {vr['format_name']}, {vr['count']} reference sectors, "
                  f"{'content verified' if vr['content_verified'] else 'not verified'}"
                  + (", read-back missing" if vr["readback_missing"] else "")
                  + (", reference overflow" if vr["overflow"] else "") + (", conflicting source" if vr["conflicting"] else ""))
            for s in vr["sectors"]:
                if s["state"] != 1:
                    print(f"  C{s['c']} H{s['h']} R{s['r']} N{s['n']}: {s['state_name']}")
        elif a.cmd == "refusal":
            rf = dev.last_refusal()
            if not rf["command"]:
                print("no refusal since reset")
            else:
                print(f"command 0x{rf['command']:02X} seq {rf['seq']} status {rf['status']} "
                      f"({P2.STATUS_TEXT.get(rf['status'], '?')}): {rf['field_name']}, detail {rf['detail']}")
        elif a.cmd == "masks":
            for k, v in dev.flag_masks().items():
                print(f"{k:18} 0x{v:04x}" if k != "format_hint_max" else f"{k:18} {v}")
        elif a.cmd == "write-lock":
            lk = dev.write_lock(engage=a.action == "engage")
            print(f"session lock {'engaged (until MCU reset)' if lk['session'] else 'off'}, "
                  f"WRITE-LOCK jumper {'set' if lk['jumper'] else 'open'}")
        elif a.cmd == "profile":
            if a.action == "save":
                prof = dev.operating_profile_store()
            elif a.action:
                prof = dev.timing_preset(a.action)
            else:
                prof = dev.operating_profile()
            for k, v in prof.timing.items():
                print(f"{k:18} {v}")
            for k, v in prof.extra.items():
                if k != "reserved":
                    print(f"{k:18} {v}")
            line = {0: "none", 1: "density", 2: "drate"}.get(prof.rpm_line, str(prof.rpm_line))
            print(f"{'rpm_line':18} {line}" + (" (low = 360)" if prof.rpm_line and prof.rpm_360_low else ""))
            print(f"{'rpm':18} {prof.rpm or 'not set'}")
            print(f"{'stored profile':18} {'yes' if prof.stored else 'no'}" +
                  ("" if prof.stored or a.action == "save" else "  (ufi profile save keeps these settings for this drive)"))
        elif a.cmd == "read":
            if a.steps:
                dev.step_schedule(*_parse_steps(a.steps))
            cap = dev.read_track(a.track, a.side, a.revs, hard_sectors=a.hard_sectors, quarter=a.quarter)
            print(f"track {a.track}{'.%d' % a.quarter if a.quarter else ''}.{a.side}:\n{_summary(cap)}")
            if a.output and a.output.lower().endswith(".hfe"):
                write_hfe(a.output, _hfe_image({(a.track, a.side): cap.revolutions}, a.bitrate, a.encoding))
                print(f"wrote {a.output}")
            elif a.output:
                write_scp(a.output, {a.track * 2 + a.side: [_to_scp(r) for r in cap.revolutions]},
                          len(cap.revolutions))
                print(f"wrote {a.output}")
            if a.amiga_quality:
                import json
                from .amiga import analyze_capture
                report, _ = analyze_capture(cap, a.bitrate or (500000 if a.amiga_spt == 22 else 250000),
                                            a.track * 2 + a.side, a.amiga_spt)
                with open(a.amiga_quality, 'x', encoding='utf-8') as output:
                    json.dump(report, output, indent=2)
                print(f"Amiga quality: {'complete' if report['complete'] else 'incomplete/conflicting'}; {a.amiga_quality}")
                if not report['complete']:
                    return 1
        elif a.cmd == "read-disk":
            cmd_read_disk(dev, a)
        elif a.cmd == "read-dsk":
            from . import apple
            opts = apple.Options(bytes.fromhex(a.addr_prologue), bytes.fromhex(a.data_prologue),
                                 bytes.fromhex(a.addr_epilogue), bytes.fromhex(a.data_epilogue),
                                 sum(P2.APPLE_GCR_FLAGS[n.strip()] for n in a.flags.split("+") if n.strip())).check()
            image = apple.DiskImage(a.tracks, apple.order_for_name(a.output, a.order))
            _prepare(dev, a.drive)
            for track in range(a.tracks):
                cap = dev.read_track(track, 0, a.revs)
                dec = apple.decode_capture(cap, opts)
                good = image.add_track(track, dec)
                print(f"track {track:2d}: {good:2d}/16 sectors, {dec.header_errors} header, {dec.data_errors} data, "
                      f"{dec.code_errors} coding errors, {dec.nibbles} nibbles")
            image.write(a.output)
            print(f"{a.output}: {a.tracks} tracks, order {a.order or apple.order_for_name(a.output, None)}, "
                  f"volume(s) {sorted(image.volumes) or '-'}, missing {len(image.missing)}, weak {len(image.weak)}")
            for t, s in image.missing[:40]:
                print(f"  missing t{t} s{s}")
            for t, s in image.weak[:40]:
                print(f"  checksum/epilogue fault kept t{t} s{s}")
        elif a.cmd == "write" and a.image.lower().endswith(".hfe"):
            img = read_hfe(a.image)
            if (a.track, a.side) not in img.tracks:
                raise SystemExit(f"track {a.track}.{a.side} not in {a.image}")
            if a.steps:
                dev.step_schedule(*_parse_steps(a.steps))
            _hfe_write_tracks(dev, a, img, only=(a.track, a.side))
        elif a.cmd == "write":
            _, tracks = read_scp(a.image)
            n = a.track * 2 + a.side
            if n not in tracks:
                raise SystemExit(f"track {a.track}.{a.side} not in {a.image}")
            if a.steps:
                dev.step_schedule(*_parse_steps(a.steps))
            r = dev.write_track(a.track, a.side, scp_to_ticks(tracks[n][0].cells), a.verify,
                                hard_sectors=a.hard_sectors, quarter=a.quarter, scale=a.scale,
                                clamp=a.clamp,
                                **({'index_ticks': (tracks[n][0].index_time*P.FLUX_CLOCK_HZ+20000000)//40000000,
                                    'erase_rest': a.erase_rest} if isinstance(dev,P2.Device2) else {}))
            print(_write_text(r, a.verify) + (f", erased tail {r.erased_ticks / P.FLUX_CLOCK_HZ * 1e3:.1f} ms"
                                              if getattr(r, "erased_ticks", 0) else ""))
        elif a.cmd == "write-disk":
            cmd_write_disk(dev, a)
        elif a.cmd == "erase":
            dev.erase(a.track, a.side)
        elif a.cmd == "abort":
            dev.abort()
        elif a.cmd == "iec-status":
            print(dev.iec_status())
        elif a.cmd == "iec-identify":
            import json
            from .iec_identity import identify
            print(json.dumps(identify(dev, a.device, a.fingerprint), indent=2))
        elif a.cmd == "iec-read":
            cmd_iec_read(dev, a)
        elif a.cmd == "iec-write":
            cmd_iec_write(dev, a)
        elif a.cmd == "iec-reset":
            dev.iec_reset()
        elif a.cmd == "iec-send":
            dev.iec_send(a.byte, a.eoi)
        elif a.cmd == "iec-recv":
            b, eoi = dev.iec_receive()
            print(f"0x{b:02X}" + (" EOI" if eoi else ""))
        elif a.cmd == "reset":
            dev.reset()
        elif a.cmd == "bootloader":
            dev.bootloader()
            print("device restarts into the USB-DFU bootloader (dfu-util -a 0 ...)")
    except (P.DeviceError, ValueError, TimeoutError, OSError) as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    finally:
        if isinstance(dev, P2.Device2) and hasattr(dev.s, "cancel"):
            try:
                dev.close()
            except (P.DeviceError, TimeoutError, OSError):
                pass  # original operation result takes precedence during disconnect
    return 0
