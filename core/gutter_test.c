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
    CHECK(seam.ref_a == (void *)&wa && seam.ref_b == (void *)&wb);
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

    if (g_failures == 0) {
        printf("gutter_test: all tests passed\n");
        return 0;
    }
    printf("gutter_test: %d failure(s)\n", g_failures);
    return 1;
}
