/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * gutter.h - the dividers between snapped windows.
 *
 * When windows end up snapped against each other, the boundary between
 * them becomes a handle: drag it and everything touching it resizes
 * together, so a half/half split can become 60/40 without touching any
 * window's own size gadget. This is the "internal cursor" the project
 * set out to provide after the first snap.
 *
 * A seam is a LINE, not a pair. One window filling the left half, faced
 * by two stacked on the right, share a single vertical seam: dragging
 * it must move all three. Four quadrants share one vertical seam down
 * the middle, moving all four, and one horizontal seam across. Windows
 * are grouped by the boundary they touch, so a horizontal split that
 * exists only on the right side is a seam of its own, moving those two
 * windows and nobody else.
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

/* How many windows may share one side of a seam. Four quadrants put
 * two on each side; the room here is for layouts nobody has built yet. */
#define ES_SEAM_SIDE_MAX 8

/* How many seams a layout may offer at once. Four quadrants have three:
 * one vertical down the middle and one horizontal across. */
#define ES_SEAM_MAX 8

typedef struct ESSeamSide {
    int n;
    void *ref[ES_SEAM_SIDE_MAX];
    ESRect box[ES_SEAM_SIDE_MAX];   /* geometry when the seam was found */
} ESSeamSide;

typedef struct ESSeam {
    int valid;
    int vertical;        /* 1: a vertical seam, dragged left/right    */
    ESRect rect;         /* the strip itself, thickness_px wide/tall  */
    ESSeamSide a;        /* windows on the left (or above)            */
    ESSeamSide b;        /* windows on the right (or below)           */
    int pos;             /* current seam position: x if vertical      */
    int min_pos;         /* limits, honouring every window's minimum  */
    int max_pos;
} ESSeam;

/*
 * Every seam in the layout, not just the first. A seam is a boundary
 * line shared by one or more windows on each side: dragging it moves
 * all of them at once, which is what a half screen facing two stacked
 * windows needs, and what four quadrants need down the middle. Returns
 * how many were written, at most max.
 */
int es_gutter_find_all(const ESRegistry *reg, int thickness_px,
                       ESSeam *out, int max);

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
