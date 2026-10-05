#!/usr/bin/env bash
# Fabrication outputs for JLCPCB: Gerber + Excellon (zipped), BOM (with LCSC), CPL.
set -eo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
PRJ="$(dirname "$HERE")"
CLI="/c/Program Files/KiCad/10.0/bin/kicad-cli.exe"
B="$PRJ/UFI_Headless.kicad_pcb"
S="$PRJ/UFI_Headless.kicad_sch"
F="$PRJ/fertigung"
rm -rf "$F"
mkdir -p "$F/gerber"

"$CLI" pcb export gerbers --layers F.Cu,In1.Cu,In2.Cu,B.Cu,F.Paste,B.Paste,F.Silkscreen,B.Silkscreen,F.Mask,B.Mask,Edge.Cuts \
    --subtract-soldermask -o "$F/gerber/" "$B" >/dev/null
"$CLI" pcb export drill --format excellon --excellon-separate-th --generate-map --map-format gerberx2 \
    -o "$F/gerber/" "$B" >/dev/null
powershell.exe -NoProfile -Command "Compress-Archive -Path '$(cygpath -w "$F/gerber")\\*' -DestinationPath '$(cygpath -w "$F/UFI_Headless_gerber.zip")' -Force"

# BOM: KiCad view and JLC view (Comment, Designator, Footprint, LCSC)
"$CLI" sch export bom --fields 'Reference,Value,Footprint,MPN,LCSC,${QUANTITY}' \
    --labels 'Designator,Value,Footprint,MPN,LCSC,Quantity' --group-by Value,Footprint,LCSC \
    -o "$F/UFI_Headless-bom.csv" "$S" >/dev/null
"$CLI" sch export bom --fields 'Value,Reference,Footprint,LCSC' \
    --labels 'Comment,Designator,Footprint,LCSC Part #' --group-by Value,Footprint,LCSC \
    -o "$F/UFI_Headless-bom-jlc.csv" "$S" >/dev/null

# CPL: KiCad position file and JLC column names
"$CLI" pcb export pos --format csv --units mm --side both --exclude-dnp -o "$F/UFI_Headless-pos.csv" "$B" >/dev/null
{
  echo '"Designator","Mid X","Mid Y","Layer","Rotation"'
  tail -n +2 "$F/UFI_Headless-pos.csv" | awk -F, '{gsub(/"/,"",$1); gsub(/"/,"",$7);
      side = ($7 == "top") ? "Top" : "Bottom";
      printf "\"%s\",\"%smm\",\"%smm\",\"%s\",\"%s\"\n", $1, $4, $5, side, $6}'
} >"$F/UFI_Headless-cpl-jlc.csv"
ls -1 "$F"
