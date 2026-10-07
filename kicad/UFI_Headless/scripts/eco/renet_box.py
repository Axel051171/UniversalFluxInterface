"""Move tracks/vias of net A lying fully inside a box to net B; optionally one pad too.
usage: renet_box.py <board> A B x0 y0 x1 y1 [REF.PAD]"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
a, nb = sys.argv[2], sys.argv[3]
x0, y0, x1, y1 = map(float, sys.argv[4:8])
net = b.FindNet(nb)
inside = lambda p: x0 <= T(p.x) <= x1 and y0 <= T(p.y) <= y1  # noqa: E731
n = 0
for t in b.GetTracks():
    if t.GetNetname() != a:
        continue
    ok = inside(t.GetPosition()) if t.GetClass() == "PCB_VIA" else inside(t.GetStart()) and inside(t.GetEnd())
    if ok:
        t.SetNet(net)
        n += 1
if len(sys.argv) > 8:
    ref, pad = sys.argv[8].split(".")
    for p in b.FindFootprintByReference(ref).Pads():
        if p.GetNumber() == pad:
            p.SetNet(net)
            n += 1
b.Save(sys.argv[1])
print("renetted", n)
