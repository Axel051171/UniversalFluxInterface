"""ufi - command line host tool for the UFI Flux Engine (UFI Headless board).

  ufi info | status | selftest | rpm
  ufi select {a,b,amiga,iec,none} | motor {on,off} | seek N | recal | side {0,1}
  ufi read TRACK SIDE [-r REVS] [-o out.scp]          one track, summary or SCP
  ufi read-disk -o disk.scp [--tracks 80] [--sides 2] [-r 3] [--drive a]
  ufi write disk.scp TRACK SIDE [--verify]            rev 0 of that SCP track
  ufi write-disk disk.scp [--verify] [--drive a]
  ufi erase TRACK SIDE | abort
  ufi iec-reset | iec-send BYTE [--eoi] | iec-recv
  ufi reset | bootloader
"""
from __future__ import annotations

import argparse
import sys

from . import protocol as P
from .scp import ScpRevolution, read_scp, scp_to_ticks, ticks_to_scp, write_scp


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


def _prepare(dev: P.Device, drive: str) -> None:
    dev.select(drive)
    dev.motor(True)
    dev.recalibrate()


def cmd_read_disk(dev: P.Device, a) -> None:
    _prepare(dev, a.drive)
    tracks = {}
    try:
        for cyl in range(a.tracks):
            for head in range(a.sides):
                cap = dev.read_track(cyl, head, a.revs)
                tracks[cyl * 2 + head] = [_to_scp(r) for r in cap.revolutions]
                print(f"track {cyl:2d}.{head}: {len(cap.revolutions)} revs, "
                      f"{cap.revolutions[0].rpm if cap.revolutions else 0:5.1f} rpm", flush=True)
    finally:
        dev.motor(False)
    revs = min(len(v) for v in tracks.values())
    write_scp(a.output, tracks, revs, heads=0 if a.sides == 2 else 1)
    print(f"wrote {a.output}: {len(tracks)} tracks x {revs} revolutions")


def cmd_write_disk(dev: P.Device, a) -> None:
    _, tracks = read_scp(a.image)
    _prepare(dev, a.drive)
    try:
        for n in sorted(tracks):
            dev.write_track(n // 2, n % 2, scp_to_ticks(tracks[n][0].cells), a.verify)
            print(f"track {n // 2:2d}.{n % 2}: written" + (" + verified" if a.verify else ""), flush=True)
    finally:
        dev.motor(False)


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(prog="ufi", description="UFI Flux Engine host tool",
                                 formatter_class=argparse.RawDescriptionHelpFormatter, epilog=__doc__)
    ap.add_argument("--port", help="serial port (default: auto-detect VID 1209 / PID 4F54)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    for name in ("info", "status", "selftest", "rpm", "recal", "abort", "iec-reset", "iec-recv",
                 "reset", "bootloader"):
        sub.add_parser(name)
    sub.add_parser("select").add_argument("drive", choices=list(P.DRIVES))
    sub.add_parser("motor").add_argument("state", choices=("on", "off"))
    sub.add_parser("seek").add_argument("track", type=int)
    sub.add_parser("side").add_argument("side", type=int, choices=(0, 1))
    p = sub.add_parser("read")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("-o", "--output")
    p = sub.add_parser("read-disk")
    p.add_argument("-o", "--output", required=True)
    p.add_argument("--tracks", type=int, default=80)
    p.add_argument("--sides", type=int, default=2, choices=(1, 2))
    p.add_argument("-r", "--revs", type=int, default=3)
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p = sub.add_parser("write")
    p.add_argument("image")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p.add_argument("--verify", action="store_true")
    p = sub.add_parser("write-disk")
    p.add_argument("image")
    p.add_argument("--verify", action="store_true")
    p.add_argument("--drive", default="a", choices=list(P.DRIVES))
    p = sub.add_parser("erase")
    p.add_argument("track", type=int)
    p.add_argument("side", type=int, choices=(0, 1))
    p = sub.add_parser("iec-send")
    p.add_argument("byte", type=lambda s: int(s, 0))
    p.add_argument("--eoi", action="store_true")
    a = ap.parse_args(argv)

    dev = P.Device(P.open_serial(a.port))
    try:
        if a.cmd == "info":
            print("\n".join(dev.info()))
        elif a.cmd == "status":
            print(dev.status())
        elif a.cmd == "selftest":
            r = dev.selftest()
            print(" ".join(f"{n}={'OK' if r & (1 << i) else 'FAIL'}"
                           for i, n in enumerate(("timer", "dma", "usb", "gpio"))))
        elif a.cmd == "rpm":
            print(f"{dev.rpm()} rpm")
        elif a.cmd == "select":
            dev.select(a.drive)
        elif a.cmd == "motor":
            dev.motor(a.state == "on")
        elif a.cmd == "seek":
            dev.seek(a.track)
        elif a.cmd == "recal":
            dev.recalibrate()
        elif a.cmd == "side":
            dev.side(a.side)
        elif a.cmd == "read":
            cap = dev.read_track(a.track, a.side, a.revs)
            print(f"track {a.track}.{a.side}:\n{_summary(cap)}")
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
            dev.write_track(a.track, a.side, scp_to_ticks(tracks[n][0].cells), a.verify)
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
