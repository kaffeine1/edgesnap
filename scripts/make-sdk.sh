#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the SDK of edgesnap.library: what a program needs to use the
# library on AmigaOS 4, MorphOS and AROS, and nothing of EdgeSnap's
# own. One archive for every system, numbered as the library it
# describes.
#
#   scripts/make-sdk.sh   -> build/EdgeSnap-SDK-<library version>.lha
#
# The example binaries come from the lanes: build them first
# (scripts/build-mostool.sh, build-os4tool.sh with TARGET= and SRC=,
# and the three AROS lanes, which build it with the rest).
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
LIBVER=$(sed -n 's/^version  *\([0-9][0-9.]*\).*/\1/p' "$ROOT/library/aros/edgesnap.conf")
if [ -z "$LIBVER" ]; then
    echo "ERROR: no library version in library/aros/edgesnap.conf" >&2
    exit 1
fi
NAME=EdgeSnap-SDK
STAGE="$ROOT/build/sdk"
SDK="$STAGE/$NAME"
OUT="$ROOT/build/$NAME-$LIBVER.lha"
LHA_BIN=${LHA_BIN:-"$HOME/amiga-dev/tools/lha-src/src/lha"}

# The autodocs must describe the header as it is, and the two .fd files
# (MorphOS by hand, AROS from genmodule) must give every function the
# same vector and registers: make-autodoc checks both.
python3 "$ROOT/scripts/make-autodoc.py" --check

# The example binaries must be newer than what they are built from, or
# the archive would carry an example that is not the source beside it.
newest=$(ls -t "$ROOT/examples/sidebyside.c" "$ROOT/include/edgesnap.h" \
               "$ROOT/include/edgesnap_types.h" | head -1)
stale=0
for b in os4 morphos aros-x86_64 aros-aarch64 aros-i386; do
    f="$ROOT/build/$b/sidebyside"
    if [ ! -f "$f" ]; then
        echo "ERROR: build/$b/sidebyside is missing" >&2
        stale=1
    elif [ "$f" -ot "$newest" ]; then
        echo "ERROR: build/$b/sidebyside is older than ${newest#$ROOT/}" >&2
        stale=1
    fi
done
[ "$stale" = "0" ] || exit 1

rm -rf "$STAGE"
mkdir -p "$SDK/include/interfaces" "$SDK/include/ppcinline" \
         "$SDK/include/aros/proto" "$SDK/include/aros/clib" \
         "$SDK/include/aros/defines" "$SDK/include/aros/inline" \
         "$SDK/fd" "$SDK/autodocs" "$SDK/examples"

cp "$ROOT/include/edgesnap.h"        "$SDK/include/"
cp "$ROOT/include/edgesnap_types.h"  "$SDK/include/"
cp "$ROOT/include/interfaces/edgesnap.h" "$SDK/include/interfaces/"
cp "$ROOT/include/ppcinline/edgesnap.h"  "$SDK/include/ppcinline/"
cp "$ROOT/include/aros/proto/edgesnap.h"        "$SDK/include/aros/proto/"
cp "$ROOT/include/aros/clib/edgesnap_protos.h"  "$SDK/include/aros/clib/"
cp "$ROOT/include/aros/defines/edgesnap.h"      "$SDK/include/aros/defines/"
cp "$ROOT/include/aros/inline/edgesnap.h"       "$SDK/include/aros/inline/"
cp "$ROOT/library/morphos/edgesnap_lib.fd" "$SDK/fd/"
cp "$ROOT/docs/autodocs/edgesnap.doc"      "$SDK/autodocs/"
cp "$ROOT/examples/sidebyside.c"           "$SDK/examples/"
for pair in os4:os4 morphos:mos aros-x86_64:aros64 aros-aarch64:aarch64 aros-i386:i386; do
    mkdir -p "$SDK/examples/${pair#*:}"
    cp "$ROOT/build/${pair%%:*}/sidebyside" "$SDK/examples/${pair#*:}/sidebyside"
done
cp "$ROOT/LICENSE" "$SDK/LICENSE"

cat > "$SDK/$NAME.readme" <<EOF
Short:        Headers, autodocs and an example for edgesnap.library
Author:       michele.dipace@kaffeine.net (Michele Dipace)
Uploader:     michele.dipace@kaffeine.net (Michele Dipace)
Type:         dev/misc
Version:      $LIBVER
Architecture: ppc-amigaos >= 4.0.0; ppc-morphos; x86_64-aros; aarch64-aros; i386-aros
License:      MIT

edgesnap.library is what EdgeSnap does, window snapping in the style of
Windows and macOS, offered to any program: the EdgeSnap commodity is
only its first client. This archive is what a program needs to use it,
on AmigaOS 4, MorphOS and AROS. The library itself comes with EdgeSnap
(util/cdity on Aminet).

  include/edgesnap.h         the API, documented function by function
  include/edgesnap_types.h   error codes, zones and the revision history
  include/interfaces/        AmigaOS 4: the interface (IEdgeSnap)
  include/ppcinline/         MorphOS: the inline calls
  include/aros/              AROS: proto, clib, defines and inline
  fd/edgesnap_lib.fd         the jump table, with registers
  autodocs/edgesnap.doc      the same documentation, laid out as an
                             autodoc
  examples/sidebyside.c      a small layout client, one source for the
                             three systems, with a build of it for each
                             (os4, mos, aros64, aarch64, i386)

The example opens two windows of its own, puts them side by side as a
group with a gap between them, and moves the seam between them with the
cursor keys; Esc or a close gadget quits. It shows the order a client
follows: open the library and check its revision, register a role, make
a group, place its windows, move a seam by its token, and let everything
go again. Its first lines say how to build it on each system.

This SDK describes edgesnap.library $LIBVER, API version 2. A program
built against it runs on every later 2.x; a call that came with a later
revision than the program checks for must be guarded by the library's
revision, as the example does, since OpenLibrary() checks the version
only. The revision each call came with is in its NAME line, (V2.n).

The source of the library, the commodity and this SDK is at
https://github.com/kaffeine1/edgesnap
EOF

# Self-sufficient: the example must build against the staged headers
# alone, warnings included, or the archive is missing something.
MORPHOS_WORKDIR="$ROOT" "$ROOT/../morphos-cross/bin/morphos-run" \
    ppc-morphos-gcc -Wall -Wextra -Werror -O2 -noixemul \
    -I"build/sdk/$NAME/include" -o build/sdk/check-mos \
    "build/sdk/$NAME/examples/sidebyside.c"
OS4_WORKDIR="$ROOT" "$ROOT/../os4-cross/bin/os4-run" \
    ppc-amigaos-gcc -Wall -Wextra -Werror -O2 \
    -I"build/sdk/$NAME/include" -o build/sdk/check-os4 \
    "build/sdk/$NAME/examples/sidebyside.c"
rm -f "$STAGE/check-mos" "$STAGE/check-os4"
echo "the example builds against the SDK's own headers (MorphOS, AmigaOS 4)"

rm -f "$OUT"
if [ ! -x "$LHA_BIN" ]; then
    echo "ERROR: no LhA encoder at $LHA_BIN" >&2
    exit 1
fi
( cd "$STAGE" && "$LHA_BIN" a "$OUT" "$NAME" >/dev/null )
check="$ROOT/build/sdk-check"
rm -rf "$check"
mkdir -p "$check"
(cd "$check" && lha xfq "$OUT" >/dev/null 2>&1) || true
if ! diff -r "$STAGE/$NAME" "$check/$NAME" >/dev/null 2>&1; then
    echo "ERROR: $(basename "$OUT") does not unpack to what went in" >&2
    diff -r "$STAGE/$NAME" "$check/$NAME" | head -10 >&2
    exit 1
fi
rm -rf "$check"
echo "verified: $(basename "$OUT") unpacks byte for byte"
ls -l "$OUT"
