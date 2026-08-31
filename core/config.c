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
        if (*ref == '\0' || es_lower((unsigned char)*s) != *ref) {
            return 0;
        }
        s++;
        ref++;
    }
    return *ref == '\0';
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

int es_config_line(ESConfig *cfg, const char *line)
{
    char key[ES_CFG_MAXTOK];
    char val[ES_CFG_MAXTOK];
    const char *eq;
    const char *p;
    int n;

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

    p = eq + 1;
    while (es_blank((unsigned char)*p)) {
        p++;
    }
    n = 0;
    while (*p != '\0') {
        if (n >= ES_CFG_MAXTOK - 1) {
            return ES_ERR_BAD_ARGS;
        }
        val[n++] = *p;
        p++;
    }
    while (n > 0 && es_blank((unsigned char)val[n - 1])) {
        n--;
    }
    val[n] = '\0';
    if (n == 0) {
        return ES_ERR_BAD_ARGS;
    }
    return es_config_set(cfg, key, val);
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
    { "ZONES", "Zones", "Which corners and edges react",
      "Which screen edges and corners snap a window.",
      ES_SET_ZONES, 0, 0, 0 },
    { "BYPASSQUAL", "Zones", "Hold to ignore zones",
      "Qualifier that lets a window be dragged past the zones.",
      ES_SET_CHOICE, 0, 0, es_qual_choices },
    { "EDGEPX", "Sensitivity", "Edge distance",
      "How close to an edge the pointer must come, in pixels.",
      ES_SET_INT, ES_EDGE_PX_MIN, ES_EDGE_PX_MAX, 0 },
    { "CORNERDIV", "Sensitivity", "Corner size",
      "Corner length is the usable height divided by this.",
      ES_SET_INT, ES_CORNER_DIV_MIN, ES_CORNER_DIV_MAX, 0 },
    { "DRAGMINPX", "Sensitivity", "Drag threshold",
      "How far the pointer must travel before it counts as a drag.",
      ES_SET_INT, ES_DRAG_MIN_PX_MIN, ES_DRAG_MIN_PX_MAX, 0 },
    { "PREVIEW", "Look", "Show the preview frame",
      "Outline where the window will land while dragging.",
      ES_SET_BOOL, 0, 0, 0 },
    { "PANELDETECT", "Docks", "Keep clear of docks",
      "Detect docks and panels, and never cover them.",
      ES_SET_BOOL, 0, 0, 0 },
    { "PANELMARGIN", "Docks", "Space around a dock",
      "Breathing room left around a detected dock, in pixels.",
      ES_SET_INT, ES_PANEL_MARGIN_MIN, ES_PANEL_MARGIN_MAX, 0 },
    { "MARGINLEFT", "Margins", "Left",
      "Extra space left free at the left edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "MARGINTOP", "Margins", "Top",
      "Extra space left free at the top edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "MARGINRIGHT", "Margins", "Right",
      "Extra space left free at the right edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 },
    { "MARGINBOTTOM", "Margins", "Bottom",
      "Extra space left free at the bottom edge.",
      ES_SET_INT, ES_MARGIN_MIN, ES_MARGIN_MAX, 0 }
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

int es_config_write(const ESConfig *cfg, char *buf, int size)
{
    static const char header[] =
        "# EdgeSnap preferences\n"
        "# Written by the EdgeSnap preferences program.\n"
        "# Every setting also works as a Shell argument:\n"
        "#   EdgeSnap ZONES=halves EDGEPX=24\n"
        "\n";
    char num[16];
    char zones[96];
    int at = 0;
    int i;

    if (cfg == 0 || buf == 0 || size <= 0) {
        return 0;
    }
    at = es_emit(buf, size, at, header);
    for (i = 0; i < ES_SETTING_COUNT; i++) {
        const ESSetting *s = &es_setting_table[i];
        int v = es_setting_value(cfg, i);
        const char *text;

        switch (s->kind) {
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
        at = es_emit(buf, size, at, s->key);
        at = es_emit(buf, size, at, "=");
        at = es_emit(buf, size, at, text);
        at = es_emit(buf, size, at, "\n");
    }
    buf[at < size ? at : size - 1] = '\0';
    return at;
}
