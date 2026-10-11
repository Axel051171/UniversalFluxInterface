#!/usr/bin/env bash
# Fabrication outputs for JLCPCB (2 layers): Gerber + Excellon (zipped), BOM (with LCSC), CPL,
# plus board previews for the README.
set -eo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PRJ="$(dirname "$HERE")"
CLI="/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"
B="$PRJ/C64_PSU_Guard.kicad_pcb"
S="$PRJ/C64_PSU_Guard.kicad_sch"
F="$PRJ/fertigung"
rm -rf "$F"
mkdir -p "$F/gerber" "$PRJ/docs"

"$CLI" pcb export gerbers --layers F.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
    --subtract-soldermask -o "$F/gerber/" "$B" >/dev/null
"$CLI" pcb export drill --format excellon --excellon-separate-th --generate-map --map-format gerberx2 \
    -o "$F/gerber/" "$B" >/dev/null
powershell.exe -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$F/gerber")\\*' -DestinationPath '$(cygpath -w "$F/C64_PSU_Guard_gerber.zip")' -Force"

"$CLI" sch export bom --fields 'Reference,Value,Footprint,MPN,LCSC,${QUANTITY}' \
    --labels 'Designator,Value,Footprint,MPN,LCSC,Quantity' --group-by Value,Footprint,LCSC \
    -o "$F/C64_PSU_Guard-bom.csv" "$S" >/dev/null
"$CLI" sch export bom --fields 'Value,Reference,Footprint,LCSC' \
    --labels 'Comment,Designator,Footprint,LCSC Part #' --group-by Value,Footprint,LCSC \
    -o "$F/C64_PSU_Guard-bom-jlc.csv" "$S" >/dev/null

"$CLI" pcb export pos --format csv --units mm --side both --exclude-dnp -o "$F/C64_PSU_Guard-pos.csv" "$B" >/dev/null
w() { cygpath -w "$1"; }
"/c/Program Files/KiCad/10.0/bin/python.exe" "$(w "$HERE/jlc_cpl.py")" "$(w "$B")" "$(w "$F/C64_PSU_Guard-pos.csv")" \
    "$(w "$F/C64_PSU_Guard-cpl-jlc.csv")" "$(w "$F/JLC_Rotation_Check.md")" 2>&1 | grep -vi swig

# previews
"$CLI" sch export pdf -o "$PRJ/docs/C64_PSU_Guard_schematic.pdf" "$S" >/dev/null
"$CLI" pcb render --side top --background opaque --quality basic -w 1600 -h 1200 -o "$PRJ/docs/top.png" "$B" >/dev/null 2>&1 || true
"$CLI" pcb render --side bottom --background opaque --quality basic -w 1600 -h 1200 -o "$PRJ/docs/bottom.png" "$B" >/dev/null 2>&1 || true
ls -1 "$F"
