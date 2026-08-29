#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build a data ISO for the MorphOS QEMU VM (same idea as the OS4 one:
# a unique 16-char volume label each time, so CDFS never reuses one).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LABEL="EL_$(date +%y%m%d_%H%M%S)"
STAGE="$ROOT/build/iso-mos/stage"
OUT="$ROOT/build/iso-mos/$LABEL.iso"

rm -rf "$STAGE"
mkdir -p "$STAGE"
for f in EdgeSnap EdgeSnapPrefs edgesnap.library esnaptest; do
    if [ -f "$ROOT/build/morphos/$f" ]; then
        cp "$ROOT/build/morphos/$f" "$STAGE/$f"
    fi
done
cp "$ROOT/build/swap-morphos/README.txt" "$STAGE/README.txt" 2>/dev/null || true

mkisofs -quiet -r -J -V "$LABEL" -o "$OUT" "$STAGE"
rm -rf "$STAGE"
echo "$OUT"
