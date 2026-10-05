"""Post-routing step: add GND pours on F.Cu/B.Cu, stitch them to In1 GND, fill all zones.

Outer pours are added only after routing so Freerouting does not treat them as
planes (it would skip the vias that tie GND pads to the inner GND layer).
usage: python finish_pcb.py <board>
"""
import math
import sys

import pcbnew

MM = pcbnew.FromMM
path = sys.argv[1]
board = pcbnew.LoadBoard(path)
bb = board.GetBoardEdgesBoundingBox()
x0, y0 = pcbnew.ToMM(bb.GetX()), pcbnew.ToMM(bb.GetY())
x1, y1 = x0 + pcbnew.ToMM(bb.GetWidth()), y0 + pcbnew.ToMM(bb.GetHeight())

for layer, netname in ((pcbnew.F_Cu, "GND"), (pcbnew.B_Cu, "GND"), (pcbnew.In2_Cu, "+3V3")):
    if any(z.GetLayer() == layer for z in board.Zones()):
        continue
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(board.FindNet(netname))
    z.SetLocalClearance(MM(0.25))
    z.SetMinThickness(MM(0.2))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THT_THERMAL)  # SMD solid, THT thermal relief
    z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_ALWAYS)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((x0 + 0.3, y0 + 0.3), (x1 - 0.3, y0 + 0.3), (x1 - 0.3, y1 - 0.3), (x0 + 0.3, y1 - 0.3)):
        ol.Append(MM(x), MM(y))
    board.Add(z)


# GND stitching vias on a grid: tie outer GND pours to the In1 plane.  A position is
# used only if it is clear of every pad, track and via (any layer) and of every courtyard.
VIA_D, VIA_DRILL, GAP, PITCH = 0.6, 0.3, 0.3, 4.0
gnd = board.FindNet("GND")
pads = list(board.GetPads())
tracks = list(board.GetTracks())
courts = []
for fp in board.GetFootprints():
    if fp.GetReference() == "U5":
        continue  # LQFP body interior has no pads: vias there tie the inner GND pour down
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    if cy.OutlineCount():
        courts.append(cy.BBox())
    else:
        courts.append(fp.GetBoundingBox(False))
added = 0
for off in (0.0, PITCH / 2):          # square grid plus offset grid -> ~2.8 mm spacing
    x = x0 + 2.0 + off
    while x < x1 - 2.0:
        y = y0 + 2.0 + off
        while y < y1 - 2.0:
            p = pcbnew.VECTOR2I(MM(x), MM(y))
            clear = MM(VIA_D / 2 + GAP)
            if (not any(b.Contains(p) for b in courts)
                    and not any(pd.HitTest(p, clear) for pd in pads)
                    and not any(t.HitTest(p, clear) for t in tracks)):
                v = pcbnew.PCB_VIA(board)
                v.SetPosition(p)
                v.SetWidth(MM(VIA_D))
                v.SetDrill(MM(VIA_DRILL))
                v.SetNet(gnd)
                board.Add(v)
                tracks.append(v)
                added += 1
            y += PITCH
        x += PITCH
print("stitching vias", added)

pcbnew.ZONE_FILLER(board).Fill(board.Zones())


def island_vias():
    """Give every outer GND pour fragment without a via/THT anchor its own via."""
    anchors = [t.GetPosition() for t in board.GetTracks()
               if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND"]
    anchors += [p.GetPosition() for p in board.GetPads() if p.HasHole() and p.GetNetname() == "GND"]
    others = [p for p in board.GetPads() if p.GetNetname() != "GND"]
    other_tracks = [t for t in board.GetTracks() if t.GetNetname() != "GND"]
    clear = MM(VIA_D / 2 + 0.2)
    ring = [(math.cos(a) * 0.45, math.sin(a) * 0.45) for a in [k * math.pi / 4 for k in range(8)]]
    added = 0
    for z in board.Zones():
        if z.GetNetname() != "GND" or z.GetLayer() not in (pcbnew.F_Cu, pcbnew.B_Cu):
            continue
        polys = z.GetFilledPolysList(z.GetLayer())
        for i in range(polys.OutlineCount()):
            if any(polys.Contains(a, i) for a in anchors):
                continue
            bb = polys.Outline(i).BBox()
            found = None
            step = MM(0.25)
            yy = bb.GetTop()
            while yy <= bb.GetBottom() and not found:
                xx = bb.GetLeft()
                while xx <= bb.GetRight():
                    p = pcbnew.VECTOR2I(xx, yy)
                    if (polys.Contains(p, i)
                            and all(polys.Contains(pcbnew.VECTOR2I(xx + MM(dx), yy + MM(dy)), i) for dx, dy in ring)
                            and not any(o.HitTest(p, clear) for o in others)
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
                added += 1
    return added


DOG_VIA = (0.48, 0.25)  # small via, same as the USB-C D- fan-out


def pad_dogbones():
    """GND SMD pads sitting in a pour fragment without anchor get pad -> track -> via."""
    anchors = [t.GetPosition() for t in board.GetTracks()
               if t.GetClass() == "PCB_VIA" and t.GetNetname() == "GND"]
    anchors += [p.GetPosition() for p in board.GetPads() if p.HasHole() and p.GetNetname() == "GND"]
    foreign_pads = [p for p in board.GetPads() if p.GetNetname() != "GND"]
    foreign_tracks = [t for t in board.GetTracks() if t.GetNetname() != "GND"]
    zones = {z.GetLayer(): z.GetFilledPolysList(z.GetLayer()) for z in board.Zones()
             if z.GetNetname() == "GND" and z.GetLayer() in (pcbnew.F_Cu, pcbnew.B_Cu)}
    added = 0
    for pad in board.GetPads():
        if pad.GetNetname() != "GND" or pad.HasHole():
            continue
        layer = pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
        polys = zones.get(layer)
        c = pad.GetPosition()
        if polys is None:
            continue
        frag = next((i for i in range(polys.OutlineCount()) if polys.Contains(c, i)), None)
        if frag is not None and any(polys.Contains(a, frag) for a in anchors):
            continue
        # also skip pads already joined to a GND via by a track
        if any(t.GetNetname() == "GND" and t.GetClass() == "PCB_TRACK" and t.IsOnLayer(layer)
               and (pad.HitTest(t.GetStart()) or pad.HitTest(t.GetEnd())) for t in board.GetTracks()):
            continue
        done = False
        for r in [x * 0.25 for x in range(3, 17)]:
            for k in range(32):
                a = k * math.pi / 16
                v = pcbnew.VECTOR2I(c.x + MM(r * math.cos(a)), c.y + MM(r * math.sin(a)))
                if any(o.HitTest(v, MM(DOG_VIA[0] / 2 + 0.16)) for o in foreign_pads):
                    continue
                if any(t.HitTest(v, MM(DOG_VIA[0] / 2 + 0.16)) for t in foreign_tracks):
                    continue
                if any((a.x - v.x) ** 2 + (a.y - v.y) ** 2 < MM(VIA_D + 0.3) ** 2 for a in anchors):
                    continue  # keep hole-to-hole distance to existing GND vias
                # sample the connecting 0.25 mm track on the pad layer
                ok = True
                for s in range(1, 11):
                    q = pcbnew.VECTOR2I(c.x + (v.x - c.x) * s // 10, c.y + (v.y - c.y) * s // 10)
                    if any(o.IsOnLayer(layer) and o.HitTest(q, MM(0.1 + 0.16)) for o in foreign_pads) or \
                       any(t.IsOnLayer(layer) and t.HitTest(q, MM(0.1 + 0.16)) for t in foreign_tracks):
                        ok = False
                        break
                if not ok:
                    continue
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(c)
                t.SetEnd(v)
                t.SetLayer(layer)
                t.SetWidth(MM(0.2))
                t.SetNet(gnd)
                board.Add(t)
                via = pcbnew.PCB_VIA(board)
                via.SetPosition(v)
                via.SetWidth(MM(DOG_VIA[0]))
                via.SetDrill(MM(DOG_VIA[1]))
                via.SetNet(gnd)
                board.Add(via)
                anchors.append(v)
                added += 1
                done = True
                break
            if done:
                break
        if not done:
            print("no dogbone for", pad.GetParentFootprint().GetReference(), pad.GetNumber())
    return added


print("dogbones", pad_dogbones())
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
for _ in range(3):
    n = island_vias()
    print("island vias", n)
    if not n:
        break
    pcbnew.ZONE_FILLER(board).Fill(board.Zones())

# Annular ring >= 0.125 mm on every via (Freerouting emits 0.6/0.4, dogbones are 0.48/0.25):
# shrink the drill rather than grow the pad, so copper clearances stay unchanged.
MIN_AR = 0.125
shrunk = 0
for t in board.GetTracks():
    if t.GetClass() == "PCB_VIA":
        w = pcbnew.ToMM(t.GetWidth(pcbnew.F_Cu))
        if (w - pcbnew.ToMM(t.GetDrillValue())) / 2 < MIN_AR - 1e-6:
            t.SetDrill(MM(round(w - 2 * MIN_AR, 3)))
            shrunk += 1
print("via drills shrunk", shrunk)
board.Save(path)
print("zones", len(list(board.Zones())), "tracks", len(board.GetTracks()))
