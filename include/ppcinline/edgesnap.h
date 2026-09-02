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

#define ESnap_FeedInput(presses, motions, releases, qualifiers, report) \
    LP5NR(0x48, ESnap_FeedInput, \
        ULONG, presses, d0, ULONG, motions, d1, ULONG, releases, d2, \
        ULONG, qualifiers, d3, struct ESnapReport *, report, a0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_ResetInput(report) \
    LP1NR(0x4e, ESnap_ResetInput, \
        struct ESnapReport *, report, a0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_IgnoreWindows(windows, count) \
    LP2(0x54, LONG, ESnap_IgnoreWindows, \
        struct Window **, windows, a0, ULONG, count, d0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_QueryScreenArea(screen, area) \
    LP2(0x5a, LONG, ESnap_QueryScreenArea, \
        struct Screen *, screen, a0, struct ESnapArea *, area, a1, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_QueryDivider(thickness, divider) \
    LP2(0x60, LONG, ESnap_QueryDivider, \
        ULONG, thickness, d0, struct ESnapDivider *, divider, a0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_MoveDivider(position) \
    LP1(0x66, LONG, ESnap_MoveDivider, \
        LONG, position, d0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

/* appended for 2.3: never moved, only added to the end, and the 2.2
 * vectors above keep their arguments */
#define ESnap_QueryDividerAt(thickness, x, y, divider) \
    LP4(0x6c, LONG, ESnap_QueryDividerAt, \
        ULONG, thickness, d0, LONG, x, d1, LONG, y, d2, \
        struct ESnapDivider *, divider, a0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#define ESnap_MoveDividerAt(vertical, line, position) \
    LP3(0x72, LONG, ESnap_MoveDividerAt, \
        LONG, vertical, a0, LONG, line, a1, LONG, position, d0, \
        , EDGESNAP_BASE_NAME, 0, 0, 0, 0, 0, 0)

#endif /* PPCINLINE_EDGESNAP_H */
