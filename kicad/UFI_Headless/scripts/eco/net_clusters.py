"""Probe: connected clusters of a net (pads per cluster) via KiCad connectivity. usage: net_clusters.py board NET"""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
pcbnew.ZONE_FILLER(b).Fill(b.Zones())
b.BuildConnectivity()
con = b.GetConnectivity()
net = b.FindNet(sys.argv[2]).GetNetCode()
pads = [p for f in b.GetFootprints() for p in f.Pads() if p.GetNetCode() == net]
seen, clusters = set(), []
for p in pads:
    if p.m_Uuid.AsString() in seen:
        continue
    items = con.GetConnectedItems(p)
    group = [p] + [i for i in items if i.GetClass() == "PAD"]
    for g in group:
        seen.add(g.m_Uuid.AsString())
    clusters.append(group)
for c in clusters:
    print(len(c), " ".join(f"{g.GetParentFootprint().GetReference()}.{g.GetNumber()}"
                           f"({T(g.GetPosition().x):.1f},{T(g.GetPosition().y):.1f})" for g in c))
