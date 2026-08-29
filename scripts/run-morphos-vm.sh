#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Start the telegram-amiga MorphOS VM with QEMU's native macOS window
# and our own data CD. Derived from the VM's RunMorphOS-hd-deploy
# (left untouched); same monitor socket, so the helper scripts work.
# NOTE: unregistered MorphOS stops after 30 minutes - work briskly.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VMDIR="/Volumes/EXT/Macchine Virtuali/Amiga/emu/telegram-amiga/morphos"

ISO="${1:-}"
if [ -z "$ISO" ]; then
    ISO=$(ls -t "$ROOT"/build/iso-mos/*.iso 2>/dev/null | head -1 || true)
fi
if [ -z "$ISO" ] || [ ! -f "$ISO" ]; then
    echo "ERROR: no ISO - run scripts/make-morphos-iso.sh first" >&2
    exit 1
fi
ISO=$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")

if pgrep -f "qemu-system-ppc.*morphos.*hd.img" >/dev/null 2>&1; then
    echo "ERROR: a MorphOS VM is already running" >&2
    exit 1
fi

# The monitor socket lives outside the VM directory: the EXT volume
# refuses to bind one ("Operation not permitted"), and the socket is
# ours anyway - nothing in the VM's own files needs it.
MON="${MORPHOS_MONITOR:-/tmp/morphos-monitor.sock}"
rm -f "$MON"
nohup qemu-system-ppc \
  -machine pegasos2 \
  -m 512 \
  -rtc base=localtime \
  -device ati-vga,guest_hwcursor=true,romfile="" \
  -kernel "$VMDIR/boot.img" \
  -drive if=none,id=hd,format=raw,file="$VMDIR/hd.img" \
  -device ide-hd,drive=hd \
  -cdrom "$ISO" \
  -netdev user,id=net0 \
  -device ne2k_pci,netdev=net0 \
  -device usb-tablet \
  -serial file:"$VMDIR/serial.log" \
  -monitor unix:"$MON",server,nowait \
  -display cocoa \
  -name morphos-edgesnap \
  >/dev/null 2>&1 &

echo "started qemu pid $! (cocoa), CD: $ISO"
echo "monitor: $MON"
