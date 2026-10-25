"""Stage B3: keep the bottom-side area under the CR2032 holder BT1 free of pour copper.
Adds a B.Cu rule area (copper pour not allowed) over the holder body circle (B.Fab circle of
the footprint, +0.5 mm); existing tracks and vias stay allowed, so the routed load trunk under
the holder is not touched.  finish_pcb.py / add_vias.py keep stitching vias out of the
footprint bounding box of bottom-side parts anyway.  Idempotent (zone name 'BT1_keepout').
usage: python bt1_keepout.py <board>
"""
import math
import sys

import pcbnew

MM = pcbnew.FromMM
NAME = "BT1_keepout"
path = sys.argv[1]
board = pcbnew.LoadBoard(path)
if any(z.GetIsRuleArea() and z.GetZoneName() == NAME for z in board.Zones()):
    print("keepout exists")
    sys.exit(0)
fp = board.FindFootprintByReference("BT1")
circ = [g for g in fp.GraphicalItems()
        if g.GetClass() == "PCB_SHAPE" and g.GetShape() == pcbnew.SHAPE_T_CIRCLE and g.GetLayer() == pcbnew.B_Fab]
c = circ[0].GetCenter()
r = circ[0].GetRadius() + MM(0.5)
z = pcbnew.ZONE(board)
z.SetIsRuleArea(True)
z.SetZoneName(NAME)
z.SetLayer(pcbnew.B_Cu)
z.SetDoNotAllowZoneFills(True)
z.SetDoNotAllowTracks(False)
z.SetDoNotAllowVias(False)
z.SetDoNotAllowPads(False)
z.SetDoNotAllowFootprints(False)
ol = z.Outline()
ol.NewOutline()
for k in range(48):
    a = 2 * math.pi * k / 48
    ol.Append(int(c.x + r * math.cos(a)), int(c.y + r * math.sin(a)))
board.Add(z)
board.Save(path)
print("BT1 keepout centre (%.2f, %.2f) r %.2f mm" % (pcbnew.ToMM(c.x), pcbnew.ToMM(c.y), pcbnew.ToMM(r)))
