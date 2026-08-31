#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the readme files for the three distribution channels from ONE
# source, so they cannot drift apart, and check them against the rules
# each channel enforces before a human ever sees them.
#
#   python3 scripts/make-channels.py [version]
#
# Output, one directory per channel:
#
#   build/channels/aminet/          edgesnap.lha + edgesnap.readme
#   build/channels/os4depot/        edgesnap.lha + edgesnap_lha.readme
#   build/channels/morphos-storage/ edgesnap.lha + edgesnap.readme
#
# Each channel gets its OWN directory on purpose. The archives are named
# the same for all three, and on macOS's case-insensitive filesystem two
# artefacts that differ only in name case in one directory silently
# overwrite each other - that is how a sibling project once shipped an
# OS4 binary inside its OS3 archive.

import hashlib
import os
import shutil
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
VERSION = sys.argv[1] if len(sys.argv) > 1 else "0.1"
ARCHIVE = os.path.join(ROOT, "build", "EdgeSnap-%s.lha" % VERSION)
OUT = os.path.join(ROOT, "build", "channels")

AUTHOR = "Michele Dipace <michele.dipace@kaffeine.net>"
EMAIL = "michele.dipace@kaffeine.net"
URL = "https://github.com/kaffeine1/edgesnap"

# Aminet caps Short at 40 characters, and it must not name the file, the
# version or the platform.
SHORT = "Tile windows by dragging them to an edge"

# ------------------------------------------------------------ the body
#
# One text for all three channels. Plain ASCII, wrapped below, no markup:
# these are read in a terminal, an AmigaGuide viewer, or a web page that
# does no formatting of its own.

BODY = [
    ("WHAT IT IS", """
EdgeSnap gives AmigaOS 4.x and MorphOS the window snapping that Windows
and macOS users reach for without thinking. Drag a window against a
screen edge or corner and it fills that half or quarter of the screen. A
frame shows where it will land before you let go.

Two windows that end up side by side share a seam, and that seam can be
dragged: both windows are resized together, so half and half becomes
sixty and forty. Docks and panels are detected and never covered.

It installs as a commodity that starts with the system, so the behaviour
is simply there. Nobody has to launch anything.
"""),
    ("THIS IS A BETA", """
Version %s is a first public release. It does what it says on two
machines - one AmigaOS 4.1 Final Edition and one MorphOS 3.20 - and
nowhere else yet. If it covers your dock, misses your seam, or draws
something odd with window transparency switched on, that is exactly the
report worth having.

Report it here, or to the address at the top of this file:

  %s/issues
""" % (VERSION, URL)),
    ("INSTALLING", """
Unpack the archive and double-click Install. It recognises the system,
proposes the matching build, and asks before doing anything: the library
goes to LIBS:, the commodity to C:, the preferences window to SYS:Prefs/,
and one line into S:User-Startup so that snapping is there from the next
boot. Updating is just installing again - the running copy is stopped
and replaced, with no reboot.
"""),
    ("USING IT", """
  - Drag a window's title bar until the POINTER touches an edge or a
    corner, then release.
  - Grab the seam between two tiled windows - the pointer becomes a
    double arrow - and drag it to re-balance them.
  - ctrl alt cursor left/right/up snap the active window,
    ctrl alt cursor down puts it back where it was.
  - Settings live in SYS:Prefs/EdgeSnap, native on each system: ReAction
    on AmigaOS 4, MUI on MorphOS. Change something and EdgeSnap follows
    at once, without being restarted.
  - Exchange enables, disables or removes it, as with any commodity.
  - EdgeSnap QUIT stops it from a Shell or a script.
"""),
    ("FOR PROGRAMMERS", """
The behaviour lives in edgesnap.library, not in the commodity: the
commodity is a client of it like any other program can be. Another
program can ask for a window to be placed, ask where a zone is, or find
and move the seam between two tiled windows.

The library says 2.2 while EdgeSnap says %s, and that is not a mistake:
a library's version is its interface, not its product. While EdgeSnap is
below 1.0 treat that interface as not frozen - methods are only ever
appended, never moved or removed, but names and arguments may still
change.
""" % VERSION),
    ("LICENCE AND SOURCE", """
MIT. The full text is in the LICENSE file inside the archive.

Source, issues and the design notes:
%s
""" % URL),
]


def wrap(text, width=76):
    out = []
    for para in text.strip("\n").split("\n\n"):
        lines = para.split("\n")
        if lines[0].lstrip().startswith("-"):
            out.extend(lines)          # a hand-laid list: leave it alone
        else:
            out.extend(textwrap.wrap(" ".join(l.strip() for l in lines),
                                     width=width) or [""])
        out.append("")
    return out


def body_lines():
    out = []
    for title, text in BODY:
        out.append(title)
        out.append("")
        out.extend(wrap(text))
    while out and out[-1] == "":
        out.pop()
    return out


def aminet_readme():
    head = [
        "Short:        %s" % SHORT,
        "Uploader:     %s (Michele Dipace)" % EMAIL,
        "Author:       %s" % AUTHOR,
        "Type:         util/cdity",
        "Version:      %s" % VERSION,
        # The wiki: "You can list several architectures, separated by
        # semicolons." One archive, one entry, both systems' icons.
        "Architecture: ppc-amigaos >= 4.0.0; ppc-morphos",
        # No Distribution: field. Its only legal values are restrictions,
        # and omitting it is what says "distribute freely". There is no
        # License: field on Aminet; MIT is stated in the body.
        #
        # 0.1 was the FIRST upload, so no Replaces:. Every release after
        # it needs "Replaces: util/cdity/edgesnap.lha" here, and
        # "replaces:utility/workbench/edgesnap.lha" in the OS4Depot
        # header below, or the old entry survives next to the new one -
        # and OS4Depot fails validation SILENTLY when it is missing.
        "",
    ]
    return head + body_lines()


def os4depot_readme():
    # No padding after the colons. That is the shape OS4Depot's processor
    # has actually accepted; its validation failures are silent, so this
    # is not the place to improvise formatting.
    head = [
        "name:EdgeSnap",
        "description:Tile windows by dragging them to an edge",
        "version:%s" % VERSION,
        "author:Michele Dipace",
        "submitter:Michele Dipace",
        "email:%s" % EMAIL,
        "url:%s" % URL,
        "category:utility/workbench",
        "requirements:AmigaOS 4.1",
        # Their enum has no MIT: Other is the honest slot, and the real
        # licence is named in the body and shipped in the archive.
        "license:Other",
        # replaces: belongs here only when updating an existing entry.
        "distribute:yes",
        "minosversion:4.0",
        "hend:",
        "",
    ]
    return head + body_lines()


def morphos_readme():
    # The form wants a readme to display; the Aminet one reads well and
    # keeps the three channels saying the same thing.
    return aminet_readme()


def write(path, lines):
    # LF only, no trailing blanks, exactly what the channels ask for.
    with open(path, "w", newline="\n") as fh:
        fh.write("\n".join(lines).rstrip("\n") + "\n")


def check(path, label, problems):
    raw = open(path, "rb").read()
    if b"\r" in raw:
        problems.append("%s: contains CR (must be LF only)" % label)
    for n, line in enumerate(raw.decode("ascii", "replace").split("\n"), 1):
        if len(line) > 78:
            problems.append("%s:%d: %d columns (max 78)" % (label, n, len(line)))
    try:
        raw.decode("ascii")
    except UnicodeDecodeError:
        problems.append("%s: not plain ASCII" % label)
    name = os.path.basename(path)
    if len(name) > 30:
        problems.append("%s: filename is %d characters (max 30)" %
                        (label, len(name)))


def main():
    if not os.path.exists(ARCHIVE):
        print("ERROR: %s missing - run scripts/make-release.sh first" %
              ARCHIVE, file=sys.stderr)
        return 1
    if len(SHORT) > 40:
        print("ERROR: Short is %d characters, Aminet allows 40" % len(SHORT),
              file=sys.stderr)
        return 1

    shutil.rmtree(OUT, ignore_errors=True)
    channels = [
        ("aminet", "edgesnap.readme", aminet_readme()),
        ("os4depot", "edgesnap_lha.readme", os4depot_readme()),
        ("morphos-storage", "edgesnap.readme", morphos_readme()),
    ]
    problems = []
    digest = hashlib.md5(open(ARCHIVE, "rb").read()).hexdigest()

    for name, readme, lines in channels:
        d = os.path.join(OUT, name)
        os.makedirs(d)
        shutil.copy2(ARCHIVE, os.path.join(d, "edgesnap.lha"))
        rp = os.path.join(d, readme)
        write(rp, lines)
        check(rp, "%s/%s" % (name, readme), problems)
        print("%-16s edgesnap.lha + %s" % (name, readme))

    print()
    print("archive md5: %s  (%d bytes)" % (digest, os.path.getsize(ARCHIVE)))
    if problems:
        print("\nPROBLEMS:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("readmes: LF only, ASCII, <= 78 columns, names <= 30 characters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
