#!/bin/sh
# Rebuild everything: schematic -> ERC -> netlist -> PCB -> autoroute -> DRC -> fab outputs.
# usage: ./build.sh [path/to/freerouting.jar]
set -e
cd "$(dirname "$0")"
KAPP="$HOME/Applications/KiCad/KiCad.app/Contents"
[ -d "$KAPP" ] || KAPP="/Applications/KiCad/KiCad.app/Contents"
KC="$KAPP/MacOS/kicad-cli"
KPY="$KAPP/Frameworks/Python.framework/Versions/Current/bin/python3"
JAR="${1:-tools/freerouting.jar}"

python3 gen/build_sch.py
"$KC" sch erc --severity-all --exit-code-violations -o kicad/erc.rpt kicad/eink-pod.kicad_sch
"$KC" sch export netlist --format kicadsexpr -o kicad/eink-pod.net kicad/eink-pod.kicad_sch
"$KPY" -u gen/build_pcb.py
"$KPY" -u gen/route.py "$JAR" 40
"$KC" pcb drc --severity-error --refill-zones --schematic-parity --exit-code-violations -o kicad/drc.rpt kicad/eink-pod.kicad_pcb
"$KC" pcb drc --severity-all --schematic-parity -o kicad/drc-all.rpt kicad/eink-pod.kicad_pcb
"$KPY" -u gen/verify_board.py > kicad/verification.txt
cat kicad/verification.txt
python3 gen/fab.py
echo "OK: ERC + DRC clean, outputs in fab/"
