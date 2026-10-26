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

# v0.2: Freerouting sometimes necks a Load-class track down (e.g. 0.90 mm next to a pad);
# restore the class width 1.0 mm (DRC afterwards shows whether the clearance still holds)
LOAD = {"+5V_IN", "+12V_IN", "/V5_P", "/V12_P", "+5V_OUT", "+12V_OUT"}
widened = kept = 0
copper = [p for p in board.GetPads()] + [t for t in board.GetTracks()]
for t in board.GetTracks():
    if t.GetClass() == "PCB_TRACK" and t.GetNetname() in LOAD and t.GetWidth() < MM(1.0):
        old = t.GetWidth()
        t.SetWidth(MM(1.0))
        mine = t.GetEffectiveShape(t.GetLayer())
        hit = any(o.GetNetname() != t.GetNetname() and o.IsOnLayer(t.GetLayer())
                  and mine.Collide(o.GetEffectiveShape(t.GetLayer()), MM(0.2))
                  for o in copper if o is not t)
        if hit:
            t.SetWidth(old)
            kept += 1
        else:
            widened += 1
print("load tracks widened", widened, "left narrow", kept)

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


def island_vias():
    """Give every outer GND pour fragment without a via / THT anchor its own via."""
    anchors = [t.GetPosition() for t in board.GetTracks()
               if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND"]
    anchors += [p.GetPosition() for p in board.GetPads() if p.HasHole() and p.GetNetname() == "GND"]
    others = [p for p in board.GetPads() if p.GetNetname() != "GND"]
    own_pads = [p for p in board.GetPads() if p.GetNetname() == "GND"]
    other_tracks = [t for t in board.GetTracks() if t.GetNetname() != "GND"]
    clear = MM(VIA_D / 2 + 0.25)
    ring = [(math.cos(a) * 0.45, math.sin(a) * 0.45) for a in [k * math.pi / 4 for k in range(8)]]
    n = 0
    for z in board.Zones():
        if z.GetNetname() != "GND" or z.GetLayer() not in (pcbnew.F_Cu, pcbnew.B_Cu):
            continue
        polys = z.GetFilledPolysList(z.GetLayer())
        for i in range(polys.OutlineCount()):
            if any(polys.Contains(a, i) for a in anchors):
                continue
            bb = polys.Outline(i).BBox()
            found, step, yy = None, MM(0.25), bb.GetTop()
            while yy <= bb.GetBottom() and not found:
                xx = bb.GetLeft()
                while xx <= bb.GetRight():
                    p = pcbnew.VECTOR2I(xx, yy)
                    if (polys.Contains(p, i)
                            and all(polys.Contains(pcbnew.VECTOR2I(xx + MM(dx), yy + MM(dy)), i) for dx, dy in ring)
                            and not any(o.HitTest(p, clear) for o in others)
                            and not any(o.HitTest(p, MM(VIA_D / 2 + 0.1)) for o in own_pads)
                            and not any(t.HitTest(p, clear) for t in other_tracks)):
                        found = p
                        break
                    xx += step
                yy += step
            if found:
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(found)
                v.SetWidth(MM(VIA_D))
                v.SetDrill(MM(VIA_DRILL))
                v.SetNet(gnd)
                board.Add(v)
                anchors.append(found)
                n += 1
    return n


extra = island_vias()
print("island vias", extra)
if extra:
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())
board.Save(sys.argv[1])
