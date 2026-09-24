#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Icons for the AmigaOS 4 and MorphOS builds. The commodity and the
# preferences icons were drawn for EdgeSnap by Carlo Spadoni, one set
# per system, and each system gets its own format:
#
#   AmigaOS 4  a Workbench icon whose picture is an IFF ICON form with
#              ARGB chunks (assets/os4/src/*.info). The DiskObject in
#              front of it gets the default tool and the tooltypes of
#              make-icon.py's presets through icon-tooltypes.py, which
#              leaves the picture alone, and the stack our programs
#              want; the source says 8000.
#   MorphOS    a PNG (assets/mos/src/*.png). It gets the same "icOn"
#              chunk as the AROS icons, after IHDR, with the type, the
#              stack and the tooltypes; the image is kept byte for byte.
#
# The tooltypes come from make-icon.py, so every icon of the commodity
# lists the same settings.
#
#   python3 scripts/make-system-icons.py   -> assets/os4/*.info,
#                                              assets/mos/*.info
import importlib.util
import os
import struct
import subprocess
import sys
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STACK = 65536


def load(name):
    spec = importlib.util.spec_from_file_location(
        name.replace("-", "_").replace(".py", ""),
        os.path.join(ROOT, "scripts", name))
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


ICON = load("make-icon.py")
AROS = load("make-aros-icons.py")


def os4(src, dst, preset):
    _, tool, tooltypes, _ = ICON.PRESETS[preset]
    cmd = [sys.executable, os.path.join(ROOT, "scripts", "icon-tooltypes.py"),
           src, dst, "--tool", tool]
    for t in tooltypes:
        cmd += ["--tooltype", t]
    subprocess.check_call(cmd, stdout=subprocess.DEVNULL)
    blob = bytearray(open(dst, "rb").read())
    blob[74:78] = struct.pack(">I", STACK)      # do_StackSize
    open(dst, "wb").write(bytes(blob))
    print("%-28s %5d bytes, AmigaOS 4, %d tooltype(s)" %
          (os.path.relpath(dst, ROOT), len(blob), len(tooltypes)))


def mos(src, dst, preset):
    _, _, tooltypes, _ = ICON.PRESETS[preset]
    data = AROS.chunk(AROS.WBTOOL, stack=STACK, tooltypes=tooltypes)
    raw = open(src, "rb").read()
    if raw[:8] != b"\x89PNG\r\n\x1a\n":
        raise SystemExit("%s is not a PNG" % src)
    out = raw[:8]
    pos = 8
    while pos + 8 <= len(raw):
        ln, ty = struct.unpack(">I4s", raw[pos:pos + 8])
        piece = raw[pos:pos + 12 + ln]
        pos += 12 + ln
        if ty == b"icOn":
            continue
        out += piece
        if ty == b"IHDR":
            out += struct.pack(">I", len(data)) + b"icOn" + data
            out += struct.pack(">I", zlib.crc32(b"icOn" + data) & 0xffffffff)
        if ty == b"IEND":
            break
    out += raw[pos:]
    open(dst, "wb").write(out)
    print("%-28s %5d bytes, MorphOS, %d tooltype(s)" %
          (os.path.relpath(dst, ROOT), len(out), len(tooltypes)))


def main():
    for system in ("os4", "mos"):
        os.makedirs(os.path.join(ROOT, "assets", system), exist_ok=True)
    os4(os.path.join(ROOT, "assets/os4/src/EdgeSnap.info"),
        os.path.join(ROOT, "assets/os4/EdgeSnap.info"), "commodity")
    os4(os.path.join(ROOT, "assets/os4/src/EdgeSnapPrefs.info"),
        os.path.join(ROOT, "assets/os4/EdgeSnapPrefs.info"), "prefs")
    mos(os.path.join(ROOT, "assets/mos/src/EdgeSnap.png"),
        os.path.join(ROOT, "assets/mos/EdgeSnap.info"), "commodity")
    mos(os.path.join(ROOT, "assets/mos/src/EdgeSnapPrefs.png"),
        os.path.join(ROOT, "assets/mos/EdgeSnapPrefs.info"), "prefs")


if __name__ == "__main__":
    main()
