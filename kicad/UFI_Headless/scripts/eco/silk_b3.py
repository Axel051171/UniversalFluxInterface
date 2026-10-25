"""Silkscreen notes for stage B3 (power chain re-placed, J6/J7/J14 moved, J3 in the J6/J7 pocket):
drop the "FDD PWR" label that sat below the removed polyfuse F2 and three free texts that lost
their spot (DROP_AT), put a label at the eFuse chain and one for BT1 on the back, and refresh
the two User.Comments edge notes.  Run before pin_labels.py / place_silk.py.
usage: python silk_b3.py <board>
"""
import sys

import pcbnew

MM = pcbnew.FromMM
path = sys.argv[1]
board = pcbnew.LoadBoard(path)

DROP = {"FDD PWR"}
# free texts without a free spot after the B3 placement, redundant with the per-pin labels
# (J17: 5V/GND/GND/12V), the J6 reference and the LAUFWERKSKANTE note: (text, x, y) +-0.5 mm
DROP_AT = [("PSU", 2.5, 20.7), ("FDD 34", 30.0, 71.1), ("5V", 14.25, 74.65)]
NOTES = {
    "LAUFWERKSKANTE": ("LAUFWERKSKANTE: J14 Disk II links, J6 34-pol, J3 Laufwerksstrom in der Tasche "
                       "J6/J7, J7 Amiga, J23 Mac rechts (J8 IEC links, J12 Front rechts, J20 Guard, "
                       "J21 LA intern) - v1.0 Layout B3"),
    "EINGANGSKANTE": ("EINGANGSKANTE: J17 PSU-IN, J2 12V, J22 LAN, J1 USB-C, J16 USB, J4 SWD, J5 UART "
                      "- Oeffnungen von J22 (3 mm) und J1 (1 mm) stehen ueber die Kante, Wand dort aussparen"),
}
ADD = [  # (text, x, y, angle, height, layer)
    ("eFUSE 5V/12V", 41.0, 54.94, 0.0, 0.8, pcbnew.F_SilkS),      # free spot found by place_silk.py
    ("BT1 CR2032 unten", 52.0, 44.24, 0.0, 0.8, pcbnew.B_SilkS),
]
dropped = renamed = added = 0
texts = [d for d in board.GetDrawings() if d.GetClass() == "PCB_TEXT"]  # Drawings() breaks after Remove
existing = set()
for d in texts:
    txt = d.GetText()
    p = d.GetPosition()
    if txt in DROP or any(txt == t and abs(pcbnew.ToMM(p.x) - x) < 0.5 and abs(pcbnew.ToMM(p.y) - y) < 0.5
                          for t, x, y in DROP_AT):
        board.Remove(d)
        dropped += 1
        continue
    for key, new in NOTES.items():
        if txt.startswith(key) and txt != new:
            d.SetText(new)
            renamed += 1
            txt = new
    existing.add(txt)
for txt, x, y, ang, h, layer in ADD:
    if txt in existing:
        continue
    t = pcbnew.PCB_TEXT(board)
    t.SetText(txt)
    t.SetLayer(layer)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetTextSize(pcbnew.VECTOR2I(MM(h), MM(h)))
    t.SetTextThickness(MM(0.15))
    t.SetTextAngleDegrees(ang)
    if layer == pcbnew.B_SilkS:
        t.SetMirrored(True)
    board.Add(t)
    added += 1
print("texts dropped", dropped, "renamed", renamed, "added", added)
board.Save(path)
