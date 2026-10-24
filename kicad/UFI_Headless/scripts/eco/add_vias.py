"""Add through vias of a net at given points, e.g. to tie an isolated GND plane island to the
pours; each via is checked against every other-net copper (0.15 mm), every hole (0.25 mm) and
every SMD pad (no via in pad) on the way, like fanout.py.
usage: add_vias.py <board> NET x,y [x,y ...] [dia drill] [check]
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
CLR, HOLE_CLR = MM(0.15), MM(0.25)
LAYERS = (pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)

args = sys.argv[1:]
check = "check" in args
args = [a for a in args if a != "check"]
path, netname = args[0], args[1]
pts = [tuple(map(float, a.split(","))) for a in args[2:] if "," in a]
rest = [float(a) for a in args[2:] if "," not in a]
via_d, via_dr = (MM(rest[0]), MM(rest[1])) if len(rest) >= 2 else (MM(0.6), MM(0.3))

board = pcbnew.LoadBoard(path)
net = board.FindNet(netname)
if net is None:
    sys.exit("no net " + netname)
code = net.GetNetCode()

obst, holes, smd = [], [], []
for f in board.GetFootprints():
    for p in f.Pads():
        lbl = "%s.%s[%s]" % (f.GetReference(), p.GetNumber(), p.GetNetname())
        if p.HasHole():
            holes.append((p.GetEffectiveHoleShape(), lbl))
        else:
            smd.append((p.GetEffectiveShape(p.GetLayer()), lbl))
        if p.GetNetCode() != code:
            for lay in LAYERS:
                if p.IsOnLayer(lay):
                    obst.append((lay, p.GetEffectiveShape(lay), lbl))
for t in board.GetTracks():
    lbl = "%s[%s]" % (t.GetClass(), t.GetNetname())
    if t.GetClass() == "PCB_VIA":
        holes.append((pcbnew.SHAPE_SEGMENT(t.GetPosition(), t.GetPosition(), t.GetDrill()), lbl))
        if t.GetNetCode() != code:
            for lay in LAYERS:
                obst.append((lay, t.GetEffectiveShape(lay), lbl))
    elif t.GetNetCode() != code:
        obst.append((t.GetLayer(), t.GetEffectiveShape(), lbl))

new, bad = [], []
for x, y in pts:
    c = pcbnew.VECTOR2I(MM(x), MM(y))
    via = pcbnew.SHAPE_CIRCLE(c, via_d // 2)
    hole = pcbnew.SHAPE_CIRCLE(c, via_dr // 2)
    hits = ["copper %s %s" % (board.GetLayerName(lay), lbl) for lay, s, lbl in obst if s.Collide(via, CLR)]
    hits += ["hole %s" % lbl for s, lbl in holes if s.Collide(hole, HOLE_CLR)]
    hits += ["in pad %s" % lbl for s, lbl in smd if s.Collide(via, MM(0.05))]
    print("via %s (%.2f, %.2f): %s" % (netname, x, y, "ok" if not hits else "; ".join(sorted(set(hits)))))
    bad += hits
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(c)
    v.SetWidth(via_d)
    v.SetDrill(via_dr)
    v.SetNet(net)
    new.append(v)
    holes.append((pcbnew.SHAPE_SEGMENT(c, c, via_dr), "new via"))
if not check and not bad:
    for v in new:
        board.Add(v)
    board.Save(path)
    print("added", len(new))
else:
    print("collisions", len(bad), "- nothing added")
