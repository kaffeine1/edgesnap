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
 *   - a panel hugs one screen edge within ES_PANEL_FLUSH_TOL px - the
 *     tolerance is generous because real docks FLOAT a few pixels off
 *     the edge, macOS-style (MorphOS field finding, 2026-08-26);
 *   - it is thin: thickness <= screen dimension / ES_PANEL_MAX_THICK_DIV;
 *   - it is long enough to be a bar, not a corner widget:
 *     length >= ES_PANEL_MIN_LEN_PCT % of its edge (compact centered
 *     docks are short: threshold tuned down from 25).
 * A floating panel reserves up to the screen edge (gap included), like
 * the macOS Dock. Multiple panels on one edge reserve the deepest
 * inset. Matching real AmiDock/Ambient panel windows is an explicit
 * validation task recorded in docs/DESIGN.md.
 */

#ifndef EDGESNAP_PANELS_H
#define EDGESNAP_PANELS_H

#include "zones.h"

#define ES_PANEL_FLUSH_TOL     16
#define ES_PANEL_MAX_THICK_DIV  4
#define ES_PANEL_MIN_LEN_PCT   15

/* es_panel_classify() results. */
enum {
    ES_PEDGE_NONE = 0,
    ES_PEDGE_LEFT,
    ES_PEDGE_RIGHT,
    ES_PEDGE_TOP,
    ES_PEDGE_BOTTOM
};

typedef struct ESInsets {
    int l, t, r, b;
} ESInsets;

/* Which edge does this box reserve, if any? Exposed for diagnostics. */
int es_panel_classify(const ESRect *screen, const ESRect *box);

/*
 * screen: full screen rectangle. boxes/count: candidate window boxes,
 * pre-filtered by the caller as described above. out: reserved inset
 * per edge (0 when free).
 */
void es_panel_insets(const ESRect *screen, const ESRect *boxes, int count,
                     ESInsets *out);

#endif /* EDGESNAP_PANELS_H */
