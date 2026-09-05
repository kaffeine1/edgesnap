#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Fill a directory with the release package, so that the CD image and a
# copy on a card are the same thing built once:
#
#   Install (+ icon)        the Commodore Installer - double-click it
#   EdgeSnap.guide (+icon)  the documentation
#   EdgeSnap.readme (+icon) the Aminet-style summary
#   EdgeSnap.prefs          the commented settings template (no icon)
#   os4/ mos/               one build per system, picked by the installer
#
# Only those three carry icons. The programs themselves are deliberately
# invisible: the installer knows where they go, and a drawer full of
# executables invites people to start the wrong one by hand.
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
if [ -f "$ROOT/build/os4/EdgeSnapPrefs" ]; then
    cp "$ROOT/build/os4/EdgeSnapPrefs" "$DEST/os4/EdgeSnapPrefs"
    # (infos) in the installer copies the icon along with the program,
    # so the icon has to travel next to it.
    cp "$ROOT/assets/EdgeSnapPrefs.info" "$DEST/os4/EdgeSnapPrefs.info"
fi
# The commodity's own Workbench icon. The installer does not use it -
# it writes a line into S:User-Startup - but the manual tells anyone
# who prefers SYS:WBStartup to drop the program AND this icon in there,
# and a package should carry what its documentation promises. The icon
# holds DONOTWAIT and every setting as a commented tooltype.
cp "$ROOT/assets/EdgeSnap.info" "$DEST/os4/EdgeSnap.info"
if [ -f "$ROOT/build/morphos/EdgeSnap" ]; then
    cp "$ROOT/build/morphos/EdgeSnap"         "$DEST/mos/EdgeSnap"
    cp "$ROOT/build/morphos/edgesnap.library" "$DEST/mos/edgesnap.library"
    cp "$ROOT/build/morphos/esnaptest"        "$DEST/mos/esnaptest"
    if [ -f "$ROOT/build/morphos/EdgeSnapPrefs" ]; then
        cp "$ROOT/build/morphos/EdgeSnapPrefs" "$DEST/mos/EdgeSnapPrefs"
        cp "$ROOT/assets/EdgeSnapPrefs.info" "$DEST/mos/EdgeSnapPrefs.info"
    fi
    cp "$ROOT/assets/EdgeSnap.info" "$DEST/mos/EdgeSnap.info"
else
    echo "WARNING: no MorphOS build - mos/ left empty" >&2
fi

# AROS x86_64: built on the bench (scripts/build-aros.sh brings the
# binaries back). No icons of its own yet: the installer starts the
# commodity from S:User-Startup there, and a WBStartup icon in the
# AROS style will join the package when there is one.
mkdir -p "$DEST/aros64"
if [ -f "$ROOT/build/aros-x86_64/EdgeSnap" ]; then
    cp "$ROOT/build/aros-x86_64/EdgeSnap"          "$DEST/aros64/EdgeSnap"
    cp "$ROOT/build/aros-x86_64/edgesnap.library"  "$DEST/aros64/edgesnap.library"
    cp "$ROOT/build/aros-x86_64/esnaptest"         "$DEST/aros64/esnaptest"
    cp "$ROOT/build/aros-x86_64/EdgeSnapPrefs"     "$DEST/aros64/EdgeSnapPrefs"
else
    echo "WARNING: no AROS build - aros64/ left empty" >&2
fi

# The licence travels with the package. The readme and the guide both
# say the MIT text is in here, and a licence that is only in the
# repository is not in the hands of the person holding the archive.
cp "$ROOT/LICENSE"                     "$DEST/LICENSE"

cp "$ROOT/installer/Install"           "$DEST/Install"
cp "$ROOT/assets/icons/install-es.info" "$DEST/Install.info"
cp "$ROOT/assets/icons/guide.info"      "$DEST/EdgeSnap.guide.info"
cp "$ROOT/assets/icons/readme.info"     "$DEST/EdgeSnap.readme.info"

"$ROOT/scripts/make-guide.sh" "$DEST/EdgeSnap.guide" >/dev/null

cat > "$DEST/EdgeSnap.readme" <<'EOF'
Short:        Drag windows to screen edges to tile them
Author:       michele.dipace@kaffeine.net (Michele Dipace)
Uploader:     michele.dipace@kaffeine.net (Michele Dipace)
Type:         util/wb
Version:      0.2 (beta)
Architecture: ppc-amigaos >= 4.0; ppc-morphos >= 3.0
Distribution: Aminet
License:      MIT

EdgeSnap gives AmigaOS 4.x, MorphOS and AROS x86_64 the window snapping
of Windows and macOS. Drag a window by its title bar until the POINTER touches a
screen edge or corner: a frame shows where it will land, and letting
go fills that half or quarter of the screen.

  - docks and panels are detected and never covered;
  - two windows side by side share their edge: press the seam and the
    pointer becomes a double arrow, drag it and both are resized;
  - hotkeys for those who prefer them (ctrl alt cursor keys);
  - it is a commodity: it starts with the system and Exchange enables,
    disables or removes it like any other.

The behaviour lives in edgesnap.library, not in the commodity, so any
program can ask for the same things - the commodity is a client like
any other. esnaptest, in the package, is a worked example.

TO INSTALL: double-click the Install icon. It recognises the system,
proposes the matching build, and puts everything where it belongs -
including one line in S:User-Startup, so snapping is simply there from
the next boot. Updating is installing again: no reboot needed.

Full documentation is in EdgeSnap.guide.
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
