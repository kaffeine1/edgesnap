#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the AROS aarch64 lane (native Raspberry Pi) on the hosted bench
# and bring the binaries back, as scripts/build-aros.sh does for x86_64.
#   scripts/build-aros-aarch64.sh   -> build/aros-aarch64/{edgesnap.library,
#                                      EdgeSnap,EdgeSnapPrefs,esnaptest}
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
HOST="${ES_AROS_HOST:-codex_aros@ns31722153.ip-141-94-154.eu}"
KEY="${ES_AROS_KEY:-$HOME/.ssh/codex_aros_alma}"
WS="${ES_AROS_WS:-/srv/codex-aros/src/edgesnap-eval}"
TC="${ES_AROS_ARM_TOOLCHAIN:-/srv/codex-aros/src/toolchain-core-aarch64/bin}"
GENMODULE="${ES_AROS_ARM_GENMODULE:-/srv/codex-aros/src/core-raspi-aarch64-20260912/bin/linux-x86_64/tools/genmodule}"
SSH="ssh -o BatchMode=yes -o ConnectTimeout=15 -i $KEY"

# The private diaries never leave this machine.
rsync -az -e "$SSH" --delete \
    --exclude build --exclude .git --exclude '*.local.md' --exclude '__pycache__' \
    "$ROOT/" "$HOST:$WS/"

$SSH "$HOST" "cd '$WS' && make -f Makefile.aros-aarch64 \
    AROS_TOOLCHAIN='$TC' GENMODULE='$GENMODULE' clean all"

mkdir -p "$ROOT/build/aros-aarch64"
for b in edgesnap.library EdgeSnap EdgeSnapPrefs esnaptest; do
    scp -q -i "$KEY" "$HOST:$WS/build/aros-aarch64/$b" "$ROOT/build/aros-aarch64/$b"
    file "$ROOT/build/aros-aarch64/$b" | grep -q "ARM aarch64.*AROS Research Operating System" \
        || { echo "ERROR: $b is not an AROS aarch64 binary" >&2; exit 1; }
    echo "$b: $(stat -f%z "$ROOT/build/aros-aarch64/$b") byte, AROS aarch64"
done
