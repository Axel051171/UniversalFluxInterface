"""Hand routes for connections Freerouting could not complete on the v0.1 layout.

  (BUCK_FB, crystal and buck hot paths are pre-routed in build_pcb.py)
  USB_DP  : J1 D+ fan-out (B6 leg) -> U1 (ESD) D+ line, on In2 between D- and CC2/VBUS
Each route is added only if the net is still split; tracks are locked.
usage: python manual_routes.py <board>
"""
import sys

import pcbnew

MM = pcbnew.FromMM
V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731
board = pcbnew.LoadBoard(sys.argv[1])

ROUTES = {
    "USB_DP": [  # via sits on the B6 leg of the fan-out, above the VBUS_F track at y~3.87
        ("via", (72.75, 3.2)),
        ("In2.Cu", [(72.75, 3.2), (72.75, 14.3)]),
        ("via", (72.75, 14.3)),
        ("F.Cu", [(72.75, 14.3), (72.75, 15.05)]),
    ],
}

conn = board.GetConnectivity()
conn.RecalculateRatsnest()
MM_EPS = 0.01


def has_via(netname, xy):
    return any(t.GetClass() == "PCB_VIA" and t.GetNetname() == netname
               and abs(pcbnew.ToMM(t.GetPosition().x) - xy[0]) < MM_EPS
               and abs(pcbnew.ToMM(t.GetPosition().y) - xy[1]) < MM_EPS for t in board.GetTracks())


for netname, parts in ROUTES.items():
    net = board.FindNet(netname)
    # per-net ratsnest is not exposed to Python in KiCad 10: act only on an incomplete board
    # and never twice (the route's first via marks it as already applied)
    if conn.GetUnconnectedCount(False) == 0 or has_via(netname, parts[0][1]):
        continue
    for kind, geo in parts:
        if kind == "via":
            v = pcbnew.PCB_VIA(board)
            v.SetPosition(V(*geo))
            v.SetWidth(MM(0.48))
            v.SetDrill(MM(0.25))
            v.SetNet(net)
            v.SetLocked(True)
            board.Add(v)
        else:
            layer = board.GetLayerID(kind)
            for a, b in zip(geo, geo[1:]):
                t = pcbnew.PCB_TRACK(board)
                t.SetStart(V(*a))
                t.SetEnd(V(*b))
                t.SetLayer(layer)
                t.SetWidth(MM(0.2))
                t.SetNet(net)
                t.SetLocked(True)
                board.Add(t)
    print("routed", netname)
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
board.Save(sys.argv[1])
