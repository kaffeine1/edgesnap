#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Put the release package on the SWAP card, for testing on the real
# MorphOS machine: SWAP:EdgeSnap/ with its drawer icon, so it can be
# opened and the Install icon double-clicked like any other package.
# The card is ejected at the end - always, or the writes may still be
# sitting in the host's cache when the card is pulled.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CARD="${1:-/Volumes/SWAP}"
DEST="$CARD/EdgeSnap"

if [ ! -d "$CARD" ]; then
    echo "ERROR: $CARD not mounted" >&2
    exit 1
fi

# Only ever clear OUR drawer, and only once it looks like ours.
if [ -d "$DEST" ] && [ -z "$(ls -A "$DEST" 2>/dev/null)" ]; then
    :
elif [ -d "$DEST" ] && [ ! -e "$DEST/Install" ] && [ ! -e "$DEST/EdgeSnap" ]; then
    echo "ERROR: $DEST exists but does not look like an EdgeSnap package" >&2
    echo "       refusing to delete it - check by hand" >&2
    exit 1
fi
rm -rf "$DEST"
mkdir -p "$DEST"

# Stage locally FIRST, then copy to the card: a copy can only be
# verified against something, and staging straight onto the card leaves
# nothing to compare it with.
STAGE="$ROOT/build/swap-stage"
rm -rf "$STAGE"
mkdir -p "$STAGE"
"$ROOT/scripts/stage-package.sh" "$STAGE"
cp -R "$STAGE/." "$DEST/"
cp "$ROOT/assets/EdgeSnapDrawer.info" "$CARD/EdgeSnap.info"

# The archive as it will reach testers, next to the drawer it unpacks
# to - so both can be tried from the same card.
if [ -f "$ROOT/build/EdgeSnap-1.0.lha" ]; then
    cp "$ROOT/build/EdgeSnap-1.0.lha" "$CARD/EdgeSnap-1.0.lha"
fi

# macOS litters removable media with these; on Ambient they show up as
# junk files next to the real ones.
find "$DEST" -name '._*' -delete 2>/dev/null || true
dot_clean "$CARD" 2>/dev/null || true

sync

# Read every file back and compare it with the source. A CF card takes
# a write into the host's cache and reports success long before the
# flash has it, so "the copy finished" is not the same as "the card
# holds it" - and the failure shows up as a truncated binary on the
# real machine, an hour later, looking like a bug in the program.
echo "verifying..."
bad=0
for f in $(cd "$STAGE" && find . -type f | sed 's|^\./||'); do
    if ! cmp -s "$STAGE/$f" "$DEST/$f"; then
        echo "MISMATCH: $f" >&2
        bad=1
    fi
done
if [ "$bad" != "0" ]; then
    echo "ERROR: the card does not hold what was copied - do NOT trust it" >&2
    exit 1
fi
echo "verified: every file on the card matches its source"
rm -rf "$STAGE"

echo "package on $DEST:"
ls "$DEST"
echo
if diskutil eject "$CARD" >/dev/null 2>&1; then
    echo "SWAP ejected - safe to pull the card"
else
    echo "WARNING: could not eject $CARD - eject it by hand before pulling it" >&2
fi
