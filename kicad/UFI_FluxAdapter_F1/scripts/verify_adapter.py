"""Independent check: read nets back from the finished board and compare with Greaseweazle F1 firmware map."""
import sys

import pcbnew

H1 = ["VB", "C13", "C14", "C15", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
      "B0", "B1", "B10", "B11", "R", "3V3", "GND", "GND"]
H2 = ["3V3", "GND", "5V", "B9", "B8", "B7", "B6", "B5", "B4", "B3", "A15", "A12",
      "A11", "A10", "A9", "A8", "B15", "B14", "B13", "B12"]
# From greaseweazle-firmware src/mcu/stm32f1/floppy.c (pin_*) and board.c (_user_pins_std)
FW = {8: "B6", 26: "B7", 28: "B8", 18: "B12", 20: "B13", 24: "B14", 32: "B15",
      30: "B3", 22: "B4", 2: "B9", 10: "B11", 14: "B10"}

b = pcbnew.LoadBoard(sys.argv[1])
fp = {f.GetReference(): f for f in b.GetFootprints()}
label = {}
for ref, names in (("J1", H1), ("J2", H2)):
    for p in fp[ref].Pads():
        label.setdefault(p.GetNetname(), []).append(names[int(p.GetNumber()) - 1])
errors = 0
for p in sorted(fp["J3"].Pads(), key=lambda p: int(p.GetNumber())):
    n = int(p.GetNumber())
    net = p.GetNetname()
    got = sorted(set(label.get(net, [])))
    if n % 2:
        ok = net == "GND"
    elif n in FW:
        ok = got == [FW[n]]
    else:
        ok = net == ""
    errors += not ok
    if not ok or n % 2 == 0:
        print(f"FDD pin {n:2}: net={net or '-':18} -> BluePill {got or '-'}  {'OK' if ok else 'FEHLER'}")
# nothing else may be tied to floppy nets or to PC14/PC15 (F1 basic ID straps must float)
for ref, names in (("J1", H1), ("J2", H2)):
    for p in fp[ref].Pads():
        nm = names[int(p.GetNumber()) - 1]
        if nm in ("C14", "C15") and p.GetNetname():
            print("FEHLER: ID-Pin", nm, "verbunden mit", p.GetNetname())
            errors += 1
print("GND-Pads Blue Pill:", sorted(label.get("GND", [])))
print("Fehler:", errors)
