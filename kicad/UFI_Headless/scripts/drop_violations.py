"""Delete unlocked tracks/vias involved in copper DRC violations (Freerouting leftovers).

usage: python drop_violations.py <board> <drc.json>
Prints the number of removed items; the freed connections are re-routed afterwards.
"""
import json
import sys

import pcbnew

COPPER = {"clearance", "shorting_items", "tracks_crossing", "hole_clearance", "via_dangling",
          "track_dangling", "copper_edge_clearance"}

board = pcbnew.LoadBoard(sys.argv[1])
report = json.load(open(sys.argv[2], encoding="utf8"))
bad = set()
for v in report.get("violations", []):
    if v.get("type") in COPPER:
        bad.update(i.get("uuid") for i in v.get("items", []))
removed = 0
for t in list(board.GetTracks()):
    if t.m_Uuid.AsString() in bad and not t.IsLocked():
        board.Remove(t)
        removed += 1
board.Save(sys.argv[1])
print("removed", removed)
