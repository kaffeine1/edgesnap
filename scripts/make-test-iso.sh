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
"$ROOT/scripts/stage-package.sh" "$STAGE"

# Rock Ridge (-r), not plain ISO9660: without it the names are mangled
# to uppercase 8.3 and a file with two dots - EdgeSnap.prefs - vanishes
# from the disc entirely. -J adds Joliet for anything reading that.
mkisofs -quiet -r -J -V "$LABEL" -o "$OUT" "$STAGE"
rm -rf "$STAGE"

echo "$OUT"
