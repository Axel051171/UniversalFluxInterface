"""Stage B2 placement of board v1.0: fixed connector positions plus a nearest-free-spot search
for the parts that stage B1 left overlapping or far from their partners.

FIXED parts go exactly where stated.  TARGET parts are moved, in order, to the free spot
closest to their target (courtyard clear of every other courtyard by MARGIN, inside the
outline by EDGE, not on locked copper; among the free spots the one covering the fewest
tracks wins).  Unlocked copper under every moved part is ripped up; the USB-C fan-out at J1
is deleted and regenerated at the new pad positions (J1 is rotated 180 deg so the
receptacle opening faces the board edge - see the HRO TYPE-C-31-M-12 drawing: SMD tails at
the rear, PCB edge 5.79 mm in front of the rear shell legs).
usage: python place_v10.py <board>
"""
import math
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
MARGIN = 0.15       # courtyard-to-courtyard clearance (mm); the front column needs it this tight
EDGE = 0.6          # courtyard-to-outline clearance (mm)
SEARCH = 7.0        # search radius around a target (mm)
STEP = 0.25

# ref: (x, y, rot[, "B"])   "B" = footprint goes to the back side
FIXED = {
    "J1": (72.0, 2.66, 180.0),     # opening 0.99 mm beyond the edge, pad row at y 6.7
    "J22": (32.96, 13.11, 180.0),  # RJ45 front 3.05 mm beyond the edge (HanRun "D 3.05 min"), 0.2 mm right of J2
    "J21": (88.6, 23.5, 0.0),      # LA header between F1/SW1 and the front column
    "SW2": (82.3, 19.4, 0.0),      # BOOT button left of J21
    # W5500 rotated: SPI/XI/XO/VDD pins at the bottom towards U5, TX/RX at the top, PMODE/RST right,
    # 3V3A/TOCAP/V1V2 left.  Rotation 0 boxed the SPI pins in between the crystal, U15 and J16.
    "U23": (44.0, 12.3, 270.0),
    "Y2": (44.0, 19.5, 0.0),       # crystal right under XI/XO (pins 30/31 at x 43.75/44.25)
    # DNP tactile switch (buttons are on the front panel): parked on the back side under the RJ45
    # body, the only hole-free area; it must not sit in the J6/J7 routing channel
    "SW3": (28.5, 7.5, 0.0, "B"),
}

# (ref, x, y, [rotations]) - processed in this order
TARGETS = [
    # --- LAN front end at J22: centre-tap networks stay next to the J22 pin rows
    ("R114", 31.3, 19.0, [0]), ("C71", 28.4, 19.0, [0]), ("C74", 24.7, 21.2, [0]),
    ("C31", 33.4, 25.5, [0]),
    # --- three rows above U23 (TX/RX pins at the top; U23 courtyard starts at y 7.1)
    ("C73", 40.0, 4.95, [0]), ("C72", 43.3, 4.95, [0]), ("R112", 46.6, 4.95, [0]), ("R113", 49.9, 4.95, [0]),
    ("R111", 40.0, 3.2, [0]), ("R116", 43.3, 3.2, [0]), ("R115", 46.6, 3.2, [0]), ("C64", 49.9, 3.2, [0]),
    ("R117", 40.0, 1.45, [0]), ("R118", 43.3, 1.45, [0]), ("C66", 46.6, 1.45, [0]), ("C67", 49.9, 1.45, [0]),
    # --- column below J22 (x 37.6): TOCAP and V1V2 caps for the left-side pins
    ("C62", 37.65, 19.4, [90]), ("C61", 37.6, 22.8, [90]),
    # --- right of U23 (PMODE/RST pins at x 48.2): pull-ups; below them INT/CS pull-ups, VDD bulk, 1 M
    ("R122", 51.0, 8.8, [0]), ("R123", 51.0, 10.6, [0]), ("R124", 51.0, 12.4, [0]), ("R121", 51.0, 14.2, [0]),
    ("R120", 50.2, 19.6, [90]), ("R119", 51.9, 19.3, [90]), ("C69", 50.2, 23.0, [90]), ("R109", 51.9, 22.6, [90]),
    # --- below U23 (SPI/XI/XO/VDD pins at y 16.5): crystal caps, 3V3A cap, VDD cap, bulk + ferrite
    ("C68", 40.3, 19.6, [90]), ("C60", 47.8, 20.2, [90]),
    ("C65", 40.5, 22.9, [0]), ("C59", 44.0, 22.8, [0]), ("C70", 47.8, 22.8, [0]),
    ("C63", 39.9, 25.6, [0]), ("FB2", 43.6, 25.6, [0]),
    # --- parts displaced from the old top area
    ("C37", 81.5, 10.3, [0]), ("R8", 79.0, 14.5, [0]), ("R11", 99.5, 10.8, [0]),
    ("R19", 73.0, 44.0, [0]), ("R20", 73.0, 46.5, [0]),
    ("TP10", 75.5, 44.0, [0]), ("TP11", 75.5, 46.5, [0]), ("TP12", 75.5, 49.0, [0]), ("TP13", 75.5, 51.5, [0]),
    # --- front column next to J12: series resistors (x 94.7) and ESD diodes (x 98.19), rows 2.3 mm apart (SOD-323 courtyard 3.28 x 1.98)
    ("R105", 94.7, 27.4, [0]),
    ("R35", 94.7, 29.7, [0]), ("D22", 98.19, 29.7, [0]),
    ("R37", 94.7, 32.0, [0]), ("D23", 98.19, 32.0, [0]),
    ("R100", 94.7, 34.3, [0]), ("D24", 98.19, 34.3, [0]),
    ("R36", 94.7, 36.6, [0]), ("D25", 98.19, 36.6, [0]),
    ("R34", 94.7, 38.9, [0]), ("D26", 98.19, 38.9, [0]),
    ("R101", 94.7, 41.2, [0]), ("D18", 98.19, 41.2, [0]),
    ("R102", 94.7, 43.5, [0]), ("D19", 98.19, 43.5, [0]),
    ("R103", 94.7, 45.8, [0]), ("D20", 98.19, 45.8, [0]),
    ("R104", 94.7, 48.1, [0]), ("D21", 98.19, 48.1, [0]),
    # --- loopback block above J6/J7
    ("U22", 58.0, 70.6, [0]), ("Q5", 62.0, 70.6, [0]), ("C58", 55.3, 70.3, [90]),
    ("R106", 61.8, 73.6, [0]), ("JP4", 55.0, 73.5, [0]),   # keep the J6/J7 routing channel clear
    # --- 5 V source selector group and test points
    ("TP8", 36.5, 57.5, [0]), ("TP9", 28.5, 58.2, [0]),
    ("U19", 29.0, 61.0, [0]), ("R74", 25.6, 60.3, [0]), ("C57", 25.6, 62.6, [0]),
    ("R72", 32.3, 60.3, [0]), ("R73", 32.3, 58.2, [0]), ("R84", 23.0, 64.5, [0]),
    # --- remaining overlaps
    ("D2", 11.0, 24.8, [0]), ("D15", 38.5, 89.2, [0]),
    ("D14", 90.0, 71.5, [0]), ("D10", 95.5, 71.0, [90]), ("D9", 84.2, 72.4, [0]),
]

path = sys.argv[1]
board = pcbnew.LoadBoard(path)
fps = {fp.GetReference(): fp for fp in board.GetFootprints()}
V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731

eb = board.GetBoardEdgesBoundingBox()
bx0, by0 = T(eb.GetX()) + EDGE, T(eb.GetY()) + EDGE
bx1, by1 = T(eb.GetRight()) - EDGE, T(eb.GetBottom()) - EDGE


def rect_of(fp):
    layer = pcbnew.F_CrtYd if fp.GetLayer() == pcbnew.F_Cu else pcbnew.B_CrtYd
    fp.BuildCourtyardCaches()   # GetCourtyard() is cached and does not follow a move
    cy = fp.GetCourtyard(layer)
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
    for k in range(21):
        x, y = x0 + (x1 - x0) * k / 20, y0 + (y1 - y0) * k / 20
        if r[0] - w <= x <= r[2] + w and r[1] - w <= y <= r[3] + w:
            return True
    return False


# obsolete locked hand routes of v0.x (IEC header J8 was at the right edge, RN8 is gone)
stale_locked = [t for t in board.GetTracks() if t.IsLocked()
                and t.GetNetname() in ("/IEC_Bus/IEC_RESET", "/IEC_Bus/IEC_SRQ")]
for t in stale_locked:
    board.Remove(t)
print("obsolete locked IEC copper removed", len(stale_locked))

rects = {ref: rect_of(fp) for ref, fp in fps.items()}
old_j1 = rects["J1"]
old_rects = dict(rects)
for ref, _x, _y, _rots in TARGETS:   # target parts are unplaced until their turn
    rects.pop(ref, None)
tracks = list(board.GetTracks())
moved = {}

# --- fixed parts
for ref, spec in FIXED.items():
    x, y, rot = spec[:3]
    fp = fps[ref]
    if len(spec) > 3 and spec[3] == "B" and fp.GetLayer() != pcbnew.B_Cu:
        fp.Flip(fp.GetPosition(), False)
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(V(x, y))
    rects[ref] = rect_of(fp)
    moved[ref] = rects[ref]
    print("%-5s fixed at (%.2f,%.2f) rot %g" % (ref, x, y, rot))

# --- target parts
for ref, tx, ty, rots in TARGETS:
    fp = fps[ref]
    others = [r for k, r in rects.items() if k != ref]
    best = None
    for rot in rots:
        fp.SetOrientationDegrees(rot)
        fp.SetPosition(V(0, 0))
        r0 = rect_of(fp)
        n = int(SEARCH / STEP)
        for ix in range(-n, n + 1):
            for iy in range(-n, n + 1):
                d = math.hypot(ix * STEP, iy * STEP)
                if d > SEARCH:
                    continue
                dx, dy = tx + ix * STEP, ty + iy * STEP
                r = (r0[0] + dx, r0[1] + dy, r0[2] + dx, r0[3] + dy)
                if r[0] < bx0 or r[1] < by0 or r[2] > bx1 or r[3] > by1:
                    continue
                if any(overlap(r, q, MARGIN) for q in others):
                    continue
                hit = [t for t in tracks if seg_hits_rect(t, (r[0] - 0.2, r[1] - 0.2, r[2] + 0.2, r[3] + 0.2))]
                if any(t.IsLocked() for t in hit):
                    continue
                score = len(hit) * 0.05 + d + (0.3 if rot != rots[0] else 0.0)  # old copper is re-routed anyway
                if best is None or score < best[0]:
                    best = (score, rot, dx, dy, r, len(hit))
    if best is None or math.hypot(best[2] - tx, best[3] - ty) > 1.5:
        fp.SetOrientationDegrees(rots[0])
        fp.SetPosition(V(tx, ty))
        r = rect_of(fp)
        blockers = [k for k, q in rects.items() if k != ref and overlap(r, q, MARGIN)]
        lk = [t.GetNetname() for t in tracks if t.IsLocked()
              and seg_hits_rect(t, (r[0] - 0.2, r[1] - 0.2, r[2] + 0.2, r[3] + 0.2))]
        print("   %s target rect %.2f %.2f %.2f %.2f blocked by %s locked %s" % (ref, *r, blockers, sorted(set(lk))))
    if best is None:
        raise SystemExit("no free spot for %s near (%.1f,%.1f)" % (ref, tx, ty))
    _, rot, dx, dy, r, nhit = best
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(V(dx, dy))
    rects[ref] = r
    moved[ref] = r
    far = math.hypot(dx - tx, dy - ty)
    print("%-5s at (%.2f,%.2f) rot %-3g  %.1f mm from target, tracks under it: %d" % (ref, dx, dy, rot, far, nhit))

# --- rip up unlocked copper under the moved parts, all copper of the old USB-C fan-out
j1box = (old_j1[0] - 1.5, old_j1[1] - 1.5, old_j1[2] + 1.5, old_j1[3] + 1.5)
doomed = []
for t in tracks:
    if seg_hits_rect(t, j1box):
        doomed.append(t)
    elif not t.IsLocked() and any(seg_hits_rect(t, (r[0] - 0.2, r[1] - 0.2, r[2] + 0.2, r[3] + 0.2))
                                   for r in moved.values()):
        doomed.append(t)
for t in doomed:
    board.Remove(t)
print("tracks/vias removed", len(doomed))


# --- USB-C fan-out at the new J1 position (same geometry as build_pcb.py, interior = +y)
def net(name):
    n = board.FindNet(name)
    if n is None:
        raise SystemExit("net missing: " + name)
    return n


def seg(netname, layer, pts, width):
    for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(V(xa, ya))
        t.SetEnd(V(xb, yb))
        t.SetLayer(layer)
        t.SetWidth(MM(width))
        t.SetNet(net(netname))
        t.SetLocked(True)
        board.Add(t)


def via(netname, x, y, d, drill):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(V(x, y))
    v.SetWidth(MM(d))
    v.SetDrill(MM(drill))
    v.SetNet(net(netname))
    v.SetLocked(True)
    board.Add(v)


jx = {p.GetNumber(): T(p.GetPosition().x) for p in fps["J1"].Pads()}
jy = T(next(p for p in fps["J1"].Pads() if p.GetNumber() == "A6").GetPosition().y)
cx = T(fps["J1"].GetPosition().x)
vb = {}
for pad in ("A4", "A9"):           # via next to each double VBUS pad, offset towards the centre
    inward = 0.35 if jx[pad] < cx else -0.35
    vb[pad] = (jx[pad] + inward, jy + 1.13)
    seg("VBUS", pcbnew.F_Cu, [(jx[pad], jy), (jx[pad], jy + 0.425), vb[pad]], 0.3)
    via("VBUS", vb[pad][0], vb[pad][1], 0.5, 0.25)
seg("VBUS", pcbnew.B_Cu, [vb["A4"], (vb["A4"][0], jy + 0.33), (vb["A9"][0], jy + 0.33), vb["A9"]], 0.5)
yv = jy + 1.48
for pad in ("B7", "A7"):           # D-: straight down to small vias, joined on B.Cu
    seg("USB_DM", pcbnew.F_Cu, [(jx[pad], jy), (jx[pad], yv)], 0.2)
    via("USB_DM", jx[pad], yv, 0.48, 0.23)   # 0.23 drill keeps the 0.125 mm annular ring
seg("USB_DM", pcbnew.B_Cu, [(jx["B7"], yv), (jx["A7"], yv)], 0.2)
yd = jy + 2.18                     # D+: A6 passes between the D- vias, B6 joins below on F.Cu
seg("USB_DP", pcbnew.F_Cu, [(jx["A6"], jy), (jx["A6"], yd), (jx["B6"], yd), (jx["B6"], jy)], 0.2)
print("J1 fan-out regenerated: pads y %.2f, D- vias y %.2f, D+ loop y %.2f" % (jy, yv, yd))

board.Save(path)
print("saved", path)
