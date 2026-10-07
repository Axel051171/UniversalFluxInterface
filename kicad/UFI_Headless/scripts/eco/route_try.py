"""Check (and optionally add) a hand route: polyline of x,y,layer points, via where the layer changes.
Collisions are tested against other-net pads, tracks, vias and holes (zones are refilled anyway).
usage: route_try.py <board> NET WIDTH check|apply x,y,L x,y,L ...   (L = F|I2|B)
"""
import os
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
CLR = MM(float(os.environ.get("ROUTE_CLR", "0.2")))  # a bit above the 0.15 mm rule
VIA_D, VIA_DR = MM(0.6), MM(0.3)
LAY = {"F": pcbnew.F_Cu, "I2": pcbnew.In2_Cu, "B": pcbnew.B_Cu, "I1": pcbnew.In1_Cu}

path, netname, width, mode = sys.argv[1], sys.argv[2], MM(float(sys.argv[3])), sys.argv[4]
pts = []
for a in sys.argv[5:]:
    x, y, lay = a.split(",")
    pts.append((pcbnew.VECTOR2I(MM(float(x)), MM(float(y))), LAY[lay]))
b = pcbnew.LoadBoard(path)
net = b.FindNet(netname)
code = net.GetNetCode()

obst = []  # (layer or None for all copper, shape, label)
for f in b.GetFootprints():
    for p in f.Pads():
        lbl = f"{f.GetReference()}.{p.GetNumber()}[{p.GetNetname()}]"
        if p.GetNetCode() != code:
            for lay in LAY.values():
                if p.IsOnLayer(lay):
                    obst.append((lay, p.GetEffectiveShape(lay), lbl))
        if p.HasHole() and p.GetNetCode() != code:  # own THT pad: track ends on it
            obst.append((None, p.GetEffectiveHoleShape(), lbl + " hole"))
for t in b.GetTracks():
    if t.GetNetCode() == code:
        continue
    lbl = f"{t.GetClass()}[{t.GetNetname()}]"
    if t.GetClass() == "PCB_VIA":
        for lay in LAY.values():
            obst.append((lay, t.GetEffectiveShape(lay), lbl))
    else:
        obst.append((t.GetLayer(), t.GetEffectiveShape(), lbl))


def hits(shape, layer):
    out = []
    for lay, s, lbl in obst:
        if (lay is None or lay == layer) and s.Collide(shape, CLR):
            out.append(lbl)
    return out


new, bad = [], []
for (p0, l0), (p1, l1) in zip(pts, pts[1:]):
    if p0 == p1 and l0 != l1:  # via
        for lay in LAY.values():
            h = hits(pcbnew.SHAPE_CIRCLE(p0, VIA_D // 2), lay)
            bad += [f"via ({T(p0.x):.2f},{T(p0.y):.2f}) {b.GetLayerName(lay)}: {x}" for x in h]
        v = pcbnew.PCB_VIA(b)
        v.SetPosition(p0)
        v.SetWidth(VIA_D)
        v.SetDrill(VIA_DR)
        v.SetNet(net)
        new.append(v)
        continue
    seg = pcbnew.SHAPE_SEGMENT(p0, p1, width)
    h = hits(seg, l0)
    bad += [f"seg ({T(p0.x):.2f},{T(p0.y):.2f})-({T(p1.x):.2f},{T(p1.y):.2f}) {b.GetLayerName(l0)}: {x}" for x in h]
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(p0)
    t.SetEnd(p1)
    t.SetWidth(width)
    t.SetLayer(l0)
    t.SetNet(net)
    new.append(t)
for x in sorted(set(bad)):
    print("HIT", x)
print("collisions", len(set(bad)))
if mode == "apply" and not bad:
    for t in new:
        b.Add(t)
    b.Save(path)
    print("added", len(new))
