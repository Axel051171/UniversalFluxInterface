"""After routing: GND pours on both layers, a via next to every SMD GND pad that has no
via of its own nearby, zone fill.  usage: python finish_pcb.py <board>"""
from __future__ import annotations

import math
import sys

import pcbnew

MM = pcbnew.FromMM
VIA_D, VIA_DRILL = 0.6, 0.3
board = pcbnew.LoadBoard(sys.argv[1])
gnd = board.FindNet("GND")
bb = board.GetBoardEdgesBoundingBox()
L, T, Rr, B = (pcbnew.ToMM(v) for v in (bb.GetLeft(), bb.GetTop(), bb.GetRight(), bb.GetBottom()))

for z in list(board.Zones()):
    board.Remove(z)
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(gnd)
    z.SetLocalClearance(MM(0.3))
    z.SetMinThickness(MM(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    z.SetThermalReliefGap(MM(0.3))
    z.SetThermalReliefSpokeWidth(MM(0.4))
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((L + 0.5, T + 0.5), (Rr - 0.5, T + 0.5), (Rr - 0.5, B - 0.5), (L + 0.5, B - 0.5)):
        ol.Append(MM(x), MM(y))
    board.Add(z)

# a via just outside every SMD GND pad (pointing away from the part centre) so the pad
# reaches the solid B.Cu pour even where the F.Cu pour is cut up by tracks
pads = list(board.GetPads())
tracks = list(board.GetTracks())
added = 0
for fp in board.GetFootprints():
    if fp.HasThroughHolePads():
        continue
    c = fp.GetPosition()
    for p in fp.Pads():
        if p.GetNetname() != "GND":
            continue
        dx, dy = p.GetPosition().x - c.x, p.GetPosition().y - c.y
        n = math.hypot(dx, dy) or 1.0
        half = max(pcbnew.ToMM(p.GetBoundingBox().GetWidth()), pcbnew.ToMM(p.GetBoundingBox().GetHeight())) / 2
        for dist in (half + 0.7, half + 1.0, half + 1.4):
            v = pcbnew.VECTOR2I(int(p.GetPosition().x + dx / n * MM(dist)), int(p.GetPosition().y + dy / n * MM(dist)))
            clear = MM(VIA_D / 2 + 0.25)
            # the stub from the pad to the via must not cross a foreign track either
            steps = max(2, int(dist / 0.2))
            stub = [pcbnew.VECTOR2I(int(p.GetPosition().x + (v.x - p.GetPosition().x) * k / steps),
                                    int(p.GetPosition().y + (v.y - p.GetPosition().y) * k / steps))
                    for k in range(1, steps + 1)]
            if any(t.HitTest(s, MM(0.4)) for s in stub for t in tracks if t.GetNetname() != "GND"):
                continue
            if (L + 0.8 < pcbnew.ToMM(v.x) < Rr - 0.8 and T + 0.8 < pcbnew.ToMM(v.y) < B - 0.8
                    and not any(o.HitTest(v, clear) for o in pads if o.GetNetname() != "GND")
                    and not any(o.HitTest(v, clear) for o in pads if o.GetNetname() == "GND" and o is not p)
                    and not any(t.HitTest(v, clear) for t in tracks if t.GetNetname() != "GND")
                    and not any(t.HitTest(v, MM(0.7)) for t in tracks if t.GetClass() == "PCB_VIA")):
                via = pcbnew.PCB_VIA(board)
                via.SetPosition(v)
                via.SetWidth(MM(VIA_D))
                via.SetDrill(MM(VIA_DRILL))
                via.SetNet(gnd)
                board.Add(via)
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(p.GetPosition())
                t.SetEnd(v)
                t.SetLayer(pcbnew.F_Cu)
                t.SetWidth(MM(0.3))
                t.SetNet(gnd)
                board.Add(t)
                tracks += [via, t]
                added += 1
                break
print("gnd vias", added)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
board.Save(sys.argv[1])
