/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * engine.h - the portable EdgeSnap drag/snap state machine.
 *
 * Pure C89, no Amiga includes: this is the one engine every frontend
 * (commodity, tests, future clients) must share. The platform glue:
 *
 *   - feeds it input facts (button transitions, pointer motion together
 *     with an instantaneous snapshot of the active window);
 *   - executes the actions it emits (show/hide a zone preview, snap a
 *     window) in a context where Intuition calls are safe.
 *
 * The engine never dereferences the window: ESWinFacts.ref is an opaque
 * identity token chosen by the glue (on Amiga, the struct Window
 * pointer). Any ref the engine hands back in an action may already be
 * stale - the executor MUST re-validate it against the live window list
 * before acting. That rule is the contract, not an implementation detail.
 */

#ifndef EDGESNAP_ENGINE_H
#define EDGESNAP_ENGINE_H

#include "zones.h"

/* Window facts, sampled atomically by the glue (under LockIBase on
 * Amiga) at one instant. All geometry is in screen coordinates. */
#define ES_WF_SNAPPABLE 0x0001u /* policy allows snapping this window   */
#define ES_WF_DRAGBAR   0x0002u /* window has a system drag bar         */
#define ES_WF_BYPASS    0x0004u /* user holds the bypass qualifier      */

typedef struct ESWinFacts {
    void *ref;             /* identity token, never dereferenced        */
    ESRect box;            /* current outer window box                  */
    ESRect usable;         /* usable area of the window's screen        */
    int mouse_x, mouse_y;  /* pointer position on that screen           */
    int min_w, min_h;      /* window size limits; max 0 = unlimited     */
    int max_w, max_h;
    int bar_h;             /* height of the drag bar (top border), px   */
    unsigned flags;        /* ES_WF_*                                   */
    int push_x, push_y;    /* raw pointer travel since the last motion, */
                           /* signed, in pixels; 0 when unknown         */
} ESWinFacts;

typedef struct ESEngineConfig {
    int edge_px;      /* pointer this close to an edge = zone           */
    int corner_div;   /* corner length = usable height / corner_div;    */
                      /* 0 or 1: only the corner itself                 */
    int drag_min_px;  /* pointer travel needed to call it a drag        */
    unsigned zones_mask; /* ES_ZONEBIT() set of zones that react        */
    int push_px;      /* raw travel against a pinned edge = at the edge */
} ESEngineConfig;

void es_engine_config_defaults(ESEngineConfig *cfg);

/*
 * How far a corner reaches along an edge of the usable area, in pixels.
 * corner_div 0 and 1 give 0: a corner is then only where the pointer
 * stands at or beyond the usable area's top or bottom, in the screen's
 * title bar or below a dock, where the screen's own edge stops it.
 */
int es_engine_corner_px(const ESEngineConfig *cfg, int usable_h);

/*
 * How far below the usable area's top the pointer still counts as on
 * the top edge: the edge distance, or 0 with corners of no length, so
 * that a window whose title bar sits at the top of the usable area is
 * not in the maximize zone the moment it is dragged. The top edge is
 * then the screen's title bar and above.
 */
int es_engine_top_px(const ESEngineConfig *cfg);

/* Actions emitted by one input step. Flags say which fields are valid.
 * On release both hide_preview and do_snap can be set: hide first.
 * do_drop and do_snap are never set together. */
typedef struct ESEngineActions {
    int show_preview;   /* preview_rect (and preview_ref) are valid     */
    int hide_preview;
    int do_snap;        /* snap_ref/snap_zone/snap_rect are valid       */
    int drag_started;   /* informational (logging)                      */
    int zone_changed;   /* informational; zone holds the new zone       */
    int zone;
    ESRect preview_rect;
    void *preview_ref;
    void *snap_ref;     /* MAY BE STALE - executor must re-validate     */
    int snap_zone;
    ESRect snap_rect;
    int do_drop;        /* 2.24: a window drag was let go               */
                        /* outside every zone; drop_* are valid         */
    void *drop_ref;     /* MAY BE STALE: the executor re-validates it   */
    ESRect drop_start;  /* start box for the wait, size and placement   */
    int drop_start_x, drop_start_y; /* pointer at the gesture's start   */
    int drop_min_px;    /* engine's pointer travel threshold            */
    ESRect drop_from;   /* box sampled with the grab, not always start  */
    int drop_grab_x, drop_grab_y; /* pointer sampled with drop_from      */
    int drop_x, drop_y; /* the pointer at the last motion               */
} ESEngineActions;

typedef struct ESEngine {
    ESEngineConfig cfg;
    int button_down;
    int dragging;
    int on_bar;      /* the press landed on the candidate's drag bar   */
    int followed;    /* its position changed during a recognised drag  */
    int first_x, first_y; /* position at the candidate's first motion   */
    void *candidate;
    ESRect base_box;
    int base_mx, base_my;
    int press_mx, press_my; /* where the button went down; -1 = unknown */
    int zone;
    ESRect zone_rect;   /* fitted target of the current zone            */
    ESWinFacts last;    /* facts snapshot from the latest motion        */
    int push_acc_x, push_acc_y; /* raw travel while the pointer stood,  */
                                /* in half pixels (es_push_axis)        */
    int push_wall_x, push_wall_y; /* -1 low wall, 1 high wall, 0 none   */
    void *press_ref;    /* 2.24: the active window at the press, 0 if   */
    ESRect press_box;   /* unknown, and its box then                    */
} ESEngine;

/* cfg == NULL uses defaults. */
void es_engine_init(ESEngine *e, const ESEngineConfig *cfg);

/* Primary button went down. */
/* mx, my: the pointer where the button went down, -1 when unknown. */
void es_engine_press(ESEngine *e, int mx, int my, ESEngineActions *out);

/*
 * The press, knowing which window was active then and its box, sampled
 * together with mx, my (2.24). When the first motion names that window,
 * the drag is judged from this box rather than from the box of the
 * first motion: where windows move with the pointer, a fast pull can
 * take the window well away before the first motion is sampled, and a
 * press on the title bar of a window pulled down would then look like a
 * press below it. ref or box NULL is es_engine_press.
 */
void es_engine_press_on(ESEngine *e, void *ref, const ESRect *box,
                        int mx, int my, ESEngineActions *out);

/* Pointer moved while tracking. facts describes the active window at
 * this instant, or NULL when there is none. */
void es_engine_motion(ESEngine *e, const ESWinFacts *facts,
                      ESEngineActions *out);

/* Primary button released. A drag let go outside every zone, the window
 * snappable, emits do_drop if the press was on its bar (2.24) or its box
 * followed the pointer while held (2.25). The bar test remains needed
 * for outline drags. A left/top resize can move the box too: the
 * registry's size check refuses it.
 * The executor checks the release's own bypass qualifier. The engine
 * only reports the gesture: whether the window gets its old size back
 * is the registry's to say (es_registry_drag_out), since the engine
 * never sees it. */
void es_engine_release(ESEngine *e, ESEngineActions *out);

/* At the decision, the pointer must still be at least drop_min_px from
 * its start in either axis. A drag brought back within that threshold
 * does not restore. The start belongs to the gesture, not to the grab
 * sample, which can be a later motion when the press missed the bar. */
int es_engine_drop_moved(const ESEngineActions *a, int mx, int my);

/* The pointer after a drop: the live box plus the grab's offset in
 * drop_from, unless the box is still within the drag threshold of its
 * start while the pointer is not. That window could not follow the
 * drag, so use mx, my, sampled together with live. Otherwise the live
 * box keeps a later pointer fling from carrying the restore away.
 * A last motion batched with the release was not processed, so drop_x
 * and drop_y need not be where the drag ended. Coordinates and sizes
 * are sampled from Intuition's signed 16-bit window boxes. */
void es_engine_drop_point(const ESEngineActions *a, const ESRect *live,
                           int mx, int my,
                           int *px, int *py);

/* Abort any tracking (disable, shutdown): emits hide_preview if one
 * could be showing. */
void es_engine_reset(ESEngine *e, ESEngineActions *out);

#endif /* EDGESNAP_ENGINE_H */
