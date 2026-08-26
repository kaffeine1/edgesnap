/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * registry_test.c - executable specification of the snap registry and
 * its stale-safe restore contract.
 */

#include <stdio.h>

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

int main(void)
{
    test_remember_restore_roundtrip();
    test_resnap_keeps_first_prebox();
    test_independent_change_drops_entry();
    test_slack_tolerates_app_rounding();
    test_forget_and_capacity();

    if (g_failures == 0) {
        printf("registry_test: all tests passed\n");
        return 0;
    }
    printf("registry_test: %d failure(s)\n", g_failures);
    return 1;
}
