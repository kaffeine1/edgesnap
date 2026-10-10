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
#   build/channels/aminet-aarch64/  edgesnap.aarch64-aros.lha + .readme
#   build/channels/arosarchives/    edgesnap.x86_64-aros-v11.lha + _lha.readme
#                                   edgesnap.aarch64-aros.lha + _lha.readme
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
# AROS on the Raspberry Pi (aarch64) has a third archive from 0.42. Its
# entries are new ones, named as BebboSSH's: edgesnap.aarch64-aros.lha,
# filed on Aminet under i386-aros like the x86_64 one (no aarch64 token
# there either), and a new submission on the AROS Archives.
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
ARCHIVE_ARM = os.path.join(ROOT, "build", "EdgeSnap-%s-AROS-aarch64.lha" % VERSION)
# From the second AROS release on, the previous archive is named here so
# the old entry does not survive beside the new one. None for the first.
AROS_REPLACES_AMINET = "util/cdity/edgesnap.x86_64-aros.lha"  # 0.41, checked 2026-10-10
AROS_REPLACES_ARCHIVES = "utility/workbench/edgesnap.x86_64-aros-v11.lha"
# The AROS Archives give a replaced file a NEW id: the 0.4 upload replaced
# 3393 and became 3441, the 0.41 one replaced 3441 and became 3464.
# Before each release, read it off the entry's page (the fileid= in its
# links) and set it here.
AROS_ARCHIVES_FILEID = 3464  # 0.41, checked 2026-10-10
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
EdgeSnap now runs on AROS for the Raspberry Pi as well: an aarch64
build, with an archive of its own, tried on a Raspberry Pi 400.

Docks are recognised in two more shapes. A short dock standing along a
side of the screen was taken for a small widget, and a panel kept
behind the windows, as Ambient allows, was never looked at: windows
snapped over both. Now they keep their edge clear, so margins set by
hand to keep a dock uncovered can go back to 0.

Install recognises which AROS it runs on, x86_64 or aarch64, and stops
with an explanation when the archive is the one for the other
processor. On the Raspberry Pi it installs EdgeSnap by itself, with no
Installer, as it does on any AROS whose own Installer cannot copy files.
"""),
    ("WHAT IT IS", """
EdgeSnap gives AmigaOS 4.x, MorphOS and AROS (x86_64 and aarch64) the
window snapping that Windows and macOS users reach for without thinking. Drag a window against a
screen edge or corner and it glides into that half or quarter of the
screen. A frame shows where it will land before you let go.

Two windows that end up side by side share a seam, and that seam can be
dragged: both windows are resized together, so half and half becomes
sixty and forty. Docks and panels are detected and never covered.

It installs as a commodity that starts with the system, so the behaviour
is simply there. Nobody has to launch anything.
"""),
    ("THIS IS A BETA", """
Version %s is still a beta. I checked it on real MorphOS hardware, on a
Raspberry Pi 400 with AROS aarch64, and under emulation on AmigaOS 4.1
and AROS One 1.3 x86_64, each time by installing the package over an
earlier installation. On one AROS installation
Wanderer's drawers can show black areas after a snap; another program
that moves windows does the same there, and the AROS developers are
looking into how Wanderer redraws a drawer. All of it remains worth
reporting.

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
C: is stopped and removed. On the Raspberry Pi, and on any AROS whose
own Installer cannot copy files, Install does all of this by itself
after one question. Updating is just installing again: the running copy
is stopped and replaced, with no reboot, except that on the Raspberry Pi
a new library takes over at the next boot.
"""),
    ("USING IT", """
  - Drag a window's title bar until the POINTER touches an edge or a
    corner, then release.
  - Grab the seam between two tiled windows - the pointer becomes a
    double arrow - and drag it to re-balance them.
  - ctrl alt cursor left/right/up snap the active window,
    ctrl alt cursor down puts it back where it was, ctrl alt c puts
    it in the middle at the size it has, and ctrl alt z opens a
    selector under the pointer with all of that on nine cells.
  - Settings live in SYS:Prefs/EdgeSnap, native on each system: ReAction
    on AmigaOS 4, MUI on MorphOS, Zune on AROS. EdgeSnap follows changes
    at once, without being restarted.
  - Exchange enables, disables or removes it, as with any commodity.
  - EdgeSnap QUIT stops it from a Shell or a script.
"""),
    ("FOR PROGRAMMERS", """
The behaviour lives in edgesnap.library, not in the commodity: the
commodity is a client of it like any other program can be. Another
program can place a window in a zone or in any rectangle, find and move
the seam between two tiled windows, list the windows with their state,
follow their changes and lay out several windows in one call.

Since 2.10 the library also serves tiling window managers: one client
owns the interactive drag and any number own their layouts, work areas
keep an identity across a screen mode change, and a layout group can
keep every other program, the commodity's drag included, off its
windows. Since 2.20 a group can also keep gaps between its windows and
margins from the edges of the screen. The included esnaptest client
exercises the whole API on every system.

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
    "edgesnap.lha, and AROS on the Raspberry Pi has",
    "edgesnap.aarch64-aros.lha.",
    "",
]

AROS_ARCHIVES_NOTE = [
    "This is the AROS x86_64 build (ABIv11, as on AROS One x64); it does",
    "not run on i386 AROS. AmigaOS 4 and MorphOS have their own archive,",
    "on Aminet, OS4Depot and MorphOS-Storage, and AROS on the Raspberry",
    "Pi has edgesnap.aarch64-aros.lha.",
    "",
]

ARM_NOTE = [
    "This is the AROS aarch64 build, for the native AROS on the Raspberry",
    "Pi; I tried it on a Raspberry Pi 400. Aminet has no token for it, so",
    "it is filed under i386-aros, where it does not run. AROS x86_64 has",
    "edgesnap.x86_64-aros.lha, AmigaOS 4 and MorphOS edgesnap.lha.",
    "The AROS images for the Raspberry Pi I tried have no LhA command: if",
    "yours has none, unpack the archive on another computer and copy the",
    "EdgeSnap drawer to the card.",
    "",
]

ARM_ARCHIVES_NOTE = [
    "This is the AROS aarch64 build, for the native AROS on the Raspberry",
    "Pi; I tried it on a Raspberry Pi 400. AROS x86_64 has",
    "edgesnap.x86_64-aros-v11.lha here; AmigaOS 4 and MorphOS have their",
    "own archive, on Aminet, OS4Depot and MorphOS-Storage.",
    "The AROS images for the Raspberry Pi I tried have no LhA command: if",
    "yours has none, unpack the archive on another computer and copy the",
    "EdgeSnap drawer to the card.",
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


def aminet_arm_readme():
    # A new entry: nothing to replace yet.
    head = [
        "Short:        %s" % SHORT,
        "Uploader:     %s (Michele Dipace)" % EMAIL,
        "Author:       %s" % AUTHOR,
        "Type:         util/cdity",
        "Version:      %s" % VERSION,
        "Architecture: i386-aros",
        "",
    ]
    return head + ARM_NOTE + body_lines()


def arosarchives_arm_readme():
    head = [
        "name:EdgeSnap",
        "description:Tile windows by dragging them to an edge",
        "version:%s" % VERSION,
        "author:Michele Dipace",
        "submitter:Michele Dipace",
        "email:%s" % EMAIL,
        "url:%s" % URL,
        "category:utility/workbench",
        "requirements:AROS aarch64 (Raspberry Pi)",
        "license:Other",
        "distribute:yes",
        "hend:",
        "",
    ]
    return head + ARM_ARCHIVES_NOTE + body_lines()


# ------------------------------------------- the two channels done by hand
#
# Both used to be written by hand into build/channels, which this script
# empties on every run: the 0.4 ones were gone by the next release. Now
# they are made here, with the archives they go with.

# Michele runs this one: it asks for the passphrase itself, and the agent
# never types a passphrase anywhere.
AROS_ARCHIVES_SCRIPT = r'''#!/bin/bash
# EdgeSnap @@VERSION@@ to The AROS Archives: the x86_64 archive replaces
# the previous one (FileID @@FILEID@@, "Replace file"); the aarch64 one is
# a new entry. The passphrase
# (telegram-amiga SECRETS, entry for EdgeSnap's AROS Archives) is asked of
# you, never shown and never written to disk; curl reads it from stdin, so
# it does not appear among the process arguments either. The site's answer
# page prints it back in clear, which is why this script prints only OK or
# ERRORE.
#   bash publish-arosarchives-@@VERSION@@.sh          send it
#   DRY=1 bash publish-arosarchives-@@VERSION@@.sh    show the fields, send nothing
set -u
cd "$(dirname "$0")/arosarchives" || exit 1
BASE='https://archives.arosworld.org/index.php?function=submit'

field() { sed -n "s/^$1://p" "$2" | head -1; }
body() { sed '1,/^hend:$/d' "$1"; }

if [ -z "${DRY:-}" ]; then
    printf 'Passphrase AROS Archives di EdgeSnap (non viene mostrata): '
    IFS= read -r -s PASS
    echo
    [ -n "$PASS" ] || { echo "passphrase vuota: niente invio"; exit 1; }
fi

send() { # $1 = archive, $2 = URL tail
    lha=$1
    readme="${lha%.lha}_lha.readme"
    [ -f "$lha" ] && [ -f "$readme" ] || { echo "ERRORE  manca $lha o $readme"; return; }
    if [ -n "${DRY:-}" ]; then
        echo "== $lha -> $BASE$2"
        for k in name description version author submitter email url category replaces requirements license; do
            printf '   f_%-13s %s\n' "$k" "$(field $k "$readme")"
        done
        echo "   f_text         $(body "$readme" | wc -l | tr -d ' ') righe, $(body "$readme" | wc -c | tr -d ' ') byte"
        echo "   f_userfile     $(stat -f %z "$lha") byte, md5 $(md5 -q "$lha")"
        return
    fi
    out=$(mktemp)
    printf '%s' "$PASS" | curl -sS -L -o "$out" "$BASE$2" \
        --form-string "f_name=$(field name "$readme")" \
        --form-string "f_description=$(field description "$readme")" \
        --form-string "f_version=$(field version "$readme")" \
        --form-string "f_author=$(field author "$readme")" \
        --form-string "f_submitter=$(field submitter "$readme")" \
        --form-string "f_email=$(field email "$readme")" \
        --form-string "f_url=$(field url "$readme")" \
        --form-string "f_category=$(field category "$readme")" \
        --form-string "f_replaces=$(field replaces "$readme")" \
        --form-string "f_requirements=$(field requirements "$readme")" \
        --form-string "f_license=$(field license "$readme")" \
        --form-string "f_distributesubm=selected" \
        --form-string "f_distribute=on" \
        -F "f_passphrase=<-" \
        --form-string "f_text=$(body "$readme")" \
        --form-string "f_submit_go=Submit" \
        -F "f_userfile=@$lha;type=application/octet-stream"
    if grep -qi 'as soon as possible' "$out"; then
        echo "OK      $lha"
        rm -f "$out"
    else
        echo "ERRORE  $lha: risposta del sito in $out"
        echo "        (contiene la passphrase in chiaro: cancellalo dopo averlo letto)"
    fi
}

send edgesnap.x86_64-aros-v11.lha '&replace=@@FILEID@@&mode=go'
send edgesnap.aarch64-aros.lha '&mode=go'
unset PASS
echo "Coda: https://archives.arosworld.org/index.php?function=uploads"
'''

# The short description stays; the line of news changes with every
# release, like the body above.
MORPHOS_STORAGE_SHORT = """Window snapping for MorphOS, AmigaOS 4 and AROS: drag a window against a
screen edge or corner and it glides into that half or quarter of the
screen. Two windows side by side share a seam that can be dragged to
re-balance them. It installs as a commodity that starts with the system.
MIT licence."""
MORPHOS_STORAGE_NEW = ("New in %s: docks are recognised in two more shapes, "
                       "a short one along a side of the screen and an Ambient "
                       "panel kept behind the windows, so snapped windows no "
                       "longer cover them (library %s); and an AROS build for "
                       "the Raspberry Pi." % (VERSION, LIBRARY_VERSION))


def arosarchives_script():
    return (AROS_ARCHIVES_SCRIPT.replace("@@VERSION@@", VERSION)
            .replace("@@FILEID@@", str(AROS_ARCHIVES_FILEID)))


def morphos_storage_form():
    data = open(ARCHIVE, "rb").read()
    return [
        "MorphOS-Storage, EdgeSnap %s: cosa mettere nel form" % VERSION,
        "(https://www.morphos-storage.net/?page=submit, categoria "
        "Ambient/Commodities)",
        "",
        "Nome:        Michele Dipace",
        "Email:       %s" % EMAIL,
        "Homepage:    %s" % URL,
        "Software:    EdgeSnap",
        "Versione:    %s" % VERSION,
        "Archivio:    build/channels/morphos-storage/edgesnap.lha",
        "             (%d byte, md5 %s)" % (len(data),
                                            hashlib.md5(data).hexdigest()),
        "Readme:      build/channels/morphos-storage/edgesnap.readme",
        "Screenshot:  facoltativo, docs/screenshots/prefs-morphos.jpg",
        "",
        "Descrizione (corta):",
    ] + MORPHOS_STORAGE_SHORT.split("\n") + [
        "",
        "Novita' (facoltative, le porta comunque il readme):",
    ] + textwrap.wrap(MORPHOS_STORAGE_NEW, width=74)


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
    for archive in (ARCHIVE, ARCHIVE_AROS, ARCHIVE_ARM):
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
        ("aminet-aarch64", ARCHIVE_ARM, "edgesnap.aarch64-aros.lha",
         "edgesnap.aarch64-aros.readme", aminet_arm_readme()),
        # beside the x86_64 one: the publishing script sends both
        ("arosarchives", ARCHIVE_ARM, "edgesnap.aarch64-aros.lha",
         "edgesnap.aarch64-aros_lha.readme", arosarchives_arm_readme()),
    ]
    problems = []

    for name, archive, lha, readme, lines in channels:
        d = os.path.join(OUT, name)
        os.makedirs(d, exist_ok=True)
        shutil.copy2(archive, os.path.join(d, lha))
        rp = os.path.join(d, readme)
        write(rp, lines)
        # The 30-character name is Aminet's rule; the AROS Archives run
        # OS4Depot's software and take the longer _lha.readme names.
        check(rp, "%s/%s" % (name, readme), problems,
              name_max=None if name == "arosarchives" else 30)
        print("%-16s %s + %s" % (name, lha, readme))

    script = os.path.join(OUT, "publish-arosarchives-%s.sh" % VERSION)
    with open(script, "w", newline="\n") as fh:
        fh.write(arosarchives_script())
    os.chmod(script, 0o755)
    form = os.path.join(OUT, "morphos-storage",
                        "MORPHOS-STORAGE-%s.txt" % VERSION)
    write(form, morphos_storage_form())
    check(form, "morphos-storage/" + os.path.basename(form), problems,
          name_max=None)
    print("by hand          %s (replaces FileID %d), %s" %
          (os.path.relpath(script, ROOT), AROS_ARCHIVES_FILEID,
           os.path.relpath(form, ROOT)))

    print()
    for archive in (ARCHIVE, ARCHIVE_AROS, ARCHIVE_ARM):
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
