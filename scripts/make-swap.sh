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

"$ROOT/scripts/stage-package.sh" "$DEST"
cp "$ROOT/assets/EdgeSnapDrawer.info" "$CARD/EdgeSnap.info"

# macOS litters removable media with these; on Ambient they show up as
# junk files next to the real ones.
find "$DEST" -name '._*' -delete 2>/dev/null || true
dot_clean "$CARD" 2>/dev/null || true

sync
echo "package on $DEST:"
ls "$DEST"
echo
if diskutil eject "$CARD" >/dev/null 2>&1; then
    echo "SWAP ejected - safe to pull the card"
else
    echo "WARNING: could not eject $CARD - eject it by hand before pulling it" >&2
fi
