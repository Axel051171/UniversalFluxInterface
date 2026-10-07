"""Hide the silkscreen reference of crowded small parts (the F.Fab copy stays).
usage: hide_ref.py <board> REF [REF ...]"""
import sys

import pcbnew

b = pcbnew.LoadBoard(sys.argv[1])
for ref in sys.argv[2:]:
    fp = b.FindFootprintByReference(ref)
    if not fp:
        sys.exit(f"{ref} not found")
    fp.Reference().SetVisible(False)
b.Save(sys.argv[1])
print("hidden", *sys.argv[2:])
