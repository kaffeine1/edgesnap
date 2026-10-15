#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the AROS i386 lane (ABI v0: AROS One x86) on this Mac, with the
# alt-abiv0 cross toolchain and AROS One i386's SDK (see Makefile.aros-i386),
# and check that what comes out is AROS i386 code with nothing left
# unresolved for the loader.
#   scripts/build-aros-i386.sh   -> build/aros-i386/{edgesnap.library,
#                                   EdgeSnap,EdgeSnapPrefs,esnaptest}
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
NM="${ES_AROS_I386_NM:-$HOME/amiga-dev/toolchains/aros-i386-abiv0/i386-aros-nm}"

make -s -C "$ROOT" -f Makefile.aros-i386 clean all

for b in edgesnap.library EdgeSnap EdgeSnapPrefs esnaptest; do
    f="$ROOT/build/aros-i386/$b"
    file "$f" | grep -q "Intel 80386.*AROS Research Operating System" \
        || { echo "ERROR: $b is not an AROS i386 binary" >&2; exit 1; }
    if [ -n "$("$NM" -u "$f")" ]; then
        echo "ERROR: $b has unresolved symbols:" >&2
        "$NM" -u "$f" >&2
        exit 1
    fi
    echo "$b: $(stat -f%z "$f") byte, AROS i386"
done
