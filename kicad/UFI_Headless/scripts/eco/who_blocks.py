"""List other-net copper within CLR of a circle (x, y, r) on a layer.
usage: who_blocks.py board NET LAYER x y r [clr]"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
code = b.FindNet(sys.argv[2]).GetNetCode()
L = {"F": pcbnew.F_Cu, "I2": pcbnew.In2_Cu, "B": pcbnew.B_Cu}[sys.argv[3]]
x, y, r = map(float, sys.argv[4:7])
clr = MM(float(sys.argv[7]) if len(sys.argv) > 7 else 0.15)
c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(MM(x), MM(y)), MM(r))
for f in b.GetFootprints():
    for p in f.Pads():
        if p.GetNetCode() != code and p.IsOnLayer(L) and p.GetEffectiveShape(L).Collide(c, clr):
            print("pad", f.GetReference(), p.GetNumber(), p.GetNetname())
for t in b.GetTracks():
    if t.GetNetCode() == code:
        continue
    s = t.GetEffectiveShape(L) if t.GetClass() == "PCB_VIA" else (t.GetEffectiveShape() if t.GetLayer() == L else None)
    if s and s.Collide(c, clr):
        print(t.GetClass(), t.GetNetname())
