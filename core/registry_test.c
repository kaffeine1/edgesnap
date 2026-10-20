/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * registry_test.c - executable specification of the snap registry and
 * its stale-resistant restore contract.
 */

#include <stdio.h>
#include <limits.h>

#include "registry.h"

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

static void test_remember_restore_roundtrip(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect snapped = rect(0, 12, 320, 468);
    ESRect out;
    int w1;

    es_registry_init(&reg);
    CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_NONE);
    CHECK(es_registry_remember(&reg, &w1, &pre, &snapped,
                               ES_ZONE_LEFT) == ES_OK);
    CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_LEFT);

    /* window still where the snap put it: restore succeeds, consumed */
    CHECK(es_registry_restore(&reg, &w1, &snapped, &out) == ES_OK);
    CHECK(out.x == 100 && out.y == 100 && out.w == 200 && out.h == 150);
    CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_NONE);
    CHECK(es_registry_restore(&reg, &w1, &snapped, &out) ==
          ES_ERR_NOT_SNAPPED);
}

static void test_resnap_keeps_first_prebox(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect left = rect(0, 12, 320, 468);
    ESRect right = rect(320, 12, 320, 468);
    ESRect out;
    int w1;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    /* re-snap left -> right; the glue passes the CURRENT box as prebox,
     * which is the left placement - it must NOT overwrite the original */
    CHECK(es_registry_remember(&reg, &w1, &left, &right,
                               ES_ZONE_RIGHT) == ES_OK);
    CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_RIGHT);
    CHECK(es_registry_restore(&reg, &w1, &right, &out) == ES_OK);
    CHECK(out.x == 100 && out.w == 200);
}

static void test_independent_change_drops_entry(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect snapped = rect(0, 12, 320, 468);
    ESRect moved = rect(50, 60, 300, 200);
    ESRect out;
    int w1;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &w1, &pre, &snapped,
                               ES_ZONE_LEFT) == ES_OK);
    /* the user re-arranged the window since the snap */
    CHECK(es_registry_restore(&reg, &w1, &moved, &out) == ES_ERR_CHANGED);
    /* the entry is gone, not retried forever */
    CHECK(es_registry_restore(&reg, &w1, &snapped, &out) ==
          ES_ERR_NOT_SNAPPED);
}

static void test_slack_tolerates_app_rounding(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect snapped = rect(0, 12, 320, 468);
    ESRect rounded = rect(0, 12, 312, 460); /* size-increment app */
    ESRect out;
    int w1;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &w1, &pre, &snapped,
                               ES_ZONE_LEFT) == ES_OK);
    CHECK(es_registry_restore(&reg, &w1, &rounded, &out) == ES_OK);
    CHECK(out.w == 200);
}

static void test_forget_and_capacity(void)
{
    ESRegistry reg;
    ESRect pre = rect(0, 0, 10, 10);
    ESRect snapped = rect(0, 0, 20, 20);
    char refs[ES_REGISTRY_SLOTS + 1];
    int i;

    es_registry_init(&reg);
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        CHECK(es_registry_remember(&reg, &refs[i], &pre, &snapped,
                                   ES_ZONE_LEFT) == ES_OK);
    }
    /* full: the contract is an explicit error, not silent eviction */
    CHECK(es_registry_remember(&reg, &refs[ES_REGISTRY_SLOTS], &pre,
                               &snapped, ES_ZONE_LEFT) == ES_ERR_NO_MEMORY);
    /* forget frees the slot for reuse */
    es_registry_forget(&reg, &refs[0]);
    CHECK(es_registry_zone(&reg, &refs[0]) == ES_ZONE_NONE);
    CHECK(es_registry_remember(&reg, &refs[ES_REGISTRY_SLOTS], &pre,
                               &snapped, ES_ZONE_LEFT) == ES_OK);
}

/* The box a group starts from when a snapped window joins it (2.24):
 * where the snap put it, not where it was before, and nothing for a
 * window the registry does not hold. */
static void test_snapped_accessor(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect left = rect(0, 12, 320, 468);
    ESRect right = rect(320, 12, 320, 468);
    ESRect out;
    char refs[ES_REGISTRY_SLOTS];
    int w1, w2, i;

    es_registry_init(&reg);
    out = rect(-1, -2, -3, -4);
    CHECK(es_registry_snapped(&reg, &w1, &out) == 0);
    CHECK(out.x == -1 && out.y == -2 && out.w == -3 && out.h == -4);
    CHECK(es_registry_snapped(&reg, 0, &out) == 0);

    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    CHECK(es_registry_snapped(&reg, &w1, &out) == 1);
    CHECK(out.x == 0 && out.y == 12 && out.w == 320 && out.h == 468);
    CHECK(es_registry_snapped(&reg, &w1, 0) == 1);
    CHECK(es_registry_snapped(&reg, &w2, &out) == 0);
    CHECK(out.x == 0 && out.w == 320);

    /* a re-snap moves it, and the prebox stays the restore box */
    CHECK(es_registry_remember(&reg, &w1, &left, &right,
                               ES_ZONE_RIGHT) == ES_OK);
    CHECK(es_registry_snapped(&reg, &w1, &out) == 1);
    CHECK(out.x == 320 && out.w == 320);

    /* restore consumes the entry, forget drops it */
    CHECK(es_registry_restore(&reg, &w1, &right, &out) == ES_OK);
    CHECK(out.x == 100);
    CHECK(es_registry_snapped(&reg, &w1, &out) == 0);
    CHECK(out.x == 100);
    CHECK(es_registry_remember(&reg, &w2, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    es_registry_forget(&reg, &w2);
    CHECK(es_registry_snapped(&reg, &w2, 0) == 0);

    /* the last slot is found as well as the first */
    es_registry_init(&reg);
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        ESRect s = rect(i, 2 * i, 10 + i, 20 + i);

        CHECK(es_registry_remember(&reg, &refs[i], &pre, &s,
                                   ES_ZONE_LEFT) == ES_OK);
    }
    CHECK(es_registry_snapped(&reg, &refs[ES_REGISTRY_SLOTS - 1],
                              &out) == 1);
    CHECK(out.x == ES_REGISTRY_SLOTS - 1 &&
          out.y == 2 * (ES_REGISTRY_SLOTS - 1) &&
          out.w == 10 + ES_REGISTRY_SLOTS - 1 &&
          out.h == 20 + ES_REGISTRY_SLOTS - 1);
    CHECK(es_registry_snapped(&reg, &refs[0], &out) == 1);
    CHECK(out.x == 0 && out.y == 0 && out.w == 10 && out.h == 20);
}

/*
 * 2.24: a snapped window dragged by its title bar and let go outside
 * every zone gets back the box from before the snap, and only when it
 * was snapped to a zone, kept the snapped size and really moved out.
 */
static void test_drag_out_restores_a_moved_window(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect left = rect(0, 12, 320, 468);
    ESRect right = rect(320, 12, 320, 468);
    ESRect out;
    int w1;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    {
        ESRect now = rect(300, 200, 320, 468);

        CHECK(es_registry_drag_out(&reg, &w1, &left, &now, &out) == ES_OK);
        CHECK(out.x == 100 && out.y == 100 && out.w == 200 && out.h == 150);
        CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_NONE);  /* consumed */
        CHECK(es_registry_drag_out(&reg, &w1, &left, &now, &out) ==
              ES_ERR_NOT_SNAPPED);
    }

    /* an application that rounds the box to its cells, both times */
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    {
        ESRect start = rect(0, 12, 312, 460);
        ESRect now = rect(400, 100, 312, 460);

        CHECK(es_registry_drag_out(&reg, &w1, &start, &now, &out) == ES_OK);
        CHECK(out.w == 200 && out.h == 150);
    }

    /* snapped left, then right: the first box is the one that comes back */
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    CHECK(es_registry_remember(&reg, &w1, &left, &right,
                               ES_ZONE_RIGHT) == ES_OK);
    {
        ESRect now = rect(100, 150, 320, 468);

        CHECK(es_registry_drag_out(&reg, &w1, &right, &now, &out) == ES_OK);
        CHECK(out.x == 100 && out.y == 100 && out.w == 200 && out.h == 150);
    }

    /* the centre keeps the size, and goes back all the same */
    CHECK(es_registry_remember(&reg, &w1, &pre, &pre,
                               ES_ZONE_CENTRE) == ES_OK);
    {
        ESRect now = rect(300, 300, 200, 150);

        CHECK(es_registry_drag_out(&reg, &w1, &pre, &now, &out) == ES_OK);
        CHECK(out.x == 100 && out.y == 100);
        CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_NONE);
    }
}

/*
 * Where the window moves with the pointer, the box at the start of the
 * drag can be sampled after it has already gone further than the slack:
 * only its size is compared. The drop must still be beyond the slack
 * from that start, so a drag put back where it began does nothing.
 */
static void test_drag_out_judges_the_start_by_its_size(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect left = rect(0, 12, 320, 468);
    ESRect late = rect(200, 150, 320, 468);   /* 200 px into the drag */
    ESRect out = rect(-1, -1, -1, -1);
    int w1;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    {
        ESRect near_start = rect(210, 150, 320, 468);
        ESRect far = rect(500, 300, 320, 468);
        ESRect home = rect(10, 20, 320, 468);

        CHECK(es_registry_drag_out(&reg, &w1, &late, &near_start, &out) ==
              ES_ERR_CHANGED);
        CHECK(out.x == -1);                       /* untouched */
        CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_LEFT);
        /* taken away and brought back to where the snap put it */
        CHECK(es_registry_drag_out(&reg, &w1, &late, &home, &out) ==
              ES_ERR_CHANGED);
        CHECK(out.x == -1 && es_registry_zone(&reg, &w1) == ES_ZONE_LEFT);
        CHECK(es_registry_drag_out(&reg, &w1, &late, &far, &out) == ES_OK);
        CHECK(out.x == 100 && out.w == 200);
    }
}

/* Every refusal leaves the entry as it was, and the restore hotkey then
 * decides about it as it always did. */
static void test_drag_out_refusals_keep_the_entry(void)
{
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect left = rect(0, 12, 320, 468);
    ESRect out;
    int w1, w2;

    es_registry_init(&reg);
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_LEFT) == ES_OK);
    {
        ESRect within = rect(10, 20, 320, 468);
        ESRect thin = rect(0, 12, 280, 468);
        ESRect away = rect(300, 200, 320, 468);
        ESRect shrunk = rect(300, 200, 280, 468);
        ESRect taller = rect(300, 200, 320, 500);

        /* let go within the slack: still snapped */
        CHECK(es_registry_drag_out(&reg, &w1, &left, &within, &out) ==
              ES_ERR_CHANGED);
        CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_LEFT);
        /* resized since the snap, before the drag began */
        CHECK(es_registry_drag_out(&reg, &w1, &thin, &away, &out) ==
              ES_ERR_CHANGED);
        /* resized by the drag, or so it looks */
        CHECK(es_registry_drag_out(&reg, &w1, &left, &shrunk, &out) ==
              ES_ERR_CHANGED);
        CHECK(es_registry_drag_out(&reg, &w1, &left, &taller, &out) ==
              ES_ERR_CHANGED);
        CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_LEFT);
        /* the hotkey still puts it back */
        CHECK(es_registry_restore(&reg, &w1, &within, &out) == ES_OK);
        CHECK(out.x == 100 && out.w == 200);
    }

    /* a client's placement is not a snap, and no entry is no entry */
    CHECK(es_registry_remember(&reg, &w1, &pre, &left,
                               ES_ZONE_RECT) == ES_OK);
    {
        ESRect away = rect(300, 200, 320, 468);

        CHECK(es_registry_drag_out(&reg, &w1, &left, &away, &out) ==
              ES_ERR_NOT_SNAPPED);
        CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_RECT);
        CHECK(es_registry_drag_out(&reg, &w2, &left, &away, &out) ==
              ES_ERR_NOT_SNAPPED);
        CHECK(es_registry_drag_out(&reg, 0, &left, &away, &out) ==
              ES_ERR_NOT_SNAPPED);
    }
}

/* Distances beyond INT_MAX are far away, in either order. Checking all
 * four fields also covers the size checks of a drag restore. */
static void test_extreme_coordinates_do_not_match(void)
{
    static const int a[] = { INT_MIN, INT_MAX, INT_MIN, 0, INT_MAX, -1 };
    static const int b[] = { INT_MAX, INT_MIN, 0, INT_MIN, -1, INT_MAX };
    ESRegistry reg;
    ESRect pre = rect(100, 100, 200, 150);
    ESRect snapped, now, out;
    int w1, i, field;

    for (i = 0; i < 6; i++) {
        for (field = 0; field < 4; field++) {
            snapped = rect(0, 12, 320, 468);
            now = snapped;
            switch (field) {
            case 0: snapped.x = a[i]; now.x = b[i]; break;
            case 1: snapped.y = a[i]; now.y = b[i]; break;
            case 2: snapped.w = a[i]; now.w = b[i]; break;
            default: snapped.h = a[i]; now.h = b[i]; break;
            }
            es_registry_init(&reg);
            CHECK(es_registry_remember(&reg, &w1, &pre, &snapped,
                                       ES_ZONE_LEFT) == ES_OK);
            CHECK(es_registry_restore(&reg, &w1, &now, &out) == ES_ERR_CHANGED);
            CHECK(es_registry_zone(&reg, &w1) == ES_ZONE_NONE);
        }
    }
    /* Close coordinates at either extreme still honour the slack. */
    for (i = 0; i < 2; i++) {
        snapped = rect(i ? INT_MAX : INT_MIN, 12, 320, 468);
        now = snapped;
        now.x += i ? -ES_RESTORE_SLACK_PX : ES_RESTORE_SLACK_PX;
        es_registry_init(&reg);
        CHECK(es_registry_remember(&reg, &w1, &pre, &snapped,
                                   ES_ZONE_LEFT) == ES_OK);
        CHECK(es_registry_restore(&reg, &w1, &now, &out) == ES_OK);
    }
}

int main(void)
{
    test_extreme_coordinates_do_not_match();
    test_remember_restore_roundtrip();
    test_resnap_keeps_first_prebox();
    test_independent_change_drops_entry();
    test_slack_tolerates_app_rounding();
    test_forget_and_capacity();
    test_snapped_accessor();
    test_drag_out_restores_a_moved_window();
    test_drag_out_judges_the_start_by_its_size();
    test_drag_out_refusals_keep_the_entry();

    if (g_failures == 0) {
        printf("registry_test: all tests passed\n");
        return 0;
    }
    printf("registry_test: %d failure(s)\n", g_failures);
    return 1;
}
