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
#include <dos/dos.h>

#include "prefs_io.h"

#define ESP_BUF 4096

/*
 * One buffer, not one per call: four kilobytes on the stack of a
 * program that also runs a GUI toolkit is how you get a Grim Reaper
 * instead of a window. Nothing here is re-entrant.
 */
static char esp_buf[ESP_BUF];
static char esp_line[256];

static int esp_read_file(const char *name, ESConfig *cfg)
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
                es_config_line(cfg, line);
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

int esp_load(ESConfig *cfg)
{
    es_config_defaults(cfg);
    if (esp_read_file(ESP_ENV, cfg)) {
        return 1;
    }
    return esp_read_file(ESP_ENVARC, cfg);
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

int esp_store(const ESConfig *cfg, int permanent)
{
    char *buf = esp_buf;
    int len;
    int ok;

    len = es_config_write(cfg, buf, ESP_BUF);
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
