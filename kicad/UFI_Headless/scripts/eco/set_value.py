"""Set a footprint's value (and optional extra fields) after a schematic value change.
usage: set_value.py <board> REF VALUE [FIELD=VALUE ...]"""
import sys

import pcbnew

b = pcbnew.LoadBoard(sys.argv[1])
fp = b.FindFootprintByReference(sys.argv[2])
if not fp:
    sys.exit(f"{sys.argv[2]} not found")
fp.SetValue(sys.argv[3])
for kv in sys.argv[4:]:
    k, v = kv.split("=", 1)
    fp.SetField(k, v)
b.Save(sys.argv[1])
print(f"{sys.argv[2]} = {sys.argv[3]}", *sys.argv[4:])
