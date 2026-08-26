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
    es_panel_insets(&SCREEN, p, 1, &ins);
    CHECK(ins.b == 80 && ins.l == 0 && ins.r == 0 && ins.t == 0);
}

static void test_side_docks(void)
{
    ESRect p[2];
    ESInsets ins;

    p[0] = rect(0, 200, 72, 600);      /* left panel   */
    p[1] = rect(1280 - 64, 100, 64, 700); /* right panel  */
    es_panel_insets(&SCREEN, p, 2, &ins);
    CHECK(ins.l == 72 && ins.r == 64 && ins.t == 0 && ins.b == 0);
}

static void test_top_panel_and_deepest_wins(void)
{
    ESRect p[2];
    ESInsets ins;

    p[0] = rect(0, 0, 1280, 28);   /* full-width top bar    */
    p[1] = rect(300, 0, 700, 44);  /* deeper top dock       */
    es_panel_insets(&SCREEN, p, 2, &ins);
    CHECK(ins.t == 44);
}

static void test_corner_widget_ignored(void)
{
    ESRect p[1];
    ESInsets ins;

    /* small borderless clock in a corner: too short to be a bar */
    p[0] = rect(1180, 920, 100, 40);
    es_panel_insets(&SCREEN, p, 1, &ins);
    CHECK(ins.b == 0 && ins.r == 0);
}

static void test_mid_screen_window_ignored(void)
{
    ESRect p[1];
    ESInsets ins;

    /* borderless splash in the middle: flush with no edge */
    p[0] = rect(400, 300, 480, 200);
    es_panel_insets(&SCREEN, p, 1, &ins);
    CHECK(ins.l == 0 && ins.t == 0 && ins.r == 0 && ins.b == 0);
}

static void test_thick_window_ignored(void)
{
    ESRect p[1];
    ESInsets ins;

    /* a half-screen borderless window is not a panel */
    p[0] = rect(0, 480, 1280, 480);
    es_panel_insets(&SCREEN, p, 1, &ins);
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
    es_panel_insets(&SCREEN, p, 1, &ins);
    CHECK(ins.l == 4);
}

int main(void)
{
    test_bottom_dock();
    test_side_docks();
    test_top_panel_and_deepest_wins();
    test_corner_widget_ignored();
    test_mid_screen_window_ignored();
    test_thick_window_ignored();
    test_thin_preview_bar_shape();

    if (g_failures == 0) {
        printf("panels_test: all tests passed\n");
        return 0;
    }
    printf("panels_test: %d failure(s)\n", g_failures);
    return 1;
}
