"""Grow the board downward: move every Edge.Cuts point and every board-wide plane vertex below Y_FROM
by DY (zones that ended at the old edge follow it), optionally copy mounting holes.
usage: extend_outline.py board Y_FROM DY [SRC:NEW:x:y ...]   e.g. 80 12 H3:H5:3.5:93.5
Run before 'make_pcb.sh eco' for the parts that go into the new area."""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
path, y_from, dy = sys.argv[1], float(sys.argv[2]), float(sys.argv[3])
b = pcbnew.LoadBoard(path)
lim, d = MM(y_from), MM(dy)


def shift(p):
    return pcbnew.VECTOR2I(p.x, p.y + d) if p.y > lim else p


n_edge = 0
for g in b.GetDrawings():
    if g.GetLayer() != pcbnew.Edge_Cuts or not isinstance(g, pcbnew.PCB_SHAPE):
        continue
    if g.GetShape() == pcbnew.SHAPE_T_ARC:
        s, m, e = g.GetStart(), g.GetArcMid(), g.GetEnd()
        if max(s.y, m.y, e.y) > lim:
            g.SetArcGeometry(shift(s), shift(m), shift(e))
            n_edge += 1
    else:
        s, e = g.GetStart(), g.GetEnd()
        if max(s.y, e.y) > lim:
            g.SetStart(shift(s))
            g.SetEnd(shift(e))
            n_edge += 1

n_zone = 0
for z in b.Zones():
    if z.GetBoundingBox().GetY() > MM(1):
        continue                        # only board-wide planes follow the edge, not pours
    o = z.Outline()
    moved = False
    for i in range(o.TotalVertices()):
        v = o.CVertex(i)
        if v.y > lim:
            o.SetVertex(i, pcbnew.VECTOR2I(v.x, v.y + d))
            moved = True
    if moved:
        z.UnFill()
        n_zone += 1

for spec in sys.argv[4:]:
    src, new, x, y = spec.split(":")
    fp = b.FindFootprintByReference(src)
    c = fp.Duplicate(False).Cast()
    c.SetReference(new)
    c.SetPosition(pcbnew.VECTOR2I(MM(float(x)), MM(float(y))))
    b.Add(c)
    print("copied", src, "->", new, "at", x, y)

pcbnew.SaveBoard(path, b)
print(f"edge items moved {n_edge}, zones stretched {n_zone}")
