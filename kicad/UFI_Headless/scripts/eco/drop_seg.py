"""Remove tracks of NET whose endpoints match x0,y0,x1,y1 (either direction, 0.02 mm).
usage: drop_seg.py <board> NET x0,y0,x1,y1 [...]"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
net = sys.argv[2]
want = [tuple(map(float, a.split(","))) for a in sys.argv[3:]]


def near(p, x, y):
    return abs(T(p.x) - x) < 0.02 and abs(T(p.y) - y) < 0.02


doomed = []
for t in b.GetTracks():
    if t.GetClass() != "PCB_TRACK" or t.GetNetname() != net:
        continue
    s, e = t.GetStart(), t.GetEnd()
    for x0, y0, x1, y1 in want:
        if (near(s, x0, y0) and near(e, x1, y1)) or (near(s, x1, y1) and near(e, x0, y0)):
            doomed.append(t)
for t in doomed:
    b.Remove(t)
b.Save(sys.argv[1])
print("removed", len(doomed))
