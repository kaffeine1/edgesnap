#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Fill a directory with the release package, so that the CD image and a
# copy on a card are the same thing built once:
#
#   Install (+ icon)   the Commodore Installer script - double-click it
#   README.txt         what it is, what it does, how to configure it
#   EdgeSnap.prefs     the commented settings template
#   os4/ mos/          one build per system, picked by the installer
#
# The per-system drawers deliberately have no icons: nothing in them is
# meant to be started by hand.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DEST="${1:?usage: stage-package.sh <directory>}"

if [ ! -f "$ROOT/build/os4/EdgeSnap" ]; then
    echo "ERROR: build/os4/EdgeSnap missing - run scripts/build-os4.sh first" >&2
    exit 1
fi

mkdir -p "$DEST/os4" "$DEST/mos"

cp "$ROOT/build/os4/EdgeSnap"          "$DEST/os4/EdgeSnap"
cp "$ROOT/build/os4/edgesnap.library"  "$DEST/os4/edgesnap.library"
cp "$ROOT/build/os4/esnaptest"         "$DEST/os4/esnaptest"
if [ -f "$ROOT/build/morphos/EdgeSnap" ]; then
    cp "$ROOT/build/morphos/EdgeSnap"         "$DEST/mos/EdgeSnap"
    cp "$ROOT/build/morphos/edgesnap.library" "$DEST/mos/edgesnap.library"
    cp "$ROOT/build/morphos/esnaptest"        "$DEST/mos/esnaptest"
else
    echo "WARNING: no MorphOS build - mos/ left empty" >&2
fi

cp "$ROOT/installer/Install"          "$DEST/Install"
cp "$ROOT/assets/Install.info"        "$DEST/Install.info"
cp "$ROOT/assets/EdgeSnap.info"       "$DEST/os4/EdgeSnap.info"
cp "$ROOT/assets/EdgeSnap.info"       "$DEST/mos/EdgeSnap.info" 2>/dev/null || true
cp "$ROOT/assets/README.txt.info"     "$DEST/README.txt.info"
cp "$ROOT/assets/EdgeSnap.prefs.info" "$DEST/EdgeSnap.prefs.info"

cat > "$DEST/README.txt" <<'EOF'
EdgeSnap - window snapping for AmigaOS 4.x and MorphOS
Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
Distributed under the MIT license.

TO INSTALL: double-click the Install icon.

It recognises the system, proposes the matching build, and asks before
doing anything. It puts edgesnap.library into LIBS:, the commodity into
C:, and one line into S:User-Startup so that snapping is simply there
from the next boot - nobody has to start anything by hand. Updating is
just installing again: the running copy is stopped and replaced, with
no reboot.

(From a Shell instead: make this drawer the current directory - CD to
it - and run "Installer Install", because the script's paths are
relative to itself.)

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
  - "EdgeSnap QUIT" stops a running copy from a Shell or a script.
    Starting it twice by accident is simply refused.

SETTINGS

  ENVARC:EdgeSnap.prefs - EdgeSnap.prefs here documents every setting
  and changes nothing on its own. The same KEY=VALUE words work as
  Shell arguments: EdgeSnap ZONES=halves EDGEPX=24 BYPASSQUAL=alt

FOR DEVELOPERS

  os4/esnaptest and mos/esnaptest are small clients that open
  edgesnap.library and drive its public API - useful as an example of
  how another program can ask for windows to be placed.
EOF

cat > "$DEST/EdgeSnap.prefs" <<'EOF'
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
