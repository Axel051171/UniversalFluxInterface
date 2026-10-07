"""Add a silkscreen label near a target spot, at the closest position that collides with no
pad, no silkscreen item and no reference text (shape based) and stays inside the board.
Re-running replaces a label with the same text on the same layer within RADIUS + 1 mm.
usage: python add_silk_label.py <board> F|B SIZE_MM "TEXT" X Y [RADIUS_MM]
"""
import math
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
CLR = MM(0.15)
STEP = 0.25

path, side, size, text, tx, ty = sys.argv[1:7]
radius = float(sys.argv[7]) if len(sys.argv) > 7 else 4.0
b = pcbnew.LoadBoard(path)
silk = pcbnew.F_SilkS if side == "F" else pcbnew.B_SilkS
cu = pcbnew.F_Cu if side == "F" else pcbnew.B_Cu


obs = []
for f in b.GetFootprints():
    obs += [p.GetEffectiveShape(cu) for p in f.Pads() if p.IsOnLayer(cu)]
    obs += [p.GetEffectiveHoleShape() for p in f.Pads() if p.HasHole()]
    obs += [g.GetEffectiveShape() for g in f.GraphicalItems() if g.GetLayer() == silk]
    for t in (f.Reference(), f.Value()):
        if t.IsVisible() and t.GetLayer() == silk:
            obs.append(t.GetEffectiveShape())
old = [d for d in b.GetDrawings()   # same text near the target only (e.g. several "GND")
       if d.GetClass() == "PCB_TEXT" and d.GetLayer() == silk and d.GetText() == text
       and math.hypot(T(d.GetPosition().x) - float(tx), T(d.GetPosition().y) - float(ty)) <= radius + 1]
obs += [d.GetEffectiveShape() for d in b.GetDrawings() if d.GetLayer() == silk and d not in old]
obs += [v.GetEffectiveShape(cu) for v in b.GetTracks() if v.GetClass() == "PCB_VIA"]  # mask-free vias

lbl = pcbnew.PCB_TEXT(b)
lbl.SetText(text)
lbl.SetLayer(silk)
lbl.SetTextSize(pcbnew.VECTOR2I(MM(float(size)), MM(float(size))))
lbl.SetTextThickness(MM(float(size) * 0.15))
if side == "B":
    lbl.SetMirrored(True)
eb = b.GetBoardEdgesBoundingBox()
eb.Inflate(-MM(0.5))

cands = []
n = int(radius / STEP)
for ix in range(-n, n + 1):
    for iy in range(-n, n + 1):
        d = math.hypot(ix * STEP, iy * STEP)
        if d <= radius:
            cands.append((d, float(tx) + ix * STEP, float(ty) + iy * STEP))
for _, x, y in sorted(cands):
    lbl.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    if not eb.Contains(lbl.GetBoundingBox()):
        continue
    shape = lbl.GetEffectiveShape()
    if any(o.Collide(shape, CLR) for o in obs):
        continue
    for d in old:                       # removed only now: pcbnew must not iterate after Remove()
        b.Remove(d)
    b.Add(lbl)
    b.Save(path)
    print(f"'{text}' ({side}) -> ({x:.2f},{y:.2f})")
    break
else:
    raise SystemExit(f"no free spot for '{text}' near ({tx},{ty})")
