/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * engine_test.c - executable specification of the drag/snap engine.
 * Simulates full input sequences and checks every emitted action.
 */

#include <stdio.h>

#include "engine.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

/* A 640x480 screen with a 12px title bar. */
static void std_facts(ESWinFacts *f, void *ref)
{
    f->ref = ref;
    f->box.x = 100;
    f->box.y = 100;
    f->box.w = 200;
    f->box.h = 150;
    f->usable.x = 0;
    f->usable.y = 12;
    f->usable.w = 640;
    f->usable.h = 468;
    f->mouse_x = 200;
    f->mouse_y = 110;
    f->min_w = 0;
    f->min_h = 0;
    f->max_w = 0;
    f->max_h = 0;
    f->bar_h = 0;                /* no bar geometry: classic tests  */
    f->flags = ES_WF_SNAPPABLE | ES_WF_DRAGBAR;
    f->push_x = 0;
    f->push_y = 0;
}

/* Baseline motion, then a correlated window+pointer move: engine must
 * consider the window dragged afterwards. */
static void start_drag(ESEngine *e, ESWinFacts *f)
{
    ESEngineActions a;

    es_engine_press(e, -1, -1, &a);
    es_engine_motion(e, f, &a);          /* baseline */
    f->box.x += 30;
    f->box.y += 5;
    f->mouse_x += 30;
    f->mouse_y += 5;
    es_engine_motion(e, f, &a);
    CHECK(a.drag_started == 1);
}

static void test_happy_left_snap(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);

    /* into the left edge: preview of the fitted left half */
    f.mouse_x = 3;
    f.mouse_y = 240;
    f.box.x = -97;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_LEFT);
    CHECK(a.show_preview == 1 && a.hide_preview == 0);
    CHECK(a.preview_ref == (void *)&w1);
    CHECK(a.preview_rect.x == 0 && a.preview_rect.y == 12);
    CHECK(a.preview_rect.w == 320 && a.preview_rect.h == 468);

    /* same zone: no re-emission */
    f.mouse_x = 5;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 0 && a.show_preview == 0 &&
          a.hide_preview == 0);

    /* release: hide + snap with the previewed geometry */
    es_engine_release(&e, &a);
    CHECK(a.hide_preview == 1);
    CHECK(a.do_snap == 1 && a.snap_ref == (void *)&w1);
    CHECK(a.snap_zone == ES_ZONE_LEFT);
    CHECK(a.snap_rect.w == 320 && a.snap_rect.h == 468);

    /* engine is idle again */
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0 && a.hide_preview == 0);
}

static void test_click_without_drag(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f, &a);
    /* pointer wiggles but the window does not move: not a drag */
    f.mouse_x += 20;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);
    f.mouse_x = 2; /* pointer parks on the edge - still not dragging */
    es_engine_motion(&e, &f, &a);
    CHECK(a.show_preview == 0);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

static void test_app_moves_window_alone(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f, &a);
    /* window teleports while the pointer stands still: not a drag */
    f.box.x += 120;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

static void test_no_dragbar_never_drags(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.flags = ES_WF_SNAPPABLE; /* no ES_WF_DRAGBAR */
    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f, &a);
    f.box.x += 30;
    f.mouse_x += 30;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);
}

static void test_zone_exit_hides_preview(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.show_preview == 1);
    /* back to the interior */
    f.mouse_x = 300;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_NONE);
    CHECK(a.hide_preview == 1 && a.show_preview == 0);
    /* release in the interior: no snap */
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

static void test_min_width_clamp_anchors_right(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.min_w = 400; /* wider than the 320px half */
    start_drag(&e, &f);
    f.mouse_x = 638;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.show_preview == 1 && a.zone == ES_ZONE_RIGHT);
    CHECK(a.preview_rect.w == 400);
    CHECK(a.preview_rect.x + a.preview_rect.w == 640); /* glued right */
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_rect.w == 400);
}

static void test_candidate_switch_rebaselines(void)
{
    ESEngine e;
    ESWinFacts f1, f2;
    ESEngineActions a;
    int w1, w2;

    es_engine_init(&e, 0);
    std_facts(&f1, &w1);
    std_facts(&f2, &w2);
    f2.box.x = 400;

    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f1, &a);
    /* active window changes mid-press: baseline moves to w2 */
    f2.mouse_x = f1.mouse_x + 30;
    es_engine_motion(&e, &f2, &a);
    CHECK(a.drag_started == 0);
    /* w2's box differs from w1's but that must not count as movement */
    es_engine_motion(&e, &f2, &a);
    CHECK(a.drag_started == 0);
}

static void test_facts_lost_mid_press(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    es_engine_motion(&e, 0, &a); /* active window gone */
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

static void test_unsnappable_no_preview_no_snap(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.flags = ES_WF_DRAGBAR; /* draggable but not snappable */
    start_drag(&e, &f);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_LEFT);
    CHECK(a.show_preview == 0 && a.hide_preview == 1);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

static void test_reset_hides_preview(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.show_preview == 1);
    es_engine_reset(&e, &a);
    CHECK(a.hide_preview == 1);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}


/*
 * A system that drags windows as an OUTLINE does not move the window
 * until the button is released (AROS One, 2026-09-03). The window box
 * therefore never changes during the drag, and "window moved while the
 * pointer moved" is never true. A press on the drag bar plus pointer
 * travel must count as a drag on its own evidence, preview and all,
 * and the release must snap.
 */
/*
 * The press lands on the bar of a window that is not active yet. The
 * first facts still name the old window, elsewhere on the screen; the
 * pressed one is first seen with the pointer already 60 px below the
 * bar. The press position decides, so the drag still starts.
 */
static void test_late_activation_keeps_the_bar_press(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, w2;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;
    es_engine_press(&e, f.mouse_x, 110, &a);   /* on w2's bar, y 100..119 */

    f.box.y = 400;                  /* the old window: bar nowhere near */
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);

    std_facts(&f, &w2);             /* activation done, pointer lower */
    f.bar_h = 20;
    f.mouse_y = 170;
    es_engine_motion(&e, &f, &a);   /* baseline of w2 from the press */
    CHECK(a.drag_started == 0);

    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 1);
    CHECK(a.zone == ES_ZONE_LEFT && a.show_preview == 1);
}

/* Without a press position the pointer of the first facts decides, as
 * before: a first sight in the body is a press in the body. */
static void test_unknown_press_position_falls_back_to_the_pointer(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;
    f.mouse_y = 170;                /* body */
    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f, &a);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);
}

/* 2.5: an arbitrary rectangle fitted to a window's limits keeps the
 * edge that touches the usable area. */
static void test_fit_rect_keeps_the_touching_edge(void)
{
    ESRect usable = {0, 20, 1024, 673};
    ESRect want = {512, 20, 512, 673};    /* the right half */
    ESRect out;

    es_fit_rect(&want, &usable, 548, 0, 0, 0, &out);
    CHECK(out.w == 548 && out.x == 476);  /* anchored to the right edge */

    want.x = 0;                           /* the left half */
    es_fit_rect(&want, &usable, 548, 0, 0, 0, &out);
    CHECK(out.w == 548 && out.x == 0);

    want.x = 100;                         /* touches neither: keeps x */
    want.w = 300;
    es_fit_rect(&want, &usable, 0, 0, 200, 0, &out);
    CHECK(out.w == 200 && out.x == 100);

    want.y = 300;                         /* the bottom edge, in height */
    want.h = 393;
    es_fit_rect(&want, &usable, 0, 500, 0, 0, &out);
    CHECK(out.h == 500 && out.y == 193);
}

/* 2.5: a layout is applied shrinking first, growing last. */
static void test_order_by_growth_shrinks_first(void)
{
    long d[5] = {100, -50, 0, -200, -50};
    int o[5];

    es_order_by_growth(d, 5, o);
    CHECK(o[0] == 3 && o[1] == 1 && o[2] == 4 && o[3] == 2 && o[4] == 0);
}

static void test_outline_drag_from_the_bar(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;                   /* bar: y 100..119, press at y 110 */
    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f, &a);   /* baseline, on the bar */
    CHECK(a.drag_started == 0);

    f.mouse_x = 3;                  /* box untouched, pointer at the edge */
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 1);
    CHECK(a.zone == ES_ZONE_LEFT && a.show_preview == 1);

    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.hide_preview == 1);
}

/* The same travel with the press inside the window body is not a drag:
 * a selection being dragged in an editor must not snap the editor. */
static void test_outline_press_in_body_is_not_a_drag(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;
    f.mouse_y = 200;                /* inside the body, below the bar */
    es_engine_press(&e, -1, -1, &a);
    es_engine_motion(&e, &f, &a);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

/* A window that may not leave the screen: it stops flush with the
 * left edge and the pointer stays pinned at the grab point, 60 px in.
 * The raw travel keeps pushing left; after push_px of it the engine
 * treats the pointer as being on the edge and previews the left half. */
static void test_pinned_pointer_pushing_at_the_edge(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);

    f.box.x = 0;                    /* flush with the left edge        */
    f.mouse_x = 60;                 /* pinned at the grab point        */
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE);
    for (i = 0; i < 3; i++) {       /* three pushes of 10 px: 30 > 24 */
        f.push_x = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_LEFT);
    CHECK(a.show_preview == 1 && a.preview_rect.x == 0);

    /* let go while pushing: it snaps */
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_LEFT);

    /* the pointer moving on that axis frees it: the count restarts and
     * the zone is left, exactly as a pointer leaving the edge */
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;
    f.mouse_x = 60;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 3; i++) {
        f.push_x = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_LEFT);
    f.mouse_x = 70;
    es_engine_motion(&e, &f, &a);
    CHECK(e.push_acc_x == 0);
    CHECK(a.zone == ES_ZONE_NONE && a.hide_preview == 1);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

/*
 * The same pinned pointer into a corner, with the corners as they come:
 * a corner is the corner itself, so the pointer has to be on both edges
 * at once. A window flush with the left edge and the bottom holds the
 * pointer up at its title bar, far from the bottom: pushing left alone
 * gives the half, pushing on down gives the quarter. At the top the
 * window stops at the usable area's top, the title bar under it, and
 * pushing on up gives the top quarter.
 */
static void test_pinned_pointer_pushing_into_a_corner(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;                    /* flush with the left edge ...    */
    f.box.y = 330;                  /* ... and the bottom: 330+150=480 */
    f.mouse_x = 60;                 /* pinned in the title bar         */
    f.mouse_y = 340;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE);
    for (i = 0; i < 3; i++) {
        f.push_x = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_LEFT);
    for (i = 0; i < 3; i++) {
        f.push_x = -2;
        f.push_y = 10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_BOTTOM_LEFT);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_BOTTOM_LEFT);

    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;
    f.box.y = 12;                   /* flush with the usable area's top */
    f.mouse_x = 60;
    f.mouse_y = 20;                 /* the title bar, under that top    */
    f.push_x = 0;
    f.push_y = 0;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE);
    for (i = 0; i < 3; i++) {
        f.push_x = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_LEFT);
    for (i = 0; i < 3; i++) {
        f.push_x = -2;
        f.push_y = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_TOP_LEFT);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_TOP_LEFT);
}

/*
 * A hand, not a test script (MorphOS, 2026-10-05): pushing left against
 * the edge, half the reports carry only a little vertical travel, and
 * the window, free on that axis, moves with it. The first form of the
 * push started over at each of those and never reached the edge.
 */
static void test_push_survives_a_hand_mixing_the_axes(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i, changes = 0, reached = 0;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;
    f.mouse_x = 60;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 40; i++) {
        if (i % 2 == 0) {
            f.push_x = -3;
            f.push_y = 0;
        } else {
            f.push_x = 0;
            f.push_y = 1;
            f.mouse_y += 1;          /* the window follows down */
            f.box.y += 1;
        }
        es_engine_motion(&e, &f, &a);
        if (a.zone_changed) {
            changes++;
        }
        if (a.zone == ES_ZONE_LEFT && !reached) {
            reached = i + 1;
        }
    }
    CHECK(reached > 0 && reached <= 24);  /* about 24 px of push */
    CHECK(a.zone == ES_ZONE_LEFT);
    CHECK(changes == 1);                  /* reached once, kept  */
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_LEFT);
}

/*
 * A window held on both axes, in the bottom left of the screen, pushed
 * left with the downward drift a hand has: the left half, never the
 * quarter. A push that means the corner, down more than half as much as
 * left, gets it.
 */
static void test_drift_does_not_make_a_corner(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;
    f.box.y = 330;                   /* flush with the bottom too      */
    f.mouse_x = 60;
    f.mouse_y = 340;
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 60; i++) {
        f.push_x = -3;
        f.push_y = (i % 3 == 0) ? 1 : 0; /* drift, one in nine        */
        es_engine_motion(&e, &f, &a);
        CHECK(a.zone != ES_ZONE_BOTTOM_LEFT);
    }
    CHECK(a.zone == ES_ZONE_LEFT);
    for (i = 0; i < 60; i++) {
        f.push_x = -3;
        f.push_y = 1;                /* drift, one in three            */
        es_engine_motion(&e, &f, &a);
        CHECK(a.zone != ES_ZONE_BOTTOM_LEFT);
    }
    for (i = 0; i < 12; i++) {       /* now the corner, meant          */
        f.push_x = -2;
        f.push_y = 4;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_BOTTOM_LEFT);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_BOTTOM_LEFT);

    /* straight from the middle of nowhere into the corner, at the angle
     * of a hand aiming at it: both edges together */
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;
    f.box.y = 330;
    f.mouse_x = 60;
    f.mouse_y = 340;
    f.push_x = 0;
    f.push_y = 0;
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 20; i++) {
        f.push_x = -3;
        f.push_y = 3;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_BOTTOM_LEFT);
    es_engine_release(&e, &a);
}

/*
 * At the wall, a shaking hand: travel back and forth, a little up and
 * down. The zone stays; it neither goes nor comes back.
 */
static void test_push_holds_under_a_shaking_hand(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i, changes = 0;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 0;
    f.mouse_x = 60;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 4; i++) {
        f.push_x = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_LEFT);
    for (i = 0; i < 50; i++) {
        f.push_x = (i % 2 == 0) ? 2 : -2;
        f.push_y = (i % 4 < 2) ? 1 : -1;
        es_engine_motion(&e, &f, &a);
        if (a.zone_changed) {
            changes++;
        }
    }
    CHECK(changes == 0 && a.zone == ES_ZONE_LEFT);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_LEFT);
}

/*
 * With the settings as they come, a window whose title bar is just
 * under the screen's bar is not in the maximize zone when it is
 * dragged: the top edge is the screen's bar. Pushed up into it, it is.
 */
static void test_default_top_is_the_screen_bar(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.box.y = 12;                    /* the window at the top ... */
    f.mouse_y = 22;                  /* ... held by its title bar */
    start_drag(&e, &f);              /* moves it 30 right, 5 down */

    f.mouse_x = 320;
    f.mouse_y = 18;                  /* six below the usable top: 12 */
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE);

    f.mouse_y = 5;                   /* in the screen's bar */
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_MAX);

    f.mouse_y = 12;                  /* the usable area's first row */
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_MAX);

    f.mouse_y = 13;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_NONE);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

/* Pushing while the window is NOT at the edge, or pushing the wrong
 * way, changes nothing: only a wall counts. */
static void test_push_without_a_wall_is_ignored(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);
    f.box.x = 40;                   /* not flush                       */
    f.mouse_x = 60;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 5; i++) {
        f.push_x = -10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_NONE && a.show_preview == 0);
    f.box.x = 0;                    /* flush, but pushing away from it */
    for (i = 0; i < 5; i++) {
        f.push_x = 10;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(a.zone == ES_ZONE_NONE && a.show_preview == 0);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0);
}

/*
 * With the settings as they come, a window dragged sideways by a title
 * bar just under the screen's bar snaps to the half; pushed on up into
 * the screen's bar it takes the quarter, and back down it is a half
 * again.
 */
static void test_default_corner_is_the_corner(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    start_drag(&e, &f);

    f.mouse_x = 2;
    f.mouse_y = 20;                  /* the usable area starts at 12 */
    f.box.x = -98;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_LEFT);

    f.mouse_y = 4;                   /* in the screen's bar */
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_TOP_LEFT);

    f.mouse_y = 30;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_LEFT);

    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_LEFT);
}

/* ------------------------------------- 2.24: let go outside the zones */

static int same_rect(const ESRect *a, const ESRect *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

static int same_actions(const ESEngineActions *a, const ESEngineActions *b)
{
    return a->show_preview == b->show_preview &&
           a->hide_preview == b->hide_preview &&
           a->do_snap == b->do_snap && a->drag_started == b->drag_started &&
           a->zone_changed == b->zone_changed && a->zone == b->zone &&
           same_rect(&a->preview_rect, &b->preview_rect) &&
           a->preview_ref == b->preview_ref && a->snap_ref == b->snap_ref &&
           a->snap_zone == b->snap_zone &&
           same_rect(&a->snap_rect, &b->snap_rect) &&
           a->do_drop == b->do_drop && a->drop_ref == b->drop_ref &&
           same_rect(&a->drop_from, &b->drop_from) &&
           a->drop_grab_x == b->drop_grab_x &&
           a->drop_grab_y == b->drop_grab_y &&
           a->drop_x == b->drop_x && a->drop_y == b->drop_y;
}

/* The std_facts window with a title bar of 20 (y 100..119), pressed on
 * it at (200, 110), and the pointer dragged on the bar until a drag is
 * recognised, the box untouched: an outline drag. */
static void start_bar_drag(ESEngine *e, ESWinFacts *f, void *ref)
{
    ESEngineActions a;

    std_facts(f, ref);
    f->bar_h = 20;
    es_engine_press(e, 200, 110, &a);
    es_engine_motion(e, f, &a);          /* baseline, on the bar */
    f->mouse_x = 260;
    f->mouse_y = 160;
    es_engine_motion(e, f, &a);
    CHECK(a.drag_started == 1 && a.zone == ES_ZONE_NONE);
}

/* An outline drag from the bar let go in the middle of the screen: the
 * drop, with the box of the start, the grab and the pointer. */
static void test_drop_outline_from_the_bar(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    start_bar_drag(&e, &f, &w1);
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE && a.show_preview == 0);

    es_engine_release(&e, &a);
    CHECK(a.hide_preview == 1 && a.do_snap == 0);
    CHECK(a.do_drop == 1 && a.drop_ref == (void *)&w1);
    CHECK(a.drop_from.x == 100 && a.drop_from.y == 100 &&
          a.drop_from.w == 200 && a.drop_from.h == 150);
    CHECK(a.drop_grab_x == 200 && a.drop_grab_y == 110);
    CHECK(a.drop_x == 300 && a.drop_y == 300);

    /* idle again */
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 0 && a.do_snap == 0 && a.hide_preview == 0);
}

/* A live drag from the bar, the window following the pointer: the drop
 * names the pointer of the last motion, and the box of the start. */
static void test_drop_live_from_the_bar(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1, i;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;
    es_engine_press(&e, 200, 110, &a);
    es_engine_motion(&e, &f, &a);
    for (i = 0; i < 4; i++) {
        f.box.x += 40;
        f.box.y += 50;
        f.mouse_x += 40;
        f.mouse_y += 50;
        es_engine_motion(&e, &f, &a);
    }
    CHECK(e.dragging == 1 && a.zone == ES_ZONE_NONE);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 1 && a.do_snap == 0 && a.drop_ref == (void *)&w1);
    CHECK(a.drop_from.x == 100 && a.drop_from.y == 100);
    CHECK(a.drop_grab_x == 200 && a.drop_grab_y == 110);
    CHECK(a.drop_x == 360 && a.drop_y == 310);
}

/* Let go in a zone: the snap, never the drop. In a zone and back out
 * to the middle: the drop. */
static void test_drop_only_outside_the_zones(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    start_bar_drag(&e, &f, &w1);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_LEFT && a.show_preview == 1);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 1 && a.snap_zone == ES_ZONE_LEFT && a.do_drop == 0);

    start_bar_drag(&e, &f, &w1);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_LEFT);
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone_changed == 1 && a.zone == ES_ZONE_NONE);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 1 && a.do_snap == 0);
    CHECK(a.drop_x == 300 && a.drop_y == 300);
}

/* A zone the user switched off is no zone: a snapped window let go
 * there gets its old size back too. */
static void test_drop_on_a_zone_switched_off(void)
{
    ESEngineConfig cfg;
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_config_defaults(&cfg);
    cfg.zones_mask &= ~ES_ZONEBIT(ES_ZONE_LEFT);
    es_engine_init(&e, &cfg);
    start_bar_drag(&e, &f, &w1);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE && a.show_preview == 0);
    es_engine_release(&e, &a);
    CHECK(a.do_snap == 0 && a.do_drop == 1 && a.drop_x == 3);
}

/* What is no drop: a press in the body, even when the window moved with
 * the pointer; the bypass key at the last motion; a window that may not
 * be snapped; a click on the bar that never became a drag; a drag the
 * engine was told to forget. */
static void test_drop_refusals(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    int w1;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;
    f.mouse_y = 200;                     /* the body */
    es_engine_press(&e, 200, 200, &a);
    es_engine_motion(&e, &f, &a);
    f.box.x += 30;
    f.box.y += 5;
    f.mouse_x += 30;
    f.mouse_y += 5;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 1 && e.on_bar == 0);  /* a drag all the same */
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 0 && a.do_snap == 0);

    start_bar_drag(&e, &f, &w1);
    f.mouse_x = 300;
    f.mouse_y = 300;
    f.flags |= ES_WF_BYPASS;
    es_engine_motion(&e, &f, &a);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 0 && a.do_snap == 0);

    /* the key held on the way and let go before the last motion */
    start_bar_drag(&e, &f, &w1);
    f.flags |= ES_WF_BYPASS;
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.zone == ES_ZONE_NONE);
    f.flags &= ~ES_WF_BYPASS;
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 1);

    start_bar_drag(&e, &f, &w1);
    f.flags = ES_WF_DRAGBAR;             /* draggable, not snappable */
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 0 && a.do_snap == 0);

    std_facts(&f, &w1);
    f.bar_h = 20;
    es_engine_press(&e, 200, 110, &a);
    es_engine_motion(&e, &f, &a);
    f.mouse_x = 202;                     /* 3 px of travel, 4 needed */
    f.mouse_y = 111;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 0 && a.do_snap == 0);

    start_bar_drag(&e, &f, &w1);
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    es_engine_reset(&e, &a);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 0 && a.do_snap == 0 && a.hide_preview == 0);
}

/*
 * es_engine_press_on: the active window at the press, and its box then.
 * On AmigaOS 4 and MorphOS the window follows the pointer, and a fast
 * pull can take it 30 px down before the first motion is sampled. Here
 * the first sample has the window at y 130 and the pointer at 140: from
 * that box, the press at y 110 is above the window, not on its bar
 * (100..119 at the press). With the press box the drag is from the bar,
 * so it can be dropped; with es_engine_press and the same input, on_bar
 * comes out 0, the drag still starts because the window moved, and the
 * release drops nothing. Snapping is the same either way.
 */
static void test_press_on_keeps_a_fast_pull_on_the_bar(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    ESRect b0;
    int w1, with;

    for (with = 1; with >= 0; with--) {
        es_engine_init(&e, 0);
        std_facts(&f, &w1);
        f.bar_h = 20;
        b0 = f.box;
        if (with) {
            es_engine_press_on(&e, &w1, &b0, 200, 110, &a);
        } else {
            es_engine_press(&e, 200, 110, &a);
        }
        f.box.y = 130;                   /* pulled down already */
        f.mouse_y = 140;
        es_engine_motion(&e, &f, &a);
        CHECK(a.drag_started == 0 && e.on_bar == with);
        f.box.y = 230;
        f.mouse_y = 240;
        es_engine_motion(&e, &f, &a);
        CHECK(a.drag_started == 1);
        f.box.y = 280;
        f.mouse_y = 290;
        es_engine_motion(&e, &f, &a);
        es_engine_release(&e, &a);
        CHECK(a.do_snap == 0 && a.do_drop == with);
        if (with) {
            CHECK(a.drop_ref == (void *)&w1 && same_rect(&a.drop_from, &b0));
            CHECK(a.drop_grab_x == 200 && a.drop_grab_y == 110);
            CHECK(a.drop_x == 200 && a.drop_y == 290);
        }

        /* the same pull on into the left edge snaps, either way */
        std_facts(&f, &w1);
        f.bar_h = 20;
        b0 = f.box;
        if (with) {
            es_engine_press_on(&e, &w1, &b0, 200, 110, &a);
        } else {
            es_engine_press(&e, 200, 110, &a);
        }
        f.box.y = 130;
        f.mouse_y = 140;
        es_engine_motion(&e, &f, &a);
        f.box.x = -97;
        f.box.y = 230;
        f.mouse_x = 3;
        f.mouse_y = 240;
        es_engine_motion(&e, &f, &a);
        CHECK(a.drag_started == 1 && a.zone == ES_ZONE_LEFT &&
              a.show_preview == 1);
        es_engine_release(&e, &a);
        CHECK(a.do_snap == 1 && a.snap_ref == (void *)&w1 &&
              a.snap_zone == ES_ZONE_LEFT && a.do_drop == 0);

        /* pulled sideways from near the bar's left end: by the first
         * sample the window has gone 30 px right, past the press */
        std_facts(&f, &w1);
        f.bar_h = 20;
        b0 = f.box;
        if (with) {
            es_engine_press_on(&e, &w1, &b0, 110, 110, &a);
        } else {
            es_engine_press(&e, 110, 110, &a);
        }
        f.box.x = 130;
        f.mouse_x = 140;
        es_engine_motion(&e, &f, &a);
        CHECK(e.on_bar == with);
        es_engine_release(&e, &a);
    }
}

/*
 * The press names a window that is not the one dragged: on AROS the
 * pressed window becomes active a beat after the press. The box of the
 * press is that other window's and is not used; the pressed one is
 * judged from its first sight, as in
 * test_late_activation_keeps_the_bar_press, and dropped as itself.
 */
static void test_press_on_another_window_falls_back(void)
{
    ESEngine e;
    ESWinFacts f;
    ESEngineActions a;
    ESRect old;
    int w1, w2;

    es_engine_init(&e, 0);
    std_facts(&f, &w1);
    f.bar_h = 20;
    f.box.y = 400;                       /* the old window */
    old = f.box;
    es_engine_press_on(&e, &w1, &old, 200, 110, &a);
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0 && e.on_bar == 0);

    std_facts(&f, &w2);                  /* activation done, pointer lower */
    f.bar_h = 20;
    f.mouse_y = 170;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 0 && e.on_bar == 1);
    f.mouse_x = 3;
    f.mouse_y = 240;
    es_engine_motion(&e, &f, &a);
    CHECK(a.drag_started == 1 && a.zone == ES_ZONE_LEFT);
    f.mouse_x = 300;
    f.mouse_y = 300;
    es_engine_motion(&e, &f, &a);
    es_engine_release(&e, &a);
    CHECK(a.do_drop == 1 && a.drop_ref == (void *)&w2);
    CHECK(a.drop_from.x == 100 && a.drop_from.y == 100);
    CHECK(a.drop_grab_x == 200 && a.drop_grab_y == 110);

    /* and a press that names no window is es_engine_press */
    es_engine_press_on(&e, 0, &old, 200, 110, &a);
    CHECK(e.press_ref == 0 && e.button_down == 1);
    es_engine_press_on(&e, &w1, 0, 200, 110, &a);
    CHECK(e.press_ref == 0 && e.button_down == 1);
    es_engine_release(&e, &a);
}

/* One step of a straight drag from (200, 110) to (tx, ty), i of n, on
 * the std_facts window with a bar of bar_h: the box follows the pointer
 * when live, and stays put, as with an outline, when not. */
static void path_facts(ESWinFacts *f, void *ref, int bar_h, int live,
                       int tx, int ty, int i, int n)
{
    std_facts(f, ref);
    f->bar_h = bar_h;
    f->mouse_x = 200 + ((tx - 200) * i) / n;
    f->mouse_y = 110 + ((ty - 110) * i) / n;
    if (live) {
        f->box.x += f->mouse_x - 200;
        f->box.y += f->mouse_y - 110;
    }
}

/*
 * Drag snapping must not move with 2.24. When the window stands where it
 * stood at the press until the first motion, which is every case before
 * 2.24 could tell, es_engine_press_on and es_engine_press emit the same
 * actions at every step: for live and outline drags, with and without a
 * bar to press on, into every kind of zone and into none.
 */
#define PATH_STEPS 8

static void test_press_on_changes_nothing_when_the_window_waits(void)
{
    static const int target[6][2] = {
        { 300, 300 }, { 3, 240 }, { 638, 240 }, { 320, 4 }, { 2, 4 },
        { 636, 479 }
    };
    ESEngine e1, e2;
    ESWinFacts f;
    ESEngineActions a1, a2;
    ESRect box0;
    int w1, bar, live, t, i, differ = 0;

    for (bar = 0; bar <= 20; bar += 20) {
        for (live = 0; live < 2; live++) {
            for (t = 0; t < 6; t++) {
                es_engine_init(&e1, 0);
                es_engine_init(&e2, 0);
                path_facts(&f, &w1, bar, live, target[t][0], target[t][1],
                           0, PATH_STEPS);
                box0 = f.box;
                es_engine_press(&e1, 200, 110, &a1);
                es_engine_press_on(&e2, &w1, &box0, 200, 110, &a2);
                differ += !same_actions(&a1, &a2);
                for (i = 0; i <= PATH_STEPS; i++) {
                    path_facts(&f, &w1, bar, live, target[t][0],
                               target[t][1], i, PATH_STEPS);
                    es_engine_motion(&e1, &f, &a1);
                    es_engine_motion(&e2, &f, &a2);
                    differ += !same_actions(&a1, &a2);
                }
                es_engine_release(&e1, &a1);
                es_engine_release(&e2, &a2);
                differ += !same_actions(&a1, &a2);
                /* an outline drag needs the bar; a drop needs it always */
                CHECK(a1.do_snap == (t != 0 && (bar > 0 || live)));
                CHECK(a1.do_drop == (t == 0 && bar > 0));
            }
        }
    }
    CHECK(differ == 0);
}

int main(void)
{
    test_happy_left_snap();
    test_default_corner_is_the_corner();
    test_default_top_is_the_screen_bar();
    test_pinned_pointer_pushing_at_the_edge();
    test_pinned_pointer_pushing_into_a_corner();
    test_push_survives_a_hand_mixing_the_axes();
    test_drift_does_not_make_a_corner();
    test_push_holds_under_a_shaking_hand();
    test_push_without_a_wall_is_ignored();
    test_click_without_drag();
    test_app_moves_window_alone();
    test_no_dragbar_never_drags();
    test_zone_exit_hides_preview();
    test_min_width_clamp_anchors_right();
    test_candidate_switch_rebaselines();
    test_facts_lost_mid_press();
    test_unsnappable_no_preview_no_snap();
    test_reset_hides_preview();
    test_outline_drag_from_the_bar();
    test_outline_press_in_body_is_not_a_drag();
    test_late_activation_keeps_the_bar_press();
    test_unknown_press_position_falls_back_to_the_pointer();
    test_fit_rect_keeps_the_touching_edge();
    test_order_by_growth_shrinks_first();
    test_drop_outline_from_the_bar();
    test_drop_live_from_the_bar();
    test_drop_only_outside_the_zones();
    test_drop_on_a_zone_switched_off();
    test_drop_refusals();
    test_press_on_keeps_a_fast_pull_on_the_bar();
    test_press_on_another_window_falls_back();
    test_press_on_changes_nothing_when_the_window_waits();

    if (g_failures == 0) {
        printf("engine_test: all tests passed\n");
        return 0;
    }
    printf("engine_test: %d failure(s)\n", g_failures);
    return 1;
}
