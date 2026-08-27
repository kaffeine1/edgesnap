/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * ppcinline/edgesnap.h - MorphOS inline stubs for edgesnap.library.
 *
 * Hand-written in the shape idltool/fd2pragma would generate from
 * library/morphos/edgesnap_lib.fd; the offsets and registers here are
 * the same ABI the library's gates implement. Semantics live once, in
 * include/edgesnap.h.
 */

#ifndef PPCINLINE_EDGESNAP_H
#define PPCINLINE_EDGESNAP_H

#ifndef __PPCINLINE_MACROS_H
#include <ppcinline/macros.h>
#endif

#ifndef EDGESNAP_BASE_NAME
#define EDGESNAP_BASE_NAME EdgeSnapBase
#endif

#define ESnap_QueryCapabilities() \
    LP0(0x1e, ULONG, ESnap_QueryCapabilities, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_SnapWindow(win, zone) \
    LP2(0x24, LONG, ESnap_SnapWindow, \
        struct Window *, win, a0, ULONG, zone, d0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_UnsnapWindow(win) \
    LP1(0x2a, LONG, ESnap_UnsnapWindow, \
        struct Window *, win, a0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_QueryWindow(win, zoneOut) \
    LP2(0x30, LONG, ESnap_QueryWindow, \
        struct Window *, win, a0, ULONG *, zoneOut, a1, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_ExcludeWindow(win, exclude) \
    LP2(0x36, LONG, ESnap_ExcludeWindow, \
        struct Window *, win, a0, BOOL, exclude, d0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_SetOptionsA(tags) \
    LP1(0x3c, LONG, ESnap_SetOptionsA, \
        const struct TagItem *, tags, a0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_Enable(on) \
    LP1(0x42, LONG, ESnap_Enable, \
        BOOL, on, d0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#endif /* PPCINLINE_EDGESNAP_H */
