"""ufi - command line host tool for the UFI Flux Engine (UFI Headless board).

Talks UFI v2 (docs/USB_Protokoll.md, section 3); falls back to v1 for old firmware.

  ufi info | status | caps | selftest | rpm
  ufi select {a,b,amiga,iec,none,...} | motor {on,off} | seek N | recal | side {0,1}
  ufi read TRACK SIDE [-r REVS] [-o out.scp]          one track, summary or SCP
  ufi read-disk -o disk.scp [--tracks 80] [--sides 2] [-r 3] [--drive a]
  ufi write disk.scp TRACK SIDE [--verify]            rev 0 of that SCP track
  ufi write-disk disk.scp [--verify] [--drive a]
  ufi erase TRACK SIDE | abort
  ufi iec-reset | iec-send BYTE [--eoi] | iec-recv
  ufi files [PATH] | get NAME [-o out] | put LOCAL NAME | rm NAME       (v2, SD NAND)
  ufi cfg [KEY=VALUE ...]                             show / edit UFI.CFG (v2)
  ufi dump [--drive a --tracks 80 --sides 2 -r 3] | dump-status | dump-abort | copy
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
from .scp import ScpRevolution, read_scp, scp_to_ticks, ticks_to_scp, write_scp

CFG_NAME = "UFI.CFG"


def _to_scp(rev: P.Revolution) -> ScpRevolution:
    return ScpRevolution((rev.index_ticks * 40 + 137) // 275, ticks_to_scp(rev.samples))


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
    _prepare(dev, a.drive)
    tracks = {}
    try:
        for cyl in range(a.tracks):
            for head in range(a.sides):
                cap = dev.read_track(cyl, head, a.revs, hard_sectors=a.hard_sectors)
                tracks[cyl * 2 + head] = [_to_scp(r) for r in cap.revolutions]
                print(f"track {cyl:2d}.{head}: {len(cap.revolutions)} revs, "
                      f"{cap.revolutions[0].rpm if cap.revolutions else 0:5.1f} rpm", flush=True)
    finally:
        dev.motor(False)
    revs = min(len(v) for v in tracks.values())
    write_scp(a.output, tracks, revs, heads=0 if a.sides == 2 else 1)
    print(f"wrote {a.output}: {len(tracks)} tracks x {revs} revolutions")


def cmd_write_disk(dev, a) -> None:
    _, tracks = read_scp(a.image)
    _prepare(dev, a.drive)
    try:
        for n in sorted(tracks):
            dev.write_track(n // 2, n % 2, scp_to_ticks(tracks[n][0].cells), a.verify,
                            hard_sectors=a.hard_sectors)
            print(f"track {n // 2:2d}.{n % 2}: written" + (" + verified" if a.verify else ""), flush=True)
    finally:
        dev.motor(False)


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
        dev.dump_start(a.drive, a.tracks, a.sides, a.revs)
    elif a.cmd == "dump-status":
        print(dev.dump_status())
    elif a.cmd == "dump-abort":
        dev.dump_abort()
    elif a.cmd == "copy":
        dev.copy_start()
    elif a.cmd == "power":
        print(dev.power(a.mask))
    else:
        return False
    return True


V2_ONLY = {"caps", "files", "get", "put", "rm", "cfg", "events", "mode", "dump", "dump-status", "scan", "index-sim", "rpm-select", "iec-nib",
           "dump-abort", "copy", "power"}


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="ufi", description="UFI Flux Engine host tool",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--port", help="serial port (default: auto-detect VID 1209 / PID 4F54)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("rpm", help="spindle speed from the index pulses").add_argument(
        "revs", type=int, nargs="?", default=5, help="revolutions to average (default 5)")
    sub.add_parser("scan", help="which drives answer (moves the heads)").add_argument(
        "bus", nargs="?", choices=("pc", "ds"), default="pc", help="pc: a/b/amiga/amiga2, ds: Shugart bus ds0-ds3")
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
    sub.add_parser("timing", help="show/set drive timings, e.g. timing step_rate_us=6000").add_argument(
        "set", nargs="*", metavar="FIELD=VALUE")
    sub.add_parser("select").add_argument("drive", choices=list(P.DRIVES))
    sub.add_parser("motor").add_argument("state", choices=("on", "off"))
    q_help = "Apple Disk II quarter-track offset 0-3 (2 = half track), firmware >= 1.14"
    p = sub.add_parser("seek")
    p.add_argument("track", type=int)
    p.add_argument("--quarter", type=int, default=0, choices=(0, 1, 2, 3), help=q_help)
    sub.add_parser("side").add_argument("side", type=int, choices=(0, 1))
    hs_help = "hard-sectored disk: number of sector holes (10/16/32), firmware >= 1.14"
    p = sub.add_parser("read")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("-o", "--output")
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p.add_argument("--quarter", type=int, default=0, choices=(0, 1, 2, 3), help=q_help)
    st_help = "head steps during the operation (firmware >= 1.14): MS:TRACK[.Q],... e.g. 50:17.1,100:17.2"
    p.add_argument("--steps", metavar="SPEC", help=st_help)
    p = sub.add_parser("read-disk")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--tracks", type=int, default=80)
    p.add_argument("--sides", type=int, default=2, choices=(1, 2))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p = sub.add_parser("write")
    p.add_argument("image")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("--verify", action="store_true")
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
    p.add_argument("--quarter", type=int, default=0, choices=(0, 1, 2, 3), help=q_help)
    p.add_argument("--steps", metavar="SPEC", help=st_help)
    p = sub.add_parser("write-disk")
    p.add_argument("image")
    p.add_argument("--hard-sectors", type=int, default=0, metavar="N", help=hs_help)
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
    sub.add_parser("power", help="board status; MASK bit0 FDD_5V, bit1 FDD_12V").add_argument(
        "mask", nargs="?", type=lambda s: int(s, 0))
    a = ap.parse_args(argv)

    dev = P2.connect(a.port)
    try:
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
        elif a.cmd == "iec-nib":
            from .nib import NibReader, write_g64
            rd = NibReader(dev, a.device)
            rd.calibrate()
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
        elif a.cmd == "read":
            if a.steps:
                dev.step_schedule(*_parse_steps(a.steps))
            cap = dev.read_track(a.track, a.side, a.revs, hard_sectors=a.hard_sectors, quarter=a.quarter)
            print(f"track {a.track}{'.%d' % a.quarter if a.quarter else ''}.{a.side}:\n{_summary(cap)}")
            if a.output:
                write_scp(a.output, {a.track * 2 + a.side: [_to_scp(r) for r in cap.revolutions]},
                          len(cap.revolutions))
                print(f"wrote {a.output}")
        elif a.cmd == "read-disk":
            cmd_read_disk(dev, a)
        elif a.cmd == "write":
            _, tracks = read_scp(a.image)
            n = a.track * 2 + a.side
            if n not in tracks:
                raise SystemExit(f"track {a.track}.{a.side} not in {a.image}")
            if a.steps:
                dev.step_schedule(*_parse_steps(a.steps))
            dev.write_track(a.track, a.side, scp_to_ticks(tracks[n][0].cells), a.verify,
                            hard_sectors=a.hard_sectors, quarter=a.quarter)
            print("written" + (" + verified" if a.verify else ""))
        elif a.cmd == "write-disk":
            cmd_write_disk(dev, a)
        elif a.cmd == "erase":
            dev.erase(a.track, a.side)
        elif a.cmd == "abort":
            dev.abort()
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
    except P.DeviceError as e:
        print(f"error: {e}", file=sys.stderr)
        return 1
    return 0
