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
 *   - waits are followed by another validation before the window is
 *     read or used again; Intuition calls use the validated pointer
 *     just after the lock is released;
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
 * code running in input.device context. Snaps and restores can wait for
 * a glide and, on AROS, for repainting; FeedInput can also wait for a
 * drop (see below). Internal locking uses LockIBase and the registry
 * semaphore. Geometry changes are requested asynchronously
 * (ChangeWindowBox semantics): a successful return means "accepted",
 * not "already applied".
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

/*
 * What fits in a TagItem's ti_Data. On AmigaOS 4 and MorphOS pointers
 * are 32 bits and the field is a ULONG; on AROS x86_64 pointers are 64
 * bits and the field is an IPTR. A pointer cast to ULONG on the latter
 * loses its top half silently, which is how the first 64-bit port of
 * anything usually fails. Cast tag data to this, never to ULONG.
 */
#if defined(__AROS__)
typedef IPTR ESTagData;
#else
typedef ULONG ESTagData;
#endif

#include "edgesnap_types.h"

/* ------------------------------------------------------------- tags */

/* ESnap_SetOptionsA(). Unknown tags are ignored (forward compat). */
#define ES_TAGBASE          (0x8E5A0000UL)
#define ES_OPT_EdgePx       (ES_TAGBASE + 1)  /* LONG, default 12     */
#define ES_OPT_CornerDiv    (ES_TAGBASE + 2)  /* LONG, default 0: the  */
                                               /* corner only where the */
                                               /* pointer is pushed into */
                                               /* it; 2..16 a band of    */
                                               /* height/n (2.19; before */
                                               /* 2..16, default 4)      */
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
#define ES_OPT_CycleSizes   (ES_TAGBASE + 13) /* BOOL, default TRUE: the
                                                 same side hotkey again
                                                 gives two thirds, then
                                                 one third (2.13)       */
#define ES_OPT_Animate      (ES_TAGBASE + 14) /* BOOL, default TRUE: a
                                                 snap and a restore move
                                                 the window in a few
                                                 steps instead of one.
                                                 Placement by a client
                                                 is never animated
                                                 (2.13)                 */
#define ES_OPT_DragRestore  (ES_TAGBASE + 15) /* BOOL, default TRUE: a window
                    snapped to a zone, dragged away and let go outside
                    every zone, gets back its size from before the snap,
                    under the pointer (2.24). A drag is recognised from
                    the bar press or from the window following the pointer
                    (2.25). The pointer's distance from its start must
                    still meet the threshold set by ES_OPT_DragMinPx in
                    either axis; the window's box need not move */
#define ES_OPT_PushPx       (ES_TAGBASE + 16) /* LONG 0..200, default 24: a
                    window held flush with an edge (it may not leave the
                    screen, or meets AmigaOS 4's edge resistance) keeps
                    the pointer back; this much of the mouse's own travel
                    pushing on against that edge counts as the pointer
                    being on it. 0 turns the push off: only the pointer
                    reaching the edge makes a zone (2.26) */

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
 *
 * From 2.24 a release outside every zone can give a window EdgeSnap
 * snapped its size back (ES_OPT_DragRestore). The report does not say
 * so, since it does not grow: ESnap_QueryWindow answers ES_ZONE_NONE for
 * that window afterwards. Such a release can wait up to ten ticks for
 * Intuition to put the window where the drag left it, then up to
 * nineteen ticks for the glide when ES_OPT_Animate is on. At 50 Hz
 * these are one fifth of a second and 0.38 seconds respectively.
 * On AROS a move and resize can add two ticks, and a Wanderer window
 * can also take up to four box or repaint waits, each capped at 150
 * ticks (three seconds at 50 Hz). These bounds count the explicit
 * waits; the system calls and scheduling take their own time.
 *
 * From 2.25 the decision compares the pointer's distance from its start
 * with the threshold set by ES_OPT_DragMinPx, in either axis. Size
 * rounding slack is not a drag threshold. If the live box is still
 * within that threshold of its start, the restore uses the pointer
 * sampled with it, so a window kept on screen restores under the
 * pointer too.
 * Otherwise the live box and the grab's offset locate the drop, even
 * when a last motion was batched with the release or the pointer went
 * on after it. A window that followed the pointer also qualifies when
 * the sampled press missed its bar; its grab comes from the latest
 * motion's box and pointer, sampled together. The original bar-press
 * grab still serves outline drags. Only the release's own qualifiers
 * decide whether the bypass key skips the restore. A snap still uses
 * the zone the preview showed.
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
 * should be. From 2.24 the strip of a seam between windows of a group
 * whose gap is wider than that is the gap itself, so a handle put on
 * it fills the space between the two windows. Fills *divider (with
 * present = 0 when there is no pair).
 *   ES_OK / ES_ERR_BAD_ARGS.
 */
LONG ESnap_QueryDivider(ULONG thickness, struct ESnapDivider *divider);

/*
 * The divider nearest a point, which is how a frontend asks "what is
 * the pointer on?". A layout can hold several: one window facing two
 * stacked ones has a vertical seam moving all three and a horizontal
 * seam moving only the two, and four quadrants have one of each. Pass
 * the pointer position in screen coordinates; present is 0 when
 * nothing is within reach. From 2.16 a point ON a seam, within a few
 * pixels of its strip, also answers whether the seam can be grabbed
 * there: when another window covers it at that point, present is 0.
 * Asked from further away, the nearest seam is named as before.
 * From 2.25 a point with x or y outside -32768..32767 reports no seam.
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
 * From 2.25 position is clamped to -32768..32767 first, then to the
 * seam's limits.
 *   ES_OK / ES_ERR_STALE (a window on the seam is gone) /
 *   ES_ERR_UNSUPPORTED (no seam).
 */
LONG ESnap_MoveDivider(LONG position);

/*
 * Drag a NAMED divider to a screen coordinate: every window on it is
 * resized, and the value is clamped so none can be squeezed away.
 * From 2.25 position is clamped to -32768..32767 before the seam's
 * own limits are applied.
 * Window references are re-validated first, as everywhere else.
 *
 * The seam is named by the line it sits on - `vertical` and the
 * coordinate a query reported as `position` - because a layout can
 * hold more than one and "the divider" would be ambiguous. A seam that
 * has stopped existing is not moved. Appended in 2.4.
 *   ES_OK / ES_ERR_STALE (a window on the seam is gone) /
 *   ES_ERR_UNSUPPORTED (no seam on that line).
 */
LONG ESnap_MoveDividerAt(LONG vertical, LONG line, LONG position);

/*
 * From 2.24 the four calls above look at the front screen only, and
 * take in the windows of the layout groups on it (2.12), those placed
 * without a registry slot included. A seam between two windows of a
 * group keeps the group's gap when it is moved, and a point in that
 * gap is not covered by a window that lies behind the windows beside
 * it. A seam of a group locked for the caller is never reported and
 * never moved. QueryDivider and MoveDivider still take a seam between
 * windows of no group first, when there is one. A group's seam can
 * report a position outside minPosition..maxPosition (see 2.24): a
 * move clamps, so a frontend should move nothing on a click without a
 * drag.
 *
 * MoveDividerAt follows the seam QueryDividerAt last reported to the
 * calling task (an answer of no seam leaves it as it was), or the one
 * the task last moved itself: a line within twelve pixels of where
 * that seam was, or the line the task last asked it to move to, moves
 * that seam or none (ES_ERR_STALE when one of its windows is gone,
 * ES_ERR_UNSUPPORTED when they are no longer that seam on the front
 * screen), never another seam that has come to lie there. For windows
 * of a group, that seam is the one of exactly those windows; for
 * windows of no group, it is the seam on that line whose sides hold
 * them all, windows that have joined it since included. Any other line
 * takes the nearest seam on that axis, as before. A frontend that asks
 * about other points while it shows a seam moves the seam of the last
 * answer it got, so that is the one it should show.
 *
 * From 2.25 an ES_ERR_STALE answer keeps the record, marked gone:
 * every later call that follows it answers ES_ERR_STALE and moves
 * nothing. A QueryDividerAt answer of a seam replaces it; a move on a
 * line it does not follow drops it and takes the nearest seam as
 * before. The records of eight tasks are kept at a time. A task that
 * is gone gives way first; otherwise the record made longest ago is
 * evicted, and that task's next move takes the nearest seam until it
 * records one again.
 */

/* ------------------------------------------------------- functions */

/*
 * What this environment can do right now (screen, compositing, build).
 * Returns a mask of ES_CAP_*. Never fails. Capabilities can change when
 * the environment does (screen switch): query, do not cache forever.
 */
ULONG ESnap_QueryCapabilities(void);

/*
 * Snap win to zone: ES_ZONE_LEFT.._MAX, or ES_ZONE_CENTRE from library
 * 2.9, which is the window's own size in the middle of the usable area,
 * cut down to that area when it is larger than it.
 * Records pre-snap geometry for ESnap_UnsnapWindow. Honors the window's
 * own size limits (the result stays anchored to the zone's outer edge
 * when clamped; a centred window stays centred).
 *   ES_OK / ES_ERR_BAD_ARGS (bad zone) / ES_ERR_STALE /
 *   ES_ERR_REJECTED (unsnappable or excluded window) /
 *   ES_ERR_UNSUPPORTED / ES_ERR_NO_MEMORY (registry full; the snap is
 *   NOT performed - restore must never silently become impossible).
 */
LONG ESnap_SnapWindow(struct Window *win, ULONG zone);

/*
 * Restore win to its pre-snap geometry and drop its snap state. From
 * 2.24 a snapped window the user drags away gets that size back, under
 * the pointer, and loses its snap state the same way.
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
 * All tags are applied together; a bad value changes nothing.
 *   ES_OK / ES_ERR_BAD_ARGS.
 */
LONG ESnap_SetOptionsA(const struct TagItem *tags);

/*
 * Master switch for the interactive engine (drag detection, hotkeys).
 * Programmatic calls above keep working while disabled.
 *   ES_OK.
 */
LONG ESnap_Enable(BOOL on);

/* ------------------------------------------ 2.5: windows and layouts */

/*
 * The windows as the library sees them, for a client that lays them
 * out rather than snapping one at a time (a tiler). Each entry is one
 * window of the screen; the window pointer is an identity key with the
 * validate-per-call semantics of every other call here.
 *
 * The struct is part of the ABI from 2.5 on: fields are appended, never
 * moved. serial is reserved for the window identity of a later
 * revision and reads 0 until then; reserved[] reads 0.
 */
struct ESnapWindowInfo {
    struct Window *window;
    struct ESnapRect rect;     /* outer geometry, screen coordinates   */
    ULONG flags;               /* ESWI_*                               */
    ULONG zone;                /* what ESnap_QueryWindow would report  */
    LONG minWidth, minHeight;  /* size limits; 0 = none                */
    LONG maxWidth, maxHeight;
    ULONG serial;              /* 2.6: the window's identity, see below */
    ULONG reserved[3];         /* reserved: 0                          */
};

#define ESWI_SNAPPABLE   0x0001UL /* size gadget, not backdrop/borderless */
#define ESWI_SNAPPED     0x0002UL /* known to the registry (snapped/placed)*/
#define ESWI_EXCLUDED    0x0004UL /* ESnap_ExcludeWindow                   */
#define ESWI_FIXED_SIZE  0x0008UL /* no size gadget                        */
#define ESWI_BORDERLESS  0x0010UL
#define ESWI_BACKDROP    0x0020UL
#define ESWI_PANEL       0x0040UL /* taken for a dock/panel                */
#define ESWI_ACTIVE      0x0080UL

/*
 * ESnap_QueryWindows: the windows of screen (NULL: the frontmost), in
 * Intuition's front-to-back order. Up to count entries are written to
 * buf; *needed receives how many there are in all, so a short buffer
 * shows as *needed > count and the caller can come back with a bigger
 * one. ES_OK unless needed is NULL, or buf is NULL with a count.
 *
 * ESnap_QueryGeneration: a counter that moves whenever the window set
 * or any window's geometry changes. The library does not see windows
 * open or close, it validates per call, so this call rescans the
 * window lists each time: cheap for a desktop's worth of windows, and
 * the caller's poll is what pays for it. The screen narrows nothing
 * yet: the generation counts changes on every screen. A client polls
 * it and re-reads the list only when the number moved.
 */
LONG ESnap_QueryWindows(struct Screen *screen, struct ESnapWindowInfo *buf,
                        ULONG count, ULONG *needed);
ULONG ESnap_QueryGeneration(struct Screen *screen);

/*
 * ESnap_PlaceWindow: put win into rect with the contract of SnapWindow:
 * the geometry before placement is recorded for UnsnapWindow, the
 * window's size limits are honoured with the result anchored to the
 * edge of rect that touches the usable area, and the errors are the
 * same (ES_ERR_STALE, ES_ERR_REJECTED, ES_ERR_NO_MEMORY). QueryWindow
 * reports ES_ZONE_RECT for a window placed this way. Geometry changes
 * are requested, not applied (ChangeWindowBox semantics).
 * From 2.25 rect's x and y must lie in -32768..32767, and its width
 * and height in 1..32767, as Intuition can represent them; otherwise
 * ES_ERR_BAD_ARGS, before fitting the window's limits or a cell.
 *
 *   ES_PF_NO_RESTORE   spend no registry slot on a window that was
 *                      never adopted. A window already in the registry
 *                      is followed all the same, so UnsnapWindow still
 *                      gives back the geometry from before the first
 *                      placement. For a client that moves windows many
 *                      times a minute.
 *   ES_PF_KEEP_ZORDER  do not reorder the windows. On AROS a window is
 *                      brought to the front before it is moved, so
 *                      that the move copies everything and leaves no
 *                      damage a file manager will not repaint; with
 *                      this flag it stays where it is in the stack,
 *                      damage and all.
 *   ES_PF_CELL         (2.20) rect is a cell of the layout of the
 *                      window's group: the group's margins and gap are
 *                      taken off it (see ESnap_SetGroupOptionsA). A
 *                      window in no group is placed in the cell as it
 *                      is. An older library answers ES_ERR_BAD_ARGS.
 *
 * ESnap_PlaceWindowsA: a whole layout in one call. Every entry gets its
 * own result, so one stale window does not fail the rest. The library
 * orders the moves so that windows which shrink go first and windows
 * which grow go last; it cannot make the batch atomic, each move is a
 * request of its own. ES_OK once every entry has been tried;
 * ES_ERR_BAD_ARGS for a NULL list, an empty one, or more than
 * ES_PLACE_MAX entries.
 * From 2.25 the same rectangle bounds apply per entry: a bad rectangle
 * receives ES_ERR_BAD_ARGS in that entry's result and moves nothing,
 * while the other entries are tried as usual.
 */
#define ES_PF_NO_RESTORE   0x0001UL
#define ES_PF_KEEP_ZORDER  0x0002UL
#define ES_PF_CELL         0x0004UL

LONG ESnap_PlaceWindow(struct Window *win, const struct ESnapRect *rect,
                       ULONG flags);

struct ESnapPlacement {
    struct Window *window;
    struct ESnapRect rect;
    LONG result;               /* per entry: ES_OK or an error         */
};

#define ES_PLACE_MAX 64

LONG ESnap_PlaceWindowsA(struct ESnapPlacement *list, ULONG count,
                         ULONG flags);

/* ------------------------------------------ 2.6: window identity */

/*
 * A struct Window * is an address, and an address is reused: a client
 * that keeps state for a whole session, not just for a drag, meets
 * that as the normal case. So every window the library observes gets
 * a serial, never 0 and never reused, and the one promise is: same
 * serial, same window.
 *
 * Observed means: seen by QueryWindows or QueryGeneration, or asked
 * about with QueryWindowSerial. A window closed and another opened at
 * the same address is told apart by its layer; a window never observed
 * has no serial yet, and gets one when it is. What the library cannot
 * see is a window that came and went entirely between two
 * observations, and a client that polls QueryGeneration sees often.
 *
 * ESnap_QueryWindowSerial: the serial of win, assigned now if it had
 * none. ES_ERR_STALE if there is no such window.
 *
 * ESnap_FindWindow: the window behind a serial, validated against the
 * live window lists; ES_ERR_STALE once it is gone, and the serial is
 * retired with it. The result is a struct Window * with the same
 * validate-per-call rule as every other call here: it is good for
 * the next call, not for keeping.
 */
LONG ESnap_QueryWindowSerial(struct Window *win, ULONG *serial);
LONG ESnap_FindWindow(ULONG serial, struct Window **window);

/*
 * --- 2.7: the pointer's own travel ---------------------------------
 *
 * Where Intuition keeps a window inside the screen (MorphOS as
 * delivered, AROS with "offscreen move" switched off), a window
 * dragged against the edge stops there, and the pointer stays pinned
 * where it grabbed the title bar: it never touches the edge, so no
 * zone would ever be entered. The mouse keeps moving all the same, and
 * the frontend's input handler sees that travel. ESnap_FeedMotion
 * reports it, in pixels and signed, accumulated since the last
 * ESnap_FeedInput: the engine then treats a pointer that stands still
 * while the travel keeps pushing against an edge the window is flush
 * with as being on that edge. A frontend that never calls it loses
 * nothing but this case.
 */
void ESnap_FeedMotion(LONG dx, LONG dy);

/*
 * --- 2.10: client roles ---------------------------------------------
 *
 * A tiler and the commodity run at the same time, and neither may
 * switch the other off. A client says what it is, once, from the task
 * that will make its calls:
 *
 *   ES_CL_ENGINE  "I own the interactive drag engine". One holder at a
 *                 time; a second asker gets ES_ERR_IN_USE. While a
 *                 holder exists, FeedInput, FeedMotion and ResetInput
 *                 are taken from its task only and ignored from any
 *                 other, and its ESnap_Enable is the engine's switch.
 *   ES_CL_LAYOUT  "I own the layout of the groups I create". Any
 *                 number of holders. Its ESnap_Enable switches only its
 *                 own layout on and off, which is what the groups of a
 *                 later revision will honour; the engine is not touched.
 *
 * SnapWindow, UnsnapWindow, PlaceWindow, PlaceWindowsA and every query
 * stay open to everyone, registered or not. A caller that never
 * registers keeps the behaviour it always had: its ESnap_Enable moves
 * the engine's switch, as in 2.2, so a client written before roles
 * existed does not change meaning. A registration belongs to the task
 * that made it and is dropped once that task is gone, the same rule
 * that lets windows go stale: the id is never reused.
 *
 * ESnap_RegisterClient: name is for logs and may be NULL; wants is one
 * or both roles; *client_out receives the id. Registering again from
 * the same task changes its roles and returns the same id.
 *   ES_OK / ES_ERR_BAD_ARGS / ES_ERR_IN_USE / ES_ERR_NO_MEMORY.
 * ESnap_UnregisterClient: ES_OK / ES_ERR_STALE for an id nobody holds.
 */
#define ES_CL_ENGINE   0x0001UL
#define ES_CL_LAYOUT   0x0002UL

LONG ESnap_RegisterClient(const char *name, ULONG wants, ULONG *client_out);
LONG ESnap_UnregisterClient(ULONG client);

/*
 * --- 2.11: work areas -----------------------------------------------
 *
 * A tiler lays windows out per work area, and it needs an identity
 * for one that survives what a struct Screen pointer does not: the
 * Workbench screen is closed and reopened by a screen-mode change,
 * and comes back at another address. So an area has an id: a public
 * screen is known by its name and keeps its id for as long as the
 * library is loaded, a private screen is known by its address for as
 * long as it stays open. Same id, same area, as far as the library can
 * tell. Ids are never reused.
 *
 * One area per screen for now: monitor reads 0. A screen spanning
 * several displays (MorphOS 3.20) is the case this call exists for,
 * and it will report one area per display, each with its monitor,
 * once the library can tell them apart; a client written against the
 * shape below needs no change for that day, only a bigger buffer.
 *
 * ESnap_QueryWorkAreas: the areas of screen (NULL: the frontmost).
 * Up to count entries are written to buf; *needed receives how many
 * there are in all, so a short buffer shows as *needed > count.
 *   ES_OK / ES_ERR_BAD_ARGS (needed NULL, or buf NULL with a count) /
 *   ES_ERR_STALE (no such screen) / ES_ERR_NO_MEMORY (no room left for
 *   another screen's id; sixteen are kept).
 */
struct ESnapWorkArea {
    ULONG id;                  /* stable for the screen, never 0      */
    ULONG monitor;             /* 0 until displays can be told apart  */
    struct ESnapRect bounds;   /* the whole screen, in its coordinates */
    struct ESnapArea area;     /* usable and insets, as QueryScreenArea */
};

LONG ESnap_QueryWorkAreas(struct Screen *screen, struct ESnapWorkArea *buf,
                          ULONG count, ULONG *needed);

/*
 * --- 2.12: layout groups --------------------------------------------
 *
 * A tiler's unit of state: these windows, in this work area, owned by
 * this client. The library keeps the membership, by window serial, so
 * that the interactive engine can tell a window a tiler owns from one
 * it may snap.
 *
 * ESnap_CreateGroup: the caller must hold ES_CL_LAYOUT, that is what
 * the role is for. workArea is an id from QueryWorkAreas or 0 for
 * "wherever"; name is for logs and may be NULL; flags:
 *   ES_GF_LOCKED  keep everyone else off these windows. The owner's
 *                 own SnapWindow, PlaceWindow and PlaceWindowsA work as
 *                 usual; from any other task, the commodity included,
 *                 they answer ES_ERR_REJECTED, and the interactive
 *                 engine does not snap a locked window when the user
 *                 drags it. It does count the touch: QueryGeneration
 *                 moves, so the owner learns that the user reached for
 *                 its window and can decide what that means. A locked
 *                 group whose owner switched its layout off
 *                 (ESnap_Enable(FALSE) from a layout client) keeps
 *                 nobody out until it switches back on.
 *   ES_OK / ES_ERR_BAD_ARGS / ES_ERR_REJECTED (no layout role) /
 *   ES_ERR_STALE (no such work area) / ES_ERR_NO_MEMORY (sixteen groups).
 *
 * ESnap_DeleteGroup, ESnap_GroupAddWindow, ESnap_GroupRemoveWindow:
 * the owner's task only (ES_ERR_REJECTED otherwise), ES_ERR_STALE for
 * a group that does not exist or a window that does not. A window
 * belongs to one group at a time: adding it to a second one is
 * ES_ERR_REJECTED, removing it first is the way. Thirty-two windows
 * per group. A window that closes leaves its group by itself.
 *
 * ESnap_QueryGroupOf: *group_out receives the group of win, or 0.
 *   ES_OK / ES_ERR_BAD_ARGS / ES_ERR_STALE.
 *
 * A group dies with the client that made it, when that client
 * unregisters or its task is gone. Group ids are never reused.
 */
#define ES_GF_LOCKED   0x0001UL

LONG ESnap_CreateGroup(ULONG workArea, const char *name, ULONG flags,
                       ULONG *group_out);
LONG ESnap_DeleteGroup(ULONG group);
LONG ESnap_GroupAddWindow(ULONG group, struct Window *win);
LONG ESnap_GroupRemoveWindow(ULONG group, struct Window *win);
LONG ESnap_QueryGroupOf(struct Window *win, ULONG *group_out);

/*
 * --- 2.20: gaps and margins per group --------------------------------
 *
 * The global margins (ES_OPT_Margin*) are the user's; a tiler wants
 * room between the cells of its layout and around it, its own for each
 * group. ESnap_SetGroupOptionsA keeps them with the group, and
 * PlaceWindow and PlaceWindowsA take them off a cell when asked with
 * ES_PF_CELL: a side at or beyond the edge of the usable area gives up
 * the margin of that side, a side shared with another cell half the
 * gap, so that two neighbours leave exactly the gap between them (an
 * odd pixel goes to the right or bottom cell's side). From 2.24 a seam
 * dragged between two of the group's windows keeps the gap they have.
 * This header said so from 2.20, which was a mistake: up to 2.23 a
 * drag closed a gap of up to twelve pixels, and found no seam across
 * a wider one.
 *
 *   ES_GO_Gap           LONG, room between two cells, 0..ES_GO_GAP_MAX
 *   ES_GO_Margin        LONG, all four margins at once, applied before
 *                       the per-side tags of the same call
 *   ES_GO_MarginLeft, ES_GO_MarginTop, ES_GO_MarginRight,
 *   ES_GO_MarginBottom  LONG, 0..ES_GO_MARGIN_MAX
 *
 * All 0 when the group is made. A change moves no window: it applies
 * from the next placement with ES_PF_CELL (see 2.24 for what that
 * means for the seams). Unknown tags are ignored; a value out of range
 * changes nothing and answers ES_ERR_BAD_ARGS. The owner's task only:
 *   ES_OK / ES_ERR_BAD_ARGS / ES_ERR_REJECTED (not the owner) /
 *   ES_ERR_STALE (no such group).
 */
#define ES_GO_Gap          (ES_TAGBASE + 0x101)
#define ES_GO_Margin       (ES_TAGBASE + 0x102)
#define ES_GO_MarginLeft   (ES_TAGBASE + 0x103)
#define ES_GO_MarginTop    (ES_TAGBASE + 0x104)
#define ES_GO_MarginRight  (ES_TAGBASE + 0x105)
#define ES_GO_MarginBottom (ES_TAGBASE + 0x106)
#define ES_GO_GAP_MAX      200
#define ES_GO_MARGIN_MAX   2000

LONG ESnap_SetGroupOptionsA(ULONG group, const struct TagItem *tags);

/*
 * --- 2.24: divider tokens per group ---------------------------------
 *
 * A tiler's own seams. ESnap_QueryDividers lists the seams between the
 * windows of a group, each with a token; ESnap_MoveDividerT moves the
 * seam a token names; ESnap_QueryDividerWindows says which windows
 * those are. A token names the windows on each side of a seam, not a
 * line: it moves them and no other, and stays valid across its own
 * moves for as long as they are that seam. Once they are not (one of
 * them closed or left the group, or a window of the group joined them
 * across the seam) it is retired, and a retired token is never valid
 * again; the next QueryDividers gives the seam they make now a token
 * of its own. Tokens are never 0. Any task may ask QueryDividers and
 * QueryDividerWindows, and either retires a token it finds no longer
 * valid, whoever asks: a stale token is stale for everyone.
 * MoveDividerT alone refuses a caller the group is locked for, and
 * does so before it checks the token's windows.
 *
 * The seams are found from the boxes EdgeSnap last put the windows in
 * (a placement, a snap, UnsnapWindow, a divider move), so a tiler may
 * ask right after a placement it has not seen land, and a window
 * EdgeSnap asked to move in the last two seconds is never counted as
 * moved by hand (ES_DIF_MOVED, ES_ERR_CHANGED), whatever box it shows
 * on its way. A window still in the box it had before EdgeSnap's last
 * request, or in the box EdgeSnap last put it in, is not counted as
 * moved by hand, however long after the request. Once it is seen in
 * the requested box the earlier box stops counting. From 2.25 a window
 * joining before its remembered placement has landed gets the transit
 * interval on joining. A window not placed since it joined counts
 * where EdgeSnap last put it, if UnsnapWindow can undo that (a snap, or a
 * placement or move of a window with a way back), or else where it
 * stood when it joined: an ES_PF_NO_RESTORE placement of a window with
 * no way back leaves nothing to remember, so a tiler that places with
 * it adds its windows to the group first. Members that are excluded or
 * cannot be snapped are on no seam.
 *
 * Windows of the group face each other across a seam when the gap
 * between them is the group's (ESnap_SetGroupOptionsA), and a move
 * keeps it. A change of the gap moves no window, and the boxes
 * EdgeSnap put them in keep the old one: a seam between them may not
 * be found until the tiler places them again. position is the cell
 * boundary ES_PF_CELL starts from: the windows left of (above) the
 * seam end at position less half the gap, rounded up (x plus width,
 * y plus height), and those right of (below) it start at position
 * plus half the gap, rounded down. For an odd gap it lies half a pixel
 * right of (below) the middle of the gap.
 *
 * ESnap_QueryDividers: up to count entries are written to buf,
 * vertical seams first, then horizontal ones, each by position, then
 * by from; *needed receives how many there are in all, so a short
 * buffer shows as *needed > count. from and to are where along the
 * seam its windows face each other, to one past the last pixel (y
 * plus height along a vertical seam, x plus width along a horizontal
 * one). The limits honour every window's own size limits. position
 * can lie outside them, in a layout where a window's own limits kept
 * it from filling its cell: a move clamps, so moving to position may
 * then move the windows.
 *   ES_OK / ES_ERR_BAD_ARGS (needed NULL, or buf NULL with a count) /
 *   ES_ERR_STALE (no such group) / ES_ERR_UNSUPPORTED.
 *
 * ESnap_MoveDividerT: the seam of token to position, clamped to its
 * limits (from 2.25, to -32768..32767 first); *applied (applied may be
 * NULL) receives the position it was moved to, on ES_OK only. Only the
 * token's windows move. flags:
 * ES_PF_KEEP_ZORDER, the windows are not raised (see PlaceWindow).
 * Geometry changes are requested, not applied (ChangeWindowBox
 * semantics). The token stays valid. After the argument and group lock
 * checks, the order from 2.25 is: still that seam, moved by hand, room
 * to move, then the move.
 *   ES_OK / ES_ERR_BAD_ARGS (token 0, or an unknown flag) /
 *   ES_ERR_REJECTED (the group is locked for the caller; the token is
 *   not touched) / ES_ERR_STALE (no such token, or its windows are no
 *   longer that seam: it is retired) / ES_ERR_CHANGED (a window moved
 *   by hand, outside the current and earlier boxes and the transit
 *   interval described above; the token stays) /
 *   ES_ERR_UNSUPPORTED (no room to move it after every window's
 *   limits; the token stays).
 *
 * ESnap_QueryDividerWindows: the serials of the token's windows, those
 * left of (above) the seam, then those right of (below) it, each side
 * top-most (along a vertical seam) or left-most first: serials[0] is
 * serialA, serials[countA] is serialB. Up to count are written,
 * *needed receives how many there are.
 *   ES_OK / ES_ERR_BAD_ARGS (needed NULL, serials NULL with a count, or
 *   token 0) / ES_ERR_STALE (no such token, or its windows are no
 *   longer that seam: it is retired) / ES_ERR_UNSUPPORTED.
 *
 * MoveDivider and MoveDividerAt keep the contracts of 2.2 and 2.3 and
 * do not ask whether a window was moved by hand: on the seam of a
 * group that is not locked for the caller they put such a window back
 * in its place beside the seam, where MoveDividerT answers
 * ES_ERR_CHANGED. QueryDivider and QueryDividerAt report no seam
 * there, so the commodity does not offer it to the user, but any task
 * may call the two: a tiler that wants its windows left alone locks
 * its group.
 *
 * The struct is 64 bytes on every system, and stays so.
 */
struct ESnapDividerInfo {
    ULONG token;               /* names the seam for MoveDividerT; 0:  */
                               /* too many seams to give each a token  */
    ULONG flags;               /* ES_DIF_*                             */
    LONG vertical;             /* 1: dragged left/right, 0: up/down    */
    LONG position;             /* the cell boundary, see above         */
    LONG minPosition;          /* how far it may be dragged, every     */
    LONG maxPosition;          /* window's own limits honoured         */
    LONG from, to;             /* where along it its windows face, to  */
                               /* one past the last pixel              */
    LONG gap;                  /* what it keeps between its sides      */
    ULONG countA;              /* windows left of (above) it           */
    ULONG countB;              /* windows right of (below) it          */
    ULONG serialA;             /* of each side the first window: the   */
    ULONG serialB;             /* top-most, or the left-most           */
    ULONG reserved[3];         /* 0                                    */
};

#define ES_DIF_LOCKED  0x0001UL /* locked for the caller: MoveDividerT   */
                                /* answers ES_ERR_REJECTED               */
#define ES_DIF_STUCK   0x0002UL /* no room after the windows' limits:    */
                                /* minPosition = maxPosition = position  */
#define ES_DIF_MOVED   0x0004UL /* a window is not where EdgeSnap put    */
                                /* it: MoveDividerT answers CHANGED      */

typedef char es_dif_size_check[(sizeof(struct ESnapDividerInfo) == 64)
                               ? 1 : -1];

LONG ESnap_QueryDividers(ULONG group, struct ESnapDividerInfo *buf,
                         ULONG count, ULONG *needed);
LONG ESnap_MoveDividerT(ULONG token, LONG position, ULONG flags,
                        LONG *applied);
LONG ESnap_QueryDividerWindows(ULONG token, ULONG *serials, ULONG count,
                               ULONG *needed);

#endif /* EDGESNAP_H */
