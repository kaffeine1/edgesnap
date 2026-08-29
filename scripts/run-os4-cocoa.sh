#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Start the telegram-amiga OS4 QEMU VM with QEMU's native macOS (cocoa)
# window instead of VNC: relative mouse control is far better (this image
# cannot use usb-tablet - OS4's hid.usbfd Guru-meditates on it).
#
# Derived from the VM's own run-os4-2223.sh (left untouched): same disk,
# network, monitor socket and serial log, so scripts/os4-cd.sh and the
# monitor sendkey tricks keep working. The CD starts loaded with $1, or
# with the newest ISO under build/iso/.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VMDIR="${ES_OS4_VMDIR:-/Volumes/EXT/Macchine Virtuali/Amiga/emu/telegram-amiga/os4}"

ISO="${1:-}"
if [ -z "$ISO" ]; then
    ISO=$(ls -t "$ROOT"/build/iso/*.iso 2>/dev/null | head -1 || true)
fi
if [ -z "$ISO" ] || [ ! -f "$ISO" ]; then
    echo "ERROR: no ISO found - run scripts/make-test-iso.sh first" >&2
    exit 1
fi
ISO=$(cd "$(dirname "$ISO")" && pwd)/$(basename "$ISO")

if pgrep -f "qemu-system-ppc.*amigaos4.img" >/dev/null 2>&1; then
    echo "ERROR: an OS4 QEMU is already running - quit it first:" >&2
    echo "  printf 'quit\\n' | nc -U \"$VMDIR/qemu-monitor-2223.sock\"" >&2
    exit 1
fi

cd "$VMDIR"
rm -f qemu-monitor-2223.sock
nohup /opt/homebrew/bin/qemu-system-ppc \
  -M pegasos2 \
  -m 1024 \
  -kernel ./bboot \
  -initrd ./Kickstart.zip \
  -vga none \
  -device sm501 \
  -g 1280x960x32 \
  -drive format=raw,file=./amigaos4.img,index=0,media=disk \
  -drive format=raw,file="$ISO",index=2,media=cdrom,readonly=on,id=os4cd \
  -device rtl8139,netdev=net0 \
  -netdev user,id=net0,hostfwd=tcp:127.0.0.1:2223-:22,dns=10.0.2.3 \
  -rtc base=localtime \
  -display cocoa \
  -monitor unix:qemu-monitor-2223.sock,server,nowait \
  -serial file:os4-2223-serial.log \
  >/dev/null 2>&1 &

echo "started qemu pid $! (cocoa window), CD: $ISO"
