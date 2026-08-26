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
mkdir -p "$STAGE"
cp "$ROOT/build/os4/EdgeSnap" "$STAGE/EdgeSnap"
if [ -f "$ROOT/build/morphos/EdgeSnap" ]; then
    cp "$ROOT/build/morphos/EdgeSnap" "$STAGE/EdgeSnap-MorphOS"
fi

cat > "$STAGE/EdgeSnap.prefs" <<'EOF'
# EdgeSnap preferences - copy to ENVARC:EdgeSnap.prefs (and ENV: for
# the running session). Every setting is optional; the same KEY=VALUE
# vocabulary works as a Shell argument, e.g.
#   EdgeSnap ZONES=halves EDGEPX=24
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

ZONES=all
EDGEPX=12
PREVIEW=yes
PANELDETECT=yes
PANELMARGIN=8
BYPASSQUAL=alt
EOF

cat > "$STAGE/README.txt" <<'EOF'
EdgeSnap 0.2 - reference commodity test build (phase 2: all snap
logic lives in the shared library kernel).

Install (OS4 shell; use EdgeSnap-MorphOS on MorphOS, from disk):

  Copy <thisvolume>:EdgeSnap RAM:
  Protect RAM:EdgeSnap +e
  RAM:EdgeSnap

The ISO carries no Amiga protection bits, hence the Protect +e.

Try:
  - drag a window's title bar until the POINTER touches an edge or
    corner: a frame previews the zone; release to snap. Docks are
    detected and never covered.
  - ctrl alt cursor left/right/up = snap active window, down = restore
  - ctrl alt d = window dump (dock diagnosis)
  - preferences: EdgeSnap.prefs on this volume documents every
    setting. Copy it to ENVARC:EdgeSnap.prefs (and ENV:) or pass the
    same KEY=VALUE pairs as Shell arguments:
      RAM:EdgeSnap ZONES=halves EDGEPX=24 BYPASSQUAL=alt
    The startup banner echoes the settings actually in force.
  - quit: Ctrl-C in the shell, or remove EdgeSnap from Exchange.

The startup banner prints the build date/time - check it matches.
Diagnostics print when no drag is in flight.
EOF

hdiutil makehybrid -quiet -iso -joliet \
    -iso-volume-name "$LABEL" -joliet-volume-name "$LABEL" \
    -o "$OUT" "$STAGE"
rm -rf "$STAGE"

echo "$OUT"
