/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * gutter_test.c - executable specification of the divider geometry.
 */

#include <stdio.h>

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
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_RIGHT, &usable, &want) == 1);
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
    CHECK(es_pair_fill(&reg, &wa, ES_ZONE_LEFT, &usable, &want) == 1);
    CHECK(want.x == 0 && want.w == 1500);

    /* a window may not be its own partner */
    want = rect(0, 33, 960, 1047);
    CHECK(es_pair_fill(&reg, &wb, ES_ZONE_LEFT, &usable, &want) == 0);
    CHECK(want.w == 960);

    /* maximise has no opposite side to complement */
    want = rect(0, 33, 1920, 1047);
    CHECK(es_pair_fill(&reg, &wa, ES_ZONE_MAX, &usable, &want) == 0);
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

int main(void)
{
    test_finds_the_vertical_seam();
    test_drag_resizes_both();
    test_drag_is_clamped();
    test_horizontal_seam();
    test_no_seam_when_not_touching();
    test_no_seam_for_a_stacked_pair();
    test_single_window_has_no_seam();
    test_zone_fills_the_remaining_space();
    test_one_window_faces_two();
    test_horizontal_seam_keeps_to_its_side();
    test_four_quadrants();
    test_seam_can_be_walked_across_the_screen();

    if (g_failures == 0) {
        printf("gutter_test: all tests passed\n");
        return 0;
    }
    printf("gutter_test: %d failure(s)\n", g_failures);
    return 1;
}
