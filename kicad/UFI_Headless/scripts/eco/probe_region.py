"""Probe: courtyards and tracks/vias (locked flag) inside a box. usage: probe_region.py board x0 y0 x1 y1"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
x0, y0, x1, y1 = map(float, sys.argv[2:6])


def ov(r):
    return r[0] < x1 and x0 < r[2] and r[1] < y1 and y0 < r[3]


for fp in b.GetFootprints():
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    bb = cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)
    r = (T(bb.GetX()), T(bb.GetY()), T(bb.GetRight()), T(bb.GetBottom()))
    if ov(r):
        print(f"CRT {fp.GetReference():6} {r[0]:.2f}..{r[2]:.2f} x {r[1]:.2f}..{r[3]:.2f}")
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        p = t.GetPosition()
        if x0 <= T(p.x) <= x1 and y0 <= T(p.y) <= y1:
            print(f"VIA {t.GetNetname():14} ({T(p.x):.2f},{T(p.y):.2f}) locked={t.IsLocked()}")
        continue
    s, e = t.GetStart(), t.GetEnd()
    r = (min(T(s.x), T(e.x)), min(T(s.y), T(e.y)), max(T(s.x), T(e.x)), max(T(s.y), T(e.y)))
    if ov(r):
        print(f"TRK {t.GetNetname():14} {t.GetLayerName()} ({T(s.x):.2f},{T(s.y):.2f})-({T(e.x):.2f},{T(e.y):.2f}) w{T(t.GetWidth()):.2f} locked={t.IsLocked()}")
