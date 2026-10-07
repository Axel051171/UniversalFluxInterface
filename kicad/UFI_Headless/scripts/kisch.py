"""Minimal KiCad schematic writer.

Embeds symbols from the installed KiCad standard libraries (flattening derived
symbols), places instances and connects every pin through a short wire stub to
a power symbol, local label, global label or no-connect flag.  Electrical
connectivity is therefore fully defined by net names; the drawing is a clean
starting point that can be tidied up manually in Eeschema.
"""
from __future__ import annotations

import copy
import os
import re
import uuid as _uuid
from pathlib import Path

KICAD_SYM_DIR = Path(os.environ.get(
    "KICAD_SYMBOL_DIR", "C:/Program Files/KiCad/10.0/share/kicad/symbols"))
PROJECT_SYM_DIR = Path(__file__).resolve().parent.parent / "lib"
FORMAT_VERSION = "20231120"
STUB = 2.54
# Nets that are rendered as power symbols from the 'power' library
POWER_NETS = {"GND", "+3V3", "+5V", "+12V", "VBUS"}


class Q(str):
    """Quoted string atom (raw content, escapes preserved)."""


_TOKEN = re.compile(r'\(|\)|"(?:[^"\\]|\\.)*"|[^\s()]+')


def parse(text: str):
    stack: list[list] = [[]]
    for tok in _TOKEN.findall(text):
        if tok == "(":
            stack.append([])
        elif tok == ")":
            done = stack.pop()
            stack[-1].append(done)
        elif tok.startswith('"'):
            stack[-1].append(Q(tok[1:-1]))
        else:
            stack[-1].append(tok)
    return stack[0][0]


def dump(node, indent: int = 0) -> str:
    if isinstance(node, Q):
        return f'"{node}"'
    if not isinstance(node, list):
        return str(node)
    if all(not isinstance(c, list) for c in node):
        return "(" + " ".join(dump(c) for c in node) + ")"
    pad = "\t" * (indent + 1)
    head = [dump(c) for c in node if not isinstance(c, list)]
    out = "(" + " ".join(head)
    for c in node:
        if isinstance(c, list):
            out += "\n" + pad + dump(c, indent + 1)
    return out + "\n" + "\t" * indent + ")"


def child(node, key):
    for c in node:
        if isinstance(c, list) and c and c[0] == key:
            return c
    return None


_LIB_CACHE: dict[str, list] = {}


def _lib(lib: str):
    if lib not in _LIB_CACHE:
        # project-local library (kicad/UFI_Headless/lib) first, then the KiCad standard libraries
        local = PROJECT_SYM_DIR / f"{lib}.kicad_sym"
        path = local if local.exists() else KICAD_SYM_DIR / f"{lib}.kicad_sym"
        _LIB_CACHE[lib] = parse(path.read_text(encoding="utf8"))
    return _LIB_CACHE[lib]


def lib_symbol(lib: str, name: str):
    """Return a flattened copy of library symbol `name` (derived symbols resolved)."""
    for s in _lib(lib):
        if isinstance(s, list) and s[0] == "symbol" and s[1] == name:
            break
    else:
        raise KeyError(f"{lib}:{name} not found")
    ext = child(s, "extends")
    if not ext:
        return copy.deepcopy(s)
    base = lib_symbol(lib, ext[1])
    props = [c for c in s if isinstance(c, list) and c[0] == "property"]
    # 'ki_locked' is a symbol attribute that derived symbols inherit from the base
    props += [c for c in base if isinstance(c, list) and c[0] == "property" and c[1] == "ki_locked"]
    body = [c for c in base[2:] if not (isinstance(c, list) and c[0] in ("property", "extends"))]
    for c in body:
        if isinstance(c, list) and c[0] == "symbol":
            c[1] = Q(str(c[1]).replace(str(ext[1]), name, 1))
    first_sub = next(i for i, c in enumerate(body) if isinstance(c, list) and c[0] == "symbol")
    return ["symbol", Q(name)] + body[:first_sub] + props + body[first_sub:]


def symbol_pins(sym, unit: int | None = None) -> list[dict]:
    """Pins of `sym`; with `unit` only that unit's pins plus the common ones."""
    pins = []
    for sub in sym:
        if not (isinstance(sub, list) and sub[0] == "symbol"):
            continue
        sub_unit = int(str(sub[1]).rsplit("_", 2)[-2])  # "<name>_<unit>_<style>", 0 = all units
        if unit is not None and sub_unit not in (0, unit):
            continue
        for p in sub:
            if isinstance(p, list) and p[0] == "pin":
                at = child(p, "at")
                pins.append({
                    "type": p[1],
                    "x": float(at[1]), "y": float(at[2]), "a": int(float(at[3])),
                    "name": str(child(p, "name")[1]),
                    "number": str(child(p, "number")[1]),
                })
    return pins


_NS = _uuid.UUID("6f1c2b1e-5a4d-4e0f-9a51-7b0c3e2d9f10")


def uid(key: str | None = None) -> str:
    """Deterministic UUID for `key` (stable across regenerations), random without key."""
    return str(_uuid.uuid5(_NS, key)) if key else str(_uuid.uuid4())


def snap(v: float, grid: float = 2.54) -> float:
    """Snap a coordinate to the schematic connection grid."""
    return round(v / grid) * grid


def fmt(v: float) -> str:
    s = f"{round(v, 4):.4f}".rstrip("0").rstrip(".")
    return "0" if s in ("-0", "") else s


# outward stub direction in schematic coordinates, indexed by library pin angle
_OUT = {0: (-1, 0), 90: (0, 1), 180: (1, 0), 270: (0, -1)}


class Sheet:
    # optional callable(value, footprint) -> dict of extra symbol fields
    field_hook = None

    def __init__(self, title: str, project: str, paper: str = "A3"):
        self.title, self.project, self.paper = title, project, paper
        self._n = 0
        self.uuid = self.uid("sheet")
        self.lib_syms: dict[str, list] = {}
        self.items: list[str] = []
        self.path = ""  # instance path, set by the root
        self._pending: list[tuple] = []  # (lib_id, ref, value, x, y, fp, props, pins)
        self.netlist: dict[str, set[str]] = {}

    def uid(self, key: str | None = None) -> str:
        """Deterministic per-sheet UUID: named key, else sequential counter.

        Symbol UUIDs are keyed by reference so the PCB keeps its links to the
        schematic when the generator is re-run.
        """
        if key is None:
            self._n += 1
            key = f"item|{self._n}"
        return uid(f"{self.title}|{key}")

    # -- symbols ----------------------------------------------------------
    def _embed(self, lib: str, name: str):
        lid = f"{lib}:{name}"
        if lid not in self.lib_syms:
            s = lib_symbol(lib, name)
            s[1] = Q(lid)
            self.lib_syms[lid] = s
        return self.lib_syms[lid]

    def part(self, lib, name, ref, value, x, y, conn: dict[str, str], fp=None, props=None, in_bom=True,
             unit: int = 1):
        """Place a symbol unit; `conn` maps pin number -> net ('NC' = no-connect)."""
        x, y = snap(x), snap(y)
        sym = self._embed(lib, name)
        if Sheet.field_hook is not None:  # e.g. sourcing fields (LCSC) looked up centrally
            props = {**Sheet.field_hook(value, fp), **(props or {})}
        pins = symbol_pins(sym, unit)
        groups: dict[tuple, list[dict]] = {}
        for p in pins:
            groups.setdefault((p["x"], p["y"]), []).append(p)
        for (px, py), grp in groups.items():
            nets = {conn.get(p["number"]) for p in grp}
            if None in nets:
                missing = [p["number"] + "/" + p["name"] for p in grp if p["number"] not in conn]
                raise ValueError(f"{ref}: unconnected pins {missing}")
            if len(nets) != 1:
                raise ValueError(f"{ref}: stacked pins {[p['number'] for p in grp]} map to {nets}")
            net = nets.pop()
            ex, ey = x + px, y - py
            if net == "NC":
                self.items.append(f'(no_connect (at {fmt(ex)} {fmt(ey)}) (uuid "{self.uid()}"))')
                continue
            for p in grp:
                self.netlist.setdefault(net.removeprefix("G:"), set()).add(f"{ref}.{p['number']}")
            dx, dy = _OUT[grp[0]["a"]]
            sx, sy = ex + dx * STUB, ey + dy * STUB
            self.wire(ex, ey, sx, sy)
            self.net_tag(net, sx, sy, (dx, dy))
        self._place(lib, name, sym, ref, value, x, y, fp, props or {}, pins, in_bom=in_bom, unit=unit)

    def _place(self, lib, name, sym, ref, value, x, y, fp, props, pins, in_bom, rot=0, unit=1):
        lp = {str(c[1]): c for c in sym if isinstance(c, list) and c[0] == "property"}
        if fp is None:
            fp = str(lp["Footprint"][2]) if "Footprint" in lp else ""
        fields = {"Reference": ref, "Value": value, "Footprint": fp,
                  "Datasheet": str(lp["Datasheet"][2]) if "Datasheet" in lp else ""}
        fields.update(props)
        out = [f'(symbol (lib_id "{lib}:{name}") (at {fmt(x)} {fmt(y)} {rot}) (unit {unit})',
               f'(exclude_from_sim no) (in_bom {"yes" if in_bom else "no"}) (on_board yes) (dnp no)',
               f'(uuid "{self.uid(f"sym|{ref}|{unit}")}")']
        for k, v in fields.items():
            hidden = k not in ("Reference", "Value") or ref.startswith("#")
            if k in lp:
                at = child(lp[k], "at")
                fx, fy, fa = x + float(at[1]), y - float(at[2]), at[3]
            else:
                fx, fy, fa = x, y, 0
            if rot and k in ("Reference", "Value"):
                fx, fy = x, y + (3.81 if rot == 180 else 0)
            hide = " (hide yes)" if hidden else ""
            out.append(f'(property "{k}" "{v}" (at {fmt(fx)} {fmt(fy)} {fa}) '
                       f'(effects (font (size 1.27 1.27)){hide}))')
        for num in sorted({p["number"] for p in pins}):
            out.append(f'(pin "{num}" (uuid "{self.uid()}"))')
        self._pending.append((out, ref, unit))

    def wire(self, x1, y1, x2, y2):
        self.items.append(f'(wire (pts (xy {fmt(x1)} {fmt(y1)}) (xy {fmt(x2)} {fmt(y2)})) '
                          f'(stroke (width 0) (type default)) (uuid "{self.uid()}"))')

    _pwr_count = [0]

    def net_tag(self, net, x, y, d):
        dx, dy = d
        if net in POWER_NETS or net == "PWR_FLAG":
            sym = self._embed("power", net)
            up = net != "GND"  # supply arrows point away from the pin, GND hangs below
            if dy == -1:
                rot = 0 if up else 180
            elif dy == 1:
                rot = 180 if up else 0
            elif dx == 1:
                rot = 270 if up else 90
            else:
                rot = 90 if up else 270
            Sheet._pwr_count[0] += 1
            ref = f"#PWR{Sheet._pwr_count[0]:03d}"
            self._place("power", net, sym, ref, net, x, y, "", {}, symbol_pins(sym), in_bom=False, rot=rot)
            return
        glob = net.startswith("G:")
        text = net.removeprefix("G:")
        angle = {(1, 0): 0, (-1, 0): 180, (0, -1): 90, (0, 1): 270}[(dx, dy)]
        just = "left" if angle in (0, 90) else "right"
        if glob:
            self.items.append(
                f'(global_label "{text}" (shape bidirectional) (at {fmt(x)} {fmt(y)} {angle}) '
                f'(fields_autoplaced yes) (effects (font (size 1.27 1.27)) (justify {just})) (uuid "{self.uid()}") '
                f'(property "Intersheetrefs" "${{INTERSHEET_REFS}}" (at {fmt(x)} {fmt(y)} 0) '
                f'(effects (font (size 1.27 1.27)) (hide yes))))')
        else:
            self.items.append(
                f'(label "{text}" (at {fmt(x)} {fmt(y)} {angle}) (fields_autoplaced yes) '
                f'(effects (font (size 1.27 1.27)) (justify {just} bottom)) (uuid "{self.uid()}"))')

    def pwr_flag(self, net, x, y):
        """Place a PWR_FLAG connected to `net` (power symbol or label) at (x, y)."""
        x, y = snap(x), snap(y)
        sym = self._embed("power", "PWR_FLAG")
        Sheet._pwr_count[0] += 1
        self._place("power", "PWR_FLAG", sym, f"#FLG{Sheet._pwr_count[0]:03d}", "PWR_FLAG",
                    x, y, "", {}, symbol_pins(sym), in_bom=False)
        self.wire(x, y, x, y + STUB)
        self.net_tag(net, x, y + STUB, (0, 1))

    def text(self, s, x, y, size=1.27):
        s = s.replace('"', "'").replace("\n", "\\n")
        self.items.append(f'(text "{s}" (exclude_from_sim no) (at {fmt(x)} {fmt(y)} 0) '
                          f'(effects (font (size {size} {size})) (justify left top)) (uuid "{self.uid()}"))')

    # -- output -------------------------------------------------------------
    def render(self, project, inst_path, extra="", is_root=False) -> str:
        lines = [f'(kicad_sch (version {FORMAT_VERSION}) (generator "eeschema") (generator_version "8.0")',
                 f'(uuid "{self.uuid}")', f'(paper "{self.paper}")',
                 f'(title_block (title "{self.title}") (date "2026-10-05") (rev "0.1") (company "UFI Project"))',
                 "(lib_symbols"]
        for s in self.lib_syms.values():
            lines.append(dump(s, 1))
        lines.append(")")
        lines += self.items
        for out, ref, unit in self._pending:
            lines += out
            lines.append(f'(instances (project "{project}" (path "{inst_path}" '
                         f'(reference "{ref}") (unit {unit})))))')
        lines.append(extra)
        if is_root:
            lines.append('(sheet_instances (path "/" (page "1")))')
        lines.append(")")
        return "\n".join(lines) + "\n"
