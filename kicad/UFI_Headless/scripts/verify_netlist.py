"""Cross-check a KiCad-exported netlist against a recorded net list.

Usage: python verify_netlist.py <exported.net> [--baseline FILE] [--write]

Two baselines exist, and they are not the same kind of evidence:

  netlist_v10.txt      (default) every net of the released v1.0 schematic, recorded from an
                       export of that schematic.  It is a regression baseline: the check shows
                       any net that changed since the record was written.  It does NOT show
                       that the recorded nets are the intended ones - that was the review of
                       the v1.0 rework (README, PLAN), not this script.
  netlist_summary.txt  the intended connections written by scripts/build_schematic.py for
                       board v0.7 (last written 2026-10-07).  The v1.0 rework changed the
                       schematic files without running that generator again, so the v1.0
                       schematic differs from it (182 differences on 2026-11-14).  Use it only
                       with a v0.7 export.

--write records the given export as the baseline (after a reviewed schematic change).
Exit status 1 when a net differs.
"""
from __future__ import annotations

import argparse
import sys
from pathlib import Path

from kisch import child, parse

HERE = Path(__file__).resolve().parent.parent
PENDING_PREFIXES: tuple[str, ...] = ()  # nets connected by sheets not drawn yet


def exported(path: Path) -> dict[str, set[str]]:
    tree = parse(path.read_text(encoding="utf8"))
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
    return actual


def recorded(path: Path) -> dict[str, set[str]]:
    intended: dict[str, set[str]] = {}
    for line in path.read_text(encoding="utf8").splitlines():
        if line.startswith(("==", "#")) or not line.strip():
            continue
        net, *pins = line.split()
        intended.setdefault(net, set()).update(pins)
    return intended


def compare(intended: dict[str, set[str]], actual: dict[str, set[str]]) -> tuple[list[str], list[str]]:
    errors, notes = [], []
    for net, pins in sorted(intended.items()):
        got = actual.get(net, set())
        if pins != got:
            errors.append(f"MISMATCH {net}: missing={sorted(pins - got)} extra={sorted(got - pins)}")
    for net, pins in sorted(actual.items()):
        if net not in intended:
            errors.append(f"UNEXPECTED net {net}: {sorted(pins)}")
        elif len(pins) < 2 and not net.startswith(PENDING_PREFIXES) and net != "PWR_SRC":
            notes.append(f"SINGLE-PIN {net}: {sorted(pins)}")
    return errors, notes


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("netlist", type=Path)
    ap.add_argument("--baseline", type=Path, default=HERE / "netlist_v10.txt")
    ap.add_argument("--write", action="store_true", help="record this export as the baseline")
    a = ap.parse_args()
    actual = exported(a.netlist)
    if a.write:
        lines = ["# Nets of the UFI_Headless schematic, recorded by scripts/verify_netlist.py --write.",
                 "# Regression baseline, not an independent statement of intent (see the script).", ""]
        lines += [f"{net:<24} {' '.join(sorted(pins))}" for net, pins in sorted(actual.items())]
        a.baseline.write_text("\n".join(lines) + "\n", encoding="utf8", newline="\n")
        print(f"{len(actual)} nets recorded in {a.baseline.name}")
        return 0
    intended = recorded(a.baseline)
    errors, notes = compare(intended, actual)
    for line in errors + notes:
        print(line)
    print(f"{len(intended)} nets checked against {a.baseline.name}, {len(errors)} errors")
    return 1 if errors else 0


if __name__ == "__main__":
    sys.exit(main())
