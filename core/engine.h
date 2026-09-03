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
} ESWinFacts;

typedef struct ESEngineConfig {
    int edge_px;      /* pointer this close to an edge = zone           */
    int corner_div;   /* corner length = usable height / corner_div     */
    int drag_min_px;  /* pointer travel needed to call it a drag        */
    unsigned zones_mask; /* ES_ZONEBIT() set of zones that react        */
} ESEngineConfig;

void es_engine_config_defaults(ESEngineConfig *cfg);

/* Actions emitted by one input step. Flags say which fields are valid.
 * On release both hide_preview and do_snap can be set: hide first. */
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
} ESEngineActions;

typedef struct ESEngine {
    ESEngineConfig cfg;
    int button_down;
    int dragging;
    int on_bar;      /* the press landed on the candidate's drag bar   */
    void *candidate;
    ESRect base_box;
    int base_mx, base_my;
    int zone;
    ESRect zone_rect;   /* fitted target of the current zone            */
    ESWinFacts last;    /* facts snapshot from the latest motion        */
} ESEngine;

/* cfg == NULL uses defaults. */
void es_engine_init(ESEngine *e, const ESEngineConfig *cfg);

/* Primary button went down. */
void es_engine_press(ESEngine *e, ESEngineActions *out);

/* Pointer moved while tracking. facts describes the active window at
 * this instant, or NULL when there is none. */
void es_engine_motion(ESEngine *e, const ESWinFacts *facts,
                      ESEngineActions *out);

/* Primary button released. */
void es_engine_release(ESEngine *e, ESEngineActions *out);

/* Abort any tracking (disable, shutdown): emits hide_preview if one
 * could be showing. */
void es_engine_reset(ESEngine *e, ESEngineActions *out);

#endif /* EDGESNAP_ENGINE_H */
