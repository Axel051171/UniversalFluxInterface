"""Dogbone fan-out for a row of fine-pitch SMD pads: stub out of each pad plus a via, vias in two
staggered rows so that the neighbouring stubs clear them (0.5/0.25 mm vias: 0.15 mm rule at 0.5 mm pitch).

usage: fanout.py <board> REF PAD[,PAD...] +x|-x|+y|-y D1 D2 [check]
  direction: which way the stubs leave the pad centre; D1/D2: distance of the via rows from the
  pad centre (mm), pads alternate D1, D2, D1, ...  'check' only reports collisions.
"""
import os
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
VIA_D, VIA_DR = MM(0.5), MM(0.25)
CLR = MM(float(os.environ.get("ROUTE_CLR", "0.15")))
HOLE_CLR = MM(0.25)
LAYERS = (pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)

path, ref, pads, direction = sys.argv[1:5]
d1, d2 = float(sys.argv[5]), float(sys.argv[6])
check = len(sys.argv) > 7 and sys.argv[7] == "check"
dx, dy = {"+x": (1, 0), "-x": (-1, 0), "+y": (0, 1), "-y": (0, -1)}[direction]
board = pcbnew.LoadBoard(path)
fp = board.FindFootprintByReference(ref)
want = pads.split(",")
plist = {p.GetNumber(): p for p in fp.Pads()}

obst = []   # (layer or None, shape, label)
holes = []  # (shape, label)
smd = []    # (layer, shape, label) every SMD pad: no via inside
for f in board.GetFootprints():
    for p in f.Pads():
        lbl = "%s.%s[%s]" % (f.GetReference(), p.GetNumber(), p.GetNetname())
        if p.HasHole():
            holes.append((p.GetEffectiveHoleShape(), lbl))
        else:
            smd.append((p.GetLayer(), p.GetEffectiveShape(p.GetLayer()), lbl))
        for lay in LAYERS:
            if p.IsOnLayer(lay):
                obst.append((lay, p.GetEffectiveShape(lay), lbl, p.GetNetCode()))
for t in board.GetTracks():
    lbl = "%s[%s]" % (t.GetClass(), t.GetNetname())
    if t.GetClass() == "PCB_VIA":
        holes.append((pcbnew.SHAPE_SEGMENT(t.GetPosition(), t.GetPosition(), t.GetDrill()), lbl))
        for lay in LAYERS:
            obst.append((lay, t.GetEffectiveShape(lay), lbl, t.GetNetCode()))
    else:
        obst.append((t.GetLayer(), t.GetEffectiveShape(), lbl, t.GetNetCode()))


def hits(shape, layer, net):
    out = [lbl for lay, s, lbl, n in obst if n != net and (layer is None or lay == layer) and s.Collide(shape, CLR)]
    return out


new, bad = [], []
for k, num in enumerate(want):
    pad = plist[num]
    net = pad.GetNetCode()
    c = pad.GetPosition()
    d = d1 if k % 2 == 0 else d2
    e = pcbnew.VECTOR2I(c.x + MM(dx * d), c.y + MM(dy * d))
    w = MM(0.2)
    seg = pcbnew.SHAPE_SEGMENT(c, e, w)
    bad += ["%s.%s stub: %s" % (ref, num, x) for x in hits(seg, pad.GetLayer(), net)]
    via = pcbnew.SHAPE_CIRCLE(e, VIA_D // 2)
    for lay in LAYERS:
        bad += ["%s.%s via %s: %s" % (ref, num, board.GetLayerName(lay), x) for x in hits(via, lay, net)]
    hole = pcbnew.SHAPE_CIRCLE(e, VIA_DR // 2)
    bad += ["%s.%s hole: %s" % (ref, num, lbl) for s, lbl in holes if s.Collide(hole, HOLE_CLR)]
    bad += ["%s.%s via in pad %s" % (ref, num, lbl) for lay, s, lbl in smd if s.Collide(via, MM(0.05))]
    t = pcbnew.PCB_TRACK(board)
    t.SetStart(c)
    t.SetEnd(e)
    t.SetWidth(w)
    t.SetLayer(pad.GetLayer())
    t.SetNet(pad.GetNet())
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(e)
    v.SetWidth(VIA_D)
    v.SetDrill(VIA_DR)
    v.SetNet(pad.GetNet())
    new += [t, v]
    # the new via blocks the following stubs/vias of this fan-out
    obst.append((pad.GetLayer(), seg, "stub %s" % num, net))
    for lay in LAYERS:  # SHAPE_SEGMENT: SHAPE_CIRCLE.Collide(SHAPE) is not wrapped for Python
        obst.append((lay, pcbnew.SHAPE_SEGMENT(e, e, VIA_D), "via %s" % num, net))
    holes.append((pcbnew.SHAPE_SEGMENT(e, e, VIA_DR), "via %s" % num))
    print("%s.%s %-18s via (%.2f, %.2f)" % (ref, num, pad.GetNetname()[:18], T(e.x), T(e.y)))
for b in sorted(set(bad)):
    print("HIT", b)
print("collisions", len(set(bad)))
if not check and not bad:
    for it in new:
        board.Add(it)
    board.Save(path)
    print("added", len(new))
