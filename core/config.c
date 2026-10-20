/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * config.c - portable EdgeSnap preferences parser.
 */

#include "config.h"

#define ES_CFG_MAXTOK 64

void es_config_defaults(ESConfig *cfg)
{
    es_engine_config_defaults(&cfg->engine);
    cfg->margin.l = 0;
    cfg->margin.t = 0;
    cfg->margin.r = 0;
    cfg->margin.b = 0;
    cfg->panel_detect = 1;
    cfg->panel_margin = ES_PANEL_MARGIN_PX;
    cfg->preview = 1;
    cfg->cycle_sizes = 1;
    cfg->animate = 1;
    cfg->drag_restore = 1;
    cfg->bypass_qual = ES_QUAL_NONE;
}

static int es_lower(int c)
{
    if (c >= 'A' && c <= 'Z') {
        return c - 'A' + 'a';
    }
    return c;
}

static int es_blank(int c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Case-insensitive compare, ignoring '_' and '-' in the input so that
 * EDGE_PX, edge-px and EdgePx are all the same key. */
static int es_key_eq(const char *s, const char *ref)
{
    while (*s != '\0') {
        if (*s == '_' || *s == '-') {
            s++;
            continue;
        }
        if (*ref == '\0' || es_lower((unsigned char)*s) !=
                            es_lower((unsigned char)*ref)) {
            return 0;
        }
        s++;
        ref++;
    }
    return *ref == '\0';
}

static const char *const es_hotkey_names[ES_HOTKEY_COUNT] = {
    "HOTKEYLEFT", "HOTKEYRIGHT", "HOTKEYMAX",
    "HOTKEYTOPLEFT", "HOTKEYTOPRIGHT", "HOTKEYBOTTOMLEFT", "HOTKEYBOTTOMRIGHT",
    "HOTKEYCENTRE", "HOTKEYRESTORE", "HOTKEYSELECT", "CX_POPKEY",
    "HOTKEYDUMP", "HOTKEYFRAME"
};

static const char *const es_hotkey_defaults[ES_HOTKEY_COUNT] = {
    "ctrl alt cursor_left", "ctrl alt cursor_right", "ctrl alt cursor_up",
    "", "", "", "", "ctrl alt c", "ctrl alt cursor_down",
    "ctrl alt z", "", "ctrl alt d", "ctrl alt f"
};

void es_hotkeys_defaults(ESHotkeys *keys)
{
    int i, n;

    for (i = 0; i < ES_HOTKEY_COUNT; i++) {
        const char *p = es_hotkey_defaults[i];

        for (n = 0; n < ES_HOTKEY_SIZE; n++) {
            keys->key[i][n] = *p;
            if (*p != '\0') {
                p++;
            }
        }
    }
}

static int es_hotkey_index(const char *key)
{
    int i;

    if (key == 0) {
        return -1;
    }
    if (es_key_eq(key, "popkey") || es_key_eq(key, "cxpopkey")) {
        return ES_HK_POPKEY;
    }
    for (i = 0; i < ES_HOTKEY_COUNT; i++) {
        if (es_key_eq(key, es_hotkey_names[i])) {
            return i;
        }
    }
    return -1;
}

/* Unlike setting names, the words none/off ignore case only. */
static int es_text_eq(const char *a, const char *b)
{
    while (*a != '\0' && es_lower((unsigned char)*a) ==
                            es_lower((unsigned char)*b)) {
        a++;
        b++;
    }
    return *a == *b;
}

int es_prefs_set(ESConfig *cfg, ESHotkeys *keys,
                 const char *key, const char *value)
{
    int index = es_hotkey_index(key);
    char text[ES_HOTKEY_SIZE];
    int n = 0;
    int first = 0;
    int last;
    int i;

    if (key == 0 || value == 0) {
        return ES_ERR_BAD_ARGS;
    }
    if (index < 0 || keys == 0) {
        return cfg != 0 ? es_config_set(cfg, key, value) : ES_ERR_BAD_ARGS;
    }
    while (value[n] != '\0') {
        unsigned char c = (unsigned char)value[n];

        if (n >= ES_HOTKEY_SIZE - 1 || c < 32 || c > 126) {
            return ES_ERR_BAD_ARGS;
        }
        text[n] = (char)c;
        n++;
    }
    last = n;
    while (first < last && text[first] == ' ') { first++; }
    while (last > first && text[last - 1] == ' ') { last--; }
    if (first < last && (text[first] == '"' || text[last - 1] == '"')) {
        if (last - first < 2 || text[first] != '"' || text[last - 1] != '"') {
            return ES_ERR_BAD_ARGS;
        }
        first++;
        last--;
    }
    n = last - first;
    for (i = 0; i < n; i++) {
        text[i] = text[first + i];
    }
    text[n] = '\0';
    if (es_text_eq(text, "none") || es_text_eq(text, "off")) {
        n = 0;
    }
    /* Zero the tail too, without changing anything before validation.
     * value may itself point into keys. */
    for (i = 0; i < ES_HOTKEY_SIZE; i++) {
        keys->key[index][i] = i < n ? text[i] : '\0';
    }
    return ES_OK;
}

static int es_parse_int(const char *v, int *out)
{
    int sign = 1;
    int seen = 0;
    long acc = 0;

    while (es_blank((unsigned char)*v)) {
        v++;
    }
    if (*v == '-') {
        sign = -1;
        v++;
    } else if (*v == '+') {
        v++;
    }
    while (*v >= '0' && *v <= '9') {
        acc = acc * 10 + (*v - '0');
        if (acc > 100000L) {
            return 0;
        }
        seen = 1;
        v++;
    }
    while (es_blank((unsigned char)*v)) {
        v++;
    }
    if (!seen || *v != '\0') {
        return 0;
    }
    *out = (int)(acc * sign);
    return 1;
}

static int es_parse_bool(const char *v, int *out)
{
    static const char *yes[] = { "yes", "true", "on", "1", 0 };
    static const char *no[] = { "no", "false", "off", "0", 0 };
    int i;

    for (i = 0; yes[i] != 0; i++) {
        if (es_key_eq(v, yes[i])) {
            *out = 1;
            return 1;
        }
    }
    for (i = 0; no[i] != 0; i++) {
        if (es_key_eq(v, no[i])) {
            *out = 0;
            return 1;
        }
    }
    return 0;
}

/* One zone name (or group) -> mask bits, 0 when unknown. */
static unsigned es_zone_bits(const char *name)
{
    static const unsigned corners =
        ES_ZONEBIT(ES_ZONE_TOP_LEFT) | ES_ZONEBIT(ES_ZONE_TOP_RIGHT) |
        ES_ZONEBIT(ES_ZONE_BOTTOM_LEFT) | ES_ZONEBIT(ES_ZONE_BOTTOM_RIGHT);
    static const unsigned halves =
        ES_ZONEBIT(ES_ZONE_LEFT) | ES_ZONEBIT(ES_ZONE_RIGHT);

    if (es_key_eq(name, "all")) {
        return corners | halves | ES_ZONEBIT(ES_ZONE_MAX);
    }
    if (es_key_eq(name, "none")) {
        return 0;
    }
    if (es_key_eq(name, "corners") || es_key_eq(name, "quarters")) {
        return corners;
    }
    if (es_key_eq(name, "halves") || es_key_eq(name, "sides")) {
        return halves;
    }
    if (es_key_eq(name, "left")) {
        return ES_ZONEBIT(ES_ZONE_LEFT);
    }
    if (es_key_eq(name, "right")) {
        return ES_ZONEBIT(ES_ZONE_RIGHT);
    }
    if (es_key_eq(name, "topleft")) {
        return ES_ZONEBIT(ES_ZONE_TOP_LEFT);
    }
    if (es_key_eq(name, "topright")) {
        return ES_ZONEBIT(ES_ZONE_TOP_RIGHT);
    }
    if (es_key_eq(name, "bottomleft")) {
        return ES_ZONEBIT(ES_ZONE_BOTTOM_LEFT);
    }
    if (es_key_eq(name, "bottomright")) {
        return ES_ZONEBIT(ES_ZONE_BOTTOM_RIGHT);
    }
    if (es_key_eq(name, "maximize") || es_key_eq(name, "max") ||
        es_key_eq(name, "top")) {
        return ES_ZONEBIT(ES_ZONE_MAX);
    }
    return 0;
}

/* "left,right,corners" -> mask. Any unknown name fails the whole
 * value: a typo must not silently disable half the zones. */
static int es_parse_zones(const char *v, unsigned *out)
{
    char tok[ES_CFG_MAXTOK];
    unsigned mask = 0;
    int n = 0;
    int explicit_none = 0;

    for (;;) {
        if (*v == ',' || *v == '\0') {
            unsigned bits;

            tok[n] = '\0';
            if (n > 0) {
                if (es_key_eq(tok, "none")) {
                    explicit_none = 1;
                } else {
                    bits = es_zone_bits(tok);
                    if (bits == 0) {
                        return 0;
                    }
                    mask |= bits;
                }
            }
            n = 0;
            if (*v == '\0') {
                break;
            }
            v++;
            continue;
        }
        if (!es_blank((unsigned char)*v)) {
            if (n >= ES_CFG_MAXTOK - 1) {
                return 0;
            }
            tok[n++] = *v;
        }
        v++;
    }
    if (mask == 0 && !explicit_none) {
        return 0; /* empty value */
    }
    *out = mask;
    return 1;
}

static int es_parse_qual(const char *v, int *out)
{
    if (es_key_eq(v, "none") || es_key_eq(v, "off")) {
        *out = ES_QUAL_NONE;
        return 1;
    }
    if (es_key_eq(v, "alt")) {
        *out = ES_QUAL_ALT;
        return 1;
    }
    if (es_key_eq(v, "ctrl") || es_key_eq(v, "control")) {
        *out = ES_QUAL_CTRL;
        return 1;
    }
    if (es_key_eq(v, "shift")) {
        *out = ES_QUAL_SHIFT;
        return 1;
    }
    return 0;
}

int es_config_set(ESConfig *cfg, const char *key, const char *value)
{
    int iv;
    unsigned mask;

    if (key == 0 || value == 0) {
        return ES_ERR_BAD_ARGS;
    }
    if (es_key_eq(key, "edgepx")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_EDGE_PX_MIN || iv > ES_EDGE_PX_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.edge_px = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "cornerdiv")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_CORNER_DIV_MIN || iv > ES_CORNER_DIV_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.corner_div = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "dragminpx")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_DRAG_MIN_PX_MIN || iv > ES_DRAG_MIN_PX_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.drag_min_px = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "marginleft")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_MARGIN_MIN || iv > ES_MARGIN_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.l = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "margintop")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_MARGIN_MIN || iv > ES_MARGIN_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.t = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "marginright")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_MARGIN_MIN || iv > ES_MARGIN_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.r = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "marginbottom")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_MARGIN_MIN || iv > ES_MARGIN_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.b = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "paneldetect")) {
        if (!es_parse_bool(value, &iv)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->panel_detect = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "panelmargin")) {
        if (!es_parse_int(value, &iv) ||
            iv < ES_PANEL_MARGIN_MIN || iv > ES_PANEL_MARGIN_MAX) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->panel_margin = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "preview")) {
        if (!es_parse_bool(value, &iv)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->preview = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "cyclesizes")) {
        if (!es_parse_bool(value, &iv)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->cycle_sizes = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "animate")) {
        if (!es_parse_bool(value, &iv)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->animate = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "dragrestore")) {
        if (!es_parse_bool(value, &iv)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->drag_restore = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "zones")) {
        if (!es_parse_zones(value, &mask)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.zones_mask = mask;
        return ES_OK;
    }
    if (es_key_eq(key, "bypassqual") || es_key_eq(key, "bypass")) {
        if (!es_parse_qual(value, &iv)) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->bypass_qual = iv;
        return ES_OK;
    }
    return ES_ERR_UNSUPPORTED;
}

static int es_read_line(ESConfig *cfg, ESHotkeys *keys, const char *line)
{
    char key[ES_CFG_MAXTOK];
    char val[ES_CFG_MAXTOK];
    const char *eq;
    const char *p;
    int n;
    int hotkey;
    const char *end;

    if (line == 0) {
        return ES_ERR_BAD_ARGS;
    }
    while (es_blank((unsigned char)*line)) {
        line++;
    }
    if (*line == '\0' || *line == '#' || *line == ';') {
        return ES_OK;
    }
    eq = line;
    while (*eq != '=' && *eq != '\0') {
        eq++;
    }
    if (*eq != '=') {
        return ES_ERR_BAD_ARGS;
    }

    n = 0;
    for (p = line; p < eq; p++) {
        if (es_blank((unsigned char)*p)) {
            continue;
        }
        if (n >= ES_CFG_MAXTOK - 1) {
            return ES_ERR_BAD_ARGS;
        }
        key[n++] = *p;
    }
    key[n] = '\0';
    if (n == 0) {
        return ES_ERR_BAD_ARGS;
    }

    hotkey = keys != 0 && es_hotkey_index(key) >= 0;
    p = eq + 1;
    end = p;
    while (*end != '\0') { end++; }
    /* A line terminator is syntax, not part of an input description. */
    if (hotkey && end > p && end[-1] == '\n') {
        end--;
    }
    if (hotkey && end > p && end[-1] == '\r') { end--; }
    while (p < end && (hotkey ? *p == ' ' : es_blank((unsigned char)*p))) {
        p++;
    }
    n = 0;
    while (p < end) {
        if (n >= ES_CFG_MAXTOK - 1) {
            return ES_ERR_BAD_ARGS;
        }
        val[n++] = *p;
        p++;
    }
    while (n > 0 && (hotkey ? val[n - 1] == ' ' :
                             es_blank((unsigned char)val[n - 1]))) {
        n--;
    }
    val[n] = '\0';
    if (n == 0) {
        return ES_ERR_BAD_ARGS;
    }
    return es_prefs_set(cfg, keys, key, val);
}

int es_config_line(ESConfig *cfg, const char *line)
{
    return es_read_line(cfg, 0, line);
}

int es_prefs_line(ESConfig *cfg, ESHotkeys *keys, const char *line)
{
    return es_read_line(cfg, keys, line);
}


/* ------------------------------------------------- settings, described
 *
 * See the note in config.h: the table is the single description of the
 * vocabulary, and everything that writes a setting goes through
 * es_config_set() so the GUI can never accept what the file parser
 * would refuse.
 */

static const char *const es_qual_choices[] = {
    "none", "alt", "ctrl", "shift", 0
};

static const ESSetting es_setting_table[] = {
    { "ZONES", "Snapping", "Zones", "Which corners and edges react",
      "Which screen edges and corners snap a window.",
      ES_SET_ZONES, 0, 0, 0 },
    { "BYPASSQUAL", "Snapping", "Zones", "Hold to ignore zones",
      "Qualifier that lets a window be dragged past the zones.",
      ES_SET_CHOICE, 0, 0, es_qual_choices },
    { "DRAGRESTORE", "Snapping", "Zones", "Old size when dragged away",
      "A snapped window dragged by its title bar and let go away from the "
      "edges gets back the size it had before it was snapped, under the "
      "pointer. With the bypass key held it keeps the snapped size.",
      ES_SET_BOOL, 0, 0, 0 },
    { "EDGEPX", "Snapping", "Sensitivity", "Edge distance",
      "How close to an edge the pointer must come, in pixels.",
      ES_SET_INT, ES_EDGE_PX_MIN, ES_EDGE_PX_MAX, 0 },
    { "CORNERDIV", "Snapping", "Sensitivity", "Corner size",
      "0: a corner only where the pointer is pushed into it. From 2: "
      "a band at each end of the side edges, the usable height divided "
      "by this.",
      ES_SET_INT, ES_CORNER_DIV_MIN, ES_CORNER_DIV_MAX, 0 },
    { "DRAGMINPX", "Snapping", "Sensitivity", "Drag threshold",
      "How far the pointer must travel before it counts as a drag.",
      ES_SET_INT, ES_DRAG_MIN_PX_MIN, ES_DRAG_MIN_PX_MAX, 0 },
    { "PREVIEW", "Snapping", "Look", "Show the preview frame",
      "Outline where the window will land while dragging.",
      ES_SET_BOOL, 0, 0, 0 },
    { "ANIMATE", "Snapping", "Look", "Glide into place",
      "Move the window to its zone in a few steps instead of one. "
      "Every step asks the program to redraw itself, so on a slow "
      "machine or with a large window the plain jump looks better.",
      ES_SET_BOOL, 0, 0, 0 },
    { "PANELDETECT", "Snapping", "Docks", "Keep clear of docks",
      "Detect docks and panels, and never cover them.",
      ES_SET_BOOL, 0, 0, 0 },
    { "PANELMARGIN", "Snapping", "Docks", "Space around a dock",
      "Breathing room left around a detected dock, in pixels.",
      ES_SET_INT, ES_PANEL_MARGIN_MIN, ES_PANEL_MARGIN_MAX, 0 },
    { "MARGINLEFT", "Snapping", "Margins", "Left",
      "Extra space left free at the left edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "MARGINTOP", "Snapping", "Margins", "Top",
      "Extra space left free at the top edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "MARGINRIGHT", "Snapping", "Margins", "Right",
      "Extra space left free at the right edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "MARGINBOTTOM", "Snapping", "Margins", "Bottom",
      "Extra space left free at the bottom edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "HOTKEYLEFT", "Hotkeys", "Snap the active window", "Left half",
      "Snap left. Repeat for the width cycle below.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYRIGHT", "Hotkeys", "Snap the active window", "Right half",
      "Snap right. Repeat for the width cycle below.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYMAX", "Hotkeys", "Snap the active window", "Maximize",
      "Fill the usable screen area.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYTOPLEFT", "Hotkeys", "Snap the active window", "Top left",
      "Snap to the top left quarter.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYTOPRIGHT", "Hotkeys", "Snap the active window", "Top right",
      "Snap to the top right quarter.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYBOTTOMLEFT", "Hotkeys", "Snap the active window", "Bottom left",
      "Snap to the bottom left quarter.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYBOTTOMRIGHT", "Hotkeys", "Snap the active window", "Bottom right",
      "Snap to the bottom right quarter.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYCENTRE", "Hotkeys", "Snap the active window", "Centre",
      "Keep the window's own size and put it in the middle.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYRESTORE", "Hotkeys", "Snap the active window", "Put back",
      "Restore the box the window had before its first snap.",
      ES_SET_KEY, 0, 0, 0 },
    { "CYCLESIZES", "Hotkeys", "Snap the active window", "Repeat a hotkey for a third",
      "Pressing the same side hotkey again gives the window two "
      "thirds of the screen, then one third, then a half again.",
      ES_SET_BOOL, 0, 0, 0 },
    { "HOTKEYSELECT", "Hotkeys", "Open", "Selector",
      "Open the selector under the pointer. The same hotkey closes it.",
      ES_SET_KEY, 0, 0, 0 },
    { "CX_POPKEY", "Hotkeys", "Open", "Preferences",
      "Open these preferences, as Exchange's Show does. POPKEY works too.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYDUMP", "Hotkeys", "Diagnostics", "Window dump",
      "Print the windows and dock decisions in the commodity's output.",
      ES_SET_KEY, 0, 0, 0 },
    { "HOTKEYFRAME", "Hotkeys", "Diagnostics", "Preview frame test",
      "Show the preview frame briefly, without a drag.",
      ES_SET_KEY, 0, 0, 0 }
};

#define ES_SETTING_COUNT \
    ((int)(sizeof(es_setting_table) / sizeof(es_setting_table[0])))

const ESSetting *es_settings(int *count)
{
    if (count != 0) {
        *count = ES_SETTING_COUNT;
    }
    return es_setting_table;
}

int es_setting_value(const ESConfig *cfg, int index)
{
    if (cfg == 0 || index < 0 || index >= ES_SETTING_COUNT) {
        return 0;
    }
    /*
     * By key, not by position. This used to be a switch on the index,
     * which meant the table could not be reordered without silently
     * handing the preferences window the wrong field: grouping the
     * settings into sections moved four rows and broke it at once. The
     * parser above already dispatches on the key; so does this.
     */
    {
        const char *key = es_setting_table[index].key;

        if (es_key_eq(key, "zones")) {
            return (int)cfg->engine.zones_mask;
        }
        if (es_key_eq(key, "bypassqual")) {
            return cfg->bypass_qual;
        }
        if (es_key_eq(key, "edgepx")) {
            return cfg->engine.edge_px;
        }
        if (es_key_eq(key, "cornerdiv")) {
            return cfg->engine.corner_div;
        }
        if (es_key_eq(key, "dragminpx")) {
            return cfg->engine.drag_min_px;
        }
        if (es_key_eq(key, "preview")) {
            return cfg->preview;
        }
        if (es_key_eq(key, "cyclesizes")) {
            return cfg->cycle_sizes;
        }
        if (es_key_eq(key, "animate")) {
            return cfg->animate;
        }
        if (es_key_eq(key, "dragrestore")) {
            return cfg->drag_restore;
        }
        if (es_key_eq(key, "paneldetect")) {
            return cfg->panel_detect;
        }
        if (es_key_eq(key, "panelmargin")) {
            return cfg->panel_margin;
        }
        if (es_key_eq(key, "marginleft")) {
            return cfg->margin.l;
        }
        if (es_key_eq(key, "margintop")) {
            return cfg->margin.t;
        }
        if (es_key_eq(key, "marginright")) {
            return cfg->margin.r;
        }
        if (es_key_eq(key, "marginbottom")) {
            return cfg->margin.b;
        }
    }
    return 0;
}

/* Whole numbers only, and no sprintf: this is C89 that also has to
 * build without a C runtime inside the library. */
static char *es_num(int v, char *end)
{
    int neg = v < 0;
    unsigned u = (unsigned)(neg ? -v : v);

    *--end = '\0';
    do {
        *--end = (char)('0' + (u % 10u));
        u /= 10u;
    } while (u != 0u);
    if (neg) {
        *--end = '-';
    }
    return end;
}

/* The zone mask as words. The three sets people actually use get their
 * own name; anything else is spelled out. */
static const char *es_zones_text(unsigned mask, char *buf, int size)
{
    static const struct { int zone; const char *name; } names[] = {
        { ES_ZONE_LEFT, "left" },
        { ES_ZONE_RIGHT, "right" },
        { ES_ZONE_TOP_LEFT, "topleft" },
        { ES_ZONE_TOP_RIGHT, "topright" },
        { ES_ZONE_BOTTOM_LEFT, "bottomleft" },
        { ES_ZONE_BOTTOM_RIGHT, "bottomright" },
        { ES_ZONE_MAX, "maximize" }
    };
    unsigned halves = ES_ZONEBIT(ES_ZONE_LEFT) | ES_ZONEBIT(ES_ZONE_RIGHT);
    unsigned corners = ES_ZONEBIT(ES_ZONE_TOP_LEFT) |
                       ES_ZONEBIT(ES_ZONE_TOP_RIGHT) |
                       ES_ZONEBIT(ES_ZONE_BOTTOM_LEFT) |
                       ES_ZONEBIT(ES_ZONE_BOTTOM_RIGHT);
    int n = 0;
    int i;

    if (mask == ES_ZONEMASK_ALL) { return "all"; }
    if (mask == 0u)              { return "none"; }
    if (mask == halves)          { return "halves"; }
    if (mask == corners)         { return "corners"; }

    for (i = 0; i < 7; i++) {
        const char *p;

        if ((mask & ES_ZONEBIT(names[i].zone)) == 0u) {
            continue;
        }
        if (n > 0 && n < size - 1) {
            buf[n++] = ',';
        }
        for (p = names[i].name; *p != '\0' && n < size - 1; p++) {
            buf[n++] = *p;
        }
    }
    buf[n < size ? n : size - 1] = '\0';
    return buf;
}

int es_setting_apply(ESConfig *cfg, int index, int value)
{
    const ESSetting *s;
    char num[16];
    char zones[96];
    const char *text;

    if (cfg == 0 || index < 0 || index >= ES_SETTING_COUNT) {
        return ES_ERR_BAD_ARGS;
    }
    s = &es_setting_table[index];
    switch (s->kind) {
    case ES_SET_KEY:
        return ES_ERR_BAD_ARGS;
    case ES_SET_BOOL:
        text = value ? "yes" : "no";
        break;
    case ES_SET_CHOICE:
        {
            int n = 0;

            while (s->choices[n] != 0) {
                n++;
            }
            if (value < 0 || value >= n) {
                return ES_ERR_BAD_ARGS;
            }
            text = s->choices[value];
        }
        break;
    case ES_SET_ZONES:
        text = es_zones_text((unsigned)value, zones, (int)sizeof(zones));
        break;
    default:
        text = es_num(value, num + sizeof(num));
        break;
    }
    return es_config_set(cfg, s->key, text);
}

const char *es_setting_text(const ESHotkeys *keys, int index)
{
    int key;

    if (keys == 0 || index < 0 || index >= ES_SETTING_COUNT ||
        es_setting_table[index].kind != ES_SET_KEY) {
        return 0;
    }
    key = es_hotkey_index(es_setting_table[index].key);
    return key >= 0 ? keys->key[key] : 0;
}

int es_setting_apply_text(ESHotkeys *keys, int index, const char *value)
{
    if (keys == 0 || index < 0 || index >= ES_SETTING_COUNT ||
        es_setting_table[index].kind != ES_SET_KEY) {
        return ES_ERR_BAD_ARGS;
    }
    return es_prefs_set(0, keys, es_setting_table[index].key, value);
}

static int es_emit(char *buf, int size, int at, const char *text)
{
    while (*text != '\0') {
        if (at < size - 1) {
            buf[at] = *text;
        }
        at++;
        text++;
    }
    return at;
}

static int es_write(const ESConfig *cfg, const ESHotkeys *keys,
                     char *buf, int size)
{
    static const char header[] =
        "# EdgeSnap preferences\n"
        "# Written by the EdgeSnap preferences program.\n"
        "# Every setting also works as a Shell argument:\n"
        "#   EdgeSnap ZONES=halves EDGEPX=24\n"
        "# A line that starts with # is a setting left as it comes,\n"
        "# so that a later version's default reaches it.\n"
        "\n";
    /* Configuration-only callers keep the order used before pages. */
    static const char *const config_order[] = {
        "ZONES", "BYPASSQUAL", "CYCLESIZES", "DRAGRESTORE",
        "EDGEPX", "CORNERDIV", "DRAGMINPX", "PREVIEW", "ANIMATE",
        "PANELDETECT", "PANELMARGIN", "MARGINLEFT", "MARGINTOP",
        "MARGINRIGHT", "MARGINBOTTOM"
    };
    ESConfig def;
    char num[16];
    char zones[96];
    int at = 0;
    int i;
    int rows = keys != 0 ? ES_SETTING_COUNT :
        (int)(sizeof(config_order) / sizeof(config_order[0]));

    if (cfg == 0 || buf == 0 || size <= 0) {
        return 0;
    }
    /*
     * Until 0.41 every setting was written as it stood, defaults
     * included, and a file saved once pinned them all: when the corner
     * size's default went from 4 to 0, whoever had ever pressed Save
     * kept the 4 without having chosen it. A default stays a default.
     */
    es_config_defaults(&def);
    at = es_emit(buf, size, at, header);
    for (i = 0; i < rows; i++) {
        int index = i;
        const ESSetting *s;
        int v;
        const char *text;
        int is_default;
        int quote = 0;

        if (keys == 0) {
            for (index = 0; index < ES_SETTING_COUNT; index++) {
                if (es_key_eq(es_setting_table[index].key, config_order[i])) {
                    break;
                }
            }
        }
        s = &es_setting_table[index];
        v = es_setting_value(cfg, index);
        is_default = v == es_setting_value(&def, index);

        switch (s->kind) {
        case ES_SET_KEY:
            {
                int key = es_hotkey_index(s->key);

                text = es_setting_text(keys, index);
                /* Input syntax is case-insensitive, but a chosen spelling
                 * is still the user's, so only exact defaults are comments. */
                {
                    const char *a = text;
                    const char *b = es_hotkey_defaults[key];

                    while (*a != '\0' && *a == *b) { a++; b++; }
                    is_default = *a == *b;
                }
                if (*text == '\0') {
                    text = "none";
                } else {
                    int len = 0;

                    while (text[len] != '\0') { len++; }
                    quote = text[0] == ' ' || text[0] == '"' ||
                            text[len - 1] == ' ' || text[len - 1] == '"';
                }
            }
            break;
        case ES_SET_BOOL:
            text = v ? "yes" : "no";
            break;
        case ES_SET_CHOICE:
            text = s->choices[v];
            break;
        case ES_SET_ZONES:
            text = es_zones_text((unsigned)v, zones, (int)sizeof(zones));
            break;
        default:
            text = es_num(v, num + sizeof(num));
            break;
        }
        if (is_default) {
            at = es_emit(buf, size, at, "#");
        }
        at = es_emit(buf, size, at, s->key);
        at = es_emit(buf, size, at, "=");
        if (quote) { at = es_emit(buf, size, at, "\""); }
        at = es_emit(buf, size, at, text);
        if (quote) { at = es_emit(buf, size, at, "\""); }
        at = es_emit(buf, size, at, "\n");
    }
    buf[at < size ? at : size - 1] = '\0';
    return at;
}

int es_config_write(const ESConfig *cfg, char *buf, int size)
{
    return es_write(cfg, 0, buf, size);
}

int es_prefs_write(const ESConfig *cfg, const ESHotkeys *keys,
                   char *buf, int size)
{
    return es_write(cfg, keys, buf, size);
}
