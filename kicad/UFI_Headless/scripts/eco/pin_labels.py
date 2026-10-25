"""Per-pin silkscreen labels for the internal headers of UFI Headless v1.0.

Every label is a board text (h 0.8 mm, stroke 0.15 mm; 0.7 mm for the 1.27-mm SWD header) placed
next to its pin outside the connector outline: 1xN headers get horizontal labels beside the pins,
2xN headers get labels rotated by 90 deg above / below the pin rows.  Each label is checked
against pads, silkscreen graphics, reference fields and other texts of that layer and against
the board edge; if the preferred side is blocked the other side is tried, then the same spot on
the bottom silkscreen (mirrored, readable from the solder side).  Labels are grouped per header
("pinlabels REF"), a re-run replaces them.  Old free texts that the labels make redundant are
removed (REMOVE).
usage: pin_labels.py <board> [check] [REF ...]
"""
import sys

import pcbnew

MM, T = pcbnew.FromMM, pcbnew.ToMM
H, STROKE, GAP, MARGIN, EDGE = 0.8, 0.15, 0.4, 0.2, 0.3

# REF: (preferred side for 1xN or for the row order of 2xN, {pin: label}, text height)
# side letters: R/L = horizontal labels right/left of the pin (1xN, or 2xN with vertical pin
# columns: L for the left column, R for the right); T/B = vertical labels above/below the rows
# (2xN with horizontal rows: T for the upper row, B for the lower row).
LABELS = {
    "J8":  ("R", {1: "SRQ", 2: "GND", 3: "ATN", 4: "CLK", 5: "DATA", 6: "RST"}, H),
    "J20": ("R", {1: "GND", 2: "OFF", 3: "ERR5", 4: "ERR12"}, H),
    "J17": ("R", {1: "5V", 2: "GND", 3: "GND", 4: "12V"}, H),   # B3: JST XH B4B (A9a), pins 2.5 mm apart
    "J3":  ("R", {1: "5V", 2: "GND", 3: "GND", 4: "12V"}, H),   # B3: JST XH B4B in the J6/J7 pocket
    "J19": ("L", {1: "5V", 2: "GND", 3: "SYNC"}, H),            # B3: JST XH B3B, pin 3 = SYNC_5V (A11d)
    # J23 Mac 3.5" port (A11d part 2): Apple 20-pin pinout, 1/3/5/7 GND, 9 -12 V only via JP6,
    # 20 CSTOUT (PWM only via JP5)
    "J23": ("TB", {2: "CA0", 4: "CA1", 6: "CA2", 8: "LSTB", 10: "WREQ", 12: "SEL", 14: "ENBL", 16: "RD", 18: "WR", 20: "CSTO",
                   1: "GND", 3: "GND", 5: "GND", 7: "GND", 9: "-12V", 11: "5V", 13: "12V", 15: "12V", 17: "12V", 19: "12V"}, H),
    "J5":  ("R", {1: "TX", 2: "RX", 3: "GND"}, H),
    "J4":  ("LR", {1: "3V3", 3: "GND", 5: "GND", 7: "KEY", 9: "GND",
                   2: "SWDIO", 4: "SWCLK", 6: "SWO", 8: "NC", 10: "RST"}, H),   # board rule: text height >= 0.8 mm
    "J12": ("LR", {1: "3V3", 3: "ACT", 5: "ERR", 7: "USB", 9: "FDD", 11: "PWR", 13: "BT-A", 15: "MOD1"}, H),
    # J12 right column (2-12 GND, 14 BTN_B, 16 MODE2): 2.5 mm to the board edge -> vertical legend, see LEGENDS
    "J21": ("LR", {1: "RDAT", 3: "WDAT", 5: "IDX", 7: "WGAT", 9: "STEP", 11: "DIR", 13: "SIDE", 15: "DSA", 17: "MOTA", 19: "TRK0",
                   2: "WPRT", 4: "DCHG", 6: "RDY", 8: "DENS", 10: "ISIM", 12: "LOOP", 14: "WGSN", 16: "GND", 18: "GND", 20: "GND"}, H),
    "J6":  ("TB", {2: "DENS", 6: "P6", 8: "IDX", 10: "MOTA", 12: "DSB", 14: "DSA", 16: "MOTB", 18: "DIR", 20: "STEP",
                   22: "WDAT", 24: "WGAT", 26: "TRK0", 28: "WPRT", 30: "RDAT", 32: "SIDE", 34: "DCHG",
                   1: "GND", 3: "P3"}, H),
    "J7":  ("TB", {2: "RDAT", 4: "GND", 6: "GND", 8: "MOTB", 10: "DRES", 12: "5V", 14: "WPRT", 16: "WGAT", 18: "STEP", 20: "SEL3", 22: "IDX", 24: "GND",
                   1: "RDY", 3: "GND", 5: "GND", 7: "GND", 9: "SEL2", 11: "DCHG", 13: "SIDE", 15: "TRK0", 17: "WDAT", 19: "DIR", 21: "DSB", 23: "12V"}, H),
    "J14": ("TB", {2: "PH0", 4: "PH1", 6: "PH2", 8: "PH3", 10: "WREQ", 12: "5V", 14: "EN1", 16: "RDAT", 18: "WDAT", 20: "WPRT",
                   1: "GND", 3: "GND", 5: "GND", 7: "GND", 9: "-12V", 11: "5V", 13: "12V", 15: "12V", 17: "12V", 19: "12V"}, H),
}
MARGINS = {"J4": 0.1}   # 1.27-mm pitch: 0.7-mm labels with 0.1 mm between them
# pins without room for a label beside them: one legend text per header at a fixed spot
# (text, x, y, angle, layer); J12's right column has 2.5 mm to the board edge, the front outline
# fills it, so the legend sits on the back next to the solder pins
LEGENDS = {"J12": ("2-8 GND  10 WRLED  12 LOCK  14 BT-B  16 MOD2", 108.3, 36.9, 90, pcbnew.B_SilkS)}
# free texts that the per-pin labels replace: (text, x, y)
REMOVE = [("IEC 1=SRQ 6=RST", 9.0, 38.6), ("+5V", 4.5, 7.75), ("+12V", 2.15, 19.35), ("EN2", 9.5, 93.25),
          ("GND", 17.69, 86.23), ("GND", 27.76, 91.04), ("+5V", 33.4, 88.5)]

args = sys.argv[1:]
check = "check" in args
args = [a for a in args if a != "check"]
board = pcbnew.LoadBoard(args[0])
only = args[1:] or list(LABELS)
present = {f.GetReference() for f in board.GetFootprints()}  # B3: label groups of removed headers (J15) go too
edge = board.GetBoardEdgesBoundingBox()
edge_in = pcbnew.BOX2I(edge.GetPosition(), edge.GetSize())
edge_in.Inflate(-MM(EDGE))


def grow(bb, d):
    b = pcbnew.BOX2I(bb.GetPosition(), bb.GetSize())
    b.Inflate(d)
    return b


def remove_old():
    """Called last: pcbnew crashes when the board is walked again after Remove()."""
    n = 0
    for d in list(board.GetDrawings()):
        if doomed(d):
            board.Remove(d)
            n += 1
    for g in list(board.Groups()):
        if g.GetName().startswith("pinlabels ") and (g.GetName()[10:] in only or g.GetName()[10:] not in present):
            board.Remove(g)
    return n


def doomed(d):
    """Old label texts (REMOVE list) and the previous labels of the headers being redone."""
    if d.GetClass() != "PCB_TEXT":
        return False
    g = d.GetParentGroup()
    if g is not None and g.GetName().startswith("pinlabels ") and (g.GetName()[10:] in only or g.GetName()[10:] not in present):
        return True
    return any(d.GetText() == txt and abs(T(d.GetPosition().x) - x) < 0.3 and abs(T(d.GetPosition().y) - y) < 0.3
               for txt, x, y in REMOVE)


def obstacles(layer):
    out = []
    cu = pcbnew.F_Cu if layer == pcbnew.F_SilkS else pcbnew.B_Cu
    for fp in board.GetFootprints():
        for p in fp.Pads():
            if p.IsOnLayer(cu):   # SMD pads of the other side do not block this silk layer
                out.append((grow(p.GetBoundingBox(), MM(0.1)), "pad %s.%s" % (fp.GetReference(), p.GetNumber())))
        for g in fp.GraphicalItems():
            if g.GetLayer() == layer:
                out.append((g.GetBoundingBox(), "silk %s" % fp.GetReference()))
        for f in fp.GetFields():
            if f.IsVisible() and f.GetLayer() == layer:
                out.append((f.GetBoundingBox(), "ref %s" % fp.GetReference()))
    for d in board.GetDrawings():
        if d.GetLayer() == layer and not doomed(d):
            out.append((d.GetBoundingBox(), "text %s" % (d.GetText() if d.GetClass() == "PCB_TEXT" else d.GetClass())))
    return out


def tight_box(t):
    """KiCad's text box is about 1.8 x the nominal height (interline space); for collisions use
    the glyph extent: nominal height x 1.2, centred, along the text's vertical axis."""
    bb = t.GetBoundingBox()
    b = pcbnew.BOX2I(bb.GetPosition(), bb.GetSize())
    g = int(t.GetTextHeight() * 1.2)
    if abs(t.GetTextAngleDegrees()) % 180 < 45:
        cy = bb.GetCenter().y
        b.SetY(cy - g // 2)
        b.SetHeight(g)
    else:
        cx = bb.GetCenter().x
        b.SetX(cx - g // 2)
        b.SetWidth(g)
    return b


def blocker(t, obs, margin=MARGIN):
    bb = grow(tight_box(t), MM(margin))
    if not edge_in.Contains(bb):
        return "edge"
    for obb, who in obs:
        if obb.Intersects(bb):
            return who
    return None


def make_text(txt, h, angle, layer):
    t = pcbnew.PCB_TEXT(board)
    t.SetText(txt)
    t.SetLayer(layer)
    t.SetTextHeight(MM(h))
    t.SetTextWidth(MM(h))
    t.SetTextThickness(MM(STROKE))
    t.SetTextAngleDegrees(angle)
    t.SetMirrored(layer == pcbnew.B_SilkS)
    t.SetHorizJustify(pcbnew.GR_TEXT_H_ALIGN_CENTER)
    t.SetVertJustify(pcbnew.GR_TEXT_V_ALIGN_CENTER)
    return t


def put(t, pad, side, outline):
    """Position t beside pad (R/L: horizontal, beside the pad; T/B: vertical).  On the front the
    label stays outside the connector outline (the body covers it), on the back only the pad counts."""
    t.SetPosition(pad.GetPosition())
    bb = tight_box(t)
    w, h = bb.GetWidth(), bb.GetHeight()
    pb = pad.GetBoundingBox()
    if t.GetLayer() == pcbnew.B_SilkS or outline is None:
        outline = pb
    x, y = pad.GetPosition().x, pad.GetPosition().y
    if side == "R":
        x = max(pb.GetRight(), outline.GetRight()) + MM(GAP) + w // 2
    elif side == "L":
        x = min(pb.GetLeft(), outline.GetLeft()) - MM(GAP) - w // 2
    elif side == "T":
        y = min(pb.GetTop(), outline.GetTop()) - MM(GAP) - h // 2
    elif side == "B":
        y = max(pb.GetBottom(), outline.GetBottom()) + MM(GAP) + h // 2
    t.SetPosition(pcbnew.VECTOR2I(int(x), int(y)))


obs = {pcbnew.F_SilkS: obstacles(pcbnew.F_SilkS), pcbnew.B_SilkS: obstacles(pcbnew.B_SilkS)}
placed, failed, new = 0, [], []
for ref in only:
    sides, table, h = LABELS[ref]
    fp = board.FindFootprintByReference(ref)
    if fp is None:
        print(ref, "missing")
        continue
    outline = None
    for g in fp.GraphicalItems():
        if g.GetLayer() == pcbnew.F_SilkS:
            outline = g.GetBoundingBox() if outline is None else outline
            outline.Merge(g.GetBoundingBox())
    group = pcbnew.PCB_GROUP(board)
    group.SetName("pinlabels " + ref)
    items = []
    pads = {int(p.GetNumber()): p for p in fp.Pads() if p.GetNumber().isdigit()}
    xs = sorted(set(round(T(p.GetPosition().x), 2) for p in pads.values()))
    ys = sorted(set(round(T(p.GetPosition().y), 2) for p in pads.values()))
    margin = MARGINS.get(ref, MARGIN)
    for num, txt in sorted(table.items()):
        pad = pads[num]
        txt, h = txt if isinstance(txt, tuple) else (txt, h)
        if sides in ("R", "L"):
            order = [sides, "L" if sides == "R" else "R"]
            angle = 0
        elif sides == "LR":      # two vertical columns: left column -> left, right column -> right
            col = "L" if round(T(pad.GetPosition().x), 2) == xs[0] else "R"
            order = [col, "R" if col == "L" else "L"]
            angle = 0
        else:                    # two horizontal rows: upper row -> above, lower row -> below
            row = "T" if round(T(pad.GetPosition().y), 2) == ys[0] else "B"
            order = [row, "B" if row == "T" else "T"]
            angle = 90
        done, why = None, []
        for layer in (pcbnew.F_SilkS, pcbnew.B_SilkS):
            for side in order:
                t = make_text(txt, h, angle, layer)
                put(t, pad, side, outline)
                b = blocker(t, obs[layer], margin)
                if b is None:
                    done = (t, side, layer)
                    break
                why.append("%s%s:%s" % (board.GetLayerName(layer)[0], side, b))
            if done:
                break
        if done is None:
            failed.append("%s.%d %s (%s)" % (ref, num, txt, ", ".join(why)))
            continue
        t, side, layer = done
        items.append(t)
        obs[layer].append((tight_box(t), "label %s.%d" % (ref, num)))
        placed += 1
        print("%s.%-3d %-6s %s %s (%.2f,%.2f)" % (ref, num, txt, side, board.GetLayerName(layer)[:6], T(t.GetPosition().x), T(t.GetPosition().y)))
    if ref in LEGENDS:
        txt, x, y, ang, lay = LEGENDS[ref]
        t = make_text(txt, H, ang, lay)
        t.SetPosition(pcbnew.VECTOR2I(MM(x), MM(y)))
        b = blocker(t, obs[lay], margin)
        if b is None:
            items.append(t)
            obs[lay].append((tight_box(t), "legend %s" % ref))
            print("%s legend %r at (%.2f,%.2f)" % (ref, txt, x, y))
        else:
            failed.append("%s legend %r (%s)" % (ref, txt, b))
    new.append((group, items))
for f in failed:
    print("FAIL", f)
print("placed", placed, "failed", len(failed))
if not check:
    removed = remove_old()
    for group, items in new:
        if items:
            board.Add(group)
            for t in items:
                board.Add(t)
                group.AddItem(t)
    board.Save(args[0])
    print("saved, old texts removed", removed)
