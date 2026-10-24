"""Set the in_bom flag of schematic symbols by reference (text edit of the .kicad_sch files).

Test points and solder jumpers carry (in_bom no) like TP1-TP8 / JP1-JP3; symbols added later
through the MCP tools came in with (in_bom yes), which the PCB parity check flags because the
footprints are excluded from the BOM.
usage: set_in_bom.py yes|no REF [REF ...]      (run in the project directory)
"""
import glob
import re
import sys

flag = sys.argv[1]
refs = set(sys.argv[2:])
changed = 0
for path in glob.glob("*.kicad_sch"):
    text = open(path, encoding="utf-8").read()
    out = []
    pos = 0
    for m in re.finditer(r'\(property "Reference" "([^"]+)"', text):
        if m.group(1) not in refs:
            continue
        # the flags precede the property block inside the same (symbol ...) entry
        head = text.rfind("(symbol", pos, m.start())  # instance blocks are "(symbol\n\t\t(lib_id ..."
        if head < 0:
            continue
        block = text[head:m.start()]
        new = re.sub(r"\(in_bom (yes|no)\)", "(in_bom %s)" % flag, block, count=1)
        if new != block:
            changed += 1
        out.append(text[pos:head] + new)
        pos = m.start()
    out.append(text[pos:])
    new_text = "".join(out)
    if new_text != text:
        open(path, "w", encoding="utf-8", newline="\n").write(new_text)
        print(path, "updated")
print("symbols changed", changed)
