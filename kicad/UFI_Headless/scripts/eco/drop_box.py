"""Remove unlocked tracks/vias of NET whose endpoints all lie inside a box.
usage: drop_box.py <board> NET x0 y0 x1 y1"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
net = sys.argv[2]
x0, y0, x1, y1 = map(float, sys.argv[3:7])
inside = lambda p: x0 <= T(p.x) <= x1 and y0 <= T(p.y) <= y1  # noqa: E731
doomed = [t for t in b.GetTracks() if t.GetNetname() == net and not t.IsLocked() and (
    inside(t.GetPosition()) if t.GetClass() == "PCB_VIA" else inside(t.GetStart()) and inside(t.GetEnd()))]
for t in doomed:
    b.Remove(t)
b.Save(sys.argv[1])
print("removed", len(doomed))
