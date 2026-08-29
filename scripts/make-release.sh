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

# A real LhA encoder compresses (-lh5-); scripts/make-lha.py only stores
# (-lh0-), which is fine for a GitHub download and wasteful for Aminet -
# 154K against 525K for the same files. The Mac's own `lha` is Lhasa,
# which only extracts, so this wants Koji Arai's:
#   git clone https://github.com/jca02266/lha && ./configure && make
LHA_BIN=${LHA_BIN:-"$HOME/amiga-dev/tools/lha-src/src/lha"}

rm -f "$OUT"
if [ -x "$LHA_BIN" ]; then
    ( cd "$STAGE" && "$LHA_BIN" a "$OUT" EdgeSnap.info EdgeSnap >/dev/null )
    echo "packed with $LHA_BIN (compressed)"
else
    python3 "$ROOT/scripts/make-lha.py" "$OUT" "$STAGE" EdgeSnap.info EdgeSnap
    echo "WARNING: no LhA encoder at $LHA_BIN - the archive is STORED, not" >&2
    echo "         compressed. Fine for GitHub, not what Aminet expects." >&2
fi

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
