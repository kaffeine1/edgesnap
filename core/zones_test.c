/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * zones_test.c - host-side unit tests for the C89 zone geometry.
 * Build and run with any C89 compiler; exits 0 on success.
 */

#include <stdio.h>

#include "zones.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static void test_zone_from_pointer(void)
{
    ESRect u;
    u.x = 0;
    u.y = 12;      /* screen bar above */
    u.w = 640;
    u.h = 468;

    /* interior */
    CHECK(es_zone_from_pointer(&u, 320, 240, 12, 100) == ES_ZONE_NONE);
    /* plain edges */
    CHECK(es_zone_from_pointer(&u, 0, 240, 12, 100) == ES_ZONE_LEFT);
    CHECK(es_zone_from_pointer(&u, 639, 240, 12, 100) == ES_ZONE_RIGHT);
    CHECK(es_zone_from_pointer(&u, 320, 12, 12, 100) == ES_ZONE_MAX);
    /* pointer in the screen bar still counts as top */
    CHECK(es_zone_from_pointer(&u, 320, 3, 12, 100) == ES_ZONE_MAX);
    /* corners via the side edges */
    CHECK(es_zone_from_pointer(&u, 2, 40, 12, 100) == ES_ZONE_TOP_LEFT);
    CHECK(es_zone_from_pointer(&u, 2, 470, 12, 100) == ES_ZONE_BOTTOM_LEFT);
    CHECK(es_zone_from_pointer(&u, 638, 40, 12, 100) == ES_ZONE_TOP_RIGHT);
    CHECK(es_zone_from_pointer(&u, 638, 470, 12, 100) == ES_ZONE_BOTTOM_RIGHT);
    /* corners via the top edge */
    CHECK(es_zone_from_pointer(&u, 60, 13, 12, 100) == ES_ZONE_TOP_LEFT);
    CHECK(es_zone_from_pointer(&u, 600, 13, 12, 100) == ES_ZONE_TOP_RIGHT);
    /* bottom interior edge is not a zone */
    CHECK(es_zone_from_pointer(&u, 320, 478, 12, 100) == ES_ZONE_NONE);
}

static void test_zone_rect_tiles(void)
{
    ESRect u, l, r, tl, tr, bl, br, m;
    u.x = 0;
    u.y = 12;
    u.w = 641;    /* odd on purpose */
    u.h = 469;    /* odd on purpose */

    es_zone_rect(ES_ZONE_LEFT, &u, &l);
    es_zone_rect(ES_ZONE_RIGHT, &u, &r);
    CHECK(l.x == 0 && l.y == 12 && l.h == 469);
    CHECK(l.w + r.w == 641);
    CHECK(r.x == l.x + l.w);
    CHECK(r.x + r.w == u.x + u.w);

    es_zone_rect(ES_ZONE_TOP_LEFT, &u, &tl);
    es_zone_rect(ES_ZONE_TOP_RIGHT, &u, &tr);
    es_zone_rect(ES_ZONE_BOTTOM_LEFT, &u, &bl);
    es_zone_rect(ES_ZONE_BOTTOM_RIGHT, &u, &br);
    CHECK(tl.h + bl.h == 469);
    CHECK(bl.y == tl.y + tl.h);
    CHECK(tr.x == tl.x + tl.w);
    CHECK(br.x + br.w == u.x + u.w);
    CHECK(br.y + br.h == u.y + u.h);

    es_zone_rect(ES_ZONE_MAX, &u, &m);
    CHECK(m.x == u.x && m.y == u.y && m.w == u.w && m.h == u.h);
}

static void test_fit_zone_rect(void)
{
    ESRect u, out;
    u.x = 0;
    u.y = 12;
    u.w = 640;
    u.h = 468;

    /* no limits: same as es_zone_rect */
    es_fit_zone_rect(ES_ZONE_LEFT, &u, 0, 0, 0, 0, &out);
    CHECK(out.x == 0 && out.w == 320);

    /* min width wins, left zone stays left-anchored */
    es_fit_zone_rect(ES_ZONE_LEFT, &u, 400, 0, 0, 0, &out);
    CHECK(out.x == 0 && out.w == 400);

    /* min width on a right zone: stays glued to the right edge */
    es_fit_zone_rect(ES_ZONE_RIGHT, &u, 400, 0, 0, 0, &out);
    CHECK(out.w == 400 && out.x + out.w == 640);

    /* max width on a right zone: still glued to the right edge */
    es_fit_zone_rect(ES_ZONE_RIGHT, &u, 0, 0, 200, 0, &out);
    CHECK(out.w == 200 && out.x + out.w == 640);

    /* max height on a bottom quarter: glued to the bottom edge */
    es_fit_zone_rect(ES_ZONE_BOTTOM_RIGHT, &u, 0, 0, 0, 100, &out);
    CHECK(out.h == 100 && out.y + out.h == u.y + u.h);
    CHECK(out.x + out.w == 640);
}

/* Asking for the same side again walks the cycle: half, two thirds,
 * one third, and back. Left and right at complementary steps still
 * tile the usable area exactly, with no gap and no overlap. */
static void test_side_zones_cycle_widths(void)
{
    ESRect u, a, b;

    u.x = 0; u.y = 20; u.w = 1000; u.h = 700;

    es_zone_rect_step(ES_ZONE_LEFT, &u, 0, &a);
    CHECK(a.x == 0 && a.w == 500);
    es_zone_rect_step(ES_ZONE_LEFT, &u, 1, &a);
    CHECK(a.x == 0 && a.w == 666);
    es_zone_rect_step(ES_ZONE_LEFT, &u, 2, &a);
    CHECK(a.x == 0 && a.w == 333);

    es_zone_rect_step(ES_ZONE_RIGHT, &u, 1, &a);   /* two thirds, right */
    CHECK(a.x == 333 && a.w == 667);
    es_zone_rect_step(ES_ZONE_RIGHT, &u, 2, &a);   /* one third, right  */
    CHECK(a.x == 666 && a.w == 334);

    /* two thirds on the left, one third on the right: they meet */
    es_zone_rect_step(ES_ZONE_LEFT, &u, 1, &a);
    es_zone_rect_step(ES_ZONE_RIGHT, &u, 2, &b);
    CHECK(a.x + a.w == b.x);
    CHECK(b.x + b.w == u.x + u.w);

    /* the height never moves, and corners keep their quarters */
    CHECK(a.y == 20 && a.h == 700);
    es_zone_rect_step(ES_ZONE_TOP_LEFT, &u, 1, &a);
    CHECK(a.w == 500 && a.h == 350);
    es_zone_rect_step(ES_ZONE_MAX, &u, 2, &a);
    CHECK(a.w == 1000 && a.h == 700);
}

/* A step still respects a window that cannot be that narrow, and the
 * right side stays anchored to the right edge when it is clamped. */
static void test_stepped_zone_honours_limits(void)
{
    ESRect u, r;

    u.x = 0; u.y = 20; u.w = 1000; u.h = 700;
    es_fit_zone_rect_step(ES_ZONE_RIGHT, &u, 2, 500, 0, 0, 0, &r);
    CHECK(r.w == 500);       /* one third would be 334: the limit wins */
    CHECK(r.x + r.w == u.x + u.w);
}

/* The centre keeps the window's size and puts it in the middle; with an
 * odd remainder the extra pixel goes right and down. */
static void test_centre_keeps_the_size(void)
{
    ESRect u, w, r;

    u.x = 0; u.y = 20; u.w = 1000; u.h = 700;
    w.x = 7; w.y = 500; w.w = 400; w.h = 300;
    es_centre_rect(&u, &w, 0, 0, 0, 0, &r);
    CHECK(r.w == 400 && r.h == 300);
    CHECK(r.x == 300 && r.y == 220);
    CHECK(r.x - u.x == u.x + u.w - (r.x + r.w));     /* exactly centred */

    w.w = 401; w.h = 301;
    es_centre_rect(&u, &w, 0, 0, 0, 0, &r);
    CHECK(r.x == 299 && r.w == 401);                 /* odd pixel to the right */
    CHECK(u.x + u.w - (r.x + r.w) == 300);
    CHECK(r.y == 219 && r.h == 301);
}

/* A window larger than the area is cut down to it, and the limits win
 * over the area: one that cannot be that narrow stays wider. */
static void test_centre_honours_area_and_limits(void)
{
    ESRect u, w, r;

    u.x = 0; u.y = 20; u.w = 1000; u.h = 700;
    w.x = -50; w.y = 0; w.w = 1400; w.h = 900;
    es_centre_rect(&u, &w, 0, 0, 0, 0, &r);
    CHECK(r.x == 0 && r.y == 20 && r.w == 1000 && r.h == 700);

    w.w = 300; w.h = 200;
    es_centre_rect(&u, &w, 500, 0, 0, 0, &r);
    CHECK(r.w == 500 && r.x == 250);                 /* min width wins */
    es_centre_rect(&u, &w, 0, 0, 200, 0, &r);
    CHECK(r.w == 200 && r.x == 400);                 /* max width wins */

    w.w = 1400; w.h = 300;
    es_centre_rect(&u, &w, 1200, 0, 0, 0, &r);
    CHECK(r.w == 1200);                              /* wider than the area */
    CHECK(r.x == u.x);                               /* as centred as it can */

    /* the logs name it: a zone nobody can name reads as "?" */
    CHECK(es_zone_name(ES_ZONE_CENTRE)[0] == 'c');
}

static void test_glide_moves_and_lands(void)
{
    ESRect from, to, r, prev;
    int steps, i;

    from.x = 0; from.y = 20; from.w = 400; from.h = 300;
    to.x = 512; to.y = 20; to.w = 512; to.h = 673;
    steps = es_glide_steps(&from, &to);
    CHECK(steps >= 4);                        /* a trip worth showing */

    /* every step moves towards the target and the last one IS it */
    prev = from;
    for (i = 1; i <= steps; i++) {
        es_glide_rect(&from, &to, i, steps, &r);
        CHECK(r.x >= prev.x);
        CHECK(r.w >= prev.w);
        CHECK(r.x <= to.x);
        CHECK(r.w <= to.w);
        prev = r;
    }
    CHECK(r.x == to.x && r.y == to.y && r.w == to.w && r.h == to.h);

    /* it eases out: the first half covers more than half the distance */
    es_glide_rect(&from, &to, steps / 2, steps, &r);
    CHECK(r.x - from.x > (to.x - from.x) / 2);

    /* out of range asks for the ends */
    es_glide_rect(&from, &to, 0, steps, &r);
    CHECK(r.x == from.x && r.w == from.w);
    es_glide_rect(&from, &to, steps + 3, steps, &r);
    CHECK(r.x == to.x && r.w == to.w);
}

static void test_glide_declines_when_it_should(void)
{
    ESRect from, to;

    /* a move nobody would see */
    from.x = 100; from.y = 100; from.w = 300; from.h = 200;
    to = from;
    to.x = 110;
    CHECK(es_glide_steps(&from, &to) == 0);

    /* a window too large to redraw eight times */
    from.x = 0; from.y = 0; from.w = 1900; from.h = 1000;
    to.x = 0; to.y = 0; to.w = 950; to.h = 1000;
    CHECK(es_glide_steps(&from, &to) == 0);

    /* the long trip of an ordinary window earns the most steps */
    from.x = 0; from.y = 24; from.w = 400; from.h = 300;
    to.x = 1500; to.y = 24; to.w = 400; to.h = 300;
    CHECK(es_glide_steps(&from, &to) == 8);
}

int main(void)
{
    test_glide_moves_and_lands();
    test_glide_declines_when_it_should();
    test_centre_keeps_the_size();
    test_centre_honours_area_and_limits();
    test_side_zones_cycle_widths();
    test_stepped_zone_honours_limits();
    test_zone_from_pointer();
    test_zone_rect_tiles();
    test_fit_zone_rect();

    if (g_failures == 0) {
        printf("zones_test: all tests passed\n");
        return 0;
    }
    printf("zones_test: %d failure(s)\n", g_failures);
    return 1;
}
