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
 * Windows of a layout group (2.24) are paired by another rule, since a
 * group keeps a gap between its cells and a tiler wants one seam per
 * boundary it drew: see es_seams_find.
 *
 * Pure C89 geometry, host-tested: which pairs have a seam, where the
 * strip is, and what dragging it does to both windows. Opening the
 * little window that carries the resize pointer is a frontend job -
 * this file never touches Intuition. Nothing here keeps static state:
 * the library calls it under its own lock, but every function is
 * reentrant. es_seams_find, the hungriest, takes under a kilobyte of
 * stack on a 32-bit system; es_gutter_find_all and es_gutter_find add
 * a kilobyte more (see there).
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

/* How many windows may share one side of a seam: a whole group may
 * stand on one side of it. A side that would need more gives no seam
 * at all, never a seam that leaves some of its windows behind. */
#define ES_SEAM_SIDE_MAX 32

/* How many seams a layout may offer at once. Four ungrouped quadrants
 * have two: one vertical down the middle and one horizontal across. */
#define ES_SEAM_MAX 8

/* Most boxes one es_seams_find or es_seam_by_refs call takes. */
#define ES_SEAM_BOXES_MAX 64

/* Largest gap a box may carry (ES_GO_GAP_MAX in edgesnap.h). */
#define ES_SEAM_GAP_MAX 200

typedef struct ESSeamSide {
    int n;
    void *ref[ES_SEAM_SIDE_MAX];
    ESRect box[ES_SEAM_SIDE_MAX];   /* geometry when the seam was found */
} ESSeamSide;

typedef struct ESSeam {
    int valid;
    int vertical;        /* 1: a vertical seam, dragged left/right    */
    ESRect rect;         /* the strip: thickness_px wide/tall, or the */
                         /* gap itself when a gap is wider (2.24)     */
    ESSeamSide a;        /* windows on the left (or above)            */
    ESSeamSide b;        /* windows on the right (or below)           */
    int pos;             /* current seam position: x if vertical      */
    int min_pos;         /* limits, honouring every window's minimum  */
    int max_pos;
    int gap;             /* kept between the sides; 0 when ungrouped  */
    unsigned long key;   /* the boxes' key, 0 when ungrouped          */
    const void *scope;   /* the boxes' scope                          */
} ESSeam;

/*
 * One window as es_seams_find sees it. ref is opaque, as in ESSeamSide,
 * and names one box: a ref may appear at most once in one list, which
 * the library's builder sees to. Boxes pair only within one scope (the
 * library passes the screen; NULL is a scope like any other) and only
 * with an equal key: 0 for a window in no group, the group's id for a
 * member. gap is the group's, 0 to ES_SEAM_GAP_MAX: the core treats key
 * 0 as gap 0 and clamps the rest, and boxes of one key must carry the
 * same gap.
 */
typedef struct ESSeamBox {
    void *ref;
    ESRect box;
    const void *scope;
    unsigned long key;
    int gap;
} ESSeamBox;

/*
 * Every seam among in[0..n-1], vertical ones first, then horizontal.
 * Returns how many seams there are, which may exceed max: at most max
 * are written, and out may be NULL when max is 0 or less. n below 0 or
 * above ES_SEAM_BOXES_MAX returns 0.
 *
 * Boxes of key 0 follow the rule es_gutter_find_all always had, and on
 * each axis their seams come first: every box's leading edge is a
 * candidate, taken in list order; a side is every window of the scope
 * whose trailing (side a) or leading (side b) edge lies within
 * ES_SEAM_TOUCH_TOL of the candidate's leading edge, along the whole
 * line, so four quadrants share one seam down the middle; the strip
 * spans the hull of the stretches where the sides face each other, pos
 * is the candidate's leading edge, each window keeps ES_SEAM_MIN_SIDE,
 * and a candidate near a line that already gave a seam is passed over.
 * One thing is new: a side that would need more than ES_SEAM_SIDE_MAX
 * windows gives no seam, and no candidate near that line tries again.
 *
 * Boxes of one key (a group, gap G) are paired by cell boundary, the
 * one es_cell_rect starts from: a's trailing edge + (G - G/2), which is
 * b's leading edge - G/2 (rounded down). For an odd gap that lies half a
 * pixel right of (below) the middle of the gap. The boundary a box's
 * leading edge implies is a candidate line. The lines are looked at
 * from the smallest boundary up; of two at one boundary, the one with
 * the smaller gap first, then the smaller key, then the one a box
 * earlier in the list proposes; and a line that one scope and key
 * propose twice is looked at once (with the smallest gap, should boxes
 * of one key differ). On a line, a window of the scope and key whose
 * trailing (leading) edge implies a boundary within ES_SEAM_TOUCH_TOL
 * of it is on side a (side b); one that is both goes to side a, as with
 * key 0. Two windows on opposite sides are linked when they share any
 * stretch along the line, and the windows that links connect make up
 * one seam, provided two of them face each other (share at least
 * ES_SEAM_OVERLAP_PCT of the shorter one); a window linked to none is
 * on none. The strip spans the hull of the stretches linked windows
 * share; across the line it is the gap itself when the gap is wider
 * than thickness_px, so that all of the space between the windows
 * holds the seam. So four quadrants are two seams down the middle and two
 * across, but a 2x2 whose splits do not line up, where a window on the
 * left shares a stretch with both on the right, is one seam down the
 * middle that moves all four.
 *
 * A seam's position is, of the boundaries its windows imply, one that
 * every one of them lies within ES_SEAM_TOUCH_TOL of, and of those the
 * one most of them imply exactly, ties to the smallest. There is no
 * seam without one; nor when one of its windows touches the position
 * from both sides (it is no larger than the tolerance, gap included);
 * nor when two of its windows on one side share a stretch along it,
 * one behind the other (one ends where the other begins, or before):
 * every drag would give them one edge and run one over the other; nor
 * when a window of the scope and key outside it touches the position
 * from one side and is linked to one of its windows on the other (that
 * window belongs to the seam); nor when a side would need more than
 * ES_SEAM_SIDE_MAX windows; nor when one of its windows is on the same
 * side of a seam a line before gave on this axis, so that a window is
 * on one side of at most one seam per axis. A set of windows that gives
 * no seam keeps none of them from a later line.
 *
 * On each axis the seams of the groups follow those of key 0, by line
 * in the order above, and on one line in the order they start along
 * it. So which seams of the groups there are, with which windows,
 * positions and limits, and in what order, never depends on the order
 * of the list, but for two things: the windows of a side come in list
 * order, and of two seams of one key on two scopes at one boundary, the
 * one whose line a box earlier in the list proposes comes first.
 *
 * Each window of a group keeps ES_SEAM_TOUCH_TOL + 1 - G pixels, at
 * least one (the library adds every window's own limits), so that no
 * drag leaves one touching a boundary from both sides. A window of the
 * scope and key outside the seam that is linked to one of its windows
 * and reaches further toward the other side (ends after a window of
 * side a ends, or begins before a window of side b begins) stops the
 * drag where the gap between them is still whole. Where that is short
 * of both the position and the boundary that window of the seam implies
 * now (es_fit_rect widened one of the two, or the seam's window falls
 * short of the position), the drag toward it stops instead at whichever
 * of those two it reaches first: the seam's window never moves toward
 * it. No drag within the limits thus runs a window into another of its
 * scope and key, or leaves less of a gap to one outside the seam than
 * the group's or than there was; and the position itself may lie
 * outside the limits: a window of the seam that falls short of it can
 * do no more than stay where it is, and one too narrow to reach it keeps
 * its floor. A seam whose limits cross is still
 * reported: its windows face each other, there is just no room to move
 * it.
 *
 * Whatever es_seams_find reports for a key is rebuilt by es_seam_by_refs
 * from the same windows, in any order of in[] and of the sides, at the
 * same position and with the same limits (with -1 for a seam that has no
 * room).
 */
int es_seams_find(const ESSeamBox *in, int n, int thickness_px,
                  ESSeam *out, int max);

/*
 * The seam of a key between exactly these windows (a group's divider,
 * named by a token): a[0..na-1] on the left of (above) it, b[0..nb-1] on
 * the right (below). Returns 1 with *out built, its sides in the order
 * given; -1 when the windows are still that seam but leave no room to
 * move it (min_pos > max_pos, *out built all the same); 0 when they are
 * no longer that seam. 0 too unless na and nb are 1 to
 * ES_SEAM_SIDE_MAX, every ref is distinct and in in[] with one scope,
 * one non-zero key and one gap, and the rules of es_seams_find hold for
 * exactly these windows: they give a position, links among them connect
 * them all and two of them face each other, none of them touches the
 * position from both sides or stands behind another of its side along
 * a stretch they share, and no other window of the scope and key
 * touches it from one side and is linked to one of them on the other (a
 * window that joined the seam makes the set wrong). Windows of the
 * scope and key on the same line that are linked to none of these are
 * not its business. The limits are those es_seams_find gives. out may
 * be NULL to ask for the answer alone.
 */
int es_seam_by_refs(const ESSeamBox *in, int n, int thickness_px,
                    int vertical, void *const *a, int na,
                    void *const *b, int nb, ESSeam *out);

/*
 * Narrow a seam's limits by one window's own size limits: idx is the
 * window's place on side a (side_b 0) or side b (side_b 1), min_size and
 * max_size its smallest and largest width (height for a horizontal
 * seam), max_size 0 or less for none. The gap is honoured: a window on
 * side a spans from its leading edge to pos - (gap - gap/2), one on side
 * b from pos + gap/2 to its trailing edge, and pos is held where both
 * stay within their limits. An idx outside the side changes nothing.
 */
void es_seam_tighten_side(ESSeam *s, int side_b, int idx,
                          int min_size, int max_size);

/*
 * Every seam in the layout, not just the first. A seam is a boundary
 * line shared by one or more windows on each side: dragging it moves
 * all of them at once, which is what a half screen facing two stacked
 * windows needs, and what four quadrants need down the middle. Returns
 * how many were written, at most max.
 *
 * This is es_seams_find over the registry's used slots, in slot order,
 * as boxes of key 0 in one scope: the list costs a kilobyte of stack on
 * a 32-bit system, on top of what es_seams_find uses.
 */
int es_gutter_find_all(const ESRegistry *reg, int thickness_px,
                       ESSeam *out, int max);

int es_gutter_find(const ESRegistry *reg, int thickness_px, ESSeam *out);

/*
 * Where the two windows end up when the seam is dragged to new_pos.
 * The position is clamped to [min_pos, max_pos] first, so a caller can
 * pass a raw pointer coordinate. The seam's gap is kept: side a ends
 * gap - gap/2 before that position, side b starts gap/2 after it, and
 * both keep their far edges. A seam whose limits cross has no room:
 * every window keeps the box the seam holds for it.
 */
void es_gutter_apply(const ESSeam *seam, int new_pos,
                     ESRect *out_a, ESRect *out_b);

/*
 * Fill what the opposite side is not using. The complement is taken
 * only when the window can actually be that wide: a window whose
 * minimum width is larger than the complement would be clamped by
 * Intuition to something other than what was recorded, and a recorded
 * box that does not match the real one reads as "the user moved it",
 * which kills the seam. min_w and max_w are the window's limits, 0 for
 * none. Returns 1 when rect was changed.
 */
int es_pair_fill(const ESRegistry *reg, void *self, int zone,
                 const ESRect *usable, int min_w, int max_w, ESRect *rect);

#endif /* EDGESNAP_GUTTER_H */
