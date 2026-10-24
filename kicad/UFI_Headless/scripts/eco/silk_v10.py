"""Silkscreen notes for board v1.0: drop labels of removed parts (J9 front LEDs, J13 mode switch,
J18 5 V source jumper), move the labels of J8 (IEC) and J2 (12 V) to their new places, bump
the version text and the two User.Comments edge notes.
usage: python silk_v10.py <board>
"""
import sys

import pcbnew

MM = pcbnew.FromMM
path = sys.argv[1]
board = pcbnew.LoadBoard(path)

DROP = {"MODE 1=FLOP 3=SD", "FRONT LED", "BUCK", "EXT", "5V SRC"}
MOVE = {  # text: (x, y, rotation)
    "IEC 1=SRQ 6=RST": (9.0, 38.6, 0.0),   # J8 now at the left edge below J20
    "12V DC": (13.0, 16.9, 0.0),           # below the barrel jack J2, clear of J22
}
RENAME = {
    "UFI Headless v0.7": "UFI Headless v1.0",
}
NOTES = {
    "LAUFWERKSKANTE": ("LAUFWERKSKANTE: J6 34-pol, J14 Disk II, J7 Amiga, J3 Laufwerksstrom "
                       "(J8 IEC links, J12 Front rechts, J20 Guard, J21 LA intern) - v1.0 Layout B2"),
    "EINGANGSKANTE": ("EINGANGSKANTE: J17 PSU-IN, J2 12V, J22 LAN, J1 USB-C, J16 USB, J4 SWD, J5 UART "
                      "- Oeffnungen von J22 (3 mm) und J1 (1 mm) stehen ueber die Kante, Wand dort aussparen"),
}
dropped = moved = renamed = 0
for d in list(board.GetDrawings()):
    if d.GetClass() != "PCB_TEXT":
        continue
    txt = d.GetText()
    if txt in DROP:
        board.Remove(d)
        dropped += 1
    elif txt in MOVE:
        x, y, rot = MOVE[txt]
        d.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        d.SetTextAngleDegrees(rot)
        moved += 1
    elif txt in RENAME:
        d.SetText(RENAME[txt])
        renamed += 1
    else:
        for key, new in NOTES.items():
            if txt.startswith(key):
                d.SetText(new)
                renamed += 1
print("texts dropped", dropped, "moved", moved, "renamed", renamed)
board.Save(path)
