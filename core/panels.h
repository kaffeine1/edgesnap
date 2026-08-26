/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * panels.h - dock/panel detection policy: which windows reserve screen
 * edges (AmiDock on OS4, Ambient panels on MorphOS), macOS-style.
 *
 * Pure C89 geometry, host-tested. The platform glue does the walking
 * and the filtering it alone can do (borderless, no drag bar, no size
 * gadget, no backdrop, not one of our own preview windows) and passes
 * the surviving window boxes here; this module decides which of them
 * are edge panels and how much of each screen edge they reserve.
 *
 * Heuristic (documented, deliberately conservative):
 *   - a panel hugs one screen edge (within ES_PANEL_FLUSH_TOL px);
 *   - it is thin: thickness <= screen dimension / ES_PANEL_MAX_THICK_DIV;
 *   - it is long enough to be a bar, not a corner widget:
 *     length >= ES_PANEL_MIN_LEN_PCT % of its edge.
 * Multiple panels on one edge reserve the deepest inset. Whether real
 * AmiDock/Ambient panel windows match these filters is an explicit
 * validation task recorded in docs/DESIGN.md.
 */

#ifndef EDGESNAP_PANELS_H
#define EDGESNAP_PANELS_H

#include "zones.h"

#define ES_PANEL_FLUSH_TOL      2
#define ES_PANEL_MAX_THICK_DIV  4
#define ES_PANEL_MIN_LEN_PCT   25

typedef struct ESInsets {
    int l, t, r, b;
} ESInsets;

/*
 * screen: full screen rectangle. boxes/count: candidate window boxes,
 * pre-filtered by the caller as described above. out: reserved inset
 * per edge (0 when free).
 */
void es_panel_insets(const ESRect *screen, const ESRect *boxes, int count,
                     ESInsets *out);

#endif /* EDGESNAP_PANELS_H */
