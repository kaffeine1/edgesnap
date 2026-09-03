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

int main(void)
{
    test_happy_left_snap();
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

    if (g_failures == 0) {
        printf("engine_test: all tests passed\n");
        return 0;
    }
    printf("engine_test: %d failure(s)\n", g_failures);
    return 1;
}
