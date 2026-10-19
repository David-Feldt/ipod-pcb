#!/bin/sh
# Photo-style renders of the assembled board (with display + part models) into renders/.
set -e
cd "$(dirname "$0")"
KAPP="$HOME/Applications/KiCad/KiCad.app/Contents"
[ -d "$KAPP" ] || KAPP="/Applications/KiCad/KiCad.app/Contents"
export KICAD10_3DMODEL_DIR="$KAPP/SharedSupport/3dmodels"
KC="$KAPP/MacOS/kicad-cli"
PCB=kicad/eink-pod.kicad_pcb
mkdir -p renders
R() { "$KC" pcb render --quality high --background opaque "$@" "$PCB" >/dev/null 2>&1; }
R --side top    -w 1000 -h 1700 -o renders/front.png
R --side bottom -w 1000 -h 1700 -o renders/back.png
R --side top --perspective --rotate '-35,0,25' --zoom 0.9 -w 1600 -h 1600 -o renders/iso-front.png
R --side bottom --perspective --rotate '35,0,-25' --zoom 0.9 -w 1600 -h 1600 -o renders/iso-back.png
echo "renders written to renders/"
