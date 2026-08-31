/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * panels_test.c - executable specification of the dock/panel
 * edge-reservation policy.
 */

#include <stdio.h>

#include "panels.h"

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

static const ESRect SCREEN = { 0, 0, 1280, 960 };

static void test_bottom_dock(void)
{
    ESRect p[1];
    ESInsets ins;

    /* AmiDock-style bar: bottom center, 600x80 */
    p[0] = rect(340, 880, 600, 80);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.b == 80 + ES_PANEL_MARGIN_PX);
    CHECK(ins.l == 0 && ins.r == 0 && ins.t == 0);
}

static void test_side_docks(void)
{
    ESRect p[2];
    ESInsets ins;

    p[0] = rect(0, 200, 72, 600);      /* left panel   */
    p[1] = rect(1280 - 64, 100, 64, 700); /* right panel  */
    es_panel_insets(&SCREEN, p, 2, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.l == 72 + ES_PANEL_MARGIN_PX);
    CHECK(ins.r == 64 + ES_PANEL_MARGIN_PX);
    CHECK(ins.t == 0 && ins.b == 0);
}

static void test_top_panel_and_deepest_wins(void)
{
    ESRect p[2];
    ESInsets ins;

    p[0] = rect(0, 0, 1280, 28);   /* full-width top bar    */
    p[1] = rect(300, 0, 700, 44);  /* deeper top dock       */
    es_panel_insets(&SCREEN, p, 2, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.t == 44 + ES_PANEL_MARGIN_PX);
}

static void test_floating_centered_dock(void)
{
    ESRect p[1];
    ESInsets ins;

    /* MorphOS field case: compact dock, bottom center, FLOATING 12px
     * above the screen edge. Reserves up to the edge, gap included. */
    p[0] = rect(490, 900, 300, 48);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.b == 60 + ES_PANEL_MARGIN_PX);
    CHECK(ins.l == 0 && ins.r == 0 && ins.t == 0);
}

static void test_raised_dock_outer_band(void)
{
    ESRect p[1];
    ESInsets ins;

    /* dock raised well off the bottom edge but still in the outer
     * quarter of the screen: always reserved, gap included */
    p[0] = rect(490, 700, 300, 48);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.b == 260 + ES_PANEL_MARGIN_PX);
}

static void test_corner_widget_ignored(void)
{
    ESRect p[1];
    ESInsets ins;

    /* small borderless clock in a corner: too short to be a bar */
    p[0] = rect(1180, 920, 100, 40);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.b == 0 && ins.r == 0);
}

static void test_mid_screen_window_ignored(void)
{
    ESRect p[1];
    ESInsets ins;

    /* borderless splash in the middle: flush with no edge */
    p[0] = rect(400, 300, 480, 200);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.l == 0 && ins.t == 0 && ins.r == 0 && ins.b == 0);
}

static void test_thick_window_ignored(void)
{
    ESRect p[1];
    ESInsets ins;

    /* a half-screen borderless window is not a panel */
    p[0] = rect(0, 480, 1280, 480);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.b == 0);
}

static void test_thin_preview_bar_shape(void)
{
    ESRect p[1];
    ESInsets ins;

    /* a 4px vertical strip at the left edge (our own preview bar
     * shape): the glue excludes ours by pointer, but a foreign one
     * still classifies as a left panel - by design, it IS flush,
     * thin and long. Verify the policy is at least consistent. */
    p[0] = rect(0, 12, 4, 468);
    es_panel_insets(&SCREEN, p, 1, ES_PANEL_MARGIN_PX, &ins);
    CHECK(ins.l == 4 + ES_PANEL_MARGIN_PX);
}

/*
 * A desktop may carry more docks than any fixed array would hold. The
 * incremental form exists so the library can walk a window list without
 * one; it must agree with the array form, and the deepest panel on an
 * edge must still win however late in the walk it turns up.
 */
static void test_many_panels_no_limit(void)
{
    ESRect p[40];
    ESInsets ins, incremental;
    int i;

    for (i = 0; i < 40; i++) {
        /* Thin bottom bars, each one shallower than the last except the
         * final one, which is the deepest and arrives last of all. */
        p[i] = rect(0, 940 - i, 1280, 20);
    }
    p[39] = rect(0, 860, 1280, 20);

    es_panel_insets(&SCREEN, p, 40, 0, &ins);
    CHECK(ins.b == 100);          /* 960 - 860, the deepest bar */

    es_panel_begin(&incremental);
    for (i = 0; i < 40; i++) {
        es_panel_add(&SCREEN, &p[i], &incremental);
    }
    es_panel_end(&incremental, 0);
    CHECK(incremental.b == ins.b);
    CHECK(incremental.l == ins.l);
    CHECK(incremental.t == ins.t);
    CHECK(incremental.r == ins.r);
}

/*
 * Panels deep enough to swallow the screen are the library's problem to
 * survive, not this table's - but the policy must at least report them
 * honestly rather than clamping quietly, or the caller cannot tell.
 */
static void test_panels_may_exceed_the_screen(void)
{
    ESRect p[2];
    ESInsets ins;

    p[0] = rect(0, 0, 1280, 200);        /* top bar, 200 deep */
    p[1] = rect(0, 100, 1280, 200);      /* another, deeper still */
    es_panel_insets(&SCREEN, p, 2, 0, &ins);
    CHECK(ins.t == 300);
}

int main(void)
{
    test_bottom_dock();
    test_side_docks();
    test_top_panel_and_deepest_wins();
    test_floating_centered_dock();
    test_raised_dock_outer_band();
    test_corner_widget_ignored();
    test_mid_screen_window_ignored();
    test_thick_window_ignored();
    test_thin_preview_bar_shape();
    test_many_panels_no_limit();
    test_panels_may_exceed_the_screen();

    if (g_failures == 0) {
        printf("panels_test: all tests passed\n");
        return 0;
    }
    printf("panels_test: %d failure(s)\n", g_failures);
    return 1;
}
