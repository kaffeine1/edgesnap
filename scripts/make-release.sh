#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the LhA archive that goes to testers: a drawer with its icon,
# three things to click, and the programs out of sight.
#
#   scripts/make-release.sh [version]
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION="${1:-0.1}"
STAGE="$ROOT/build/release"
OUT="$ROOT/build/EdgeSnap-$VERSION.lha"

rm -rf "$STAGE"
mkdir -p "$STAGE/EdgeSnap"
"$ROOT/scripts/stage-package.sh" "$STAGE/EdgeSnap"
cp "$ROOT/assets/EdgeSnapDrawer.info" "$STAGE/EdgeSnap.info"

rm -f "$OUT"
python3 "$ROOT/scripts/make-lha.py" "$OUT" "$STAGE" EdgeSnap.info EdgeSnap

# Unpack it again and compare: an archive nobody has opened is a
# promise, not a package.
CHECK="$ROOT/build/release-check"
rm -rf "$CHECK"
mkdir -p "$CHECK"
(cd "$CHECK" && lha xfq "$OUT" >/dev/null 2>&1) || true
if diff -r "$STAGE" "$CHECK" >/dev/null 2>&1; then
    echo "verified: unpacks byte for byte"
else
    echo "ERROR: the archive does not unpack to what went in" >&2
    diff -r "$STAGE" "$CHECK" | head -10 >&2
    exit 1
fi

ls -l "$OUT"
