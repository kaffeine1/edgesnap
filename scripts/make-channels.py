#!/usr/bin/env python3
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the readme files for the five distribution channels from ONE
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
#   build/channels/aminet-aros/     edgesnap.x86_64-aros.lha + .readme
#   build/channels/arosarchives/    edgesnap.x86_64-aros-v11.lha + _lha.readme
#
# AROS has an archive of its own (make-release.sh packs it): Aminet has
# no x86_64 token, so the x86_64 build is declared i386-aros and carries
# the platform in the file name like every other x86_64 package there;
# the AROS Archives (archives.arosworld.org, OS4Depot's software, so the
# same header format) want the platform in the name too, and "-v11"
# marks the x86_64 ABIv11 build. Their form is index.php?function=submit,
# no account and no captcha; a passphrase set on our own uploads is what
# lets a later replace go through without the previous uploader's nod,
# and its value lives in SECRETS, never here.
#
# Each channel gets its OWN directory on purpose. The archives are named
# the same for all three, and on macOS's case-insensitive filesystem two
# artefacts that differ only in name case in one directory silently
# overwrite each other - that is how a sibling project once shipped an
# OS4 binary inside its OS3 archive.

import hashlib
import os
import re
import shutil
import sys
import textwrap

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
with open(os.path.join(ROOT, "include", "edgesnap_version.h")) as source:
    PRODUCT_VERSION = re.search(r'^#define ES_VERSION\s+"([^"]+)"',
                                source.read(), re.M).group(1)
with open(os.path.join(ROOT, "library", "aros", "edgesnap.conf")) as source:
    LIBRARY_VERSION = re.search(r'^version\s+(\d+\.\d+)',
                                source.read(), re.M).group(1)
VERSION = sys.argv[1] if len(sys.argv) > 1 else PRODUCT_VERSION
ARCHIVE = os.path.join(ROOT, "build", "EdgeSnap-%s.lha" % VERSION)
ARCHIVE_AROS = os.path.join(ROOT, "build", "EdgeSnap-%s-AROS64.lha" % VERSION)
# From the second AROS release on, the previous archive is named here so
# the old entry does not survive beside the new one. None for the first.
AROS_REPLACES_AMINET = None       # still waiting in /new, no published entry yet
AROS_REPLACES_ARCHIVES = "utility/workbench/edgesnap.x86_64-aros-v11.lha"
OUT = os.path.join(ROOT, "build", "channels")

AUTHOR = "Michele Dipace <michele.dipace@kaffeine.net>"
EMAIL = "michele.dipace@kaffeine.net"
URL = "https://github.com/kaffeine1/edgesnap"

# Aminet caps Short at 40 characters, and it must not name the file, the
# version or the platform.
SHORT = "Tile windows by dragging them to an edge"

# ------------------------------------------------------------ the body
#
# One text for all five channels. Plain ASCII, wrapped below, no markup:
# these are read in a terminal, an AmigaGuide viewer, or a web page that
# does no formatting of its own.

BODY = [
    ("WHAT IS NEW IN %s" % VERSION, """
0.31 corrects the installer: it was offering to copy the commented
settings template into ENVARC:, with the answer set to yes by default,
so an update could replace the settings the user had saved. A file
already in ENVARC: is now left alone and never asked about. Reported by
a distribution maintainer reading the 0.3 archive.

I have added AROS x86_64 ABIv11, with a native shared library, Zune
preferences and PNG icons. It has its own archive; the AmigaOS 4 and
MorphOS builds still travel together.

Library API 2.5 adds window enumeration, change tracking and placement
of individual windows or complete layouts in arbitrary rectangles.
API 2.6 adds window serials and lookup, so a client need not use a reused
window address as its identity.

On MorphOS as delivered a window may not leave the screen, and the pointer
stayed pinned to it short of the edge: snapping by drag now works there
too, without holding Control, because the library (2.7) is told how far
the mouse itself travelled.

Drag detection remembers where the title-bar press began and accepts
outline drags and tablet motion. Pair fill and seam resizing respect
each window's size limits, and layout changes shrink before growing.
The PPC hotkey path lets the window change finish before finding the seam.

On AROS, the preview works with outline dragging, Wanderer receives fresh
damage after resizing, and hiding the seam preserves repainted pixels.
The installer retires the first AROS preview in C: before installing
the current commodity and its icon in SYS:WBStartup.
"""),
    ("WHAT IT IS", """
EdgeSnap gives AmigaOS 4.x, MorphOS and AROS x86_64 the window snapping that Windows
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
Version %s is still a beta. I checked this build under emulation on
AmigaOS 4.1, MorphOS 3.20 and a fresh AROS One 1.3 x86_64 installation.
The AROS test passed 40 snaps with DualPNG icons, overlapping drawers
and off-screen starts, but that does not resolve the report of black
drawer areas on real hardware. On AmigaOS 4 and MorphOS the narrow seam
handle can retain old pixels after another window covers it and goes
away; this also reproduces with the original 0.2. On AROS drivers where
the screen cannot be read back reliably the preview frame is drawn as a
plain inversion; a tester on NVIDIA Nouveau saw thin traces of it stay on
the desktop, which this build addresses, awaiting his confirmation. All
of it remains worth reporting.

The interface below 1.0 is not frozen. If EdgeSnap covers your dock,
misses your seam or draws something odd, please include the system,
version banner, steps and a screenshot in your report.

Report it here, or to the address at the top of this file:

  %s/issues
""" % (VERSION, URL)),
    ("INSTALLING", """
Unpack the archive and double-click Install. It recognises the system,
proposes the matching build, and asks before doing anything: the library
goes to LIBS: and the preferences window to SYS:Prefs/. On AmigaOS 4 and
MorphOS the commodity goes to C: with a line in S:User-Startup. On AROS
it goes to SYS:WBStartup with its icon, and the first preview's copy in
C: is stopped and removed. Updating is just installing again: the
running copy is stopped and replaced, with no reboot.
"""),
    ("USING IT", """
  - Drag a window's title bar until the POINTER touches an edge or a
    corner, then release.
  - Grab the seam between two tiled windows - the pointer becomes a
    double arrow - and drag it to re-balance them.
  - ctrl alt cursor left/right/up snap the active window,
    ctrl alt cursor down puts it back where it was.
  - Settings live in SYS:Prefs/EdgeSnap, native on each system: ReAction
    on AmigaOS 4, MUI on MorphOS, Zune on AROS. EdgeSnap follows changes
    at once, without being restarted.
  - Exchange enables, disables or removes it, as with any commodity.
  - EdgeSnap QUIT stops it from a Shell or a script.
"""),
    ("FOR PROGRAMMERS", """
The behaviour lives in edgesnap.library, not in the commodity: the
commodity is a client of it like any other program can be. Another
program can ask for a window to be placed, ask where a zone is, or find
and move the seam between two tiled windows.

The API also enumerates windows, tracks changes, places whole layouts
and supplies serials for window identity. The included esnaptest client
exercises the 2.5 and 2.6 calls on both PPC systems.

The library says %s while EdgeSnap says %s, and that is not a mistake:
a library's version is its interface, not its product. While EdgeSnap is
below 1.0 treat that interface as not frozen - methods are only ever
appended, never moved or removed, but names and arguments may still
change.
""" % (LIBRARY_VERSION, VERSION)),
    ("THANKS", """
The icons are by Carlo Spadoni, who drew them for EdgeSnap and let me
ship them with it. The testers on the AROSWorld forum found the black
areas in Wanderer's drawers, the colour left along the seam and the
traces of the preview frame on one graphics driver, each with a video
or a log that made the cause findable.
"""),
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
        # From the second release on this is not optional: without it
        # the old entry survives beside the new one, and OS4Depot fails
        # validation SILENTLY when it is missing.
        "Replaces:     util/cdity/edgesnap.lha",
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
        "replaces:utility/workbench/edgesnap.lha",
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


AROS_NOTE = [
    "This is the AROS x86_64 build (ABIv11, as on AROS One x64). Aminet",
    "has no token for it, so it is filed under i386-aros: it does not run",
    "on i386 AROS. AmigaOS 4 and MorphOS have their own archive,",
    "edgesnap.lha.",
    "",
]

AROS_ARCHIVES_NOTE = [
    "This is the AROS x86_64 build (ABIv11, as on AROS One x64); it does",
    "not run on i386 AROS. AmigaOS 4 and MorphOS have their own archive,",
    "on Aminet, OS4Depot and MorphOS-Storage.",
    "",
]


def aminet_aros_readme():
    head = [
        "Short:        %s" % SHORT,
        "Uploader:     %s (Michele Dipace)" % EMAIL,
        "Author:       %s" % AUTHOR,
        "Type:         util/cdity",
        "Version:      %s" % VERSION,
        "Architecture: i386-aros",
    ]
    if AROS_REPLACES_AMINET:
        head.append("Replaces:     %s" % AROS_REPLACES_AMINET)
    head.append("")
    return head + AROS_NOTE + body_lines()


def arosarchives_readme():
    head = [
        "name:EdgeSnap",
        "description:Tile windows by dragging them to an edge",
        "version:%s" % VERSION,
        "author:Michele Dipace",
        "submitter:Michele Dipace",
        "email:%s" % EMAIL,
        "url:%s" % URL,
        # Present in the public submit form checked on 2026-09-06.
        "category:utility/workbench",
        "requirements:AROS x86_64 ABIv11 (AROS One x64)",
        "license:Other",
    ]
    if AROS_REPLACES_ARCHIVES:
        head.append("replaces:%s" % AROS_REPLACES_ARCHIVES)
    head += ["distribute:yes", "hend:", ""]
    return head + AROS_ARCHIVES_NOTE + body_lines()


def write(path, lines):
    # LF only, no trailing blanks, exactly what the channels ask for.
    with open(path, "w", newline="\n") as fh:
        fh.write("\n".join(lines).rstrip("\n") + "\n")


def check(path, label, problems, name_max=30):
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
    if name_max is not None and len(name) > name_max:
        problems.append("%s: filename is %d characters (max %d)" %
                        (label, len(name), name_max))


def main():
    if VERSION != PRODUCT_VERSION:
        print("ERROR: requested %s but the product header says %s" %
              (VERSION, PRODUCT_VERSION), file=sys.stderr)
        return 1
    for archive in (ARCHIVE, ARCHIVE_AROS):
        if not os.path.exists(archive):
            print("ERROR: %s missing - run scripts/make-release.sh first" %
                  archive, file=sys.stderr)
            return 1
    if len(SHORT) > 40:
        print("ERROR: Short is %d characters, Aminet allows 40" % len(SHORT),
              file=sys.stderr)
        return 1

    shutil.rmtree(OUT, ignore_errors=True)
    channels = [
        ("aminet", ARCHIVE, "edgesnap.lha", "edgesnap.readme", aminet_readme()),
        ("os4depot", ARCHIVE, "edgesnap.lha", "edgesnap_lha.readme", os4depot_readme()),
        ("morphos-storage", ARCHIVE, "edgesnap.lha", "edgesnap.readme", morphos_readme()),
        ("aminet-aros", ARCHIVE_AROS, "edgesnap.x86_64-aros.lha",
         "edgesnap.x86_64-aros.readme", aminet_aros_readme()),
        ("arosarchives", ARCHIVE_AROS, "edgesnap.x86_64-aros-v11.lha",
         "edgesnap.x86_64-aros-v11_lha.readme", arosarchives_readme()),
    ]
    problems = []

    for name, archive, lha, readme, lines in channels:
        d = os.path.join(OUT, name)
        os.makedirs(d)
        shutil.copy2(archive, os.path.join(d, lha))
        rp = os.path.join(d, readme)
        write(rp, lines)
        # The 30-character name is Aminet's rule; the AROS Archives run
        # OS4Depot's software and take the longer _lha.readme names.
        check(rp, "%s/%s" % (name, readme), problems,
              name_max=None if name == "arosarchives" else 30)
        print("%-16s %s + %s" % (name, lha, readme))

    print()
    for archive in (ARCHIVE, ARCHIVE_AROS):
        if os.path.exists(archive):
            digest = hashlib.md5(open(archive, "rb").read()).hexdigest()
            print("%s md5: %s  (%d bytes)" % (os.path.basename(archive), digest,
                                              os.path.getsize(archive)))
    if problems:
        print("\nPROBLEMS:", file=sys.stderr)
        for p in problems:
            print("  " + p, file=sys.stderr)
        return 1
    print("readmes: LF only, ASCII, <= 78 columns, names <= 30 characters")
    return 0


if __name__ == "__main__":
    sys.exit(main())
