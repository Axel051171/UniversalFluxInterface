"""Copy tracks/vias of NET inside a box from an old board into the current one, skipping any
item that would collide with other-net copper (0.2 mm) or that already exists.
usage: copy_tracks.py <old_board> <board> NET x0 y0 x1 y1 [check|apply]
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
CLR = MM(0.2)
old_path, path, netname = sys.argv[1:4]
x0, y0, x1, y1 = map(float, sys.argv[4:8])
mode = sys.argv[8] if len(sys.argv) > 8 else "check"
old = pcbnew.LoadBoard(old_path)
b = pcbnew.LoadBoard(path)
net = b.FindNet(netname)
code = net.GetNetCode()
CU = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]


def inside(p):
    return x0 <= T(p.x) <= x1 and y0 <= T(p.y) <= y1


obst = []
for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetCode() != code:
            obst += [(lay, p.GetEffectiveShape(lay), f"{f.GetReference()}.{p.GetNumber()}") for lay in CU if p.IsOnLayer(lay)]
        if p.HasHole() and p.GetNetCode() != code:
            obst.append((None, p.GetEffectiveHoleShape(), f"{f.GetReference()}.{p.GetNumber()} hole"))
for t in b.GetTracks():
    if t.GetNetCode() == code:
        continue
    if t.GetClass() == "PCB_VIA":
        obst += [(lay, t.GetEffectiveShape(lay), f"via[{t.GetNetname()}]") for lay in CU]
    else:
        obst.append((t.GetLayer(), t.GetEffectiveShape(), f"trk[{t.GetNetname()}]"))

have = set()
for t in b.GetTracks():
    if t.GetNetCode() == code:
        have.add((t.GetClass(), t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y, t.GetLayer()))

added = 0
for t in old.GetTracks():
    if t.GetNetname() != netname:
        continue
    if t.GetClass() == "PCB_VIA":
        if not inside(t.GetPosition()):
            continue
        layers = CU
        shapes = [(lay, t.GetEffectiveShape(lay)) for lay in CU]
    else:
        if not (inside(t.GetStart()) and inside(t.GetEnd())):
            continue
        shapes = [(t.GetLayer(), t.GetEffectiveShape())]
    key = (t.GetClass(), t.GetStart().x, t.GetStart().y, t.GetEnd().x, t.GetEnd().y, t.GetLayer())
    if key in have:
        continue
    hit = [lbl for lay, s in shapes for ol, os_, lbl in obst if (ol is None or ol == lay) and os_.Collide(s, CLR)]
    desc = f"{t.GetClass()} ({T(t.GetStart().x):.2f},{T(t.GetStart().y):.2f})-({T(t.GetEnd().x):.2f},{T(t.GetEnd().y):.2f}) {old.GetLayerName(t.GetLayer())} w{T(t.GetWidth()):.2f}"
    if hit:
        print("SKIP", desc, sorted(set(hit))[:3])
        continue
    print("OK  ", desc)
    if mode == "apply":
        n = t.Duplicate()
        n.SetParent(b)
        n.SetNet(net)
        b.Add(n)
        added += 1
if mode == "apply":
    b.Save(path)
    print("added", added)
