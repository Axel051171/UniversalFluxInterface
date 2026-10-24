"""Delete copper clusters that no longer join two pads (stubs left by moved or removed parts).

A cluster is a set of tracks/vias linked end to end.  Kept: clusters with >= 2 pads, locked
items, and on the plane nets (GND, +3V3) clusters with a via and >= 1 pad or lone vias.
usage: cleanup_copper.py <board> [apply]
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
tracks = {t.m_Uuid.AsString(): t for t in board.GetTracks()}
seen = set()
removed, kept_locked = Counter(), Counter()
doomed = []
for uid, t in tracks.items():
    if uid in seen:
        continue
    cluster, stack = [], [t]
    seen.add(uid)
    while stack:
        it = stack.pop()
        cluster.append(it)
        for nb in conn.GetConnectedTracks(it):
            k = nb.m_Uuid.AsString()
            if k not in seen:
                seen.add(k)
                stack.append(nb)
    pads = set()
    for it in cluster:
        for p in conn.GetConnectedPads(it):
            pads.add(p.m_Uuid.AsString())
    net = t.GetNetname()
    has_via = any(it.GetClass() == "PCB_VIA" for it in cluster)
    keep = len(pads) >= 2
    if net in PLANE and ((has_via and len(pads) >= 1) or (has_via and len(cluster) == 1)):
        keep = True
    if keep:
        continue
    for it in cluster:
        if it.IsLocked():
            kept_locked[net] += 1
        else:
            doomed.append(it)
            removed[net] += 1
print("items to remove", len(doomed), "of", len(tracks))
for net, n in removed.most_common():
    print("   %-28s %d" % (net, n))
if kept_locked:
    print("locked items in stub clusters (kept):", dict(kept_locked))
if apply:
    for it in doomed:
        board.Remove(it)
    board.Save(path)
    print("saved, remaining", len(board.GetTracks()))
