/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_version.h - the product version, in one place.
 *
 * Not the library's version: that is ES_API_VERSION in
 * edgesnap_types.h and moves for its own reasons, because a library's
 * version is its interface. This is what the user sees - the banner,
 * the $VER: strings, the Exchange entry, the installer and the
 * readmes.
 *
 * The scripts cannot include a C header, so make-release.sh checks
 * that they still agree with this file before it packs anything: a
 * release that says 0.2 on the tin and 0.1 in the banner is the kind
 * of mistake nobody notices until a user reports it.
 */

#ifndef EDGESNAP_VERSION_H
#define EDGESNAP_VERSION_H

#define ES_VERSION       "0.2"
#define ES_VERSION_LABEL "0.2 beta"
#define ES_VERSION_DATE  "1.9.2026"

#endif /* EDGESNAP_VERSION_H */
