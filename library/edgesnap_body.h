/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_body.h - the implementation behind the public API.
 *
 * This is edgesnap.library's body, kept separate from every platform
 * skeleton: AmigaOS 4 wraps it in an ELF library with interfaces,
 * MorphOS in a classic LVO jump table, and the reference commodity
 * links it directly while the skeletons are being built. Whatever the
 * wrapper, the semantics below are the ones documented in
 * include/edgesnap.h - there is only ever one implementation.
 *
 * Threading: every entry point may be called from any normal task.
 * Shared state is protected by an internal semaphore; nothing here may
 * be called from input.device context (see the CxCustom rule).
 */

#ifndef EDGESNAP_BODY_H
#define EDGESNAP_BODY_H

#include <exec/types.h>
#include <intuition/intuition.h>

#include "edgesnap_types.h"
#include "config.h"
#include "engine.h"
#include "registry.h"
#include "panels.h"

/* Called once by the platform skeleton at library init / first open.
 * Returns 0 on failure (the library must then refuse to open). */
int esb_init(void);
void esb_cleanup(void);

/* The public API, one implementation for every wrapper. */
ULONG esb_query_capabilities(void);
LONG esb_snap_window(struct Window *win, ULONG zone);
LONG esb_unsnap_window(struct Window *win);
LONG esb_query_window(struct Window *win, ULONG *zone_out);
LONG esb_exclude_window(struct Window *win, BOOL exclude);
LONG esb_set_config(const ESConfig *cfg);
LONG esb_enable(BOOL on);

/* Read-only view of the live configuration (the frontend echoes it). */
const ESConfig *esb_config(void);

/*
 * Interactive path. The frontend contributes only raw input facts; the
 * body samples the windows, runs the engine and performs the snap. It
 * reports back what happened so the frontend can draw the preview
 * frame (platform drawing stays outside) and log it - a library never
 * prints.
 */
typedef struct ESBReport {
    int drag_started;
    int zone_changed;
    int zone;
    int preview_show;
    int preview_hide;
    ESRect preview_rect;
    struct Screen *preview_screen;
    int snapped;          /* a snap was attempted: fields below valid */
    int snap_zone;
    LONG snap_rc;
    struct Window *snap_win;
} ESBReport;

/* press/motion/release: how many of each arrived since the last call
 * (counters, so a fast press+release pair is never lost). quals: the
 * qualifier bits seen with the last mouse event. */
void esb_input(int press, int motion, int release, ULONG quals,
               ESBReport *out);

/* Abort any tracking (disable, shutdown): asks for the frame to go. */
void esb_input_reset(ESBReport *out);

int esb_enabled(void);

/* True while a drag is being tracked - the frontend uses it to keep
 * console output out of the drag (the OS4 console freezes then). */
int esb_drag_active(void);

/*
 * Windows the panel scan must never mistake for a dock - a frontend's
 * own preview frame is thin, edge-flush and long, which is exactly
 * what a dock looks like. Pass NULL/0 to clear. At most
 * ESB_IGNORE_SLOTS are kept.
 */
#define ESB_IGNORE_SLOTS 4
void esb_ignore_windows(struct Window **wins, int count);

/* Diagnostics: usable area and insets of a screen. The caller must
 * hold LockIBase (the frontend's window dump already does). */
void esb_debug_usable(struct Screen *scr, ESRect *usable, ESInsets *ins);

/*
 * Snap driven by the engine's decision: the previewed rectangle is
 * honored as-is, and the window reference is re-validated first
 * (it may be stale by now - that is the contract, not an accident).
 */
LONG esb_snap_rect(struct Window *win, ULONG zone, const ESRect *want);

#endif /* EDGESNAP_BODY_H */
