"""Bring schematic additions into an already routed board (incremental ECO).

1. Every pad of every existing footprint gets the net the netlist now specifies.
2. Each new reference is placed near its target: a spot whose courtyard (+margin) stays
   inside the board, clear of other courtyards, holes and locked tracks; among those the
   one touching the fewest existing tracks/vias wins (distance breaks ties).
3. Tracks and vias under the new parts are removed; the caller re-routes
   (drop_violations.py for stale power tracks + Freerouting).
usage: python add_parts.py <netlist.net> <board> REF:x:y[:rot] ...
"""
import math
import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netlist import read_netlist  # noqa: E402

STD = Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
MM, T = pcbnew.FromMM, pcbnew.ToMM
MARGIN = 0.3        # courtyard clearance to other parts (mm)
EDGE = 1.0          # keep this far from the board outline (mm)
SEARCH = 14.0       # search radius around the target (mm)
STEP = 0.5

net_file, path = sys.argv[1:3]
targets = []
for arg in sys.argv[3:]:
    f = arg.split(":")
    targets.append((f[0], float(f[1]), float(f[2]), [float(f[3])] if len(f) > 3 else [0.0, 90.0]))

comps, nets = read_netlist(net_file)
board = pcbnew.LoadBoard(path)
pin_net = {(r, p): n for n, nodes in nets.items() for r, p in nodes}


def net_item(name):
    n = board.FindNet(name)
    if not n:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
    return n


# 1. re-net existing pads
changed = 0
for fp in board.GetFootprints():
    for p in fp.Pads():
        want = pin_net.get((fp.GetReference(), p.GetNumber()))
        if want and p.GetNetname() != want:
            p.SetNet(net_item(want))
            changed += 1
print("pads re-netted", changed)

eb = board.GetBoardEdgesBoundingBox()
bx0, by0 = T(eb.GetX()) + EDGE, T(eb.GetY()) + EDGE
bx1, by1 = T(eb.GetRight()) - EDGE, T(eb.GetBottom()) - EDGE


def rect_of(fp):
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    bb = cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)
    return T(bb.GetX()), T(bb.GetY()), T(bb.GetRight()), T(bb.GetBottom())


def overlap(a, b, m=0.0):
    return a[0] - m < b[2] and b[0] - m < a[2] and a[1] - m < b[3] and b[1] - m < a[3]


def seg_hits_rect(t, r):
    if t.GetClass() == "PCB_VIA":
        p = t.GetPosition()
        rad = T(t.GetWidth(pcbnew.F_Cu)) / 2
        return overlap((T(p.x) - rad, T(p.y) - rad, T(p.x) + rad, T(p.y) + rad), r)
    s, e = t.GetStart(), t.GetEnd()
    w = T(t.GetWidth()) / 2
    x0, y0, x1, y1 = T(s.x), T(s.y), T(e.x), T(e.y)
    for k in range(21):  # sampled segment vs rectangle, good enough at 0.5 mm grid
        x, y = x0 + (x1 - x0) * k / 20, y0 + (y1 - y0) * k / 20
        if r[0] - w <= x <= r[2] + w and r[1] - w <= y <= r[3] + w:
            return True
    return False


placed_rects = [rect_of(f) for f in board.GetFootprints()]
holes = [(T(p.GetPosition().x), T(p.GetPosition().y), T(max(p.GetDrillSize().x, p.GetDrillSize().y)) / 2)
         for f in board.GetFootprints() for p in f.Pads() if p.HasHole()]
tracks = list(board.GetTracks())
new_rects = []

for ref, tx, ty, rots in targets:
    c = comps[ref]
    lib, name = c["footprint"].split(":")
    fp = pcbnew.FootprintLoad(str(STD / f"{lib}.pretty"), name)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetReference(ref)
    fp.SetValue(c["value"])
    fp.SetPath(pcbnew.KIID_PATH(c["path"]))
    for k, v in c["props"].items():
        if k not in ("Reference", "Value", "Footprint") and not k.startswith("ki_"):
            fp.SetField(k, v)
    if c["datasheet"] not in ("", "~"):
        fp.SetField("Datasheet", c["datasheet"])
    for f in fp.GetFields():
        if f.GetName() != "Reference":
            f.SetVisible(False)
    for p in fp.Pads():
        n = pin_net.get((ref, p.GetNumber()))
        if n:
            p.SetNet(net_item(n))

    best = None
    for rot in rots:
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(pcbnew.VECTOR2I(0, 0))
        r0 = rect_of(fp)
        n_steps = int(SEARCH / STEP)
        for ix in range(-n_steps, n_steps + 1):
            for iy in range(-n_steps, n_steps + 1):
                dx, dy = tx + ix * STEP, ty + iy * STEP
                d = math.hypot(ix * STEP, iy * STEP)
                if d > SEARCH:
                    continue
                r = (r0[0] + dx, r0[1] + dy, r0[2] + dx, r0[3] + dy)
                if r[0] < bx0 or r[1] < by0 or r[2] > bx1 or r[3] > by1:
                    continue
                if any(overlap(r, q, MARGIN) for q in placed_rects):
                    continue
                if any(overlap(r, (hx - hr, hy - hr, hx + hr, hy + hr), 0.3) for hx, hy, hr in holes):
                    continue
                hit = [t for t in tracks if seg_hits_rect(t, (r[0] - 0.2, r[1] - 0.2, r[2] + 0.2, r[3] + 0.2))]
                if any(t.IsLocked() for t in hit):
                    continue
                score = len(hit) * 3 + d
                if best is None or score < best[0]:
                    best = (score, rot, dx, dy, r, len(hit))
    if best is None:
        raise SystemExit(f"no free spot for {ref} near ({tx},{ty})")
    _, rot, dx, dy, r, nhit = best
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(MM(dx), MM(dy)))
    board.Add(fp)
    placed_rects.append(r)
    new_rects.append(r)
    print(f"{ref:5} at ({dx:.2f},{dy:.2f}) rot {rot:g}  tracks under it: {nhit}")

# 3. rip up copper under the new parts (collect first, remove, save right away)
doomed = [t for t in tracks
          if any(seg_hits_rect(t, (r[0] - 0.2, r[1] - 0.2, r[2] + 0.2, r[3] + 0.2)) for r in new_rects)]
for t in doomed:
    board.Remove(t)
print("tracks/vias removed", len(doomed))
board.Save(path)
