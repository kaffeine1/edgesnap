#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build a test ISO with the spike binaries for the QEMU VMs' CD drive.
# Convention learned in telegram-amiga: every ISO gets a NEW, UNIQUE volume
# label (A-Z, 0-9, _ only, max 32 chars) - OS4's CDFS identifies volumes by
# label, and re-inserting a different ISO with a recycled label confuses it.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# 16 chars exactly: the Joliet volume name is capped at 16, and a longer
# label would mount under a silently truncated name.
LABEL="ES_$(date +%y%m%d_%H%M%S)"
STAGE="$ROOT/build/iso/stage"
OUT="$ROOT/build/iso/$LABEL.iso"

if [ ! -f "$ROOT/build/os4/EdgeSnap" ]; then
    echo "ERROR: build/os4/EdgeSnap missing - run scripts/build-os4.sh first" >&2
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE/os4" "$STAGE/mos"

# Per-system layout: the installer picks the directory that matches the
# machine it is running on.
cp "$ROOT/build/os4/EdgeSnap"          "$STAGE/os4/EdgeSnap"
cp "$ROOT/build/os4/edgesnap.library"  "$STAGE/os4/edgesnap.library"
cp "$ROOT/build/os4/esnaptest"         "$STAGE/os4/esnaptest"
if [ -f "$ROOT/build/morphos/EdgeSnap" ]; then
    cp "$ROOT/build/morphos/EdgeSnap"         "$STAGE/mos/EdgeSnap"
    cp "$ROOT/build/morphos/edgesnap.library" "$STAGE/mos/edgesnap.library"
    cp "$ROOT/build/morphos/esnaptest"        "$STAGE/mos/esnaptest"
fi

# What the user sees in the drawer: an Install icon to double-click,
# and readable files. Everything visible carries an icon, or Workbench
# shows an empty window.
cp "$ROOT/installer/Install"        "$STAGE/Install"
cp "$ROOT/assets/Install.info"      "$STAGE/Install.info"
cp "$ROOT/assets/EdgeSnap.info"     "$STAGE/os4/EdgeSnap.info"
cp "$ROOT/assets/EdgeSnap.info"     "$STAGE/mos/EdgeSnap.info" 2>/dev/null || true
cp "$ROOT/assets/README.txt.info"   "$STAGE/README.txt.info"
cp "$ROOT/assets/EdgeSnap.prefs.info" "$STAGE/EdgeSnap.prefs.info"

cat > "$STAGE/README.txt" <<'EOF'
EdgeSnap - window snapping for AmigaOS 4.x and MorphOS
Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
Distributed under the MIT license.

TO INSTALL: double-click the Install icon.

It recognises the system, proposes the matching build, and asks before
doing anything. It puts edgesnap.library into LIBS:, the commodity into
C:, and one line into S:User-Startup so that snapping is simply there
from the next boot - nobody has to start anything by hand.

WHAT IT DOES

  - Drag a window's title bar until the POINTER touches a screen edge
    or corner: a frame shows where it will land; release and it snaps
    to that half or quarter. Docks are detected and never covered.
  - When two windows end up side by side, a handle appears on the seam:
    drag it and both are resized, so half/half becomes 60/40.
  - Hotkeys: ctrl alt cursor left/right/up snap the active window,
    ctrl alt cursor down puts it back where it was.
  - ctrl alt d prints a window dump, for diagnosing dock detection.
  - Exchange enables, disables or removes it, as with any commodity.

SETTINGS

  ENVARC:EdgeSnap.prefs - EdgeSnap.prefs here documents every setting
  and changes nothing on its own. The same KEY=VALUE words work as
  Shell arguments: EdgeSnap ZONES=halves EDGEPX=24 BYPASSQUAL=alt

FOR DEVELOPERS

  os4/esnaptest and mos/esnaptest are small clients that open
  edgesnap.library and drive its public API - useful as an example of
  how another program can ask for windows to be placed.
EOF

cat > "$STAGE/EdgeSnap.prefs" <<'EOF'
# EdgeSnap preferences
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net> - MIT.
#
# Copy to ENVARC:EdgeSnap.prefs (the installer offers to do it). Every
# setting is optional and every line here is commented out, so the file
# changes nothing until you remove a '#'. The same KEY=VALUE words also
# work as Shell arguments:  EdgeSnap ZONES=halves EDGEPX=24
#
# ZONES       which zones react: all | none | halves | corners |
#             left,right,topleft,topright,bottomleft,bottomright,maximize
# EDGEPX      how close to an edge the POINTER must be (default 12)
# CORNERDIV   corner length = usable height / this (default 4)
# DRAGMINPX   pointer travel before a drag counts (default 4)
# PREVIEW     show the zone preview frame: yes | no
# PANELDETECT reserve dock/panel strips: yes | no
# PANELMARGIN breathing room around a detected dock (default 8)
# MARGINLEFT/TOP/RIGHT/BOTTOM  extra margins of your own (default 0)
# BYPASSQUAL  hold to drag past the zones: none | alt | ctrl | shift

#ZONES=all
#EDGEPX=12
#PREVIEW=yes
#PANELDETECT=yes
#PANELMARGIN=8
#BYPASSQUAL=alt
EOF

# Rock Ridge (-r), not plain ISO9660: without it the names are mangled
# to uppercase 8.3 and a file with two dots - EdgeSnap.prefs - vanishes
# from the disc entirely. -J adds Joliet for anything reading that.
mkisofs -quiet -r -J -V "$LABEL" -o "$OUT" "$STAGE"
rm -rf "$STAGE"

echo "$OUT"
