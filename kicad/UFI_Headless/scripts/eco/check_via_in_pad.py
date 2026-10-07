"""Report every via whose copper touches an SMD pad (via-in-pad / mask-dam check)."""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
pads = [(fp.GetReference(), p) for fp in b.GetFootprints() for p in fp.Pads() if not p.HasHole()]
hits = 0
for v in b.GetTracks():
    if v.GetClass() != "PCB_VIA":
        continue
    vs = v.GetEffectiveShape(pcbnew.F_Cu)
    for ref, p in pads:
        L = pcbnew.F_Cu if p.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
        if p.GetEffectiveShape(L).Collide(vs, 0):
            q = v.GetPosition()
            print(f"{v.GetNetname():12} via ({T(q.x):.3f},{T(q.y):.3f}) in {ref}.{p.GetNumber()}")
            hits += 1
print("via-in-pad:", hits)
