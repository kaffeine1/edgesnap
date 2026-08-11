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

if [ ! -f "$ROOT/build/os4/EdgeSnapSpike" ]; then
    echo "ERROR: build/os4/EdgeSnapSpike missing - run scripts/build-os4.sh first" >&2
    exit 1
fi

rm -rf "$STAGE"
mkdir -p "$STAGE"
cp "$ROOT/build/os4/EdgeSnapSpike" "$STAGE/EdgeSnapSpike"
if [ -f "$ROOT/build/morphos/EdgeSnapSpike" ]; then
    cp "$ROOT/build/morphos/EdgeSnapSpike" "$STAGE/EdgeSnapSpike-MorphOS"
fi

cat > "$STAGE/README.txt" <<'EOF'
EdgeSnap spike - phase 0 test build

Install (OS4 shell; use EdgeSnapSpike-MorphOS on MorphOS):

  Copy <thisvolume>:EdgeSnapSpike RAM:
  Protect RAM:EdgeSnapSpike +e
  RAM:EdgeSnapSpike

The ISO carries no Amiga protection bits, so the Protect +e is required.

Try:
  - drag a window's title bar until the POINTER touches a screen
    edge or corner, then release -> snaps to half / quarter / max
  - ctrl alt cursor left/right/up = snap active window, down = restore
  - quit: Ctrl-C in the shell, or remove "EdgeSnap" from Exchange

The program prints every decision to the shell - that output is the
test result. Things to watch: does drag detection fire? does the
snapped geometry stick after release, or does Intuition's own drop
handling overwrite it?
EOF

hdiutil makehybrid -quiet -iso -joliet \
    -iso-volume-name "$LABEL" -joliet-volume-name "$LABEL" \
    -o "$OUT" "$STAGE"
rm -rf "$STAGE"

echo "$OUT"
