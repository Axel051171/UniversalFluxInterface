"""Find via spots on NET's F.Cu tracks inside a box that clear all other-net copper on all
layers (0.2 mm).  usage: via_scan.py board NET x0 y0 x1 y1 [step]"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
code = b.FindNet(sys.argv[2]).GetNetCode()
x0, y0, x1, y1 = map(float, sys.argv[3:7])
step = float(sys.argv[7]) if len(sys.argv) > 7 else 0.25
CLR, R = MM(0.2), MM(0.3)
LAY = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]
obs = []
for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetCode() != code:
            obs += [(l, p.GetEffectiveShape(l)) for l in LAY if p.IsOnLayer(l)]
        if p.HasHole() and p.GetNetCode() != code:
            obs.append((None, p.GetEffectiveHoleShape()))
for t in b.GetTracks():
    if t.GetNetCode() == code:
        continue
    if t.GetClass() == "PCB_VIA":
        obs += [(l, t.GetEffectiveShape(l)) for l in LAY]
    else:
        obs.append((t.GetLayer(), t.GetEffectiveShape()))
seen = set()
for t in b.GetTracks():
    if t.GetNetCode() != code or t.GetClass() == "PCB_VIA" or t.GetLayer() != pcbnew.F_Cu:
        continue
    s, e = t.GetStart(), t.GetEnd()
    L = max(1, int(T((e - s).EuclideanNorm()) / step))
    for k in range(L + 1):
        x = T(s.x) + (T(e.x) - T(s.x)) * k / L
        y = T(s.y) + (T(e.y) - T(s.y)) * k / L
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            continue
        key = (round(x, 2), round(y, 2))
        if key in seen:
            continue
        seen.add(key)
        c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(MM(x), MM(y)), R)
        if not any((l is None or l in LAY) and o.Collide(c, CLR) for l, o in obs):
            print(f"{x:.2f},{y:.2f}")
