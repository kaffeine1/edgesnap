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
#define ES_OPT_PanelMargin  (ES_TAGBASE + 9)  /* LONG, default 8: breathing
                    room left around a detected panel */
#define ES_OPT_Zones        (ES_TAGBASE + 10) /* ULONG mask of ES_ZONEBIT(),
                    default ES_ZONEMASK_ALL: which zones react         */
#define ES_OPT_Preview      (ES_TAGBASE + 11) /* BOOL, default TRUE      */
#define ES_OPT_BypassQual   (ES_TAGBASE + 12) /* ES_QUAL_*, default NONE */

/* Bypass qualifier values for ES_OPT_BypassQual. */
#define ES_QUAL_NONE  0
#define ES_QUAL_ALT   1
#define ES_QUAL_CTRL  2
#define ES_QUAL_SHIFT 3

/* ------------------------------------------ frontend integration */

/*
 * The entry points below are what a FRONTEND needs - EdgeSnap's own
 * commodity, or any alternative one. An application that just wants to
 * place its windows never touches them.
 *
 * The division of labour: the frontend owns the input handler (a
 * commodities CxCustom object, which must only count events and signal
 * its task - never call Intuition) and the drawing of the preview
 * frame, because that is platform decoration. Everything that decides
 * or remembers stays in the library. The frontend hands over raw event
 * counts and gets back a report of what happened; a library never
 * prints, so the frontend logs on its behalf.
 */

/* Rectangle and per-edge insets, in screen coordinates. */
struct ESnapRect {
    LONG x, y, w, h;
};

struct ESnapArea {
    struct ESnapRect usable;   /* what snapping may use            */
    LONG insetLeft, insetTop;  /* what docks/margins reserved      */
    LONG insetRight, insetBottom;
};

/*
 * What one ESnap_FeedInput() call did. Fixed for API v0: later
 * revisions add information through new functions, never by growing
 * this structure, so an old client keeps working.
 */
struct ESnapReport {
    LONG dragStarted;          /* a window drag was recognised     */
    LONG zoneChanged;          /* zone holds the new zone          */
    ULONG zone;
    LONG previewShow;          /* draw the frame at previewRect    */
    LONG previewHide;          /* erase it                         */
    struct ESnapRect previewRect;
    struct Screen *previewScreen;
    LONG snapped;              /* a snap was attempted             */
    ULONG snapZone;
    LONG snapResult;           /* ES_OK or an ES_ERR_* code        */
    struct Window *snapWindow;
    LONG dragActive;           /* still tracking a drag            */
};

/*
 * Feed the engine the events seen since the last call: how many
 * presses, pointer motions and releases arrived (counts, so a fast
 * press+release pair is never lost) and the qualifier bits that came
 * with the last mouse event. Fills *report; never fails.
 */
void ESnap_FeedInput(ULONG presses, ULONG motions, ULONG releases,
                     ULONG qualifiers, struct ESnapReport *report);

/* Abandon any tracking (the frontend was disabled, or is quitting).
 * The report asks for the preview frame to be erased. */
void ESnap_ResetInput(struct ESnapReport *report);

/*
 * Windows the dock scan must never mistake for a panel: a frontend's
 * own preview frame is thin, edge-flush and long, which is exactly a
 * dock's shape. Pass NULL/0 to clear.
 */
LONG ESnap_IgnoreWindows(struct Window **windows, ULONG count);

/*
 * Where snapping may place windows on this screen, and what was
 * reserved. Useful to a frontend's diagnostics, and to any client that
 * wants to lay windows out itself.
 *   ES_OK / ES_ERR_BAD_ARGS.
 */
LONG ESnap_QueryScreenArea(struct Screen *screen, struct ESnapArea *area);

/* --------------------------------------------------- the divider */

/*
 * When two windows are snapped side by side, the seam between them is
 * a handle: dragging it resizes both, so a half/half split becomes
 * 60/40 without touching either window's size gadget. The library
 * finds the seam and performs the resize; a frontend puts a thin
 * window with a resize pointer on top of it, and reports the drags.
 */
struct ESnapDivider {
    LONG present;              /* 0: there is no pair to divide      */
    LONG vertical;             /* 1: dragged left/right, 0: up/down  */
    struct ESnapRect strip;    /* where to place the handle          */
    LONG position;             /* current seam coordinate            */
    LONG minPosition;          /* how far it may be dragged          */
    LONG maxPosition;
    LONG windowCount;          /* how many windows this seam moves   */
    struct Window *windowA;    /* first window on the left/top side  */
    struct Window *windowB;    /* first on the right/bottom side     */
};

/*
 * Where the divider is, if any. thickness is how wide the handle
 * should be. Fills *divider (with present = 0 when there is no pair).
 *   ES_OK / ES_ERR_BAD_ARGS.
 */
LONG ESnap_QueryDivider(ULONG thickness, struct ESnapDivider *divider);

/*
 * The divider nearest a point, which is how a frontend asks "what is
 * the pointer on?". A layout can hold several: one window facing two
 * stacked ones has a vertical seam moving all three and a horizontal
 * seam moving only the two, and four quadrants have one of each. Pass
 * the pointer position in screen coordinates; present is 0 when
 * nothing is within reach.
 *   ES_OK / ES_ERR_BAD_ARGS.
 */
LONG ESnap_QueryDividerAt(ULONG thickness, LONG x, LONG y,
                          struct ESnapDivider *divider);

/*
 * Drag the FIRST divider in the layout to a screen coordinate. Kept
 * with the arguments it had in 2.2, because a library's major version
 * is its compatibility promise and a 2.x client must keep working on
 * every later 2.x: this vector is neither moved nor changed. With more
 * than one seam on screen "the first" is whichever the layout walk
 * finds first, so new code should say which seam it means and call
 * ESnap_MoveDividerAt().
 *   ES_OK / ES_ERR_STALE (a window on the seam is gone) /
 *   ES_ERR_UNSUPPORTED (no seam).
 */
LONG ESnap_MoveDivider(LONG position);

/*
 * Drag a NAMED divider to a screen coordinate: every window on it is
 * resized, and the value is clamped so none can be squeezed away.
 * Window references are re-validated first, as everywhere else.
 *
 * The seam is named by the line it sits on - `vertical` and the
 * coordinate a query reported as `position` - because a layout can
 * hold more than one and "the divider" would be ambiguous. A seam that
 * has stopped existing is not moved. Appended in 2.3.
 *   ES_OK / ES_ERR_STALE (a window on the seam is gone) /
 *   ES_ERR_UNSUPPORTED (no seam on that line).
 */
LONG ESnap_MoveDividerAt(LONG vertical, LONG line, LONG position);

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
 *
 * The exclusion is held BY ADDRESS. It is dropped once the library
 * notices the window has gone, but a window closed and another opened
 * at the same address before that moment would inherit it - so a
 * program that excludes its windows should re-include them before
 * closing them. Below 1.0 this is the honest description of what the
 * library can do from outside Intuition; a form that cannot be
 * inherited is on the list for the ABI freeze.
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
