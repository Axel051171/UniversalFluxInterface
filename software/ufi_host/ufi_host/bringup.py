"""Board bring-up without a drive: every check a bare UFI board can pass over USB alone.

Run:  python -m ufi_host.bringup [--port COMx] [--report bringup.json] [--index-sim]

The sequence follows docs/Bringup_2026-10-24.md: identification, protocol discovery,
diagnostics, supplies, SD NAND, the firmware self-test with the expectations of a board
without drive termination networks, and the discovery queries of the 1.18 analysis ops.
Nothing here moves a head, opens WGATE or needs the FDD bus.  Every step is tolerant: a
device that refuses a request is recorded as "skip", not as a failure, so older firmware
still yields a readable report.  Exit code 1 when any step failed.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass

from . import protocol as P
from . import protocol2 as P2

# self-test expectations on a bare board: (acceptable statuses, note)
SELFTEST_EXPECT = {
    "timer": ((0,), "TIM2 must run at 275 MHz; fail = clock tree"),
    "usb_sof": ((0, 3), "crystal vs. USB frames; warn is acceptable, fail = Y1"),
    "psram": ((0,), "U11 pattern test; fail value = first bad offset"),
    "sd": ((0, 2, 3), "U15 capacity; skip/warn when the SD NAND is absent"),
    "rail_5v": ((0, 2), "skip while the drive supply is off"),
    "rail_12v": ((0, 2), "skip while the drive supply is off"),
    "board_id": ((0, 3), "warn = divider value of no known revision"),
    "usbc_cc": ((0, 3), "warn = USB-A cable without CC"),
    "fdd_lines": ((0, 3), "WARN expected while RN1-RN8 are not fitted (inputs float); ok once fitted"),
    "iec_loop": ((0, 3), "transceivers idle; fail = line held asserted"),
    "index_loop": ((0, 2), "skip without the J9 pin 6 -> INDEX wire"),
    "last_reset": ((0,), "a fault record means the last run crashed (CRASH.LOG)"),
    "stack_free": ((0, 3), "fail below 512 bytes"),
    "log": ((0, 1, 2), "SELFTEST.LOG written when the SD NAND works; fail = no SD NAND (tolerated)"),
    # board v1.0, firmware stage A10
    "rtc": ((0, 2), "v1.0: the calendar must run (Y3 LSE, BT1); skip on v0.x"),
    "sht40": ((0, 2), "v1.0: U25 must answer; skip on v0.x"),
    "sync": ((0, 2), "skip without a spinning Apple drive with sensor"),
    "v_fdd5": ((0, 2), "skip while the drive supply is off or without dividers"),
    "v_fdd12": ((0, 2), "skip while the drive supply is off or without dividers"),
    "wgate_lock": ((0, 2), "v1.0: WGATE must stay off with the lock set (A11f); skip on v0.x"),
    "fdd_pins": ((0, 2, 3), "v1.0 read-backs; warn = WGATE bus or eFuse fault seen at idle"),
}


@dataclass
class Step:
    name: str
    status: str                 # ok | warn | fail | skip
    detail: str


class Report:
    def __init__(self) -> None:
        self.steps: list[Step] = []
        self.started = time.time()

    def add(self, name: str, status: str, detail: str = "") -> None:
        self.steps.append(Step(name, status, detail))
        print(f"{status.upper():5s} {name:28s} {detail}", flush=True)

    def guarded(self, name: str, fn):
        """Run fn(); DeviceError / ValueError / TimeoutError -> skip with the message."""
        try:
            return fn()
        except (P.DeviceError, P2.DeviceError, ValueError, TimeoutError, OSError) as e:
            self.add(name, "skip", f"not available: {e}")
            return None

    @property
    def failed(self) -> int:
        return sum(1 for s in self.steps if s.status == "fail")

    def to_json(self) -> str:
        counts = {k: sum(1 for s in self.steps if s.status == k) for k in ("ok", "warn", "fail", "skip")}
        return json.dumps(dict(started=self.started, seconds=round(time.time() - self.started, 2),
                               counts=counts, steps=[asdict(s) for s in self.steps]), indent=2)


def run(dev: P2.Device2, rep: Report, index_sim: bool = False) -> int:
    """All bare-board checks against an open Device2; returns the number of failed steps."""
    # 1 identification
    if rep.guarded("ping", dev.ping):
        rep.add("ping", "ok", "v2 echo")
    info = rep.guarded("info", dev.device_info)
    if info:
        ok = info.proto == 2 and info.sample_hz == 275_000_000
        rep.add("info", "ok" if ok else "fail",
                f"proto {info.proto}, firmware {info.fw_major}.{info.fw_minor}, board rev {info.board_rev}, "
                f"caps 0x{info.caps:08x}, {info.sample_hz} Hz, store {info.store_bytes} B, payload {info.max_payload}")
        for line in info.text:
            rep.add("info.text", "ok", line)
        if not info.board_rev:
            rep.add("board_rev", "warn", "BOARD_ID divider gave no known revision")
    # 2 protocol discovery
    d = rep.guarded("describe", dev.describe)
    if d:
        rep.add("describe", "ok", f"transports 0x{d.transports:x}, flags 0x{d.flags:x}, native {d.native_hz} Hz, "
                                  f"store {d.store_words} words, max payload {d.max_payload}")
    feats = rep.guarded("features", dev.features)
    if feats is not None:
        missing = [f["id"] for f in feats if f["supported"] and not f["available"]]
        rep.add("features", "ok" if not missing else "warn",
                f"{len(feats)} features" + (f", supported but unavailable: {missing}" if missing else ""))
    # 3 diagnostics
    diag = rep.guarded("diagnostics", dev.diagnostics)
    if diag:
        errors = {k: diag[k] for k in ("capture_overruns", "capture_dma_errors", "write_dma_errors", "write_underruns") if diag[k]}
        rep.add("diagnostics", "ok" if not errors else "warn",
                f"psram {'on' if diag['psram'] else 'OFF (fallback store)'}, store {diag['store_words']} words, "
                f"AXI free {diag['axi_free_bytes']} B, stack space {diag['stack_space_bytes']} B"
                + (f", error counters {errors}" if errors else ""))
        if not diag["psram"]:
            rep.add("psram_store", "fail", "flux store is the internal fallback: U11 not detected")
    # 4 supplies and board state
    bs = rep.guarded("board_status", dev.power)
    if bs:
        flags = bs.flags
        bad = []
        if flags & 0x06:
            bad.append("overcurrent trip latched")
        if flags & 0x40:
            bad.append("safe mode")
        rep.add("board_status", "ok" if not bad else "fail",
                f"power 0x{bs.power:x}, flags 0x{flags:02x} (lock jumper {'set' if flags & 1 else 'open'}, "
                f"SD {'ready' if flags & 8 else 'absent'}), 5 V {bs.i5_ma} mA, 12 V {bs.i12_ma} mA, "
                f"BOARD_ID {bs.board_id_mv} mV" + (f"; {', '.join(bad)}" if bad else ""))
    cc = rep.guarded("usb_power", dev.usb_power)
    if cc:
        rep.add("usb_power", "ok", f"USB-C values {cc}")
    sd = rep.guarded("sd_info", dev.sd_info)
    if sd:
        present, status, card_type, width, cap = sd
        rep.add("sd_info", "ok" if present and cap else "warn",
                f"present {present}, status {status}, type {card_type}, bus {width}, {cap} MB")
    # 4b board v1.0 sensing (firmware stage A10; older firmware answers 'unknown command' -> skipped)
    v10 = bool(info) and info.board_rev >= 10
    clk = rep.guarded("rtc", dev.rtc)
    if clk:
        rep.add("rtc", "ok" if clk.ready or not v10 else "fail", str(clk))
    sens = rep.guarded("sensor", dev.sensor)
    if sens:
        rep.add("sensor", "ok" if sens.ok else ("fail" if v10 else "skip"), str(sens))
    rl = rep.guarded("rails", dev.rails)
    if rl:
        rep.add("rails", "fail" if rl.low else "ok", str(rl))
    # 5 firmware self-test with bare-board expectations
    items = rep.guarded("selftest", dev.selftest)
    if items:
        for _id, name, st, val in items:
            allowed, note = SELFTEST_EXPECT.get(name, ((0,), ""))
            status = {0: "ok", 2: "skip", 3: "warn"}.get(st, "fail")
            if st in allowed and status == "fail":
                status = "warn"                 # tolerated on the bare board (SD NAND absent etc.)
            elif st not in allowed and status != "fail":
                status = "warn"                 # unexpected skip/warn: look at the value
            if name == "usb_sof" and val >= 1 << 31:
                val -= 1 << 32                  # ppm is signed
            rep.add(f"selftest.{name}", status, f"{P2.SELFTEST_STATUS.get(st, st)} value {val}; {note}")
    # 6 discovery of the 1.18 analysis ops (pure queries)
    for name, fn, text in (
        ("flag_masks", dev.flag_masks, lambda r: f"read 0x{r['read_flags']:02x}, write2 0x{r['write_flags2']:02x}"),
        ("write_limits", dev.write_limits, lambda r: f"flags 0x{r.flags:02x}, {r.min_ticks}..{r.max_ticks} ticks, {r.max_flux} words"),
        ("provenance", dev.provenance, lambda r: f"board rev {r.get('board_rev')}, git {r.get('git_rev')}, "
                                                 f"uptime {r.get('uptime_ms')} ms, media token {r.get('media_token')}"),
        ("last_refusal", dev.last_refusal, lambda r: f"command 0x{r['command']:02x} {r['field_name']}"),
        ("speed_check", dev.speed_check, lambda r: f"tolerance {r['tolerance_permille']} per mille"),
        ("raw_verify_options", dev.raw_verify_options, lambda r: f"flags {r['flags']}, tolerance {r['tolerance_permille']}"),
        ("track_survey", dev.track_survey_status, lambda r: f"state {r['state_name']}, entry size {r['entry_size']}"),
        ("operating_profile", dev.operating_profile, lambda r: "read"),
    ):
        r = rep.guarded(name, fn)
        if r is not None:
            try:
                rep.add(name, "ok", text(r))
            except (KeyError, AttributeError, TypeError) as e:
                rep.add(name, "warn", f"reply decoded with unexpected fields: {e}")
    # 7 optional: internal index simulation and rpm measurement without a drive
    if index_sim:
        st = rep.guarded("index_sim", lambda: dev.index_sim(300, mode=P2.INDEX_SIM_INTERNAL))
        if st:
            rpm = rep.guarded("rpm_from_sim", dev.rpm)
            if rpm is not None:
                rep.add("rpm_from_sim", "ok" if 295 <= rpm <= 305 else "fail", f"{rpm} rpm from the internal 300 rpm simulation")
            rep.guarded("index_sim_off", lambda: dev.index_sim(0))
    else:
        st = rep.guarded("index_sim_state", dev.index_sim)
        if st:
            rep.add("index_sim_state", "ok", f"rpm {st[0]}, mode {st[1]} (use --index-sim to measure)")
    return rep.failed


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", default=None, help="serial port (default: first UFI device)")
    ap.add_argument("--report", default=None, metavar="PATH", help="write the JSON report here")
    ap.add_argument("--index-sim", action="store_true", help="also measure rpm from the internal index simulation")
    a = ap.parse_args(argv)
    rep = Report()
    dev = P2.connect(a.port)
    if not isinstance(dev, P2.Device2):
        print("error: the board answers with the v1 protocol only; flash a 1.18 firmware first", file=sys.stderr)
        return 2
    failed = run(dev, rep, index_sim=a.index_sim)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write(rep.to_json())
    counts = {k: sum(1 for s in rep.steps if s.status == k) for k in ("ok", "warn", "fail", "skip")}
    print(f"bring-up: {counts['ok']} ok, {counts['warn']} warn, {counts['fail']} fail, {counts['skip']} skip")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
