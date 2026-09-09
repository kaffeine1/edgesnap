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

int main(void)
{
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
