"""Board bring-up: every check a UFI board can pass over USB, v1.0 bench sequence included.

Run:  python -m ufi_host.bringup [--port COMx] [--report bringup.json] [--expect-rev 10]
                                 [--index-sim] [--no-12v] [--keep-rtc]
                                 [--drive a [--read-revs 3] [--write-test --write-track 2]]

The sequence follows docs/Bringup_2026-10-24.md (section 3 for every board, section 7
"v1.0 Schnellpruefung" for the v1.0 board): identification, protocol discovery, diagnostics,
supplies, SD NAND, the v1.0 sensing (RTC set and read, SHT40 range check), the firmware
self-test, the clock source, the drive rails switched off and on with the eFuse fault bits,
the discovery queries of the 1.18 analysis ops and, only with --drive, one READ with the read
context block.  Without --drive nothing moves a head; without --write-test nothing opens WGATE
(and --write-test needs --drive and a scratch disk).

Every step reports ok (PASS), warn, fail or skip.  A device that refuses a request is recorded
as "skip", not as a failure, so older firmware and v0.x boards still yield a readable report:
the v1.0-only steps skip there.  Exit code 1 when any step failed.
"""
from __future__ import annotations

import argparse
import json
import sys
import time
from dataclasses import asdict, dataclass, field
from datetime import datetime

from . import protocol as P
from . import protocol2 as P2

# patched by the tests (no real waiting, deterministic clock)
SLEEP = time.sleep
NOW = datetime.now

# self-test expectations on a bare board: (acceptable statuses, note)
SELFTEST_EXPECT = {
    "timer": ((0,), "TIM2 must run at 275 MHz; fail = clock tree"),
    "usb_sof": ((0, 3), "clock vs. USB frames; warn is acceptable, fail = HSE source"),
    "psram": ((0,), "U11 pattern test; fail value = first bad offset"),
    "sd": ((0, 2, 3), "U15 capacity; skip/warn when the SD NAND is absent"),
    "rail_5v": ((0, 2), "skip while the drive supply is off"),
    "rail_12v": ((0, 2), "skip while the drive supply is off"),
    "board_id": ((0, 3), "warn = divider value of no known revision"),
    "usbc_cc": ((0, 3), "warn = USB-A cable without CC"),
    "fdd_lines": ((0, 3), "WARN expected while RN1-RN8 are not fitted (inputs float) or a drive pulls a line"),
    "iec_loop": ((0, 3), "transceivers idle; fail = line held asserted"),
    "index_loop": ((0, 2), "skip without the J9 pin 6 -> INDEX wire (v0.7)"),
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

# v1.0 board with A10 firmware: these items must be ok, anything else is a failure (the
# assembled board carries every part; nothing is optional any more)
V10_STRICT = {
    "psram": "U11",
    "sd": "U15 SD NAND (assembled on v1.0)",
    "log": "SELFTEST.LOG on U15",
    "board_id": "R17/R18 divider must give revision 10",
    "rtc": "Y3/C76/C77 (LSE) or BT1",
    "sht40": "U25 / R19 / R20",
    "wgate_lock": "U31 / Q9 / U10F (value 1 = gate leaks through the lock, 2 = bus active at idle)",
    "index_loop": "PE5 -> U7F -> INDEX bus is hardwired on v1.0; skip = pulse did not arrive",
}
FDD_WGATE_BUS, FDD_FLT5, FDD_FLT12 = 0x08, 0x20, 0x40

# v1.0 rail windows at the connector (TPS25921A eFuse + 50 mOhm shunt in the path, no load)
V5_WINDOW = (4750, 5250)
V12_WINDOW = (11400, 12600)
RAIL_OFF_MAX_MV = 1000          # above this with the rail off: something back-feeds the rail
IDLE_CURRENT_MAX_MA = 30        # per rail without a drive (INA180 offset, Inbetriebnahme 4.2: < 20 mA)
RPM_WINDOWS = ((290, 310), (350, 370))
WRITE_TEST_TICKS = 1100         # 4 us intervals at 275 MHz
WRITE_TEST_COUNT = 47_000       # about 94 % of a 300 rpm revolution
SHUGART_DRIVES = ("a", "b", "ds0", "ds1", "ds2", "ds3", "amiga", "amiga2")


@dataclass
class Step:
    name: str
    status: str                 # ok | warn | fail | skip
    detail: str
    data: dict | None = field(default=None)


class Report:
    def __init__(self) -> None:
        self.steps: list[Step] = []
        self.started = time.time()
        self.meta: dict = {}

    def add(self, name: str, status: str, detail: str = "", data: dict | None = None) -> None:
        self.steps.append(Step(name, status, detail, data))
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

    def counts(self) -> dict:
        return {k: sum(1 for s in self.steps if s.status == k) for k in ("ok", "warn", "fail", "skip")}

    def to_json(self) -> str:
        return json.dumps(dict(started=self.started, seconds=round(time.time() - self.started, 2),
                               verdict="FAIL" if self.failed else "PASS", meta=self.meta,
                               counts=self.counts(), steps=[asdict(s) for s in self.steps]), indent=2)


def _quiet(fn):
    """Call fn() and swallow device refusals (clean-up and preparation steps)."""
    try:
        return fn()
    except (P.DeviceError, P2.DeviceError, ValueError, TimeoutError, OSError):
        return None


def _in(v: int, window: tuple[int, int]) -> bool:
    return window[0] <= v <= window[1]


@dataclass
class Context:
    """What the steps learn about the board on the way."""
    v10: bool = False               # INFO board_rev 10
    a10: bool = False               # firmware reports the A10 self-test items (ids 15..)
    power: int | None = None        # board_status.power at the start
    usb_sof_ppm: int | None = None
    selftest: dict = field(default_factory=dict)


# ---- 1-4 identification, discovery, diagnostics, supplies --------------------------------
def _identify(dev: P2.Device2, rep: Report, ctx: Context, expect_rev: int | None):
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
        rep.meta.update(firmware=f"{info.fw_major}.{info.fw_minor}", board_rev=info.board_rev, info_text=info.text)
        ctx.v10 = info.board_rev >= 10
        if expect_rev is not None:
            rep.add("board_rev", "ok" if info.board_rev == expect_rev else "fail",
                    f"BOARD_ID gives revision {info.board_rev} ({P2.board_rev_text(info.board_rev)}), expected {expect_rev}")
        elif not info.board_rev:
            rep.add("board_rev", "warn", "BOARD_ID divider gave no known revision")
        else:
            rep.add("board_rev", "ok", f"{P2.board_rev_text(info.board_rev)}"
                    + ("" if ctx.v10 else "; v1.0-only steps are skipped"))
    elif expect_rev is not None:
        rep.add("board_rev", "fail", f"no INFO reply, expected revision {expect_rev}")
    return info


def _discovery(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    d = rep.guarded("describe", dev.describe)
    if d:
        rep.add("describe", "ok", f"transports 0x{d.transports:x}, flags 0x{d.flags:x}, native {d.native_hz} Hz, "
                                  f"store {d.store_words} words, max payload {d.max_payload}")
    feats = rep.guarded("features", dev.features)
    if feats is not None:
        missing = [f["id"] for f in feats if f["supported"] and not f["available"]]
        rep.add("features", "ok" if not missing else "warn",
                f"{len(feats)} features" + (f", supported but unavailable: {missing}" if missing else ""))
        if ctx.v10:
            by_id = {f["id"]: f for f in feats}
            v10_ids = (11, 12, 13, 14)
            absent = [i for i in v10_ids if i not in by_id]
            if len(absent) == len(v10_ids):
                rep.add("features.v10", "skip", "firmware before stage A10 (no feature ids 11-14)")
            else:
                down = [P2.FEATURE_NAMES.get(i, str(i)) for i in v10_ids
                        if i in by_id and by_id[i]["supported"] and not by_id[i]["available"]]
                rep.add("features.v10", "fail" if down else "ok",
                        "RTC, SHT40, SYNC, rail voltages " + (f"unavailable: {down}" if down else "available"))


def _diagnostics(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    diag = rep.guarded("diagnostics", dev.diagnostics)
    if diag:
        errors = {k: diag[k] for k in ("capture_overruns", "capture_dma_errors", "write_dma_errors", "write_underruns") if diag[k]}
        rep.add("diagnostics", "ok" if not errors else "warn",
                f"psram {'on' if diag['psram'] else 'OFF (fallback store)'}, store {diag['store_words']} words, "
                f"AXI free {diag['axi_free_bytes']} B, stack space {diag['stack_space_bytes']} B"
                + (f", error counters {errors}" if errors else ""))
        if not diag["psram"]:
            rep.add("psram_store", "fail", "flux store is the internal fallback: U11 not detected")


def _supplies(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    bs = rep.guarded("board_status", dev.power)
    if bs:
        ctx.power = bs.power & 0x0F
        flags = bs.flags
        bad = []
        if flags & 0x06:
            bad.append("overcurrent trip latched")
        if flags & 0x40:
            bad.append("safe mode")
        if ctx.v10 and bs.power & 0x0C:
            bad.append(f"eFuse fault {'5 V ' if bs.power & 4 else ''}{'12 V' if bs.power & 8 else ''}".strip())
        rep.add("board_status", "ok" if not bad else "fail",
                f"power 0x{bs.power:x}, flags 0x{flags:02x} (lock {'set' if flags & 1 else 'open'}, "
                f"SD {'ready' if flags & 8 else 'absent'}), 5 V {bs.i5_ma} mA, 12 V {bs.i12_ma} mA, "
                f"BOARD_ID {bs.board_id_mv} mV" + (f"; {', '.join(bad)}" if bad else ""))
    cc = rep.guarded("usb_power", dev.usb_power)
    if cc:
        rep.add("usb_power", "ok", f"USB-C values {cc}")
    sd = rep.guarded("sd_info", dev.sd_info)
    if sd:
        present, status, card_type, width, cap = sd
        good = bool(present and cap)
        rep.add("sd_info", "ok" if good else ("fail" if ctx.v10 else "warn"),
                f"present {present}, status {status}, type {card_type}, bus {width}, {cap} MB"
                + ("" if good or not ctx.v10 else "; U15 is assembled on v1.0"))


# ---- 5 v1.0 sensing: RTC, SHT40 ---------------------------------------------------------
def _rtc(dev: P2.Device2, rep: Report, ctx: Context, keep: bool) -> None:
    clk = rep.guarded("rtc", dev.rtc)
    if clk is None:
        return
    if not clk.ready:
        rep.add("rtc", "fail" if ctx.v10 else "skip",
                str(clk) + ("; v1.0: LSE Y3/C76/C77 or BT1" if ctx.v10 else ""))
        return
    rep.add("rtc", "ok", f"running, {clk}")
    if keep:
        drift = (clk.datetime - NOW()).total_seconds()
        rep.add("rtc.hold", "ok" if abs(drift) <= 5 else "warn",
                f"calendar vs. PC {drift:+.0f} s (--keep-rtc: after a power cut this shows the CR2032 hold-over; "
                f"warn = never set or drifted)", dict(drift_s=drift))
        return
    want = NOW().replace(microsecond=0)
    got = rep.guarded("rtc.set", lambda: dev.rtc_set(want))
    if got is None:
        return
    diff = (got.datetime - want).total_seconds() if got.ready else None
    if diff is None or abs(diff) > 2:
        rep.add("rtc.set", "fail", f"set {want.isoformat()}, read back {got}")
        return
    rep.add("rtc.set", "ok", f"set to the PC time {want.isoformat()}, read back {diff:+.0f} s")
    SLEEP(2.2)
    later = rep.guarded("rtc.runs", dev.rtc)
    if later is None:
        return
    step = (later.datetime - got.datetime).total_seconds() if later.ready else None
    rep.add("rtc.runs", "ok" if step is not None and 1 <= step <= 4 else "fail",
            f"advanced {step} s in 2.2 s" + ("" if step is not None and 1 <= step <= 4 else "; LSE not oscillating"))


def _sensor(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    sens = rep.guarded("sensor", dev.sensor)
    if sens is None:
        return
    if not sens.ok:
        rep.add("sensor", "fail" if ctx.v10 else "skip", str(sens) + ("; v1.0: U25/R19/R20/TP10-TP13" if ctx.v10 else ""))
        return
    plausible = 5.0 <= sens.temp_c <= 45.0 and 10.0 <= sens.rh <= 90.0
    rep.add("sensor", "ok" if plausible else "warn",
            str(sens) + ("" if plausible else "; outside 5-45 C / 10-90 %RH: compare with a room thermometer"),
            dict(temp_c=sens.temp_c, rh=sens.rh))


# ---- 6 self-test ------------------------------------------------------------------------
def _fdd_pins_verdict(value: int, power: int | None) -> tuple[str, str]:
    """Selftest fdd_pins / FDD_PINS: the WGATE bus at idle is always a fault; FLT5/FLT12 only count
    while the rail is on (ENUV low reads as undervoltage, so FLT is asserted with the rail off)."""
    names = [n for i, n in enumerate(P2.FDD_PIN_NAMES) if value & (1 << i)]
    if value & FDD_WGATE_BUS:
        return "fail", f"WGATE bus active at idle [{', '.join(names)}]: U31/Q9/U10F"
    on5, on12 = (power or 0) & 1, (power or 0) & 2
    flt = [n for bit, n, on in ((FDD_FLT5, "FLT5", on5), (FDD_FLT12, "FLT12", on12)) if value & bit and on]
    if flt:
        return "fail", f"eFuse fault with the rail on: {', '.join(flt)} [{', '.join(names)}]"
    off = [n for bit, n, on in ((FDD_FLT5, "FLT5", on5), (FDD_FLT12, "FLT12", on12)) if value & bit and not on]
    return "ok", f"[{', '.join(names) or '-'}]" + (f"; {', '.join(off)} with the rail off is expected (ENUV low)" if off else "")


def _selftest(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    if ctx.v10:
        _quiet(lambda: dev.select(0))       # wgate_lock and index_loop run only with nothing selected
    items = rep.guarded("selftest", dev.selftest)
    if not items:
        return
    ctx.a10 = any(i >= 15 for i, *_ in items)
    strict = ctx.v10 and ctx.a10
    for _id, name, st, val in items:
        allowed, note = SELFTEST_EXPECT.get(name, ((0,), ""))
        status = {0: "ok", 2: "skip", 3: "warn"}.get(st, "fail")
        if name == "usb_sof" and val >= 1 << 31:
            val -= 1 << 32                  # ppm is signed
        if name == "usb_sof" and st in (0, 3):
            ctx.usb_sof_ppm = val
        ctx.selftest[name] = (st, val)
        extra = ""
        if strict and name in V10_STRICT:
            status = "ok" if st == 0 else "fail"
            note = f"v1.0 must be ok; else {V10_STRICT[name]}"
        elif strict and name == "fdd_pins" and st in (0, 3):
            status, extra = _fdd_pins_verdict(val, ctx.power)
        elif st in allowed and status == "fail":
            status = "warn"                 # tolerated on the bare board (SD NAND absent etc.)
        elif st not in allowed and status != "fail":
            status = "warn"                 # unexpected skip/warn: look at the value
        if name == "sht40" and st == 0:
            t = (val >> 16) - (1 << 16) if val >> 31 else val >> 16
            extra = f"{t / 100:.2f} C, {(val & 0xFFFF) / 100:.2f} %RH"
        rep.add(f"selftest.{name}", status,
                f"{P2.SELFTEST_STATUS.get(st, st)} value {val}" + (f" ({extra})" if extra else "") + f"; {note}")


# ---- 7 clocks ---------------------------------------------------------------------------
def _clocks(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    pv = rep.guarded("clocks", dev.provenance)
    if pv is None:
        return
    ppm = ctx.usb_sof_ppm if ctx.usb_sof_ppm is not None else pv.get("clock_ppm")
    if pv.get("clock_ppm") is None and not pv.get("tcxo"):
        rep.add("clocks", "skip", "firmware does not report the clock source (provenance bytes 90-95)"
                + (f"; usb_sof {ppm} ppm" if ppm is not None else ""))
        return
    src = "TCXO X1 (HSE bypass)" if pv.get("tcxo") else "crystal oscillator"
    detail = f"{src}, usb_sof {ppm} ppm against the host's USB frames"
    data = dict(tcxo=pv.get("tcxo"), usb_sof_ppm=ppm)
    if ctx.v10 and not pv.get("tcxo"):
        rep.add("clocks", "fail", detail + "; v1.0 must run from the TCXO (X1, R132, C75)", data)
    elif ppm is None:
        rep.add("clocks", "warn", detail + "; no usb_sof measurement", data)
    elif abs(ppm) <= 100:
        rep.add("clocks", "ok", detail, data)
    elif abs(ppm) <= 500:
        rep.add("clocks", "warn", detail + "; above 100 ppm: host clock or HSE source (TCXO is +-2.5 ppm)", data)
    else:
        rep.add("clocks", "fail" if ctx.v10 else "warn", detail + "; beyond the USB tolerance", data)


# ---- 8 drive rails off / on (v1.0) --------------------------------------------------------
def _rails(dev: P2.Device2, rep: Report, ctx: Context, has_12v: bool, with_drive: bool) -> None:
    if not ctx.v10:
        rl = rep.guarded("rails", dev.rails)
        if rl:
            rep.add("rails", "skip" if not rl.fitted else ("fail" if rl.low else "ok"), str(rl))
        return
    first = rep.guarded("rails", dev.rails)
    if first is None:
        return
    if not first.fitted:
        rep.add("rails", "fail", "v1.0 but RAILS reports no dividers (R160-R163)")
        return
    p0 = ctx.power if ctx.power is not None else 0
    want_on = 0x03 if has_12v else 0x01
    try:
        dev.power(0)
        SLEEP(0.3)
        off = dev.rails()
        pins = _quiet(dev.fdd_pins)
        leak = [n for v, n in ((off.v5_mv, "5 V"), (off.v12_mv, "12 V")) if v > RAIL_OFF_MAX_MV]
        on_flags = off.flags & (P2.RAIL_5V_ON | P2.RAIL_12V_ON)
        status = "fail" if on_flags else ("warn" if leak else "ok")
        rep.add("rails.off", status,
                f"{off}" + ("; rails still flagged on" if on_flags else "")
                + (f"; {', '.join(leak)} above {RAIL_OFF_MAX_MV} mV with the rail off: back-feed (R5 AHCT244 Ioff?)" if leak else "")
                + (f"; FDD_PINS 0x{pins.pins:02x}" if pins else ""),
                dict(v5_mv=off.v5_mv, v12_mv=off.v12_mv, flags=off.flags))
        dev.power(want_on)
        SLEEP(0.5)
        on = dev.rails()
        bs = dev.power()
        pins = _quiet(dev.fdd_pins)
        problems, soft = [], []
        if on.low:
            problems.append(f"below limit: {', '.join(on.low)}")
        if not _in(on.v5_mv, V5_WINDOW):
            soft.append(f"FDD_5V outside {V5_WINDOW[0]}-{V5_WINDOW[1]} mV")
        if has_12v and not _in(on.v12_mv, V12_WINDOW):
            soft.append(f"FDD_12V outside {V12_WINDOW[0]}-{V12_WINDOW[1]} mV")
        if bs.power & 0x0C:
            problems.append(f"board_status eFuse fault bits 0x{bs.power & 0x0C:x}")
        if bs.flags & 0x06:
            problems.append("overcurrent trip")
        if pins:
            v, txt = _fdd_pins_verdict(pins.pins, want_on)
            if v == "fail":
                problems.append(txt)
        currents = f"5 V {bs.i5_ma} mA, 12 V {bs.i12_ma} mA"
        if not with_drive and max(bs.i5_ma, bs.i12_ma if has_12v else 0) > IDLE_CURRENT_MAX_MA:
            soft.append(f"current above {IDLE_CURRENT_MAX_MA} mA without a drive")
        status = "fail" if problems else ("warn" if soft else "ok")
        rep.add("rails.on", status,
                f"{on}; {currents}; FLT {'-' if not pins else ('5V ' if pins.pins & FDD_FLT5 else '') + ('12V' if pins.pins & FDD_FLT12 else '') or 'clear'}"
                + ("" if has_12v else " (--no-12v: 12 V rail not switched)")
                + (f"; {'; '.join(problems + soft)}" if problems or soft else ""),
                dict(v5_mv=on.v5_mv, v12_mv=on.v12_mv, flags=on.flags, i5_ma=bs.i5_ma, i12_ma=bs.i12_ma,
                     power=bs.power, fdd_pins=pins.pins if pins else None))
    except (P.DeviceError, P2.DeviceError, ValueError, TimeoutError, OSError) as e:
        rep.add("rails.switch", "skip", f"not available: {e}")
    finally:
        _quiet(lambda: dev.power(p0 & 0x03))


def _lan(dev: P2.Device2, rep: Report, ctx: Context) -> None:
    """NET 0x48 (LAN stage L5): W5500 detected, link and address.  No cable is a warning, not a
    failure: the bench may have none.  Firmware without the op skips."""
    if not ctx.v10:
        return
    try:
        n = dev.net()
    except P2.DeviceError as e:
        if e.status == P2.UNKNOWN_COMMAND:
            rep.add("lan", "skip", "firmware without the NET op (before LAN stage L5)")
            return
        rep.add("lan", "fail", f"NET refused: {e}")
        return
    except (ValueError, TimeoutError) as e:
        rep.add("lan", "fail", f"NET reply unusable: {e}")
        return
    data = dict(state=n.state, mode=n.mode, flags=n.flags, ip=n.ip, mac=n.mac, version=n.version)
    if n.state <= 1:
        rep.add("lan", "fail", f"{P2.NET_STATES.get(n.state, n.state)} (VERSIONR 0x{n.version:02x}, expected 0x04)", data)
    elif n.state == 2:
        rep.add("lan", "skip", f"W5500 off by LAN.CFG (lan=off), MAC {n.mac}", data)
    elif n.state < 5:
        rep.add("lan", "warn", f"W5500 0x{n.version:02x}, MAC {n.mac}: {P2.NET_STATES.get(n.state, n.state)}"
                + ("" if n.state == 4 else " (cable / switch?)"), data)
    else:
        speed = ("100" if n.flags & 2 else "10") + (" FD" if n.flags & 4 else " HD")
        rep.add("lan", "ok", f"W5500 0x{n.version:02x}, {speed}, {P2.NET_MODES.get(n.mode, n.mode)} {n.ip}/{n.mask}, "
                f"{n.hostname}, MAC {n.mac}", data)


# ---- 9 1.18 analysis-op discovery (pure queries) ------------------------------------------
def _queries(dev: P2.Device2, rep: Report) -> dict | None:
    masks = None
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
            if name == "flag_masks":
                masks = r
            try:
                rep.add(name, "ok", text(r))
            except (KeyError, AttributeError, TypeError) as e:
                rep.add(name, "warn", f"reply decoded with unexpected fields: {e}")
    return masks


def _index_sim(dev: P2.Device2, rep: Report, index_sim: bool) -> None:
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


# ---- 10 drive: one READ with the read context, optional write test -------------------------
def _read_info_text(ri) -> tuple[list[str], list[str], dict]:
    """(problems, notes, data) of a ReadInfo (4.24)."""
    problems, notes = [], []
    if ri.losses or ri.loss_total:
        problems.append(f"{ri.loss_total} loss window(s): " + "; ".join(
            f"{w.kind_text} at rev {w.revolution_start} tick {w.start_ticks}, {w.samples} transitions intact" for w in ri.losses))
    if not ri.position_valid:
        notes.append("head position not valid at the end")
    if ri.revs_clamped:
        notes.append(f"revs clamped {ri.revs_requested} -> {ri.revs_effective}")
    data = dict(revs_requested=ri.revs_requested, revs_effective=ri.revs_effective, revs_clamped=ri.revs_clamped,
                track_start=ri.track_start, side_start=ri.side_start, track_end=ri.track_end, side_end=ri.side_end,
                position_valid=ri.position_valid, head_moved=ri.head_moved,
                density=(ri.density_start, ri.density_end), drate=(ri.drate_start, ri.drate_end),
                lines_changed=ri.lines_changed, tcxo=ri.tcxo, clock_ppm=ri.clock_ppm, temp_c=ri.temp_c,
                rh_percent=ri.rh_percent, loss_total=ri.loss_total,
                losses=[asdict(w) for w in ri.losses])
    return problems, notes, data


def _drive_read(dev: P2.Device2, rep: Report, ctx: Context, masks: dict | None, revs: int):
    read_flags = masks["read_flags"] if masks else 0
    want_ctx = bool(read_flags & P2.READ_CONTEXT)
    want_prov = bool(read_flags & P2.READ_PROVENANCE)
    try:
        cap = dev.read_track(0, 0, revs, provenance=want_prov, read_context=want_ctx)
    except P2.DeviceError as e:
        ri = getattr(e, "read_info", None)
        problems, _notes, data = _read_info_text(ri) if ri else ([], [], None)
        rep.add("drive.read", "fail", f"READ track 0 side 0: {e}"
                + (f"; {'; '.join(problems)}" if problems else ""), data)
        return None
    except (ValueError, TimeoutError, OSError) as e:
        rep.add("drive.read", "fail", f"READ track 0 side 0: {e}")
        return None
    rpms = [round(r.rpm, 1) for r in cap.revolutions]
    counts = [len(r.samples) for r in cap.revolutions]
    problems, notes = [], []
    if len(cap.revolutions) != revs:
        problems.append(f"{len(cap.revolutions)} of {revs} revolutions")
    if not all(any(_in(x, w) for w in RPM_WINDOWS) for x in rpms):
        notes.append("rpm outside 290-310 / 350-370")
    if not counts or min(counts) < 1000:
        notes.append("fewer than 1000 transitions per revolution: no disk or unformatted disk")
    rep.add("drive.read", "fail" if problems else ("warn" if notes else "ok"),
            f"{len(cap.revolutions)} revolutions, rpm {rpms}, transitions {counts}"
            + (f"; {'; '.join(problems + notes)}" if problems or notes else ""),
            dict(rpm=rpms, transitions=counts, status=cap.status))
    if not want_ctx:
        rep.add("drive.read_context", "skip", "firmware read mask has no bit5 (op 38): no read context block")
    elif cap.read_info is None:
        rep.add("drive.read_context", "fail", "bit5 requested and accepted, but the END carries no read context block")
    else:
        ri = cap.read_info
        p, n, data = _read_info_text(ri)
        if ctx.v10 and not ri.tcxo:
            p.append("v1.0 read context says crystal oscillator, not TCXO")
        rep.add("drive.read_context", "fail" if p else ("warn" if n else "ok"),
                f"revs {ri.revs_requested}->{ri.revs_effective}, head {ri.track_start}/{ri.side_start} -> "
                f"{ri.track_end}/{ri.side_end} ({'valid' if ri.position_valid else 'NOT valid'}), "
                f"DENSITY {ri.density_start}->{ri.density_end}, DRATE {ri.drate_start}->{ri.drate_end}, "
                f"{'TCXO' if ri.tcxo else 'crystal'}, clock {ri.clock_ppm} ppm, "
                f"climate {ri.temp_c} C {ri.rh_percent} %RH, loss windows {ri.loss_total}"
                + (f"; {'; '.join(p + n)}" if p or n else ""), data)
    return cap


def _write_test(dev: P2.Device2, rep: Report, track: int) -> None:
    deltas = [WRITE_TEST_TICKS] * WRITE_TEST_COUNT
    bs = rep.guarded("write.lock", dev.power)
    if bs is None:
        return
    locked = bool(bs.flags & 1) or bs.session_locked
    if locked:
        try:
            dev.write_track(track, 0, deltas)
        except P2.DeviceError as e:
            refused = e.status in (P2.WRITE_PROTECTED, P2.SOFT_WRITE_LOCK)
            rep.add("write.lock", "ok" if refused else "fail",
                    f"lock set: WRITE refused with '{e}'" + ("" if refused else "; expected 'write protected'"))
        else:
            rep.add("write.lock", "fail", "lock set but the WRITE completed")
        rep.add("write.test", "skip", "front switch SCHREIBEN open (lock set): close it and run --write-test again")
        return
    rep.add("write.lock", "ok", "lock open (front switch SCHREIBEN closed): write test runs")
    try:
        dev.write_track(track, 0, deltas)
        cap = dev.read_track(track, 0, 2)
    except (P.DeviceError, P2.DeviceError, ValueError, TimeoutError, OSError) as e:
        rep.add("write.test", "fail", f"track {track} side 0: {e}")
        return
    counts = [len(r.samples) for r in cap.revolutions]
    medians = []
    for r in cap.revolutions:
        d = sorted(b - a for a, b in zip(r.samples, r.samples[1:]))
        medians.append(d[len(d) // 2] if d else 0)
    ok = bool(counts) and all(abs(c - WRITE_TEST_COUNT) <= WRITE_TEST_COUNT * 0.05 for c in counts) \
        and all(abs(m - WRITE_TEST_TICKS) <= WRITE_TEST_TICKS * 0.10 for m in medians)
    rep.add("write.test", "ok" if ok else "fail",
            f"track {track} side 0: wrote {WRITE_TEST_COUNT} x 4 us, read back {counts} transitions, "
            f"median interval {medians} ticks (expected {WRITE_TEST_TICKS} +-10 %, count +-5 %)",
            dict(track=track, written=WRITE_TEST_COUNT, counts=counts, median_ticks=medians))


def _drive(dev: P2.Device2, rep: Report, ctx: Context, masks: dict | None, drive: str, revs: int,
           write_test: bool, write_track: int, has_12v: bool) -> None:
    p0 = ctx.power if ctx.power is not None else 0
    try:
        dev.select(drive)
        dev.power(0x03 if has_12v else 0x01)
        SLEEP(0.5)
        bs = dev.power()
        lock = bool(bs.flags & 1)
        rep.add("drive.select", "ok", f"drive {drive}, rails on, 5 V {bs.i5_ma} mA, 12 V {bs.i12_ma} mA, "
                                      f"lock {'set' if lock else 'OPEN'}"
                + ("" if lock or write_test else "; open the front switch SCHREIBEN for the J6 pin 24 check"))
        if ctx.v10:
            rl = _quiet(dev.rails)
            pins = _quiet(dev.fdd_pins)
            problems = []
            if rl and rl.low:
                problems.append(f"rails below limit with the drive: {', '.join(rl.low)}")
            if pins:
                v, txt = _fdd_pins_verdict(pins.pins, 0x03 if has_12v else 0x01)
                if v == "fail":
                    problems.append(txt)
            rep.add("drive.rails", "fail" if problems else ("ok" if rl or pins else "skip"),
                    (str(rl) if rl else "no RAILS reply") + (f"; FDD_PINS {pins}" if pins else "")
                    + (f"; {'; '.join(problems)}" if problems else ""),
                    dict(v5_mv=rl.v5_mv, v12_mv=rl.v12_mv) if rl else None)
        dev.motor(True)
        dev.recalibrate()
        SLEEP(1.0)
        _drive_read(dev, rep, ctx, masks, revs)
        if write_test:
            _write_test(dev, rep, write_track)
    except (P.DeviceError, P2.DeviceError, ValueError, TimeoutError, OSError) as e:
        rep.add("drive", "fail", f"drive {drive}: {e}")
    finally:
        _quiet(lambda: dev.motor(False))
        _quiet(lambda: dev.select(0))
        _quiet(lambda: dev.power(p0 & 0x03))


def run(dev: P2.Device2, rep: Report, index_sim: bool = False, *, expect_rev: int | None = None,
        has_12v: bool = True, keep_rtc: bool = False, drive: str | None = None, read_revs: int = 3,
        write_test: bool = False, write_track: int = 2) -> int:
    """The whole sequence against an open Device2; returns the number of failed steps."""
    if write_test and not drive:
        raise ValueError("--write-test needs --drive (and a scratch disk)")
    ctx = Context()
    rep.meta.update(options=dict(index_sim=index_sim, expect_rev=expect_rev, has_12v=has_12v, keep_rtc=keep_rtc,
                                 drive=drive, read_revs=read_revs, write_test=write_test,
                                 write_track=write_track if write_test else None))
    _identify(dev, rep, ctx, expect_rev)
    _discovery(dev, rep, ctx)
    _diagnostics(dev, rep, ctx)
    _supplies(dev, rep, ctx)
    _rtc(dev, rep, ctx, keep_rtc)
    _sensor(dev, rep, ctx)
    _selftest(dev, rep, ctx)
    _clocks(dev, rep, ctx)
    _rails(dev, rep, ctx, has_12v, with_drive=bool(drive))
    _lan(dev, rep, ctx)
    masks = _queries(dev, rep)
    _index_sim(dev, rep, index_sim)
    if drive:
        _drive(dev, rep, ctx, masks, drive, read_revs, write_test, write_track, has_12v)
    return rep.failed


def main(argv: list[str] | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port", default=None, help="serial port (default: first UFI device)")
    ap.add_argument("--report", default=None, metavar="PATH", help="write the JSON report here")
    ap.add_argument("--expect-rev", type=int, default=None, metavar="N", help="fail unless BOARD_ID gives this revision (10 = v1.0)")
    ap.add_argument("--index-sim", action="store_true", help="also measure rpm from the internal index simulation")
    ap.add_argument("--no-12v", action="store_true", help="no 12 V input (USB or 5 V only): do not switch or judge FDD_12V")
    ap.add_argument("--keep-rtc", action="store_true", help="only read the RTC and compare with the PC (CR2032 hold-over)")
    ap.add_argument("--drive", choices=SHUGART_DRIVES, default=None, help="drive connected: one READ of track 0 side 0")
    ap.add_argument("--read-revs", type=int, default=3, metavar="N", help="revolutions of the drive READ (50 = 10 s for the J6 pin 24 check)")
    ap.add_argument("--write-test", action="store_true", help="WRITES a test track: scratch disk only, needs --drive")
    ap.add_argument("--write-track", type=int, default=2, metavar="T", help="track of the write test (side 0)")
    a = ap.parse_args(argv)
    if a.write_test and not a.drive:
        ap.error("--write-test needs --drive and a scratch disk")
    if not 1 <= a.read_revs <= P2.MAX_READ_REVS:
        ap.error(f"--read-revs 1..{P2.MAX_READ_REVS}")
    rep = Report()
    dev = P2.connect(a.port)
    if not isinstance(dev, P2.Device2):
        print("error: the board answers with the v1 protocol only; flash a 1.18 firmware first", file=sys.stderr)
        return 2
    failed = run(dev, rep, index_sim=a.index_sim, expect_rev=a.expect_rev, has_12v=not a.no_12v,
                 keep_rtc=a.keep_rtc, drive=a.drive, read_revs=a.read_revs, write_test=a.write_test,
                 write_track=a.write_track)
    if a.report:
        with open(a.report, "w", encoding="utf-8") as f:
            f.write(rep.to_json())
    c = rep.counts()
    print(f"bring-up: {'FAIL' if failed else 'PASS'} - {c['ok']} ok, {c['warn']} warn, {c['fail']} fail, {c['skip']} skip")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
