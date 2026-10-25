"""Stage B3: plane fan-out before the signal re-route.  Every SMD pad of GND (In1 plane) and
+3V3 (In2 pour) without a stub yet gets a short stub (0.25 mm) and a via (0.5/0.25 mm), so
the autorouter later routes the signals around fixed plane connections instead of boxing the
supply pads in.  The via spot is searched on rings of 0.6..5.0 mm around the pad centre
(32 directions); it must keep 0.16 mm to foreign copper (pads, tracks, vias), 0.25 mm hole to
hole, stay out of every SMD pad (no via-in-pad), and the stub must clear foreign copper on the
pad layer.  These functional vias may sit under the BT1 holder (tented, BOTTOM_KEEPOUT); only
the GND stitching grid keeps out of it.
New items are locked so a following Freerouting pass keeps them.
usage: plane_fanout.py <board> [NET,NET]   (default GND,+3V3)
"""
import math
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
VIA = (0.5, 0.25)
STUB = 0.25
BOTTOM_KEEPOUT = False  # functional fan-out vias may sit under the BT1 holder (tented); stitching vias stay out
CLR = 0.16
path = sys.argv[1]
nets = sys.argv[2].split(",") if len(sys.argv) > 2 else ["GND", "+3V3"]
board = pcbnew.LoadBoard(path)
pads = list(board.GetPads())
tracks = list(board.GetTracks())
bb = board.GetBoardEdgesBoundingBox()
X0, Y0 = T(bb.GetLeft()) + 0.6, T(bb.GetTop()) + 0.6
X1, Y1 = T(bb.GetRight()) - 0.6, T(bb.GetBottom()) - 0.6
holes = [(p.GetPosition(), T(max(p.GetDrillSize().x, p.GetDrillSize().y)) / 2) for p in pads if p.HasHole()] + \
        [(t.GetPosition(), T(t.GetDrillValue()) / 2) for t in tracks if t.GetClass() == "PCB_VIA"]
bottom_boxes = [f.GetBoundingBox(False) for f in board.GetFootprints() if f.IsFlipped()]
# keepout rule areas (board level and inside footprints, e.g. the SHT40 U25 body)
keepouts = [z for z in board.Zones() if z.GetIsRuleArea()]
for f in board.GetFootprints():
    keepouts += [z for z in f.Zones() if z.GetIsRuleArea()]


def in_keepout(shape, layer, what):
    """what = 'via' or 'track'; layer None = through all layers (via)."""
    for z in keepouts:
        if what == "via" and not z.GetDoNotAllowVias():
            continue
        if what == "track" and not z.GetDoNotAllowTracks():
            continue
        if layer is not None and not z.IsOnLayer(layer):
            continue
        if z.Outline().Collide(shape):
            return True
    return False


def seg_shape(a, b, w):
    return pcbnew.SHAPE_SEGMENT(a, b, MM(w))


def clear_of(shape, net, layer, clr):
    """True if shape (SHAPE) keeps clr mm to all foreign copper on layer (None = all layers)."""
    for p in pads:
        if p.GetNetname() == net:
            continue
        if layer is not None and not p.IsOnLayer(layer):
            continue
        lay = layer if layer is not None else (pcbnew.F_Cu if p.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu)
        if p.GetEffectiveShape(lay).Collide(shape, MM(clr)):
            return False
    for t in tracks:
        if t.GetNetname() == net:
            continue
        if t.GetClass() == "PCB_VIA":
            if t.GetEffectiveShape(pcbnew.F_Cu).Collide(shape, MM(clr)):
                return False
        elif (layer is None or t.GetLayer() == layer) and t.GetEffectiveShape().Collide(shape, MM(clr)):
            return False
    return True


def via_ok(v, net):
    shape = pcbnew.SHAPE_CIRCLE(v, MM(VIA[0] / 2))
    if not (X0 < T(v.x) < X1 and Y0 < T(v.y) < Y1):
        return False
    for h, hr in holes:
        if (h - v).EuclideanNorm() < MM(VIA[1] / 2 + hr + 0.26):   # hole to hole 0.25 mm
            return False
    for p in pads:  # no via in any SMD pad, own net included
        if not p.HasHole() and p.GetEffectiveShape(pcbnew.F_Cu if p.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu).Collide(shape, MM(0.1)):
            return False
    if BOTTOM_KEEPOUT and any(b.Contains(v) for b in bottom_boxes):
        return False
    if in_keepout(shape, None, "via"):
        return False
    return clear_of(shape, net, None, CLR)


def has_near_via(pad, net):
    c = pad.GetPosition()
    for t in tracks:
        if t.GetClass() != "PCB_TRACK" or t.GetNetname() != net:
            continue
        for e in (t.GetStart(), t.GetEnd()):
            if pad.HitTest(e):
                return True  # already has a stub (fan-out exists)
    return False


added = failed = 0
fails = []
for pad in pads:
    net = pad.GetNetname()
    if net not in nets or pad.HasHole():
        continue
    if has_near_via(pad, net):
        continue
    layer = pcbnew.F_Cu if pad.IsOnLayer(pcbnew.F_Cu) else pcbnew.B_Cu
    c = pad.GetPosition()
    done = False
    for r in [0.6 + 0.1 * k for k in range(45)]:
        for k in range(32):
            a = k * math.pi / 16
            v = pcbnew.VECTOR2I(c.x + MM(r * math.cos(a)), c.y + MM(r * math.sin(a)))
            if not via_ok(v, net):
                continue
            if not clear_of(seg_shape(c, v, STUB), net, layer, CLR) or in_keepout(seg_shape(c, v, STUB), layer, "track"):
                continue
            t = pcbnew.PCB_TRACK(board)
            t.SetStart(c)
            t.SetEnd(v)
            t.SetLayer(layer)
            t.SetWidth(MM(STUB))
            t.SetNet(pad.GetNet())
            t.SetLocked(True)
            board.Add(t)
            via = pcbnew.PCB_VIA(board)
            via.SetPosition(v)
            via.SetWidth(MM(VIA[0]))
            via.SetDrill(MM(VIA[1]))
            via.SetNet(pad.GetNet())
            via.SetLocked(True)
            board.Add(via)
            tracks.extend([t, via])
            holes.append((v, VIA[1] / 2))
            added += 1
            done = True
            break
        if done:
            break
    if not done:
        failed += 1
        fails.append("%s.%s" % (pad.GetParentFootprint().GetReference(), pad.GetNumber()))
board.Save(path)
print("plane fan-out: added", added, "failed", failed, " ".join(fails))
