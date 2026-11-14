"""ufi_host.weak: instability of a READ capture from its own revolutions (USB_Protokoll 4.13a).

Equality with the firmware's op 42/44 code is shown in firmware/tests/test_weak_reference.py on
identical data; here: the definition on hand-computed values, the binding to a capture and the CLI."""
from __future__ import annotations

import copy
from types import SimpleNamespace

import pytest
from test_protocol2 import REVS, GuardedDevice, _read_info_block

from ufi_host import cli, weak
from ufi_host import protocol as P
from ufi_host import protocol2 as P2


def rev(index_ticks, samples):
    return SimpleNamespace(index_ticks=index_ticks, samples=list(samples))


def test_window_edges_and_integer_division():
    # T = 6400: coarse window w = [100w, 100w+100), fine window w = [6400w/1024, 6400(w+1)/1024)
    a = rev(6400, [0, 99, 100, 199, 6399])
    b = rev(6400, [0, 100, 6399, 6400, 7000])       # at/after the index: not counted
    co = weak.weak_windows([a, b])
    assert co["revs"] == 2 and co["counts"][0] == 1 and co["counts"][1] == 1 and co["counts"][63] == 0
    assert sum(co["counts"]) == 2 and co["mask"] == 0 and co["weak_count"] == 0
    assert weak.window_ticks(6400, 0) == (0, 6) and weak.window_ticks(6400, 1) == (6, 12)
    assert weak.window_ticks(6400, 1023) == (6393, 6400) and weak.window_ticks(1000, 1023) == (999, 1000)
    assert weak.window_ticks(6400, 63, 64) == (6300, 6400)
    with pytest.raises(ValueError):
        weak.window_ticks(6400, 1024)
    fi = weak.weak_positions([a, b], 1)
    # 99 and 199 exist only in revolution a: fine windows 99*1024//6400 = 15 and 199*1024//6400 = 31
    assert fi["entries"] == [{"window": 15, "diff": 1, "min": 0}, {"window": 31, "diff": 1, "min": 0}]
    assert fi["revs"] == 2 and fi["windows"] == 1024 and fi["threshold"] == 1 and not fi["truncated"]


def test_each_revolution_is_scaled_by_its_own_length():
    """The same recorded positions at two speeds land in the same windows: no difference."""
    base = list(range(50, 64000, 100))
    fast = [t * 63000 // 64000 for t in base]
    co = weak.weak_windows([rev(64000, base), rev(63000, fast)])
    assert max(co["counts"]) <= 1 and co["weak_count"] == 0
    # read against the wrong length they drift apart towards the end of the track
    wrong = weak.weak_windows([rev(64000, base), rev(64000, fast)])
    assert wrong["counts"][63] >= 3 and wrong["mask"] >> 63 & 1


def test_threshold_mask_caps_and_fewer_than_two_revolutions():
    dense = rev(6400, range(6400))                  # 100 per coarse window, 6 or 7 per fine window
    co = weak.weak_windows([dense, rev(6400, [])])
    assert co["counts"] == [100] * 64 and co["mask"] == (1 << 64) - 1 and co["weak_count"] == 64
    huge = weak.weak_windows([rev(64000, range(64000)), rev(64000, [])])
    assert huge["counts"] == [255] * 64            # 1000 per window, capped after the subtraction
    lo, hi = weak.fine_min_max([rev(1_024_000, range(0, 1_024_000, 2)), rev(1_024_000, range(0, 1_024_000, 4))])
    assert set(hi) == {255} and set(lo) == {250}   # fine counts cap at 255 before the subtraction: 500 -> 255
    one = [rev(6400, [1, 2, 3])]
    assert weak.weak_windows(one) == {"revs": 0, "counts": [0] * 64, "mask": 0, "weak_count": 0}
    assert weak.fine_min_max(one) is None and weak.weak_positions(one)["entries"] == []
    assert weak.weak_positions([], 5) == {"revs": 0, "windows": 1024, "threshold": 5, "truncated": False, "entries": []}
    for bad in (0, 256, -1):
        with pytest.raises(ValueError):
            weak.weak_positions(one, bad)
    with pytest.raises(ValueError):
        weak.weak_windows([rev(1 << 32, []), rev(5, [])])
    with pytest.raises(ValueError):
        weak.weak_windows([rev(5, [-1]), rev(5, [])])


def test_report_limit_and_full_list():
    many = [rev(1_024_000, range(0, 1_024_000, 1000)), rev(1_024_000, [])]
    dev = weak.weak_positions(many, 1)              # one firmware report: 64 entries and the flag
    assert len(dev["entries"]) == 64 and dev["truncated"] and dev["entries"][-1]["window"] == 63
    full = weak.weak_positions(many, 1, limit=None)
    assert len(full["entries"]) == 1024 and not full["truncated"] and full["entries"][:64] == dev["entries"]


def test_capture_binding_and_cli(monkeypatch, capsys):
    """The report names the capture it was computed from, leaves the capture untouched and says when
    the numbers cannot be taken at face value."""
    revs = [(55_000_000, list(range(1000, 54_000_000, 20_000))),
            (55_000_000, [t for t in range(1000, 54_000_000, 20_000) if not 20_000_000 <= t < 21_000_000]),
            (55_000_000, list(range(1000, 54_000_000, 20_000)))]
    fake = GuardedDevice(revolutions=revs)
    dev = P2.Device2(fake)
    cap = dev.read_track(0, 0, 3)
    before = copy.deepcopy(cap)
    r = weak.capture_instability(cap)
    assert cap == before                                            # flux and metadata unchanged
    assert r["capture_id"] == cap.generations["capture_id"] and r["capture_id"] is not None
    assert r["track"] == 0 and r["side"] == 0 and r["index_ticks"] == [55_000_000] * 3
    assert r["complete"] is True and r["index_physical"] is None    # read context not requested
    assert r["coarse"]["revs"] == 3 and r["coarse"]["weak_count"] >= 1
    hit = [w for w in range(64) if r["coarse"]["mask"] >> w & 1]
    assert hit and all(20_000_000 * 64 // 55_000_000 <= w <= 21_000_000 * 64 // 55_000_000 for w in hit)
    fine = [e["window"] for e in r["fine"]["entries"]]
    assert fine and all(20_000_000 * 1024 // 55_000_000 <= w <= 21_000_000 * 1024 // 55_000_000 for w in fine)
    assert not r["fine"]["truncated"]                               # the capture report lists every window
    # equal to the functions on the bare revolutions
    assert r["coarse"] == weak.weak_windows(cap.revolutions) and r["fine"] == weak.weak_positions(cap.revolutions, 2, None)

    # incomplete capture and a read without a physical index are flagged, the numbers stay as computed
    lossy = copy.deepcopy(cap)
    lossy.status = 10
    assert weak.capture_instability(lossy)["complete"] is False
    sliced = copy.deepcopy(cap)
    sliced.read_info = P2.decode_read_info(_read_info_block(origin=4))
    rs = weak.capture_instability(sliced)
    assert rs["index_physical"] is False and rs["complete"] is True and rs["coarse"] == r["coarse"]
    lost = copy.deepcopy(cap)
    lost.read_info = P2.decode_read_info(_read_info_block(origin=1, losses=[(0x01, 1, 1, 0, 60_000, 61_000, 500)]))
    assert weak.capture_instability(lost)["complete"] is False and weak.capture_instability(lost)["index_physical"] is True
    assert any("no physical index" in line for line in cli._weak_lines(sliced))
    assert any("capture incomplete" in line for line in cli._weak_lines(lossy))

    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake)
    requests = len(fake.requests)
    assert cli.main(["read", "0", "0", "-r", "3", "--weak"]) == 0
    out = capsys.readouterr().out
    assert "instability (capture id" in out and "same data as the stream" in out and "1/64 windows at or above 3: w23=" in out
    assert "no proof of copy protection" in out
    sent = [q.cmd for q in fake.requests[requests:]]
    assert sent.count(P2.READ) == 1 and 0x0F not in sent and 0x25 not in sent       # one READ, no ANALYZE, no op 42/44

    # a capture with one revolution says so instead of printing zeros
    fake2 = GuardedDevice(revolutions=REVS[:1])
    monkeypatch.setattr(P2, "open_serial", lambda port=None: fake2)
    assert cli.main(["read", "0", "0", "-r", "1", "--weak"]) == 0
    assert "needs at least 2 revolutions, this capture has 1" in capsys.readouterr().out
    assert isinstance(cap, P.Capture)
