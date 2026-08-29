#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Rewrite the default tool and tooltypes of an existing Amiga .info,
# leaving its imagery untouched - which matters, because a ColorIcon
# keeps its picture in an IFF chunk appended after the DiskObject and
# a NewIcon keeps it in the tooltypes themselves.
#
#   python3 scripts/icon-tooltypes.py in.info out.info \
#       --tool Installer --tooltype APPNAME=EdgeSnap
import struct
import sys

DO_SIZE = 78
GADGET_AT = 4          # struct Gadget starts after magic+version


def image_size(blob, at):
    """A struct Image plus its planes."""
    w, h, depth = struct.unpack(">hhh", blob[at + 4:at + 10])
    words = (w + 15) // 16
    return 20 + words * 2 * h * depth


def parse(blob):
    magic, version = struct.unpack(">HH", blob[0:4])
    if magic != 0xE310:
        raise SystemExit("not an Amiga .info (magic %04x)" % magic)
    render, select = struct.unpack(">II", blob[GADGET_AT + 18:GADGET_AT + 26])
    default_tool, tooltypes = struct.unpack(">II", blob[50:58])
    drawer_data = struct.unpack(">I", blob[66:70])[0]
    tool_window = struct.unpack(">I", blob[70:74])[0]

    at = DO_SIZE
    if drawer_data:
        at += 56
    if render:
        at += image_size(blob, at)
    if select:
        at += image_size(blob, at)

    tool_at = at
    tool = b""
    if default_tool:
        n = struct.unpack(">I", blob[at:at + 4])[0]
        tool = blob[at + 4:at + 4 + n]
        at += 4 + n

    types_at = at
    types = []
    if tooltypes:
        count = struct.unpack(">I", blob[at:at + 4])[0] // 4 - 1
        at += 4
        for _ in range(count):
            n = struct.unpack(">I", blob[at:at + 4])[0]
            types.append(blob[at + 4:at + 4 + n])
            at += 4 + n
    if tool_window:
        n = struct.unpack(">I", blob[at:at + 4])[0]
        at += 4 + n
    return tool_at, tool, types_at, types, at


def amiga_string(text):
    data = text if isinstance(text, bytes) else text.encode("latin-1")
    if not data.endswith(b"\x00"):
        data += b"\x00"
    return struct.pack(">I", len(data)) + data


def main():
    args = sys.argv[1:]
    src, dst = args[0], args[1]
    tool = None
    new_types = []
    i = 2
    while i < len(args):
        if args[i] == "--tool":
            tool = args[i + 1]; i += 2
        elif args[i] == "--tooltype":
            new_types.append(args[i + 1]); i += 2
        else:
            raise SystemExit("unknown option " + args[i])

    blob = open(src, "rb").read()
    tool_at, old_tool, types_at, old_types, end = parse(blob)

    # A NewIcon hides its picture in tooltypes beginning IM1=/IM2= and
    # in the marker line before them: those are image data, not
    # settings, and must survive.
    keep = [t for t in old_types
            if t.startswith(b"IM1=") or t.startswith(b"IM2=")
            or t.startswith(b"*** DON'T EDIT")]

    out = bytearray(blob[:tool_at])
    if old_tool:
        out += amiga_string(tool if tool is not None else old_tool)
    types = keep + [t.encode("latin-1") for t in new_types]
    if types or old_types:
        out += struct.pack(">I", (len(types) + 1) * 4)
        for t in types:
            out += amiga_string(t)
        # the list is terminated by the count above, as Workbench expects
    out += blob[end:]                       # the IFF ColorIcon, verbatim

    open(dst, "wb").write(bytes(out))
    print("%s: tool=%s, %d tooltype(s), %d bytes" %
          (dst, (tool or old_tool.rstrip(b"\x00").decode()),
           len(types), len(out)))


main()
