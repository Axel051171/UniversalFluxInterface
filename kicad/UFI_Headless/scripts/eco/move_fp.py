"""Move one footprint, remove tracks/vias under its new courtyard (+0.2 mm) and all tracks
of the given nets (they get re-routed).  usage: move_fp.py board REF x y rot [NET ...]"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
ref, x, y, rot = sys.argv[2], float(sys.argv[3]), float(sys.argv[4]), float(sys.argv[5])
nets = set(sys.argv[6:])
fp = b.FindFootprintByReference(ref)
fp.SetOrientationDegrees(rot)
fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
cy = fp.GetCourtyard(pcbnew.F_CrtYd)
bb = cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)
bb.Inflate(MM(0.2))
print(f"{ref} courtyard x {T(bb.GetLeft()):.2f}..{T(bb.GetRight()):.2f} y {T(bb.GetTop()):.2f}..{T(bb.GetBottom()):.2f}")
doomed = [t for t in b.GetTracks() if t.GetNetname() in nets or t.GetBoundingBox().Intersects(bb)]
for t in doomed:
    b.Remove(t)
b.Save(sys.argv[1])
print("removed", len(doomed))
