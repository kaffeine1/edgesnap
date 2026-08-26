/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * config_test.c - executable specification of the preferences parser.
 */

#include <stdio.h>

#include "config.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

static void test_defaults(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(c.engine.edge_px == 12);
    CHECK(c.engine.corner_div == 4);
    CHECK(c.engine.drag_min_px == 4);
    CHECK(c.engine.zones_mask == ES_ZONEMASK_ALL);
    CHECK(c.panel_detect == 1);
    CHECK(c.panel_margin == ES_PANEL_MARGIN_PX);
    CHECK(c.preview == 1);
    CHECK(c.bypass_qual == ES_QUAL_NONE);
    CHECK(c.margin.l == 0 && c.margin.b == 0);
}

static void test_numbers_and_bounds(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(es_config_line(&c, "EDGEPX=24") == ES_OK);
    CHECK(c.engine.edge_px == 24);
    CHECK(es_config_line(&c, "PANELMARGIN=16") == ES_OK);
    CHECK(c.panel_margin == 16);
    CHECK(es_config_line(&c, "MARGINBOTTOM=40") == ES_OK);
    CHECK(c.margin.b == 40);

    /* out of range and malformed values are refused, state untouched */
    CHECK(es_config_line(&c, "EDGEPX=0") == ES_ERR_BAD_ARGS);
    CHECK(es_config_line(&c, "EDGEPX=99999") == ES_ERR_BAD_ARGS);
    CHECK(es_config_line(&c, "EDGEPX=twelve") == ES_ERR_BAD_ARGS);
    CHECK(es_config_line(&c, "EDGEPX=12x") == ES_ERR_BAD_ARGS);
    CHECK(es_config_line(&c, "MARGINLEFT=-4") == ES_ERR_BAD_ARGS);
    CHECK(c.engine.edge_px == 24);
    CHECK(c.margin.l == 0);
}

static void test_key_spelling_and_whitespace(void)
{
    ESConfig c;

    es_config_defaults(&c);
    /* tooltype style, env-file style, sloppy spacing: same key */
    CHECK(es_config_line(&c, "DRAG_MIN_PX=9") == ES_OK);
    CHECK(c.engine.drag_min_px == 9);
    CHECK(es_config_line(&c, "  drag-min-px  =  7  ") == ES_OK);
    CHECK(c.engine.drag_min_px == 7);
    CHECK(es_config_line(&c, "DragMinPx=5") == ES_OK);
    CHECK(c.engine.drag_min_px == 5);
}

static void test_comments_and_blanks(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(es_config_line(&c, "") == ES_OK);
    CHECK(es_config_line(&c, "   ") == ES_OK);
    CHECK(es_config_line(&c, "# EDGEPX=99") == ES_OK);
    CHECK(es_config_line(&c, "; comment") == ES_OK);
    CHECK(c.engine.edge_px == 12);
    /* a line without '=' is an error, not a silent skip */
    CHECK(es_config_line(&c, "EDGEPX 24") == ES_ERR_BAD_ARGS);
    CHECK(es_config_line(&c, "EDGEPX=") == ES_ERR_BAD_ARGS);
    CHECK(es_config_line(&c, "=24") == ES_ERR_BAD_ARGS);
}

static void test_unknown_key_is_distinct(void)
{
    ESConfig c;

    es_config_defaults(&c);
    /* unknown key must be told apart from a bad value: the frontend
     * warns differently, and a future version may add the key */
    CHECK(es_config_line(&c, "WOBBLE=3") == ES_ERR_UNSUPPORTED);
}

static void test_booleans(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(es_config_line(&c, "PREVIEW=off") == ES_OK);
    CHECK(c.preview == 0);
    CHECK(es_config_line(&c, "PREVIEW=YES") == ES_OK);
    CHECK(c.preview == 1);
    CHECK(es_config_line(&c, "PANELDETECT=0") == ES_OK);
    CHECK(c.panel_detect == 0);
    CHECK(es_config_line(&c, "PANELDETECT=maybe") == ES_ERR_BAD_ARGS);
    CHECK(c.panel_detect == 0);
}

static void test_zones(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(es_config_line(&c, "ZONES=left,right") == ES_OK);
    CHECK(c.engine.zones_mask ==
          (ES_ZONEBIT(ES_ZONE_LEFT) | ES_ZONEBIT(ES_ZONE_RIGHT)));

    CHECK(es_config_line(&c, "ZONES=halves, corners") == ES_OK);
    CHECK(c.engine.zones_mask ==
          (ES_ZONEBIT(ES_ZONE_LEFT) | ES_ZONEBIT(ES_ZONE_RIGHT) |
           ES_ZONEBIT(ES_ZONE_TOP_LEFT) | ES_ZONEBIT(ES_ZONE_TOP_RIGHT) |
           ES_ZONEBIT(ES_ZONE_BOTTOM_LEFT) |
           ES_ZONEBIT(ES_ZONE_BOTTOM_RIGHT)));

    CHECK(es_config_line(&c, "ZONES=all") == ES_OK);
    CHECK(c.engine.zones_mask == ES_ZONEMASK_ALL);
    CHECK(es_config_line(&c, "ZONES=none") == ES_OK);
    CHECK(c.engine.zones_mask == 0);

    /* a typo must not silently disable the zones it did understand */
    CHECK(es_config_line(&c, "ZONES=left,rihgt") == ES_ERR_BAD_ARGS);
    CHECK(c.engine.zones_mask == 0);
    CHECK(es_config_line(&c, "ZONES=maximize") == ES_OK);
    CHECK(c.engine.zones_mask == ES_ZONEBIT(ES_ZONE_MAX));
}

static void test_bypass_qualifier(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(es_config_line(&c, "BYPASSQUAL=alt") == ES_OK);
    CHECK(c.bypass_qual == ES_QUAL_ALT);
    CHECK(es_config_line(&c, "BYPASS=Control") == ES_OK);
    CHECK(c.bypass_qual == ES_QUAL_CTRL);
    CHECK(es_config_line(&c, "BYPASS=none") == ES_OK);
    CHECK(c.bypass_qual == ES_QUAL_NONE);
    CHECK(es_config_line(&c, "BYPASS=amiga") == ES_ERR_BAD_ARGS);
}

int main(void)
{
    test_defaults();
    test_numbers_and_bounds();
    test_key_spelling_and_whitespace();
    test_comments_and_blanks();
    test_unknown_key_is_distinct();
    test_booleans();
    test_zones();
    test_bypass_qualifier();

    if (g_failures == 0) {
        printf("config_test: all tests passed\n");
        return 0;
    }
    printf("config_test: %d failure(s)\n", g_failures);
    return 1;
}
