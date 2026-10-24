"""Set the unlocked vias inside a box to 0.5/0.25 mm (annular ring 0.125 = the board minimum).
A 0.6 mm dogbone via next to a 0.5 mm pitch QFP row blocks the neighbouring pad's exit
(0.5 - 0.3 - 0.1 = 0.1 mm); at 0.5 mm the lane keeps the 0.15 mm rule.
usage: shrink_vias.py <board> x0 y0 x1 y1 [x0 y0 x1 y1 ...]
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
board = pcbnew.LoadBoard(sys.argv[1])
nums = list(map(float, sys.argv[2:]))
boxes = [nums[i:i + 4] for i in range(0, len(nums), 4)]
n = 0
for t in board.GetTracks():
    if t.GetClass() != "PCB_VIA" or t.IsLocked():
        continue
    x, y = T(t.GetPosition().x), T(t.GetPosition().y)
    for b in boxes:
        if b[0] <= x <= b[2] and b[1] <= y <= b[3] and t.GetWidth() > MM(0.5):
            t.SetWidth(MM(0.5))
            t.SetDrill(MM(0.25))
            print("via %-18s (%.2f, %.2f) -> 0.5/0.25" % (t.GetNetname()[:18], x, y))
            n += 1
board.Save(sys.argv[1])
print("shrunk", n)
