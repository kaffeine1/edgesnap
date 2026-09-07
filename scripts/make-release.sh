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
VERSION="${1:-0.3}"

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

# Every binary that goes into the package must be newer than every
# source it is built from. Each lane script builds ONE target, so a
# lane that was not rebuilt after a change ships the old binary in
# silence: the first 0.3 package carried PPC libraries still at 2.4
# for exactly that reason (2026-09-06). The newest source sets the
# bar; any binary older than it stops the release and names the
# command that rebuilds everything. include/aros is left out: those
# headers are generated on the bench and come back with the AROS
# binaries, so they are newer than the PPC lanes whenever AROS was
# built last.
newest=$(find "$ROOT/core" "$ROOT/library" "$ROOT/commodity" "$ROOT/prefs" \
              "$ROOT/include" "$ROOT/tools/esnaptest.c" "$ROOT"/Makefile.* \
              -path "$ROOT/include/aros" -prune -o \
              -type f \( -name '*.c' -o -name '*.h' -o -name '*.conf' \
                         -o -name 'Makefile.*' \) -print0 \
         | xargs -0 stat -f '%m %N' | sort -n | tail -1)
newest_mtime=${newest%% *}
newest_name=${newest#* }
stale=0
for b in os4/EdgeSnap os4/edgesnap.library os4/EdgeSnapPrefs os4/esnaptest \
         morphos/EdgeSnap morphos/edgesnap.library morphos/EdgeSnapPrefs morphos/esnaptest \
         aros-x86_64/EdgeSnap aros-x86_64/edgesnap.library aros-x86_64/EdgeSnapPrefs aros-x86_64/esnaptest; do
    f="$ROOT/build/$b"
    if [ ! -f "$f" ]; then
        echo "ERROR: build/$b is missing" >&2
        stale=1
    elif [ "$(stat -f '%m' "$f")" -lt "$newest_mtime" ]; then
        echo "ERROR: build/$b is older than ${newest_name#$ROOT/}" >&2
        stale=1
    fi
done
if [ "$stale" != "0" ]; then
    echo "       rebuild every lane first: scripts/build-all.sh" >&2
    exit 1
fi
echo "binaries: all 12 newer than the newest source (${newest_name#$ROOT/})"
STAGE="$ROOT/build/release"
OUT="$ROOT/build/EdgeSnap-$VERSION.lha"
# AROS travels on its own: Aminet has no x86_64 token and the AROS
# Archives want the platform in the file name, so the same stage is
# packed twice, once without aros64/ and once with nothing else.
OUT_AROS="$ROOT/build/EdgeSnap-$VERSION-AROS64.lha"

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
EdgeSnap/aros64/EdgeSnap
EdgeSnap/aros64/EdgeSnap.info
EdgeSnap/aros64/edgesnap.library
EdgeSnap/aros64/esnaptest
EdgeSnap/aros64/EdgeSnapPrefs
EdgeSnap/aros64/EdgeSnapPrefs.info
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

# pack_one <archive> <stage copy> : pack, unpack again and compare. An
# archive nobody has opened is a promise, not a package.
pack_one() {
    out="$1"; src="$2"
    rm -f "$out"
    if [ -x "$LHA_BIN" ]; then
        ( cd "$src" && "$LHA_BIN" a "$out" EdgeSnap.info EdgeSnap >/dev/null )
        echo "packed $(basename "$out") with $LHA_BIN (compressed)"
    else
        python3 "$ROOT/scripts/make-lha.py" "$out" "$src" EdgeSnap.info EdgeSnap
        echo "WARNING: no LhA encoder at $LHA_BIN - the archive is STORED, not" >&2
        echo "         compressed. Fine for GitHub, not what Aminet expects." >&2
    fi
    check="$ROOT/build/release-check"
    rm -rf "$check"
    mkdir -p "$check"
    (cd "$check" && lha xfq "$out" >/dev/null 2>&1) || true
    if diff -r "$src" "$check" >/dev/null 2>&1; then
        echo "verified: $(basename "$out") unpacks byte for byte"
    else
        echo "ERROR: $(basename "$out") does not unpack to what went in" >&2
        diff -r "$src" "$check" | head -10 >&2
        exit 1
    fi
}

MAIN="$ROOT/build/release-main"
AROS="$ROOT/build/release-aros"
rm -rf "$MAIN" "$AROS"
cp -R "$STAGE" "$MAIN" && rm -rf "$MAIN/EdgeSnap/aros64"
cp -R "$STAGE" "$AROS" && rm -rf "$AROS/EdgeSnap/os4" "$AROS/EdgeSnap/mos"
# The combined stage names all three lanes; each archive names only
# what it contains. Aminet's i386-aros filing workaround belongs in its
# channel readme, not in the package's actual architecture declaration.
package_readme() {
    package_arch="$1"; package_dest="$2/EdgeSnap/EdgeSnap.readme"
    sed "s/^Architecture:.*/Architecture: $package_arch/" \
        "$STAGE/EdgeSnap/EdgeSnap.readme" > "$package_dest"
    if [ "$(grep -c '^Architecture:' "$package_dest")" -ne 1 ] || \
       ! grep -qxF "Architecture: $package_arch" "$package_dest"; then
        echo "ERROR: wrong package architecture in $package_dest" >&2
        exit 1
    fi
}
package_readme 'ppc-amigaos >= 4.0.0; ppc-morphos' "$MAIN"
package_readme 'x86_64-aros' "$AROS"
# The AROS archive carries icons in AROS's own PNG format in place of
# the classic Workbench ones, which AROS's icon.library reads badly:
# one sent the Installer into an illegal access, another had Wanderer
# open the Install script as a document. Made by
# scripts/make-aros-icons.py: the commodity and the prefs icons from a
# contributed AROS One style set, the rest drawn until the set covers them.
cp "$ROOT/assets/aros/EdgeSnapDrawer.info"   "$AROS/EdgeSnap.info"
cp "$ROOT/assets/aros/Install.info"          "$AROS/EdgeSnap/Install.info"
cp "$ROOT/assets/aros/EdgeSnap.guide.info"   "$AROS/EdgeSnap/EdgeSnap.guide.info"
cp "$ROOT/assets/aros/EdgeSnap.readme.info"  "$AROS/EdgeSnap/EdgeSnap.readme.info"
pack_one "$OUT" "$MAIN"
pack_one "$OUT_AROS" "$AROS"

ls -l "$OUT" "$OUT_AROS"
