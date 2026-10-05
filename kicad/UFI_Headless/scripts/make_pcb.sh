#!/usr/bin/env bash
# Schematic netlist -> placed board -> Freerouting -> SES import -> DRC.
# usage: make_pcb.sh [place|route|all]   (default: all)
# Requires Java 25 + freerouting-2.4.1.jar in $TOOLS (default: ./tools next to this script).
set -eo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PRJ="$(dirname "$HERE")"
TOOLS="${TOOLS:-$HERE/tools}"
WORK="${WORK:-$PRJ/build}"
PY="/c/Program Files/KiCad/10.0/bin/python.exe"
CLI="/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"
W() { cygpath -w "$1"; }
B="$PRJ/UFI_Headless.kicad_pcb"
STEP="${1:-all}"
mkdir -p "$WORK"

if [ "$STEP" = place ] || [ "$STEP" = all ]; then
  "$CLI" sch export netlist -o "$WORK/headless.net" "$PRJ/UFI_Headless.kicad_sch" >/dev/null
  "$PY" "$HERE/build_pcb.py" "$(W "$WORK/headless.net")" "$(W "$B")" 2>&1 | grep -vi swig
fi
if [ "$STEP" = route ] || [ "$STEP" = all ]; then
  J="$(ls -d "$TOOLS"/jdk-25*-jre)/bin/java.exe"
  freeroute() {  # $1 = max passes
    rm -f "$WORK/b.dsn" "$WORK/b.ses"
    "$PY" "$HERE/route.py" export "$(W "$B")" "$(W "$WORK/b.dsn")" 2>&1 | grep -vi swig
    (cd "$WORK" && timeout 1800 "$J" -jar "$(W "$TOOLS/freerouting-2.4.1.jar")" -de b.dsn -do b.ses -mp "$1" \
        --gui.enabled=false >>"$WORK/freerouting.log" 2>&1) || echo "freerouting exit $?"
    "$PY" "$HERE/route.py" import "$(W "$B")" "$(W "$WORK/b.ses")" 2>&1 | grep -vi swig
  }
  : >"$WORK/freerouting.log"
  freeroute 20
  # Freerouting leaves a few clearance violations: drop those tracks and re-route them
  for round in 1 2 3 4; do
    "$CLI" pcb drc --format json -o "$WORK/drc_v.json" "$B" >/dev/null 2>&1 || true
    out="$("$PY" "$HERE/drop_violations.py" "$(W "$B")" "$(W "$WORK/drc_v.json")" 2>&1 | grep -vi swig)"
    echo "cleanup $round: $out"
    [ "$out" = "removed 0" ] && break
    freeroute 10
  done
  # fallback hand route (only applied if Freerouting left USB D+ open)
  "$PY" "$HERE/manual_routes.py" "$(W "$B")" 2>&1 | grep -vi swig || true
  # widen supply tracks, then undo every widening that DRC flags until stable
  "$PY" "$HERE/widen_power.py" widen "$(W "$B")" "$(W "$WORK/widened.json")" 2>&1 | grep -vi swig
  for _ in 1 2 3 4 5 6; do
    "$CLI" pcb drc --format json -o "$WORK/drc_w.json" "$B" >/dev/null 2>&1 || true
    out="$("$PY" "$HERE/widen_power.py" revert "$(W "$B")" "$(W "$WORK/widened.json")" "$(W "$WORK/drc_w.json")" 2>&1 | grep -vi swig)"
    echo "$out"
    [ "$out" = "reverted 0" ] && break
  done
  "$PY" "$HERE/finish_pcb.py" "$(W "$B")" 2>&1 | grep -vi swig
  "$PY" "$HERE/power_pours.py" "$(W "$B")" 2>&1 | grep -vi swig
fi
"$CLI" pcb drc --schematic-parity -o "$WORK/drc.rpt" "$B" 2>&1 | grep -E "Verst|Unverb|gefunden" || true
grep -E '^\[' "$WORK/drc.rpt" | sed 's/:.*//' | sort | uniq -c || true
