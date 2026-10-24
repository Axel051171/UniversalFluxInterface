"""B2 follow-up: free the W5500 SPI exits.

Freerouting could not route LAN_SCK/MISO/MOSI twice: the crystal Y2 and the load capacitor C60
sat right under U23's pins 33-35 (0.5 mm pitch), leaving no room for the three vias.  New
arrangement below U23 (all F.Cu):
  Y2 at x 41.2 (XO pad under pin 31, XI through the gap between the crystal pad columns),
  C60 (XO load) between the crystal and the SPI column, C59 (XI load) below the crystal,
  R109 (feedback) below C60, the 3V3A filter caps C68/C62/C61 in the column left of the
  crystal at x 37.65, C65 below C59, C63/FB2 nudged right.
SW3 (DNP, back side) turns by 90 deg so its pads leave the RJ45 peg holes alone.
Copper touching the moved pads is removed; the crystal nets and the LAN_CS stub under the pin
row are ripped completely.  Re-route with batch_route.py afterwards.
usage: relayout_lan.py <board>
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731

MOVES = {  # ref: (x, y, rot)
    "Y2": (41.2, 19.5, 0.0),
    "C60": (44.0, 19.6, 270.0),
    "C59": (40.6, 22.1, 0.0),
    "R109": (43.0, 22.9, 90.0),
    "C68": (37.65, 19.4, 90.0),
    "C62": (37.65, 22.8, 90.0),
    "C61": (37.65, 26.2, 90.0),
    "C65": (40.5, 23.9, 0.0),
    "C63": (40.3, 25.6, 0.0),
    "FB2": (43.9, 25.6, 0.0),
    "SW3": (28.5, 7.5, 90.0),
}
RIP_NETS = {"/LAN/XI", "/LAN/XO"}
RIP_BOX = {"LAN_CS": (44.0, 16.0, 52.5, 20.0)}  # stub under the pin row up to R119

board = pcbnew.LoadBoard(sys.argv[1])
fps = {f.GetReference(): f for f in board.GetFootprints()}
tracks = list(board.GetTracks())
doomed = {}  # uuid -> track (PCB_TRACK objects are not hashable)


def touching(pad):
    out = []
    for t in tracks:
        if t.IsLocked() or t.GetNetCode() != pad.GetNetCode():
            continue
        if t.GetClass() == "PCB_VIA":
            if pad.HitTest(t.GetPosition()):
                out.append(t)
        elif pad.HitTest(t.GetStart()) or pad.HitTest(t.GetEnd()):
            out.append(t)
    return out


for ref in MOVES:
    for pad in fps[ref].Pads():
        for t in touching(pad):
            doomed[t.m_Uuid.AsString()] = t
for t in tracks:
    if t.IsLocked():
        continue
    if t.GetNetname() in RIP_NETS:
        doomed[t.m_Uuid.AsString()] = t
    box = RIP_BOX.get(t.GetNetname())
    if box:
        for p in ((t.GetStart(),) if t.GetClass() == "PCB_VIA" else (t.GetStart(), t.GetEnd())):
            if box[0] <= T(p.x) <= box[2] and box[1] <= T(p.y) <= box[3]:
                doomed[t.m_Uuid.AsString()] = t
for t in doomed.values():
    board.Remove(t)
print("copper removed", len(doomed))
for ref, (x, y, rot) in MOVES.items():
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(V(x, y))
    fp.BuildCourtyardCaches()
    print("moved %-4s -> (%.2f, %.2f) rot %.0f" % (ref, x, y, rot))
board.Save(sys.argv[1])
