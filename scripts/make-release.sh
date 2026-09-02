#!/bin/sh
# Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
# SPDX-License-Identifier: MIT
#
# Build the LhA archive that goes to testers: a drawer with its icon,
# three things to click, and the programs out of sight.
#
#   scripts/make-release.sh [version]
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
VERSION="${1:-0.2}"

# The version lives in include/edgesnap_version.h; the scripts and the
# Installer cannot include a C header, so they are checked against it
# here. A package that says one number on the tin and another in the
# banner is a mistake nobody catches until a user reports it.
HDR="$ROOT/include/edgesnap_version.h"
HDR_VERSION=$(sed -n 's/^#define ES_VERSION  *"\([^"]*\)".*/\1/p' "$HDR")
if [ "$HDR_VERSION" != "$VERSION" ]; then
    echo "ERROR: building $VERSION but edgesnap_version.h says $HDR_VERSION" >&2
    exit 1
fi
for pair in \
    "installer/Install:(set #app-version \"$VERSION beta\")" \
    "packaging/EdgeSnap.guide.in:EdgeSnap $VERSION beta" \
    "scripts/stage-package.sh:Version:      $VERSION (beta)"
do
    f="${pair%%:*}"
    want="${pair#*:}"
    if ! grep -qF "$want" "$ROOT/$f"; then
        echo "ERROR: $f does not carry version $VERSION" >&2
        exit 1
    fi
done
echo "version $VERSION agrees across header, installer, guide and staging"

# The host tests are the only proof the portable core has; a release
# that skips them is a release that trusts the last person to have run
# them. They take a second.
if ! make -s -f "$ROOT/Makefile.host" -C "$ROOT" test >/dev/null 2>&1; then
    echo "ERROR: host tests fail - not packaging" >&2
    make -s -f "$ROOT/Makefile.host" -C "$ROOT" test 2>&1 | grep -v "passed" >&2
    exit 1
fi
echo "host tests: all suites pass"
STAGE="$ROOT/build/release"
OUT="$ROOT/build/EdgeSnap-$VERSION.lha"

rm -rf "$STAGE"
mkdir -p "$STAGE/EdgeSnap"
"$ROOT/scripts/stage-package.sh" "$STAGE/EdgeSnap"
cp "$ROOT/assets/EdgeSnapDrawer.info" "$STAGE/EdgeSnap.info"

# Every file the package promises, by name. 0.1 shipped once without
# EdgeSnap.info, the icon the manual told people to put in WBStartup,
# and nobody noticed until the archive was listed by hand: the staging
# script copies what it finds, so a build that did not happen is a
# file that is quietly not there. This list is the promise; a missing
# entry stops the release.
MANIFEST="
EdgeSnap.info
EdgeSnap/Install
EdgeSnap/Install.info
EdgeSnap/LICENSE
EdgeSnap/EdgeSnap.guide
EdgeSnap/EdgeSnap.guide.info
EdgeSnap/EdgeSnap.readme
EdgeSnap/EdgeSnap.readme.info
EdgeSnap/EdgeSnap.prefs
EdgeSnap/os4/EdgeSnap
EdgeSnap/os4/EdgeSnap.info
EdgeSnap/os4/edgesnap.library
EdgeSnap/os4/esnaptest
EdgeSnap/os4/EdgeSnapPrefs
EdgeSnap/os4/EdgeSnapPrefs.info
EdgeSnap/mos/EdgeSnap
EdgeSnap/mos/EdgeSnap.info
EdgeSnap/mos/edgesnap.library
EdgeSnap/mos/esnaptest
EdgeSnap/mos/EdgeSnapPrefs
EdgeSnap/mos/EdgeSnapPrefs.info
"
missing=0
for f in $MANIFEST; do
    if [ ! -s "$STAGE/$f" ]; then
        echo "ERROR: package is missing $f" >&2
        missing=1
    fi
done
extra=$(cd "$STAGE" && find . -type f | sed 's|^\./||' | sort)
for f in $extra; do
    case " $(echo $MANIFEST) " in
        *" $f "*) ;;
        *) echo "ERROR: package contains $f, which is not in the manifest" >&2
           missing=1 ;;
    esac
done
if [ "$missing" != "0" ]; then
    exit 1
fi
echo "manifest: all $(echo $MANIFEST | wc -w | tr -d ' ') files present, nothing extra"

# A real LhA encoder compresses (-lh5-); scripts/make-lha.py only stores
# (-lh0-), which is fine for a GitHub download and wasteful for Aminet -
# 154K against 525K for the same files. The Mac's own `lha` is Lhasa,
# which only extracts, so this wants Koji Arai's:
#   git clone https://github.com/jca02266/lha && ./configure && make
LHA_BIN=${LHA_BIN:-"$HOME/amiga-dev/tools/lha-src/src/lha"}

rm -f "$OUT"
if [ -x "$LHA_BIN" ]; then
    ( cd "$STAGE" && "$LHA_BIN" a "$OUT" EdgeSnap.info EdgeSnap >/dev/null )
    echo "packed with $LHA_BIN (compressed)"
else
    python3 "$ROOT/scripts/make-lha.py" "$OUT" "$STAGE" EdgeSnap.info EdgeSnap
    echo "WARNING: no LhA encoder at $LHA_BIN - the archive is STORED, not" >&2
    echo "         compressed. Fine for GitHub, not what Aminet expects." >&2
fi

# Unpack it again and compare: an archive nobody has opened is a
# promise, not a package.
CHECK="$ROOT/build/release-check"
rm -rf "$CHECK"
mkdir -p "$CHECK"
(cd "$CHECK" && lha xfq "$OUT" >/dev/null 2>&1) || true
if diff -r "$STAGE" "$CHECK" >/dev/null 2>&1; then
    echo "verified: unpacks byte for byte"
else
    echo "ERROR: the archive does not unpack to what went in" >&2
    diff -r "$STAGE" "$CHECK" | head -10 >&2
    exit 1
fi

ls -l "$OUT"
