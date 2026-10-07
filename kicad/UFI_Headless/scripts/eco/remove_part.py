"""Remove footprints dropped from the schematic and rip up the given nets completely.
usage: remove_part.py <board> REF[,REF...] [NET ...]
Prints the removed footprint positions so the replacement can be placed there."""
import sys

import pcbnew

T = pcbnew.ToMM
b = pcbnew.LoadBoard(sys.argv[1])
refs = set(sys.argv[2].split(","))
nets = set(sys.argv[3:])
for fp in [f for f in b.GetFootprints() if f.GetReference() in refs]:
    p = fp.GetPosition()
    print(f"removed {fp.GetReference()} at {T(p.x):.2f} {T(p.y):.2f} rot {fp.GetOrientationDegrees():.0f}")
    b.Remove(fp)
doomed = [t for t in b.GetTracks() if t.GetNetname() in nets]  # collect first, then remove
for t in doomed:
    b.Remove(t)
print("tracks/vias removed", len(doomed))
b.Save(sys.argv[1])
