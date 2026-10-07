"""Build UFI Flux Adapter F1: Blue Pill (STM32F103C8) carrier with Shugart 34-pin floppy header.

Wiring follows Greaseweazle firmware, src/mcu/stm32f1/floppy.c + board.c (F1 'basic' submodel:
PC14/PC15 left floating, all floppy lines direct to 5V-tolerant PB pins, open-drain outputs).
"""
import sys

import pcbnew

OUT = sys.argv[1]
STD = r"C:\Program Files\KiCad\10.0\share\kicad\footprints"
MM = pcbnew.FromMM

# Blue Pill headers, pin 1 = end opposite the USB connector (verified on board photo + stm32-base.org)
H1 = ["VB", "C13", "C14", "C15", "A0", "A1", "A2", "A3", "A4", "A5", "A6", "A7",
      "B0", "B1", "B10", "B11", "R", "3V3", "GND", "GND"]
H2 = ["3V3", "GND", "5V", "B9", "B8", "B7", "B6", "B5", "B4", "B3", "A15", "A12",
      "A11", "A10", "A9", "A8", "B15", "B14", "B13", "B12"]

# Floppy pin -> (signal name, MCU port)
FDD = {
    2: ("DENSEL", "B9"), 8: ("INDEX", "B6"), 10: ("SEL0_MOTA", "B11"), 14: ("SEL1", "B10"),
    18: ("DIR", "B12"), 20: ("STEP", "B13"), 22: ("WDATA", "B4"), 24: ("WGATE", "B14"),
    26: ("TRK0", "B7"), 28: ("WRPROT", "B8"), 30: ("RDATA", "B3"), 32: ("SIDE", "B15"),
}

board = pcbnew.BOARD()
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(2)
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(MM(0.2))
nc.SetTrackWidth(MM(0.3))
nc.SetViaDiameter(MM(0.7))
nc.SetViaDrill(MM(0.35))
ds.m_TrackMinWidth = MM(0.15)
ds.m_MinClearance = MM(0.15)
ds.m_ViasMinSize = MM(0.5)
ds.m_MinThroughDrill = MM(0.3)
ds.m_CopperEdgeClearance = MM(0.5)

nets = {}


def net(name):
    if name not in nets:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        nets[name] = n
    return nets[name]


def place(lib, name, ref, value, x, y, rot):
    fp = pcbnew.FootprintLoad(f"{STD}\\{lib}.pretty", name)
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetReference(ref)
    fp.SetValue(value)
    board.Add(fp)
    fp.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    fp.SetOrientationDegrees(rot)
    return fp


def pad_xy(p):
    return round(pcbnew.ToMM(p.GetPosition().x), 3), round(pcbnew.ToMM(p.GetPosition().y), 3)


# Blue Pill sockets: pin 1 at x=58.26 (right), pin 20 at x=10 (left, USB side)
J1 = place("Connector_PinSocket_2.54mm", "PinSocket_1x20_P2.54mm_Vertical", "J1", "BluePill_Top", 58.26, 10, -90)
J2 = place("Connector_PinSocket_2.54mm", "PinSocket_1x20_P2.54mm_Vertical", "J2", "BluePill_Bottom", 58.26, 25.24, -90)
# Floppy header: pin 1 bottom-left, even row on top facing the Blue Pill
J3 = place("Connector_IDC", "IDC-Header_2x17_P2.54mm_Vertical", "J3", "FDD_34pin", 17.0, 45.54, 90)
for i, (hx, hy) in enumerate([(4, 4), (71, 4), (4, 50), (71, 50)], 1):
    h = place("MountingHole", "MountingHole_3.2mm_M3", f"H{i}", "M3", hx, hy, 0)
    h.Reference().SetVisible(False)  # would clip the board edge

# Geometry self-check
for fp, labels, y in ((J1, H1, 10.0), (J2, H2, 25.24)):
    for p in fp.Pads():
        n = int(p.GetNumber())
        x, py = pad_xy(p)
        assert abs(x - (58.26 - (n - 1) * 2.54)) < 0.01 and abs(py - y) < 0.01, (fp.GetReference(), n, x, py)
for p in J3.Pads():
    n = int(p.GetNumber())
    x, y = pad_xy(p)
    col = (n - 1) // 2
    assert abs(x - (17.0 + col * 2.54)) < 0.01, (n, x)
    assert abs(y - (42.999 if n % 2 == 0 else 45.54)) < 0.01, (n, y)

port_pad = {}
for fp, labels in ((J1, H1), (J2, H2)):
    for p in fp.Pads():
        lab = labels[int(p.GetNumber()) - 1]
        port_pad.setdefault(lab, []).append(p)

# Power/ground on sockets
for p in port_pad["GND"]:
    p.SetNet(net("GND"))
for p in port_pad["3V3"]:
    p.SetNet(net("+3V3"))
for p in port_pad["5V"]:
    p.SetNet(net("+5V"))

# Floppy header: odd pins GND, mapped even pins to MCU
for p in J3.Pads():
    n = int(p.GetNumber())
    if n % 2 == 1:
        p.SetNet(net("GND"))
    elif n in FDD:
        sig, port = FDD[n]
        name = f"FDD{n}_{sig}"
        p.SetNet(net(name))
        (mcu,) = port_pad[port]
        mcu.SetNet(net(name))

# Board outline 75 x 54 mm, 2 mm corner radius
W, H, R = 75.0, 54.0, 2.0


def seg(x1, y1, x2, y2):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(pcbnew.VECTOR2I(MM(x1), MM(y1)))
    s.SetEnd(pcbnew.VECTOR2I(MM(x2), MM(y2)))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)


def arc(cx, cy, sx, sy, deg):
    a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    a.SetCenter(pcbnew.VECTOR2I(MM(cx), MM(cy)))
    a.SetStart(pcbnew.VECTOR2I(MM(sx), MM(sy)))
    a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(deg, pcbnew.DEGREES_T), True)
    a.SetLayer(pcbnew.Edge_Cuts)
    a.SetWidth(MM(0.1))
    board.Add(a)


seg(R, 0, W - R, 0)
seg(W, R, W, H - R)
seg(W - R, H, R, H)
seg(0, H - R, 0, R)
arc(R, R, 0, R, 90)
arc(W - R, R, W - R, 0, 90)
arc(W - R, H - R, W, H - R, 90)
arc(R, H - R, R, H, 90)

# GND pours on both layers
for layer in (pcbnew.F_Cu, pcbnew.B_Cu):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net("GND"))
    z.SetLocalClearance(MM(0.3))
    z.SetMinThickness(MM(0.25))
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((0.5, 0.5), (W - 0.5, 0.5), (W - 0.5, H - 0.5), (0.5, H - 0.5)):
        ol.Append(MM(x), MM(y))
    board.Add(z)


def text(s, x, y, size=1.0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
    t.SetTextSize(pcbnew.VECTOR2I(MM(size), MM(size)))
    t.SetTextThickness(MM(0.15))
    t.SetLayer(layer)
    t.SetMirrored(layer == pcbnew.B_SilkS)
    board.Add(t)


text("USB", 6.0, 17.6, 1.0)
text("<", 6.0, 19.4, 1.2)
text("BLUE PILL - USB LINKS - BAUTEILSEITE OBEN", 34.1, 17.6, 0.9)
text("VB", 58.26, 7.0, 0.8)
text("3.3", 58.26, 28.2, 0.8)
text("B12", 10.0, 28.2, 0.8)
text("G", 10.0, 7.0, 0.8)
text("FDD 34pol (Shugart) - Pin 1", 30.0, 50.6, 0.9)
text("1", 15.2, 45.54, 1.0)
text("UFI Flux Adapter F1 v1.0", 37.5, 32.2, 1.0)
text("Firmware: Greaseweazle F1 - PC14/PC15 offen", 37.5, 52.4, 0.8, pcbnew.B_SilkS)

board.Save(OUT)
print("saved", OUT, "nets", board.GetNetCount())
