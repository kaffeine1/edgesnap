/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * divtokens.h - tokens for the dividers of a layout group, and the
 * small decisions about a divider that need no Intuition.
 *
 * A tiler asks for the seams of its group (2.24) and gets a token for
 * each, which it hands back to move that seam. A token names the
 * windows on each side, not a line on the screen: the seam it moves is
 * rebuilt from exactly those windows, so a drag can never slide over to
 * another seam, and once those windows stop being that seam the token
 * is retired for good. The library keeps one table of tokens per group
 * and one counter for all of them.
 *
 * Next to the table: the order in which a tiler is told about the
 * seams and about the windows of a side, the rule by which a drag of
 * the commodity keeps following the seam it grabbed, and the walk that
 * decides whether a point of a seam is one the user can see.
 *
 * Pure C89, host-tested. No static state and no system calls: the
 * library calls all of it under its own lock.
 */

#ifndef EDGESNAP_DIVTOKENS_H
#define EDGESNAP_DIVTOKENS_H

#include "gutter.h"

/* How many tokens one group keeps. A window is on side a of one seam
 * per axis at most, so a group of 32 windows has at most 64 seams, and
 * a query never has to leave one of its own seams without a token. */
#define ES_DIVTOK_SLOTS 64

/* How many members a mask can name: bit k is member k, and an unsigned
 * long has at least 32 bits. */
#define ES_DIVTOK_MEMBERS 32
#define ES_DIVTOK_BIT(k) (1UL << (k))

/* The largest token. Tokens travel as ULONG, 32 bits on every system,
 * so the counter turns round there, on a 64-bit system too. */
#define ES_DIVTOK_MAX 0xFFFFFFFFUL

/*
 * One token: a seam of the group, named by the windows on each side as
 * member masks. The library keeps the masks in step with its member
 * list (see es_divtok_member_left), so equal masks are equal sets of
 * windows at that moment. A token keeps its axis: the same windows
 * above and below each other are another seam with another token.
 */
typedef struct ESDivToken {
    unsigned long token;   /* 0: a free slot                          */
    unsigned long a;       /* the windows left of (above) the seam    */
    unsigned long b;       /* the windows right of (below) it         */
    int vertical;
} ESDivToken;

typedef struct ESDivTokens {
    ESDivToken slot[ES_DIVTOK_SLOTS];
} ESDivTokens;

/* One seam of a query, as the table sees it. */
typedef struct ESDivSet {
    int vertical;
    unsigned long a;
    unsigned long b;
} ESDivSet;

/* Every slot free: for a group that is created, deleted or cleared. */
void es_divtok_clear(ESDivTokens *t);

/* The slot holding token, or -1 (always for 0, which no token is). */
int es_divtok_find(const ESDivTokens *t, unsigned long token);

/*
 * A query hands its seams to the table in three passes, so that a seam
 * never takes the slot of a token another seam of the same query still
 * has a claim to, and stale tokens leave before live ones are evicted:
 *
 *   1. es_divtok_reuse: every seam the table knows gets its token back;
 *   2. the library retires, with es_divtok_retire, every token of a
 *      slot this query did not claim whose windows are no longer that
 *      seam (only those: a claimed slot is a token being reported);
 *   3. es_divtok_mint: every seam still without one gets a new token.
 *
 * s[0..n-1] are the seams, tok[0..n-1] receives their tokens (0 for
 * none), claimed[0..ES_DIVTOK_SLOTS-1] marks the slots whose tokens the
 * query reports. A set with an empty side, with a window on both sides
 * or with a member beyond ES_DIVTOK_MEMBERS names nothing a token could
 * name, and gets 0 from both passes.
 */

/* Pass 1. claimed is cleared first, then set for every slot a seam
 * gets its token from; tok[i] is 0 for a seam the table does not know
 * (by axis and by both masks). */
void es_divtok_reuse(const ESDivTokens *t, const ESDivSet *s, int n,
                     unsigned long *tok, unsigned char *claimed);

/* A token leaves for good: its slot is free, and since the counter
 * only moves on, its value never names a seam again. A slot outside
 * the table changes nothing. */
void es_divtok_retire(ESDivTokens *t, int slot);

/*
 * Pass 3. Every seam whose tok[i] is still 0 gets a new token in a free
 * slot, or else in the slot of the oldest token (the smallest value)
 * this query has not claimed; a slot it takes is claimed. The value is
 * *next, and *next moves on, never to 0 and never past ES_DIVTOK_MAX
 * (it starts again at 1). With every slot claimed a seam keeps 0. Two
 * seams of one query between the same windows share one token.
 *
 * The counter turns round after ES_DIVTOK_MAX tokens. From then on a
 * small value may be a new one and an old one may come back; at one
 * token per new seam that is far beyond any session, and it is left
 * at that.
 */
void es_divtok_mint(ESDivTokens *t, const ESDivSet *s, int n,
                    unsigned long *tok, unsigned char *claimed,
                    unsigned long *next);

/*
 * Member k left the group and the last one (index last) took its
 * place, as the library's member list does it. Every token naming k
 * retires; the others name last as k from now on; and a token naming
 * last or beyond after that (no such member is left) retires too.
 * k == last is the last member leaving. Unless 0 <= k <= last <
 * ES_DIVTOK_MEMBERS nothing changes.
 */
void es_divtok_member_left(ESDivTokens *t, int k, int last);

/*
 * The order in which a tiler is told about the seams: vertical ones
 * first, then horizontal; on each axis by position, then by where the
 * strip starts along the seam; seams equal in all three keep their
 * order in s. rank[0..n-1] receives the indices into s, in that order.
 * rank holds unsigned chars: n above 256 counts as 256.
 */
void es_divider_order(const ESSeam *s, int n, unsigned char *rank);

/*
 * The order of the windows of one side: top-most first along a
 * vertical seam, left-most first along a horizontal one, windows that
 * start at the same place in the order of the side. rank[0..side->n-1]
 * receives the indices into the side; an n outside 0 to
 * ES_SEAM_SIDE_MAX counts as the nearer of the two.
 */
void es_divider_side_order(const ESSeamSide *side, int vertical,
                           unsigned char *rank);

/*
 * Does a move of the seam at line (vertical or not) follow the seam a
 * task last grabbed? That seam was at rpos on axis rvertical, and
 * rlast is where the task last asked to move it. It does when the axis
 * is the same and line is either rlast exactly or within
 * ES_SEAM_TOUCH_TOL of rpos: a drag names the line it last asked for,
 * a fresh press names the line it was told about.
 */
int es_divider_follows(int rvertical, int rpos, int rlast,
                       int vertical, int line);

/*
 * Is a point of a seam's strip one the user can see, or does another
 * window lie over it there? A frontend lights the seam only when it is
 * seen. The library walks the screen's layers front to back and hands
 * each to es_cover_step as one of the kinds below, with whether its
 * bounds hold the point, until es_cover_step returns 1; w->visible is
 * the answer, and a walk that runs out undecided leaves it 1.
 *
 * The FACING windows are those of the seam whose extent along it holds
 * the point's coordinate there: on a vertical seam, the ones beside the
 * point. For a group's seam the library takes that extent widened by
 * the group's gap at both ends, so that the gap between two windows of
 * one side, where a stack meets a master, is no hole in the strip. A
 * window of the seam holding the point, or a backdrop holding it (the
 * gap can show the desktop), decides: seen. Another window holding it
 * covers it, unless the point lies between the two sides, with a facing
 * window on each (in the gap of a group), and the walk has met all of
 * them by then: a window behind every window beside the gap is passed
 * over. Anywhere else, beside the windows of one side only (a hole in
 * the strip) or beside none, the first window holding the point
 * decides, the rule from before 2.24. The library starts the walk with
 * no facing windows for a seam of windows in no group, which keeps no
 * gap, so the answers for those are the ones they always were. Layers
 * with no window, or a frontend's own, are of no kind (SKIP), as is any
 * other kind.
 */
#define ES_LAYER_SKIP     0   /* no window, or a frontend's own        */
#define ES_LAYER_FOREIGN  1   /* a window outside the seam             */
#define ES_LAYER_BACKDROP 2   /* a backdrop window outside the seam    */
#define ES_LAYER_SEAM     3   /* a window of the seam not facing       */
#define ES_LAYER_FACING   4   /* a facing window (each fed once)       */

typedef struct ESCoverWalk {
    int facing;    /* the facing windows that count: 0 unless the   */
                   /* point lies between the two sides              */
    int unseen;    /* of those, how many the walk has not met yet   */
    int done;      /* decided                                       */
    int visible;   /* the answer: 1 until the walk decides it is 0  */
} ESCoverWalk;

/* Start a walk for a point with facing_a facing windows on side a of
 * the seam and facing_b on side b (below 0: none, above
 * ES_SEAM_SIDE_MAX: that many). Unless both are above 0 the point does
 * not lie between the two sides, and none counts. */
void es_cover_begin(ESCoverWalk *w, int facing_a, int facing_b);

/* One layer, the next one back. Returns 1 once decided, from then on
 * without looking at what it is given. */
int es_cover_step(ESCoverWalk *w, int kind, int holds_point);

#endif /* EDGESNAP_DIVTOKENS_H */
