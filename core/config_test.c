/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * config_test.c - executable specification of the preferences parser.
 */

#include <stdio.h>
#include <string.h>

#include "config.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

/* 0 and 1 are "only the corner itself"; 17 is still too many. */
static void test_corner_div_range(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(es_config_line(&c, "CORNERDIV=4") == ES_OK && c.engine.corner_div == 4);
    CHECK(es_config_line(&c, "CORNERDIV=0") == ES_OK && c.engine.corner_div == 0);
    CHECK(es_config_line(&c, "CORNERDIV=1") == ES_OK && c.engine.corner_div == 1);
    CHECK(es_config_line(&c, "CORNERDIV=17") == ES_ERR_BAD_ARGS &&
          c.engine.corner_div == 1);
}

static void test_defaults(void)
{
    ESConfig c;

    es_config_defaults(&c);
    CHECK(c.engine.edge_px == 12);
    CHECK(c.engine.corner_div == 0);
    CHECK(c.engine.drag_min_px == 4);
    CHECK(c.engine.zones_mask == ES_ZONEMASK_ALL);
    CHECK(c.panel_detect == 1);
    CHECK(c.panel_margin == ES_PANEL_MARGIN_PX);
    CHECK(c.preview == 1);
    CHECK(c.drag_restore == 1);
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
    CHECK(es_config_line(&c, "DRAGRESTORE=no") == ES_OK);
    CHECK(c.drag_restore == 0);
    CHECK(es_config_line(&c, "Drag_Restore=yes") == ES_OK);
    CHECK(c.drag_restore == 1);
    CHECK(es_config_line(&c, "DRAGRESTORE=maybe") == ES_ERR_BAD_ARGS);
    CHECK(c.drag_restore == 1);
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


/* ------------------------------------------------- settings + writing
 *
 * The preferences GUI and the file have to agree for ever: whatever
 * the window can produce, the parser must read back as the same
 * settings. These check the round trip, not the spelling.
 */

static int same_config(const ESConfig *a, const ESConfig *b)
{
    return a->engine.zones_mask == b->engine.zones_mask &&
           a->engine.edge_px == b->engine.edge_px &&
           a->engine.corner_div == b->engine.corner_div &&
           a->engine.drag_min_px == b->engine.drag_min_px &&
           a->margin.l == b->margin.l && a->margin.t == b->margin.t &&
           a->margin.r == b->margin.r && a->margin.b == b->margin.b &&
           a->panel_detect == b->panel_detect &&
           a->panel_margin == b->panel_margin &&
           a->preview == b->preview &&
           a->cycle_sizes == b->cycle_sizes &&
           a->animate == b->animate &&
           a->drag_restore == b->drag_restore &&
           a->bypass_qual == b->bypass_qual;
}

static void feed_text(ESConfig *cfg, const char *text)
{
    char line[256];
    int n = 0;

    es_config_defaults(cfg);
    for (;;) {
        if (*text == '\n' || *text == '\0') {
            line[n] = '\0';
            es_config_line(cfg, line);
            n = 0;
            if (*text == '\0') {
                return;
            }
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = *text;
        }
        text++;
    }
}

static void test_write_round_trip(void)
{
    ESConfig a, b;
    char buf[1024];
    char small[8];
    int n;

    es_config_defaults(&a);
    n = es_config_write(&a, buf, (int)sizeof(buf));
    CHECK(n > 0 && n < (int)sizeof(buf));
    feed_text(&b, buf);
    CHECK(same_config(&a, &b));

    /* something nobody would write by hand */
    es_config_defaults(&a);
    a.engine.zones_mask = ES_ZONEBIT(ES_ZONE_LEFT) |
                          ES_ZONEBIT(ES_ZONE_BOTTOM_RIGHT);
    a.engine.edge_px = 37;
    a.engine.corner_div = 5;
    a.engine.drag_min_px = 9;
    a.margin.l = 11; a.margin.t = 12; a.margin.r = 13; a.margin.b = 14;
    a.panel_detect = 0;
    a.panel_margin = 21;
    a.preview = 0;
    a.cycle_sizes = 0;
    a.animate = 0;
    a.drag_restore = 0;
    a.bypass_qual = ES_QUAL_SHIFT;
    n = es_config_write(&a, buf, (int)sizeof(buf));
    CHECK(n > 0);
    feed_text(&b, buf);
    CHECK(same_config(&a, &b));

    /* a buffer too small says what it needed and writes nothing past
     * the end it was given */
    small[7] = '!';
    n = es_config_write(&a, small, 4);
    CHECK(n > 4);
    CHECK(small[7] == '!');
}

/* Defaults go out as comments; what the user changed does not. */
static void test_write_defaults_as_comments(void)
{
    ESConfig a, b;
    char buf[1024];
    const char *p;
    int pinned = 0;

    es_config_defaults(&a);
    es_config_write(&a, buf, (int)sizeof(buf));
    for (p = buf; *p != '\0'; p++) {
        if ((p == buf || p[-1] == '\n') && *p >= 'A' && *p <= 'Z') {
            pinned++;               /* a line that starts with a key */
        }
    }
    CHECK(pinned == 0);
    CHECK(strstr(buf, "\n#CORNERDIV=0\n") != NULL);
    CHECK(strstr(buf, "\n#EDGEPX=12\n") != NULL);
    CHECK(strstr(buf, "\n#DRAGRESTORE=yes\n") != NULL);

    a.engine.corner_div = 4;
    a.drag_restore = 0;
    es_config_write(&a, buf, (int)sizeof(buf));
    CHECK(strstr(buf, "\nCORNERDIV=4\n") != NULL);
    CHECK(strstr(buf, "\n#EDGEPX=12\n") != NULL);
    CHECK(strstr(buf, "\nDRAGRESTORE=no\n") != NULL);
    feed_text(&b, buf);
    CHECK(same_config(&a, &b));

    /* a file written with the defaults pins none of them: read by a
     * version whose default is another, that other is what holds */
    es_config_defaults(&a);
    es_config_write(&a, buf, (int)sizeof(buf));
    es_config_defaults(&b);
    b.engine.corner_div = 7;     /* as if 7 were a later default */
    {
        char line[256];
        const char *q = buf;
        int n = 0;

        for (;;) {
            if (*q == '\n' || *q == '\0') {
                line[n] = '\0';
                es_config_line(&b, line);
                n = 0;
                if (*q == '\0') {
                    break;
                }
            } else if (n < (int)sizeof(line) - 1) {
                line[n++] = *q;
            }
            q++;
        }
    }
    CHECK(b.engine.corner_div == 7);
}

static void test_settings_table(void)
{
    const ESSetting *t;
    int count = 0;
    int i;
    ESConfig cfg;

    t = es_settings(&count);
    CHECK(t != 0);
    CHECK(count > 0);

    for (i = 0; i < count; i++) {
        CHECK(t[i].key != 0);
        CHECK(t[i].label != 0);
        CHECK(t[i].help != 0);
        if (t[i].kind == ES_SET_CHOICE) {
            CHECK(t[i].choices != 0);
            CHECK(t[i].choices[0] != 0);
        }
        if (t[i].kind == ES_SET_INT) {
            CHECK(t[i].min < t[i].max);
        }
    }

    /* what the GUI reads back is what the GUI set */
    es_config_defaults(&cfg);
    for (i = 0; i < count; i++) {
        int v;

        if (t[i].kind == ES_SET_KEY) {
            continue;       /* text accessors are exercised below */
        }
        switch (t[i].kind) {
        case ES_SET_INT:    v = t[i].max; break;
        case ES_SET_BOOL:   v = 0; break;
        case ES_SET_CHOICE: v = 1; break;
        default:            v = (int)ES_ZONEBIT(ES_ZONE_RIGHT); break;
        }
        CHECK(es_setting_apply(&cfg, i, v) == ES_OK);
        CHECK(es_setting_value(&cfg, i) == v);
    }

    /* and the GUI cannot slip past the parser's rules */
    es_config_defaults(&cfg);
    for (i = 0; i < count; i++) {
        if (t[i].kind != ES_SET_INT) {
            continue;
        }
        CHECK(es_setting_apply(&cfg, i, t[i].max + 1) == ES_ERR_BAD_ARGS);
        CHECK(es_setting_apply(&cfg, i, t[i].min - 1) == ES_ERR_BAD_ARGS);
    }
    CHECK(es_setting_apply(&cfg, -1, 0) == ES_ERR_BAD_ARGS);
    CHECK(es_setting_apply(&cfg, count, 0) == ES_ERR_BAD_ARGS);
}

static int es_str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/*
 * The windows build their frames by starting a new section whenever the
 * group changes as they walk the table, and the manual takes the same
 * headings. That only works if entries sharing a group sit together: a
 * stray row would open a second frame with a title already used.
 */
static void test_groups_are_named_and_contiguous(void)
{
    const ESSetting *t;
    int count = 0;
    int i, j;

    t = es_settings(&count);
    for (i = 0; i < count; i++) {
        CHECK(t[i].group != 0);
        CHECK(t[i].group[0] != '\0');
    }
    /* every run of a group must be the only run of that group */
    for (i = 1; i < count; i++) {
        if (es_str_eq(t[i].group, t[i - 1].group)) {
            continue;                      /* still inside the section */
        }
        for (j = 0; j < i - 1; j++) {      /* a section reopened? */
            CHECK(!es_str_eq(t[j].group, t[i].group));
        }
    }
}

static void feed_prefs(ESConfig *cfg, ESHotkeys *keys, const char *text)
{
    char line[256];
    int n = 0;

    es_config_defaults(cfg);
    es_hotkeys_defaults(keys);
    for (;;) {
        if (*text == '\n' || *text == '\0') {
            line[n] = '\0';
            CHECK(es_prefs_line(cfg, keys, line) == ES_OK);
            n = 0;
            if (*text == '\0') {
                return;
            }
        } else if (n < (int)sizeof(line) - 1) {
            line[n++] = *text;
        }
        text++;
    }
}

static void test_hotkey_defaults_and_spelling(void)
{
    ESConfig cfg;
    ESHotkeys keys;
    static const char *const defaults[ES_HOTKEY_COUNT] = {
        "ctrl alt cursor_left", "ctrl alt cursor_right", "ctrl alt cursor_up",
        "", "", "", "", "ctrl alt c", "ctrl alt cursor_down",
        "ctrl alt z", "", "ctrl alt d", "ctrl alt f"
    };
    int i;

    es_config_defaults(&cfg);
    es_hotkeys_defaults(&keys);
    for (i = 0; i < ES_HOTKEY_COUNT; i++) {
        CHECK(strcmp(keys.key[i], defaults[i]) == 0);
    }
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEY_LEFT=alt x") == ES_OK);
    CHECK(strcmp(keys.key[ES_HK_LEFT], "alt x") == 0);
    CHECK(es_prefs_line(&cfg, &keys, "hotkeyleft=ctrl x") == ES_OK);
    CHECK(strcmp(keys.key[ES_HK_LEFT], "ctrl x") == 0);
    CHECK(es_prefs_line(&cfg, &keys, "CX_POPKEY=ctrl p") == ES_OK);
    CHECK(strcmp(keys.key[ES_HK_POPKEY], "ctrl p") == 0);
    CHECK(es_prefs_line(&cfg, &keys, "POPKEY=alt p") == ES_OK);
    CHECK(strcmp(keys.key[ES_HK_POPKEY], "alt p") == 0);
    CHECK(es_prefs_line(&cfg, &keys, "EDGEPX=25") == ES_OK);
    CHECK(cfg.engine.edge_px == 25);
    CHECK(es_config_line(&cfg, "HOTKEYLEFT=ctrl q") == ES_ERR_UNSUPPORTED);
    CHECK(es_prefs_line(&cfg, &keys, "WOBBLE=x") == ES_ERR_UNSUPPORTED);
}

static void test_hotkey_validation(void)
{
    ESConfig cfg;
    ESHotkeys keys;
    char value[ES_HOTKEY_SIZE + 4];
    char line[128];
    int i;

    es_config_defaults(&cfg);
    es_hotkeys_defaults(&keys);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=\"ctrl alt x\"") == ES_OK);
    CHECK(strcmp(keys.key[ES_HK_LEFT], "ctrl alt x") == 0);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=NoNe") == ES_OK);
    CHECK(keys.key[ES_HK_LEFT][0] == '\0');
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=\"OFF\"") == ES_OK);
    CHECK(keys.key[ES_HK_LEFT][0] == '\0');
    CHECK(es_prefs_set(&cfg, &keys, "HOTKEYLEFT", "") == ES_OK);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=\"\"") == ES_OK);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=") == ES_ERR_BAD_ARGS);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=ctrl x\r\n") == ES_OK);
    /* prefs_io splits on LF, leaving the CR of a DOS text line. */
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=ctrl x\r") == ES_OK);

    /* Every non-printable byte fails atomically, including high ASCII. */
    for (i = 1; i <= 255; i++) {
        if (i >= 32 && i <= 126) {
            continue;
        }
        value[0] = 'x'; value[1] = (char)i; value[2] = 'y'; value[3] = '\0';
        CHECK(es_prefs_set(&cfg, &keys, "HOTKEYLEFT", value) == ES_ERR_BAD_ARGS);
        CHECK(strcmp(keys.key[ES_HK_LEFT], "ctrl x") == 0);
        strcpy(line, "HOTKEYLEFT=");
        strcat(line, value);
        CHECK(es_prefs_line(&cfg, &keys, line) == ES_ERR_BAD_ARGS);
        CHECK(strcmp(keys.key[ES_HK_LEFT], "ctrl x") == 0);
    }
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=ctrl x\t") == ES_ERR_BAD_ARGS);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=\tctrl x") == ES_ERR_BAD_ARGS);
    for (i = 0; i < ES_HOTKEY_SIZE - 1; i++) {
        value[i] = 'x';
    }
    value[i] = '\0';
    CHECK(es_prefs_set(&cfg, &keys, "HOTKEYLEFT", value) == ES_OK);
    strcpy(line, "HOTKEYLEFT=");
    strcat(line, value);
    CHECK(es_prefs_line(&cfg, &keys, line) == ES_OK);
    value[i++] = 'y'; value[i] = '\0';
    CHECK(es_prefs_set(&cfg, &keys, "HOTKEYLEFT", value) == ES_ERR_BAD_ARGS);
    strcpy(line, "HOTKEYLEFT=");
    strcat(line, value);
    CHECK(es_prefs_line(&cfg, &keys, line) == ES_ERR_BAD_ARGS);
    CHECK(strlen(keys.key[ES_HK_LEFT]) == ES_HOTKEY_SIZE - 1);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=\"ctrl x") == ES_ERR_BAD_ARGS);
    CHECK(es_prefs_line(&cfg, &keys, "HOTKEYLEFT=ctrl x\"") == ES_ERR_BAD_ARGS);
    CHECK(es_prefs_set(&cfg, &keys, "HOTKEYLEFT", 0) == ES_ERR_BAD_ARGS);
}

static void test_prefs_table_and_round_trip(void)
{
    ESConfig a, b;
    ESHotkeys ka, kb;
    const ESSetting *t;
    char buf[4096];
    char legacy[1024];
    char after[1024];
    int count, i, j, key_count = 0;
    int seen[ES_HOTKEY_COUNT];

    es_config_defaults(&a);
    es_hotkeys_defaults(&ka);
    memset(seen, 0, sizeof(seen));
    t = es_settings(&count);
    es_config_write(&a, legacy, (int)sizeof(legacy));
    CHECK(es_prefs_write(&a, &ka, buf, (int)sizeof(buf)) < (int)sizeof(buf));
    CHECK(strstr(buf, "\n#HOTKEYLEFT=ctrl alt cursor_left\n") != 0);
    CHECK(strstr(buf, "\n#HOTKEYTOPLEFT=none\n") != 0);
    CHECK(strstr(buf, "\n#CX_POPKEY=none\n") != 0);
    for (i = 0; i < count; i++) {
        CHECK(t[i].page != 0 && t[i].page[0] != '\0');
        if (i > 0 && strcmp(t[i].page, t[i - 1].page) != 0) {
            for (j = 0; j < i - 1; j++) {
                CHECK(strcmp(t[j].page, t[i].page) != 0);
            }
        }
        if (strcmp(t[i].key, "CYCLESIZES") == 0) {
            CHECK(strcmp(t[i].page, "Hotkeys") == 0);
            CHECK(strcmp(t[i].group, "Snap the active window") == 0);
        } else if (t[i].kind != ES_SET_KEY) {
            CHECK(strcmp(t[i].page, "Snapping") == 0);
        }
        if (t[i].kind == ES_SET_KEY) {
            const char *text = es_setting_text(&ka, i);

            key_count++;
            CHECK(strcmp(t[i].page, "Hotkeys") == 0);
            CHECK(text != 0);
            for (j = 0; j < ES_HOTKEY_COUNT; j++) {
                if (text == ka.key[j]) {
                    seen[j]++;
                }
            }
            CHECK(es_setting_apply(&a, i, 42) == ES_ERR_BAD_ARGS);
            CHECK(es_setting_apply_text(&ka, i, "\001") == ES_ERR_BAD_ARGS);
            CHECK(es_setting_apply_text(&ka, i, "ctrl shift x") == ES_OK);
            CHECK(strcmp(es_setting_text(&ka, i), "ctrl shift x") == 0);
            CHECK(es_setting_apply_text(&ka, i, "") == ES_OK);
            CHECK(es_setting_text(&ka, i)[0] == '\0');
            CHECK(es_prefs_set(&a, &ka, t[i].key, text) == ES_OK);
        } else {
            CHECK(es_setting_text(&ka, i) == 0);
            CHECK(es_setting_apply_text(&ka, i, "x") == ES_ERR_BAD_ARGS);
        }
    }
    CHECK(key_count == ES_HOTKEY_COUNT);
    for (i = 0; i < ES_HOTKEY_COUNT; i++) {
        CHECK(seen[i] == 1);
    }
    CHECK(es_setting_text(&ka, -1) == 0);
    CHECK(es_setting_text(&ka, count) == 0);
    CHECK(es_setting_apply_text(&ka, count, "x") == ES_ERR_BAD_ARGS);
    CHECK(es_setting_apply_text(&ka, -1, "x") == ES_ERR_BAD_ARGS);

    CHECK(es_prefs_line(&a, &ka, "HOTKEYLEFT=\"ctrl shift x\"") == ES_OK);
    CHECK(es_prefs_line(&a, &ka, "HOTKEYTOPRIGHT=alt r") == ES_OK);
    CHECK(es_prefs_line(&a, &ka, "CX_POPKEY=ctrl p") == ES_OK);
    CHECK(es_prefs_line(&a, &ka, "HOTKEYRESTORE=off") == ES_OK);
    CHECK(es_prefs_write(&a, &ka, buf, (int)sizeof(buf)) < (int)sizeof(buf));
    CHECK(strstr(buf, "\nHOTKEYRESTORE=none\n") != 0);
    feed_prefs(&b, &kb, buf);
    CHECK(same_config(&a, &b));
    CHECK(memcmp(&ka, &kb, sizeof(ka)) == 0);
    es_config_write(&a, after, (int)sizeof(after));
    CHECK(strcmp(legacy, after) == 0);
    CHECK(strstr(after, "HOTKEY") == 0 && strstr(after, "POPKEY") == 0);
    CHECK(strstr(after, "#BYPASSQUAL=none\n#CYCLESIZES=yes\n#DRAGRESTORE=yes") != 0);

    /* Every text row survives a non-default round trip. */
    for (i = 0; i < count; i++) {
        if (t[i].kind == ES_SET_KEY) {
            char text[] = "ctrl shift a";

            text[11] = (char)('a' + (i % ES_HOTKEY_COUNT));
            CHECK(es_setting_apply_text(&ka, i, text) == ES_OK);
        }
    }
    CHECK(es_prefs_write(&a, &ka, buf, (int)sizeof(buf)) < (int)sizeof(buf));
    feed_prefs(&b, &kb, buf);
    CHECK(memcmp(&ka, &kb, sizeof(ka)) == 0);
}

static void test_quoted_text_round_trip(void)
{
    static const char *const values[] = {
        "\" ctrl x \"", "\"\"ctrl x\"\"", "\"\"ctrl x\"", "\" \"", "ctrl \" x", 0
    };
    ESConfig a, b;
    ESHotkeys ka, kb;
    char buf[4096];
    int i;

    es_config_defaults(&a);
    es_hotkeys_defaults(&ka);
    for (i = 0; values[i] != 0; i++) {
        CHECK(es_prefs_set(&a, &ka, "HOTKEYLEFT", values[i]) == ES_OK);
        es_prefs_write(&a, &ka, buf, (int)sizeof(buf));
        feed_prefs(&b, &kb, buf);
        CHECK(memcmp(&ka, &kb, sizeof(ka)) == 0);
    }
}

int main(void)
{
    test_corner_div_range();
    test_defaults();
    test_numbers_and_bounds();
    test_key_spelling_and_whitespace();
    test_comments_and_blanks();
    test_unknown_key_is_distinct();
    test_booleans();
    test_zones();
    test_bypass_qualifier();
    test_groups_are_named_and_contiguous();
    test_hotkey_defaults_and_spelling();
    test_hotkey_validation();
    test_prefs_table_and_round_trip();
    test_quoted_text_round_trip();

    /* the table and the writer stand on the parser: only once it holds,
     * and their failures count as much as its own */
    if (g_failures == 0) {
        test_settings_table();
        test_write_round_trip();
        test_write_defaults_as_comments();
    }
    if (g_failures == 0) {
        printf("config_test: all tests passed\n");
        return 0;
    }
    printf("config_test: %d failure(s)\n", g_failures);
    return 1;
}
