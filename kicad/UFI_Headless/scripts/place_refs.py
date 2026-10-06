"""Move reference texts of the given footprints to a spot that collides with no pad,
no silkscreen item and no other reference (shape based), trying positions around the part.
usage: python place_refs.py <board> REF [REF ...]
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
CLR = MM(0.15)
b = pcbnew.LoadBoard(sys.argv[1])
refs = sys.argv[2:]
fps = {f.GetReference(): f for f in b.GetFootprints()}
SILK = (pcbnew.F_SilkS, pcbnew.B_SilkS)


def obstacles(skip):
    obs = []
    for f in b.GetFootprints():
        obs += [p.GetEffectiveShape(pcbnew.F_Cu) for p in f.Pads() if p.IsOnLayer(pcbnew.F_Cu)]
        obs += [g.GetEffectiveShape() for g in f.GraphicalItems()
                if g.GetLayer() in SILK and g.GetClass() == "PCB_SHAPE"]
        if f.GetReference() != skip and f.Reference().IsVisible():
            obs.append(f.Reference().GetEffectiveShape())
    obs += [d.GetEffectiveShape() for d in b.GetDrawings()
            if d.GetLayer() in SILK and d.GetClass() in ("PCB_TEXT", "PCB_SHAPE")]
    return obs


eb = b.GetBoardEdgesBoundingBox()
for ref in refs:
    fp = fps[ref]
    txt = fp.Reference()
    obs = obstacles(ref)
    bb = fp.GetCourtyard(pcbnew.F_CrtYd).BBox() if fp.GetCourtyard(pcbnew.F_CrtYd).OutlineCount() \
        else fp.GetBoundingBox(False)
    c = fp.GetPosition()
    th = txt.GetBoundingBox().GetHeight()
    tw = txt.GetBoundingBox().GetWidth()
    cands = []
    for d in (0.0, 0.5, 1.0, 1.5, 2.0):  # mm beyond the courtyard
        g = MM(d)
        cands += [pcbnew.VECTOR2I(c.x, bb.GetTop() - th // 2 - g),
                  pcbnew.VECTOR2I(c.x, bb.GetBottom() + th // 2 + g),
                  pcbnew.VECTOR2I(bb.GetLeft() - tw // 2 - g, c.y),
                  pcbnew.VECTOR2I(bb.GetRight() + tw // 2 + g, c.y)]
    done = False
    for p in cands:
        txt.SetPosition(p)
        tb = txt.GetBoundingBox()
        if not eb.Contains(tb):
            continue
        shape = txt.GetEffectiveShape()
        if any(o.Collide(shape, CLR) for o in obs):
            continue
        print(f"{ref} ref -> ({T(p.x):.2f},{T(p.y):.2f})")
        done = True
        break
    if not done:
        txt.SetVisible(False)
        print(f"{ref} ref hidden (no free spot)")
b.Save(sys.argv[1])
