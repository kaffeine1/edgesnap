/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * panels.h - dock/panel detection policy: which windows reserve screen
 * edges (AmiDock on OS4, Ambient panels on MorphOS), macOS-style.
 *
 * Pure C89 geometry, host-tested. The platform glue does the walking
 * and the filtering it alone can do (no drag bar, no size gadget, not
 * one of our own preview windows; borderless is not required and a
 * backdrop window is not refused, since Ambient can keep a panel
 * behind the windows) and passes the surviving window boxes here; this
 * module decides which of them are edge panels and how much of each
 * screen edge they reserve. The desktop's own backdrop window covers
 * the whole screen and fails the thickness rule below.
 *
 * Heuristic (documented, field-tuned on real MorphOS, 2026-08-26):
 *   - a panel lives in the OUTER BAND of the screen: its gap from the
 *     nearest edge (in its thin dimension) is at most screen dimension
 *     / ES_PANEL_MAX_GAP_DIV. Real docks float, and users raise them:
 *     a dock anywhere in the outer band is always reserved;
 *   - it is thin: thickness <= screen dimension / ES_PANEL_MAX_THICK_DIV;
 *   - it is long enough to be a bar, not a corner widget:
 *     length >= ES_PANEL_MIN_LEN_PCT % of its edge. A strip standing
 *     along a side edge is a bar also when it is at least
 *     ES_PANEL_MIN_ASPECT times as tall as it is wide, whatever share
 *     of the edge it covers: a dock is made of icons, which keep their
 *     size on any screen, and three of them down the side of a
 *     1080-line screen make 156 lines, under 15% (field case, MorphOS,
 *     2026-10-08). A strip lying along the top or the bottom keeps the
 *     share alone: thin and long is also what a tooltip, a label or a
 *     notification looks like, and they run across the screen, not
 *     down it.
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
#define ES_PANEL_MIN_ASPECT     2
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

/* "left"/"top"/"right"/"bottom", or "no-reserve" for ES_PEDGE_NONE.
 * For diagnostics: a dump that names the edge saves the reader from
 * guessing which window ate the usable area. */
const char *es_panel_edge_name(int edge);

/* How deep this box reserves on the edge it claims, 0 if none. The
 * inset arithmetic lives here once: es_panel_add() uses it, and so does
 * any diagnostic that wants to attribute the reservation to a window. */
int es_panel_depth(const ESRect *screen, const ESRect *box);

/*
 * The same policy one box at a time, so a caller walking a window list
 * needs no array and therefore no fixed limit on how many panels a
 * desktop may have. Start from es_panel_begin(), call es_panel_add()
 * for every candidate, then es_panel_end() to add the breathing room.
 * es_panel_insets() below is these three over an array.
 */
void es_panel_begin(ESInsets *out);
void es_panel_add(const ESRect *screen, const ESRect *box, ESInsets *out);
void es_panel_end(ESInsets *out, int margin_px);

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
