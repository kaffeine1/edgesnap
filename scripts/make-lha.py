#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Write an LhA archive, because the only lha on this Mac (lhasa) can
# read them and not write them.
#
# Method -lh0-: stored, no compression. It is the one method every LhA
# ever shipped on an Amiga can unpack, and for a package that is mostly
# already-compact executables the loss is small - correctness that can
# be verified here beats a few kilobytes.
#
#   python3 scripts/make-lha.py out.lha <dir> [name ...]
import os
import struct
import sys
import time

def crc16(data):
    """The ARC/IBM CRC-16 that LhA stores for each file."""
    crc = 0
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


def dos_time(seconds):
    t = time.localtime(seconds)
    year = max(1980, t.tm_year)
    date = ((year - 1980) << 9) | (t.tm_mon << 5) | t.tm_mday
    clock = (t.tm_hour << 11) | (t.tm_min << 5) | (t.tm_sec // 2)
    return (date << 16) | clock


def header(name, data, stamp):
    """A level-1 header.

    Level 0 puts the whole path in the name field with 0xFF as the
    separator, which is the classic Amiga way - and which lhasa, the
    only LhA available here to check the result, refuses to extract.
    Level 1 keeps the base name in the header and the directory in an
    extended header, is what every modern lha writes, and unpacks
    correctly on both sides. Since the archive that ships is the one
    that can be verified, level 1 it is.
    """
    directory, _, base = name.rpartition("/")
    encoded = base.encode("latin-1")

    # The chain: the base header ends with the size of the first
    # extended header, and each extended header ends with the size of
    # the next one. A size counts its own type byte and that trailing
    # field, but not the size field that announced it.
    ext = b""
    if directory:
        path = (directory + "/").replace("/", "\xff").encode("latin-1")
        ext = b"\x02" + path + struct.pack("<H", 0)

    body = (b"-lh0-" +
            struct.pack("<II", len(data) + len(ext), len(data)) +
            struct.pack("<I", stamp) +
            bytes([0x20, 0x01]) +        # attribute, header level 1
            bytes([len(encoded)]) + encoded +
            struct.pack("<H", crc16(data)) +
            b"A" +                       # written on an Amiga's behalf
            struct.pack("<H", len(ext)))
    checksum = sum(body) & 0xFF
    return bytes([len(body), checksum]) + body + ext


def walk(root):
    """Every file under root, sorted, so the archive is reproducible."""
    entries = []
    for base, dirs, files in os.walk(root):
        dirs.sort()
        for f in sorted(files):
            path = os.path.join(base, f)
            name = os.path.relpath(path, root).replace(os.sep, "/")
            entries.append((name, path))
    return entries


def main():
    out, root = sys.argv[1], sys.argv[2]
    wanted = sys.argv[3:]
    blob = bytearray()

    names = wanted or sorted(os.listdir(root))
    for top in names:
        path = os.path.join(root, top)
        if os.path.isdir(path):
            for name, src in walk(path):
                data = open(src, "rb").read()
                blob += header(top + "/" + name, data,
                               dos_time(os.path.getmtime(src)))
                blob += data
        else:
            data = open(path, "rb").read()
            blob += header(top, data, dos_time(os.path.getmtime(path)))
            blob += data

    blob += b"\x00"                        # end of archive
    open(out, "wb").write(bytes(blob))
    print("%s (%d bytes)" % (out, len(blob)))


main()
