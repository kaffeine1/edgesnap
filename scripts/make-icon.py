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


# Two bitplanes. '.' = colour 0 (transparent/background), '1' = pen 1
# (blue frame), '2' = pen 2 (white fill), '3' = pen 3 (black outline).
# The picture: a window split in two, which is what EdgeSnap does.
# Drawn rather than typed, so the size can be changed without redrawing
# it by hand. Four colours, the Workbench standard: 0 background,
# 1 black, 2 white, 3 blue.
W, H, DEPTH = 40, 36, 2

TITLE_H = 7          # rows of title bar, under the top outline
GAP = 2              # the seam between the two panes


def draw():
    rows = []
    for y in range(H):
        row = ""
        for x in range(W):
            edge = (x == 0 or x == W - 1 or y == 0 or y == H - 1)
            if edge:
                row += "1"                       # outline
            elif y <= TITLE_H:
                row += "3"                       # title bar
            elif y == TITLE_H + 1:
                row += "1"                       # under the title bar
            elif abs(x - W // 2) < GAP:
                row += "1"                       # the seam itself
            else:
                row += "2"                       # the two panes
        rows.append(row)
    return rows


ART = draw()


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


WBDISK = 1
WBDRAWER = 2
WBTOOL = 3
WBPROJECT = 4


def drawer_data():
    """
    A drawer icon carries the window Workbench opens for it: a 56-byte
    DrawerData (a NewWindow plus the scroll offsets) sitting between the
    DiskObject and the imagery. Without it the drawer has no icon at
    all and the package is invisible to anyone who does not switch
    their file manager to "show all files".
    """
    new_window = struct.pack(">hhhh", 80, 60, 420, 160)   # Left/Top/W/H
    new_window += struct.pack(">BB", 0, 1)                # Detail/BlockPen
    new_window += struct.pack(">I", 0)                    # IDCMPFlags
    new_window += struct.pack(">I", 0x0000024F)           # window flags
    new_window += struct.pack(">IIII", 0, 0, 0, 0)        # gadget..screen
    new_window += struct.pack(">I", 0)                    # BitMap
    new_window += struct.pack(">hhhh", 90, 40, 640, 400)  # Min/Max size
    new_window += struct.pack(">H", 1)                    # Type: WBENCHSCREEN
    return new_window + struct.pack(">ii", 0, 0)          # CurrentX/Y


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
    do += struct.pack(">I", 1 if icon_type == WBDRAWER else 0)
    do += struct.pack(">I", 0)                   # do_ToolWindow
    do += struct.pack(">I", stack)               # do_StackSize

    out = bytearray(do)
    if icon_type == WBDRAWER:
        out += drawer_data()
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
    # LOGFILE keeps the diagnostic the Installer writes but puts it in
    # T:, where it costs nothing; left unset it lands in the middle of
    # the user's Work: disk. MINUSER stays unset so the friendly novice
    # mode - no questions at all - remains the default.
    "install": (WBPROJECT, "Installer",
                ["APPNAME=EdgeSnap",
                 "LOGFILE=T:EdgeSnap-install.log",
                 "(EdgeSnap installer - Michele Dipace)",
                 "(MINUSER=average)"], 65536),
    # The preferences program, as it sits in SYS:Prefs/ - a tool with
    # nothing to configure about itself.
    "prefs": (WBTOOL, "EdgeSnapPrefs",
              ["(EdgeSnap preferences - Michele Dipace)"], 65536),
    "text": (WBPROJECT, "SYS:Utilities/MultiView",
             ["(EdgeSnap - Michele Dipace)"], 32768),
    "data": (WBPROJECT, "SYS:Utilities/MultiView",
             ["(EdgeSnap - Michele Dipace)"], 32768),
    # The package drawer itself, for a release that is a drawer on a
    # disk rather than the root of a CD.
    "drawer": (WBDRAWER, "", ["(EdgeSnap - Michele Dipace)"], 0),
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
