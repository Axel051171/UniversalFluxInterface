"""Create C64_PSU_Guard.kicad_pcb from the schematic netlist: footprints, nets, placement,
outline, mounting holes, silkscreen.  Routing is done afterwards by Freerouting, the GND
pours are added by finish_pcb.py (see make_pcb.sh).

usage: python build_pcb.py <netlist.net> <out.kicad_pcb>
"""
from __future__ import annotations

import sys
from pathlib import Path

import pcbnew

sys.path.insert(0, str(Path(__file__).resolve().parent))
from netlist import read_netlist  # noqa: E402

STD = Path(r"C:\Program Files\KiCad\10.0\share\kicad\footprints")
MM = pcbnew.FromMM
W, H, CORNER = 58.0, 44.0, 2.0

comps, nets = read_netlist(sys.argv[1])
OUT = sys.argv[2]

board = pcbnew.BOARD()
ds = board.GetDesignSettings()
ds.SetCopperLayerCount(2)
nc = ds.m_NetSettings.GetDefaultNetclass()
nc.SetClearance(MM(0.2))
nc.SetTrackWidth(MM(0.25))
nc.SetViaDiameter(MM(0.6))
nc.SetViaDrill(MM(0.3))
ds.m_TrackMinWidth = MM(0.2)
ds.m_MinClearance = MM(0.2)
ds.m_ViasMinSize = MM(0.5)
ds.m_MinThroughDrill = MM(0.3)
ds.m_CopperEdgeClearance = MM(0.3)

# supply nets carry up to 1.5 A: 1.0 mm tracks (Freerouting reads the classes from the DSN)
NET_CLASSES = {
    "Power": (1.0, 0.2, ["+5V_IN", "+12V_IN", "V5_P", "V12_P", "+5V_OUT", "+12V_OUT"]),
    "Supply": (0.5, 0.2, ["GND"]),
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

netinfo: dict[str, pcbnew.NETINFO_ITEM] = {}


def net(name: str):
    if name not in netinfo:
        n = pcbnew.NETINFO_ITEM(board, name)
        board.Add(n)
        netinfo[name] = n
    return netinfo[name]


fps: dict[str, pcbnew.FOOTPRINT] = {}
for ref, c in sorted(comps.items()):
    lib, name = c["footprint"].split(":")
    fp = pcbnew.FootprintLoad(str(STD / f"{lib}.pretty"), name)
    assert fp is not None, c["footprint"]
    fp.SetFPID(pcbnew.LIB_ID(lib, name))
    fp.SetReference(ref)
    fp.SetValue(c["value"])
    fp.SetPath(pcbnew.KIID_PATH(c["path"]))
    for k, v in c["props"].items():
        if k not in ("Reference", "Value", "Footprint") and not k.startswith("ki_"):
            fp.SetField(k, v)
    for f in fp.GetFields():
        if f.GetName() != "Reference":
            f.SetVisible(False)
    if "Description" not in c["props"]:
        fp.GetField(pcbnew.FIELD_T_DESCRIPTION).SetText("")
    if c["datasheet"] not in ("", "~"):
        fp.GetField(pcbnew.FIELD_T_DATASHEET).SetText(c["datasheet"])
    fp.Reference().SetTextSize(pcbnew.VECTOR2I(MM(0.8), MM(0.8)))
    fp.Reference().SetTextThickness(MM(0.12))
    if ref.rstrip("0123456789") in ("C", "R"):
        fp.Reference().SetVisible(False)
    board.Add(fp)
    fps[ref] = fp

pad_net: dict[tuple[str, str], str] = {}
for name, nodes in nets.items():
    for ref, pin in nodes:
        pad_net[(ref, pin)] = name
for ref, fp in fps.items():
    for p in fp.Pads():
        n = pad_net.get((ref, p.GetNumber()))
        if n:
            p.SetNet(net(n))


def courtyard(fp):
    cy = fp.GetCourtyard(pcbnew.F_CrtYd)
    return cy.BBox() if cy.OutlineCount() else fp.GetBoundingBox(False)


def put(ref, cx, cy, rot=0.0):
    """Place a footprint so that its courtyard centre lands on (cx, cy)."""
    fp = fps[ref]
    fp.SetOrientationDegrees(rot)
    fp.SetPosition(pcbnew.VECTOR2I(0, 0))
    c = courtyard(fp).GetCenter()
    fp.SetPosition(pcbnew.VECTOR2I(MM(cx) - c.x, MM(cy) - c.y))
    return fp


def put_edge(ref, rot, edge, along):
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


# ---------------------------------------------------------------------------
# placement (x right, y down, origin top-left): 5 V channel in the upper half,
# 12 V channel in the lower half, signal flow left (PSU) -> right (drive)
# ---------------------------------------------------------------------------
put_edge("J1", 0, "left", 21.0)          # PSU in, pin 1 (+5V) at the top
put_edge("J3", 90, "bottom", 20.0)       # UFI link, pin 1 (GND) at the left
# terminal block on the right edge, wire entry outwards: rotation 90 (checked with the Phoenix
# MKDS 3D model of the same footprint generator; 270 puts the openings towards the board centre).
# Pin 1 (+5 V) is then the lowest terminal, the silkscreen labels follow the pads.
put_edge("J2", 90, "right", 22.0)
fps["J2"].Move(pcbnew.VECTOR2I(MM(-0.7), 0))        # pin-1 silk marker clear of the board edge

CH = {"5": dict(y0=11.0, q="Q1", rg="R1", dz="D1", tvs="D2", cb="C1", cs="C2", u="U1", ren="R2", ren2="R3",
                rov="R4", rov2="R5", css="C3", ril="R6", co="C4", rg_led="R7", dg="D3", rr_led="R8", dr="D4",
                qoff="Q2", led_y=4.0),
      "12": dict(y0=31.0, q="Q3", rg="R9", dz="D5", tvs="D6", cb="C5", cs="C6", u="U2", ren="R10", ren2="R11",
                 rov="R12", rov2="R13", css="C7", ril="R14", co="C8", rg_led="R15", dg="D7", rr_led="R16", dr="D8",
                 qoff="Q4", led_y=40.0)}
for v, c in CH.items():
    y0, m = c["y0"], (-1 if v == "5" else 1)          # m: towards the outer (LED) edge
    put(c["q"], 12.0, y0, 180)                         # SOT-23: drain (pad 3) towards J1, gate (pad 1) on the inner side
    put(c["rg"], 10.0, y0 - m * 4.0, 0)                # gate parts on the gate side, clear of the wide source track
    put(c["dz"], 15.0, y0 - m * 4.0, 0)
    put(c["tvs"], 18.0, y0, 0)
    put(c["cb"], 23.0, y0, 90)
    put(c["cs"], 25.5, y0, 90)
    put(c["u"], 31.0, y0, 0)                           # pin 1 GND top-left, 4 IN bottom-left, 5 OUT bottom-right
    put(c["css"], 27.0, y0 - 4.5, 0)
    put(c["rov"], 30.0, y0 - 4.5, 0)
    put(c["rov2"], 33.0, y0 - 4.5, 0)
    put(c["ril"], 36.5, y0 - 2.5, 90)
    put(c["ren"], 27.0, y0 + 4.5, 0)
    put(c["ren2"], 30.0, y0 + 4.5, 0)
    put(c["qoff"], 33.5, y0 + 4.5, 0)
    put(c["co"], 36.5, y0 + 2.0, 90)
    put(c["rg_led"], 39.5, c["led_y"], 0)
    put(c["dg"], 42.5, c["led_y"], 180)                # LED anode (pad 2) next to the resistor
    put(c["rr_led"], 46.0, c["led_y"], 0)
    put(c["dr"], 49.0, c["led_y"], 180)
# reverse-polarity indicator between the two channels, next to J1
put("R17", 7.5, 21.0, 0)
put("D9", 10.5, 21.0, 180)
put("D10", 14.5, 19.0, 0)
put("D11", 14.5, 23.0, 0)
put("R18", 27.5, 41.5, 0)                              # OFF pull-down next to J3

# silkscreen references: only ICs, connectors and LEDs, placed clear of the pads
for ref, fp in fps.items():
    kind = ref.rstrip("0123456789")
    if kind in ("Q",) or (kind == "D" and comps[ref]["value"] not in ("green", "red")):
        fp.Reference().SetVisible(False)
        continue
    if not fp.Reference().IsVisible():
        continue
    bb = courtyard(fp)
    cx, cy = pcbnew.ToMM(bb.GetCenter().x), pcbnew.ToMM(bb.GetCenter().y)
    half = pcbnew.ToMM(bb.GetHeight()) / 2
    below = cy < H / 2 and kind != "J"
    fp.Reference().SetTextAngleDegrees(0)
    fp.Reference().SetPosition(pcbnew.VECTOR2I(MM(cx), MM(cy + (half + 0.7) * (1 if below else -1))))

# mounting holes (M3, plated, GND)
for i, (hx, hy) in enumerate([(3.5, 3.5), (W - 3.5, 3.5), (3.5, H - 3.5), (W - 3.5, H - 3.5)], 1):
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
# outline and silkscreen
# ---------------------------------------------------------------------------
V = lambda x, y: pcbnew.VECTOR2I(MM(x), MM(y))  # noqa: E731
R = CORNER
for a, b in (((R, 0), (W - R, 0)), ((W, R), (W, H - R)), ((W - R, H), (R, H)), ((0, H - R), (0, R))):
    s = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_SEGMENT)
    s.SetStart(V(*a))
    s.SetEnd(V(*b))
    s.SetLayer(pcbnew.Edge_Cuts)
    s.SetWidth(MM(0.1))
    board.Add(s)
for c, s0 in (((R, R), (0, R)), ((W - R, R), (W - R, 0)), ((W - R, H - R), (W, H - R)), ((R, H - R), (R, H))):
    a = pcbnew.PCB_SHAPE(board, pcbnew.SHAPE_T_ARC)
    a.SetCenter(V(*c))
    a.SetStart(V(*s0))
    a.SetArcAngleAndEnd(pcbnew.EDA_ANGLE(90, pcbnew.DEGREES_T), True)
    a.SetLayer(pcbnew.Edge_Cuts)
    a.SetWidth(MM(0.1))
    board.Add(a)


def text(s, x, y, size=0.8, layer=pcbnew.F_SilkS, rot=0):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(s)
    t.SetPosition(V(x, y))
    t.SetTextSize(V(size, size))
    t.SetTextThickness(MM(size * 0.15))
    t.SetLayer(layer)
    t.SetMirrored(layer == pcbnew.B_SilkS)
    t.SetTextAngleDegrees(rot)
    board.Add(t)


text("C64 PSU Guard v0.1", W / 2, H / 2, 1.2, pcbnew.B_SilkS)
text("PSU IN  1=5V  2,3=GND  4=12V", 17.5, 17.3, 0.8)
for num, lbl in (("1", "5V"), ("2", "GND"), ("3", "GND"), ("4", "12V")):
    text(lbl, pad_pos("J1", num)[0] + 3.2, pad_pos("J1", num)[1], 0.8)
text("UFI  1=GND  2=OFF  3=F5  4=F12", 20.0, 38.6, 0.8)
for num, lbl in (("1", "5V  DIN1"), ("2", "GND DIN2"), ("3", "12V DIN4")):
    text(lbl, pad_pos("J2", num)[0] - 9.5, pad_pos("J2", num)[1], 0.8)
text("5V OK", 42.5, 6.6, 0.8)
text("5V ERR", 49.0, 6.6, 0.8)
text("12V OK", 42.5, 37.4, 0.8)
text("12V ERR", 49.0, 37.4, 0.8)
text("REV POL", 10.5, 25.0, 0.8)

board.Save(OUT)
print("saved", OUT, "footprints", len(fps), "nets", board.GetNetCount())
