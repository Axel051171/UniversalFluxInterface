"""Create UFI_Headless.kicad_pcb from the schematic netlist: footprints, nets, placement,
outline, planes.  Routing is done afterwards by Freerouting (see make_pcb.sh).

usage: python build_pcb.py <netlist.net> <out.kicad_pcb>
"""
from __future__ import annotations

import math
import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netlist import read_netlist  # noqa: E402

STD = Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
UFI_LIB = Path(__file__).resolve().parents[2] / "footprints"
MM = pcbnew.FromMM
W, H, CORNER = 110.0, 85.0, 2.0

comps, nets = read_netlist(sys.argv[1])
OUT = sys.argv[2]

board = pcbnew.BOARD()
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(4)
board.SetLayerType(pcbnew.In1_Cu, pcbnew.LT_POWER)
# In2 is a signal layer during routing; finish_pcb.py pours +3V3 into the free space afterwards
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(MM(0.15))
nc.SetTrackWidth(MM(0.2))
nc.SetViaDiameter(MM(0.6))
nc.SetViaDrill(MM(0.3))
ds.m_TrackMinWidth = MM(0.127)
ds.m_MinClearance = MM(0.127)
ds.m_ViasMinSize = MM(0.45)
ds.m_MinThroughDrill = MM(0.25)  # USB-C D- fan-out vias
ds.m_CopperEdgeClearance = MM(0.3)

# Net classes: wider tracks for supply / high-current nets (used by Freerouting via the DSN)
NET_CLASSES = {
    "Power": (0.25, 0.15, ["+12V", "/Power/+12V_IN", "/Power/+5V_DRV", "/Power/BUCK_SW",
                         "+5V", "VBUS", "/Power/VBUS_F"]),
    "Supply": (0.3, 0.15, ["+3V3", "GND", "/MCU_Core/VDDA"]),
}
ns = ds.m_NetSettings
for cls, (width, clearance, members) in NET_CLASSES.items():
    c = pcbnew.NETCLASS(cls)
    c.SetTrackWidth(MM(width))
    c.SetClearance(MM(clearance))
    c.SetViaDiameter(MM(0.8))
    c.SetViaDrill(MM(0.4))
    ns.SetNetclass(cls, c)
    for m in members:
        ns.SetNetclassPatternAssignment(m, cls)

# ---------------------------------------------------------------------------
# footprints + nets
# ---------------------------------------------------------------------------
netinfo: dict[str, pcbnew.NETINFO_ITEM] = {}


def net(name: str):
    if name not in netinfo:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        netinfo[name] = n
    return netinfo[name]


def short(name: str) -> str:
    """'/MCU_Core/NRST' -> 'NRST'; global nets keep their name."""
    return name.rsplit("/", 1)[-1]


fps: dict[str, pcbnew.FOOTPRINT] = {}
for ref, c in sorted(comps.items()):
    lib, name = c["footprint"].split(":")
    path = UFI_LIB if lib == "UFI" else STD / f"{lib}.pretty"
    fp = pcbnew.FootprintLoad(str(path), name)
    assert fp is not None, c["footprint"]
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetReference(ref)
    fp.SetValue(c["value"])
    fp.SetPath(pcbnew.KIID_PATH(c["path"]))
    for k, v in c["props"].items():  # copy symbol fields (Datasheet, MPN, Note, ...) for parity
        if k not in ("Reference", "Value", "Footprint") and not k.startswith("ki_"):
            fp.SetField(k, v)
    for f in fp.GetFields():  # copied fields are for parity only, never on silkscreen
        if f.GetName() not in ("Reference",):
            f.SetVisible(False)
    if "Description" not in c["props"]:  # symbol has none -> don't keep the library footprint text
        fp.GetField(pcbnew.FIELD_T_DESCRIPTION).SetText("")
    if c["datasheet"] not in ("", "~"):
        fp.GetField(pcbnew.FIELD_T_DATASHEET).SetText(c["datasheet"])
    # dense board: silkscreen references only for ICs, connectors, switches, crystal, LEDs
    if ref.rstrip("0123456789") in ("C", "R", "RN", "FB", "L", "F") or ref in ("D1", "D2", "D3", "J1", "U2"):
        fp.Reference().SetVisible(False)
    board.Add(fp)
    fps[ref] = fp

# every pad gets its schematic net, including KiCad's single-pin "unconnected-(...)" nets
pad_net: dict[tuple[str, str], str] = {}
for name, nodes in nets.items():
    for ref, pin in nodes:
        pad_net[(ref, pin)] = name  # full hierarchical name keeps schematic parity
for ref, fp in fps.items():
    for p in fp.Pads():
        n = pad_net.get((ref, p.GetNumber()))
        if n:
            p.SetNet(net(n))

# ---------------------------------------------------------------------------
# placement helpers
# ---------------------------------------------------------------------------
def courtyard(fp):
    cy = fp.GetCourtyard(pcbnew.F_CrtYd if not fp.IsFlipped() else pcbnew.B_CrtYd)
    return cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)


def put(ref, cx, cy, rot=0.0, back=False):
    """Place footprint so that its courtyard centre lands on (cx, cy)."""
    fp = fps[ref]
    if back and not fp.IsFlipped():
        fp.Flip(fp.GetPosition(), pcbnew.FLIP_DIRECTION_LEFT_RIGHT)
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(0, 0))
    c = courtyard(fp).GetCenter()
    fp.SetPosition(pcbnew.VECTOR2I(MM(cx) - c.x, MM(cy) - c.y))
    return fp


def put_edge(ref, rot, edge, along):
    """Place a connector flush against a board edge ('top', 'bottom', 'left', 'right')."""
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(0, 0))
    bb = courtyard(fp)
    w, h = pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())
    cx, cy = {"top": (along, h / 2), "bottom": (along, H - h / 2),
              "left": (w / 2, along), "right": (W - w / 2, along)}[edge]
    return put(ref, cx, cy, rot)


def pad_pos(ref, num):
    p = next(p for p in fps[ref].Pads() if p.GetNumber() == num)
    return pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)


def refs_on(net_name, value=None, prefix="C"):
    """Components (by prefix/value) having a pad on `net_name`."""
    out = []
    for (ref, _pin), n in pad_net.items():
        if n == net_name and ref.startswith(prefix) and (value is None or comps[ref]["value"] == value):
            out.append(ref)
    return sorted(set(out), key=lambda r: int("".join(ch for ch in r if ch.isdigit())))


# ---------------------------------------------------------------------------
# placement (x right, y down, board origin top-left)
# ---------------------------------------------------------------------------
# Edge connectors
put_edge("J6", 90, "bottom", 31.0)        # FDD 34-pin
put_edge("J7", 90, "bottom", 84.0)        # Amiga 2x12
put_edge("J8", 0, "right", 40.0)          # IEC 1x6 header (pin n = DIN-6 pin n), DIN socket external
put_edge("J1", 0, "top", 72.0)            # USB-C
put_edge("J2", 270, "top", 13.0)          # 12V barrel jack, opening towards top edge
put_edge("J3", 0, "left", 52.0)           # FDD power out

# MCU (LQFP144: pins 1-36 left, 37-72 bottom, 73-108 right, 109-144 top)
MCU = (55.0, 40.0)
put("U5", *MCU)

# MCU decoupling: one 100nF next to every VDD/VDD33USB/VBAT pad, just outside the pad toe
dec_caps = [r for r in refs_on("+3V3", "100nF") if r in {f"C{i}" for i in range(17, 31)}]
vdd_pads = [p for p in fps["U5"].Pads() if p.GetNetname() == "+3V3" and p.GetNumber() != "143"]
assert len(dec_caps) == 14 and len(vdd_pads) == 13, (dec_caps, [p.GetNumber() for p in vdd_pads])
for cap, pad in zip(dec_caps, vdd_pads):
    px, py = pcbnew.ToMM(pad.GetPosition().x), pcbnew.ToMM(pad.GetPosition().y)
    dx, dy = px - MCU[0], py - MCU[1]
    if abs(dx) > abs(dy):   # left/right side: cap horizontal, outside the toe
        put(cap, MCU[0] + math.copysign(14.2, dx), py, 0 if dx > 0 else 180)
    else:
        put(cap, px, MCU[1] + math.copysign(14.2, dy), 270 if dy > 0 else 90)
put(dec_caps[13], 72.5, pad_pos("U5", "95")[1], 0)   # 2nd cap on VDD33USB (pin 95)

# VCAP (pins 71 bottom-right, 106 right-top), VDDA filter, bulk
put("C35", 67.0, 55.5, 0)
put("C36", 72.5, 32.25, 0)
put("C33", 36.5, 47.5, 0)          # VDDA 1uF   (pin 33 at left side, y~47.25)
put("C34", 36.5, 50.0, 0)          # VDDA 100nF
put("FB1", 36.5, 52.5, 0)
put("C31", 40.0, 24.0, 0)          # 4.7uF bulk
put("C32", 70.0, 24.0, 0)          # 1uF

# Crystal next to PH0/PH1 (pins 23/24, left side y~42.5)
put("Y1", 39.8, 42.6, 270)         # pin1 (HSE_IN) top-left, pin3 (HSE_OUT) bottom-right
put("C38", 36.4, 41.4, 180)         # HSE pad towards the crystal
put("C39", 36.4, 43.8, 180)

# Reset / boot buttons on the top edge
put("SW1", 38.0, 8.0, 0)
put("SW2", 48.0, 8.0, 0)
put("C37", 38.0, 13.5, 0)
put("R8", 48.0, 13.5, 0)

# USB: ESD next to the receptacle, CC resistors, VBUS protection, sense divider
put("U1", 72.0, 16.0, 0)
put("R1", 64.0, 6.0, 90)
put("R2", 80.0, 6.0, 90)
put("D1", 84.0, 14.0, 90)
put("F1", 88.0, 6.0, 90)
put("R9", 76.0, 24.0, 90)
put("R10", 79.0, 24.0, 90)

# SWD, UART, LEDs top-right
put("J4", 96.0, 6.0, 0)
put("J5", 105.0, 18.0, 0)
for i, (led, res) in enumerate((("D4", "R11"), ("D5", "R12"), ("D6", "R13"), ("D7", "R14"), ("D8", "R15"))):
    put(led, 93.0, 13.0 + i * 3.0, 0)
    put(res, 88.5, 13.0 + i * 3.0, 0)

# Power section (left): 12V input -> buck -> mux -> LDO
put("D2", 11.0, 21.0, 0)           # SS54 reverse protection
put("C2", 29.0, 25.2, 0)           # 10uF/35V input bulk
put("C3", 25.0, 27.3, 180)          # 100nF input cap, +12V pad 1.8 mm from VIN
put("D3", 5.0, 31.0, 90)           # SMAJ15CA
put("C1", 13.5, 33.5, 0)           # 100uF/25V
put("U2", 25.0, 30.0, 180)          # TPS54202: VIN/SW/GND face L1 (right)
put("C4", 25.0, 32.8, 0)           # bootstrap below U2
put("L1", 31.0, 30.0, 0)           # SW pad in line with U2 pin 2
put("R3", 21.3, 26.9, 0)            # FB divider next to U2 pin 4 (pad 2 = FB on the right)
put("R4", 21.3, 29.0, 180)
put("C5", 36.6, 30.0, 90)          # 22uF out at L1 output
put("C6", 39.2, 30.0, 90)
put("C7", 36.6, 34.0, 90)
put("U3", 27.0, 47.0, 0)           # TPS2116 mux
put("C8", 23.5, 46.0, 90)
put("C9", 23.5, 49.5, 90)
put("C10", 31.0, 48.0, 90)
put("R5", 27.0, 51.0, 90)          # PR1 divider
put("R6", 29.0, 51.0, 90)
put("R7", 31.5, 51.5, 90)          # ST pull-up
put("C11", 29.0, 55.0, 0)
put("C12", 33.0, 55.0, 0)
put("C13", 14.0, 46.0, 0)          # 100uF/10V on +5V
put("U4", 16.0, 58.0, 0)           # AP7361C
put("C14", 8.5, 58.0, 90)
put("C15", 23.0, 57.0, 90)
put("C16", 25.5, 57.0, 90)

# Flux interface: output drivers above J6, input buffer left
put("U8", 14.0, 68.0, 90)
put("C42", 21.0, 66.0, 90)
put("RN4", 24.5, 70.5, 0)
put("RN5", 28.5, 70.5, 0)
put("U6", 38.0, 66.0, 90)
put("U7", 52.0, 66.0, 90)
put("C40", 38.0, 60.5, 0)
put("C41", 52.0, 60.5, 0)
put("RN1", 35.0, 72.0, 0)
put("RN2", 41.0, 72.0, 0)
put("RN3", 50.0, 72.0, 0)
put("C43", 60.0, 66.0, 90)

# Amiga pull-ups
put("RN6", 80.0, 71.0, 0)

# IEC section next to the DIN socket
put("U9", 82.0, 38.0, 0)
put("U10", 82.0, 52.0, 0)
put("C44", 82.0, 31.0, 0)
put("C45", 82.0, 45.0, 0)
put("RN7", 90.0, 55.0, 0)
put("RN8", 94.0, 55.0, 0)

# ---------------------------------------------------------------------------
# USB-C fan-out, pre-routed and locked (too tight for the autorouter).
# HRO TYPE-C-31-M-12 pad row at the board edge: B8 B7 A6 A7 B6 B5 at 0.5 mm pitch,
# double VBUS pads A4/B9 (left) and A9/B4 (right).
# ---------------------------------------------------------------------------
def seg(netname, layer, pts, width):
    for (xa, ya), (xb, yb) in zip(pts, pts[1:]):
        t = pcbnew.PCB_TRACK(board)
        t.SetStart(V(xa, ya))
        t.SetEnd(V(xb, yb))
        t.SetLayer(layer)
        t.SetWidth(MM(width))
        t.SetNet(net(netname))
        t.SetLocked(True)
        board.Add(t)


def via(netname, x, y, d, drill):
    v = pcbnew.PCB_VIA(board)
    v.SetPosition(V(x, y))
    v.SetWidth(MM(d))
    v.SetDrill(MM(drill))
    v.SetNet(net(netname))
    v.SetLocked(True)
    board.Add(v)


V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731
jx = {p.GetNumber(): pcbnew.ToMM(p.GetPosition().x) for p in fps["J1"].Pads()}
jy = pcbnew.ToMM(next(p for p in fps["J1"].Pads() if p.GetNumber() == "A6").GetPosition().y)
# VBUS: via in each double pad, joined on B.Cu (plug carries VBUS on both sides)
for pad in ("A4", "A9"):
    via("VBUS", jx[pad], jy + 0.33, 0.6, 0.3)
seg("VBUS", pcbnew.B_Cu, [(jx["A4"], jy + 0.33), (jx["A9"], jy + 0.33)], 0.5)
# D-: B7 and A7 straight down to small vias, joined on B.Cu
yv = jy + 1.48
for pad in ("B7", "A7"):
    seg("USB_DM", pcbnew.F_Cu, [(jx[pad], jy), (jx[pad], yv)], 0.2)
    via("USB_DM", jx[pad], yv, 0.48, 0.25)
seg("USB_DM", pcbnew.B_Cu, [(jx["B7"], yv), (jx["A7"], yv)], 0.2)
# D+: A6 passes between the D- vias, B6 joins below them on F.Cu
yd = jy + 2.18
seg("USB_DP", pcbnew.F_Cu, [(jx["A6"], jy), (jx["A6"], yd), (jx["B6"], yd), (jx["B6"], jy)], 0.2)

# Buck converter hot paths, pre-routed and locked (U2 rotated: VIN/SW/GND face L1).
def net_pad(ref, netname):
    return next(pcbnew.ToMM(p.GetPosition().x) for p in fps[ref].Pads() if p.GetNetname() == netname), \
        next(pcbnew.ToMM(p.GetPosition().y) for p in fps[ref].Pads() if p.GetNetname() == netname)


u2_vin, u2_sw, u2_fb, u2_bst = (pad_pos("U2", n) for n in ("3", "2", "4", "6"))
l1_sw = net_pad("L1", "/Power/BUCK_SW")
seg("/Power/BUCK_SW", pcbnew.F_Cu, [u2_sw, (l1_sw[0], u2_sw[1])], 0.6)            # 2.8 mm SW node
c4_sw, c4_bst = net_pad("C4", "/Power/BUCK_SW"), net_pad("C4", "/Power/BUCK_BST")
seg("/Power/BUCK_SW", pcbnew.F_Cu, [c4_sw, (u2_sw[0] + 1.26, u2_sw[1] + 1.2), (u2_sw[0] + 1.26, u2_sw[1])], 0.4)
seg("/Power/BUCK_BST", pcbnew.F_Cu, [u2_bst, c4_bst], 0.3)
c3_vin, c2_vin = net_pad("C3", "+12V"), net_pad("C2", "+12V")
seg("+12V", pcbnew.F_Cu, [c3_vin, u2_vin], 0.4)                                  # input cap 1.8 mm
seg("+12V", pcbnew.F_Cu, [c2_vin, c3_vin], 0.4)
r4_fb, r3_fb = net_pad("R4", "/Power/BUCK_FB"), net_pad("R3", "/Power/BUCK_FB")
seg("/Power/BUCK_FB", pcbnew.F_Cu, [r3_fb, r4_fb, u2_fb], 0.2)

# HSE crystal, pre-routed and locked: no vias, both lines a few mm long.
y1_in, y1_out = pad_pos("Y1", "1"), pad_pos("Y1", "3")
ph0, ph1 = pad_pos("U5", "23"), pad_pos("U5", "24")
seg("/MCU_Core/HSE_IN", pcbnew.F_Cu, [net_pad("C38", "/MCU_Core/HSE_IN"), y1_in], 0.2)
seg("/MCU_Core/HSE_IN", pcbnew.F_Cu,
    [y1_in, (y1_in[0] + 0.5, 40.4), (42.2, 40.4), (43.2, ph0[1]), ph0], 0.2)
seg("/MCU_Core/HSE_OUT", pcbnew.F_Cu, [y1_out, (41.6, ph1[1]), ph1], 0.2)
c39_out = net_pad("C39", "/MCU_Core/HSE_OUT")
seg("/MCU_Core/HSE_OUT", pcbnew.F_Cu,
    [c39_out, (c39_out[0] + 0.425, 44.85), (y1_out[0] - 0.45, 44.85), y1_out], 0.2)

# MCU VSS pins: short locked stub inwards under the LQFP body to a small GND via.
# Pre-routed so the escape routing cannot box the ground pins in.
for p in fps["U5"].Pads():
    if p.GetNetname() != "GND":
        continue
    px, py = pcbnew.ToMM(p.GetPosition().x), pcbnew.ToMM(p.GetPosition().y)
    dx, dy = px - MCU[0], py - MCU[1]
    ux, uy = (-math.copysign(1, dx), 0.0) if abs(dx) > abs(dy) else (0.0, -math.copysign(1, dy))
    bb = p.GetBoundingBox()
    half = max(pcbnew.ToMM(bb.GetWidth()), pcbnew.ToMM(bb.GetHeight())) / 2
    vx, vy = px + ux * (half + 1.0), py + uy * (half + 1.0)
    seg("GND", pcbnew.F_Cu, [(px, py), (vx, vy)], 0.25)
    via("GND", vx, vy, 0.48, 0.25)

# Mounting holes (M3, plated) - added as board items, not in the schematic
for i, (hx, hy) in enumerate([(3.5, 3.5), (W - 3.5, 3.5), (3.5, H - 14.0), (W - 3.5, H - 14.0)], 1):
    h = pcbnew.FootprintLoad(str(STD / "MountingHole.pretty"), "MountingHole_3.2mm_M3_Pad_Via")
    h.SetReference(f"H{i}")
    h.SetValue("M3")
    h.Reference().SetVisible(False)
    h.SetPosition(pcbnew.VECTOR2I(MM(hx), MM(hy)))
    h.SetBoardOnly(True)
    board.Add(h)
    for p in h.Pads():
        p.SetNet(net("GND"))

# ---------------------------------------------------------------------------
# outline, planes, silkscreen
# ---------------------------------------------------------------------------
def edge(shape, **kw):
    s = pcbnew.PCB_SHAPE(board, shape)
    for k, v in kw.items():
        getattr(s, k)(*v) if isinstance(v, tuple) else getattr(s, k)(v)
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)


V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731
R = CORNER
for a, b in (((R, 0), (W - R, 0)), ((W, R), (W, H - R)), ((W - R, H), (R, H)), ((0, H - R), (0, R))):
    edge(pcbnew.SHAPE_T_SEGMENT, SetStart=V(*a), SetEnd=V(*b))
for c, s in (((R, R), (0, R)), ((W - R, R), (W - R, 0)), ((W - R, H - R), (W, H - R)), ((R, H - R), (R, H))):
    a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    a.SetCenter(V(*c))
    a.SetStart(V(*s))
    a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T), True)
    a.SetLayer(pcbnew.Edge_Cuts)
    a.SetWidth(MM(0.1))
    board.Add(a)


def zone(layer, netname, prio=0):
    z = pcbnew.ZONE(board)
    z.SetLayer(layer)
    z.SetNet(net(netname))
    z.SetLocalClearance(MM(0.25))
    z.SetMinThickness(MM(0.2))
    z.SetAssignedPriority(prio)
    z.SetPadConnection(pcbnew.ZONE_CONNECTION_THERMAL)
    ol = z.Outline()
    ol.NewOutline()
    for x, y in ((0.3, 0.3), (W - 0.3, 0.3), (W - 0.3, H - 0.3), (0.3, H - 0.3)):
        ol.Append(MM(x), MM(y))
    board.Add(z)


zone(pcbnew.In1_Cu, "GND")
# In2 +3V3 pour is added after routing by finish_pcb.py
# outer GND pours are added after routing by finish_pcb.py


def text(s, x, y, size=1.0, layer=pcbnew.F_SilkS):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(x, y))
    t.SetTextSize(V(size, size))
    t.SetTextThickness(MM(size * 0.15))
    t.SetLayer(layer)
    t.SetMirrored(layer == pcbnew.B_SilkS)
    board.Add(t)


text("UFI Headless v0.1", 55.0, 26.0 - 2.0, 1.2, pcbnew.B_SilkS)
text("FDD 34", 30.0, H - 11.5, 1.0)
text("AMIGA", 84.0, H - 11.5, 1.0)
text("IEC 1=SRQ 6=RST", W - 9.5, 31.0, 0.8)
text("12V DC", 24.0, 3.0, 1.0)
text("FDD PWR", 6.0, 60.5, 0.8)

board.Save(OUT)
print("saved", OUT, "footprints", len(fps), "nets", board.GetNetCount())
