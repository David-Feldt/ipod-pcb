#!/bin/sh
# Validate the existing release without regenerating the PCB or fabrication files.
set -eu
cd "$(dirname "$0")"
KAPP="${KICAD_APP_CONTENTS:-$HOME/Applications/KiCad/KiCad.app/Contents}"
[ -d "$KAPP" ] || KAPP="/Applications/KiCad/KiCad.app/Contents"
export KICAD_CLI="$KAPP/MacOS/kicad-cli"
KPY="$KAPP/Frameworks/Python.framework/Versions/Current/bin/python3"
exec "$KPY" -B gen/check_order.py "$@"
