"""Close small leftovers after incremental routing, with collision checks against real copper.

  tp REF          move a test point onto the nearest free spot of an F.Cu track of its net
  via REF.PAD     tie an SMD GND pad to the In1 plane: short F.Cu stub + via (0.48/0.23)
usage: python fix_connections.py <board> tp:TP2 tp:TP6 via:D11.2 via:C10.2 ...
"""
import math
import sys

import pcbnew

T, MM = pcbnew.ToMM, pcbnew.FromMM
V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731
CLR, HOLE_CLR, VIA = 0.2, 0.25, (0.48, 0.23)
b = pcbnew.LoadBoard(sys.argv[1])
fps = {f.GetReference(): f for f in b.GetFootprints()}
CU = [pcbnew.F_Cu, pcbnew.In1_Cu, pcbnew.In2_Cu, pcbnew.B_Cu]


def obstacles(net, layers, skip_fp=None):
    obs, holes = [], []
    for f in b.GetFootprints():
        for p in f.Pads():
            if p.HasHole():
                holes.append((T(p.GetPosition().x), T(p.GetPosition().y), T(max(p.GetDrillSize().x, p.GetDrillSize().y)) / 2))
            if f is skip_fp or p.GetNetname() == net:
                continue
            obs += [p.GetEffectiveShape(L) for L in layers if p.IsOnLayer(L)]
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA":
            holes.append((T(t.GetPosition().x), T(t.GetPosition().y), T(t.GetDrillValue()) / 2))
        if t.GetNetname() == net:
            continue
        if t.GetClass() == "PCB_VIA":
            obs += [t.GetEffectiveShape(L) for L in layers]
        elif t.GetLayer() in layers:
            obs.append(t.GetEffectiveShape())
    return obs, holes


def free(shape, obs, clr=CLR):
    return not any(o.Collide(shape, MM(clr)) for o in obs)


def move_tp(ref):
    fp = fps[ref]
    pad = next(iter(fp.Pads()))
    net = pad.GetNetname()
    r = T(pad.GetSize(pcbnew.F_Cu).x) / 2
    obs, holes = obstacles(net, [pcbnew.F_Cu], skip_fp=fp)
    rects = [f.GetCourtyard(pcbnew.F_CrtYd).BBox() for f in b.GetFootprints()
             if f is not fp and f.GetCourtyard(pcbnew.F_CrtYd).OutlineCount()]
    cx, cy = T(fp.GetPosition().x), T(fp.GetPosition().y)
    best = None
    for t in b.GetTracks():
        if t.GetClass() == "PCB_VIA" or t.GetNetname() != net or t.GetLayer() != pcbnew.F_Cu:
            continue
        s, e = t.GetStart(), t.GetEnd()
        n = max(1, int(math.hypot(T(e.x - s.x), T(e.y - s.y)) / 0.25))
        for k in range(n + 1):
            x = T(s.x) + (T(e.x) - T(s.x)) * k / n
            y = T(s.y) + (T(e.y) - T(s.y)) * k / n
            d = math.hypot(x - cx, y - cy)
            if best and d >= best[0]:
                continue
            if any(math.hypot(x - hx, y - hy) < r + hr + HOLE_CLR for hx, hy, hr in holes):
                continue
            tp = pcbnew.BOX2I(V(x - r - 0.25, y - r - 0.25), pcbnew.VECTOR2I(MM(2 * r + 0.5), MM(2 * r + 0.5)))
            if any(bb.Intersects(tp) for bb in rects):
                continue
            if free(pcbnew.SHAPE_CIRCLE(V(x, y), MM(r)), obs):
                best = (d, x, y)
    if not best:
        print(ref, "no spot on", net)
        return
    fp.SetPosition(V(best[1], best[2]))
    print(f"{ref} ({net}) -> ({best[1]:.2f},{best[2]:.2f}), moved {best[0]:.1f} mm")


def pad_via(spec):
    ref, num = spec.split(".")
    pad = next(p for p in fps[ref].Pads() if p.GetNumber() == num)
    net = pad.GetNetname()
    px, py = T(pad.GetPosition().x), T(pad.GetPosition().y)
    obs, holes = obstacles(net, CU)
    obs_f, _ = obstacles(net, [pcbnew.F_Cu])
    best = None
    for ix in range(-20, 21):
        for iy in range(-20, 21):
            x, y = px + ix * 0.25, py + iy * 0.25
            d = math.hypot(x - px, y - py)
            if d < 0.6 or d > 5.0 or (best and d >= best[0]):
                continue
            if any(math.hypot(x - hx, y - hy) < VIA[0] / 2 + hr + HOLE_CLR for hx, hy, hr in holes):
                continue
            if not free(pcbnew.SHAPE_CIRCLE(V(x, y), MM(VIA[0] / 2)), obs):
                continue
            if not free(pcbnew.SHAPE_SEGMENT(V(px, py), V(x, y), MM(0.25)), obs_f):
                continue
            best = (d, x, y)
    if not best:
        print(spec, "no via spot")
        return
    _, x, y = best
    t = pcbnew.PCB_TRACK(b)
    t.SetStart(V(px, py))
    t.SetEnd(V(x, y))
    t.SetLayer(pcbnew.F_Cu)
    t.SetWidth(MM(0.25))
    t.SetNet(pad.GetNet())
    b.Add(t)
    v = pcbnew.PCB_VIA(b)
    v.SetPosition(V(x, y))
    v.SetWidth(MM(VIA[0]))
    v.SetDrill(MM(VIA[1]))
    v.SetNet(pad.GetNet())
    b.Add(v)
    print(f"{spec} ({net}) via at ({x:.2f},{y:.2f})")


for arg in sys.argv[2:]:
    kind, what = arg.split(":")
    move_tp(what) if kind == "tp" else pad_via(what)
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.Save(sys.argv[1])
