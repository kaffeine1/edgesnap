#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the AROS x86_64 lane on the hosted bench and bring the binaries
# back. The Mac cannot cross-build this target: a binary linked here
# dies before main on the real system (sibling project, 2026-06).
#   scripts/build-aros.sh            -> build/aros-x86_64/{EdgeSnap,EdgeSnapPrefs}
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST="${ES_AROS_HOST:-codex_aros@ns31722153.ip-141-94-154.eu}"
KEY="${ES_AROS_KEY:-$HOME/.ssh/codex_aros_alma}"
WS="${ES_AROS_WS:-/srv/codex-aros/src/edgesnap-eval}"
TC="${ES_AROS_TOOLCHAIN:-/srv/codex-aros/src/toolchain-core-x86_64}"
SDK="${ES_AROS_SDK:-/srv/codex-aros/src/core-linux-x86_64-d/bin/linux-x86_64/AROS/Development}"
GENMODULE="${ES_AROS_GENMODULE:-/srv/codex-aros/src/core-linux-x86_64-d/bin/linux-x86_64/tools/genmodule}"
SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"

# The private diaries never leave this machine.
rsync -az -e "$SSH" --delete \
    --exclude build --exclude .git --exclude '*.local.md' --exclude '__pycache__' \
    "$ROOT/" "$HOST:$WS/"

$SSH "$HOST" "cd '$WS' && make -f Makefile.aros-x86_64 \
    AROS_TOOLCHAIN='$TC' AROS_SDK_ROOT='$SDK' GENMODULE='$GENMODULE' clean all"

mkdir -p "$ROOT/build/aros-x86_64"
# The client headers genmodule made from edgesnap.conf come back with
# the binaries: include/aros is what a client on AROS compiles against.
mkdir -p "$ROOT/include/aros"
scp -q -r -i "$KEY" "$HOST:$WS/build/aros-x86_64/gen/include/." "$ROOT/include/aros/"
cp "$ROOT/build/aros-x86_64/gen/edgesnap_lib.fd" "$ROOT/include/aros/" 2>/dev/null || \
    scp -q -i "$KEY" "$HOST:$WS/build/aros-x86_64/gen/edgesnap_lib.fd" "$ROOT/include/aros/"
for b in edgesnap.library EdgeSnap EdgeSnapPrefs esnaptest; do
    scp -q -i "$KEY" "$HOST:$WS/build/aros-x86_64/$b" "$ROOT/build/aros-x86_64/$b"
    file "$ROOT/build/aros-x86_64/$b" | grep -q "AROS Research Operating System" \
        || { echo "ERROR: $b is not an AROS binary" >&2; exit 1; }
    echo "$b: $(stat -f%z "$ROOT/build/aros-x86_64/$b") byte, AROS x86_64"
done
