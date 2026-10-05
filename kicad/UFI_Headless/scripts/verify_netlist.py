"""Cross-check the KiCad-exported netlist against the intended connections.

Usage: python verify_netlist.py <exported.net>
Compares every net from the generator's netlist_summary.txt with the netlist
that kicad-cli derived from the drawing, and lists single-pin nets.
"""
from __future__ import annotations

from kisch import child, parse
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent.parent
PENDING_PREFIXES: tuple[str, ...] = ()  # nets connected by sheets not drawn yet

tree = parse(Path(sys.argv[1]).read_text(encoding="utf8"))
actual: dict[str, set[str]] = {}
for net in child(tree, "nets")[1:]:
    name = str(child(net, "name")[1]).rsplit("/", 1)[-1]
    if name.startswith("unconnected-"):
        continue
    for node in net:
        if isinstance(node, list) and node[0] == "node":
            r, p = str(child(node, "ref")[1]), str(child(node, "pin")[1])
            if not r.startswith("#"):
                actual.setdefault(name, set()).add(f"{r}.{p}")

intended: dict[str, set[str]] = {}
for line in (HERE / "netlist_summary.txt").read_text(encoding="utf8").splitlines():
    if line.startswith("==") or not line.strip():
        continue
    net, *pins = line.split()
    intended.setdefault(net, set()).update(pins)

errors = 0
for net, pins in sorted(intended.items()):
    got = actual.get(net, set())
    if pins != got:
        errors += 1
        print(f"MISMATCH {net}: missing={sorted(pins - got)} extra={sorted(got - pins)}")
for net, pins in sorted(actual.items()):
    if net not in intended:
        errors += 1
        print(f"UNEXPECTED net {net}: {sorted(pins)}")
    elif len(pins) < 2 and not net.startswith(PENDING_PREFIXES) and net != "PWR_SRC":
        print(f"SINGLE-PIN {net}: {sorted(pins)}")
print(f"{len(intended)} nets checked, {errors} errors")
