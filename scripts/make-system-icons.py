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
# The package drawer is the one icon the two systems share: AmigaOS 4
# and MorphOS travel in the same archive, and each shows only some
# formats (AmigaOS 4 no PNG, MorphOS no ARGB). So the drawer icon
# carries Carlo's blue drawer twice inside one ICON form: as ARGB for
# AmigaOS 4 and as 256-colour IMAG images, the ColorIcon form MorphOS
# and AmigaOS 3.5 and later read, quantised from the same picture. It
# becomes a drawer on the way: type WBDRAWER and the 56 bytes of
# DrawerData, without which Workbench and Ambient show no drawer at all.
#
#   python3 scripts/make-system-icons.py   -> assets/os4/*.info,
#                                              assets/mos/*.info,
#                                              assets/EdgeSnapDrawer.info
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


def packbits(data):
    """ByteRun1, which is what an IMAG's RLE is at eight bits a pixel."""
    out = bytearray()
    i, n = 0, len(data)
    while i < n:
        run = 1
        while i + run < n and run < 128 and data[i + run] == data[i]:
            run += 1
        if run >= 2:
            out += bytes([257 - run, data[i]])
            i += run
            continue
        start = i
        i += 1
        while i < n and i - start < 128:
            if i + 1 < n and data[i] == data[i + 1]:
                break
            i += 1
        out += bytes([i - start - 1]) + data[start:i]
    return bytes(out)


def imag(argb, size):
    """An IMAG chunk body: 255 colours plus a transparent index 0."""
    from PIL import Image
    im = Image.frombytes("RGBA", size, argb, "raw", "ARGB")
    alpha = im.getchannel("A")
    q = im.convert("RGB").quantize(colors=255, method=Image.Quantize.MEDIANCUT)
    pal = q.getpalette()[:255 * 3]
    pal += [0] * (255 * 3 - len(pal))
    pix = bytearray(p + 1 for p in q.tobytes())
    for k, a in enumerate(alpha.tobytes()):
        if a < 128:
            pix[k] = 0
    body = packbits(bytes(pix))
    palette = bytes([0, 0, 0] + pal)                   # index 0: transparent
    head = struct.pack(">BBBBBBHH", 0, 255, 0x03, 1, 0, 8,
                       len(body) - 1, len(palette) - 1)
    return head + body + palette


def drawer(src, dst):
    blob = bytearray(open(src, "rb").read())
    at = blob.find(b"FORM")
    head, form = blob[:at], blob[at:]
    head[48] = 2                                     # do_Type: WBDRAWER
    head[50:58] = struct.pack(">II", 0, 0)           # no tool, no tooltypes
    head[66:70] = struct.pack(">I", 1)               # do_DrawerData present
    head[74:78] = struct.pack(">I", 0)               # a drawer runs nothing
    # The classic part as Carlo left it, with the DrawerData after the
    # DiskObject and in front of the images, where a reader expects it.
    # A tool icon carries no strings after its images, so none follow.
    head = head[:78] + ICON.drawer_data() + head[78:]

    size = struct.unpack(">I", form[4:8])[0]
    pos, end = 12, 8 + size
    face, argbs, others = None, [], []
    while pos + 8 <= end:
        cid = bytes(form[pos:pos + 4])
        ln = struct.unpack(">I", form[pos + 4:pos + 8])[0]
        body = bytes(form[pos + 8:pos + 8 + ln])
        if cid == b"FACE":
            face = bytearray(body)
        elif cid == b"ARGB":
            argbs.append(body)
        elif cid != b"IMAG":
            others.append((cid, body))
        pos += 8 + ln + (ln & 1)
    w, h = face[0] + 1, face[1] + 1
    face[4:6] = struct.pack(">H", 256 * 3 - 1)       # the IMAG palettes

    def piece(cid, body):
        return cid + struct.pack(">I", len(body)) + body + (b"\0" if len(body) & 1 else b"")

    # The order is AmigaOS 4's, found by trying every arrangement in its
    # Workbench (2026-09-24): Carlo's FACE carries the flag value 2, and
    # with it set the ARGB images must follow FACE directly. IMAG first
    # and the icon is dropped; the flag cleared and ARGB present and it
    # is dropped too; the flag cleared and IMAG alone, it shows. So FACE
    # as he left it, his ARGB, and the IMAG copies after them, where a
    # reader that knows no ARGB finds them.
    inner = b"ICON" + piece(b"FACE", bytes(face))
    for body in argbs:                               # ARGB for AmigaOS 4
        inner += piece(b"ARGB", body)
    for body in argbs:                               # IMAG for the others
        inner += piece(b"IMAG", imag(zlib.decompress(body[10:]), (w, h)))
    for cid, body in others:
        inner += piece(cid, body)
    # Revision 1 in the gadget's UserData, as Carlo's icon has it, tells
    # a reader that a drawer's classic part ends with dd_Flags and
    # dd_ViewModes (AROS's diskobjio.c, ProcessNewDrawerData): without
    # these six bytes it takes the start of the ICON form for them and
    # the colour images are lost. Zero is "as the user's defaults".
    head += struct.pack(">IH", 0, 0)
    out = bytes(head) + b"FORM" + struct.pack(">I", len(inner)) + inner
    open(dst, "wb").write(out)
    print("%-28s %5d bytes, drawer, ARGB and IMAG" %
          (os.path.relpath(dst, ROOT), len(out)))


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
    drawer(os.path.join(ROOT, "assets/os4/src/EdgeSnapDrawer.info"),
           os.path.join(ROOT, "assets/EdgeSnapDrawer.info"))


if __name__ == "__main__":
    main()
