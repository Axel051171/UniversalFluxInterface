"""List filled islands of a net's zone on a layer: bbox, area, and whether any pad/via of
the net touches it.  usage: zone_islands.py board NET LAYER(F|B|I1|I2)"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
net = sys.argv[2]
lay = {"F": pcbnew.F_Cu, "B": pcbnew.B_Cu, "I1": pcbnew.In1_Cu, "I2": pcbnew.In2_Cu}[sys.argv[3]]
anchors = [p.GetPosition() for f in b.GetFootprints() for p in f.Pads()
           if p.GetNetname() == net and p.IsOnLayer(lay)]
anchors += [t.GetPosition() for t in b.GetTracks() if t.GetClass() == "PCB_VIA" and t.GetNetname() == net]
for z in b.Zones():
    if z.GetNetname() != net or not z.IsOnLayer(lay):
        continue
    polys = z.GetFilledPolysList(lay)
    for i in range(polys.OutlineCount()):
        o = polys.Outline(i)
        bb = o.BBox()
        hit = any(polys.Contains(a, i) for a in anchors)
        if not hit:
            print(f"island {i}: x {T(bb.GetLeft()):.2f}..{T(bb.GetRight()):.2f} "
                  f"y {T(bb.GetTop()):.2f}..{T(bb.GetBottom()):.2f} area {T(T(int(o.Area()))):.2f} mm2 NO ANCHOR")
