/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * gutter.h - the divider between two snapped windows.
 *
 * When two windows end up snapped side by side, the seam between them
 * becomes a handle: drag it and both windows resize together, so a
 * half/half split can become 60/40 without touching either window's
 * own size gadget. This is the "internal cursor" the project set out
 * to provide after the first snap.
 *
 * Pure C89 geometry, host-tested: which pairs have a seam, where the
 * strip is, and what dragging it does to both windows. Opening the
 * little window that carries the resize pointer is a frontend job -
 * this file never touches Intuition.
 */

#ifndef EDGESNAP_GUTTER_H
#define EDGESNAP_GUTTER_H

#include "zones.h"
#include "registry.h"

/* How much of the shorter side two windows must share before the
 * boundary between them counts as a seam rather than a coincidence. */
#define ES_SEAM_OVERLAP_PCT 50

/* How close their edges must be to count as touching (apps round
 * sizes to increments, so an exact match cannot be required). */
#define ES_SEAM_TOUCH_TOL 12

/* Smallest width/height the divider may leave a window. The registry
 * does not keep each window's real minimum, so this floor is what
 * stops a drag from squeezing one of them into nothing. */
#define ES_SEAM_MIN_SIDE 80

typedef struct ESSeam {
    int valid;
    int vertical;        /* 1: a vertical seam, dragged left/right    */
    ESRect rect;         /* the strip itself, thickness_px wide/tall  */
    void *ref_a;         /* window on the left (or top)               */
    void *ref_b;         /* window on the right (or bottom)           */
    ESRect box_a;        /* their geometry when the seam was found,   */
    ESRect box_b;        /* so applying a drag needs nothing else     */
    int pos;             /* current seam position: x if vertical      */
    int min_pos;         /* limits, honouring both windows' minima    */
    int max_pos;
} ESSeam;

/*
 * Find the seam between two snapped windows, if there is one. Returns
 * 1 and fills *out when found. thickness_px is how wide the draggable
 * strip should be.
 */
int es_gutter_find(const ESRegistry *reg, int thickness_px, ESSeam *out);

/*
 * Where the two windows end up when the seam is dragged to new_pos.
 * The position is clamped to [min_pos, max_pos] first, so a caller can
 * pass a raw pointer coordinate.
 */
void es_gutter_apply(const ESSeam *seam, int new_pos,
                     ESRect *out_a, ESRect *out_b);

/*
 * Make a zone fill the space its opposite side is NOT using, the way
 * Windows and macOS do: once a pair has been re-balanced to 70/30,
 * snapping a third window to the narrow side must give it that 30%,
 * not the default half. self is the window being snapped (it must not
 * be treated as its own partner). Returns 1 when a partner was found
 * and *rect was adjusted.
 */
int es_pair_fill(const ESRegistry *reg, void *self, int zone,
                 const ESRect *usable, ESRect *rect);

#endif /* EDGESNAP_GUTTER_H */
