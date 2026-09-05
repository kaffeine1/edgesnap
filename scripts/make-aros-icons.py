#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Icons for the AROS package, in AROS's own format: a PNG image with an
# "icOn" chunk that carries what a DiskObject carries - the icon type,
# the default tool, the tooltypes, the stack. The classic Workbench
# icons of the package are read badly by AROS's icon.library (one sent
# the Installer into an illegal access, another had Wanderer open the
# Install script as a document instead of running the Installer), so
# the AROS archive gets these instead. Drawn here, plainly, until an
# icon set in the AROS style replaces them.
#
#   python3 scripts/make-aros-icons.py     -> assets/aros/*.info
#
# The chunk format is AROS's (workbench/libs/icon/diskobjPNGio.c): a
# sequence of entries, a 32-bit big-endian attribute id followed by a
# 32-bit value for the numeric ones or a NUL-terminated string.
import os
import struct
from PIL import Image, ImageDraw, PngImagePlugin

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "assets", "aros")

ATTR_STACKSIZE = 0x80001009
ATTR_DEFAULTTOOL = 0x8000100A
ATTR_TOOLTYPE = 0x8000100B
ATTR_TYPE = 0x8000100F
ATTR_DRAWERX, ATTR_DRAWERY = 0x80001003, 0x80001004
ATTR_DRAWERWIDTH, ATTR_DRAWERHEIGHT = 0x80001005, 0x80001006

WBDRAWER, WBTOOL, WBPROJECT = 2, 3, 4

ACCENT = (34, 136, 255, 255)
PAPER = (236, 236, 236, 255)
INK = (40, 40, 48, 255)
BAR = (120, 140, 170, 255)
SHADOW = (0, 0, 0, 60)


def chunk(icon_type, default_tool=None, tooltypes=(), stack=None, drawer=None):
    data = struct.pack(">II", ATTR_TYPE, icon_type)
    if stack:
        data += struct.pack(">II", ATTR_STACKSIZE, stack)
    if default_tool:
        data += struct.pack(">I", ATTR_DEFAULTTOOL) + default_tool.encode("ascii") + b"\0"
    for t in tooltypes:
        data += struct.pack(">I", ATTR_TOOLTYPE) + t.encode("ascii") + b"\0"
    if drawer:
        x, y, w, h = drawer
        data += struct.pack(">II", ATTR_DRAWERX, x) + struct.pack(">II", ATTR_DRAWERY, y)
        data += struct.pack(">II", ATTR_DRAWERWIDTH, w) + struct.pack(">II", ATTR_DRAWERHEIGHT, h)
    return data


def canvas():
    return Image.new("RGBA", (64, 64), (0, 0, 0, 0))


def window(d, box, bar=BAR, paper=PAPER):
    x0, y0, x1, y1 = box
    d.rectangle((x0 + 2, y0 + 2, x1 + 2, y1 + 2), fill=SHADOW)
    d.rectangle(box, fill=paper, outline=INK)
    d.rectangle((x0, y0, x1, y0 + 7), fill=bar, outline=INK)
    d.rectangle((x0 + 2, y0 + 2, x0 + 5, y0 + 5), fill=PAPER)


def icon_commodity():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rectangle((4, 6, 59, 57), fill=(28, 34, 44, 255), outline=INK)   # the screen
    window(d, (8, 12, 30, 52))                                          # a window at the left half
    d.rectangle((33, 11, 56, 53), outline=ACCENT, width=3)              # the frame where the next lands
    return im


def icon_prefs():
    im = canvas()
    d = ImageDraw.Draw(im)
    window(d, (6, 8, 57, 55))
    for i, y in enumerate((26, 36, 46)):
        d.line((12, y, 51, y), fill=(150, 150, 150, 255), width=2)
        kx = (22, 40, 30)[i]
        d.ellipse((kx - 4, y - 4, kx + 4, y + 4), fill=ACCENT, outline=INK)
    return im


def icon_install():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rectangle((10, 30, 55, 57), fill=(190, 150, 100, 255), outline=INK)  # the box
    d.rectangle((10, 30, 55, 36), fill=(150, 110, 70, 255), outline=INK)
    d.polygon([(32, 6), (32, 36)], fill=ACCENT)
    d.line((32, 8, 32, 40), fill=ACCENT, width=6)                          # the arrow down
    d.polygon([(20, 34), (44, 34), (32, 48)], fill=ACCENT, outline=INK)
    return im


def icon_doc(title_lines=4):
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rectangle((14, 6, 51, 58), fill=(0, 0, 0, 60))
    d.polygon([(12, 4), (40, 4), (49, 13), (49, 56), (12, 56)], fill=PAPER, outline=INK)
    d.polygon([(40, 4), (40, 13), (49, 13)], fill=(200, 200, 200, 255), outline=INK)
    for i in range(title_lines):
        y = 22 + i * 8
        d.line((18, y, 43 - (i % 2) * 8, y), fill=(90, 90, 100, 255), width=2)
    return im


def icon_drawer():
    im = canvas()
    d = ImageDraw.Draw(im)
    d.rectangle((6, 18, 57, 56), fill=(0, 0, 0, 60))
    d.polygon([(4, 14), (24, 14), (28, 19), (56, 19), (56, 54), (4, 54)], fill=(225, 190, 110, 255), outline=INK)
    d.rectangle((4, 24, 56, 54), fill=(240, 210, 130, 255), outline=INK)
    d.rectangle((24, 33, 36, 45), outline=ACCENT, width=2)
    return im


def save(name, im, data):
    info = PngImagePlugin.PngInfo()
    info.add(b"icOn", data)
    path = os.path.join(OUT, name)
    im.save(path, "PNG", pnginfo=info)
    print("%-24s %5d bytes" % (name, os.path.getsize(path)))


def main():
    os.makedirs(OUT, exist_ok=True)
    save("EdgeSnap.info", icon_commodity(),
         chunk(WBTOOL, stack=65536, tooltypes=("DONOTWAIT",)))
    save("EdgeSnapPrefs.info", icon_prefs(), chunk(WBTOOL, stack=65536))
    save("Install.info", icon_install(),
         chunk(WBPROJECT, default_tool="Installer", tooltypes=("APPNAME=EdgeSnap",)))
    save("EdgeSnap.guide.info", icon_doc(5),
         chunk(WBPROJECT, default_tool="SYS:Utilities/MultiView"))
    save("EdgeSnap.readme.info", icon_doc(3),
         chunk(WBPROJECT, default_tool="SYS:Utilities/MultiView"))
    save("EdgeSnapDrawer.info", icon_drawer(),
         chunk(WBDRAWER, drawer=(60, 40, 480, 300)))


if __name__ == "__main__":
    main()
