"""Remove the supply pours (zones with priority >= 5, made by power_pours.py) so that
power_pours.py can rebuild them along the current tracks after a re-route; a stale pour
outline leaves islands that the DRC reports as unconnected zone pieces.
usage: drop_pours.py <board>
"""
import sys

import pcbnew

board = pcbnew.LoadBoard(sys.argv[1])
doomed = [z for z in board.Zones() if z.GetAssignedPriority() >= 5]
for z in doomed:
    print("drop pour %-14s %s" % (z.GetNetname(), board.GetLayerName(z.GetLayer())))
    board.Remove(z)
board.Save(sys.argv[1])
print("removed", len(doomed))
