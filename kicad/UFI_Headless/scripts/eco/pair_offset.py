"""Offset a differential-pair centreline into the two member polylines (mitred corners).
The member on the left of the direction of travel is printed first.
usage: python pair_offset.py WIDTH GAP LAYER x,y x,y ...      (plain Python)
prints two lines of "x,y,L" points, ready for route_try.py / addroute.py
"""
import math
import sys

w, gap, layer = float(sys.argv[1]), float(sys.argv[2]), sys.argv[3]
pts = [tuple(map(float, a.split(","))) for a in sys.argv[4:]]
d = (w + gap) / 2


def offset(sign):
    out = []
    n = len(pts)
    for i in range(n):
        if i == 0 or i == n - 1:
            a, b = (pts[0], pts[1]) if i == 0 else (pts[-2], pts[-1])
            dx, dy = b[0] - a[0], b[1] - a[1]
            L = math.hypot(dx, dy)
            nx, ny = -dy / L, dx / L          # left normal (y down: screen coordinates)
            p = pts[i]
            out.append((p[0] + sign * d * nx, p[1] + sign * d * ny))
            continue
        a, p, b = pts[i - 1], pts[i], pts[i + 1]
        d1 = (p[0] - a[0], p[1] - a[1])
        d2 = (b[0] - p[0], b[1] - p[1])
        l1, l2 = math.hypot(*d1), math.hypot(*d2)
        n1 = (-d1[1] / l1, d1[0] / l1)
        n2 = (-d2[1] / l2, d2[0] / l2)
        bis = (n1[0] + n2[0], n1[1] + n2[1])
        lb = math.hypot(*bis)
        cosh = (n1[0] * bis[0] + n1[1] * bis[1]) / lb
        k = sign * d / cosh / lb
        out.append((p[0] + bis[0] * k, p[1] + bis[1] * k))
    return out


for s in (+1, -1):
    print(" ".join("%.3f,%.3f,%s" % (x, y, layer) for x, y in offset(s)))
