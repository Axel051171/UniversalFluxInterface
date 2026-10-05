"""Update footprint values/fields from the schematic netlist without touching copper.

Field-only equivalent of "Update PCB from Schematic" for a routed board: values,
Datasheet and all symbol fields (MPN, LCSC, ...) are copied, extra fields hidden.
usage: python sync_fields.py <netlist.net> <board>
"""
import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netlist import read_netlist  # noqa: E402

comps, _nets = read_netlist(sys.argv[1])
board = pcbnew.LoadBoard(sys.argv[2])
changed = 0
for fp in board.GetFootprints():
    c = comps.get(fp.GetReference())
    if c is None:
        continue  # board-only items (mounting holes)
    if fp.GetValue() != c["value"]:
        fp.SetValue(c["value"])
        changed += 1
    for k, v in c["props"].items():
        if k in ("Reference", "Value", "Footprint") or k.startswith("ki_"):
            continue
        fp.SetField(k, v)
        changed += 1
    if c["datasheet"] not in ("", "~"):
        fp.GetField(pcbnew.FIELD_T_DATASHEET).SetText(c["datasheet"])
    for f in fp.GetFields():
        if f.GetName() != "Reference":
            f.SetVisible(False)
board.Save(sys.argv[2])
print("updated fields/values", changed)
