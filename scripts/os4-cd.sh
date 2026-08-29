#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Swap the CD in the RUNNING telegram-amiga OS4 QEMU VM (monitor socket
# 2223). insert defaults to the newest ISO under build/iso/.
#   scripts/os4-cd.sh insert [path.iso]
#   scripts/os4-cd.sh eject
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
# Where the VM lives. Override with ES_OS4_VMDIR; the default is
# the author's layout, which is the only one that has ever run it.
VMDIR="${ES_OS4_VMDIR:-/Volumes/EXT/Macchine Virtuali/Amiga/emu/telegram-amiga/os4}"
MON="$VMDIR/qemu-monitor-2223.sock"

if [ ! -S "$MON" ]; then
    echo "ERROR: monitor socket not found - is the OS4 VM running?" >&2
    echo "  start it with: scripts/run-os4-cocoa.sh" >&2
    exit 1
fi

case "${1:-}" in
insert)
    ISO="${2:-}"
    if [ -z "$ISO" ]; then
        ISO=$(ls -t "$ROOT"/build/iso/*.iso 2>/dev/null | head -1 || true)
    fi
    if [ -z "$ISO" ] || [ ! -f "$ISO" ]; then
        echo "ERROR: no ISO found - run scripts/make-test-iso.sh first" >&2
        exit 1
    fi
    ISO=$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")
    printf 'change os4cd "%s"\n' "$ISO" | nc -U "$MON" >/dev/null
    echo "inserted: $ISO"
    ;;
eject)
    printf 'eject os4cd\n' | nc -U "$MON" >/dev/null
    echo "ejected"
    ;;
*)
    echo "usage: $0 insert [path.iso] | eject" >&2
    exit 1
    ;;
esac
