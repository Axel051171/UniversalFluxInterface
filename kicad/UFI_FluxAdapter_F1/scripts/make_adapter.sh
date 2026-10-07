#!/usr/bin/env bash
# Build -> export DSN -> Freerouting -> import SES -> DRC, for the UFI Flux Adapter F1.
set -e
SP="$(cd "$(dirname "$0")" && pwd)"
A="${1:-$SP/adapter}"
PY="/c/Program Files/KiCad/10.0/bin/python.exe"
CLI="/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"
J="$(ls -d "$SP"/tools/jdk-21*-jre)/bin/java.exe"
W() { cygpath -w "$1"; }
B="$A/UFI_FluxAdapter_F1.kicad_pcb"
mkdir -p "$A"
rm -f "$A/b.dsn" "$A/b.ses"
"$PY" "$SP/build_adapter.py" "$(W "$B")" 2>&1 | grep -v swig
"$PY" "$SP/route.py" export "$(W "$B")" "$(W "$A/b.dsn")" 2>&1 | grep -v swig
(cd "$A" && timeout 500 "$J" -jar "$(W "$SP/tools/freerouting.jar")" -de b.dsn -do b.ses -mp 30 --gui.enabled=false >"$A/freerouting.log" 2>&1)
"$PY" "$SP/route.py" import "$(W "$B")" "$(W "$A/b.ses")" 2>&1 | grep -v swig
"$CLI" pcb drc -o "$A/drc.rpt" "$B" 2>&1 | grep -E "Verst|Unverb"
grep -E '^\[' "$A/drc.rpt" | sort | uniq -c || true
