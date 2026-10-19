/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * gutter.c - seam geometry for the divider between snapped windows.
 */

#include "gutter.h"

/* es_gutter_find_all hands the whole registry to es_seams_find. */
typedef char es_gutter_slots_fit[(ES_REGISTRY_SLOTS <= ES_SEAM_BOXES_MAX)
                                 ? 1 : -1];

/* A seam's sets are labelled by box index in unsigned chars. */
typedef char es_seam_labels_fit[(ES_SEAM_BOXES_MAX <= 256) ? 1 : -1];

/* What a window of a group keeps when the core knows nothing better:
 * one pixel. The library adds every window's own limits. */
#define ES_SEAM_MIN_CELL 1

/* Marks per box while one line is looked at, or one set of windows is
 * checked; the side bits are the box's side of the line. */
#define ES_M_A      0x01   /* touches it from the left (or above)    */
#define ES_M_B      0x02   /* touches it from the right (or below)   */
#define ES_M_SIDE   0x03
#define ES_M_LISTED 0x04   /* one of the windows of the seam at hand */
#define ES_M_PAIRED 0x08   /* linked to a window across the line     */
#define ES_M_SPAN   0x10   /* lo[] holds where its seam starts       */
#define ES_M_DONE   0x20   /* its seam has been dealt with           */

/* Flags per box over one axis of es_seams_find. */
#define ES_F_LINE   0x01   /* key 0: its line gave or refused a seam    */
#define ES_F_SEAM_A 0x02   /* a group's: on side a of a seam given      */
#define ES_F_SEAM_B 0x04   /* on side b of one                          */
#define ES_F_TRIED  0x08   /* the line it proposes has been looked at   */

static int es_min(int a, int b)
{
    return a < b ? a : b;
}

static int es_max(int a, int b)
{
    return a > b ? a : b;
}

static int es_near(int a, int b)
{
    int d = a - b;

    if (d < 0) {
        d = -d;
    }
    return d <= ES_SEAM_TOUCH_TOL;
}

/*
 * Do two windows share any stretch along a seam on this axis (of their
 * vertical extent for a vertical seam)? *from and *to receive it. For
 * windows of a group on either side of a line that is a link.
 */
static int es_links(const ESRect *a, const ESRect *b, int vertical,
                    int *from, int *to)
{
    if (vertical) {
        *from = es_max(a->y, b->y);
        *to = es_min(a->y + a->h, b->y + b->h);
    } else {
        *from = es_max(a->x, b->x);
        *to = es_min(a->x + a->w, b->x + b->w);
    }
    return *to - *from > 0;
}

/* Do they share enough of it to face each other across the seam: at
 * least ES_SEAM_OVERLAP_PCT of the shorter one? */
static int es_faces(const ESRect *a, const ESRect *b, int vertical,
                    int *from, int *to)
{
    int shorter = vertical ? es_min(a->h, b->h) : es_min(a->w, b->w);

    return es_links(a, b, vertical, from, to) && shorter > 0 &&
           (*to - *from) * 100 >= shorter * ES_SEAM_OVERLAP_PCT;
}

static int es_lead(const ESRect *r, int vertical)
{
    return vertical ? r->x : r->y;
}

static int es_trail(const ESRect *r, int vertical)
{
    return vertical ? r->x + r->w : r->y + r->h;
}

static int es_clamp_gap(int gap)
{
    if (gap < 0) {
        return 0;
    }
    return gap > ES_SEAM_GAP_MAX ? ES_SEAM_GAP_MAX : gap;
}

/* The gap a box carries, as the core reads it: none without a group. */
static int es_box_gap(const ESSeamBox *b)
{
    return b->key == 0 ? 0 : es_clamp_gap(b->gap);
}

/*
 * What a window of a group keeps on a seam. Its extent plus the gap is
 * how far apart the boundaries it implies from its two sides lie, and a
 * window for which that is within ES_SEAM_TOUCH_TOL touches a boundary
 * from both sides at once: no drag may leave one like that.
 */
static int es_cell_floor(int gap)
{
    return es_max(ES_SEAM_MIN_CELL, ES_SEAM_TOUCH_TOL + 1 - gap);
}

/* Boxes that may pair: one scope, one key. */
static int es_same_set(const ESSeamBox *x, const ESSeamBox *y)
{
    return x->scope == y->scope && x->key == y->key;
}

/*
 * The cell boundary a box implies from its side of a seam: es_cell_rect
 * undone, so the trailing edge of side a gives back gap - gap/2 and the
 * leading edge of side b gives back gap/2.
 */
static int es_implied(const ESRect *r, int vertical, int side, int gap)
{
    return side == ES_M_A ? es_trail(r, vertical) + (gap - gap / 2)
                          : es_lead(r, vertical) - gap / 2;
}

/* The line a box proposes: the boundary its leading edge implies. */
static int es_candidate(const ESSeamBox *b, int vertical)
{
    return es_lead(&b->box, vertical) - es_box_gap(b) / 2;
}

static int es_box_of(const ESSeamBox *in, int n, const void *ref)
{
    int i;

    for (i = 0; i < n; i++) {
        if (in[i].ref == ref) {
            return i;
        }
    }
    return -1;
}

/* Seams by connection: the box standing for the set x belongs to. */
static int es_root(unsigned char *parent, int x)
{
    while (parent[x] != x) {
        parent[x] = parent[parent[x]];
        x = parent[x];
    }
    return x;
}

/* The smaller index stands for both, so the result is the same in
 * whatever order the pairs come. */
static void es_join(unsigned char *parent, int p, int q)
{
    p = es_root(parent, p);
    q = es_root(parent, q);
    if (p < q) {
        parent[q] = (unsigned char)p;
    } else if (q < p) {
        parent[p] = (unsigned char)q;
    }
}

/*
 * Where the handle can act: the strip spans the hull of the stretches a
 * listed window on one side shares with a listed one on the other. For
 * key 0 (linked 0) only the stretches of pairs that face each other
 * count, as they always did; for a group (linked 1) those of every
 * linked pair. Returns whether some pair faces, which a seam needs.
 */
static int es_marked_span(const ESSeamBox *in, int n, int vertical,
                          const unsigned char *mark, int linked,
                          int *from, int *to)
{
    int faced = 0, spanned = 0;
    int i, j;

    for (i = 0; i < n; i++) {
        if ((mark[i] & (ES_M_LISTED | ES_M_SIDE)) != (ES_M_LISTED | ES_M_A)) {
            continue;
        }
        for (j = 0; j < n; j++) {
            int lo, hi, f;

            if ((mark[j] & (ES_M_LISTED | ES_M_SIDE)) !=
                    (ES_M_LISTED | ES_M_B) ||
                !es_links(&in[i].box, &in[j].box, vertical, &lo, &hi)) {
                continue;
            }
            f = es_faces(&in[i].box, &in[j].box, vertical, &lo, &hi);
            faced |= f;
            if (!f && !linked) {
                continue;
            }
            if (!spanned) {
                *from = lo;
                *to = hi;
                spanned = 1;
            } else {
                if (lo < *from) {
                    *from = lo;
                }
                if (hi > *to) {
                    *to = hi;
                }
            }
        }
    }
    return faced;
}

/* Every listed window keeps floor_px and the gap stays whole, so the
 * tightest one decides. */
static void es_marked_limits(const ESSeamBox *in, int n, int vertical,
                             const unsigned char *mark, int floor_px,
                             int gap, int *min_pos, int *max_pos)
{
    int i;

    *min_pos = -0x7FFF;
    *max_pos = 0x7FFF;
    for (i = 0; i < n; i++) {
        int lim;

        if (!(mark[i] & ES_M_LISTED)) {
            continue;
        }
        if (mark[i] & ES_M_A) {
            lim = es_lead(&in[i].box, vertical) + floor_px + (gap - gap / 2);
            if (lim > *min_pos) {
                *min_pos = lim;
            }
        } else {
            lim = es_trail(&in[i].box, vertical) - floor_px - gap / 2;
            if (lim < *max_pos) {
                *max_pos = lim;
            }
        }
    }
}

/*
 * Where the listed windows put their boundary: of the values they
 * imply, one that every listed window lies within ES_SEAM_TOUCH_TOL of,
 * and of those the one most of them imply exactly, ties to the
 * smallest, so that no order of the list can move it. 0 when there is
 * no such value, or a listed window carries another gap. list[0..m-1]
 * are the listed windows.
 */
static int es_marked_pos(const ESSeamBox *in, int vertical, int gap,
                         const unsigned char *mark,
                         const unsigned char *list, int m, int *pos)
{
    int best = 0, best_count = -1;
    int i, j;

    for (i = 0; i < m; i++) {
        if (es_box_gap(&in[list[i]]) != gap) {
            return 0;
        }
    }
    for (i = 0; i < m; i++) {
        int v, count = 0, all = 1;

        v = es_implied(&in[list[i]].box, vertical, mark[list[i]] & ES_M_SIDE,
                       gap);
        for (j = 0; j < m && all; j++) {
            int w = es_implied(&in[list[j]].box, vertical,
                               mark[list[j]] & ES_M_SIDE, gap);

            if (w == v) {
                count++;
            } else if (!es_near(w, v)) {
                all = 0;
            }
        }
        if (all && (count > best_count ||
                    (count == best_count && v < best))) {
            best = v;
            best_count = count;
        }
    }
    if (best_count < 0) {
        return 0;
    }
    *pos = best;
    return 1;
}

/*
 * Is the seam whole, by the rule es_seams_find and es_seam_by_refs
 * share? Not when a listed window touches pos from both sides, being no
 * larger than the tolerance with the gap: which side it is on is
 * anybody's guess, and a drag would run its neighbour over it. Nor when
 * two listed windows of one side are linked along the line, one behind
 * the other across it: any drag gives them one edge and runs the one
 * behind over the one in front. Nor when a window of the same scope and
 * key that is not listed touches pos from one side and is linked to a
 * listed window on the other: it belongs to the seam, so the listed
 * windows are not that seam.
 */
static int es_marked_whole(const ESSeamBox *in, int n, int vertical,
                           const ESSeamBox *like, int gap, int pos,
                           const unsigned char *mark,
                           const unsigned char *list, int m)
{
    int u, k, lo, hi;

    for (u = 0; u < n; u++) {
        int qa, qb;

        if (!es_same_set(&in[u], like)) {
            continue;
        }
        qa = es_near(es_implied(&in[u].box, vertical, ES_M_A, gap), pos);
        qb = es_near(es_implied(&in[u].box, vertical, ES_M_B, gap), pos);
        if (mark[u] & ES_M_LISTED) {
            if (qa && qb) {
                return 0;
            }
            for (k = 0; k < m; k++) {
                const ESRect *r = &in[list[k]].box;

                if (list[k] != u &&
                    (mark[list[k]] & ES_M_SIDE) == (mark[u] & ES_M_SIDE) &&
                    es_trail(r, vertical) <= es_lead(&in[u].box, vertical) &&
                    es_links(&in[u].box, r, vertical, &lo, &hi)) {
                    return 0;
                }
            }
            continue;
        }
        for (k = 0; k < m && (qa || qb); k++) {
            if (((mark[list[k]] & ES_M_A) ? qb : qa) &&
                es_links(&in[u].box, &in[list[k]].box, vertical, &lo, &hi)) {
                return 0;
            }
        }
    }
    return 1;
}

/*
 * Where the windows of the scope and key outside a whole seam stop it.
 * One that is linked to a listed window and reaches further toward the
 * other side than it (ends after a window of side a ends, or begins
 * before a window of side b begins) stops the drag where the gap
 * between them is still whole. Where that is short of both pos and the
 * boundary the listed window implies now (es_fit_rect widened one of
 * them, or the listed window falls short of pos), the drag toward it
 * stops instead at whichever of those two it reaches first. So a drag
 * never takes a window of the seam into one of these, nor leaves less
 * of a gap to it than the group's or than there is now, and pos may lie
 * outside the limits.
 */
static void es_marked_room(const ESSeamBox *in, int n, int vertical,
                           const ESSeamBox *like, int gap, int pos,
                           const unsigned char *mark,
                           const unsigned char *list, int m,
                           int *min_pos, int *max_pos)
{
    int u, k;

    for (u = 0; u < n; u++) {
        int va, vb, lead, trail;

        if ((mark[u] & ES_M_LISTED) || !es_same_set(&in[u], like)) {
            continue;
        }
        va = es_implied(&in[u].box, vertical, ES_M_A, gap);
        vb = es_implied(&in[u].box, vertical, ES_M_B, gap);
        lead = es_lead(&in[u].box, vertical);
        trail = es_trail(&in[u].box, vertical);
        for (k = 0; k < m; k++) {
            const ESRect *r = &in[list[k]].box;
            int lo, hi, now;

            if (!es_links(&in[u].box, r, vertical, &lo, &hi)) {
                continue;
            }
            if (mark[list[k]] & ES_M_A) {
                if (trail > es_trail(r, vertical)) {
                    now = es_implied(r, vertical, ES_M_A, gap);
                    *max_pos = es_min(*max_pos, es_max(vb, es_min(pos, now)));
                }
            } else if (lead < es_lead(r, vertical)) {
                now = es_implied(r, vertical, ES_M_B, gap);
                *min_pos = es_max(*min_pos, es_min(va, es_max(pos, now)));
            }
        }
    }
}

/* Everything of a seam but its sides. */
static void es_seam_frame(ESSeam *out, int vertical, int pos, int thickness,
                          int from, int to, int min_pos, int max_pos,
                          int gap, const ESSeamBox *like)
{
    int lo = pos - thickness / 2;
    int size = thickness;

    /* A gap wider than the strip is the strip: a press anywhere in the
     * empty space between the two windows then lands on the seam, not
     * on whatever lies behind the space beside a strip in its middle
     * (2.24, found with a gap of 20 and a handle of 10). */
    if (gap > thickness) {
        lo = pos - (gap - gap / 2);
        size = gap;
    }
    out->valid = 1;
    out->vertical = vertical;
    out->pos = pos;
    if (vertical) {
        out->rect.x = lo;
        out->rect.y = from;
        out->rect.w = size;
        out->rect.h = to - from;
    } else {
        out->rect.x = from;
        out->rect.y = lo;
        out->rect.w = to - from;
        out->rect.h = size;
    }
    out->min_pos = min_pos;
    out->max_pos = max_pos;
    out->gap = gap;
    out->key = like->key;
    out->scope = like->scope;
}

/* The listed windows as the seam's sides, in list order. */
static void es_marked_sides(const ESSeamBox *in, int n,
                            const unsigned char *mark, ESSeam *out)
{
    int i;

    out->a.n = 0;
    out->b.n = 0;
    for (i = 0; i < n; i++) {
        ESSeamSide *side;

        if (!(mark[i] & ES_M_LISTED)) {
            continue;
        }
        side = (mark[i] & ES_M_A) ? &out->a : &out->b;
        side->ref[side->n] = in[i].ref;
        side->box[side->n] = in[i].box;
        side->n++;
    }
}

/*
 * Boxes of key 0: has a line near this box's candidate already given,
 * or refused, a seam of its scope on this axis? Boundaries are found
 * once per window that touches them, so the same line comes up twice.
 */
static int es_line_seen(const ESSeamBox *in, int cand, int vertical,
                        const unsigned char *flag)
{
    int line = es_candidate(&in[cand], vertical);
    int j;

    for (j = 0; j < cand; j++) {
        if ((flag[j] & ES_F_LINE) && es_same_set(&in[j], &in[cand]) &&
            es_near(es_candidate(&in[j], vertical), line)) {
            return 1;
        }
    }
    return 0;
}

/*
 * Boxes of key 0: collect the windows whose edge lies on the leading
 * edge of box cand and build the seam there. A side is every window
 * touching the line from that direction, so one window facing two
 * stacked ones gives a side of 1 and a side of 2, and all three move
 * together. Returns 1 for a seam (built in *out when out is not NULL),
 * 0 for none, and -1 when a side would need more than ES_SEAM_SIDE_MAX
 * windows: a seam that left some of them behind would push its other
 * side into them.
 */
static int es_line_plain(const ESSeamBox *in, int n, int vertical,
                         int cand, int thickness, unsigned char *mark,
                         ESSeam *out)
{
    int line = es_lead(&in[cand].box, vertical);
    int na = 0, nb = 0;
    int from = 0, to = 0, min_pos, max_pos;
    int i;

    for (i = 0; i < n; i++) {
        mark[i] = 0;
        if (!es_same_set(&in[i], &in[cand])) {
            continue;
        }
        if (es_near(es_trail(&in[i].box, vertical), line)) {
            mark[i] = ES_M_LISTED | ES_M_A;
            na++;
        } else if (es_near(es_lead(&in[i].box, vertical), line)) {
            mark[i] = ES_M_LISTED | ES_M_B;
            nb++;
        }
    }
    if (na > ES_SEAM_SIDE_MAX || nb > ES_SEAM_SIDE_MAX) {
        return -1;
    }
    if (na == 0 || nb == 0 ||
        !es_marked_span(in, n, vertical, mark, 0, &from, &to)) {
        return 0;
    }
    es_marked_limits(in, n, vertical, mark, ES_SEAM_MIN_SIDE, 0,
                     &min_pos, &max_pos);
    if (min_pos > max_pos) {
        return 0;              /* too small to be divided sensibly */
    }
    if (out != 0) {
        es_seam_frame(out, vertical, line, thickness, from, to,
                      min_pos, max_pos, 0, &in[cand]);
        es_marked_sides(in, n, mark, out);
    }
    return 1;
}

/*
 * Boxes of one key, on the line the leading edge of box cand proposes:
 * every window of the key touching it, linked to those across it that
 * it shares any stretch with, and one seam per set of windows joined by
 * links, in the order the sets start along the line. A set none of
 * whose windows is already on its side of a seam of this axis, and
 * that passes every rule of es_seams_find, is a seam: written to
 * out[*total] while *total < max and counted all the same, its windows
 * recorded in flag[] so that no later line offers them on that side
 * again. A set turned down records nothing: a later line may still
 * hold a seam of some of its windows, and the rules see to it that such
 * a seam can run into none of the others.
 */
static void es_line_keyed(const ESSeamBox *in, int n, int vertical,
                          int cand, int thickness, unsigned char *flag,
                          unsigned char *mark, unsigned char *parent,
                          unsigned char *list, int *lo, ESSeam *out,
                          int max, int *total)
{
    const ESSeamBox *like = &in[cand];
    int gap = es_box_gap(like);
    int line = es_candidate(like, vertical);
    int i, j;

    for (i = 0; i < n; i++) {
        mark[i] = 0;
        parent[i] = (unsigned char)i;
        if (!es_same_set(&in[i], like)) {
            continue;
        }
        if (es_near(es_implied(&in[i].box, vertical, ES_M_A, gap), line)) {
            mark[i] = ES_M_A;
        } else if (es_near(es_implied(&in[i].box, vertical, ES_M_B, gap),
                           line)) {
            mark[i] = ES_M_B;
        }
    }
    for (i = 0; i < n; i++) {
        if (!(mark[i] & ES_M_A)) {
            continue;
        }
        for (j = 0; j < n; j++) {
            int from, to;

            if ((mark[j] & ES_M_B) &&
                es_links(&in[i].box, &in[j].box, vertical, &from, &to)) {
                mark[i] |= ES_M_PAIRED;
                mark[j] |= ES_M_PAIRED;
                es_join(parent, i, j);
            }
        }
    }
    /* where each set starts, kept by the box that stands for it */
    for (i = 0; i < n; i++) {
        if (!(mark[i] & ES_M_A)) {
            continue;
        }
        for (j = 0; j < n; j++) {
            int from, to, r;

            if (!(mark[j] & ES_M_B) ||
                !es_links(&in[i].box, &in[j].box, vertical, &from, &to)) {
                continue;
            }
            r = es_root(parent, i);
            if (!(mark[r] & ES_M_SPAN) || from < lo[r]) {
                lo[r] = from;
                mark[r] |= ES_M_SPAN;
            }
        }
    }

    for (;;) {
        int best = -1, na = 0, nb = 0, dealt = 0, pos = 0;
        int from = 0, to = 0, min_pos, max_pos;

        for (i = 0; i < n; i++) {
            if ((mark[i] & (ES_M_PAIRED | ES_M_DONE)) == ES_M_PAIRED &&
                es_root(parent, i) == i && (best < 0 || lo[i] < lo[best])) {
                best = i;
            }
        }
        if (best < 0) {
            break;
        }
        for (i = 0; i < n; i++) {
            mark[i] = (unsigned char)(mark[i] & ~ES_M_LISTED);
            if (!(mark[i] & ES_M_PAIRED) || es_root(parent, i) != best) {
                continue;
            }
            mark[i] |= ES_M_LISTED | ES_M_DONE;
            list[na + nb] = (unsigned char)i;
            if (mark[i] & ES_M_A) {
                na++;
                dealt |= flag[i] & ES_F_SEAM_A;
            } else {
                nb++;
                dealt |= flag[i] & ES_F_SEAM_B;
            }
        }
        if (dealt || na > ES_SEAM_SIDE_MAX || nb > ES_SEAM_SIDE_MAX ||
            !es_marked_span(in, n, vertical, mark, 1, &from, &to) ||
            !es_marked_pos(in, vertical, gap, mark, list, na + nb, &pos) ||
            !es_marked_whole(in, n, vertical, like, gap, pos, mark,
                             list, na + nb)) {
            continue;
        }
        for (i = 0; i < na + nb; i++) {
            int k = list[i];

            flag[k] = (unsigned char)(flag[k] | ((mark[k] & ES_M_A)
                                                 ? ES_F_SEAM_A
                                                 : ES_F_SEAM_B));
        }
        if (*total < max) {
            es_marked_limits(in, n, vertical, mark, es_cell_floor(gap), gap,
                             &min_pos, &max_pos);
            es_marked_room(in, n, vertical, like, gap, pos, mark,
                           list, na + nb, &min_pos, &max_pos);
            es_seam_frame(&out[*total], vertical, pos, thickness, from, to,
                          min_pos, max_pos, gap, like);
            es_marked_sides(in, n, mark, &out[*total]);
        }
        (*total)++;
    }
}

/*
 * The order in which the lines of the groups are looked at: from the
 * smallest boundary up; of two at one boundary, the smaller gap first,
 * then the smaller key, then the box earlier in the list. Does the line
 * box i proposes come before the one box j proposes?
 */
static int es_line_before(const ESSeamBox *in, int vertical, int i, int j)
{
    int ci = es_candidate(&in[i], vertical);
    int cj = es_candidate(&in[j], vertical);

    if (ci != cj) {
        return ci < cj;
    }
    if (es_box_gap(&in[i]) != es_box_gap(&in[j])) {
        return es_box_gap(&in[i]) < es_box_gap(&in[j]);
    }
    if (in[i].key != in[j].key) {
        return in[i].key < in[j].key;
    }
    return i < j;
}

int es_seams_find(const ESSeamBox *in, int n, int thickness_px,
                  ESSeam *out, int max)
{
    unsigned char flag[ES_SEAM_BOXES_MAX];
    unsigned char mark[ES_SEAM_BOXES_MAX];
    unsigned char parent[ES_SEAM_BOXES_MAX];
    unsigned char list[ES_SEAM_BOXES_MAX];
    int lo[ES_SEAM_BOXES_MAX];
    int total = 0;
    int i, pass;

    if (n < 0 || n > ES_SEAM_BOXES_MAX) {
        return 0;
    }
    if (out == 0 || max < 0) {
        max = 0;
    }
    for (pass = 0; pass < 2; pass++) {
        int vertical = (pass == 0);

        for (i = 0; i < n; i++) {
            flag[i] = 0;
        }
        /* Every window's leading edge is a candidate boundary: for key 0
         * in list order, as es_gutter_find_all always took them. */
        for (i = 0; i < n; i++) {
            int r;

            if (in[i].key != 0 || es_line_seen(in, i, vertical, flag)) {
                continue;
            }
            r = es_line_plain(in, n, vertical, i, thickness_px, mark,
                              total < max ? &out[total] : 0);
            if (r != 0) {
                flag[i] |= ES_F_LINE;
            }
            if (r > 0) {
                total++;
            }
        }
        /* For a group in the order of es_line_before, so that which
         * line deals with a window first, and so the seams, never depend
         * on the order of the list. A line one scope and key propose
         * twice (with the smallest gap, should boxes of one key differ)
         * is looked at once; the flags of es_line_keyed keep the rest
         * apart. */
        for (;;) {
            int cand = -1, line;

            for (i = 0; i < n; i++) {
                if (in[i].key != 0 && !(flag[i] & ES_F_TRIED) &&
                    (cand < 0 || es_line_before(in, vertical, i, cand))) {
                    cand = i;
                }
            }
            if (cand < 0) {
                break;
            }
            line = es_candidate(&in[cand], vertical);
            for (i = 0; i < n; i++) {
                if (es_same_set(&in[i], &in[cand]) &&
                    es_candidate(&in[i], vertical) == line) {
                    flag[i] |= ES_F_TRIED;
                }
            }
            es_line_keyed(in, n, vertical, cand, thickness_px, flag, mark,
                          parent, list, lo, out, max, &total);
        }
    }
    return total;
}

int es_seam_by_refs(const ESSeamBox *in, int n, int thickness_px,
                    int vertical, void *const *a, int na,
                    void *const *b, int nb, ESSeam *out)
{
    unsigned char mark[ES_SEAM_BOXES_MAX];
    unsigned char parent[ES_SEAM_BOXES_MAX];
    unsigned char list[2 * ES_SEAM_SIDE_MAX];
    const ESSeamBox *like = 0;
    int gap = 0, pos = 0, from = 0, to = 0, min_pos, max_pos;
    int root = -1;
    int i, j;

    vertical = vertical != 0;
    if (n < 0 || n > ES_SEAM_BOXES_MAX || a == 0 || b == 0 ||
        na < 1 || na > ES_SEAM_SIDE_MAX || nb < 1 || nb > ES_SEAM_SIDE_MAX) {
        return 0;
    }
    for (i = 0; i < n; i++) {
        mark[i] = 0;
        parent[i] = (unsigned char)i;
    }
    for (i = 0; i < na + nb; i++) {
        int k = es_box_of(in, n, i < na ? a[i] : b[i - na]);

        if (k < 0 || mark[k] != 0) {
            return 0;          /* not in the list, or named twice */
        }
        if (like == 0) {
            like = &in[k];
            gap = es_box_gap(like);
        }
        if (in[k].key == 0 || !es_same_set(&in[k], like)) {
            return 0;
        }
        mark[k] = (unsigned char)(ES_M_LISTED | (i < na ? ES_M_A : ES_M_B));
        list[i] = (unsigned char)k;
    }
    if (!es_marked_pos(in, vertical, gap, mark, list, na + nb, &pos)) {
        return 0;
    }
    /* one seam: every window reached from every other through links
     * among them, and two of them facing each other */
    for (i = 0; i < na; i++) {
        for (j = na; j < na + nb; j++) {
            int lo, hi;

            if (es_links(&in[list[i]].box, &in[list[j]].box, vertical,
                         &lo, &hi)) {
                es_join(parent, list[i], list[j]);
            }
        }
    }
    for (i = 0; i < na + nb; i++) {
        int r = es_root(parent, list[i]);

        if (root < 0) {
            root = r;
        } else if (r != root) {
            return 0;
        }
    }
    if (!es_marked_span(in, n, vertical, mark, 1, &from, &to) ||
        !es_marked_whole(in, n, vertical, like, gap, pos, mark,
                         list, na + nb)) {
        return 0;
    }
    es_marked_limits(in, n, vertical, mark, es_cell_floor(gap), gap,
                     &min_pos, &max_pos);
    es_marked_room(in, n, vertical, like, gap, pos, mark, list, na + nb,
                   &min_pos, &max_pos);
    if (out != 0) {
        es_seam_frame(out, vertical, pos, thickness_px, from, to,
                      min_pos, max_pos, gap, like);
        out->a.n = na;
        for (i = 0; i < na; i++) {
            out->a.ref[i] = a[i];
            out->a.box[i] = in[list[i]].box;
        }
        out->b.n = nb;
        for (i = 0; i < nb; i++) {
            out->b.ref[i] = b[i];
            out->b.box[i] = in[list[na + i]].box;
        }
    }
    return min_pos <= max_pos ? 1 : -1;
}

void es_seam_tighten_side(ESSeam *s, int side_b, int idx,
                          int min_size, int max_size)
{
    const ESSeamSide *side = side_b ? &s->b : &s->a;
    int gap = es_clamp_gap(s->gap);
    int lo, hi;

    if (idx < 0 || idx >= side->n) {
        return;
    }
    if (!side_b) {
        int lead = es_lead(&side->box[idx], s->vertical);

        lo = lead + min_size + (gap - gap / 2);
        hi = max_size > 0 ? lead + max_size + (gap - gap / 2) : 0x7FFF;
    } else {
        int trail = es_trail(&side->box[idx], s->vertical);

        hi = trail - min_size - gap / 2;
        lo = max_size > 0 ? trail - max_size - gap / 2 : -0x7FFF;
    }
    if (lo > s->min_pos) {
        s->min_pos = lo;
    }
    if (hi < s->max_pos) {
        s->max_pos = hi;
    }
}

int es_gutter_find_all(const ESRegistry *reg, int thickness_px,
                       ESSeam *out, int max)
{
    ESSeamBox box[ES_REGISTRY_SLOTS];
    int n = 0, total, i;

    if (max <= 0) {
        return 0;
    }
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        const ESRegistryEntry *e = &reg->slot[i];

        if (!e->used) {
            continue;
        }
        box[n].ref = e->ref;
        box[n].box = e->snapped;
        box[n].scope = 0;
        box[n].key = 0;
        box[n].gap = 0;
        n++;
    }
    total = es_seams_find(box, n, thickness_px, out, max);
    return total < max ? total : max;
}

int es_gutter_find(const ESRegistry *reg, int thickness_px, ESSeam *out)
{
    return es_gutter_find_all(reg, thickness_px, out, 1) == 1;
}

void es_gutter_apply(const ESSeam *seam, int new_pos,
                     ESRect *out_a, ESRect *out_b)
{
    int pos = new_pos;
    int gap = es_clamp_gap(seam->gap);
    int i;

    for (i = 0; i < seam->a.n; i++) {
        out_a[i] = seam->a.box[i];
    }
    for (i = 0; i < seam->b.n; i++) {
        out_b[i] = seam->b.box[i];
    }
    if (seam->min_pos > seam->max_pos) {
        return;                /* no room: nothing moves */
    }
    if (pos < seam->min_pos) {
        pos = seam->min_pos;
    }
    if (pos > seam->max_pos) {
        pos = seam->max_pos;
    }
    for (i = 0; i < seam->a.n; i++) {
        if (seam->vertical) {
            out_a[i].w = pos - (gap - gap / 2) - seam->a.box[i].x;
        } else {
            out_a[i].h = pos - (gap - gap / 2) - seam->a.box[i].y;
        }
    }
    for (i = 0; i < seam->b.n; i++) {
        int far = seam->vertical
            ? seam->b.box[i].x + seam->b.box[i].w
            : seam->b.box[i].y + seam->b.box[i].h;

        if (seam->vertical) {
            out_b[i].x = pos + gap / 2;
            out_b[i].w = far - out_b[i].x;
        } else {
            out_b[i].y = pos + gap / 2;
            out_b[i].h = far - out_b[i].y;
        }
    }
}

/* Which side of the screen does this zone occupy? */
static int es_zone_is_left(int z)
{
    return z == ES_ZONE_LEFT || z == ES_ZONE_TOP_LEFT ||
           z == ES_ZONE_BOTTOM_LEFT;
}

static int es_zone_is_right(int z)
{
    return z == ES_ZONE_RIGHT || z == ES_ZONE_TOP_RIGHT ||
           z == ES_ZONE_BOTTOM_RIGHT;
}

static int es_width_allowed(int w, int min_w, int max_w)
{
    if (min_w > 0 && w < min_w) {
        return 0;
    }
    if (max_w > 0 && w > max_w) {
        return 0;
    }
    return 1;
}

int es_pair_fill(const ESRegistry *reg, void *self, int zone,
                 const ESRect *usable, int min_w, int max_w, ESRect *rect)
{
    int want_left = es_zone_is_left(zone);
    int want_right = es_zone_is_right(zone);
    int i;

    if (!want_left && !want_right) {
        return 0;   /* maximise and the like: nothing to complement */
    }
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        const ESRegistryEntry *e = &reg->slot[i];
        int from, to;

        if (!e->used || e->ref == self) {
            continue;
        }
        /* the partner must be on the other side and share enough of
         * this rectangle's height to be the thing beside it */
        if (want_left && !es_zone_is_right(e->zone)) {
            continue;
        }
        if (want_right && !es_zone_is_left(e->zone)) {
            continue;
        }
        if (!es_faces(rect, &e->snapped, 1, &from, &to)) {
            continue;
        }
        if (want_left) {
            int edge = e->snapped.x;

            if (edge > rect->x + ES_SEAM_MIN_SIDE &&
                edge <= usable->x + usable->w &&
                es_width_allowed(edge - rect->x, min_w, max_w)) {
                rect->w = edge - rect->x;
                return 1;
            }
        } else {
            int edge = e->snapped.x + e->snapped.w;

            if (edge < usable->x + usable->w - ES_SEAM_MIN_SIDE &&
                edge >= usable->x &&
                es_width_allowed((rect->x + rect->w) - edge, min_w, max_w)) {
                rect->w = (rect->x + rect->w) - edge;
                rect->x = edge;
                return 1;
            }
        }
    }
    return 0;
}
