/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap.h - DRAFT public API of edgesnap.library (API v0).
 *
 * STATUS: direction draft, NOT a frozen ABI. Nothing here is a
 * compatibility promise until the first public library release; until
 * then names, values and signatures may change. What is already binding
 * is the CONTRACT STYLE: every rule spelled out below (ownership,
 * errors, context, staleness) must hold in any future revision.
 *
 * ------------------------------------------------------------------
 * Window references (the "opaque handle vs struct Window *" decision)
 * ------------------------------------------------------------------
 * Public entry points take a struct Window *, the lingua franca of
 * every Intuition-facing API on both target systems, with VALIDATE-PER-
 * CALL semantics:
 *
 *   - the pointer is used as an identity key and validated against the
 *     live window lists (under LockIBase) inside the call;
 *   - the library NEVER dereferences a caller-supplied window outside
 *     that validated section, and NEVER retains it beyond the call,
 *     except as an opaque key in the snap registry;
 *   - a window that no longer exists yields ES_ERR_STALE, never a
 *     crash. Callers must treat any window as free to vanish at any
 *     moment - on Amiga systems it is;
 *   - registry state defends against pointer reuse (a new window
 *     allocated at a dead window's address): restore only acts if the
 *     window still sits where the snap put it, otherwise the state is
 *     dropped with ES_ERR_CHANGED. An inherited-state mishap is thereby
 *     bounded to "nothing happens", never "a stranger window moves".
 *
 * ------------------------------------------------------------------
 * Context rules
 * ------------------------------------------------------------------
 * All functions must be called from a normal application task context.
 * None of them may be called from interrupts, input handlers, or any
 * code running in input.device context. Calls are non-blocking except
 * for brief internal locking (LockIBase, registry semaphore). Geometry
 * changes are requested asynchronously (ChangeWindowBox semantics): a
 * successful return means "accepted", not "already applied".
 *
 * ------------------------------------------------------------------
 * Ownership
 * ------------------------------------------------------------------
 * The library never takes ownership of caller memory: windows stay the
 * application's, tag lists are read during the call only, and no
 * function returns library-owned pointers. Results come back through
 * caller-provided storage.
 *
 * ------------------------------------------------------------------
 * Versioning / binary compatibility (from the first public release on)
 * ------------------------------------------------------------------
 * - existing error values, zone values, capability bits, tag values and
 *   function signatures never change meaning and are never reused;
 * - new capabilities arrive as new bits, new options as new tags; both
 *   are discoverable, and unknown tags are ignored;
 * - clients state the API generation they were built against by opening
 *   the library with the matching version.
 */

#ifndef EDGESNAP_H
#define EDGESNAP_H

#include <exec/types.h>
#include <utility/tagitem.h>
#include <intuition/intuition.h>

#include "edgesnap_types.h"

/* ------------------------------------------------------------- tags */

/* ESnap_SetOptionsA(). Unknown tags are ignored (forward compat). */
#define ES_TAGBASE          (0x8E5A0000UL)
#define ES_OPT_EdgePx       (ES_TAGBASE + 1)  /* LONG, default 12     */
#define ES_OPT_CornerDiv    (ES_TAGBASE + 2)  /* LONG, default 4      */
#define ES_OPT_DragMinPx    (ES_TAGBASE + 3)  /* LONG, default 4      */
#define ES_OPT_MarginLeft   (ES_TAGBASE + 4)  /* LONG, default 0      */
#define ES_OPT_MarginTop    (ES_TAGBASE + 5)  /* LONG, default 0      */
#define ES_OPT_MarginRight  (ES_TAGBASE + 6)  /* LONG, default 0      */
#define ES_OPT_MarginBottom (ES_TAGBASE + 7)  /* LONG, default 0      */
#define ES_OPT_PanelDetect  (ES_TAGBASE + 8)  /* BOOL, default TRUE:
                    auto-reserve dock/panel strips (AmiDock, Ambient
                    panels) out of the usable area; margins above are
                    applied on top of what detection finds */

/* ------------------------------------------------------- functions */

/*
 * What this environment can do right now (screen, compositing, build).
 * Returns a mask of ES_CAP_*. Never fails. Capabilities can change when
 * the environment does (screen switch): query, do not cache forever.
 */
ULONG ESnap_QueryCapabilities(void);

/*
 * Snap win to zone (ES_ZONE_LEFT.._MAX). Records pre-snap geometry for
 * ESnap_UnsnapWindow. Honors the window's own size limits (the result
 * stays anchored to the zone's outer edge when clamped).
 *   ES_OK / ES_ERR_BAD_ARGS (bad zone) / ES_ERR_STALE /
 *   ES_ERR_REJECTED (unsnappable or excluded window) /
 *   ES_ERR_UNSUPPORTED / ES_ERR_NO_MEMORY (registry full; the snap is
 *   NOT performed - restore must never silently become impossible).
 */
LONG ESnap_SnapWindow(struct Window *win, ULONG zone);

/*
 * Restore win to its pre-snap geometry and drop its snap state.
 *   ES_OK / ES_ERR_STALE / ES_ERR_NOT_SNAPPED /
 *   ES_ERR_CHANGED (window was moved/resized independently since the
 *   snap: state dropped, geometry untouched).
 */
LONG ESnap_UnsnapWindow(struct Window *win);

/*
 * Current snap zone of win: *zone_out receives ES_ZONE_* (ES_ZONE_NONE
 * when not snapped).
 *   ES_OK / ES_ERR_BAD_ARGS / ES_ERR_STALE.
 */
LONG ESnap_QueryWindow(struct Window *win, ULONG *zone_out);

/*
 * Exclude (TRUE) or re-include (FALSE) win from every EdgeSnap
 * behavior: drag snapping, hotkeys, programmatic ESnap_SnapWindow.
 * The exclusion dies with the window.
 *   ES_OK / ES_ERR_STALE / ES_ERR_NO_MEMORY.
 */
LONG ESnap_ExcludeWindow(struct Window *win, BOOL exclude);

/*
 * Global options; see ES_OPT_*. Applies to future interactions.
 *   ES_OK / ES_ERR_BAD_ARGS.
 */
LONG ESnap_SetOptionsA(const struct TagItem *tags);

/*
 * Master switch for the interactive engine (drag detection, hotkeys).
 * Programmatic calls above keep working while disabled.
 *   ES_OK.
 */
LONG ESnap_Enable(BOOL on);

#endif /* EDGESNAP_H */
