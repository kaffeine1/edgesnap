/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_static.h - the public API as direct calls into the body.
 *
 * A frontend built with ES_STATIC_CORE links library/edgesnap_body.c
 * into itself and calls it directly, instead of opening
 * edgesnap.library. Same names, same arguments, same contract: the
 * commodity is written once as ES_CALL(ESnap_X)(...) and does not know
 * the difference. This is the first AROS milestone (a commodity that
 * works before an AROS library exists), and a live demonstration of
 * the point made in docs/DESIGN.md under "Why a library": the value is
 * in the portable core, the shared library is the shipping form.
 */

#ifndef EDGESNAP_STATIC_H
#define EDGESNAP_STATIC_H

#include "edgesnap_body.h"

#define ESnap_QueryCapabilities esb_query_capabilities
#define ESnap_SnapWindow        esb_snap_window
#define ESnap_UnsnapWindow      esb_unsnap_window
#define ESnap_QueryWindow       esb_query_window
#define ESnap_ExcludeWindow     esb_exclude_window
#define ESnap_SetOptionsA       esb_set_options
#define ESnap_Enable            esb_enable
#define ESnap_FeedInput         esb_input
#define ESnap_ResetInput        esb_input_reset
#define ESnap_IgnoreWindows     esb_ignore_windows
#define ESnap_QueryScreenArea   esb_query_screen_area
#define ESnap_QueryDivider      esb_query_divider
#define ESnap_QueryDividerAt    esb_query_divider_at
#define ESnap_MoveDivider       esb_move_divider
#define ESnap_MoveDividerAt     esb_move_divider_at

#endif /* EDGESNAP_STATIC_H */
