"""Move silkscreen references / board texts to the nearest free spot around their footprint.
Free = the text's bounding box (plus a margin) touches no pad (any footprint, with the mask
margin), no other silkscreen item (footprint graphics, references, board texts) and stays
inside the board edge.  Candidates are tried on rings around the footprint's courtyard/bbox:
centred above/below/left/right, then the corners, at growing distances.
usage: place_silk.py <board> REF[,REF..] ['TEXT=...' ...] [check] [maxd=MM]
  REF        the reference field of that footprint is moved (at most maxd mm, default 2.0,
             beyond the courtyard; if nothing fits the text is also tried rotated by 90 deg)
  REF@x:y    put the reference at x,y (mm) and only report whether it is free there
  TEXT=str   the board text with this content is moved around its own current position
  TEXT=str@x:y  put that text at x,y
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
MARGIN = MM(0.2)      # silk to silk / silk to pad (rule: silk clearance 0.15 incl. mask expansion)
EDGE = MM(0.5)

args = sys.argv[1:]
check = "check" in args
MAXD = MM(float(next((a[5:] for a in args if a.startswith("maxd=")), "2.0")))
args = [a for a in args if a != "check" and not a.startswith("maxd=")]
board = pcbnew.LoadBoard(args[0])
refs = [r for r in args[1].split(",") if r] if len(args) > 1 and not args[1].startswith("TEXT=") else []
texts = [a[5:] for a in args[1:] if a.startswith("TEXT=")]


def split_at(spec):
    """'NAME@x:y[:angle]' -> (NAME, VECTOR2I, angle or None) ; 'NAME' -> (NAME, None, None)"""
    if "@" in spec:
        name, xy = spec.rsplit("@", 1)
        parts = xy.split(":")   # ':' because ',' separates the REF list
        return name, pcbnew.VECTOR2I(MM(float(parts[0])), MM(float(parts[1]))), (float(parts[2]) if len(parts) > 2 else None)
    return spec, None, None
edge = board.GetBoardEdgesBoundingBox()
edge_in = pcbnew.BOX2I(edge.GetPosition(), edge.GetSize())
edge_in.Inflate(-EDGE)


def grow(bb, d):
    b = pcbnew.BOX2I(bb.GetPosition(), bb.GetSize())
    b.Inflate(d)
    return b


def silk_items(layer):
    """(bbox, owner) of every silkscreen item on this layer and every pad (any copper layer;
    through holes matter on both sides)."""
    out = []
    for fp in board.GetFootprints():
        for p in fp.Pads():
            out.append((grow(p.GetBoundingBox(), MM(0.1)), "pad %s.%s" % (fp.GetReference(), p.GetNumber())))
        for g in fp.GraphicalItems():
            if g.GetLayer() == layer:
                out.append((g.GetBoundingBox(), "silk %s" % fp.GetReference()))
        for f in fp.GetFields():
            if f.IsVisible() and f.GetLayer() == layer:
                out.append((f.GetBoundingBox(), "field %s" % fp.GetReference()))
    for d in board.GetDrawings():
        if d.GetLayer() == layer:
            out.append((d.GetBoundingBox(), "text %s" % (d.GetText() if hasattr(d, "GetText") else d.GetClass())))
    return out


def free(item, items, own):
    bb = grow(item.GetBoundingBox(), MARGIN)
    if not edge_in.Contains(bb):
        return "edge"
    for obb, who in items:
        if who == own:
            continue
        if obb.Intersects(bb):
            return who
    return None


def candidates(anchor, w, h):
    """Positions (text centre) on rings at distance d around the pad box anchor, nearest ring first;
    along each side the centre slides outward in 1 mm steps but stays within MAXD of the box."""
    cx, cy = anchor.GetCenter().x, anchor.GetCenter().y
    hw, hh = anchor.GetWidth() // 2, anchor.GetHeight() // 2
    step1 = MM(1.0)

    def slide(half):
        yield 0
        k = 1
        while k * step1 <= half + MAXD:
            yield -k * step1
            yield k * step1
            k += 1

    for step in range(0, 9):
        d = MM(0.3 + 0.4 * step)
        if d > MAXD:
            return
        dx, dy = hw + w // 2 + d, hh + h // 2 + d
        for sx in slide(hw):                          # above / below
            yield pcbnew.VECTOR2I(cx + sx, cy - dy)
            yield pcbnew.VECTOR2I(cx + sx, cy + dy)
        for sy in slide(hh):                          # left / right
            yield pcbnew.VECTOR2I(cx - dx, cy + sy)
            yield pcbnew.VECTOR2I(cx + dx, cy + sy)


def place(item, anchor, own, label, fixed=None, fixed_angle=None):
    items = silk_items(item.GetLayer())
    if fixed is not None:
        if fixed_angle is not None:
            item.SetTextAngleDegrees(fixed_angle)
        item.SetPosition(fixed)
        blocker = free(item, items, own)
        print("%-22s set to (%.2f,%.2f): %s" % (label, T(fixed.x), T(fixed.y), "free" if blocker is None else "ON " + blocker))
        return blocker is None
    blocker = free(item, items, own)
    if blocker is None:
        print("%-22s ok where it is" % label)
        return True
    start, angle = item.GetPosition(), item.GetTextAngleDegrees()
    for rot in (angle, (angle + 90) % 180):
        item.SetTextAngleDegrees(rot)
        bb = item.GetBoundingBox()
        for pos in candidates(anchor, bb.GetWidth(), bb.GetHeight()):
            item.SetPosition(pos)
            if free(item, items, own) is None:
                print("%-22s (%.2f,%.2f) -> (%.2f,%.2f)%s   [was on %s]" % (
                    label, T(start.x), T(start.y), T(pos.x), T(pos.y), " rot %d" % rot if rot != angle else "", blocker))
                return True
    item.SetPosition(start)
    item.SetTextAngleDegrees(angle)
    print("%-22s NO FREE SPOT within %.1f mm (blocked by %s)" % (label, T(MAXD), blocker))
    return False


ok = True
for spec in refs:
    ref, fixed, fixed_angle = split_at(spec)
    fp = board.FindFootprintByReference(ref)
    if fp is None:
        print(ref, "not found")
        ok = False
        continue
    f = fp.Reference()
    if f.GetLayer() not in (pcbnew.F_SilkS, pcbnew.B_SilkS):
        print("%-22s reference not on a silkscreen layer (skipped)" % ref)
        continue
    anchor = None   # union of the pad boxes: the part's real extent, independent of text/courtyard quirks
    for p in fp.Pads():
        pb = p.GetBoundingBox()
        if anchor is None:
            anchor = pcbnew.BOX2I(pb.GetPosition(), pb.GetSize())
        else:
            anchor.Merge(pb)
    ok &= place(f, anchor, "field %s" % ref, "ref %s" % ref, fixed, fixed_angle)
for spec in texts:
    txt, fixed, fixed_angle = split_at(spec)
    found = [d for d in board.GetDrawings() if d.GetLayer() in (pcbnew.F_SilkS, pcbnew.B_SilkS)
             and hasattr(d, "GetText") and d.GetText() == txt]
    if not found:
        print("text %r not found" % txt)
        ok = False
        continue
    for d in found:
        ok &= place(d, d.GetBoundingBox(), "text %s" % txt, "text %r" % txt, fixed, fixed_angle)
if not check:
    board.Save(args[0])
    print("saved" if ok else "saved (some items not placed)")
