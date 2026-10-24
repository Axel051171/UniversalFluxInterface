"""Silkscreen rule of this board: references only for ICs, connectors, switches, crystals, fuses,
inductors and the LED; passives, test points, solder jumpers, transistors and protection diodes
keep their reference on F.Fab only.  Also parks the "FDD PWR" label below the fuse F2.
usage: python hide_small_refs.py <board>
"""
import sys

import pcbnew

HIDE_PREFIX = ("R", "C", "FB", "TP", "JP", "Q", "D")
KEEP = {"D4"}  # the ACT LED
MM = pcbnew.FromMM
path = sys.argv[1]
board = pcbnew.LoadBoard(path)
hidden = 0
for fp in board.GetFootprints():
    ref = fp.GetReference()
    prefix = ref.rstrip("0123456789")
    if prefix in HIDE_PREFIX and ref not in KEEP and fp.Reference().IsVisible():
        fp.Reference().SetVisible(False)
        hidden += 1
for d in board.GetDrawings():
    if d.GetClass() == "PCB_TEXT" and d.GetText() == "FDD PWR":
        d.SetPosition(pcbnew.VECTOR2I(MM(6.2), MM(65.0)))
print("references hidden", hidden)
board.Save(path)
