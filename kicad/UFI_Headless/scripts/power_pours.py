"""Reinforce routed supply nets with copper pours that follow their tracks.

For every supply net and copper layer, the net's tracks are inflated by BUFFER and
merged into one zone outline (priority above the GND / +3V3 pours).  The zone filler
then widens each route as far as clearance allows, without DRC violations and
without touching the verified routing.
usage: python power_pours.py <board>
"""
import sys

import pcbnew

MM = pcbnew.FromMM
SUPPLY_NETS = ["VBUS", "/Power/VBUS_F", "/Power/+12V_IN", "+12V", "/Power/+5V_DRV",
               "/Power/BUCK_SW", "+5V", "FDD_5V", "FDD_12V"]
BUFFER = 0.4  # mm added on each side of the routed track
LAYERS = (pcbnew.F_Cu, pcbnew.In2_Cu, pcbnew.B_Cu)

board = pcbnew.LoadBoard(sys.argv[1])
existing = {(z.GetNetname(), z.GetLayer()) for z in board.Zones() if z.GetAssignedPriority() >= 5}
added = 0
for netname in SUPPLY_NETS:
    net = board.FindNet(netname)
    if net is None:
        continue
    for layer in LAYERS:
        if (netname, layer) in existing:
            continue
        poly = pcbnew.SHAPE_POLY_SET()
        for t in board.GetTracks():
            if t.GetClass() == "PCB_TRACK" and t.GetNetname() == netname and t.GetLayer() == layer:
                t.TransformShapeToPolygon(poly, layer, MM(BUFFER), MM(0.01), pcbnew.ERROR_INSIDE)
        if poly.OutlineCount() == 0:
            continue
        poly.Simplify()
        z = pcbnew.ZONE(board)
        z.SetLayer(layer)
        z.SetNet(net)
        z.SetAssignedPriority(5)
        z.SetLocalClearance(MM(0.25))
        z.SetMinThickness(MM(0.2))
        z.SetPadConnection(pcbnew.ZONE_CONNECTION_FULL)
        z.SetIslandRemovalMode(pcbnew.ISLAND_REMOVAL_MODE_AREA)
        z.SetMinIslandArea(MM(1.0) * MM(1.0))  # drop slivers < 1 mm^2
        z.SetZoneName(f"pwr_{netname.rsplit('/', 1)[-1]}")
        for i in range(poly.OutlineCount()):
            z.Outline().AddOutline(poly.Outline(i))
        board.Add(z)
        added += 1
pcbnew.ZONE_FILLER(board).Fill(board.Zones())
board.Save(sys.argv[1])
print("power pours", added)
