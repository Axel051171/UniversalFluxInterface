"""Batch A* router for the connections a DRC report lists as unconnected.

Reads the "unconnected_items" pairs of a kicad-cli DRC JSON report, resolves the two items on
the board (pad, track end, via), routes each pair on F.Cu / In2.Cu / B.Cu with 0.6/0.3 mm vias
(same model as maze_route.py: other-net copper is an obstacle, no via in any SMD pad, hole to
hole 0.25 mm) and adds the copper to the board.  GND pads that only need a via to the In1
plane are routed in "via mode": the goal is the nearest spot where a via fits.

usage: batch_route.py <board> <drc.json> [--apply] [--only NET,NET] [--skip NET,NET]
                      [--gnd REF.PAD,...] [--margin MM] [--step MM] [--via-cost MM] [--order NET,..]
Prints one line per connection; without --apply nothing is written.  Run the DRC again
afterwards: zones are not refilled here.  Every grid step is checked as a segment (not only
its end points), found paths are string-pulled per layer run, a via costs --via-cost mm.
"""
import heapq
import json
import os
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
LN = ["F", "I2", "B"]
LID = {"F": pcbnew.F_Cu, "I2": pcbnew.In2_Cu, "B": pcbnew.B_Cu}
VIA_D, VIA_DR = 0.6, 0.3
CLR = float(os.environ.get("ROUTE_CLR", "0.17"))     # track to other copper (rule 0.15)
VCLR = 0.25                                           # via hole to any other hole (rule 0.25)
EDGE = 0.6                                            # keep away from the outline (rule 0.3)
BUCK = 2.0


def arg(name, default=None):
    if name in sys.argv:
        return sys.argv[sys.argv.index(name) + 1]
    return default


board = pcbnew.LoadBoard(sys.argv[1])
report = json.load(open(sys.argv[2], encoding="utf-8"))
apply = "--apply" in sys.argv
only = set(arg("--only", "").split(",")) - {""}
skip = set(arg("--skip", "").split(",")) - {""}
gnd_pads = set(arg("--gnd", "").split(",")) - {""}
margin = float(arg("--margin", "6"))
step = float(arg("--step", "0.25"))
via_cost = float(arg("--via-cost", "4"))   # mm of track a via is worth (grid independent)
order = [n for n in arg("--order", "").split(",") if n]   # nets routed first, in this order
unfence = float(arg("--unfence", "0"))   # mm: rip unlocked other-net copper this close to a blocked endpoint
ripped = {}                              # net -> count of removed items
bb = board.GetBoardEdgesBoundingBox()
BX0, BY0, BX1, BY1 = T(bb.GetLeft()) + EDGE, T(bb.GetTop()) + EDGE, T(bb.GetRight()) - EDGE, T(bb.GetBottom()) - EDGE
fps = {f.GetReference(): f for f in board.GetFootprints()}


# ---------------------------------------------------------------- endpoints
def pad_layers(p):
    if p.HasHole():
        return list(LN)
    return ["F"] if p.IsOnLayer(pcbnew.F_Cu) else ["B"]


def covered(net, pt, me):
    """Is point pt (VECTOR2I) touched by same-net copper other than item me?"""
    for t in board.GetTracks():
        if t.GetNetCode() != net or t.m_Uuid == me.m_Uuid:
            continue
        if t.GetClass() == "PCB_VIA":
            if (t.GetPosition() - pt).EuclideanNorm() < MM(0.05):
                return True
        elif (t.GetStart() - pt).EuclideanNorm() < MM(0.01) or (t.GetEnd() - pt).EuclideanNorm() < MM(0.01):
            return True
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetCode() == net and p.HitTest(pt):
                return True
    return False


def endpoint(item, other_pos):
    """-> (x, y, [layers], label) or None for zones."""
    cls = item.GetClass()
    if cls == "PAD":
        pos = item.GetPosition()
        lbl = "%s.%s" % (item.GetParentFootprint().GetReference(), item.GetNumber())
        if not item.HasHole():  # SMD pad with a fan-out stub: start at its via, that opens every layer
            vias = [t.GetPosition() for t in board.GetTracks()
                    if t.GetClass() == "PCB_VIA" and t.GetNetCode() == item.GetNetCode()]
            for t in board.GetTracks():
                if t.GetClass() != "PCB_TRACK" or t.GetNetCode() != item.GetNetCode():
                    continue
                for a, b in ((t.GetStart(), t.GetEnd()), (t.GetEnd(), t.GetStart())):
                    if item.HitTest(a) and any((v - b).EuclideanNorm() < MM(0.05) for v in vias):
                        return T(b.x), T(b.y), list(LN), lbl + " fan-out via"
        return T(pos.x), T(pos.y), pad_layers(item), lbl
    if cls == "PCB_VIA":
        pos = item.GetPosition()
        return T(pos.x), T(pos.y), list(LN), "via"
    if cls == "PCB_TRACK":
        lay = board.GetLayerName(item.GetLayer())
        ln = {"F.Cu": "F", "In2.Cu": "I2", "B.Cu": "B"}.get(lay)
        if ln is None:
            return None
        ends = [item.GetStart(), item.GetEnd()]
        free = [e for e in ends if not covered(item.GetNetCode(), e, item)]
        if len(free) != 1:  # both ends covered (pad + via): start at the via, it opens every layer
            vias = [e for e in ends if any(t.GetClass() == "PCB_VIA" and t.GetNetCode() == item.GetNetCode()
                                           and (t.GetPosition() - e).EuclideanNorm() < MM(0.05) for t in board.GetTracks())]
            free = vias[:1] or sorted(ends, key=lambda e: (e - other_pos).EuclideanNorm())[:1]
        e = free[0]
        for t in board.GetTracks():  # a same-net via at the end opens every layer
            if t.GetClass() == "PCB_VIA" and t.GetNetCode() == item.GetNetCode() \
                    and (t.GetPosition() - e).EuclideanNorm() < MM(0.05):
                return T(e.x), T(e.y), list(LN), "track end %s (via)" % lay
        return T(e.x), T(e.y), [ln], "track end %s" % lay
    return None


def nearest_copper(item):
    """Nearest same-net pad/track/via to item (not item itself), within 40 mm."""
    net, pos = item.GetNetCode(), item.GetPosition()
    best, bd = None, MM(40)
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.GetNetCode() == net and p.m_Uuid != item.m_Uuid:
                d = (p.GetPosition() - pos).EuclideanNorm()
                if d < bd:
                    best, bd = p, d
    for t in board.GetTracks():
        if t.GetNetCode() == net and t.m_Uuid != item.m_Uuid:
            d = (t.GetPosition() - pos).EuclideanNorm()
            if d < bd:
                best, bd = t, d
    return best


# ---------------------------------------------------------------- router
class Router:
    def __init__(self, net, w, x0, y0, x1, y1, step, anchor):
        self.net, self.w, self.step = net, w, step
        # grid aligned to the start point: a 0.3 mm QFP pad leaves no room for a snapping error
        ax, ay = anchor
        x0 = ax - int((ax - x0) / step) * step
        y0 = ay - int((ay - y0) / step) * step
        self.x0, self.y0, self.x1, self.y1 = x0, y0, x1, y1
        self.nx, self.ny = int((x1 - x0) / step) + 1, int((y1 - y0) / step) + 1
        self.box = pcbnew.BOX2I(pcbnew.VECTOR2I(MM(x0 - 2), MM(y0 - 2)), pcbnew.VECTOR2I(MM(x1 - x0 + 4), MM(y1 - y0 + 4)))
        self.obs = {l: {} for l in LN}   # track obstacles per layer
        self.hobs = {}                   # hole/via obstacles for new vias (any net)
        self.pads = {}                   # SMD pads (any net, any side): no via inside
        for f in board.GetFootprints():
            for p in f.Pads():
                pbb = p.GetBoundingBox()
                if not pbb.Intersects(self.box):
                    continue
                lbl = "%s.%s[%s]" % (f.GetReference(), p.GetNumber(), p.GetNetname())
                if p.GetNetCode() != net:
                    for ln in LN:
                        if p.IsOnLayer(LID[ln]):
                            self.add(self.obs[ln], p.GetEffectiveShape(LID[ln]), pbb, lbl)
                    if p.HasHole():
                        for ln in LN:
                            self.add(self.obs[ln], p.GetEffectiveHoleShape(), pbb, lbl + " hole")
                if p.HasHole():
                    self.add(self.hobs, p.GetEffectiveHoleShape(), pbb, lbl + " hole")
                else:
                    self.add(self.pads, p.GetEffectiveShape(p.GetLayer()), pbb, lbl)
        for t in board.GetTracks():
            tbb = t.GetBoundingBox()
            if not tbb.Intersects(self.box):
                continue
            lbl = "%s[%s]" % (t.GetClass(), t.GetNetname())
            if t.GetClass() == "PCB_VIA":
                self.add(self.hobs, pcbnew.SHAPE_SEGMENT(t.GetPosition(), t.GetPosition(), t.GetDrill()), tbb, lbl)
                if t.GetNetCode() != net:
                    for ln in LN:
                        self.add(self.obs[ln], t.GetEffectiveShape(LID[ln]), tbb, lbl)
            elif t.GetNetCode() != net:
                for ln in LN:
                    if t.GetLayer() == LID[ln]:
                        self.add(self.obs[ln], t.GetEffectiveShape(), tbb, lbl)
        self.cache, self.vcache, self.scache = {}, {}, {}

    def add(self, store, shape, sbb, label=""):
        bx0, by0 = int((T(sbb.GetLeft()) - self.x0 - 1) // BUCK), int((T(sbb.GetTop()) - self.y0 - 1) // BUCK)
        bx1, by1 = int((T(sbb.GetRight()) - self.x0 + 1) // BUCK), int((T(sbb.GetBottom()) - self.y0 + 1) // BUCK)
        for i in range(bx0, bx1 + 1):
            for j in range(by0, by1 + 1):
                store.setdefault((i, j), []).append((shape, label))

    def bucket(self, x, y):
        return int((x - self.x0) // BUCK), int((y - self.y0) // BUCK)

    def free(self, store, x, y, r, clr):
        c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(MM(x), MM(y)), MM(r))
        for s, lbl in store.get(self.bucket(x, y), ()):
            if s.Collide(c, MM(clr)):
                self.last_block = lbl
                return False
        return True

    def why(self, ln, x, y):
        """Blocker report: along each axis from (x, y) on layer ln, how far is free and what blocks."""
        out = []
        for dx, dy, name in ((1, 0, "+x"), (-1, 0, "-x"), (0, 1, "+y"), (0, -1, "-y")):
            k = 0
            while k < 40:
                k += 1
                px, py = x + dx * k * self.step, y + dy * k * self.step
                self.last_block = "edge/box"
                if not (self.free(self.obs[ln], px, py, self.w / 2, CLR) and BX0 <= px <= BX1 and BY0 <= py <= BY1):
                    out.append("%s %.1fmm %s" % (name, (k - 1) * self.step, self.last_block))
                    break
            else:
                out.append("%s >%.1fmm free" % (name, 40 * self.step))
        return "; ".join(out)

    def ok(self, ln, i, j):
        k = (ln, i, j)
        if k not in self.cache:
            x, y = self.x0 + i * self.step, self.y0 + j * self.step
            self.cache[k] = (0 <= i < self.nx and 0 <= j < self.ny
                             and BX0 <= x <= BX1 and BY0 <= y <= BY1
                             and self.free(self.obs[ln], x, y, self.w / 2, CLR))
        return self.cache[k]

    def seg_ok(self, ln, i, j, di, dj):
        """The whole step from cell (i, j) to (i+di, j+dj) keeps CLR: testing only the grid points lets a
        segment pass up to step/2 closer to an obstacle than either of its ends."""
        k = (ln, i, j, di, dj)
        if k not in self.scache:
            a = pcbnew.VECTOR2I(MM(self.x0 + i * self.step), MM(self.y0 + j * self.step))
            b = pcbnew.VECTOR2I(MM(self.x0 + (i + di) * self.step), MM(self.y0 + (j + dj) * self.step))
            seg = pcbnew.SHAPE_SEGMENT(a, b, MM(self.w))
            mx, my = self.x0 + (i + di / 2.0) * self.step, self.y0 + (j + dj / 2.0) * self.step
            good = True
            for s, lbl in self.obs[ln].get(self.bucket(mx, my), ()):
                if s.Collide(seg, MM(CLR)):
                    self.last_block = lbl
                    good = False
                    break
            self.scache[k] = good
        return self.scache[k]

    def via_ok(self, i, j):
        if (i, j) not in self.vcache:
            x, y = self.x0 + i * self.step, self.y0 + j * self.step
            good = (0 <= i < self.nx and 0 <= j < self.ny and BX0 + 0.3 <= x <= BX1 - 0.3 and BY0 + 0.3 <= y <= BY1 - 0.3
                    and self.free(self.pads, x, y, VIA_D / 2, 0.05)
                    and self.free(self.hobs, x, y, VIA_DR / 2, VCLR)
                    and all(self.free(self.obs[ln], x, y, VIA_D / 2, CLR) for ln in LN))
            self.vcache[(i, j)] = good
        return self.vcache[(i, j)]

    def cell(self, x, y):
        return int(round((x - self.x0) / self.step)), int(round((y - self.y0) / self.step))

    def route(self, start, goals, via_goal=False):
        """start = (x, y, layers); goals = list of (x, y, layers).  Returns point list [(x,y,L)]."""
        S = [(LN.index(l),) + self.cell(start[0], start[1]) for l in start[2]]
        G = set()
        for gx, gy, gl in goals:
            for l in gl:
                G.add((LN.index(l),) + self.cell(gx, gy))
        D8 = [(1, 0, 1), (-1, 0, 1), (0, 1, 1), (0, -1, 1), (1, 1, 1.414), (1, -1, 1.414), (-1, 1, 1.414), (-1, -1, 1.414)]
        gi = [g[1] for g in G] or [S[0][1]]
        gj = [g[2] for g in G] or [S[0][2]]
        gcx, gcy = sum(gi) / len(gi), sum(gj) / len(gj)
        dist = {s: 0.0 for s in S}
        prev = {}
        pq = [(0.0, 0.0, s) for s in S]
        heapq.heapify(pq)
        end = None
        while pq:
            _, d, u = heapq.heappop(pq)
            if d > dist.get(u, 1e18):
                continue
            l, i, j = u
            if via_goal:
                if d >= 2 and self.via_ok(i, j):
                    end = u
                    break
            elif u in G:
                end = u
                break
            for di, dj, c in D8:
                v = (l, i + di, j + dj)
                if (v in G or self.ok(LN[l], i + di, j + dj)) and self.seg_ok(LN[l], i, j, di, dj):
                    nd = d + c
                    if nd < dist.get(v, 1e18):
                        dist[v] = nd
                        prev[v] = u
                        h = 0.0 if via_goal else (abs(v[1] - gcx) + abs(v[2] - gcy)) * 0.9
                        heapq.heappush(pq, (nd + h, nd, v))
            if not via_goal and self.via_ok(i, j):
                for l2 in range(3):
                    v = (l2, i, j)
                    nd = d + via_cost / self.step   # via cost is given in mm of detour, independent of the grid
                    if l2 != l and nd < dist.get(v, 1e18):
                        dist[v] = nd
                        prev[v] = u
                        heapq.heappush(pq, (nd + (abs(i - gcx) + abs(j - gcy)) * 0.9, nd, v))
        if end is None:
            return None, len(dist)
        path = [end]
        while path[-1] in prev:
            path.append(prev[path[-1]])
        path.reverse()
        pts = []
        for k, (l, i, j) in enumerate(path):
            if 0 < k < len(path) - 1:
                pl, pi, pj = path[k - 1]
                nl, ni, nj = path[k + 1]
                if pl == l == nl and (i - pi, j - pj) == (ni - i, nj - j):
                    continue
            pts.append((self.x0 + i * self.step, self.y0 + j * self.step, LN[l]))
        return pts, len(dist)   # add_copper() string-pulls after the exact ends are in place

    def seg_free(self, ln, a, b):
        """Straight segment a->b (mm) of width w on layer ln keeps CLR to every obstacle."""
        seg = pcbnew.SHAPE_SEGMENT(pcbnew.VECTOR2I(MM(a[0]), MM(a[1])), pcbnew.VECTOR2I(MM(b[0]), MM(b[1])), MM(self.w))
        bx0, by0 = self.bucket(min(a[0], b[0]), min(a[1], b[1]))
        bx1, by1 = self.bucket(max(a[0], b[0]), max(a[1], b[1]))
        seen = set()
        for i in range(bx0, bx1 + 1):
            for j in range(by0, by1 + 1):
                for s, lbl in self.obs[ln].get((i, j), ()):
                    if id(s) in seen:
                        continue
                    seen.add(id(s))
                    if s.Collide(seg, MM(CLR)):
                        return False
        return True

    def smooth(self, pts):
        """String-pulling per layer run: drop every grid point that a straight free segment can skip
        (the grid path is a staircase of 0.1/0.14 mm jogs along obstacles); via points stay."""
        out, k = [], 0
        while k < len(pts):
            j = k + 1
            while j < len(pts) and pts[j][2] == pts[k][2]:
                j += 1
            run = pts[k:j]
            keep, i = [run[0]], 0
            while i < len(run) - 1:
                m = len(run) - 1
                while m > i + 1 and not self.seg_free(run[i][2], run[i], run[m]):
                    m -= 1
                keep.append(run[m])
                i = m
            out.extend(keep)
            k = j
        return out


def add_copper(net, w, pts, start, goal, via_end=False, router=None):
    """Add tracks/vias for a point list; exact start/goal coordinates replace the snapped ends.
    If the moved end segment would then touch something, the snapped point is kept as a knee;
    afterwards the path is string-pulled (router.smooth) with the real end points."""
    pts = list(pts)
    snapped0, snapped1 = pts[0], pts[-1]
    pts[0] = (start[0], start[1], pts[0][2])
    if goal is not None:
        pts[-1] = (goal[0], goal[1], pts[-1][2])
    if router is not None:
        if len(pts) > 1 and pts[0][2] == pts[1][2] and not router.seg_free(pts[0][2], pts[0], pts[1]):
            pts.insert(1, snapped0)
        if goal is not None and len(pts) > 1 and pts[-1][2] == pts[-2][2] \
                and not router.seg_free(pts[-1][2], pts[-2], pts[-1]):
            pts.insert(len(pts) - 1, snapped1)
        pts = router.smooth(pts)
    n = 0
    for (ax, ay, al), (bx, by, bl) in zip(pts, pts[1:]):
        if al != bl:
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(pcbnew.VECTOR2I(MM(ax), MM(ay)))
            v.SetWidth(MM(VIA_D))
            v.SetDrill(MM(VIA_DR))
            v.SetNet(net)
            board.Add(v)
            n += 1
            if abs(ax - bx) < 1e-6 and abs(ay - by) < 1e-6:
                continue
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(pcbnew.VECTOR2I(MM(ax), MM(ay)))
        t.SetEnd(pcbnew.VECTOR2I(MM(bx), MM(by)))
        t.SetWidth(MM(w))
        t.SetLayer(LID[bl] if al != bl else LID[al])
        t.SetNet(net)
        board.Add(t)
        n += 1
    if via_end:
        ax, ay, _ = pts[-1]
        v = pcbnew.PCB_VIA(board)
        v.SetPosition(pcbnew.VECTOR2I(MM(ax), MM(ay)))
        v.SetWidth(MM(VIA_D))
        v.SetDrill(MM(VIA_DR))
        v.SetNet(net)
        board.Add(v)
        n += 1
    return n


def width_for(net):
    if os.environ.get("ROUTE_W"):   # e.g. 0.2 for GND stitches of a Supply-class net in a tight spot
        return float(os.environ["ROUTE_W"])
    """Track width of the net's class (Power 0.25, Supply 0.3 on this board)."""
    w = 0.0
    try:
        nc = board.GetDesignSettings().m_NetSettings.GetEffectiveNetClass(net.GetNetname())
        w = T(nc.GetTrackWidth())
    except Exception:  # noqa: BLE001
        try:
            w = T(net.GetNetClass().GetTrackWidth())
        except Exception:  # noqa: BLE001
            w = 0.0
    return max(0.2, w)


def rip_around(net, x, y, r):
    """Remove unlocked tracks/vias of other nets within r mm of (x, y).  Returns the number removed."""
    c = pcbnew.SHAPE_CIRCLE(pcbnew.VECTOR2I(MM(x), MM(y)), MM(r))
    n = 0
    for t in list(board.GetTracks()):
        if t.IsLocked() or t.GetNetCode() == net.GetNetCode():
            continue
        shape = t.GetEffectiveShape(pcbnew.F_Cu) if t.GetClass() == "PCB_VIA" else t.GetEffectiveShape()
        if shape.Collide(c, 0):
            ripped[t.GetNetname()] = ripped.get(t.GetNetname(), 0) + 1
            board.Remove(t)
            n += 1
    return n


def run(netname, start, goals, via_goal, label):
    net = board.FindNet(netname)
    w = width_for(net)
    xs = [start[0]] + [g[0] for g in goals]
    ys = [start[1]] + [g[1] for g in goals]
    for m, st in ((margin, step), (margin * 2.5, step), (margin, step / 2.5), (margin * 2.5, step / 2.5)):
        x0, y0 = max(BX0, min(xs) - m), max(BY0, min(ys) - m)
        x1, y1 = min(BX1, max(xs) + m), min(BY1, max(ys) + m)
        r = Router(net.GetNetCode(), w, x0, y0, x1, y1, st, (start[0], start[1]))
        pts, explored = r.route(start, goals, via_goal)
        if pts:
            n = add_copper(net, w, pts, start, None if via_goal else goals[0], via_end=via_goal, router=r)
            vias = sum(1 for a, b in zip(pts, pts[1:]) if a[2] != b[2]) + (1 if via_goal else 0)
            print("OK   %-22s %-34s w%.2f %d pts %d vias (box %.0fx%.0f step %.2f, %d cells)" % (
                netname, label, w, len(pts), vias, x1 - x0, y1 - y0, st, explored))
            return True
    print("FAIL %-22s %-34s w%.2f explored %d" % (netname, label, w, explored))
    print("     start %s: %s" % (start[2][0], r.why(start[2][0], start[0], start[1])))
    for g in goals:
        print("     goal  %s: %s" % (g[2][0], r.why(g[2][0], g[0], g[1])))
    if unfence > 0 and not getattr(run, "retrying", False):
        n = rip_around(net, start[0], start[1], unfence)
        for g in goals:
            n += rip_around(net, g[0], g[1], unfence)
        if n:
            print("     unfenced: removed %d other-net items, retrying" % n)
            run.retrying = True
            ok = run(netname, start, goals, via_goal, label)
            run.retrying = False
            return ok
    return False


def plane_anchor(pad):
    """Nearest same-net via or through-hole pad (both reach the In1 plane) within 15 mm."""
    net, pos = pad.GetNetCode(), pad.GetPosition()
    best, bd = None, MM(15)
    for t in board.GetTracks():
        if t.GetClass() == "PCB_VIA" and t.GetNetCode() == net:
            d = (t.GetPosition() - pos).EuclideanNorm()
            if d < bd:
                best, bd = t, d
    for f in board.GetFootprints():
        for p in f.Pads():
            if p.HasHole() and p.GetNetCode() == net and p.m_Uuid != pad.m_Uuid:
                d = (p.GetPosition() - pos).EuclideanNorm()
                if d < bd:
                    best, bd = p, d
    return best


# ---------------------------------------------------------------- main
uuid_map = {}
for f in board.GetFootprints():
    for p in f.Pads():
        uuid_map[p.m_Uuid.AsString()] = p
for t in board.GetTracks():
    uuid_map[t.m_Uuid.AsString()] = t
for z in board.Zones():
    uuid_map[z.m_Uuid.AsString()] = z
done, failed = 0, 0


def net_of(u):
    for it in u.get("items", []):
        o = uuid_map.get(it["uuid"])
        if o is not None and hasattr(o, "GetNetname"):
            return o.GetNetname()
    return ""


def rank(u):
    n = net_of(u)
    return order.index(n) if n in order else len(order)


for u in sorted(report.get("unconnected_items", []), key=rank):
    items = u.get("items", [])
    if len(items) != 2:
        continue
    objs = [uuid_map.get(it["uuid"]) for it in items]
    if any(o is None for o in objs):
        print("skip: item not found", [it["description"] for it in items])
        continue
    netname = None
    for o in objs:
        if hasattr(o, "GetNetname"):
            netname = o.GetNetname()
            break
    if netname is None or (only and netname not in only) or netname in skip:
        continue
    if all(o.GetClass() == "ZONE" for o in objs):
        continue  # GND pour islands: finish_pcb.py island vias / --gnd pads
    if any(o.GetClass() == "ZONE" for o in objs):
        objs.sort(key=lambda o: o.GetClass() == "ZONE")
        objs[1] = nearest_copper(objs[0])
        if objs[1] is None:
            print("skip: no same-net copper near", netname, items[0]["description"])
            continue
    a = endpoint(objs[0], objs[1].GetPosition())
    b = endpoint(objs[1], objs[0].GetPosition())
    if a is None or b is None:
        print("skip: unsupported item", netname, [it["description"] for it in items])
        continue
    # route from the item with fewer layer choices (SMD pad) towards the other one
    if len(a[2]) > len(b[2]):
        a, b = b, a
    label = "%s -> %s" % (a[3], b[3])
    if run(netname, (a[0], a[1], a[2]), [(b[0], b[1], b[2])], False, label):
        done += 1
    else:
        failed += 1
for spec in sorted(gnd_pads):
    ref, num = spec.split(".")
    pad = next(p for p in fps[ref].Pads() if p.GetNumber() == num)
    pos = pad.GetPosition()
    if run(pad.GetNetname(), (T(pos.x), T(pos.y), pad_layers(pad)), [], True, "%s via to plane" % spec):
        done += 1
        continue
    anchor = plane_anchor(pad)
    if anchor is not None:
        g = endpoint(anchor, pos)
        if run(pad.GetNetname(), (T(pos.x), T(pos.y), pad_layers(pad)), [(g[0], g[1], g[2])], False,
               "%s -> %s" % (spec, g[3])):
            done += 1
            continue
    failed += 1
print("routed", done, "failed", failed)
if ripped:
    print("ripped for unfencing:", ", ".join("%s=%d" % kv for kv in sorted(ripped.items())))
if apply and (done or ripped):
    board.Save(sys.argv[1])
    print("saved", sys.argv[1])
