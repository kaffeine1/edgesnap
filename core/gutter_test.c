/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * gutter_test.c - executable specification of the divider geometry.
 */

#include <stdio.h>
#include <limits.h>

#include "gutter.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static ESRect rect(int x, int y, int w, int h)
{
    ESRect r;

    r.x = x;
    r.y = y;
    r.w = w;
    r.h = h;
    return r;
}

/* Two windows snapped to the left and right halves of a 1920x1047
 * usable area, the everyday case. */
static void halves(ESRegistry *reg, void *a, void *b)
{
    ESRect pre = rect(100, 100, 400, 300);
    ESRect left = rect(0, 33, 960, 1047);
    ESRect right = rect(960, 33, 960, 1047);

    es_registry_init(reg);
    es_registry_remember(reg, a, &pre, &left, ES_ZONE_LEFT);
    es_registry_remember(reg, b, &pre, &right, ES_ZONE_RIGHT);
}

static void test_finds_the_vertical_seam(void)
{
    ESRegistry reg;
    ESSeam seam;
    int wa, wb;

    halves(&reg, &wa, &wb);
    CHECK(es_gutter_find(&reg, 8, &seam) == 1);
    CHECK(seam.valid && seam.vertical);
    CHECK(seam.pos == 960);
    CHECK(seam.rect.x == 956 && seam.rect.w == 8);
    CHECK(seam.rect.y == 33 && seam.rect.h == 1047);
    CHECK(seam.a.n == 1 && seam.b.n == 1);
    CHECK(seam.a.ref[0] == (void *)&wa && seam.b.ref[0] == (void *)&wb);
}

static void test_drag_resizes_both(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect a, b;
    int wa, wb;

    halves(&reg, &wa, &wb);
    es_gutter_find(&reg, 8, &seam);

    /* drag the seam to 1200: left grows, right shrinks, no gap */
    es_gutter_apply(&seam, 1200, &a, &b);
    CHECK(a.x == 0 && a.w == 1200);
    CHECK(b.x == 1200 && b.w == 720);
    CHECK(a.x + a.w == b.x);
    CHECK(a.y == 33 && a.h == 1047 && b.h == 1047);
}

static void test_drag_is_clamped(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect a, b;
    int wa, wb;

    halves(&reg, &wa, &wb);
    es_gutter_find(&reg, 8, &seam);

    /* shoved far past the left edge: neither window may vanish */
    es_gutter_apply(&seam, -500, &a, &b);
    CHECK(a.w >= ES_SEAM_MIN_SIDE);
    CHECK(b.w >= ES_SEAM_MIN_SIDE);
    CHECK(a.x + a.w == b.x);

    es_gutter_apply(&seam, 99999, &a, &b);
    CHECK(a.w >= ES_SEAM_MIN_SIDE);
    CHECK(b.w >= ES_SEAM_MIN_SIDE);
    CHECK(a.x + a.w == b.x);
}

static void test_horizontal_seam(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect a, b;
    ESRect pre = rect(0, 0, 10, 10);
    ESRect top = rect(0, 33, 960, 520);
    ESRect bottom = rect(0, 553, 960, 527);
    int wa, wb;

    es_registry_init(&reg);
    es_registry_remember(&reg, &wa, &pre, &top, ES_ZONE_TOP_LEFT);
    es_registry_remember(&reg, &wb, &pre, &bottom, ES_ZONE_BOTTOM_LEFT);

    CHECK(es_gutter_find(&reg, 8, &seam) == 1);
    CHECK(seam.vertical == 0);
    CHECK(seam.pos == 553);
    CHECK(seam.rect.h == 8 && seam.rect.w == 960);

    es_gutter_apply(&seam, 700, &a, &b);
    CHECK(a.h == 667 && b.y == 700);
    CHECK(a.y + a.h == b.y);
}

static void test_no_seam_when_not_touching(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect pre = rect(0, 0, 10, 10);
    ESRect left = rect(0, 33, 800, 1047);
    ESRect far = rect(1200, 33, 720, 1047);
    int wa, wb;

    es_registry_init(&reg);
    es_registry_remember(&reg, &wa, &pre, &left, ES_ZONE_LEFT);
    es_registry_remember(&reg, &wb, &pre, &far, ES_ZONE_RIGHT);
    CHECK(es_gutter_find(&reg, 8, &seam) == 0);
}

static void test_no_seam_for_a_stacked_pair(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect pre = rect(0, 0, 10, 10);
    /* left half and a top-right quarter: they touch, but the shared
     * run is only half of the taller one - still enough by the rule,
     * so what must NOT match is a pair sharing almost nothing */
    ESRect left = rect(0, 33, 960, 1047);
    ESRect sliver = rect(960, 33, 960, 100);
    int wa, wb;

    es_registry_init(&reg);
    es_registry_remember(&reg, &wa, &pre, &left, ES_ZONE_LEFT);
    es_registry_remember(&reg, &wb, &pre, &sliver, ES_ZONE_TOP_RIGHT);
    /* the sliver shares 100px, which is 100% of ITS height: the rule
     * uses the shorter side, so this does match - and should, since
     * dragging that seam is meaningful for both. */
    CHECK(es_gutter_find(&reg, 8, &seam) == 1);
    CHECK(seam.rect.h == 100);
}

/* A window that cannot be as narrow as the complement keeps the box it
 * was fitted to: the registry must never record what Intuition will
 * refuse, or the seam beside it dies at birth. */
static void test_pair_fill_respects_the_window_limits(void)
{
    ESRegistry reg;
    ESRect usable = {0, 20, 1024, 673};
    ESRect left = {0, 20, 512, 673};
    ESRect want, before;
    int wa, wb;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &wa, &left, &left, ES_ZONE_LEFT) == ES_OK);

    /* fitted for a minimum width of 548: anchored at the right edge */
    want.x = 476; want.y = 20; want.w = 548; want.h = 673;
    before = want;
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_RIGHT, &usable, 548, 0, &want) == 0);
    CHECK(want.x == before.x && want.w == before.w);

    /* the same window with no limits takes the complement */
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_RIGHT, &usable, 0, 0, &want) == 1);
    CHECK(want.x == 512 && want.w == 512);

    /* a maximum width below the complement is respected the same way */
    want = before;
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_RIGHT, &usable, 0, 400, &want) == 0);
    CHECK(want.x == before.x && want.w == before.w);
}

static void test_single_window_has_no_seam(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect pre = rect(0, 0, 10, 10);
    ESRect left = rect(0, 33, 960, 1047);
    int wa;

    es_registry_init(&reg);
    es_registry_remember(&reg, &wa, &pre, &left, ES_ZONE_LEFT);
    CHECK(es_gutter_find(&reg, 8, &seam) == 0);
}

/* After the pair has been re-balanced, a third window snapped to the
 * narrow side must take exactly the space that is free. */
static void test_zone_fills_the_remaining_space(void)
{
    ESRegistry reg;
    ESRect pre = rect(0, 0, 10, 10);
    ESRect usable = rect(0, 33, 1920, 1047);
    /* the left window was widened to 1200 with the divider */
    ESRect wide_left = rect(0, 33, 1200, 1047);
    ESRect want;
    int wa, wb;

    es_registry_init(&reg);
    es_registry_remember(&reg, &wa, &pre, &wide_left, ES_ZONE_LEFT);

    /* a new window is snapped right: the default half would be 960 wide
     * starting at 960, but the free space starts at 1200 */
    want = rect(960, 33, 960, 1047);
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_RIGHT, &usable, 0, 0, &want) == 1);
    CHECK(want.x == 1200 && want.w == 720);
    CHECK(want.y == 33 && want.h == 1047);

    /* and the other way round: a narrow right window leaves a wide left */
    es_registry_init(&reg);
    {
        ESRect narrow_right = rect(1500, 33, 420, 1047);

        es_registry_remember(&reg, &wb, &pre, &narrow_right,
                             ES_ZONE_RIGHT);
    }
    want = rect(0, 33, 960, 1047);
    CHECK(es_pair_fill(&reg, &wa, ES_ZONE_LEFT, &usable, 0, 0, &want) == 1);
    CHECK(want.x == 0 && want.w == 1500);

    /* a window may not be its own partner */
    want = rect(0, 33, 960, 1047);
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_LEFT, &usable, 0, 0, &want) == 0);
    CHECK(want.w == 960);

    /* maximise has no opposite side to complement */
    want = rect(0, 33, 1920, 1047);
    CHECK(es_pair_fill(&reg, &wa, ES_ZONE_MAX, &usable, 0, 0, &want) == 0);
}

/*
 * One window filling the left half, faced by two stacked on the right.
 * The vertical seam belongs to all three: dragging it must move the
 * tall window and both of the short ones. This is the layout a user
 * asked for against 0.1, and the reason a seam is a line rather than a
 * pair.
 */
static void split_right(ESRegistry *reg, void *l, void *tr, void *br)
{
    ESRect pre = rect(100, 100, 400, 300);
    ESRect left = rect(0, 33, 960, 1047);
    ESRect top = rect(960, 33, 960, 523);
    ESRect bottom = rect(960, 556, 960, 524);

    es_registry_init(reg);
    es_registry_remember(reg, l, &pre, &left, ES_ZONE_LEFT);
    es_registry_remember(reg, tr, &pre, &top, ES_ZONE_TOP_RIGHT);
    es_registry_remember(reg, br, &pre, &bottom, ES_ZONE_BOTTOM_RIGHT);
}

static void test_one_window_faces_two(void)
{
    ESRegistry reg;
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int l, tr, br;
    int n, i, v = -1;

    split_right(&reg, &l, &tr, &br);
    n = es_gutter_find_all(&reg, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2);                       /* one vertical, one horizontal */
    for (i = 0; i < n; i++) {
        if (seams[i].vertical) {
            v = i;
        }
    }
    CHECK(v >= 0);
    if (v < 0) {
        return;
    }
    CHECK(seams[v].pos == 960);
    CHECK(seams[v].a.n == 1);            /* the tall one */
    CHECK(seams[v].b.n == 2);            /* both short ones */
    CHECK(seams[v].rect.y == 33 && seams[v].rect.h == 1047);

    es_gutter_apply(&seams[v], 1200, a, b);
    CHECK(a[0].x == 0 && a[0].w == 1200);
    CHECK(b[0].x == 1200 && b[0].w == 720);
    CHECK(b[1].x == 1200 && b[1].w == 720);
    /* heights untouched: a vertical seam moves nothing vertically */
    CHECK(b[0].h == 523 || b[0].h == 524);
    CHECK(b[1].h == 523 || b[1].h == 524);
}

/*
 * The horizontal seam in that layout exists only on the right: the tall
 * window's lower edge is at the bottom of the screen, not on that line,
 * so it must not be dragged by it.
 */
static void test_horizontal_seam_keeps_to_its_side(void)
{
    ESRegistry reg;
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int l, tr, br;
    int n, i, h = -1;

    split_right(&reg, &l, &tr, &br);
    n = es_gutter_find_all(&reg, 8, seams, ES_SEAM_MAX);
    for (i = 0; i < n; i++) {
        if (!seams[i].vertical) {
            h = i;
        }
    }
    CHECK(h >= 0);
    if (h < 0) {
        return;
    }
    CHECK(seams[h].pos == 556);
    CHECK(seams[h].a.n == 1 && seams[h].b.n == 1);
    CHECK(seams[h].a.ref[0] == (void *)&tr);
    CHECK(seams[h].b.ref[0] == (void *)&br);
    /* the strip stays in the right half */
    CHECK(seams[h].rect.x == 960 && seams[h].rect.w == 960);

    es_gutter_apply(&seams[h], 700, a, b);
    CHECK(a[0].y == 33 && a[0].h == 667);
    CHECK(b[0].y == 700 && b[0].h == 380);
}

/* Four quadrants: the middle vertical line moves all four, and so does
 * the horizontal one, because both boundaries run the whole way. */
static void test_four_quadrants(void)
{
    ESRegistry reg;
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    ESRect pre = rect(100, 100, 400, 300);
    ESRect tl = rect(0, 33, 960, 523);
    ESRect tr = rect(960, 33, 960, 523);
    ESRect bl = rect(0, 556, 960, 524);
    ESRect br = rect(960, 556, 960, 524);
    int wtl, wtr, wbl, wbr;
    int n, i, v = -1, h = -1;

    es_registry_init(&reg);
    es_registry_remember(&reg, &wtl, &pre, &tl, ES_ZONE_TOP_LEFT);
    es_registry_remember(&reg, &wtr, &pre, &tr, ES_ZONE_TOP_RIGHT);
    es_registry_remember(&reg, &wbl, &pre, &bl, ES_ZONE_BOTTOM_LEFT);
    es_registry_remember(&reg, &wbr, &pre, &br, ES_ZONE_BOTTOM_RIGHT);

    n = es_gutter_find_all(&reg, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2);
    for (i = 0; i < n; i++) {
        if (seams[i].vertical) {
            v = i;
        } else {
            h = i;
        }
    }
    CHECK(v >= 0 && h >= 0);
    if (v < 0 || h < 0) {
        return;
    }
    CHECK(seams[v].a.n == 2 && seams[v].b.n == 2);
    CHECK(seams[h].a.n == 2 && seams[h].b.n == 2);
    /* the vertical seam runs the whole height even though no single
     * window does: the two stacked pairs face each other all the way */
    CHECK(seams[v].rect.y == 33 && seams[v].rect.h == 1047);

    es_gutter_apply(&seams[v], 1200, a, b);
    CHECK(a[0].w == 1200 && a[1].w == 1200);
    CHECK(b[0].x == 1200 && b[0].w == 720);
    CHECK(b[1].x == 1200 && b[1].w == 720);
}

/*
 * A drag is not one move but a stream of them, and after each one the
 * seam is somewhere else. Walking it across the screen must keep
 * finding it: naming a seam by where it STARTED made the drag work for
 * a dozen pixels and then stop dead, which is what a user saw.
 */
static void test_seam_can_be_walked_across_the_screen(void)
{
    ESRegistry reg;
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    ESRect pre = rect(100, 100, 400, 300);
    int wa, wb;
    int target;

    halves(&reg, &wa, &wb);
    for (target = 1000; target <= 1400; target += 100) {
        int n = es_gutter_find_all(&reg, 8, seams, ES_SEAM_MAX);
        int i, v = -1;

        for (i = 0; i < n; i++) {
            if (seams[i].vertical) {
                v = i;
            }
        }
        CHECK(v >= 0);
        if (v < 0) {
            return;
        }
        es_gutter_apply(&seams[v], target, a, b);
        /* the registry follows, as the library makes it do */
        es_registry_remember(&reg, &wa, &pre, &a[0], ES_ZONE_LEFT);
        es_registry_remember(&reg, &wb, &pre, &b[0], ES_ZONE_RIGHT);
        CHECK(a[0].w == target);
    }
    /* and it is still there at the end, at the last position asked */
    {
        int n = es_gutter_find_all(&reg, 8, seams, ES_SEAM_MAX);
        CHECK(n >= 1);
        CHECK(seams[0].pos == 1400);
    }
}

/* --------------------------------- 2.24: lists of boxes, and groups */

static ESSeamBox seam_box(void *ref, ESRect r, unsigned long key, int gap)
{
    ESSeamBox b;

    b.ref = ref;
    b.box = r;
    b.scope = 0;
    b.key = key;
    b.gap = gap;
    return b;
}

/* A cell of a tiler's layout on the usable area of halves(), made into
 * a window box the way ES_PF_CELL makes it. */
static ESRect cell(int x, int y, int w, int h, int gap)
{
    ESRect c = rect(x, y, w, h);
    ESRect usable = rect(0, 33, 1920, 1047);
    ESRect out;
    int margin[4] = {0, 0, 0, 0};

    es_cell_rect(&c, &usable, gap, margin, &out, 0);
    return out;
}

static int same_rect(const ESRect *a, const ESRect *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static int count_axis(const ESSeam *seams, int n, int vertical)
{
    int i, c = 0;

    for (i = 0; i < n; i++) {
        if (seams[i].vertical == vertical) {
            c++;
        }
    }
    return c;
}

/* Is ref on that side of the seam? */
static int on_side(const ESSeamSide *side, const void *ref)
{
    int i;

    for (i = 0; i < side->n; i++) {
        if (side->ref[i] == ref) {
            return 1;
        }
    }
    return 0;
}

/* Drag the seam to pos and put its windows where that leaves them. */
static void drag_seam(const ESSeam *s, int pos, ESSeamBox *in, int n)
{
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int i, k;

    es_gutter_apply(s, pos, a, b);
    for (i = 0; i < n; i++) {
        for (k = 0; k < s->a.n; k++) {
            if (in[i].ref == s->a.ref[k]) {
                in[i].box = a[k];
            }
        }
        for (k = 0; k < s->b.n; k++) {
            if (in[i].ref == s->b.ref[k]) {
                in[i].box = b[k];
            }
        }
    }
}

static int boxes_overlap(const ESRect *p, const ESRect *q)
{
    return p->x < q->x + q->w && q->x < p->x + p->w &&
           p->y < q->y + q->h && q->y < p->y + p->h;
}

/* How many pairs of the boxes overlap? */
static int overlaps(const ESSeamBox *in, int n)
{
    int i, j, c = 0;

    for (i = 0; i < n; i++) {
        for (j = i + 1; j < n; j++) {
            c += boxes_overlap(&in[i].box, &in[j].box);
        }
    }
    return c;
}

/*
 * A side that would need more than ES_SEAM_SIDE_MAX windows gives no
 * seam at all: before 2.24 the ninth window was dropped, and dragging
 * the seam pushed its neighbours into it. A side of nine is no longer
 * cut short.
 */
static void test_side_overflow_gives_no_seam(void)
{
    ESSeamBox in[ES_SEAM_SIDE_MAX + 2];
    ESSeam seams[ES_SEAM_MAX];
    ESRegistry reg;
    ESRect ra[ES_SEAM_SIDE_MAX], rb[ES_SEAM_SIDE_MAX];
    ESRect pre = rect(0, 0, 10, 10);
    int refs[ES_SEAM_SIDE_MAX + 2];
    int k, n;

    for (k = 0; k <= ES_SEAM_SIDE_MAX; k++) {
        in[k] = seam_box(&refs[k], rect(0, 33 + k * 31, 960, 31), 0, 0);
    }
    in[k] = seam_box(&refs[k], rect(960, 33, 960, 1047), 0, 0);
    n = es_seams_find(in, ES_SEAM_SIDE_MAX + 2, 8, seams, ES_SEAM_MAX);
    CHECK(count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 1) == 0);

    /* one fewer is a seam that moves every one of them */
    in[ES_SEAM_SIDE_MAX] = in[ES_SEAM_SIDE_MAX + 1];
    n = es_seams_find(in, ES_SEAM_SIDE_MAX + 1, 8, seams, ES_SEAM_MAX);
    CHECK(n >= 1 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == ES_SEAM_SIDE_MAX && seams[0].b.n == 1);

    /* nine on a side, through the registry */
    es_registry_init(&reg);
    for (k = 0; k < 9; k++) {
        ESRect r = rect(0, 33 + k * 116, 960, 116);

        es_registry_remember(&reg, &refs[k], &pre, &r, ES_ZONE_LEFT);
    }
    {
        ESRect right = rect(960, 33, 960, 1047);

        es_registry_remember(&reg, &refs[9], &pre, &right, ES_ZONE_RIGHT);
    }
    CHECK(es_gutter_find(&reg, 8, &seams[0]) == 1);
    CHECK(seams[0].vertical && seams[0].a.n == 9 && seams[0].b.n == 1);
    es_gutter_apply(&seams[0], 1200, ra, rb);
    for (k = 0; k < 9; k++) {
        CHECK(ra[k].x == 0 && ra[k].w == 1200);
    }
    CHECK(rb[0].x == 1200 && rb[0].w == 720);

    /* a group's seam keeps to the same bound */
    for (k = 0; k <= ES_SEAM_SIDE_MAX; k++) {
        in[k] = seam_box(&refs[k], rect(0, 33 + k * 31, 950, 31), 7, 20);
    }
    in[k] = seam_box(&refs[k], cell(960, 33, 960, 1047, 20), 7, 20);
    n = es_seams_find(in, ES_SEAM_SIDE_MAX + 2, 8, seams, ES_SEAM_MAX);
    CHECK(count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 1) == 0);
    in[ES_SEAM_SIDE_MAX] = in[ES_SEAM_SIDE_MAX + 1];
    n = es_seams_find(in, ES_SEAM_SIDE_MAX + 1, 8, seams, ES_SEAM_MAX);
    CHECK(n >= 1 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == ES_SEAM_SIDE_MAX && seams[0].b.n == 1);
}

/*
 * A refused line stays refused: a candidate a few pixels away must not
 * offer the same windows again with the one that made the side too
 * long left out (review finding P6: here ES_SEAM_SIDE_MAX windows end
 * at 960 and one more at 950, while the right side starts at 960 and
 * 963).
 */
static void test_refused_line_is_not_tried_again(void)
{
    ESSeamBox in[ES_SEAM_SIDE_MAX + 3];
    ESSeam seams[ES_SEAM_MAX];
    int refs[ES_SEAM_SIDE_MAX + 3];
    int k, n;

    for (k = 0; k < ES_SEAM_SIDE_MAX; k++) {
        in[k] = seam_box(&refs[k], rect(0, 33 + k * 31, 960, 31), 0, 0);
    }
    in[k] = seam_box(&refs[k], rect(0, 33 + k * 31, 950, 31), 0, 0);
    k++;
    in[k] = seam_box(&refs[k], rect(960, 33, 960, 600), 0, 0);
    k++;
    in[k] = seam_box(&refs[k], rect(963, 633, 957, 447), 0, 0);
    k++;
    n = es_seams_find(in, k, 8, seams, ES_SEAM_MAX);
    CHECK(count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 1) == 0);
}

/*
 * The halves of a group with a gap of 20: one seam, at the boundary
 * the cells were cut at, and walking it across the screen keeps the gap
 * and both far edges, step after step.
 */
static void test_grouped_halves_keep_their_gap(void)
{
    ESSeamBox in[2];
    ESSeam seams[ES_SEAM_MAX];
    ESRect a, b;
    int wa, wb;
    int target, n, last = 960;

    in[0] = seam_box(&wa, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&wb, cell(960, 33, 960, 1047, 20), 7, 20);
    CHECK(in[0].box.w == 950 && in[1].box.x == 970);

    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1);
    if (n != 1) {
        return;
    }
    CHECK(seams[0].valid && seams[0].vertical);
    CHECK(seams[0].pos == 960 && seams[0].gap == 20 && seams[0].key == 7);
    CHECK(seams[0].scope == 0);
    /* the strip is the gap, 950 to 970, not 8 pixels in its middle */
    CHECK(seams[0].rect.x == 950 && seams[0].rect.w == 20);
    CHECK(seams[0].rect.y == 33 && seams[0].rect.h == 1047);
    CHECK(seams[0].a.n == 1 && seams[0].b.n == 1);
    CHECK(seams[0].a.ref[0] == (void *)&wa &&
          seams[0].b.ref[0] == (void *)&wb);
    /* a window of a group keeps one pixel; its own limits come later */
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1909);

    for (target = 1000; target <= 1360; target += 40) {
        ESRect want_a = cell(0, 33, target, 1047, 20);
        ESRect want_b = cell(target, 33, 1920 - target, 1047, 20);

        n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
        CHECK(n == 1);
        if (n != 1) {
            return;
        }
        CHECK(seams[0].pos == last);
        es_gutter_apply(&seams[0], target, &a, &b);
        CHECK(same_rect(&a, &want_a) && same_rect(&b, &want_b));
        CHECK(a.x == 0 && b.x + b.w == 1920);
        CHECK(b.x - (a.x + a.w) == 20);
        in[0].box = a;
        in[1].box = b;
        last = target;
    }
    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].pos == 1360);
}

/*
 * The strip of a seam with a gap wider than the thickness asked for is
 * the gap itself, from where side a ends to where side b begins, so a
 * handle put on it fills the space between the two windows. A gap no
 * wider than the thickness keeps the strip centred on the position, as
 * a seam of windows that touch has it; and so does key 0, gap or not.
 */
static void test_grouped_strip_is_the_gap(void)
{
    static const int gaps[6] = {0, 6, 8, 9, 20, 41};
    ESSeamBox in[2];
    ESSeam seams[ES_SEAM_MAX], built;
    void *a[1], *b[1];
    int wa, wb;
    int i, n, g, trail, lead;

    a[0] = &wa;
    b[0] = &wb;
    for (i = 0; i < 6; i++) {
        g = gaps[i];
        in[0] = seam_box(&wa, cell(0, 33, 960, 1047, g), 7, g);
        in[1] = seam_box(&wb, cell(960, 33, 960, 1047, g), 7, g);
        trail = in[0].box.x + in[0].box.w;
        lead = in[1].box.x;
        CHECK(lead - trail == g);
        n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
        CHECK(n == 1);
        if (n != 1) {
            continue;
        }
        CHECK(seams[0].pos == 960 && seams[0].rect.y == 33 &&
              seams[0].rect.h == 1047);
        if (g > 8) {
            CHECK(seams[0].rect.x == trail && seams[0].rect.w == g);
            CHECK(seams[0].rect.x + seams[0].rect.w == lead);
        } else {
            CHECK(seams[0].rect.x == 956 && seams[0].rect.w == 8);
        }
        /* a token's seam, rebuilt from its windows, has the same strip */
        CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 1);
        CHECK(same_rect(&built.rect, &seams[0].rect));
        /* a thickness wider than the gap wins */
        n = es_seams_find(in, 2, 50, seams, ES_SEAM_MAX);
        CHECK(n == 1 && seams[0].rect.x == 935 && seams[0].rect.w == 50);
    }

    /* across: the gap of a stack, 556 cut with a gap of 11 */
    in[0] = seam_box(&wa, cell(0, 33, 960, 523, 11), 7, 11);
    in[1] = seam_box(&wb, cell(0, 556, 960, 524, 11), 7, 11);
    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1);
    if (n == 1) {
        CHECK(!seams[0].vertical && seams[0].pos == 556);
        CHECK(seams[0].rect.y == in[0].box.y + in[0].box.h);
        CHECK(seams[0].rect.y + seams[0].rect.h == in[1].box.y);
        CHECK(seams[0].rect.x == 0 && seams[0].rect.w == 954);
    }

    /* key 0 reads every gap as none: the strip stays 8 wide */
    in[0] = seam_box(&wa, rect(0, 33, 960, 1047), 0, 20);
    in[1] = seam_box(&wb, rect(960, 33, 960, 1047), 0, 20);
    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].rect.x == 956 && seams[0].rect.w == 8);
}

/*
 * Four quadrants of a group with an odd gap. Grouped windows pair by
 * connection, and no window spans both rows or both columns, so there
 * are four seams: the top pair and the bottom pair down the middle, the
 * left pair and the right pair across. Each sits on the boundary
 * es_cell_rect cut, half a pixel right of (below) the middle of the
 * gap, and keeps the gap when dragged.
 */
static void test_grouped_quadrants_with_an_odd_gap(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int wtl, wtr, wbl, wbr;
    int n;

    in[0] = seam_box(&wtl, cell(0, 33, 960, 523, 11), 7, 11);
    in[1] = seam_box(&wtr, cell(960, 33, 960, 523, 11), 7, 11);
    in[2] = seam_box(&wbl, cell(0, 556, 960, 524, 11), 7, 11);
    in[3] = seam_box(&wbr, cell(960, 556, 960, 524, 11), 7, 11);
    CHECK(in[0].box.w == 954 && in[1].box.x == 965);
    CHECK(in[0].box.h == 517 && in[2].box.y == 561);

    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 4);
    if (n != 4) {
        return;
    }
    /* vertical first, each line's seams in the order they start */
    CHECK(seams[0].vertical && seams[0].pos == 960 && seams[0].gap == 11);
    CHECK(seams[0].a.n == 1 && seams[0].a.ref[0] == (void *)&wtl);
    CHECK(seams[0].b.n == 1 && seams[0].b.ref[0] == (void *)&wtr);
    CHECK(seams[0].rect.y == 33 && seams[0].rect.h == 517);
    CHECK(seams[1].vertical && seams[1].pos == 960);
    CHECK(seams[1].a.ref[0] == (void *)&wbl &&
          seams[1].b.ref[0] == (void *)&wbr);
    CHECK(seams[1].rect.y == 561 && seams[1].rect.h == 519);
    CHECK(!seams[2].vertical && seams[2].pos == 556);
    CHECK(seams[2].a.ref[0] == (void *)&wtl &&
          seams[2].b.ref[0] == (void *)&wbl);
    CHECK(seams[2].rect.x == 0 && seams[2].rect.w == 954);
    CHECK(seams[2].rect.y == 550 && seams[2].rect.h == 11);
    CHECK(!seams[3].vertical && seams[3].pos == 556);
    CHECK(seams[3].a.ref[0] == (void *)&wtr &&
          seams[3].b.ref[0] == (void *)&wbr);
    CHECK(seams[3].rect.x == 965 && seams[3].rect.w == 955);

    es_gutter_apply(&seams[0], 1203, a, b);
    CHECK(a[0].x == 0 && a[0].w == 1197);
    CHECK(b[0].x == 1208 && b[0].x + b[0].w == 1920);
    CHECK(b[0].x - (a[0].x + a[0].w) == 11);
    CHECK(a[0].h == 517 && b[0].h == 517);

    es_gutter_apply(&seams[2], 700, a, b);
    CHECK(a[0].y == 33 && a[0].h == 661);
    CHECK(b[0].y == 705 && b[0].y + b[0].h == 1080);
    CHECK(b[0].y - (a[0].y + a[0].h) == 11);
}

/* Master and stack: the master faces all three, so the boundary is one
 * seam with sides of 1 and 3; the stack has a seam between each pair. */
static void test_grouped_master_and_stack(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int m, s1, s2, s3;
    int n, i;

    in[0] = seam_box(&m, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(960, 33, 960, 349, 20), 7, 20);
    in[2] = seam_box(&s2, cell(960, 382, 960, 349, 20), 7, 20);
    in[3] = seam_box(&s3, cell(960, 731, 960, 349, 20), 7, 20);

    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3);
    if (n != 3) {
        return;
    }
    CHECK(seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 1 && seams[0].a.ref[0] == (void *)&m);
    CHECK(seams[0].b.n == 3);
    CHECK(seams[0].b.ref[0] == (void *)&s1 &&
          seams[0].b.ref[1] == (void *)&s2 &&
          seams[0].b.ref[2] == (void *)&s3);
    CHECK(seams[0].rect.y == 33 && seams[0].rect.h == 1047);
    CHECK(!seams[1].vertical && seams[1].pos == 382);
    CHECK(seams[1].a.ref[0] == (void *)&s1 &&
          seams[1].b.ref[0] == (void *)&s2);
    CHECK(!seams[2].vertical && seams[2].pos == 731);

    es_gutter_apply(&seams[0], 700, a, b);
    CHECK(a[0].x == 0 && a[0].w == 690);
    for (i = 0; i < 3; i++) {
        CHECK(b[i].x == 710 && b[i].x + b[i].w == 1920);
        CHECK(b[i].y == in[i + 1].box.y && b[i].h == in[i + 1].box.h);
    }
}

/*
 * Three columns, the outer two split at the same height and the middle
 * one whole. Before 2.24 that was one seam right across the screen,
 * moving both splits at once; in a group they are two seams.
 */
static void test_grouped_splits_in_two_columns(void)
{
    ESSeamBox in[5];
    ESSeam seams[ES_SEAM_MAX];
    int at, ab, bm, ct, cb;
    int n, i, h = 0;

    in[0] = seam_box(&at, cell(0, 33, 640, 523, 20), 7, 20);
    in[1] = seam_box(&ab, cell(0, 556, 640, 524, 20), 7, 20);
    in[2] = seam_box(&bm, cell(640, 33, 640, 1047, 20), 7, 20);
    in[3] = seam_box(&ct, cell(1280, 33, 640, 523, 20), 7, 20);
    in[4] = seam_box(&cb, cell(1280, 556, 640, 524, 20), 7, 20);

    n = es_seams_find(in, 5, 8, seams, ES_SEAM_MAX);
    CHECK(n == 4);
    CHECK(count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 1) == 2 &&
          count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 0) == 2);
    for (i = 0; i < n && i < ES_SEAM_MAX; i++) {
        if (seams[i].vertical) {
            continue;
        }
        CHECK(seams[i].pos == 556 && seams[i].a.n == 1 && seams[i].b.n == 1);
        if (h == 0) {
            CHECK(seams[i].a.ref[0] == (void *)&at &&
                  seams[i].b.ref[0] == (void *)&ab);
            CHECK(seams[i].rect.x == 0 && seams[i].rect.w == 630);
        } else {
            CHECK(seams[i].a.ref[0] == (void *)&ct &&
                  seams[i].b.ref[0] == (void *)&cb);
            CHECK(seams[i].rect.x == 1290 && seams[i].rect.w == 630);
        }
        h++;
    }
}

/* Two columns split 10 px apart are two seams, each at its own height:
 * one candidate line offers both, and neither drags the other along. */
static void test_grouped_neighbouring_splits(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX];
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int tl, tr, bl, br;
    int n, i, found = 0;

    in[0] = seam_box(&tl, cell(0, 33, 960, 507, 20), 7, 20);
    in[1] = seam_box(&tr, cell(960, 33, 960, 517, 20), 7, 20);
    in[2] = seam_box(&bl, cell(0, 540, 960, 540, 20), 7, 20);
    in[3] = seam_box(&br, cell(960, 550, 960, 530, 20), 7, 20);

    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 0) == 2);
    for (i = 0; i < n && i < ES_SEAM_MAX; i++) {
        if (seams[i].vertical) {
            continue;
        }
        CHECK(seams[i].a.n == 1 && seams[i].b.n == 1);
        if (seams[i].pos == 540) {
            CHECK(seams[i].a.ref[0] == (void *)&tl &&
                  seams[i].b.ref[0] == (void *)&bl);
            found |= 1;
        } else if (seams[i].pos == 550) {
            CHECK(seams[i].a.ref[0] == (void *)&tr &&
                  seams[i].b.ref[0] == (void *)&br);
            es_gutter_apply(&seams[i], 600, a, b);
            CHECK(a[0].y == 33 && a[0].h == 557 && b[0].y == 610);
            CHECK(b[0].y + b[0].h == 1080);
            found |= 2;
        }
    }
    CHECK(found == 3);
}

/* Boxes pair only within one scope and one key. The same halves on two
 * screens are two seams with the same position and strip, which the
 * library has to tell apart by scope. */
static void test_keys_and_scopes_keep_apart(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX];
    int s1, s2;
    int w[4];
    int n;

    /* an ungrouped window touching a member: no seam */
    in[0] = seam_box(&w[0], rect(0, 33, 960, 1047), 0, 0);
    in[1] = seam_box(&w[1], rect(960, 33, 960, 1047), 7, 0);
    CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 0);
    in[1].key = 8;
    in[0].key = 7;
    CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 0);

    /* one key, two screens: no seam, grouped or not */
    in[0] = seam_box(&w[0], cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(960, 33, 960, 1047, 20), 7, 20);
    in[0].scope = &s1;
    in[1].scope = &s2;
    CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 0);
    in[0] = seam_box(&w[0], rect(0, 33, 960, 1047), 0, 0);
    in[1] = seam_box(&w[1], rect(960, 33, 960, 1047), 0, 0);
    in[0].scope = &s1;
    in[1].scope = &s2;
    CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 0);

    /* halves on each of two screens */
    in[1].scope = &s1;
    in[2] = seam_box(&w[2], rect(0, 33, 960, 1047), 0, 0);
    in[3] = seam_box(&w[3], rect(960, 33, 960, 1047), 0, 0);
    in[2].scope = &s2;
    in[3].scope = &s2;
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2);
    if (n == 2) {
        CHECK(seams[0].pos == seams[1].pos &&
              same_rect(&seams[0].rect, &seams[1].rect));
        CHECK(seams[0].scope == (const void *)&s1 &&
              seams[1].scope == (const void *)&s2);
        CHECK(seams[0].a.ref[0] == (void *)&w[0] &&
              seams[1].a.ref[0] == (void *)&w[2]);
    }
}

/*
 * Windows es_fit_rect moved off their cells (review finding 1): A's
 * MinWidth of 957 pushes its right edge 7 px into the gap, B2's MinWidth
 * of 956 pulls its left edge 6 px to the left, B1 is exact. A implies
 * the boundary 967, B1 960 and B2 954: no two of them agree, but 960 is
 * within the tolerance of all three, so that is where the seam is, in
 * every order of the list, and es_seam_by_refs agrees. With A on its
 * cell it is the same seam.
 */
static void test_fitted_boxes_agree_in_every_order(void)
{
    static const int order[6][3] = {
        {0, 1, 2}, {0, 2, 1}, {1, 0, 2}, {1, 2, 0}, {2, 0, 1}, {2, 1, 0}
    };
    ESRect usable = rect(0, 33, 1920, 1047);
    ESRect area, c, cb2, box[3], exact_a;
    ESSeamBox in[3];
    ESSeam seams[ES_SEAM_MAX], built;
    int margin[4] = {0, 0, 0, 0};
    int wa, wb1, wb2;
    void *refs[3];
    void *a[1], *b[2];
    int variant, p, n, i;

    c = rect(0, 33, 960, 1047);
    es_cell_rect(&c, &usable, 20, margin, &exact_a, &area);
    es_fit_rect(&exact_a, &area, 957, 0, 0, 0, &box[0]);
    c = rect(960, 33, 960, 523);
    es_cell_rect(&c, &usable, 20, margin, &box[1], 0);
    c = rect(960, 556, 960, 524);
    es_cell_rect(&c, &usable, 20, margin, &cb2, 0);
    es_fit_rect(&cb2, &area, 956, 0, 0, 0, &box[2]);
    CHECK(box[0].x + box[0].w == 957);
    CHECK(box[1].x == 970 && box[2].x == 964);
    refs[0] = &wa;
    refs[1] = &wb1;
    refs[2] = &wb2;

    for (variant = 0; variant < 2; variant++) {
        if (variant == 1) {
            box[0] = exact_a;
        }
        for (p = 0; p < 6; p++) {
            int v = -1, h = -1;

            for (i = 0; i < 3; i++) {
                in[i] = seam_box(refs[order[p][i]], box[order[p][i]], 7, 20);
            }
            n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
            CHECK(n == 2);
            for (i = 0; i < n && i < ES_SEAM_MAX; i++) {
                if (seams[i].vertical) {
                    v = i;
                } else {
                    h = i;
                }
            }
            CHECK(h >= 0 && seams[h].pos == 556);
            CHECK(h >= 0 && seams[h].a.ref[0] == (void *)&wb1);
            CHECK(h >= 0 && seams[h].b.ref[0] == (void *)&wb2);
            CHECK(v >= 0);
            if (v < 0) {
                continue;
            }
            CHECK(seams[v].pos == 960 && seams[v].rect.x == 950);
            CHECK(seams[v].min_pos == 11 && seams[v].max_pos == 1909);
            CHECK(seams[v].a.n == 1 && seams[v].a.ref[0] == (void *)&wa);
            CHECK(seams[v].b.n == 2);
            CHECK(on_side(&seams[v].b, &wb1) && on_side(&seams[v].b, &wb2));

            a[0] = &wa;
            b[0] = (p & 1) ? &wb2 : &wb1;
            b[1] = (p & 1) ? &wb1 : &wb2;
            CHECK(es_seam_by_refs(in, 3, 8, 1, a, 1, b, 2, &built) == 1);
            CHECK(built.pos == 960 && same_rect(&built.rect, &seams[v].rect));
            CHECK(built.min_pos == seams[v].min_pos &&
                  built.max_pos == seams[v].max_pos);
            CHECK(built.b.ref[0] == b[0] && built.b.ref[1] == b[1]);
            /* nor either half of it: B1 alone leaves out B2, which
             * faces A across the same boundary; B2 alone leaves out B1
             * (A on its cell) or has no boundary in common with A */
            b[0] = &wb1;
            CHECK(es_seam_by_refs(in, 3, 8, 1, a, 1, b, 1, &built) == 0);
            b[0] = &wb2;
            CHECK(es_seam_by_refs(in, 3, 8, 1, a, 1, b, 1, &built) == 0);
        }
    }
}

/* A group's seam rebuilt from its windows, with the sides in the order
 * the caller gave them. */
static void test_by_refs_builds_the_seam(void)
{
    ESSeamBox in[4];
    ESSeam found, built;
    void *a[1], *b[3];
    int m, s1, s2, s3;

    in[0] = seam_box(&m, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(960, 33, 960, 1047, 20), 7, 20);
    CHECK(es_seams_find(in, 2, 8, &found, 1) == 1);
    a[0] = &m;
    b[0] = &s1;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 1);
    CHECK(built.valid && built.vertical && built.pos == 960);
    CHECK(built.gap == 20 && built.key == 7 && built.scope == 0);
    CHECK(same_rect(&built.rect, &found.rect));
    CHECK(built.min_pos == 11 && built.max_pos == 1909);
    CHECK(built.a.n == 1 && built.a.ref[0] == (void *)&m);
    CHECK(built.b.n == 1 && built.b.ref[0] == (void *)&s1);
    CHECK(same_rect(&built.b.box[0], &in[1].box));
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, 0) == 1);
    /* the wrong axis, and the sides swapped, are not this seam */
    CHECK(es_seam_by_refs(in, 2, 8, 0, a, 1, b, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 2, 8, 1, b, 1, a, 1, &built) == 0);

    in[1] = seam_box(&s1, cell(960, 33, 960, 349, 20), 7, 20);
    in[2] = seam_box(&s2, cell(960, 382, 960, 349, 20), 7, 20);
    in[3] = seam_box(&s3, cell(960, 731, 960, 349, 20), 7, 20);
    b[0] = &s3;
    b[1] = &s1;
    b[2] = &s2;
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b, 3, &built) == 1);
    CHECK(built.pos == 960 && built.b.n == 3);
    CHECK(built.b.ref[0] == (void *)&s3 &&
          same_rect(&built.b.box[0], &in[3].box));
    CHECK(built.b.ref[1] == (void *)&s1 &&
          same_rect(&built.b.box[1], &in[1].box));
    CHECK(built.b.ref[2] == (void *)&s2 &&
          same_rect(&built.b.box[2], &in[2].box));
    CHECK(built.rect.y == 33 && built.rect.h == 1047);
}

/* Another row on the same line that is linked to neither window is
 * not the seam's business: the token still names the same seam. */
static void test_by_refs_ignores_an_unrelated_row(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX], built;
    void *a[1], *b[1];
    int tl, tr, bl, br;

    in[0] = seam_box(&tl, cell(0, 33, 960, 523, 20), 7, 20);
    in[1] = seam_box(&tr, cell(960, 33, 960, 523, 20), 7, 20);
    a[0] = &tl;
    b[0] = &tr;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 1);

    in[2] = seam_box(&bl, cell(0, 556, 960, 524, 20), 7, 20);
    in[3] = seam_box(&br, cell(960, 556, 960, 524, 20), 7, 20);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b, 1, &built) == 1);
    CHECK(built.pos == 960 && built.rect.y == 33 && built.rect.h == 513);
    CHECK(es_seams_find(in, 4, 8, seams, ES_SEAM_MAX) == 4);
    CHECK(count_axis(seams, 4, 1) == 2);
}

/* A window that joined the seam, linked to one of the token's windows,
 * makes the token's set wrong: the old set is no longer the seam, the
 * new one is. That holds on either side. */
static void test_by_refs_refuses_a_window_that_joined(void)
{
    ESSeamBox in[4];
    ESSeam built;
    void *a[3], *b[3];
    int m, s1, s2, s3;

    in[0] = seam_box(&m, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&s2, cell(960, 556, 960, 524, 20), 7, 20);
    a[0] = &m;
    b[0] = &s1;
    b[1] = &s2;
    b[2] = &s3;
    CHECK(es_seam_by_refs(in, 3, 8, 1, a, 1, b, 2, &built) == 1);

    /* the stack grows to three */
    in[1].box = cell(960, 33, 960, 349, 20);
    in[2].box = cell(960, 382, 960, 349, 20);
    in[3] = seam_box(&s3, cell(960, 731, 960, 349, 20), 7, 20);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b, 2, &built) == 0);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b, 3, &built) == 1);
    /* and a window that left the line takes the seam with it */
    in[3].box.x += 40;
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b, 3, &built) == 0);

    /* the mirror: master on the right, the stack joined on side a */
    in[0] = seam_box(&m, cell(960, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(0, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&s2, cell(0, 556, 960, 524, 20), 7, 20);
    a[0] = &s1;
    a[1] = &s2;
    a[2] = &s3;
    b[0] = &m;
    CHECK(es_seam_by_refs(in, 3, 8, 1, a, 2, b, 1, &built) == 1);
    in[1].box = cell(0, 33, 960, 349, 20);
    in[2].box = cell(0, 382, 960, 349, 20);
    in[3] = seam_box(&s3, cell(0, 731, 960, 349, 20), 7, 20);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 2, b, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 3, b, 1, &built) == 1);
    CHECK(built.pos == 960 && built.a.n == 3 && built.b.n == 1);
}

/*
 * No room is not "not facing". The halves of a 170 px area at a gap of
 * 20 still have room in the core, where a group's window keeps one
 * pixel; a MinWidth of 80 on both (the library's tightening) leaves
 * none, which the library reports as stuck. Cells of 1 px, the least
 * es_cell_rect leaves, have no room even in the core: es_seam_by_refs
 * answers -1 with the seam built, es_seams_find still reports it, and
 * es_gutter_apply leaves both windows where they are.
 */
static void test_by_refs_reports_no_room(void)
{
    ESRect usable = rect(0, 33, 170, 400);
    ESRect c, moved[2];
    ESSeamBox in[2];
    ESSeam seams[ES_SEAM_MAX], built;
    int margin[4] = {0, 0, 0, 0};
    void *a[1], *b[1];
    int wa, wb, n;

    c = rect(0, 33, 85, 400);
    es_cell_rect(&c, &usable, 20, margin, &in[0].box, 0);
    c = rect(85, 33, 85, 400);
    es_cell_rect(&c, &usable, 20, margin, &in[1].box, 0);
    in[0] = seam_box(&wa, in[0].box, 7, 20);
    in[1] = seam_box(&wb, in[1].box, 7, 20);
    CHECK(in[0].box.w == 75 && in[1].box.x == 95 && in[1].box.w == 75);
    a[0] = &wa;
    b[0] = &wb;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 1);
    CHECK(built.pos == 85 && built.min_pos == 11 && built.max_pos == 159);
    es_seam_tighten_side(&built, 0, 0, 80, 0);
    es_seam_tighten_side(&built, 1, 0, 80, 0);
    CHECK(built.min_pos == 90 && built.max_pos == 80);

    usable = rect(0, 33, 20, 400);
    c = rect(0, 33, 10, 400);
    es_cell_rect(&c, &usable, 20, margin, &in[0].box, 0);
    c = rect(10, 33, 10, 400);
    es_cell_rect(&c, &usable, 20, margin, &in[1].box, 0);
    CHECK(in[0].box.w == 1 && in[1].box.x == 20 && in[1].box.w == 1);
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == -1);
    CHECK(built.valid && built.pos == 10);
    CHECK(built.min_pos == 11 && built.max_pos == 10);
    CHECK(built.a.n == 1 && built.b.n == 1);
    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1);
    if (n != 1) {
        return;
    }
    CHECK(seams[0].pos == 10 && seams[0].min_pos > seams[0].max_pos);
    /* and dragging it moves nothing, rather than leaving a window of
     * no width at all */
    es_gutter_apply(&seams[0], 15, &moved[0], &moved[1]);
    CHECK(same_rect(&moved[0], &in[0].box) &&
          same_rect(&moved[1], &in[1].box));
    es_gutter_apply(&built, -500, &moved[0], &moved[1]);
    CHECK(same_rect(&moved[0], &in[0].box) &&
          same_rect(&moved[1], &in[1].box));
}

static void test_by_refs_refuses_bad_lists(void)
{
    ESSeamBox in[5];
    ESSeam built;
    void *a[ES_SEAM_SIDE_MAX + 1], *b[2];
    int wa, wb, other, plain1, plain2, nobody, screen1, screen2;
    int i;

    in[0] = seam_box(&wa, cell(0, 33, 960, 523, 20), 7, 20);
    in[1] = seam_box(&wb, cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&other, cell(960, 556, 960, 524, 20), 8, 20);
    in[3] = seam_box(&plain1, rect(0, 556, 960, 524), 0, 0);
    in[4] = seam_box(&plain2, rect(960, 556, 960, 524), 0, 0);
    for (i = 0; i <= ES_SEAM_SIDE_MAX; i++) {
        a[i] = &wa;
    }
    b[0] = &wb;
    b[1] = &wb;
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 1, &built) == 1);

    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 2, &built) == 0);  /* twice */
    /* on both sides */
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, a, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 2, b, 1, &built) == 0);
    b[1] = &nobody;
    /* not in the list */
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 2, &built) == 0);
    b[1] = &other;
    /* of another key */
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 2, &built) == 0);
    a[0] = &plain1;
    b[0] = &plain2;
    /* in no group */
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 1, &built) == 0);
    a[0] = &wa;
    b[0] = &wb;
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 0, b, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 0, &built) == 0);
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, -1, b, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, ES_SEAM_SIDE_MAX + 1, b, 1,
                          &built) == 0);
    CHECK(es_seam_by_refs(in, 5, 8, 1, 0, 1, b, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, -1, 8, 1, a, 1, b, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, ES_SEAM_BOXES_MAX + 1, 8, 1, a, 1, b, 1,
                          &built) == 0);
    /* boxes of one key that do not carry one gap */
    in[1].gap = 10;
    CHECK(es_seam_by_refs(in, 5, 8, 1, a, 1, b, 1, &built) == 0);

    /* two seams are not one, even on one line: the quadrants of a
     * group, named as if the middle were a single seam */
    in[0] = seam_box(&wa, cell(0, 33, 960, 523, 20), 7, 20);
    in[1] = seam_box(&wb, cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&other, cell(0, 556, 960, 524, 20), 7, 20);
    in[3] = seam_box(&nobody, cell(960, 556, 960, 524, 20), 7, 20);
    a[0] = &wa;
    a[1] = &other;
    b[0] = &wb;
    b[1] = &nobody;
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 2, b, 2, &built) == 0);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b + 1, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 4, 8, 1, a, 1, b, 1, &built) == 1);

    /* halves that face each other, but of two keys, or of one key on
     * two screens */
    in[0] = seam_box(&wa, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&wb, cell(960, 33, 960, 1047, 20), 8, 20);
    a[0] = &wa;
    b[0] = &wb;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 0);
    in[1].key = 7;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 1);
    in[0].scope = &screen1;
    in[1].scope = &screen2;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 0);
    in[1].scope = &screen1;
    CHECK(es_seam_by_refs(in, 2, 8, 1, a, 1, b, 1, &built) == 1);
    CHECK(built.scope == (const void *)&screen1);
}

/* A window's own limits, with the gap honoured: the left window keeps
 * its 300, the right one stays between 500 and 700, and the gap of 11
 * survives both ends. Before them, a window of a group with a gap of
 * 11 keeps 2 px: one would touch its boundary from both sides. */
static void test_tighten_side_keeps_the_gap(void)
{
    ESSeamBox in[2];
    ESSeam seam, plain;
    ESRect a, b;
    ESRegistry reg;
    int wa, wb;

    in[0] = seam_box(&wa, cell(0, 33, 960, 1047, 11), 7, 11);
    in[1] = seam_box(&wb, cell(960, 33, 960, 1047, 11), 7, 11);
    CHECK(es_seams_find(in, 2, 8, &seam, 1) == 1);
    CHECK(seam.min_pos == 8 && seam.max_pos == 1913);
    es_seam_tighten_side(&seam, 0, 0, 300, 0);
    CHECK(seam.min_pos == 306 && seam.max_pos == 1913);
    es_seam_tighten_side(&seam, 0, 0, 300, -1);  /* below 0: no maximum */
    CHECK(seam.min_pos == 306 && seam.max_pos == 1913);
    es_seam_tighten_side(&seam, 1, 0, 500, 700);
    CHECK(seam.min_pos == 1215 && seam.max_pos == 1415);
    es_seam_tighten_side(&seam, 1, 0, 0, -1);
    CHECK(seam.min_pos == 1215 && seam.max_pos == 1415);
    es_seam_tighten_side(&seam, 1, 1, 1, 1);     /* no such window */
    es_seam_tighten_side(&seam, 0, -1, 1, 1);
    CHECK(seam.min_pos == 1215 && seam.max_pos == 1415);

    es_gutter_apply(&seam, -1, &a, &b);
    CHECK(a.w == 1209 && b.w == 700 && b.x - (a.x + a.w) == 11);
    es_gutter_apply(&seam, 5000, &a, &b);
    CHECK(a.w == 1409 && b.w == 500 && b.x - (a.x + a.w) == 11);

    /* without a gap it is the tightening the library always did */
    halves(&reg, &wa, &wb);
    CHECK(es_gutter_find(&reg, 8, &plain) == 1);
    es_seam_tighten_side(&plain, 0, 0, 300, 1000);
    es_seam_tighten_side(&plain, 1, 0, 500, 0);
    CHECK(plain.min_pos == 300 && plain.max_pos == 1000);
    es_seam_tighten_side(&plain, 1, 0, 0, 1500);
    CHECK(plain.min_pos == 420 && plain.max_pos == 1000);
}

/*
 * A window no larger than the tolerance, gap included, touches a
 * boundary from both sides. It goes to side a, and before, that was the
 * end of it: a bar of 10 px between two rows of a group with no gap made
 * one seam of the row above and the bar against the row below, and
 * dragging it ran the row above over the bar. A seam with such a window
 * on it is no seam now, so the rows around the bar have none; and no
 * drag of the core's own can make such a window, since every window
 * keeps ES_SEAM_TOUCH_TOL + 1 - G pixels.
 */
static void test_grouped_thin_window_gives_no_seam(void)
{
    ESSeamBox in[3];
    ESSeam seams[ES_SEAM_MAX], built;
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    void *ra[2], *rb[1];
    int wa, bar, wz, gap, k;

    for (gap = 0; gap <= 2; gap += 2) {
        in[0] = seam_box(&wa, cell(0, 33, 1920, 467, gap), 7, gap);
        in[1] = seam_box(&bar, cell(0, 500, 1920, 10, gap), 7, gap);
        in[2] = seam_box(&wz, cell(0, 510, 1920, 570, gap), 7, gap);
        CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 0);
        ra[0] = &wa;
        ra[1] = &bar;
        rb[0] = &wz;
        CHECK(es_seam_by_refs(in, 3, 8, 0, ra, 2, rb, 1, &built) == 0);
        CHECK(es_seam_by_refs(in, 3, 8, 0, ra, 1, rb, 1, &built) == 0);
        rb[0] = &bar;
        CHECK(es_seam_by_refs(in, 3, 8, 0, ra, 1, rb, 1, &built) == 0);
    }

    /* halves dragged to either limit are still a seam, for every gap */
    for (gap = 0; gap <= 14; gap++) {
        int floor_px = ES_SEAM_TOUCH_TOL + 1 - gap > 1
                       ? ES_SEAM_TOUCH_TOL + 1 - gap : 1;

        for (k = 0; k < 2; k++) {
            in[0] = seam_box(&wa, cell(0, 33, 960, 1047, gap), 7, gap);
            in[1] = seam_box(&wz, cell(960, 33, 960, 1047, gap), 7, gap);
            CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 1);
            es_gutter_apply(&seams[0], k ? seams[0].max_pos : seams[0].min_pos,
                            a, b);
            CHECK((k ? b[0].w : a[0].w) == floor_px);
            in[0].box = a[0];
            in[1].box = b[0];
            CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 1);
            ra[0] = &wa;
            rb[0] = &wz;
            CHECK(es_seam_by_refs(in, 2, 8, 1, ra, 1, rb, 1, &built) == 1);
        }
    }

    /* three columns at a gap of 4: the middle one squeezed by its right
     * seam keeps 9 px, and both of its seams are still there */
    in[0] = seam_box(&wa, cell(0, 33, 640, 1047, 4), 7, 4);
    in[1] = seam_box(&bar, cell(640, 33, 640, 1047, 4), 7, 4);
    in[2] = seam_box(&wz, cell(1280, 33, 640, 1047, 4), 7, 4);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 2);
    CHECK(seams[1].pos == 1280 && seams[1].min_pos == 653);
    es_gutter_apply(&seams[1], seams[1].min_pos, a, b);
    CHECK(a[0].x == 642 && a[0].w == 9);
    in[1].box = a[0];
    in[2].box = b[0];
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 2);
    CHECK(seams[0].pos == 640 && seams[0].a.n == 1 && seams[0].b.n == 1);
    CHECK(seams[1].pos == 653 && seams[1].a.n == 1 && seams[1].b.n == 1);
    ra[0] = &wa;
    rb[0] = &bar;
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);

    /* side a first: at a gap of 8, a column of two (the lower one 7 px
     * wide) against one window on the right. On that window's line the
     * narrow one touches from both sides; it is the column's */
    in[0] = seam_box(&wa, rect(70, 0, 17, 100), 7, 8);
    in[1] = seam_box(&bar, rect(80, 100, 7, 100), 7, 8);
    in[2] = seam_box(&wz, rect(90, 0, 19, 200), 7, 8);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 2);
    CHECK(seams[0].vertical && seams[0].pos == 91);
    CHECK(seams[0].a.n == 2 && seams[0].b.n == 1);
    ra[1] = &bar;
    rb[0] = &wz;
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 2, rb, 1, &built) == 1);

    /* a thin window on the line that shares no stretch with either
     * side is not the seam's business */
    in[0] = seam_box(&wa, rect(0, 33, 958, 500), 7, 4);
    in[1] = seam_box(&wz, rect(962, 33, 958, 500), 7, 4);
    in[2] = seam_box(&bar, rect(959, 700, 1, 100), 7, 4);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 1);
    CHECK(seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 1 && seams[0].b.n == 1);
    ra[0] = &wa;
    rb[0] = &wz;
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
}

/*
 * Two windows of a group on one side of a line that share a stretch
 * along it, one behind the other across it, less than the gap apart.
 * Both touch the line from that side, so any drag gives them one edge
 * and runs the one behind over the one in front: there is no seam. At
 * a gap of 20, a window 10 px wide stands between the left one and the
 * right one, 2 px from the left one or touching it; stacked above each
 * other instead, the two on the left are one side of a seam. Then the
 * same across a horizontal line, on side b, at a gap of 31; and a
 * column cut into cells of 500, 10 and 490 px, where es_cell_rect gives
 * the middle cell a box of 1 px.
 */
static void test_grouped_window_behind_another_gives_no_seam(void)
{
    ESRect usable = rect(0, 0, 1000, 1000);
    ESRect c;
    ESSeamBox in[3], moved[3];
    ESSeam seams[ES_SEAM_MAX], built;
    int margin[4] = {0, 0, 0, 0};
    void *ra[2], *rb[2];
    int w[3];
    int k, i;

    in[0] = seam_box(&w[0], rect(0, 0, 478, 400), 7, 20);
    in[1] = seam_box(&w[1], rect(480, 0, 10, 400), 7, 20);
    in[2] = seam_box(&w[2], rect(510, 0, 490, 400), 7, 20);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 0);
    ra[0] = &w[0];
    ra[1] = &w[1];
    rb[0] = &w[2];
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 2, rb, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra + 1, 1, rb, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 0);
    in[1].box = rect(478, 0, 12, 400);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 0);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 2, rb, 1, &built) == 0);

    in[0].box = rect(0, 0, 478, 200);
    in[1].box = rect(480, 200, 10, 200);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 1);
    CHECK(seams[0].pos == 500 && seams[0].a.n == 2 && seams[0].b.n == 1);
    CHECK(seams[0].min_pos == 491 && seams[0].max_pos == 989);
    for (k = 0; k < 2; k++) {
        for (i = 0; i < 3; i++) {
            moved[i] = in[i];
        }
        drag_seam(&seams[0], k ? seams[0].max_pos : seams[0].min_pos,
                  moved, 3);
        CHECK(overlaps(moved, 3) == 0);
    }
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 2, rb, 1, &built) == 1);

    in[0] = seam_box(&w[0], rect(0, 0, 400, 484), 7, 31);
    in[1] = seam_box(&w[1], rect(0, 515, 400, 8), 7, 31);
    in[2] = seam_box(&w[2], rect(0, 525, 400, 500), 7, 31);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 0);
    rb[0] = &w[1];
    rb[1] = &w[2];
    CHECK(es_seam_by_refs(in, 3, 8, 0, ra, 1, rb, 2, &built) == 0);

    for (i = 0; i < 3; i++) {
        c = i == 0 ? rect(0, 0, 1000, 500)
                   : i == 1 ? rect(0, 500, 1000, 10) : rect(0, 510, 1000, 490);
        in[i] = seam_box(&w[i], rect(0, 0, 1, 1), 7, 20);
        es_cell_rect(&c, &usable, 20, margin, &in[i].box, 0);
    }
    CHECK(in[1].box.y == 510 && in[1].box.h == 1 && in[2].box.y == 520);
    CHECK(es_seams_find(in, 3, 8, seams, ES_SEAM_MAX) == 0);
    CHECK(es_seam_by_refs(in, 3, 8, 0, ra, 1, rb, 2, &built) == 0);
}

/*
 * Windows of the group outside a seam that are linked to one of its
 * windows from across it. A stack window that reaches into the gap
 * (es_fit_rect widened it 16 px to the left) is too far off the
 * boundary to move with the seam. Before, the master was dragged over
 * it; then there was no seam at all. Now the seam of the master and
 * the other two is kept, and stops where it is: it moves only away
 * from that window, on either side. A stack window further off than
 * the boundary (narrower than its cell, 40 px from it) stops the drag
 * where the gap between them is still whole.
 */
static void test_grouped_window_reaching_into_the_gap(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX], built;
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    void *ra[2], *rb[3];
    int m, s1, s2, s3;
    int n, i, v = -1;

    in[0] = seam_box(&m, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(960, 33, 960, 349, 20), 7, 20);
    in[2] = seam_box(&s2, cell(960, 382, 960, 349, 20), 7, 20);
    in[3] = seam_box(&s3, cell(960, 731, 960, 349, 20), 7, 20);
    in[2].box.x -= 16;
    in[2].box.w += 16;
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3);
    if (n != 3) {
        return;
    }
    CHECK(count_axis(seams, n, 1) == 1 && seams[0].vertical);
    CHECK(seams[0].pos == 960 && seams[0].a.n == 1 && seams[0].b.n == 2);
    CHECK(on_side(&seams[0].b, &s1) && on_side(&seams[0].b, &s3));
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 960);
    es_gutter_apply(&seams[0], 5000, a, b);
    CHECK(a[0].x + a[0].w == 950 && b[0].x == 970 && b[1].x == 970);
    es_gutter_apply(&seams[0], 700, a, b);
    CHECK(a[0].x + a[0].w == 690 && b[0].x == 710 && b[1].x == 710);
    CHECK(b[0].x + b[0].w == 1920 && b[1].x + b[1].w == 1920);
    ra[0] = &m;
    rb[0] = &s3;
    rb[1] = &s1;
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 1, rb, 2, &built) == 1);
    CHECK(built.pos == 960 && built.min_pos == 11 && built.max_pos == 960);
    rb[2] = &s2;
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 1, rb, 3, &built) == 0);

    /* the mirror: the master on the right, s2 widened to the right */
    in[0] = seam_box(&m, cell(960, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(0, 33, 960, 349, 20), 7, 20);
    in[2] = seam_box(&s2, cell(0, 382, 960, 349, 20), 7, 20);
    in[3] = seam_box(&s3, cell(0, 731, 960, 349, 20), 7, 20);
    in[2].box.w += 16;
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 2 && seams[0].b.n == 1);
    CHECK(seams[0].min_pos == 960 && seams[0].max_pos == 1909);
    ra[0] = &s1;
    ra[1] = &s3;
    rb[0] = &m;
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 2, rb, 1, &built) == 1);
    CHECK(built.min_pos == 960 && built.max_pos == 1909);

    in[0] = seam_box(&m, cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&s1, cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&s2, cell(960, 556, 960, 524, 20), 7, 20);
    in[2].box.x += 40;
    in[2].box.w -= 40;
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    for (i = 0; i < n && i < ES_SEAM_MAX; i++) {
        if (seams[i].vertical) {
            v = i;
        }
    }
    CHECK(n == 2 && v >= 0);
    if (v < 0) {
        return;
    }
    CHECK(seams[v].pos == 960 && seams[v].a.n == 1 && seams[v].b.n == 1);
    CHECK(seams[v].b.ref[0] == (void *)&s1);
    CHECK(seams[v].min_pos == 11 && seams[v].max_pos == 1000);
    ra[0] = &m;
    rb[0] = &s1;
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.max_pos == 1000);
    es_gutter_apply(&seams[v], 5000, a, b);
    CHECK(a[0].x + a[0].w == 990 && in[2].box.x - (a[0].x + a[0].w) == 20);
    CHECK(b[0].x == 1010);

    /* there the far window touches the seam's line, so the seam now
     * holds it, and the old pair is no longer the seam */
    in[0].box = a[0];
    in[1].box = b[0];
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 0);
    rb[1] = &s2;
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 2, &built) == 1);
    CHECK(built.pos == 1000 && built.max_pos == 1909);
}

/*
 * A window of a seam that falls short of the position, beside one of
 * the group that reaches past it. The master is 10 px short of its cell
 * (size increments) and s2 was widened 25 px to the left: dragged to
 * the position, the master would run into s2. Before, the drag stopped
 * at the position all the same; now it stops where the master ends,
 * and the position lies outside the limits. The same with es_fit_rect
 * alone: the master on the right has a MinWidth of 966, so it reaches
 * 16 px into the gap, and the top stack window's MaxWidth of 922 puts
 * the position at 932, where the master would run over the two below.
 * That seam moves only to the right, and with the windows' own limits
 * not at all. A master that reaches past the position, beside a window
 * of the stack that comes within the gap of it, stops at the position.
 */
static void test_grouped_window_short_of_a_reaching_one(void)
{
    ESRect usable = rect(0, 33, 1920, 1047);
    ESRect area, c, box;
    ESSeamBox in[4], moved[4];
    ESSeam seams[ES_SEAM_MAX], built;
    ESRect a[ES_SEAM_SIDE_MAX], b[ES_SEAM_SIDE_MAX];
    int margin[4] = {0, 0, 0, 0};
    void *ra[1], *rb[2];
    int m, s1, s2, s3;
    int n, k, i, to;

    in[0] = seam_box(&m, rect(0, 33, 940, 1047), 7, 20);
    in[1] = seam_box(&s1, rect(970, 33, 950, 339), 7, 20);
    in[2] = seam_box(&s2, rect(945, 392, 975, 329), 7, 20);
    in[3] = seam_box(&s3, rect(970, 741, 950, 339), 7, 20);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3 && seams[0].vertical && seams[0].pos == 960);
    if (n != 3) {
        return;
    }
    CHECK(seams[0].a.n == 1 && seams[0].b.n == 2);
    CHECK(on_side(&seams[0].b, &s1) && on_side(&seams[0].b, &s3));
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 950);
    for (k = 0; k < 3; k++) {
        to = k == 0 ? seams[0].min_pos
                    : k == 1 ? seams[0].pos : seams[0].max_pos;
        for (i = 0; i < 4; i++) {
            moved[i] = in[i];
        }
        drag_seam(&seams[0], to, moved, 4);
        CHECK(overlaps(moved, 4) == 0);
    }
    CHECK(same_rect(&moved[0].box, &in[0].box));
    CHECK(moved[1].box.x == 960 && moved[3].box.x == 960);
    ra[0] = &m;
    rb[0] = &s3;
    rb[1] = &s1;
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 1, rb, 2, &built) == 1);
    CHECK(built.pos == 960 && built.min_pos == 11 && built.max_pos == 950);

    c = rect(960, 33, 960, 1047);
    es_cell_rect(&c, &usable, 20, margin, &box, &area);
    es_fit_rect(&box, &area, 966, 0, 0, 0, &in[0].box);
    c = rect(0, 33, 960, 349);
    es_cell_rect(&c, &usable, 20, margin, &box, &area);
    es_fit_rect(&box, &area, 0, 0, 922, 0, &in[1].box);
    in[2].box = cell(0, 382, 960, 349, 20);
    in[3].box = cell(0, 731, 960, 349, 20);
    CHECK(in[0].box.x == 954 && in[0].box.w == 966);
    CHECK(in[1].box.x == 0 && in[1].box.w == 922);
    CHECK(in[2].box.w == 950 && in[3].box.w == 950);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3 && seams[0].vertical && seams[0].pos == 932);
    if (n != 3) {
        return;
    }
    CHECK(seams[0].a.n == 1 && seams[0].a.ref[0] == (void *)&s1);
    CHECK(seams[0].b.n == 1 && seams[0].b.ref[0] == (void *)&m);
    CHECK(seams[0].min_pos == 944 && seams[0].max_pos == 1909);
    for (k = 0; k < 3; k++) {
        to = k == 0 ? seams[0].min_pos
                    : k == 1 ? seams[0].pos : seams[0].max_pos;
        for (i = 0; i < 4; i++) {
            moved[i] = in[i];
        }
        drag_seam(&seams[0], to, moved, 4);
        CHECK(overlaps(moved, 4) == 0);
    }
    ra[0] = &s1;
    rb[0] = &m;
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.min_pos == 944 && built.max_pos == 1909);
    es_seam_tighten_side(&built, 0, 0, 0, 922);
    es_seam_tighten_side(&built, 1, 0, 966, 0);
    CHECK(built.min_pos > built.max_pos);
    es_gutter_apply(&built, built.pos, a, b);
    CHECK(same_rect(&a[0], &in[1].box) && same_rect(&b[0], &in[0].box));

    /* a master that reaches 5 px past the position, 1 px from s2: the
     * seam stops at the position, so the master only moves away */
    in[0].box = rect(0, 33, 955, 1047);
    in[1].box = rect(970, 33, 950, 339);
    in[2].box = rect(956, 392, 964, 329);
    in[3].box = rect(970, 741, 950, 339);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 1 && seams[0].b.n == 2);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 960);
    es_gutter_apply(&seams[0], seams[0].max_pos, a, b);
    CHECK(a[0].w == 950 && b[0].x == 970 && b[1].x == 970);
    in[0].box = rect(965, 33, 955, 1047);
    in[1].box = rect(0, 33, 950, 339);
    in[2].box = rect(0, 392, 963, 329);
    in[3].box = rect(0, 741, 950, 339);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 2 && seams[0].b.n == 1);
    CHECK(seams[0].min_pos == 960 && seams[0].max_pos == 1909);
    es_gutter_apply(&seams[0], seams[0].min_pos, a, b);
    CHECK(b[0].x == 970 && a[0].w == 950 && a[1].w == 950);
}

/*
 * A window of the group beyond a seam limits its drag only when it
 * shares some stretch with one of the seam's windows (recheck R2). Under
 * the right half of the row, it leaves the limits of the row's seam
 * alone, and so it does touching the left half only at a corner; one
 * pixel higher it shares a stretch with the left half, and stops the
 * drag where the gap to it is whole. On another screen, or in another
 * group, it is none of the seam's business.
 */
static void test_grouped_window_beyond_the_seam(void)
{
    ESSeamBox in[3];
    ESSeam seams[ES_SEAM_MAX], built;
    void *ra[1], *rb[1];
    int w[3], other;
    int n;

    in[0] = seam_box(&w[0], cell(0, 33, 960, 523, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&w[2], cell(1440, 556, 480, 524, 20), 7, 20);
    ra[0] = &w[0];
    rb[0] = &w[1];
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    CHECK(!seams[1].vertical && seams[1].a.ref[0] == (void *)&w[1]);

    in[2].box = rect(1450, 546, 470, 534);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.max_pos == 1909);

    in[2].box = rect(1450, 545, 470, 535);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 11 && seams[0].max_pos == 1440);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.max_pos == 1440);

    /* nor is one of the group on another screen, nor one of another
     * group, wherever it stands */
    in[2].scope = &other;
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.max_pos == 1909);
    in[2].scope = 0;
    in[2].key = 8;
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.max_pos == 1909);
    in[2].key = 7;

    /* a window on the seam's own side, linked to none across it, is
     * not its business, even where it overlaps one of its windows,
     * from its left edge or from further right: a drag does not take
     * that window of the seam any further into it. One that reaches
     * further than the seam's window stops the drag where that window
     * is now. The same on side b. */
    in[0].box = rect(0, 33, 950, 767);
    in[1].box = rect(970, 33, 950, 367);
    in[2].box = rect(0, 600, 950, 480);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].a.n == 1 &&
          seams[0].a.ref[0] == (void *)&w[0]);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    in[2].box = rect(100, 600, 850, 480);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].a.n == 1 &&
          seams[0].a.ref[0] == (void *)&w[0]);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.max_pos == 1909);
    in[2].box = rect(100, 600, 855, 480);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 11 && seams[0].max_pos == 960);
    in[0].box = rect(970, 33, 950, 767);
    in[1].box = rect(0, 33, 950, 367);
    in[2].box = rect(970, 600, 950, 480);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].b.n == 1 &&
          seams[0].b.ref[0] == (void *)&w[0]);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    in[2].box = rect(970, 600, 850, 480);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    in[2].box = rect(965, 600, 855, 480);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].min_pos == 960 && seams[0].max_pos == 1909);

    /* nor is one behind its windows: of three columns, the seam on the
     * right keeps all its room on the left */
    in[0].box = cell(0, 33, 640, 1047, 20);
    in[1].box = cell(640, 33, 640, 1047, 20);
    in[2].box = cell(1280, 33, 640, 1047, 20);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2 && seams[0].pos == 640 && seams[1].pos == 1280);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1259);
    CHECK(seams[1].min_pos == 661 && seams[1].max_pos == 1909);
}

/*
 * The strip of a group's seam spans every stretch its linked windows
 * share. Here the lower window of the stack hangs far below the master:
 * it shares too little with it to face it, but it is on the seam, and
 * the strip covers it down to the master's bottom edge. For windows in
 * no group the strip spans the facing stretches only, as it always did.
 * And two windows of a group that share no more than a short stretch
 * are no seam at all: a seam needs two windows that face each other,
 * which half of the shorter one is enough for.
 */
static void test_grouped_strip_covers_linked_stretches(void)
{
    ESSeamBox in[3];
    ESSeam seams[ES_SEAM_MAX], built;
    void *ra[1], *rb[2];
    int m, s1, s2;
    int n;

    in[0] = seam_box(&m, rect(0, 33, 950, 1047), 7, 20);
    in[1] = seam_box(&s1, rect(970, 33, 950, 657), 7, 20);
    in[2] = seam_box(&s2, rect(970, 710, 950, 790), 7, 20);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 1 && seams[0].b.n == 2);
    CHECK(seams[0].rect.y == 33 && seams[0].rect.h == 1047);
    ra[0] = &m;
    rb[0] = &s2;
    rb[1] = &s1;
    CHECK(es_seam_by_refs(in, 3, 8, 1, ra, 1, rb, 2, &built) == 1);
    CHECK(same_rect(&built.rect, &seams[0].rect));

    in[0] = seam_box(&m, rect(0, 33, 960, 1047), 0, 0);
    in[1] = seam_box(&s1, rect(960, 33, 960, 657), 0, 0);
    in[2] = seam_box(&s2, rect(960, 710, 960, 790), 0, 0);
    n = es_seams_find(in, 3, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].vertical && seams[0].b.n == 2);
    CHECK(seams[0].rect.y == 33 && seams[0].rect.h == 657);

    in[0] = seam_box(&m, rect(0, 33, 950, 300), 7, 20);
    in[1] = seam_box(&s1, rect(970, 183, 950, 700), 7, 20);
    rb[0] = &s1;
    CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 1);
    CHECK(es_seam_by_refs(in, 2, 8, 1, ra, 1, rb, 1, &built) == 1);
    in[1].box.y++;
    CHECK(es_seams_find(in, 2, 8, seams, ES_SEAM_MAX) == 0);
    CHECK(es_seam_by_refs(in, 2, 8, 1, ra, 1, rb, 1, &built) == 0);
}

/*
 * A 2x2 of a group whose splits do not line up (540 on the left, 600 on
 * the right, at a gap of 20). Across the middle the lower left window
 * shares 40 px with the upper right one: too little to face it, enough
 * to run into it. So the middle is one seam that moves all four, as it
 * is for windows in no group, and each split is a seam of its own.
 * Dragged to either limit, no seam runs a window into another.
 */
static void test_grouped_misaligned_splits(void)
{
    ESSeamBox in[4], moved[4];
    ESSeam seams[ES_SEAM_MAX], built;
    void *ra[2], *rb[2];
    int w[4];
    int n, k, i;

    in[0] = seam_box(&w[0], cell(0, 33, 960, 507, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(0, 540, 960, 540, 20), 7, 20);
    in[2] = seam_box(&w[2], cell(960, 33, 960, 567, 20), 7, 20);
    in[3] = seam_box(&w[3], cell(960, 600, 960, 480, 20), 7, 20);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3);
    if (n != 3) {
        return;
    }
    CHECK(seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 2 && seams[0].a.ref[0] == (void *)&w[0] &&
          seams[0].a.ref[1] == (void *)&w[1]);
    CHECK(seams[0].b.n == 2 && seams[0].b.ref[0] == (void *)&w[2] &&
          seams[0].b.ref[1] == (void *)&w[3]);
    CHECK(seams[0].rect.y == 33 && seams[0].rect.h == 1047);
    CHECK(seams[0].min_pos == 11 && seams[0].max_pos == 1909);
    CHECK(!seams[1].vertical && seams[1].pos == 540);
    CHECK(seams[1].a.ref[0] == (void *)&w[0] &&
          seams[1].b.ref[0] == (void *)&w[1]);
    CHECK(!seams[2].vertical && seams[2].pos == 600);
    CHECK(seams[2].a.ref[0] == (void *)&w[2] &&
          seams[2].b.ref[0] == (void *)&w[3]);
    for (k = 0; k < 6; k++) {
        const ESSeam *s = &seams[k / 2];

        for (i = 0; i < 4; i++) {
            moved[i] = in[i];
        }
        drag_seam(s, (k & 1) ? s->max_pos : s->min_pos, moved, 4);
        CHECK(overlaps(moved, 4) == 0);
    }

    /* the four are the seam, and no pair of them is */
    ra[0] = &w[1];
    ra[1] = &w[0];
    rb[0] = &w[3];
    rb[1] = &w[2];
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 2, rb, 2, &built) == 1);
    CHECK(built.pos == 960 && built.rect.y == 33 && built.rect.h == 1047);
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra + 1, 1, rb + 1, 1, &built) == 0);
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 1, rb, 1, &built) == 0);

    /* in no group, the same cells have the same seam down the middle */
    in[0] = seam_box(&w[0], rect(0, 33, 960, 507), 0, 0);
    in[1] = seam_box(&w[1], rect(0, 540, 960, 540), 0, 0);
    in[2] = seam_box(&w[2], rect(960, 33, 960, 567), 0, 0);
    in[3] = seam_box(&w[3], rect(960, 600, 960, 480), 0, 0);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3 && seams[0].vertical && seams[0].pos == 960);
    CHECK(seams[0].a.n == 2 && seams[0].b.n == 2);
}

/*
 * Quadrants of a group at a gap of 20, the right-hand split dragged
 * 60 px down (recheck, probe Q3). The lower left window and the upper
 * right one now share 40 px across the middle, so the two seams down
 * the middle become one that moves all four: before, the lower one
 * alone, dragged right, ran the lower left window into the upper right.
 */
static void test_grouped_quadrants_after_a_drag(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX];
    int w[4];
    int n;

    in[0] = seam_box(&w[0], cell(0, 33, 960, 523, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&w[2], cell(0, 556, 960, 524, 20), 7, 20);
    in[3] = seam_box(&w[3], cell(960, 556, 960, 524, 20), 7, 20);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 4);
    if (n != 4) {
        return;
    }
    CHECK(!seams[3].vertical && seams[3].pos == 556);
    CHECK(seams[3].a.ref[0] == (void *)&w[1] &&
          seams[3].b.ref[0] == (void *)&w[3]);
    drag_seam(&seams[3], 616, in, 4);
    CHECK(in[1].box.y + in[1].box.h == 606 && in[3].box.y == 626);

    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3);
    if (n != 3) {
        return;
    }
    CHECK(count_axis(seams, 3, 1) == 1 && seams[0].vertical);
    CHECK(seams[0].a.n == 2 && seams[0].b.n == 2);
    CHECK(seams[1].pos == 556 && seams[2].pos == 616);
    drag_seam(&seams[0], 1200, in, 4);
    CHECK(in[0].box.x + in[0].box.w == 1190 &&
          in[2].box.x + in[2].box.w == 1190);
    CHECK(in[1].box.x == 1210 && in[3].box.x == 1210);
    CHECK(overlaps(in, 4) == 0);
}

/*
 * A set of windows turned down on one line keeps nothing from a later
 * line (recheck R3). On the line at 100 that the window at the bottom
 * proposes, the two windows on the left (whose boundaries are 88 and
 * 112) and the tall one on the right (112) are linked, but no boundary
 * lies within the tolerance of all three, so there is no seam there.
 * Before, that refusal kept them from the line at 112, where the lower
 * left window and the right one are a seam of their own; now it is
 * found, and the upper left window, linked to the right one from
 * further away, stops its drag where the gap is whole.
 */
static void test_grouped_refusal_keeps_nothing(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX], built;
    void *ra[2], *rb[1];
    int w[4];
    int n;

    in[0] = seam_box(&w[0], rect(0, 33, 78, 200), 7, 20);
    in[1] = seam_box(&w[1], rect(0, 253, 102, 200), 7, 20);
    in[2] = seam_box(&w[2], rect(122, 33, 400, 420), 7, 20);
    in[3] = seam_box(&w[3], rect(110, 600, 400, 200), 7, 20);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2);
    if (n != 2) {
        return;
    }
    CHECK(seams[0].vertical && seams[0].pos == 112);
    CHECK(seams[0].a.n == 1 && seams[0].a.ref[0] == (void *)&w[1]);
    CHECK(seams[0].b.n == 1 && seams[0].b.ref[0] == (void *)&w[2]);
    CHECK(seams[0].min_pos == 88 && seams[0].max_pos == 511);
    CHECK(!seams[1].vertical && seams[1].pos == 243);
    drag_seam(&seams[0], 0, in, 4);
    CHECK(in[2].box.x == 98 &&
          in[2].box.x - (in[0].box.x + in[0].box.w) == 20);
    CHECK(in[1].box.x + in[1].box.w == 78);

    in[1].box = rect(0, 253, 102, 200);
    in[2].box = rect(122, 33, 400, 420);
    ra[0] = &w[1];
    rb[0] = &w[2];
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 1, rb, 1, &built) == 1);
    CHECK(built.min_pos == 88 && built.max_pos == 511);
    ra[1] = &w[0];
    CHECK(es_seam_by_refs(in, 4, 8, 1, ra, 2, rb, 1, &built) == 0);
}

/*
 * The order of the seams on an axis: those of key 0 in list order, then
 * those of the groups by boundary, and at one boundary by gap, then by
 * key, and on one line by where they start along it, whatever the order
 * of the list. Only between seams of one key on two screens at one
 * boundary does the list decide: the one whose line a window earlier in
 * the list proposes goes first.
 */
static void test_grouped_seams_in_line_order(void)
{
    ESSeamBox in[8];
    ESSeam seams[ES_SEAM_MAX];
    int w[8], s1, s2;
    int n;

    in[0] = seam_box(&w[0], cell(0, 33, 1200, 1047, 20), 9, 20);
    in[1] = seam_box(&w[1], cell(1200, 33, 720, 1047, 20), 9, 20);
    in[2] = seam_box(&w[2], cell(0, 33, 960, 1047, 30), 8, 30);
    in[3] = seam_box(&w[3], cell(960, 33, 960, 1047, 30), 8, 30);
    in[4] = seam_box(&w[4], cell(0, 33, 960, 1047, 10), 7, 10);
    in[5] = seam_box(&w[5], cell(960, 33, 960, 1047, 10), 7, 10);
    in[6] = seam_box(&w[6], rect(0, 33, 960, 1047), 0, 0);
    in[7] = seam_box(&w[7], rect(960, 33, 960, 1047), 0, 0);
    n = es_seams_find(in, 8, 8, seams, ES_SEAM_MAX);
    CHECK(n == 4);
    if (n != 4) {
        return;
    }
    CHECK(seams[0].key == 0 && seams[1].key == 7 && seams[2].key == 8);
    CHECK(seams[3].key == 9 && seams[3].pos == 1200);
    CHECK(seams[1].pos == 960 && seams[2].pos == 960);

    /* one boundary and one gap: the smaller key first */
    in[2] = seam_box(&w[2], cell(0, 33, 960, 1047, 10), 8, 10);
    in[3] = seam_box(&w[3], cell(960, 33, 960, 1047, 10), 8, 10);
    n = es_seams_find(in + 2, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2 && seams[0].key == 7 && seams[1].key == 8);

    /* one key on two screens: the list decides, by the window on the
     * right that proposes the line first */
    in[0] = seam_box(&w[0], cell(0, 33, 960, 1047, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(0, 33, 960, 1047, 20), 7, 20);
    in[2] = seam_box(&w[2], cell(960, 33, 960, 1047, 20), 7, 20);
    in[3] = seam_box(&w[3], cell(960, 33, 960, 1047, 20), 7, 20);
    in[0].scope = &s1;
    in[1].scope = &s2;
    in[2].scope = &s2;
    in[3].scope = &s1;
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2 && seams[0].scope == (const void *)&s2 &&
          seams[1].scope == (const void *)&s1);
    in[2].scope = &s1;
    in[3].scope = &s2;
    in[0].scope = &s2;
    in[1].scope = &s1;
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 2 && seams[0].scope == (const void *)&s1 &&
          seams[1].scope == (const void *)&s2);

    /* quadrants listed from the bottom right: on each line the seam
     * that starts first comes first, the top pair down the middle and
     * the left pair across */
    in[0] = seam_box(&w[0], cell(960, 556, 960, 524, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(0, 556, 960, 524, 20), 7, 20);
    in[2] = seam_box(&w[2], cell(960, 33, 960, 523, 20), 7, 20);
    in[3] = seam_box(&w[3], cell(0, 33, 960, 523, 20), 7, 20);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 4);
    if (n != 4) {
        return;
    }
    CHECK(seams[0].vertical && seams[0].a.ref[0] == (void *)&w[3]);
    CHECK(seams[1].vertical && seams[1].a.ref[0] == (void *)&w[1]);
    CHECK(!seams[2].vertical && seams[2].a.ref[0] == (void *)&w[3]);
    CHECK(!seams[3].vertical && seams[3].a.ref[0] == (void *)&w[2]);
}

/*
 * The gap as the core reads it: past ES_SEAM_GAP_MAX it is the largest,
 * below 0 it is none, and a box of key 0 has none whatever it carries.
 * Here the candidate of the second row would come within the tolerance
 * of the first row's line, and the seam there be passed over, if its
 * gap of 40 counted.
 */
static void test_gaps_as_the_core_reads_them(void)
{
    ESSeamBox in[4];
    ESSeam seams[ES_SEAM_MAX];
    int w[4];
    int n;

    in[0] = seam_box(&w[0], cell(0, 33, 960, 1047, 200), 7, 300);
    in[1] = seam_box(&w[1], cell(960, 33, 960, 1047, 200), 7, 300);
    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].gap == 200 && seams[0].pos == 960);
    in[0] = seam_box(&w[0], rect(0, 33, 960, 1047), 7, -5);
    in[1] = seam_box(&w[1], rect(960, 33, 960, 1047), 7, -5);
    n = es_seams_find(in, 2, 8, seams, ES_SEAM_MAX);
    CHECK(n == 1 && seams[0].gap == 0 && seams[0].pos == 960);

    in[0] = seam_box(&w[0], rect(0, 33, 960, 523), 0, 0);
    in[1] = seam_box(&w[1], rect(960, 33, 960, 523), 0, 0);
    in[2] = seam_box(&w[2], rect(0, 556, 980, 524), 0, 0);
    in[3] = seam_box(&w[3], rect(980, 556, 940, 524), 0, 40);
    n = es_seams_find(in, 4, 8, seams, ES_SEAM_MAX);
    CHECK(n == 3);
    CHECK(count_axis(seams, n < ES_SEAM_MAX ? n : ES_SEAM_MAX, 1) == 2);
    CHECK(seams[0].pos == 960 && seams[1].pos == 980);
    CHECK(seams[0].gap == 0 && seams[1].gap == 0 && seams[2].gap == 0);
}

/* es_seams_find tells how many seams there are, past what fits; the
 * registry calls still answer how many were written. */
static void test_seams_find_counts_past_max(void)
{
    ESSeamBox in[4];
    ESSeam seams[3];
    ESRegistry reg;
    int w[4];
    int l, tr, br;

    in[0] = seam_box(&w[0], cell(0, 33, 960, 523, 11), 7, 11);
    in[1] = seam_box(&w[1], cell(960, 33, 960, 523, 11), 7, 11);
    in[2] = seam_box(&w[2], cell(0, 556, 960, 524, 11), 7, 11);
    in[3] = seam_box(&w[3], cell(960, 556, 960, 524, 11), 7, 11);
    seams[2].valid = 77;
    CHECK(es_seams_find(in, 4, 8, seams, 2) == 4);
    CHECK(seams[0].valid == 1 && seams[1].valid == 1 && seams[2].valid == 77);
    CHECK(seams[0].pos == 960 && seams[1].pos == 960);
    CHECK(es_seams_find(in, 4, 8, 0, 0) == 4);
    CHECK(es_seams_find(in, 4, 8, 0, 3) == 4);
    CHECK(es_seams_find(in, 4, 8, seams, -1) == 4);
    CHECK(es_seams_find(in, -1, 8, seams, 3) == 0);
    CHECK(es_seams_find(in, ES_SEAM_BOXES_MAX + 1, 8, seams, 3) == 0);
    CHECK(es_seams_find(in, 0, 8, seams, 3) == 0);

    split_right(&reg, &l, &tr, &br);
    seams[1].valid = 77;
    CHECK(es_gutter_find_all(&reg, 8, seams, 1) == 1);
    CHECK(seams[0].vertical && seams[1].valid == 77);
    CHECK(es_gutter_find_all(&reg, 8, seams, 0) == 0);
    CHECK(es_gutter_find_all(&reg, 8, seams, 3) == 2);
}

/* A fixed-seed generator of its own, so every run and every host
 * draws the same layouts. */
static unsigned long g_lcg;

static int lcg(int n)
{
    g_lcg = (g_lcg * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (int)((g_lcg >> 16) % (unsigned long)n);
}

static void shuffle_refs(void **v, int n)
{
    int i;

    for (i = n - 1; i > 0; i--) {
        int j = lcg(i + 1);
        void *t = v[i];

        v[i] = v[j];
        v[j] = t;
    }
}

static int same_sides(const ESSeamSide *p, const ESSeamSide *q)
{
    int i;

    if (p->n != q->n) {
        return 0;
    }
    for (i = 0; i < p->n; i++) {
        if (!on_side(q, p->ref[i])) {
            return 0;
        }
    }
    return 1;
}

/* Is the seam s among y[0..ny-1], with the same windows and limits? */
static int has_seam(const ESSeam *y, int ny, const ESSeam *s)
{
    int k;

    for (k = 0; k < ny; k++) {
        const ESSeam *t = &y[k];

        if (t->key == s->key && t->scope == s->scope &&
            t->vertical == s->vertical && t->pos == s->pos &&
            t->min_pos == s->min_pos && t->max_pos == s->max_pos &&
            t->gap == s->gap && same_rect(&t->rect, &s->rect) &&
            same_sides(&t->a, &s->a) && same_sides(&t->b, &s->b)) {
            return 1;
        }
    }
    return 0;
}

/* Do x and y hold the same seams of groups, in whatever order? */
static int same_grouped_seams(const ESSeam *x, int nx,
                              const ESSeam *y, int ny)
{
    int k, cx = 0, cy = 0;

    for (k = 0; k < nx; k++) {
        if (x[k].key != 0) {
            cx++;
            if (!has_seam(y, ny, &x[k])) {
                return 0;
            }
        }
    }
    for (k = 0; k < ny; k++) {
        if (y[k].key != 0) {
            cy++;
        }
    }
    return cx == cy;
}

/* The next order of idx[0..n-1] after this one, 0 after the last. */
static int next_order(int *idx, int n)
{
    int i = n - 2, j, t;

    while (i >= 0 && idx[i] >= idx[i + 1]) {
        i--;
    }
    if (i < 0) {
        return 0;
    }
    j = n - 1;
    while (idx[j] <= idx[i]) {
        j--;
    }
    t = idx[i];
    idx[i] = idx[j];
    idx[j] = t;
    for (i++, j = n - 1; i < j; i++, j--) {
        t = idx[i];
        idx[i] = idx[j];
        idx[j] = t;
    }
    return 1;
}

/* In how many orders of box[0..n-1] (every one, or 300 drawn at
 * random) es_seams_find gives other seams than in the order given. */
static int orders_disagree(const ESSeamBox *box, int n, int every)
{
    static ESSeamBox perm[16];
    static ESSeam first[64], seams[64];
    int idx[16];
    int nf, total, i, rounds = 0, bad = 0;

    nf = es_seams_find(box, n, 8, first, 64);
    for (i = 0; i < n; i++) {
        idx[i] = i;
    }
    for (;;) {
        if (every) {
            if (rounds > 0 && !next_order(idx, n)) {
                break;
            }
        } else {
            if (rounds == 300) {
                break;
            }
            for (i = n - 1; i > 0; i--) {
                int j = lcg(i + 1), t = idx[i];

                idx[i] = idx[j];
                idx[j] = t;
            }
        }
        for (i = 0; i < n; i++) {
            perm[i] = box[idx[i]];
        }
        total = es_seams_find(perm, n, 8, seams, 64);
        if (total != nf || !same_grouped_seams(first, nf, seams, total)) {
            bad++;
        }
        rounds++;
    }
    return bad;
}

/* Is there a seam at pos on that axis with exactly these sides? */
static int seam_there(const ESSeamBox *in, int n, int vertical, int pos,
                      void *const *a, int na, void *const *b, int nb)
{
    static ESSeam seams[64];
    int total = es_seams_find(in, n, 8, seams, 64), k, i, ok;

    for (k = 0; k < total && k < 64; k++) {
        const ESSeam *t = &seams[k];

        if (t->vertical != vertical || t->pos != pos ||
            t->a.n != na || t->b.n != nb) {
            continue;
        }
        ok = 1;
        for (i = 0; i < na; i++) {
            ok = ok && on_side(&t->a, a[i]);
        }
        for (i = 0; i < nb; i++) {
            ok = ok && on_side(&t->b, b[i]);
        }
        if (ok) {
            return 1;
        }
    }
    return 0;
}

/*
 * Which seams a group has depends on its windows, never on the order of
 * the list: the library builds the list from its members, and taking
 * one out reorders the rest. Before, a candidate line was passed over
 * when a nearby one had been looked at first, and a seam refused there
 * kept its windows from the line that held all of it, so each of these
 * layouts (from the reviews) lost a seam in some orders: two columns
 * split 10 px apart with one window 4 px short; a window faced by three
 * stacked ones, each 10 px further on; a layout es_fit_rect made at a
 * gap of 21; and one at a gap of 13.
 */
static void test_grouped_seams_do_not_depend_on_order(void)
{
    ESSeamBox in[16];
    void *a[3], *b[3];
    int w[16];
    int i;

    in[0] = seam_box(&w[0], rect(0, 33, 950, 473), 7, 20);
    in[1] = seam_box(&w[1], rect(0, 530, 950, 550), 7, 20);
    in[2] = seam_box(&w[2], rect(970, 33, 950, 487), 7, 20);
    in[3] = seam_box(&w[3], rect(970, 540, 950, 540), 7, 20);
    CHECK(es_seams_find(in, 4, 8, 0, 0) == 4);
    CHECK(orders_disagree(in, 4, 1) == 0);
    a[0] = &w[0];
    b[0] = &w[1];
    CHECK(seam_there(in, 4, 0, 516, a, 1, b, 1));

    in[0] = seam_box(&w[0], rect(0, 0, 110, 300), 7, 0);
    in[1] = seam_box(&w[1], rect(100, 0, 400, 100), 7, 0);
    in[2] = seam_box(&w[2], rect(110, 100, 390, 100), 7, 0);
    in[3] = seam_box(&w[3], rect(120, 200, 380, 100), 7, 0);
    CHECK(orders_disagree(in, 4, 1) == 0);
    a[0] = &w[0];
    b[0] = &w[1];
    b[1] = &w[2];
    b[2] = &w[3];
    CHECK(seam_there(in, 4, 1, 110, a, 1, b, 3));

    in[0] = seam_box(&w[0], rect(0, 33, 1199, 223), 7, 21);
    in[1] = seam_box(&w[1], rect(1220, 33, 89, 76), 7, 21);
    in[2] = seam_box(&w[2], rect(1220, 260, 700, 59), 7, 21);
    in[3] = seam_box(&w[3], rect(0, 269, 1199, 811), 7, 21);
    in[4] = seam_box(&w[4], rect(1330, 33, 590, 206), 7, 21);
    in[5] = seam_box(&w[5], rect(1209, 334, 711, 746), 7, 21);
    in[6] = seam_box(&w[6], rect(1220, 130, 89, 109), 7, 21);
    CHECK(es_seams_find(in, 7, 8, 0, 0) == 7);
    CHECK(orders_disagree(in, 7, 1) == 0);
    a[0] = &w[0];
    b[0] = &w[3];
    CHECK(seam_there(in, 7, 0, 259, a, 1, b, 1));

    {
        static const int box[14][4] = {
            {0, 33, 346, 380}, {0, 786, 1928, 50}, {359, 33, 832, 560},
            {0, 426, 346, 287}, {0, 906, 53, 174}, {1204, 33, 589, 740},
            {1806, 33, 114, 740}, {116, 897, 1804, 183},
            {359, 606, 832, 167}, {0, 726, 236, 58}, {246, 726, 100, 47},
            {66, 902, 54, 178}, {0, 846, 1642, 47}, {1645, 846, 275, 59}
        };

        for (i = 0; i < 14; i++) {
            in[i] = seam_box(&w[i], rect(box[i][0], box[i][1], box[i][2],
                                         box[i][3]), 1, 13);
        }
    }
    g_lcg = 13UL;
    CHECK(orders_disagree(in, 14, 0) == 0);
    a[0] = &w[12];
    a[1] = &w[13];
    b[0] = &w[4];
    b[1] = &w[7];
    b[2] = &w[11];
    CHECK(seam_there(in, 14, 0, 900, a, 2, b, 3));

    /* even boxes of one key that do not carry one gap, which the
     * library never lists: the two rows propose the same line, each
     * with its own gap; it is looked at once, with the smaller gap, so
     * only the row that carries that gap is a seam there */
    in[0] = seam_box(&w[0], cell(0, 33, 960, 523, 20), 7, 20);
    in[1] = seam_box(&w[1], cell(960, 33, 960, 523, 20), 7, 20);
    in[2] = seam_box(&w[2], cell(0, 556, 960, 524, 10), 7, 10);
    in[3] = seam_box(&w[3], cell(960, 556, 960, 524, 10), 7, 10);
    CHECK(orders_disagree(in, 4, 1) == 0);
    a[0] = &w[2];
    b[0] = &w[3];
    CHECK(seam_there(in, 4, 1, 960, a, 1, b, 1));
    a[0] = &w[0];
    b[0] = &w[1];
    CHECK(!seam_there(in, 4, 1, 960, a, 1, b, 1));
}

#define RND_CELLS 16

/*
 * Cut one of the n cells in two: at a random place, on a grid of 60 px
 * that every cell shares, or near a cut made before on the same axis,
 * so that boundaries line up or nearly do. cuts[] keeps the cuts made,
 * per axis, ncuts[] how many.
 */
static int random_cut(ESRect *cells, int n, int cuts[2][RND_CELLS],
                      int *ncuts)
{
    int k = lcg(n);
    int vertical = lcg(2);
    ESRect *c = &cells[k];
    int lo = vertical ? c->x : c->y;
    int len = vertical ? c->w : c->h;
    int at, how = lcg(3);

    if (len < 4) {
        return n;
    }
    if (how == 0 || ncuts[vertical] == 0) {
        at = lo + 1 + lcg(len - 1);
    } else if (how == 1) {
        at = ((lo + 1 + lcg(len - 1)) / 60) * 60;
    } else {
        at = cuts[vertical][lcg(ncuts[vertical])] + lcg(25) - 12;
    }
    if (at <= lo || at >= lo + len) {
        return n;
    }
    cuts[vertical][ncuts[vertical]++] = at;
    cells[n] = *c;
    if (vertical) {
        c->w = at - lo;
        cells[n].x = at;
        cells[n].w = lo + len - at;
    } else {
        c->h = at - lo;
        cells[n].y = at;
        cells[n].h = lo + len - at;
    }
    return n + 1;
}

/* Do two windows share any stretch along a seam on this axis? */
static int overlapping(const ESRect *p, const ESRect *q, int vertical)
{
    return vertical ? p->y < q->y + q->h && q->y < p->y + p->h
                    : p->x < q->x + q->w && q->x < p->x + p->w;
}

static int lead_of(const ESRect *r, int vertical)
{
    return vertical ? r->x : r->y;
}

static int trail_of(const ESRect *r, int vertical)
{
    return vertical ? r->x + r->w : r->y + r->h;
}

/* How far apart two windows stand across a seam on this axis; below 0
 * when they overlap across it. */
static int apart(const ESRect *p, const ESRect *q, int vertical)
{
    int d1 = lead_of(q, vertical) - trail_of(p, vertical);
    int d2 = lead_of(p, vertical) - trail_of(q, vertical);

    return d1 > d2 ? d1 : d2;
}

/*
 * Dragged to pos, does the seam run one of its windows into another
 * window of its scope and key that it did not overlap before (one of
 * the seam's own included), or leave one outside the seam that it
 * shares a stretch with less of a gap than the group's, or than they
 * had before if that was less? Every pair of the scope and key is
 * looked at, with the boxes es_gutter_apply gives. Returns how many
 * pairs fail.
 */
static int drag_runs_into(const ESSeam *s, int pos, const ESSeamBox *in,
                          int n)
{
    static ESRect ra[ES_SEAM_SIDE_MAX], rb[ES_SEAM_SIDE_MAX];
    static ESRect now[2 * RND_CELLS];
    static int moved[2 * RND_CELLS];
    int v = s->vertical, bad = 0, i, j, k;

    es_gutter_apply(s, pos, ra, rb);
    for (i = 0; i < n; i++) {
        now[i] = in[i].box;
        moved[i] = 0;
        for (k = 0; k < s->a.n; k++) {
            if (in[i].ref == s->a.ref[k]) {
                now[i] = ra[k];
                moved[i] = 1;
            }
        }
        for (k = 0; k < s->b.n; k++) {
            if (in[i].ref == s->b.ref[k]) {
                now[i] = rb[k];
                moved[i] = 1;
            }
        }
    }
    for (i = 0; i < n; i++) {
        if (in[i].key != s->key || in[i].scope != s->scope) {
            continue;
        }
        for (j = i + 1; j < n; j++) {
            int was;

            if (in[j].key != s->key || in[j].scope != s->scope ||
                (!moved[i] && !moved[j])) {
                continue;
            }
            bad += !boxes_overlap(&in[i].box, &in[j].box) &&
                   boxes_overlap(&now[i], &now[j]);
            if (moved[i] == moved[j] ||
                !overlapping(&in[i].box, &in[j].box, v)) {
                continue;
            }
            was = apart(&in[i].box, &in[j].box, v);
            if (was >= 0 && apart(&now[i], &now[j], v) <
                    (was < s->gap ? was : s->gap)) {
                bad++;
            }
        }
    }
    return bad;
}

/*
 * The promise es_seam_by_refs rests on, over layouts a tiler could
 * make: random cuts of the area, made into boxes by es_cell_rect with a
 * random gap of 0 to 40, some fitted to a larger minimum or rounded by a
 * few pixels, in one or two groups and on one or two screens, in a
 * random order. Every grouped seam es_seams_find reports is built again
 * from its windows, in any order of them and of the list, at the same
 * position and with the same limits; two orders of the list give the
 * same seams; a drag keeps its gap and leaves every window its floor,
 * or moves nothing when there is no room; a drag to either limit or to
 * the position runs no window into another of its scope and key, nor
 * narrows the gap to one outside the seam (drag_runs_into); and no
 * window is on the same side of two seams of one axis. A fixed seed
 * keeps every run the same.
 */
static void test_grouped_seams_rebuild_in_any_order(void)
{
    static ESSeamBox in[2 * RND_CELLS], perm[2 * RND_CELLS];
    static ESSeam seams[ES_SEAM_BOXES_MAX * 2], kept[ES_SEAM_BOXES_MAX * 2];
    static ESSeam built;
    static int refs[2 * RND_CELLS], scope1, scope2;
    static ESRect ra[ES_SEAM_SIDE_MAX], rb[ES_SEAM_SIDE_MAX];
    void *a[ES_SEAM_SIDE_MAX], *b[ES_SEAM_SIDE_MAX];
    long checked = 0, stuck = 0, outside = 0, layouts;
    int bad = 0, ran = 0, nkept = 0;

    g_lcg = 20261016UL;
    for (layouts = 0; layouts < 6000; layouts++) {
        ESRect usable, area, cells[RND_CELLS];
        int cuts[2][RND_CELLS];
        int ncuts[2];
        int margin[4];
        int gap = lcg(41), gap2 = lcg(41);
        int nc = 1, n, total, i, j, k, round;
        int two_keys = lcg(4) == 0, two_scopes = lcg(4) == 0;

        usable = lcg(5) == 0
            ? rect(0, 33, 40 + lcg(400), 30 + lcg(300))
            : rect(0, 33, 1920, 1047);
        for (i = 0; i < 4; i++) {
            margin[i] = lcg(3) == 0 ? lcg(21) : 0;
        }
        cells[0] = usable;
        ncuts[0] = 0;
        ncuts[1] = 0;
        for (i = 1 + lcg(RND_CELLS - 1); i > 0 && nc < RND_CELLS; i--) {
            nc = random_cut(cells, nc, cuts, ncuts);
        }
        n = 0;
        for (i = 0; i < nc; i++) {
            ESRect box;
            int key = two_keys && lcg(2) ? 2 : 1;

            es_cell_rect(&cells[i], &usable, key == 2 ? gap2 : gap, margin,
                         &box, &area);
            if (lcg(4) == 0) {
                ESRect want = box;

                es_fit_rect(&want, &area, box.w + lcg(13), box.h + lcg(13),
                            0, 0, &box);
            }
            if (lcg(6) == 0) {
                box.x += lcg(7) - 3;
                box.y += lcg(7) - 3;
                box.w += lcg(7) - 3;
                box.h += lcg(7) - 3;
                if (box.w < 1) {
                    box.w = 1;
                }
                if (box.h < 1) {
                    box.h = 1;
                }
            }
            in[n] = seam_box(&refs[n], box, (unsigned long)key,
                             key == 2 ? gap2 : gap);
            if (two_scopes && lcg(2)) {
                in[n].scope = &scope1;
            }
            if (lcg(10) == 0) {
                in[n].key = 0;
            }
            n++;
        }
        /* the same layout on a second screen, now and then */
        if (lcg(6) == 0) {
            for (i = 0; i < nc; i++) {
                in[n] = in[i];
                in[n].ref = &refs[n];
                in[n].scope = &scope2;
                n++;
            }
        }

        for (round = 0; round < 2; round++) {
            /* a random order of the list */
            for (i = 0; i < n; i++) {
                perm[i] = in[i];
            }
            for (i = n - 1; i > 0; i--) {
                ESSeamBox t;

                j = lcg(i + 1);
                t = perm[i];
                perm[i] = perm[j];
                perm[j] = t;
            }
            total = es_seams_find(perm, n, 8, seams, ES_SEAM_BOXES_MAX * 2);
            if (total > ES_SEAM_BOXES_MAX * 2) {
                bad++;
                continue;
            }
            /* the same seams as the order before */
            if (round == 0) {
                for (k = 0; k < total; k++) {
                    kept[k] = seams[k];
                }
                nkept = total;
            } else if (!same_grouped_seams(kept, nkept, seams, total)) {
                bad++;
            }
            for (k = 0; k < total; k++) {
                const ESSeam *s = &seams[k];
                int want, got, list;

                if (s->key == 0) {
                    continue;
                }
                /* no window on the same side of two seams of one axis */
                for (j = k + 1; j < total; j++) {
                    if (seams[j].vertical != s->vertical ||
                        seams[j].key == 0) {
                        continue;
                    }
                    for (i = 0; i < s->a.n; i++) {
                        if (on_side(&seams[j].a, s->a.ref[i])) {
                            bad++;
                        }
                    }
                    for (i = 0; i < s->b.n; i++) {
                        if (on_side(&seams[j].b, s->b.ref[i])) {
                            bad++;
                        }
                    }
                }
                /* a drag keeps the gap between every pair of sides and
                 * the floor of every window; with no room it moves none */
                es_gutter_apply(s, s->pos + lcg(201) - 100, ra, rb);
                for (i = 0; i < s->a.n; i++) {
                    if (s->min_pos > s->max_pos) {
                        bad += !same_rect(&ra[i], &s->a.box[i]);
                        continue;
                    }
                    bad += trail_of(&ra[i], s->vertical) -
                           lead_of(&ra[i], s->vertical) <
                           (s->gap > ES_SEAM_TOUCH_TOL ? 1 :
                            ES_SEAM_TOUCH_TOL + 1 - s->gap);
                    for (j = 0; j < s->b.n; j++) {
                        int d = s->vertical ? rb[j].x - (ra[i].x + ra[i].w)
                                            : rb[j].y - (ra[i].y + ra[i].h);

                        if (d != s->gap) {
                            bad++;
                        }
                    }
                }
                if (s->min_pos <= s->max_pos) {
                    ran += drag_runs_into(s, s->min_pos, perm, n);
                    ran += drag_runs_into(s, s->max_pos, perm, n);
                    ran += drag_runs_into(s, s->pos, perm, n);
                    outside += s->pos < s->min_pos || s->pos > s->max_pos;
                }
                want = s->min_pos <= s->max_pos ? 1 : -1;
                stuck += want < 0;
                /* rebuilt from the found list and from another order */
                for (list = 0; list < 2; list++) {
                    const ESSeamBox *from = list == 0 ? perm : in;

                    for (i = 0; i < s->a.n; i++) {
                        a[i] = s->a.ref[i];
                    }
                    for (i = 0; i < s->b.n; i++) {
                        b[i] = s->b.ref[i];
                    }
                    shuffle_refs(a, s->a.n);
                    shuffle_refs(b, s->b.n);
                    got = es_seam_by_refs(from, n, 8, s->vertical, a, s->a.n,
                                          b, s->b.n, &built);
                    checked++;
                    if (got != want || built.pos != s->pos ||
                        built.min_pos != s->min_pos ||
                        built.max_pos != s->max_pos ||
                        !same_rect(&built.rect, &s->rect) ||
                        built.gap != s->gap || built.key != s->key ||
                        built.scope != s->scope ||
                        built.vertical != s->vertical) {
                        bad++;
                        continue;
                    }
                    for (i = 0; i < s->a.n; i++) {
                        if (built.a.ref[i] != a[i]) {
                            bad++;
                        }
                    }
                    for (i = 0; i < s->b.n; i++) {
                        if (built.b.ref[i] != b[i]) {
                            bad++;
                        }
                    }
                }
            }
        }
    }
    CHECK(bad == 0);
    CHECK(ran == 0);
    CHECK(checked >= 5000);     /* about a hundred thousand, in fact */
    CHECK(stuck > 0);           /* and some of them with no room */
    CHECK(outside > 0);         /* or with the position outside */
}

/* Widely separated coordinates must never look like touching edges.
 * Every edge itself fits: only the distance between them can overflow. */
static void test_extreme_edges_are_not_near(void)
{
    ESRegistry reg;
    ESSeam seam;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect a, b;
    int wa, wb, vertical, reverse;

    for (vertical = 0; vertical <= 1; vertical++) {
        for (reverse = 0; reverse <= 1; reverse++) {
            a = rect(0, 0, 64, 64);
            b = a;
            if (vertical) {
                a.x = reverse ? INT_MAX - 64 : INT_MIN;
                b.x = reverse ? INT_MIN : INT_MAX - 64;
            } else {
                a.y = reverse ? INT_MAX - 64 : INT_MIN;
                b.y = reverse ? INT_MIN : INT_MAX - 64;
            }
            es_registry_init(&reg);
            CHECK(es_registry_remember(&reg, &wa, &pre, &a,
                                       ES_ZONE_LEFT) == ES_OK);
            CHECK(es_registry_remember(&reg, &wb, &pre, &b,
                                       ES_ZONE_RIGHT) == ES_OK);
            CHECK(es_gutter_find(&reg, 8, &seam) == 0);
        }
    }
}

int main(void)
{
    test_extreme_edges_are_not_near();
    test_finds_the_vertical_seam();
    test_drag_resizes_both();
    test_drag_is_clamped();
    test_horizontal_seam();
    test_no_seam_when_not_touching();
    test_no_seam_for_a_stacked_pair();
    test_pair_fill_respects_the_window_limits();
    test_single_window_has_no_seam();
    test_zone_fills_the_remaining_space();
    test_one_window_faces_two();
    test_horizontal_seam_keeps_to_its_side();
    test_four_quadrants();
    test_seam_can_be_walked_across_the_screen();
    test_side_overflow_gives_no_seam();
    test_refused_line_is_not_tried_again();
    test_grouped_halves_keep_their_gap();
    test_grouped_strip_is_the_gap();
    test_grouped_quadrants_with_an_odd_gap();
    test_grouped_master_and_stack();
    test_grouped_splits_in_two_columns();
    test_grouped_neighbouring_splits();
    test_keys_and_scopes_keep_apart();
    test_fitted_boxes_agree_in_every_order();
    test_by_refs_builds_the_seam();
    test_by_refs_ignores_an_unrelated_row();
    test_by_refs_refuses_a_window_that_joined();
    test_by_refs_reports_no_room();
    test_by_refs_refuses_bad_lists();
    test_tighten_side_keeps_the_gap();
    test_grouped_thin_window_gives_no_seam();
    test_grouped_window_behind_another_gives_no_seam();
    test_grouped_window_reaching_into_the_gap();
    test_grouped_window_short_of_a_reaching_one();
    test_grouped_window_beyond_the_seam();
    test_grouped_strip_covers_linked_stretches();
    test_grouped_misaligned_splits();
    test_grouped_quadrants_after_a_drag();
    test_grouped_refusal_keeps_nothing();
    test_grouped_seams_in_line_order();
    test_gaps_as_the_core_reads_them();
    test_seams_find_counts_past_max();
    test_grouped_seams_do_not_depend_on_order();
    test_grouped_seams_rebuild_in_any_order();

    if (g_failures == 0) {
        printf("gutter_test: all tests passed\n");
        return 0;
    }
    printf("gutter_test: %d failure(s)\n", g_failures);
    return 1;
}
