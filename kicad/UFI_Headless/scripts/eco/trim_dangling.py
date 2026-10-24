"""Remove dangling unlocked track ends and vias - ONE round per process (pcbnew's Tracks()
breaks after Remove + a second LoadBoard in the same interpreter); loop it in the shell.

A track end dangles when nothing (pad, track, via) touches it on its layer.  A via dangles
when it has no pad and tracks on fewer than two layers (plane nets GND/+3V3 excepted).
usage: trim_dangling.py <board> [apply]      prints "removed N" (N = 0 when nothing dangles)
"""
import sys
from collections import Counter

import pcbnew

PLANE = {"GND", "+3V3"}
path = sys.argv[1]
apply = len(sys.argv) > 2 and sys.argv[2] == "apply"
board = pcbnew.LoadBoard(path)
board.BuildConnectivity()
conn = board.GetConnectivity()
doomed = []
for t in board.GetTracks():
    if t.IsLocked():
        continue
    if t.GetClass() == "PCB_VIA":
        if t.GetNetname() in PLANE:
            continue
        pads = conn.GetConnectedPads(t)
        layers = {tr.GetLayer() for tr in conn.GetConnectedTracks(t)}
        if len(pads) == 0 and len(layers) < 2:
            doomed.append(t)
    else:
        try:
            dang = conn.TestTrackEndpointDangling(t, False)
        except TypeError:
            dang = conn.TestTrackEndpointDangling(t)
        if dang:
            doomed.append(t)
nets = Counter(t.GetNetname() for t in doomed)
print("removed", len(doomed), " ".join("%s=%d" % kv for kv in nets.most_common(12)))
if apply and doomed:
    for t in doomed:
        board.Remove(t)
    board.Save(path)
