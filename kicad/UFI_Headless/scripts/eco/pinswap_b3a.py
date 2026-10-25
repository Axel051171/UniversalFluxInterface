"""Stage B3a (pin swap for routing, owner rule 2026-10-25): three U5 GPIOs change pins so the
6-layer board can be routed to DRC 0.  MCU_Core.kicad_sch text edit (plain Python):
  LED_FDD      PG1  (pin 57)  -> PC7 (pin 97)   push-pull LED output through R36
  GUARD_ERR12  PG13 (pin 128) -> PC9 (pin 99)   input, 10k pull-up R108 on the board
  FDD_MOTOR_B  PE8  (pin 59)  -> PC13 (pin 7)   MOSFET gate (Q6), low speed; PC13 is an RTC-domain
                                                pin (3 mA, 2 MHz) - enough for a static gate drive
The label moves from the left pin to the right pin (wire stub 2.54 mm, label at its end), the
old pin gets a no-connect and the no-connect on the new pin goes.  Idempotent.
usage: python pinswap_b3a.py MCU_Core.kicad_sch
"""
import re
import sys
import uuid

path = sys.argv[1]
s = open(path, encoding="utf-8").read()
if "pinswap-b3a" in s:
    print("already applied")
    sys.exit(0)


def drop_block(text, head_regex):
    """Remove one top-level s-expression block whose head matches (tab-indented level 1)."""
    m = re.search(head_regex, text)
    if not m:
        raise SystemExit("not found: " + head_regex)
    start = text.rfind("\n\t(", 0, m.start() + 1)
    depth = 0
    i = start + 1
    while True:
        c = text[i]
        if c == "(":
            depth += 1
        elif c == ")":
            depth -= 1
            if depth == 0:
                break
        i += 1
    return text[:start] + text[i + 1:]


def nc(x, y):
    return '\t(no_connect\n\t\t(at %s %s)\n\t\t(uuid "%s")\n\t)\n' % (x, y, uuid.uuid4())


def wire(x0, y0, x1, y1):
    return ('\t(wire\n\t\t(pts\n\t\t\t(xy %s %s) (xy %s %s)\n\t\t)\n\t\t(stroke\n\t\t\t(width 0)\n'
            '\t\t\t(type default)\n\t\t)\n\t\t(uuid "%s")\n\t)\n' % (x0, y0, x1, y1, uuid.uuid4()))


# 1. no-connects on the new pins go
for y in ("170.18", "175.26", "185.42"):
    s = drop_block(s, r"\(no_connect\n\t\t\(at 248\.92 %s\)" % re.escape(y))
# 2. old stubs go
s = drop_block(s, r"\(xy 172\.72 96\.52\) \(xy 170\.18 96\.52\)")
s = drop_block(s, r"\(xy 172\.72 200\.66\) \(xy 170\.18 200\.66\)")
# 3. labels move to the right side (angle 0, left justified)
s = s.replace('\t(label "LED_FDD"\n\t\t(at 170.18 96.52 180)\n\t\t(fields_autoplaced yes)\n\t\t(effects\n'
              '\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify right bottom)',
              '\t(label "LED_FDD"\n\t\t(at 251.46 170.18 0)\n\t\t(fields_autoplaced yes)\n\t\t(effects\n'
              '\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n\t\t\t)\n\t\t\t(justify left bottom)', 1)
m = re.search(r'\t\(global_label "FDD_MOTOR_B"\n\t\t\(shape bidirectional\)\n\t\t\(at 170\.18 200\.66 180\)', s)
if not m:
    raise SystemExit("FDD_MOTOR_B label not found")
blk_end = s.index("\n\t)\n", m.start())
blk = s[m.start():blk_end]
blk = blk.replace("(at 170.18 200.66 180)", "(at 251.46 185.42 0)").replace(
    "(at 170.18 200.66 0)", "(at 251.46 185.42 0)").replace("(justify right)", "(justify left)")
s = s[:m.start()] + blk + s[blk_end:]
m = re.search(r'\t\(global_label "GUARD_ERR12"\n\t\t\(at 172\.72 127\.0+1 180\)', s)
if not m:
    raise SystemExit("GUARD_ERR12 label not found")
blk_end = s.index("\n\t)\n", m.start())
blk = s[m.start():blk_end]
blk = re.sub(r"\(at 172\.72 127\.0+1 180\)", "(at 251.46 175.26 0)", blk).replace("(justify right)", "(justify left)")
s = s[:m.start()] + blk + s[blk_end:]
if '(label "LED_FDD"\n\t\t(at 251.46 170.18 0)' not in s:
    raise SystemExit("LED_FDD label not moved")
# 4. new stubs, no-connects on the freed pins and a sheet note (also the idempotence marker)
add = (wire("248.92", "170.18", "251.46", "170.18") + wire("248.92", "175.26", "251.46", "175.26") +
       wire("248.92", "185.42", "251.46", "185.42") + nc("172.72", "96.52") + nc("172.72", "200.66") +
       nc("172.72", "127"))
marker = '\t(text "pinswap-b3a: LED_FDD PG1->PC7, GUARD_ERR12 PG13->PC9, FDD_MOTOR_B PE8->PC13 (routing, stage B3a)"\n' \
         '\t\t(exclude_from_sim no)\n\t\t(at 254 248.92 0)\n\t\t(effects\n\t\t\t(font\n\t\t\t\t(size 1.27 1.27)\n' \
         '\t\t\t)\n\t\t\t(justify left bottom)\n\t\t)\n\t\t(uuid "%s")\n\t)\n' % uuid.uuid4()
i = s.rstrip().rindex("\n)")          # before the closing parenthesis of the file
s = s[:i + 1] + add + marker + s[i + 1:]
open(path, "w", encoding="utf-8").write(s)
print("pin swap applied")
