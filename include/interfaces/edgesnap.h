/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * interfaces/edgesnap.h - AmigaOS 4 "main" interface of edgesnap.library.
 *
 * DRAFT API v0: the method order below is part of the ABI once the
 * first public release ships. New methods are only ever APPENDED, and
 * nothing already present is removed or reordered - that is what lets
 * an old client keep running against a newer library.
 *
 * Semantics (ownership, errors, staleness, context) are documented
 * once, in include/edgesnap.h. This file only says how to reach them
 * on OS4.
 */

#ifndef INTERFACES_EDGESNAP_H
#define INTERFACES_EDGESNAP_H

#include <exec/types.h>
#include <exec/interfaces.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>

#include "edgesnap.h"

struct EdgeSnapIFace
{
    struct InterfaceData Data;

    ULONG APICALL (*Obtain)(struct EdgeSnapIFace *Self);
    ULONG APICALL (*Release)(struct EdgeSnapIFace *Self);
    void APICALL (*Expunge)(struct EdgeSnapIFace *Self);
    struct Interface * APICALL (*Clone)(struct EdgeSnapIFace *Self);

    /* --- API v0. Append only. --- */
    ULONG APICALL (*ESnap_QueryCapabilities)(struct EdgeSnapIFace *Self);
    LONG APICALL (*ESnap_SnapWindow)(struct EdgeSnapIFace *Self,
                                     struct Window *win, ULONG zone);
    LONG APICALL (*ESnap_UnsnapWindow)(struct EdgeSnapIFace *Self,
                                       struct Window *win);
    LONG APICALL (*ESnap_QueryWindow)(struct EdgeSnapIFace *Self,
                                      struct Window *win, ULONG *zone_out);
    LONG APICALL (*ESnap_ExcludeWindow)(struct EdgeSnapIFace *Self,
                                        struct Window *win, BOOL exclude);
    LONG APICALL (*ESnap_SetOptionsA)(struct EdgeSnapIFace *Self,
                                      const struct TagItem *tags);
    LONG APICALL (*ESnap_Enable)(struct EdgeSnapIFace *Self, BOOL on);

    /* --- frontend integration, appended after the first set --- */
    void APICALL (*ESnap_FeedInput)(struct EdgeSnapIFace *Self,
                                    ULONG presses, ULONG motions,
                                    ULONG releases, ULONG qualifiers,
                                    struct ESnapReport *report);
    void APICALL (*ESnap_ResetInput)(struct EdgeSnapIFace *Self,
                                     struct ESnapReport *report);
    LONG APICALL (*ESnap_IgnoreWindows)(struct EdgeSnapIFace *Self,
                                        struct Window **windows,
                                        ULONG count);
    LONG APICALL (*ESnap_QueryScreenArea)(struct EdgeSnapIFace *Self,
                                          struct Screen *screen,
                                          struct ESnapArea *area);

    /* --- the divider, appended after the frontend group --- */
    LONG APICALL (*ESnap_QueryDivider)(struct EdgeSnapIFace *Self,
                                       ULONG thickness,
                                       struct ESnapDivider *divider);
    LONG APICALL (*ESnap_MoveDivider)(struct EdgeSnapIFace *Self,
                                      LONG position);
};

#endif /* INTERFACES_EDGESNAP_H */
