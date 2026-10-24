"""List every track and via of a net (layer, end points, width), vias first, then tracks sorted by
start point - to read a route off the board without a picture.
usage: net_tracks.py <board> NET [x0 y0 x1 y1]
"""
import sys

import pcbnew

T = pcbnew.ToMM
board = pcbnew.LoadBoard(sys.argv[1])
net = sys.argv[2]
box = list(map(float, sys.argv[3:7])) if len(sys.argv) >= 7 else None


def inside(p):
    return box is None or (box[0] <= T(p.x) <= box[2] and box[1] <= T(p.y) <= box[3])


vias, trks = [], []
for t in board.GetTracks():
    if t.GetNetname() != net:
        continue
    if t.GetClass() == "PCB_VIA":
        if inside(t.GetPosition()):
            vias.append("VIA (%.2f,%.2f) %.2f/%.2f" % (T(t.GetPosition().x), T(t.GetPosition().y), T(t.GetWidth(pcbnew.F_Cu)), T(t.GetDrill())))
    elif box is None or (T(t.GetBoundingBox().GetLeft()) <= box[2] and T(t.GetBoundingBox().GetRight()) >= box[0]
                         and T(t.GetBoundingBox().GetTop()) <= box[3] and T(t.GetBoundingBox().GetBottom()) >= box[1]):
        a, b = t.GetStart(), t.GetEnd()
        trks.append((T(a.x), T(a.y), "%-6s (%.2f,%.2f)-(%.2f,%.2f) w%.2f len %.2f" % (
            board.GetLayerName(t.GetLayer()), T(a.x), T(a.y), T(b.x), T(b.y), T(t.GetWidth()), T(t.GetLength()))))
for v in sorted(vias):
    print(v)
for _, _, s in sorted(trks):
    print(s)
print("vias", len(vias), "tracks", len(trks), "length %.1f mm" % sum(float(s.split("len ")[1]) for _, _, s in trks))
