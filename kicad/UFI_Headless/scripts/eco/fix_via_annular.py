"""Shrink the drill of unlocked vias whose annular ring is below the board minimum
(Freerouting occasionally emits 0.6/0.4 vias; 0.6/0.3 keeps the 0.15 mm ring).
usage: python fix_via_annular.py <board> [min_annular_mm]
"""
import sys
from collections import Counter

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
path = sys.argv[1]
min_ring = float(sys.argv[2]) if len(sys.argv) > 2 else 0.125
board = pcbnew.LoadBoard(path)
fixed = Counter()
for t in board.GetTracks():
    if t.GetClass() != "PCB_VIA" or t.IsLocked():
        continue
    w, d = T(t.GetWidth(pcbnew.F_Cu)), T(t.GetDrillValue())
    if (w - d) / 2 < min_ring - 1e-6:
        new_drill = round(w - 2 * min_ring - 0.05, 2)      # 0.6 -> 0.3, 0.8 -> 0.5
        new_drill = min(new_drill, 0.3 if w <= 0.6 else 0.4)
        t.SetDrill(MM(new_drill))
        fixed[(w, d, new_drill)] += 1
print("vias fixed", sum(fixed.values()), dict(fixed))
if fixed:
    board.Save(path)
