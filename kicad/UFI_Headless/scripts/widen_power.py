"""Widen routed supply tracks where clearance allows.

Freerouting routes supply nets at 0.3 mm so they can reach fine-pitch pads.
  widen  <board> <list>        : set POWER_WIDTH on every segment of a supply net
                                 that does not end inside a pad; store changed UUIDs
  revert <board> <list> <json> : restore the routed width of widened segments that
                                 appear in a kicad-cli DRC JSON report
"""
import json
import sys

import pcbnew

POWER_NETS = {"+12V", "/Power/+12V_IN", "/Power/+5V_DRV", "/Power/BUCK_SW", "+5V", "VBUS", "FDD_5V", "FDD_12V",
              "/Power/VBUS_F", "GND", "+3V3"}
WIDTH = {"GND": 0.5, "+3V3": 0.5}   # other supply nets: POWER_WIDTH
POWER_WIDTH = 0.8
MM = pcbnew.FromMM

mode, path, listfile = sys.argv[1:4]
board = pcbnew.LoadBoard(path)
tracks = {t.m_Uuid.AsString(): t for t in board.GetTracks() if t.GetClass() == "PCB_TRACK"}

if mode == "widen":
    pads = list(board.GetPads())
    changed = {}
    for uid, t in tracks.items():
        if t.GetNetname() not in POWER_NETS:
            continue
        layer = t.GetLayer()
        ends = (t.GetStart(), t.GetEnd())
        if any(p.IsOnLayer(layer) and p.HitTest(e) for p in pads for e in ends):
            continue  # keep the neck-down into the pad
        changed[uid] = t.GetWidth()
        t.SetWidth(MM(WIDTH.get(t.GetNetname(), POWER_WIDTH)))
    json.dump(changed, open(listfile, "w"))
    print("widened", len(changed))
else:
    changed = json.load(open(listfile))
    report = json.load(open(sys.argv[4], encoding="utf8"))
    bad = set()
    for v in report.get("violations", []):
        if v.get("type") in ("clearance", "shorting_items", "tracks_crossing", "hole_clearance",
                             "copper_edge_clearance", "solder_mask_bridge"):
            bad.update(i.get("uuid") for i in v.get("items", []))
    reverted = 0
    for uid in bad & set(changed):
        tracks[uid].SetWidth(changed[uid])
        reverted += 1
    print("reverted", reverted)
board.Save(path)
