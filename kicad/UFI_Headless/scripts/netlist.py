"""Read a KiCad S-expression netlist (kicad-cli sch export netlist) into plain dicts."""
from __future__ import annotations

from pathlib import Path

from kisch import child, parse


def read_netlist(path: str | Path):
    """Return (components, nets).

    components: ref -> {value, footprint, path} where path is the KIID path the
                PCB footprint must carry to stay linked ("/<sheet uuid>/<symbol uuid>").
    nets:       net name (without sheet prefix for local nets) -> [(ref, pin), ...]
    """
    tree = parse(Path(path).read_text(encoding="utf8"))
    comps = {}
    for c in child(tree, "components")[1:]:
        ref = str(child(c, "ref")[1])
        sheet = str(child(child(c, "sheetpath"), "tstamps")[1])
        comps[ref] = {
            "value": str(child(c, "value")[1]),
            "footprint": str(child(c, "footprint")[1]) if child(c, "footprint") else "",
            "path": sheet + str(child(c, "tstamps")[1]),
            "props": {str(p[1][1]): str(p[2][1]) for p in c
                      if isinstance(p, list) and p[0] == "property"},
            "datasheet": str(child(c, "datasheet")[1]) if child(c, "datasheet") else "",
        }
    nets = {}
    for n in child(tree, "nets")[1:]:
        name = str(child(n, "name")[1])
        nodes = [(str(child(x, "ref")[1]), str(child(x, "pin")[1]))
                 for x in n if isinstance(x, list) and x[0] == "node"]
        nodes = [(r, p) for r, p in nodes if not r.startswith("#")]
        if nodes:
            nets[name] = nodes
    return comps, nets
