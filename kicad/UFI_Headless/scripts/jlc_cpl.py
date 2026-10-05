"""JLC CPL with package rotation offsets + a pin-1 checklist for the JLC placement viewer.

JLC's part models are not oriented like KiCad's footprints for several packages; the offsets
below are the defaults of kicad-jlcpcb-tools (rotations.py).  They are a starting point only:
every polarised part still has to be checked in the JLC viewer against the checklist.
usage: python jlc_cpl.py <board> <kicad-pos.csv> <out-cpl.csv> <out-checklist.md>
"""
import csv
import re
import sys

import pcbnew

OFFSETS = [  # (footprint regex, degrees added for JLC)
    (r"^SOT-223", 180), (r"^SOT-23", 180), (r"^SOT-353", 180),
    (r"^QFN-", 270), (r"^LQFP-", 270), (r"^TQFP-", 270), (r"^DFN-", 270),
    (r"^SOP-(?!18_)", 270), (r"^SOIC-", 270), (r"^TSSOP-", 270), (r"^VSSOP-10_", 270),
    (r"^CP_Elec_", 180), (r"^R_Array_Convex_4x0603", 90),
]
POLARISED = r"^(U|D|Y|J1$|C1$|C13$)"   # parts whose orientation matters (J1: USB-C)

board_path, pos_path, cpl_path, md_path = sys.argv[1:5]
board = pcbnew.LoadBoard(board_path)
T = pcbnew.ToMM


def offset(fp_name):
    return next((d for rx, d in OFFSETS if re.search(rx, fp_name)), 0)


def where(fp):
    """Pin 1 position relative to the part centre, as seen on the board (top view, y down)."""
    pads = list(fp.Pads())
    p1 = next((p for p in pads if p.GetNumber() in ("1", "A1")), None)
    if p1 is None:
        return "-"
    c = fp.GetBoundingBox(False, False).GetCenter()
    dx, dy = T(p1.GetPosition().x - c.x), T(p1.GetPosition().y - c.y)
    v = "oben" if dy < -0.2 else "unten" if dy > 0.2 else ""
    h = "links" if dx < -0.2 else "rechts" if dx > 0.2 else ""
    return (v + " " + h).strip() or "Mitte"


fps = {f.GetReference(): f for f in board.GetFootprints()}
rows, checks = [], []
with open(pos_path, newline="") as f:
    for r in csv.DictReader(f):
        ref, pkg, rot = r["Ref"], r["Package"], float(r["Rot"])
        off = offset(pkg)
        jrot = (rot + off) % 360
        rows.append([ref, f"{r['PosX']}mm", f"{r['PosY']}mm", "Top" if r["Side"] == "top" else "Bottom",
                     f"{jrot:g}"])
        if re.match(POLARISED, ref):
            checks.append((ref, r["Val"], pkg, rot, off, jrot, where(fps[ref])))

with open(cpl_path, "w", newline="") as f:
    w = csv.writer(f, quoting=csv.QUOTE_ALL)
    w.writerow(["Designator", "Mid X", "Mid Y", "Layer", "Rotation"])
    w.writerows(rows)


def key(c):
    m = re.match(r"([A-Z]+)(\d+)", c[0])
    return (m.group(1), int(m.group(2))) if m else (c[0], 0)


with open(md_path, "w", encoding="utf-8") as f:
    f.write("# JLC-Bestückungsviewer: Orientierung prüfen\n\n"
            "Im JLC-Viewer (Schritt *Component Placement*) muss Pin 1 des Bauteilmodells dort liegen, "
            "wo er auf der Platine liegt (Draufsicht). Falls nicht: Rotation im Viewer korrigieren und "
            "den Offset unten in `scripts/jlc_cpl.py` nachtragen.\n\n"
            "| Ref | Wert | Gehäuse | KiCad ° | Offset ° | JLC ° | Pin 1 auf der Platine |\n|---|---|---|---|---|---|---|\n")
    for c in sorted(checks, key=key):
        f.write(f"| {c[0]} | {c[1]} | {c[2]} | {c[3]:g} | {c[4]:+d} | {c[5]:g} | {c[6]} |\n")
    f.write("\nDioden/LEDs: Pin 1 = Kathode. Elkos C1/C13: Pin 1 = Plus. USB-C J1: Kontakte zur Platinenkante.\n")
print(f"cpl rows {len(rows)}, checklist {len(checks)}")
