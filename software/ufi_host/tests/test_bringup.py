"""Bring-up sequence against the fake: bare v0.x board, the v1.0 bench sequence (FakeV10 models the
drive rails, eFuse FLT lines, write lock, running RTC, A10 self-test and TCXO), older firmware on a
v1.0 board, the drive READ with the read context block and the opt-in write test."""
from __future__ import annotations

import json
import struct
from datetime import datetime, timedelta

import pytest
from test_protocol2 import FakeV2, _read_info_block

from ufi_host import bringup
from ufi_host import protocol2 as P2

TRACK_REV = (55_000_000, [1100 * (i + 1) for i in range(bringup.WRITE_TEST_COUNT)])   # 4 us cells, 300 rpm


def _item(id_, st, val):
    return struct.pack("<BBI", id_, st, val & 0xFFFFFFFF)


# A10 firmware on an assembled v1.0 board, nothing selected, drive rails off (FLT asserted by ENUV low)
V10_SELFTEST = [(1, 0, 275000), (2, 0, -3), (3, 0, 8 << 20), (4, 0, 3800), (5, 2, 0), (6, 2, 0), (7, 0, 1650),
                (8, 0, 1500), (9, 0, 0), (10, 0, 0), (11, 0, 1), (12, 0, 0), (13, 0, 21000),
                (15, 0, 20261027), (16, 0, (2345 << 16) | 4567), (17, 2, 0), (18, 2, 0), (19, 2, 0),
                (20, 0, 0), (21, 3, 0x70), (14, 0, 1234)]


class FakeV10(FakeV2):
    """FakeV2 as a v1.0 board: POWER switches the rails that RAILS and FDD_PINS report (FLT5/FLT12 read
    asserted while a rail is off, as with ENUV low on the TPS25921A), `efuse_fault` latches FLT and the
    board_status bits with the rail on, `lock` is the front switch (WRITE refused with status 13),
    FEATURES lists ids 11-14, the provenance block reports the TCXO and a usb_sof value."""

    def __init__(self, **kw):
        super().__init__(board_rev=10, **kw)
        self.power_state = 0
        self.lock = True
        self.efuse_fault = 0            # bit0 5 V, bit1 12 V
        self.leak_mv = 0                # FDD_5V with the rail off (back-feed model)
        self.v5_on, self.v12_on = 5010, 11950
        self.tcxo = True
        self.selftest_items = list(V10_SELFTEST)
        self.revs = [TRACK_REV] * 3
        self.read_info_block = _read_info_block(flags=0x08 | 0x10 | 0x20)
        self.history = []               # (cmd, payload) of the board/drive requests, in order

    def _provenance_block(self) -> bytes:
        b = bytearray(super()._provenance_block())
        b[2] = 10
        b[90] = (1 if self.tcxo else 0) | 2
        struct.pack_into("<h", b, 91, -3)
        b[93] = 1
        return bytes(b)

    def _handle(self, f: P2.Frame):
        if f.type != P2.T_REQUEST:
            return super()._handle(f)
        cmd, a = f.cmd, f.payload
        self.history.append((cmd, bytes(a)))
        r = lambda status=0, payload=b"": self._frame(P2.T_RESPONSE, cmd, f.seq, status, payload)
        on5, on12 = self.power_state & 1, self.power_state & 2
        if cmd == P2.POWER:
            self.requests.append(f)
            if a:
                self.power_state = a[0] & 3
                on5, on12 = self.power_state & 1, self.power_state & 2
            fault = (self.efuse_fault & self.power_state) << 2
            r(0, P2.BOARD.pack(self.power_state | fault, 8 | int(self.lock), 12 if on5 else 0, 3 if on12 else 0, 1650))
        elif cmd == P2.RAILS:
            self.requests.append(f)
            v5 = self.v5_on if on5 else self.leak_mv
            v12 = self.v12_on if on12 else 0
            flags = 0x10 | (4 if on5 else 0) | (8 if on12 else 0)
            flags |= (1 if on5 and v5 < 4500 else 0) | (2 if on12 and v12 < 10800 else 0)
            r(0, P2.RAILS_FMT.pack(v5, v12, flags))
        elif cmd == P2.FDD_PINS and not a:
            self.requests.append(f)
            pins = (0x10 if self.lock else 0) | (0 if on5 else 0x20) | (0 if on12 else 0x40)
            pins |= (0x20 if self.efuse_fault & 1 and on5 else 0) | (0x40 if self.efuse_fault & 2 and on12 else 0)
            r(0, P2.FDD_PINS_FMT.pack(pins, 0))
        elif cmd == P2.SELFTEST:
            self.requests.append(f)
            r(0, bytes([len(self.selftest_items)]) + b"".join(_item(*i) for i in self.selftest_items))
        elif cmd == P2.FEATURES:
            self.requests.append(f)
            ids = (1, 7, 11, 12, 13, 14)
            r(0, bytes([1, len(ids)]) + b"".join(struct.pack("<HBBI", i, 0x07, 0, 0) for i in ids))
        elif cmd == P2.SD_INFO:
            self.requests.append(f)
            r(0, P2.SD.pack(1, 0, 2, 4, 3800))           # U15 4 GB SD NAND, 4-bit bus
        elif cmd == P2.WRITE and self.lock:
            self.requests.append(f)
            r(P2.WRITE_PROTECTED)
        else:
            super()._handle(f)


class OldFirmwareV10(FakeV10):
    """A v1.0 board with a firmware before stage A10: no ops 0x43-0x47, no FEATURES, the 1.18 self-test
    without items 15-21, provenance bytes 90-95 still reserved 0, READ mask without bit5."""

    def __init__(self, **kw):
        super().__init__(**kw)
        self.selftest_items = [i for i in V10_SELFTEST if i[0] <= 14]
        self.selftest_items[10] = (11, 2, 0)         # index_loop: old firmware drives J9 pin 6 only

    def _provenance_block(self) -> bytes:
        return FakeV2._provenance_block(self)

    def _handle(self, f: P2.Frame):
        if f.type == P2.T_REQUEST and f.cmd in (P2.RTC, P2.SENSOR, P2.RAILS, P2.SYNC_SENSOR, P2.FDD_PINS, P2.FEATURES):
            self.requests.append(f)
            self._frame(P2.T_RESPONSE, f.cmd, f.seq, P2.UNKNOWN_COMMAND)
            return
        if f.type == P2.T_REQUEST and f.cmd == P2.ANALYSIS_JOB and f.payload[:1] == b"\x26":   # op 38: mask 0x1F
            self.requests.append(f)
            self._frame(P2.T_RESPONSE, f.cmd, f.seq, 0, bytes([1, 0x1F, 0xFF, 0x1F, 0xFF, 7])
                        + struct.pack("<HHHH", 0x3F, 0x1F, 0x07, 0x18) + bytes([0xB1, 0]))
            return
        super()._handle(f)


@pytest.fixture
def clock(monkeypatch):
    """Deterministic PC clock; SLEEP advances it and every fake RTC registered in clock.rtcs."""
    state = dict(now=datetime(2026, 10, 27, 10, 0, 0), rtcs=[])

    def sleep(s):
        state["now"] += timedelta(seconds=s)
        for fake in state["rtcs"]:
            fake.rtc_time = tuple((datetime(*fake.rtc_time) + timedelta(seconds=round(s))).timetuple()[:6])

    monkeypatch.setattr(bringup, "SLEEP", sleep)
    monkeypatch.setattr(bringup, "NOW", lambda: state["now"])
    return state


def _steps(rep):
    return {s.name: s for s in rep.steps}


def test_bringup_sequence_against_fake(capsys, clock):
    fake = FakeV2()
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep) == 0
    names = [s.name for s in rep.steps]
    for expected in ("ping", "info", "describe", "board_status", "flag_masks", "write_limits", "provenance",
                     "track_survey", "raw_verify_options", "index_sim_state"):
        assert expected in names, expected
    assert any(n.startswith("selftest.") for n in names)
    assert not [s for s in rep.steps if s.status == "fail"]
    st = _steps(rep)                                       # v0.7: the v1.0-only steps skip, nothing fails
    assert st["rtc"].status == "skip" and st["sensor"].status == "skip" and st["rails"].status == "skip"
    assert st["clocks"].status == "skip" and "rails.on" not in st and "lan" not in st
    assert not [q for q in fake.requests if q.cmd in (P2.SELECT, P2.MOTOR, P2.READ, P2.WRITE)]
    out = capsys.readouterr().out
    assert "OK    ping" in out
    data = json.loads(rep.to_json())
    assert data["counts"]["fail"] == 0 and len(data["steps"]) == len(rep.steps) and data["verdict"] == "PASS"


def test_expect_rev_fails_on_wrong_board(clock):
    rep = bringup.Report()
    assert bringup.run(P2.Device2(FakeV2()), rep, expect_rev=10) == 1
    assert _steps(rep)["board_rev"].status == "fail"


def test_v10_sequence_passes(clock):
    fake = FakeV10()
    clock["rtcs"].append(fake)
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, expect_rev=10) == 0, [(s.name, s.detail) for s in rep.steps if s.status == "fail"]
    st = _steps(rep)
    for name in ("board_rev", "features.v10", "sd_info", "rtc", "rtc.set", "rtc.runs", "sensor", "clocks",
                 "rails.off", "rails.on", "selftest.rtc", "selftest.sht40", "selftest.wgate_lock",
                 "selftest.index_loop", "selftest.fdd_pins", "selftest.sd", "selftest.log"):
        assert st[name].status == "ok", (name, st[name].detail)
    assert fake.rtc_time[:3] == (2026, 10, 27)                       # set to the PC clock
    assert "23.45 C" in st["selftest.sht40"].detail and "TCXO" in st["clocks"].detail
    assert "FLT5, FLT12 with the rail off is expected" in st["selftest.fdd_pins"].detail
    assert st["rails.on"].data["v5_mv"] == 5010 and st["rails.on"].data["fdd_pins"] == 0x10
    assert st["lan"].status == "skip"
    # drive power off, on, then back to the start state (off); no drive request without --drive
    masks = [a[0] for c, a in fake.history if c == P2.POWER and a]
    assert masks == [0, 3, 0] and fake.power_state == 0
    assert not [c for c, _ in fake.history if c in (P2.MOTOR, P2.READ, P2.WRITE)]
    assert [a for c, a in fake.history if c == P2.SELECT] == [b"\x00"]     # nothing selected for wgate_lock


def test_v10_keep_rtc_and_no_12v(clock):
    fake = FakeV10()
    fake.rtc_time = (2026, 10, 27, 9, 59, 58)
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, keep_rtc=True, has_12v=False) == 0
    st = _steps(rep)
    assert st["rtc.hold"].status == "ok" and st["rtc.hold"].data["drift_s"] == -2 and "rtc.set" not in st
    assert not [a for c, a in fake.history if c == P2.RTC and a]               # calendar untouched
    assert [a[0] for c, a in fake.history if c == P2.POWER and a] == [0, 1, 0]
    assert "--no-12v" in st["rails.on"].detail


@pytest.mark.parametrize("fault, step, text", [
    ("efuse", "rails.on", "eFuse fault"),
    ("lowrail", "rails.on", "below limit: 5 V"),
    ("leak", "rails.off", "back-feed"),
    ("wgate", "selftest.wgate_lock", "U31"),
    ("wgate_idle", "selftest.fdd_pins", "WGATE bus active at idle"),
    ("crystal", "clocks", "TCXO"),
    ("lse", "rtc", "LSE"),
    ("sht40", "sensor", "U25"),
    ("index", "selftest.index_loop", "hardwired"),
])
def test_v10_faults(clock, fault, step, text):
    fake = FakeV10()
    clock["rtcs"].append(fake)
    if fault == "efuse":
        fake.efuse_fault = 1
    elif fault == "lowrail":
        fake.v5_on = 4300
    elif fault == "leak":
        fake.leak_mv = 2400
    elif fault == "wgate":
        fake.selftest_items[18] = (20, 1, 1)
    elif fault == "wgate_idle":
        fake.selftest_items[19] = (21, 3, 0x78)
    elif fault == "crystal":
        fake.tcxo = False
    elif fault == "lse":
        fake.board_rev = 10
        orig = fake._handle

        def no_rtc(f):
            if f.type == P2.T_REQUEST and f.cmd == P2.RTC:
                fake._frame(P2.T_RESPONSE, f.cmd, f.seq, 0, P2.RTC_FMT.pack(0, 0, 0, 0, 0, 0, 0))
                return
            orig(f)
        fake._handle = no_rtc
    elif fault == "sht40":
        fake.sensor = (1, 0, 0)
    elif fault == "index":
        fake.selftest_items[10] = (11, 2, 0)
    rep = bringup.Report()
    bringup.run(P2.Device2(fake), rep)
    st = _steps(rep)
    want = "warn" if fault == "leak" else "fail"
    assert st[step].status == want and text in st[step].detail, (st[step].status, st[step].detail)
    assert fake.power_state == 0                                         # rails restored after the check


def test_old_firmware_on_v10_skips(clock):
    fake = OldFirmwareV10()
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, drive="a") == 0, [(s.name, s.detail) for s in rep.steps if s.status == "fail"]
    st = _steps(rep)
    assert st["rtc"].status == "skip" and st["sensor"].status == "skip" and st["rails"].status == "skip"
    assert st["clocks"].status == "skip" and st["features"].status == "skip"
    assert st["selftest.index_loop"].status == "ok" or st["selftest.index_loop"].status == "skip"
    assert st["drive.read"].status == "ok" and st["drive.read_context"].status == "skip"
    read = [a for c, a in fake.history if c == P2.READ][-1]
    assert not read[3] & P2.READ_CONTEXT


def test_drive_read_with_context(clock):
    fake = FakeV10()
    clock["rtcs"].append(fake)
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, drive="a", read_revs=3) == 0
    st = _steps(rep)
    assert st["drive.select"].status == "ok" and "lock set" in st["drive.select"].detail
    assert st["drive.rails"].status == "ok"
    assert st["drive.read"].status == "ok" and st["drive.read"].data["transitions"] == [bringup.WRITE_TEST_COUNT] * 3
    assert st["drive.read"].data["rpm"] == [300.0] * 3
    ctxs = st["drive.read_context"]
    assert ctxs.status == "ok" and ctxs.data["tcxo"] and ctxs.data["clock_ppm"] == -12 and ctxs.data["loss_total"] == 0
    assert ctxs.data["density"] == (1, 1) and ctxs.data["track_end"] == 41
    read = [a for c, a in fake.history if c == P2.READ][-1]
    assert read[3] & P2.READ_CONTEXT and read[3] & P2.READ_PROVENANCE and read[:3] == bytes([0, 0, 3])
    assert not [c for c, _ in fake.history if c == P2.WRITE]                  # never writes without --write-test
    sel = [a for c, a in fake.history if c == P2.SELECT]
    assert sel[-2:] == [bytes([1]), b"\x00"] and fake.power_state == 0
    assert [a for c, a in fake.history if c == P2.MOTOR][-1] == b"\x00"
    # a loss window in the read context fails the step
    fake.read_info_block = _read_info_block(flags=0x38, losses=[(0x01, 1, 1, 0, 60_000, 61_000, 500)])
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, drive="a") == 1
    assert _steps(rep)["drive.read_context"].status == "fail" and "RDATA overcapture" in _steps(rep)["drive.read_context"].detail


def test_write_test_lock_and_scratch_write(clock):
    fake = FakeV10()
    clock["rtcs"].append(fake)
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, drive="a", write_test=True) == 0
    st = _steps(rep)
    assert st["write.lock"].status == "ok" and "refused" in st["write.lock"].detail
    assert st["write.test"].status == "skip"
    fake.lock = False                                                    # front switch SCHREIBEN closed
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep, drive="a", write_test=True, write_track=5) == 0
    st = _steps(rep)
    assert st["write.test"].status == "ok" and st["write.test"].data["counts"] == [bringup.WRITE_TEST_COUNT] * 2
    assert st["write.test"].data["median_ticks"] == [bringup.WRITE_TEST_TICKS] * 2
    assert [a[0] for c, a in fake.history if c == P2.WRITE] == [2, 5]
    with pytest.raises(ValueError):
        bringup.run(P2.Device2(fake), bringup.Report(), write_test=True)


def test_bringup_main_writes_report(monkeypatch, tmp_path, capsys, clock):
    fake = FakeV2()
    monkeypatch.setattr(P2, "connect", lambda port=None, timeout=5.0: P2.Device2(fake))
    report = tmp_path / "bringup.json"
    assert bringup.main(["--port", "COM9", "--report", str(report), "--index-sim"]) == 0
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["counts"]["fail"] == 0 and any(s["name"] == "rpm_from_sim" for s in data["steps"])
    assert "bring-up: PASS" in capsys.readouterr().out
    v10 = FakeV10()
    clock["rtcs"].append(v10)
    monkeypatch.setattr(P2, "connect", lambda port=None, timeout=5.0: P2.Device2(v10))
    assert bringup.main(["--expect-rev", "10", "--drive", "a", "--report", str(report)]) == 0
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["verdict"] == "PASS" and data["meta"]["board_rev"] == 10 and data["meta"]["options"]["drive"] == "a"
    with pytest.raises(SystemExit):
        bringup.main(["--write-test"])                                   # needs --drive
    monkeypatch.setattr(P2, "connect", lambda port=None, timeout=5.0: object())
    assert bringup.main([]) == 2
