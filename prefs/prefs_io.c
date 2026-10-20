/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 */

#ifdef __amigaos4__
/* Call the system by name (Open, Write, ...) rather than through an
 * interface pointer at every site - the same shim the commodity uses. */
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <proto/dos.h>
#include <proto/commodities.h>
#include <dos/dos.h>
#include <libraries/commodities.h>
#include <string.h>

#include "prefs_io.h"

#define ESP_BUF 4096

/*
 * One buffer, not one per call: four kilobytes on the stack of a
 * program that also runs a GUI toolkit is how you get a Grim Reaper
 * instead of a window. Nothing here is re-entrant.
 */
static char esp_buf[ESP_BUF];
static char esp_line[256];

static int esp_read_file(const char *name, ESConfig *cfg, ESHotkeys *keys)
{
    char *buf = esp_buf;
    char *line = esp_line;
    BPTR file;
    LONG got;
    int at = 0;
    int n = 0;
    int any = 0;

    file = Open((STRPTR)name, MODE_OLDFILE);
    if (file == 0) {
        return 0;
    }
    got = Read(file, buf, (LONG)ESP_BUF - 1);
    Close(file);
    if (got <= 0) {
        return 0;
    }
    buf[got] = '\0';

    /* One line at a time, exactly as the commodity reads it: a bad
     * line is reported and skipped, never fatal. */
    for (at = 0; at <= (int)got; at++) {
        if (buf[at] == '\n' || buf[at] == '\0') {
            line[n] = '\0';
            if (n > 0) {
                es_prefs_line(cfg, keys, line);
                any = 1;
            }
            n = 0;
            if (buf[at] == '\0') {
                break;
            }
        } else if (n < 255) {
            line[n++] = buf[at];
        }
    }
    return any;
}

int esp_load(ESConfig *cfg, ESHotkeys *keys)
{
    es_config_defaults(cfg);
    es_hotkeys_defaults(keys);
    if (esp_read_file(ESP_ENV, cfg, keys)) {
        return 1;
    }
    return esp_read_file(ESP_ENVARC, cfg, keys);
}

static int esp_write_file(const char *name, const char *text, int len)
{
    BPTR file;
    LONG put;

    file = Open((STRPTR)name, MODE_NEWFILE);
    if (file == 0) {
        return 0;
    }
    put = Write(file, (APTR)text, (LONG)len);
    Close(file);
    return put == (LONG)len;
}

int esp_store(const ESConfig *cfg, const ESHotkeys *keys, int permanent)
{
    char *buf = esp_buf;
    int len;
    int ok;

    len = es_prefs_write(cfg, keys, buf, ESP_BUF);
    if (len <= 0 || len >= ESP_BUF) {
        return 0;
    }
    /*
     * ENV: last. If both are written, the archived copy is on disk
     * before the live one appears - and the live one is what the
     * running EdgeSnap is watching, so it never reacts to settings
     * that are not saved yet.
     */
    ok = 1;
    if (permanent) {
        ok = esp_write_file(ESP_ENVARC, buf, len);
    }
    if (!esp_write_file(ESP_ENV, buf, len)) {
        ok = 0;
    }
    return ok;
}

/* Compare the expression's fields, never its structure padding. */
static int esp_same_key(const struct InputXpression *a,
                            const struct InputXpression *b)
{
    return a->ix_Version == b->ix_Version && a->ix_Class == b->ix_Class &&
           a->ix_Code == b->ix_Code && a->ix_CodeMask == b->ix_CodeMask &&
           a->ix_Qualifier == b->ix_Qualifier &&
           a->ix_QualMask == b->ix_QualMask && a->ix_QualSame == b->ix_QualSame;
}

int esp_validate_hotkeys(const ESHotkeys *keys, int *bad, int *duplicate)
{
    struct InputXpression parsed[ES_HOTKEY_COUNT];
    int row[ES_HOTKEY_COUNT];
    const ESSetting *t;
    int count, i, j, n = 0;

    t = es_settings(&count);
    memset(parsed, 0, sizeof(parsed));
    *bad = -1;
    *duplicate = -1;
    for (i = 0; i < count; i++) {
        const char *text;

        if (t[i].kind != ES_SET_KEY) {
            continue;
        }
        text = es_setting_text(keys, i);
        if (*text == '\0') {
            continue;
        }
        if (ParseIX((STRPTR)text, &parsed[n]) != 0) {
            *bad = i;
            return 0;
        }
        /* Compare the parsed meaning, regardless of qualifier order. */
        for (j = 0; j < n; j++) {
            if (esp_same_key(&parsed[n], &parsed[j])) {
                *bad = i;
                *duplicate = row[j];
                return 0;
            }
        }
        row[n++] = i;
    }
    return 1;
}
