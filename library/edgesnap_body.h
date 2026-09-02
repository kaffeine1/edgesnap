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
#include <utility/tagitem.h>

#include "edgesnap.h"
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

/* Public options path: ES_OPT_* tags mapped onto the configuration.
 * Unknown tags are ignored (forward compatibility); a tag with an
 * out-of-range value fails the call and changes nothing. */
LONG esb_set_options(const struct TagItem *tags);
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
/* The report is the PUBLIC struct: no translation layer between the
 * body and either skeleton, so there is one description of what
 * happened, not two that can drift. */

/* press/motion/release: how many of each arrived since the last call
 * (counters, so a fast press+release pair is never lost). quals: the
 * qualifier bits seen with the last mouse event. */
void esb_input(int press, int motion, int release, ULONG quals,
               struct ESnapReport *out);

/* Abort any tracking (disable, shutdown): asks for the frame to go. */
void esb_input_reset(struct ESnapReport *out);

/* Usable area of a screen; takes LockIBase itself. */
LONG esb_query_screen_area(struct Screen *scr, struct ESnapArea *out);

/* The divider between two snapped windows. */
LONG esb_query_divider(ULONG thickness, struct ESnapDivider *out);
LONG esb_query_divider_at(ULONG thickness, LONG x, LONG y,
                          struct ESnapDivider *out);
LONG esb_move_divider(LONG position);              /* 2.2: first seam */
LONG esb_move_divider_at(LONG vertical, LONG line, LONG position);

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
#define ESB_IGNORE_SLOTS 6
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
