"""Remove all copper pours except the In1 GND plane (before an incremental re-route).

finish_pcb.py and power_pours.py recreate them afterwards.
usage: python strip_pours.py <board>
"""
import sys

import pcbnew

board = pcbnew.LoadBoard(sys.argv[1])
doomed = [z for z in board.Zones() if not (z.GetLayer() == pcbnew.In1_Cu and z.GetNetname() == "GND")]
for z in doomed:
    board.Remove(z)
board.Save(sys.argv[1])
print("removed pours", len(doomed))
