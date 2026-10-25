"""Stage B3 (owner decision 2026-10-25): turn the 4-layer board into 6 layers with the JLC
JLC06161H-3313 stackup (1.6 mm).  Text edit of the .kicad_pcb: adds In3.Cu (signal) and
In4.Cu (power = GND plane) to the layer table and writes the stackup into (setup).  Existing
In2 copper stays on In2 (L3).  Layer use S/G/S/S/G/S: F.Cu, In1 GND, In2, In3, In4 GND, B.Cu.
  L1 F.Cu 35 um / prepreg 3313 0.0994 mm er 4.1 / L2 In1 15.2 um / core 0.55 mm er 4.6 /
  L3 In2 / prepreg 2116 0.1088 mm er 4.16 / L4 In3 / core 0.55 mm er 4.6 / L5 In4 /
  prepreg 3313 0.0994 mm er 4.1 / L6 B.Cu 35 um
Idempotent.  usage: python make_6layer.py <board.kicad_pcb>   (plain Python, no pcbnew)
"""
import re
import sys

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
if '"In3.Cu"' in s.split("(setup", 1)[0]:
    print("already 6 layers")
    sys.exit(0)
s = s.replace('\t\t(6 "In2.Cu" signal)\n', '\t\t(6 "In2.Cu" signal)\n\t\t(8 "In3.Cu" signal)\n\t\t(10 "In4.Cu" power)\n', 1)
CU_OUT, CU_IN = 0.035, 0.0152
STACK = [
    ("F.SilkS", '(type "Top Silk Screen")'),
    ("F.Paste", '(type "Top Solder Paste")'),
    ("F.Mask", '(type "Top Solder Mask") (thickness 0.01)'),
    ("F.Cu", '(type "copper") (thickness %g)' % CU_OUT),
    ("dielectric 1", '(type "prepreg") (thickness 0.0994) (material "3313") (epsilon_r 4.1) (loss_tangent 0.02)'),
    ("In1.Cu", '(type "copper") (thickness %g)' % CU_IN),
    ("dielectric 2", '(type "core") (thickness 0.55) (material "FR4") (epsilon_r 4.6) (loss_tangent 0.02)'),
    ("In2.Cu", '(type "copper") (thickness %g)' % CU_IN),
    ("dielectric 3", '(type "prepreg") (thickness 0.1088) (material "2116") (epsilon_r 4.16) (loss_tangent 0.02)'),
    ("In3.Cu", '(type "copper") (thickness %g)' % CU_IN),
    ("dielectric 4", '(type "core") (thickness 0.55) (material "FR4") (epsilon_r 4.6) (loss_tangent 0.02)'),
    ("In4.Cu", '(type "copper") (thickness %g)' % CU_IN),
    ("dielectric 5", '(type "prepreg") (thickness 0.0994) (material "3313") (epsilon_r 4.1) (loss_tangent 0.02)'),
    ("B.Cu", '(type "copper") (thickness %g)' % CU_OUT),
    ("B.Mask", '(type "Bottom Solder Mask") (thickness 0.01)'),
    ("B.Paste", '(type "Bottom Solder Paste")'),
    ("B.SilkS", '(type "Bottom Silk Screen")'),
]
st = "\t\t(stackup\n" + "".join('\t\t\t(layer "%s" %s)\n' % (n, a) for n, a in STACK) + \
     '\t\t\t(copper_finish "HAL lead-free")\n\t\t\t(dielectric_constraints no)\n\t\t)\n'
s = re.sub(r"\t\(setup\n", "\t(setup\n" + st, s, count=1)
open(path, "w", encoding="utf-8").write(s)
print("6 layers, stackup written")
