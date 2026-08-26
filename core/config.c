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
        if (!es_parse_int(value, &iv) || iv < 1 || iv > 200) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.edge_px = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "cornerdiv")) {
        if (!es_parse_int(value, &iv) || iv < 2 || iv > 16) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.corner_div = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "dragminpx")) {
        if (!es_parse_int(value, &iv) || iv < 1 || iv > 200) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->engine.drag_min_px = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "marginleft")) {
        if (!es_parse_int(value, &iv) || iv < 0) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.l = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "margintop")) {
        if (!es_parse_int(value, &iv) || iv < 0) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.t = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "marginright")) {
        if (!es_parse_int(value, &iv) || iv < 0) {
            return ES_ERR_BAD_ARGS;
        }
        cfg->margin.r = iv;
        return ES_OK;
    }
    if (es_key_eq(key, "marginbottom")) {
        if (!es_parse_int(value, &iv) || iv < 0) {
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
        if (!es_parse_int(value, &iv) || iv < 0 || iv > 200) {
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
