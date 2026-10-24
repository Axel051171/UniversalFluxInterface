"""Remove the unlocked tracks and vias of a net, optionally only inside a box (mm).
usage: rip_net.py <board> NET[:x0,y0,x1,y1] [NET[:box] ...]
Follow with trim_dangling.py rounds; re-route with batch_route.py.
"""
import sys

import pcbnew

T = pcbnew.ToMM
board = pcbnew.LoadBoard(sys.argv[1])
specs = []
for a in sys.argv[2:]:
    net, _, box = a.partition(":")
    specs.append((net, tuple(map(float, box.split(","))) if box else None))
removed = 0
for t in list(board.GetTracks()):
    if t.IsLocked():
        continue
    for net, box in specs:
        if net != "*" and t.GetNetname() != net:  # "*" = every net (clear a corridor)
            continue
        pts = (t.GetPosition(),) if t.GetClass() == "PCB_VIA" else (t.GetStart(), t.GetEnd())
        if box is None or any(box[0] <= T(p.x) <= box[2] and box[1] <= T(p.y) <= box[3] for p in pts):
            board.Remove(t)
            removed += 1
            break
board.Save(sys.argv[1])
print("removed", removed)
