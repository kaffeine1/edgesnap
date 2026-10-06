#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build EdgeSnap.guide from the prose template and the settings table.
# The settings section is GENERATED from core/config.c, so the ranges
# and defaults in the documentation are the ones the program enforces;
# the version string comes from include/edgesnap_version.h and the
# library's version from library/aros/edgesnap.conf: written by hand,
# the guide said 0.1 (29.8.2026) in every package up to 0.41.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${1:-$ROOT/build/pkg/EdgeSnap.guide}"
GEN="$ROOT/build/host/settings_guide"

mkdir -p "$(dirname "$OUT")" "$ROOT/build/host"
cc -std=c89 -pedantic -Wall -Wextra -I"$ROOT/core" -I"$ROOT/include" \
   "$ROOT/tools/settings_guide.c" "$ROOT/core/config.c" \
   "$ROOT/core/engine.c" "$ROOT/core/zones.c" "$ROOT/core/panels.c" \
   -o "$GEN"

HDR="$ROOT/include/edgesnap_version.h"
VERSION=$(sed -n 's/^#define ES_VERSION  *"\([^"]*\)".*/\1/p' "$HDR")
DATE=$(sed -n 's/^#define ES_VERSION_DATE  *"\([^"]*\)".*/\1/p' "$HDR")
LIBVER=$(sed -n 's/^version  *\([0-9][0-9.]*\).*/\1/p' \
         "$ROOT/library/aros/edgesnap.conf")
if [ -z "$VERSION" ] || [ -z "$DATE" ] || [ -z "$LIBVER" ]; then
    echo "ERROR: no version, date or library version to put in the guide" >&2
    exit 1
fi

awk -v gen="$GEN" -v ver="$VERSION ($DATE)" -v libver="$LIBVER" '
    /^%%SETTINGS%%$/ { while ((gen | getline line) > 0) print line; next }
    { gsub(/%%VER%%/, ver); gsub(/%%LIBVER%%/, libver); print }
' "$ROOT/packaging/EdgeSnap.guide.in" > "$OUT"

echo "$OUT"
