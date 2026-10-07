"""ASCII map of where a track centre of NET (width W) may go on a layer.
'.' free  '+' touches own-net copper  '#' blocked.  Also 'v' where a 0.6 mm via fits on all layers.
usage: free_map.py <board> NET W LAYER x0 y0 x1 y1 [step]
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
LAY = {"F": pcbnew.F_Cu, "I2": pcbnew.In2_Cu, "B": pcbnew.B_Cu}
path, netname, w, layname = sys.argv[1:5]
x0, y0, x1, y1 = map(float, sys.argv[5:9])
step = float(sys.argv[9]) if len(sys.argv) > 9 else 0.25
b = pcbnew.LoadBoard(path)
code = b.FindNet(netname).GetNetCode()
CLR = MM(0.2)
r_trk = MM(float(w)) // 2
r_via = MM(0.3)

box = pcbnew.BOX2I(pcbnew.VECTOR2I(MM(x0 - 2), MM(y0 - 2)), pcbnew.VECTOR2I(MM(x1 - x0 + 4), MM(y1 - y0 + 4)))
items = {k: [] for k in LAY.values()}
own = []
for f in b.GetFootprints():
    for p in f.Pads():
        if not p.GetBoundingBox().Intersects(box):
            continue
        for lay in LAY.values():
            if p.IsOnLayer(lay):
                (own if p.GetNetCode() == code and lay == LAY[layname] else items[lay]).append(
                    p.GetEffectiveShape(lay)) if p.GetNetCode() == code and lay == LAY[layname] or p.GetNetCode() != code else None
        if p.HasHole():
            for lay in LAY.values():
                items[lay].append(p.GetEffectiveHoleShape())
for t in b.GetTracks():
    if not t.GetBoundingBox().Intersects(box):
        continue
    if t.GetClass() == "PCB_VIA":
        for lay in LAY.values():
            (own if t.GetNetCode() == code and lay == LAY[layname] else items[lay]).append(
                t.GetEffectiveShape(lay)) if t.GetNetCode() == code and lay == LAY[layname] or t.GetNetCode() != code else None
    elif t.GetLayer() in items:
        if t.GetNetCode() == code:
            if t.GetLayer() == LAY[layname]:
                own.append(t.GetEffectiveShape())
        else:
            items[t.GetLayer()].append(t.GetEffectiveShape())

L = LAY[layname]
nx, ny = int((x1 - x0) / step) + 1, int((y1 - y0) / step) + 1
print("      " + "".join(str(int(x0 + i * step) % 10) if abs((x0 + i * step) - round(x0 + i * step)) < 1e-6 else " " for i in range(nx)))
for j in range(ny):
    y = y0 + j * step
    row = []
    for i in range(nx):
        p = pcbnew.VECTOR2I(MM(x0 + i * step), MM(y))
        c = pcbnew.SHAPE_CIRCLE(p, r_trk)
        if any(s.Collide(c, CLR) for s in items[L]):
            row.append("#")
            continue
        cv = pcbnew.SHAPE_CIRCLE(p, r_via)
        via_ok = all(not any(s.Collide(cv, CLR) for s in items[lay]) for lay in LAY.values())
        if any(s.Collide(c, 0) for s in own):
            row.append("+")
        else:
            row.append("v" if via_ok else ".")
    print(f"{y:6.2f}" + "".join(row))
