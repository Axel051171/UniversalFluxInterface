"""Lock every track and via except those of the given nets (and remove the unlocked ones if
'rip' is given) so that a Freerouting pass on the DSN export touches only these nets; the
specctra export writes locked copper as fixed wires.
usage: lock_except.py <board> <out_board> NET[,NET..] [rip]
"""
import sys

import pcbnew

board = pcbnew.LoadBoard(sys.argv[1])
keep = set(sys.argv[3].split(","))
rip = len(sys.argv) > 4 and sys.argv[4] == "rip"
n_lock = n_rip = 0
for t in list(board.GetTracks()):
    if t.GetNetname() in keep:
        if rip:
            board.Remove(t)
            n_rip += 1
    else:
        t.SetLocked(True)
        n_lock += 1
board.Save(sys.argv[2])
print("locked", n_lock, "ripped", n_rip)
