"""Summarise a KiCad DRC report: unconnected items grouped by net and by footprint.

usage: python unrouted_report.py <drc.rpt>
"""
import re
import sys
from collections import Counter
from pathlib import Path

text = Path(sys.argv[1]).read_text(encoding="utf8", errors="replace")
by_net, by_fp = Counter(), Counter()
for block in text.split("\n[")[1:]:
    if not block.startswith("unconnected_items"):
        continue
    nets = re.findall(r"\[([^\]]+)\] of (\S+)", block)
    for net, ref in nets[:1]:
        by_net[net] += 1
    for _net, ref in nets:
        by_fp[ref] += 1
print("by net:", ", ".join(f"{n}={c}" for n, c in by_net.most_common(40)))
print("by footprint:", ", ".join(f"{r}={c}" for r, c in by_fp.most_common(25)))
