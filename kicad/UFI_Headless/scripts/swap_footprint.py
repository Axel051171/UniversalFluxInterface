"""Swap one footprint on a routed board for the one the schematic now specifies.

The new footprint is placed flush with a board edge, gets path/fields/pad nets from the
netlist; the old one is removed.  Tracks left dangling are cleaned up and re-routed by
the caller (drop_violations.py + Freerouting).
usage: python swap_footprint.py <netlist.net> <board> <ref> <edge> <along_mm> [rot]
"""
import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netlist import read_netlist  # noqa: E402

STD = Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
MM, T = pcbnew.FromMM, pcbnew.ToMM
net_file, path, ref, edge, along = sys.argv[1:6]
rot = float(sys.argv[6]) if len(sys.argv) > 6 else 0.0
comps, nets = read_netlist(net_file)
board = pcbnew.LoadBoard(path)
c = comps[ref]
lib, name = c["footprint"].split(":")
fp = pcbnew.FootprintLoad(str(STD / f"{lib}.pretty"), name)
fp.SetFPID(pcbnew.LIB_ID(lib, name))
fp.SetReference(ref)
fp.SetValue(c["value"])
fp.SetPath(pcbnew.KIID_PATH(c["path"]))
for k, v in c["props"].items():
    if k not in ("Reference", "Value", "Footprint") and not k.startswith("ki_"):
        fp.SetField(k, v)
for f in fp.GetFields():
    if f.GetName() != "Reference":
        f.SetVisible(False)
fp.SetOrientationDegrees(rot)
fp.SetPosition(pcbnew.VECTOR2I(0, 0))
cy = fp.GetCourtyard(pcbnew.F_CrtYd)
bb = cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)
w, h = T(bb.GetWidth()), T(bb.GetHeight())
eb = board.GetBoardEdgesBoundingBox()
W, H = T(eb.GetWidth()), T(eb.GetHeight())
cx, cyy = {"right": (W - w / 2 - 0.5, float(along)), "left": (w / 2 + 0.5, float(along)),
           "top": (float(along), h / 2 + 0.5), "bottom": (float(along), H - h / 2 - 0.5)}[edge]
ctr = bb.GetCenter()
fp.SetPosition(pcbnew.VECTOR2I(MM(cx) - ctr.x, MM(cyy) - ctr.y))
pin_net = {(r, p): n for n, nodes in nets.items() for r, p in nodes}
for p in fp.Pads():
    n = pin_net.get((ref, p.GetNumber()))
    if n:
        p.SetNet(board.FindNet(n) or pcbnew.NETINFO_ITEM(board, n))
old = [f for f in board.GetFootprints() if f.GetReference() == ref]
board.Add(fp)
for o in old:            # Remove last and save right away (iterating after Remove crashes pcbnew)
    board.Remove(o)
board.Save(path)
print(f"swapped {ref} -> {name} at ({cx:.2f},{cyy:.2f})")
