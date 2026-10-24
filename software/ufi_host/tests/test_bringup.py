"""Bare-board bring-up sequence against the fake: no failure, every group reported, JSON report written."""
from __future__ import annotations

import json

from test_protocol2 import FakeV2

from ufi_host import bringup
from ufi_host import protocol2 as P2


def test_bringup_sequence_against_fake(capsys):
    fake = FakeV2()
    rep = bringup.Report()
    assert bringup.run(P2.Device2(fake), rep) == 0
    names = [s.name for s in rep.steps]
    for expected in ("ping", "info", "describe", "board_status", "flag_masks", "write_limits", "provenance",
                     "track_survey", "raw_verify_options", "index_sim_state"):
        assert expected in names, expected
    assert any(n.startswith("selftest.") for n in names)
    assert not [s for s in rep.steps if s.status == "fail"]
    out = capsys.readouterr().out
    assert "OK    ping" in out
    data = json.loads(rep.to_json())
    assert data["counts"]["fail"] == 0 and len(data["steps"]) == len(rep.steps)


def test_bringup_main_writes_report(monkeypatch, tmp_path, capsys):
    fake = FakeV2()
    monkeypatch.setattr(P2, "connect", lambda port=None, timeout=5.0: P2.Device2(fake))
    report = tmp_path / "bringup.json"
    assert bringup.main(["--port", "COM9", "--report", str(report), "--index-sim"]) == 0
    data = json.loads(report.read_text(encoding="utf-8"))
    assert data["counts"]["fail"] == 0 and any(s["name"] == "rpm_from_sim" for s in data["steps"])
    assert "bring-up:" in capsys.readouterr().out
    monkeypatch.setattr(P2, "connect", lambda port=None, timeout=5.0: object())
    assert bringup.main([]) == 2
