"""Net classes per the pcb-routing skill: every load-path net (including the intermediate nodes
fuse -> shunt -> FET) gets the width of its current class, so that routers and widen_power.py
use it.  Idempotent: rewrites the given classes and their patterns in the .kicad_pro.
usage: set_netclasses.py <project.kicad_pro>
"""
import json
import sys

# name: (track mm, via dia, via drill, nets) - 1.5-2 A -> 1.0 mm (IPC-2152, 35 um outer, 10 K)
CLASSES = {
    "Load": (1.0, 0.8, 0.4, [
        "VBUS", "/Power/VBUS_F", "/Power/+5V_PSU", "/Power/+5V_PSU_F",
        "/Power/+12V_IN", "+12V", "/Power/BUCK_SW", "/Power/+5V_DRV", "/Power/+5V_PRI", "+5V",
        "/Power/FDD5_F", "/Power/FDD5_S", "FDD_5V", "/Power/FDD12_F", "/Power/FDD12_S", "FDD_12V"]),
}
path = sys.argv[1]
p = json.load(open(path, encoding="utf-8"))
ns = p["net_settings"]
tmpl = next(c for c in ns["classes"] if c["name"] == "Power")
ns["classes"] = [c for c in ns["classes"] if c["name"] not in CLASSES]
load_nets = {n for _, _, _, nets in CLASSES.values() for n in nets}
ns["netclass_patterns"] = [x for x in ns.get("netclass_patterns", []) if x["pattern"] not in load_nets]
for name, (w, vd, vdr, nets) in CLASSES.items():
    c = dict(tmpl)
    c.update({"name": name, "track_width": w, "via_diameter": vd, "via_drill": vdr})
    ns["classes"].append(c)
    ns["netclass_patterns"] += [{"netclass": name, "pattern": n} for n in nets]
json.dump(p, open(path, "w", encoding="utf-8"), indent=2, ensure_ascii=False)
open(path, "a", encoding="utf-8").write("\n")
for c in ns["classes"]:
    print("%-8s track %.2f via %.2f/%.2f  nets %d" % (c["name"], c["track_width"], c["via_diameter"], c["via_drill"],
                                                      sum(1 for x in ns["netclass_patterns"] if x["netclass"] == c["name"])))
