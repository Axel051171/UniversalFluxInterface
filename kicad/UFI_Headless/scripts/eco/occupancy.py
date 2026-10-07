"""ASCII occupancy map of a board at 1 mm: '#' courtyard, digits = F.Cu track count in the cell
(0-9), '.' free.  usage: occupancy.py board"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
eb = b.GetBoardEdgesBoundingBox()
W, H = int(T(eb.GetWidth())), int(T(eb.GetHeight()))
grid = [[0] * W for _ in range(H)]
cy = [[False] * W for _ in range(H)]
for f in b.GetFootprints():
    c = f.GetCourtyard(pcbnew.F_CrtYd)
    bb = c.BBox() if c.OutlineCount() else f.GetBoundingBox(False)
    for y in range(max(0, int(T(bb.GetTop()))), min(H, int(T(bb.GetBottom())) + 1)):
        for x in range(max(0, int(T(bb.GetLeft()))), min(W, int(T(bb.GetRight())) + 1)):
            cy[y][x] = True
for t in b.GetTracks():
    if t.GetClass() == "PCB_VIA":
        p = t.GetPosition()
        x, y = int(T(p.x)), int(T(p.y))
        if 0 <= x < W and 0 <= y < H:
            grid[y][x] += 1
        continue
    if t.GetLayer() != pcbnew.F_Cu:
        continue
    s, e = t.GetStart(), t.GetEnd()
    for k in range(41):
        x = int(T(s.x) + (T(e.x) - T(s.x)) * k / 40)
        y = int(T(s.y) + (T(e.y) - T(s.y)) * k / 40)
        if 0 <= x < W and 0 <= y < H:
            grid[y][x] += 1
print("    " + "".join(str(x // 10) if x % 10 == 0 else " " for x in range(W)))
for y in range(H):
    print(f"{y:3} " + "".join("#" if cy[y][x] else ("." if grid[y][x] == 0 else str(min(9, grid[y][x] // 3)))
                              for x in range(W)))
