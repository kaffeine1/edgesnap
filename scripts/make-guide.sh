#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build EdgeSnap.guide from the prose template and the settings table.
# The settings section is GENERATED from core/config.c, so the ranges
# and defaults in the documentation are the ones the program enforces.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
OUT="${1:-$ROOT/build/pkg/EdgeSnap.guide}"
GEN="$ROOT/build/host/settings_guide"

mkdir -p "$(dirname "$OUT")" "$ROOT/build/host"
cc -std=c89 -pedantic -Wall -Wextra -I"$ROOT/core" -I"$ROOT/include" \
   "$ROOT/tools/settings_guide.c" "$ROOT/core/config.c" \
   "$ROOT/core/engine.c" "$ROOT/core/zones.c" "$ROOT/core/panels.c" \
   -o "$GEN"

awk -v gen="$GEN" '
    /^%%SETTINGS%%$/ { while ((gen | getline line) > 0) print line; next }
    { print }
' "$ROOT/packaging/EdgeSnap.guide.in" > "$OUT"

echo "$OUT"
