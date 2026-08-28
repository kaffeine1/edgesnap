#!/usr/bin/env python3
"""Generate EdgeSnap.info: a minimal Workbench tool icon.

Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
SPDX-License-Identifier: MIT

The icon exists so the commodity can live in SYS:WBStartup and come up
with the system, and so its settings can travel as tooltypes. It is
drawn here rather than borrowed: a 24x22 two-plane image showing a
window snapped to the left half of a screen, which is what the program
does.

Classic .info layout written below:
  DiskObject (78 bytes, big-endian)
  Image structure (20 bytes) + plane data
  do_DefaultTool : LONG len + string (len counts the NUL)
  do_ToolTypes   : LONG (n+1)*4, then n x (LONG len + string)
"""
import struct
import sys

W, H, DEPTH = 24, 22, 2

# Two bitplanes. '.' = colour 0 (transparent/background), '1' = pen 1
# (blue frame), '2' = pen 2 (white fill), '3' = pen 3 (black outline).
ART = [
    "........................",
    ".33333333333333333333333",
    ".3111111111111111111113.",
    ".3111111111111111111113.",
    ".32222222222.222222222 3",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".32222222222.2........23",
    ".33333333333333333333333",
    "........................",
    "...3333333333333333333..",
    "........................",
]


def planes(art):
    """Pack the picture into DEPTH planes of 16-bit words per row."""
    words = (W + 15) // 16
    out = bytearray()
    for plane in range(DEPTH):
        for row in art:
            bits = 0
            for x in range(words * 16):
                ch = row[x] if x < len(row) and row[x] in "0123" else "."
                val = 0 if ch == "." else int(ch)
                bit = (val >> plane) & 1
                bits = (bits << 1) | bit
            out += bits.to_bytes(words * 2, "big")
    return bytes(out)


def amiga_string(text):
    data = text.encode("latin-1") + b"\x00"
    return struct.pack(">I", len(data)) + data


WBTOOL = 3
WBPROJECT = 4


def build(default_tool, tooltypes, stack, icon_type=WBTOOL):
    image_data = planes(ART)

    # struct Image
    image = struct.pack(">hhhhhIBBI", 0, 0, W, H, DEPTH, 1, 0x03, 0x00, 0)

    # struct Gadget (44 bytes) - a plain boolean gadget owning the image
    gadget = struct.pack(">IhhhhHHHIIIhII",
                         0,            # NextGadget
                         0, 0, W, H,   # Left, Top, Width, Height
                         0x0004,       # Flags: GADGIMAGE
                         0x0003,       # Activation
                         0x0001,       # GadgetType: BOOLGADGET
                         1,            # GadgetRender (non-NULL marker)
                         0,            # SelectRender
                         0,            # GadgetText
                         0,            # MutualExclude
                         0,            # SpecialInfo
                         0)            # GadgetID + UserData packed below
    gadget += struct.pack(">I", 0)     # UserData

    # struct DiskObject
    do = struct.pack(">HH", 0xE310, 1)          # magic, version
    do += gadget                                 # do_Gadget
    do += struct.pack(">B", icon_type)           # do_Type
    do += struct.pack(">B", 0)                   # pad
    do += struct.pack(">I", 1)                   # do_DefaultTool ptr flag
    do += struct.pack(">I", 1)                   # do_ToolTypes ptr flag
    do += struct.pack(">II", 0x80000000, 0x80000000)  # NO_ICON_POSITION
    do += struct.pack(">I", 0)                   # do_DrawerData
    do += struct.pack(">I", 0)                   # do_ToolWindow
    do += struct.pack(">I", stack)               # do_StackSize

    out = bytearray(do)
    out += image
    out += image_data
    out += amiga_string(default_tool)
    out += struct.pack(">I", (len(tooltypes) + 1) * 4)
    for t in tooltypes:
        out += amiga_string(t)
    return bytes(out)


COMMODITY_TOOLTYPES = [
    "DONOTWAIT",
    "(EdgeSnap by Michele Dipace <michele.dipace@kaffeine.net>)",
    "(Settings below: remove the parentheses to enable one.)",
    "(ZONES=all)",
    "(EDGEPX=12)",
    "(CORNERDIV=4)",
    "(DRAGMINPX=4)",
    "(PREVIEW=yes)",
    "(PANELDETECT=yes)",
    "(PANELMARGIN=8)",
    "(MARGINLEFT=0)",
    "(MARGINTOP=0)",
    "(MARGINRIGHT=0)",
    "(MARGINBOTTOM=0)",
    "(BYPASSQUAL=alt)",
]

# The icons this project ships. A script gets a PROJECT icon whose
# default tool is the program that runs it: Installer for the install
# script, MultiView for the readable files.
PRESETS = {
    "commodity": (WBTOOL, "EdgeSnap", COMMODITY_TOOLTYPES, 65536),
    "install": (WBPROJECT, "Installer",
                ["APPNAME=EdgeSnap",
                 "(EdgeSnap installer - Michele Dipace)",
                 "(MINUSER=average)"], 65536),
    "text": (WBPROJECT, "SYS:Utilities/MultiView",
             ["(EdgeSnap - Michele Dipace)"], 32768),
    "data": (WBPROJECT, "SYS:Utilities/MultiView",
             ["(EdgeSnap - Michele Dipace)"], 32768),
}


def main():
    preset = "commodity"
    path = "EdgeSnap.info"
    args = sys.argv[1:]
    if args:
        path = args[0]
    if len(args) > 1:
        preset = args[1]
    if preset not in PRESETS:
        print("unknown preset %s (have: %s)" %
              (preset, ", ".join(sorted(PRESETS))))
        return 1
    icon_type, tool, tooltypes, stack = PRESETS[preset]
    data = build(tool, tooltypes, stack, icon_type)
    with open(path, "wb") as fh:
        fh.write(data)
    print("%s (%s, %d bytes)" % (path, preset, len(data)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
