"""Move tracks/vias/zones from flattened net names (e.g. BUCK_FB) to the schematic net the pads
now carry (e.g. /Power/BUCK_FB) when the mapping is unique; then drop empty nets.

usage: renet_copper.py <board>
"""
import sys
from collections import Counter

import pcbnew

path = sys.argv[1]
board = pcbnew.LoadBoard(path)
pad_nets = {p.GetNetname() for fp in board.GetFootprints() for p in fp.Pads() if p.GetNetCode() > 0}
by_leaf = {}
for n in pad_nets:
    by_leaf.setdefault(n.rsplit("/", 1)[-1], []).append(n)

moved, ambiguous, orphan = Counter(), Counter(), Counter()


def target(name):
    if name in pad_nets or not name:
        return None
    cands = by_leaf.get(name, [])
    if len(cands) == 1:
        return cands[0]
    (ambiguous if cands else orphan)[name] += 1
    return None


items = list(board.GetTracks()) + list(board.Zones())
for it in items:
    t = target(it.GetNetname())
    if t:
        it.SetNet(board.FindNet(t))
        moved[(it.GetNetname(), it.GetClass())] += 1
print("re-netted items", sum(moved.values()))
for (n, cls), c in sorted(moved.items()):
    print("   %-26s %-9s %d" % (n, cls, c))
print("ambiguous:", dict(ambiguous))
print("copper on nets without pads:", dict(orphan))
board.Save(path)
