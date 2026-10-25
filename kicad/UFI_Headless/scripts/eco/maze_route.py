"""A* maze router for one connection: grid over a box, signal layers F/In2/(In3)/B, vias where a via
via clears every layer.  Prints a route_try.py point list (x,y,L ...) or 'no path'.
usage: maze_route.py board NET W x0 y0 x1 y1 sx,sy,L tx,ty,L [step] [via_cost]
  start/target points must lie on NET copper of the given layer (pad or track)."""
import heapq
import os
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
code = b.FindNet(sys.argv[2]).GetNetCode()
w = float(sys.argv[3])
x0, y0, x1, y1 = map(float, sys.argv[4:8])
sx, sy, sl = sys.argv[8].split(",")
tx, ty, tl = sys.argv[9].split(",")
step = float(sys.argv[10]) if len(sys.argv) > 10 else 0.25
via_cost = float(sys.argv[11]) if len(sys.argv) > 11 else 8.0
LID = {"F": pcbnew.F_Cu, "I2": pcbnew.In2_Cu, "I3": pcbnew.In3_Cu, "B": pcbnew.B_Cu}
# B3 (6 layers): signal layers F/In2/In3/B, In1/In4 are GND planes.  ROUTE_LAYERS limits the
# routing layers (e.g. "F,B" for controlled-impedance nets); obstacles are taken from all of them.
LALL = [l for l in ("F", "I2", "I3", "B") if b.GetEnabledLayers().Contains(LID[l])]
LN = [l for l in os.environ.get("ROUTE_LAYERS", ",".join(LALL)).split(",") if l in LALL]
VIA_R = float(os.environ.get("ROUTE_VIA_R", "0.3"))   # via radius (0.225 for 0.45/0.2 vias)
CLR = MM(float(os.environ.get("ROUTE_CLR", "0.2")))
nx, ny = int((x1 - x0) / step) + 1, int((y1 - y0) / step) + 1
BUCK = 2.0  # mm bucket size

box = pcbnew.BOX2I(pcbnew.VECTOR2I(MM(x0 - 2), MM(y0 - 2)), pcbnew.VECTOR2I(MM(x1 - x0 + 4), MM(y1 - y0 + 4)))
obs = {l: {} for l in LALL}


def add(lname, shape, bb):
    if not bb.Intersects(box):
        return
    bx0, by0 = int((T(bb.GetLeft()) - x0 - 1) // BUCK), int((T(bb.GetTop()) - y0 - 1) // BUCK)
    bx1, by1 = int((T(bb.GetRight()) - x0 + 1) // BUCK), int((T(bb.GetBottom()) - y0 + 1) // BUCK)
    for i in range(bx0, bx1 + 1):
        for j in range(by0, by1 + 1):
            obs[lname].setdefault((i, j), []).append(shape)


for f in b.GetFootprints():
    for p in f.Pads():
        bb = p.GetBoundingBox()
        for ln in LALL:
            if p.GetNetCode() != code and p.IsOnLayer(LID[ln]):
                add(ln, p.GetEffectiveShape(LID[ln]), bb)
            if p.HasHole() and p.GetNetCode() != code:
                add(ln, p.GetEffectiveHoleShape(), bb)
for t in b.GetTracks():
    if t.GetNetCode() == code:
        continue
    bb = t.GetBoundingBox()
    if t.GetClass() == "PCB_VIA":
        for ln in LALL:
            add(ln, t.GetEffectiveShape(LID[ln]), bb)
    else:
        for ln in LALL:
            if t.GetLayer() == LID[ln]:
                add(ln, t.GetEffectiveShape(), bb)
# keepout rule areas (board level and inside footprints, e.g. under the SHT40 U25) block
# tracks on their layers (and vias, which are checked on every layer)
_rules = [z for z in b.Zones() if z.GetIsRuleArea()]
for f in b.GetFootprints():
    _rules += [z for z in f.Zones() if z.GetIsRuleArea()]
for z in _rules:
    if not (z.GetDoNotAllowTracks() or z.GetDoNotAllowVias()):
        continue
    for ln in LALL:
        if z.IsOnLayer(LID[ln]):
            poly = z.Outline()
            add(ln, poly, z.GetBoundingBox())
# In1 is the GND plane: vias only need to clear its holes, which pads/vias above cover
# no via in any SMD pad, own net included (fab rule: no via-in-pad)
pads_all = {}
for f in b.GetFootprints():
    for p in f.Pads():
        if p.IsOnLayer(pcbnew.F_Cu) and not p.HasHole():
            bb = p.GetBoundingBox()
            if bb.Intersects(box):
                for i in range(int((T(bb.GetLeft()) - x0 - 1) // BUCK), int((T(bb.GetRight()) - x0 + 1) // BUCK) + 1):
                    for j in range(int((T(bb.GetTop()) - y0 - 1) // BUCK), int((T(bb.GetBottom()) - y0 + 1) // BUCK) + 1):
                        pads_all.setdefault((i, j), []).append(p.GetEffectiveShape(pcbnew.F_Cu))


def free(ln, i, j, r):
    x, y = x0 + i * step, y0 + j * step
    c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(MM(x), MM(y)), MM(r))
    for s in obs[ln].get((int((x - x0) // BUCK), int((y - y0) // BUCK)), ()):
        if s.Collide(c, CLR):
            return False
    return True


cache = {}


def ok(ln, i, j):
    k = (ln, i, j)
    if k not in cache:
        cache[k] = 0 <= i < nx and 0 <= j < ny and free(ln, i, j, w / 2)
    return cache[k]


vcache = {}


def via_ok(i, j):
    if (i, j) not in vcache:
        x, y = x0 + i * step, y0 + j * step
        c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(MM(x), MM(y)), MM(VIA_R))
        in_pad = any(s.Collide(c, MM(0.05)) for s in
                     pads_all.get((int((x - x0) // BUCK), int((y - y0) // BUCK)), ()))
        vcache[(i, j)] = not in_pad and all(free(ln, i, j, VIA_R) for ln in LALL)
    return vcache[(i, j)]


def cell(x, y):
    return int(round((float(x) - x0) / step)), int(round((float(y) - y0) / step))


S = (LN.index(sl),) + cell(sx, sy)
G = (LN.index(tl),) + cell(tx, ty)
D8 = [(1, 0, 1), (-1, 0, 1), (0, 1, 1), (0, -1, 1), (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
dist = {S: 0.0}
prev = {}
pq = [(0.0, 0.0, S)]
found = False
while pq:
    _, d, u = heapq.heappop(pq)
    if u == G:
        found = True
        break
    if d > dist.get(u, 1e18):
        continue
    l, i, j = u
    nbrs = []
    for di, dj, c in D8:
        v = (l, i + di, j + dj)
        if v == G or ok(LN[l], i + di, j + dj):
            nbrs.append((v, c))
    if via_ok(i, j):
        for l2 in range(len(LN)):
            if l2 != l:
                nbrs.append(((l2, i, j), via_cost))
    for v, c in nbrs:
        nd = d + c
        if nd < dist.get(v, 1e18):
            dist[v] = nd
            prev[v] = u
            h = abs(v[1] - G[1]) + abs(v[2] - G[2])
            heapq.heappush(pq, (nd + h * 0.9, nd, v))
if not found:
    print("no path; explored", len(dist), "start", S, "goal", G,
          "start nbrs free", [ok(LN[S[0]], S[1] + a, S[2] + c) for a, c, _ in D8],
          "goal nbrs free", [ok(LN[G[0]], G[1] + a, G[2] + c) for a, c, _ in D8])
    sys.exit(1)
path = [G]
while path[-1] != S:
    path.append(prev[path[-1]])
path.reverse()
# keep layer changes and direction changes only
pts = []
for k, (l, i, j) in enumerate(path):
    if 0 < k < len(path) - 1:
        pl, pi, pj = path[k - 1]
        nl, ni, nj = path[k + 1]
        if pl == l == nl and (i - pi, j - pj) == (ni - i, nj - j):
            continue
    pts.append(f"{x0 + i * step:.3f},{y0 + j * step:.3f},{LN[l]}")
pts[0] = f"{sx},{sy},{sl}"
pts[-1] = f"{tx},{ty},{tl}"
print(" ".join(pts))
