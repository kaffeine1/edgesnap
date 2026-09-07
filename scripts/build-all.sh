#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Rebuild EVERYTHING the package carries, on every lane, from clean:
# the commodity, the library, the preferences program and the test
# client for AmigaOS 4 and MorphOS (locally, in the cross containers),
# and the whole AROS x86_64 lane on the bench. Then the host tests.
#
#   scripts/build-all.sh
#
# Why one script: each lane script builds ONE target, and a lane that
# was not rebuilt after a change ships the old binary in silence. The
# first 0.3 package carried PPC libraries still at 2.4 that way. The
# release script now refuses binaries older than the sources, and this
# is the command it points at.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
cd "$ROOT"

run() {
    echo "==> $*"
    "$@"
}

run sh scripts/build-os4.sh clean all
run sh scripts/build-os4lib.sh clean all
run sh scripts/build-os4prefs.sh clean all
run sh scripts/build-os4tool.sh clean all
run sh scripts/build-morphos.sh clean all
run sh scripts/build-moslib.sh clean all
run sh scripts/build-mosprefs.sh clean all
run sh scripts/build-mostool.sh clean all
run sh scripts/build-aros.sh
run make -s -f Makefile.host test

echo
echo "all lanes rebuilt:"
for b in os4/EdgeSnap os4/edgesnap.library os4/EdgeSnapPrefs os4/esnaptest \
         morphos/EdgeSnap morphos/edgesnap.library morphos/EdgeSnapPrefs morphos/esnaptest \
         aros-x86_64/EdgeSnap aros-x86_64/edgesnap.library aros-x86_64/EdgeSnapPrefs aros-x86_64/esnaptest; do
    printf '  %-36s %s\n' "$b" "$(stat -f '%Sm' -t '%Y-%m-%d %H:%M:%S' "build/$b")"
done
