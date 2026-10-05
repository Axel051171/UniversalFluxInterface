"""Print review metrics for the critical layout areas.

usage: python layout_metrics.py <board>
"""
import math
import sys

import pcbnew

T = pcbnew.ToMM
board = pcbnew.LoadBoard(sys.argv[1])
fps = {fp.GetReference(): fp for fp in board.GetFootprints()}


def pad(ref, num):
    return next(p for p in fps[ref].Pads() if p.GetNumber() == num)


def dist(a, b):
    return math.hypot(T(a.x - b.x), T(a.y - b.y))


def routed_length(netname):
    length, vias = 0.0, 0
    for t in board.GetTracks():
        if t.GetNetname() == netname:
            if t.GetClass() == "PCB_VIA":
                vias += 1
            else:
                length += T(t.GetLength())
    return length, vias


print("== Crystal (target: short, symmetric, no vias)")
for n in ("/MCU_Core/HSE_IN", "/MCU_Core/HSE_OUT"):
    ln, v = routed_length(n)
    print(f"   {n:20s} {ln:5.1f} mm, {v} vias")

print("== Buck input loop (target: cap within ~2-3 mm of U2 VIN/GND)")
vin, gnd = pad("U2", "3").GetPosition(), pad("U2", "1").GetPosition()
for ref in ("C3", "C2"):
    p12 = next(p for p in fps[ref].Pads() if p.GetNetname() == "+12V").GetPosition()
    pg = next(p for p in fps[ref].Pads() if p.GetNetname() == "GND").GetPosition()
    print(f"   {ref}: +12V pad -> VIN {dist(p12, vin):.1f} mm, GND pad -> GND pin {dist(pg, gnd):.1f} mm")
sw, l1 = pad("U2", "2").GetPosition(), next(p for p in fps["L1"].Pads() if p.GetNetname() == "/Power/BUCK_SW").GetPosition()
print(f"   SW node U2.2 -> L1: {dist(sw, l1):.1f} mm (routed {routed_length('/Power/BUCK_SW')[0]:.1f} mm)")

print("== Supply copper (pour area / routed length = mean effective width)")
for z in board.Zones():
    if z.GetZoneName().startswith("pwr_"):
        area = T(T(z.GetFilledArea())) if hasattr(z, "GetFilledArea") else 0
        ln = sum(T(t.GetLength()) for t in board.GetTracks()
                 if t.GetClass() == "PCB_TRACK" and t.GetNetname() == z.GetNetname() and t.GetLayer() == z.GetLayer())
        if ln > 1:
            print(f"   {z.GetZoneName():14s} {board.GetLayerName(z.GetLayer()):6s} {area:6.1f} mm2 over {ln:5.1f} mm "
                  f"-> ~{area / ln:.2f} mm")

print("== USB D+/D- length (FS: matching uncritical)")
for n in ("USB_DP", "USB_DM"):
    ln, v = routed_length(n)
    print(f"   {n:8s} {ln:5.1f} mm, {v} vias")
