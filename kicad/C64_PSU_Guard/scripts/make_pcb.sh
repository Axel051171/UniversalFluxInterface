#!/usr/bin/env bash
# Schematic -> ERC -> netlist -> placed board -> Freerouting -> SES import -> pours -> DRC.
# usage: make_pcb.sh            (Freerouting 2.4.1 + a JRE in $TOOLS, default: scratchpad tools)
set -eo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PRJ="$(dirname "$HERE")"
TOOLS="${TOOLS:-$HERE/tools}"
WORK="${WORK:-$PRJ/build}"
PY="/c/Program Files/KiCad/10.0/bin/python.exe"
CLI="/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"
W() { cygpath -w "$1"; }
B="$PRJ/C64_PSU_Guard.kicad_pcb"
S="$PRJ/C64_PSU_Guard.kicad_sch"
mkdir -p "$WORK"

"$PY" "$HERE/build_schematic.py"
"$CLI" sch erc --format json -o "$WORK/erc.json" "$S" >/dev/null 2>&1 || true
"$PY" -c "import json,sys;d=json.load(open(sys.argv[1]));v=[x for s in d['sheets'] for x in s['violations']];print('ERC violations',len(v));[print(' ',x['severity'],x['type'],x['description'][:100]) for x in v]" "$(W "$WORK/erc.json")"
"$CLI" sch export netlist -o "$WORK/guard.net" "$S" >/dev/null
"$PY" "$HERE/build_pcb.py" "$(W "$WORK/guard.net")" "$(W "$B")" 2>&1 | { grep -vi swig || true; }

J="$(ls -d "$TOOLS"/jdk-25*-jre | head -1)/bin/java.exe"
freeroute() {  # $1 = max passes
  rm -f "$WORK/b.dsn" "$WORK/b.ses"
  "$PY" "$HERE/route.py" export "$(W "$B")" "$(W "$WORK/b.dsn")" 2>&1 | { grep -vi swig || true; }
  (cd "$WORK" && timeout 900 "$J" -jar "$(W "$TOOLS/freerouting-2.4.1.jar")" -de b.dsn -do b.ses -mp "$1" \
      --gui.enabled=false >>"$WORK/freerouting.log" 2>&1) || echo "freerouting exit $?"
  "$PY" "$HERE/route.py" import "$(W "$B")" "$(W "$WORK/b.ses")" 2>&1 | { grep -vi swig || true; }
}
: >"$WORK/freerouting.log"
freeroute 20
for round in 1 2 3; do
  "$CLI" pcb drc --format json -o "$WORK/drc_v.json" "$B" >/dev/null 2>&1 || true
  out="$("$PY" "$HERE/drop_violations.py" "$(W "$B")" "$(W "$WORK/drc_v.json")" 2>&1 | grep -o 'removed [0-9]*' || true)"
  echo "cleanup $round: $out"
  [ "$out" = "removed 0" ] && break
  freeroute 10
done
# Freerouting sometimes drops the last segment of a net on SES import: route the rest again
for round in 1 2; do
  "$CLI" pcb drc --format json -o "$WORK/drc_u.json" "$B" >/dev/null 2>&1 || true
  n="$("$PY" -c "import json,sys;print(len(json.load(open(sys.argv[1])).get('unconnected_items',[])))" "$(W "$WORK/drc_u.json")")"
  echo "unconnected $n"
  [ "$n" = 0 ] && break
  freeroute 10
done
"$PY" "$HERE/finish_pcb.py" "$(W "$B")" 2>&1 | { grep -vi swig || true; }
"$CLI" pcb drc --schematic-parity -o "$WORK/drc.rpt" "$B" 2>&1 | grep -E "Verst|Unverb|gefunden|violations|unconnected" || true
grep -E '^\[' "$WORK/drc.rpt" | sed 's/:.*//' | sort | uniq -c || true
