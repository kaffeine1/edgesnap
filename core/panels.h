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
 * Heuristic (documented, field-tuned on real MorphOS, 2026-08-26):
 *   - a panel lives in the OUTER BAND of the screen: its gap from the
 *     nearest edge (in its thin dimension) is at most screen dimension
 *     / ES_PANEL_MAX_GAP_DIV. Real docks float, and users raise them:
 *     a dock anywhere in the outer band is always reserved;
 *   - it is thin: thickness <= screen dimension / ES_PANEL_MAX_THICK_DIV;
 *   - it is long enough to be a bar, not a corner widget:
 *     length >= ES_PANEL_MIN_LEN_PCT % of its edge.
 * A panel reserves from the screen edge up to its far side, gap
 * included, PLUS ES_PANEL_MARGIN_PX of breathing room so snapped
 * windows never sit glued to the dock. Multiple panels on one edge
 * reserve the deepest inset. Matching real AmiDock/Ambient panel
 * windows is an explicit validation task recorded in docs/DESIGN.md.
 */

#ifndef EDGESNAP_PANELS_H
#define EDGESNAP_PANELS_H

#include "zones.h"

#define ES_PANEL_MAX_GAP_DIV    4
#define ES_PANEL_MAX_THICK_DIV  4
#define ES_PANEL_MIN_LEN_PCT   15
#define ES_PANEL_MARGIN_PX      8

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
 * pre-filtered by the caller as described above. margin_px: breathing
 * room added to every non-zero inset (ES_PANEL_MARGIN_PX is the
 * default; preferences can change it). out: reserved inset per edge
 * (0 when free).
 */
void es_panel_insets(const ESRect *screen, const ESRect *boxes, int count,
                     int margin_px, ESInsets *out);

#endif /* EDGESNAP_PANELS_H */
