/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen_sys.c - see offscreen_sys.h.
 */

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "offscreen.h"
#include "offscreen_sys.h"

#if defined(__MORPHOS__)
#define ESO_ENV     "ENV:Sys/icontrol.conf"
#define ESO_ENVARC  "ENVARC:Sys/icontrol.conf"
#define ESO_NAME    "MorphOS"
#define ESO_FACTORY 0           /* off as delivered */
#elif defined(__AROS__)
#define ESO_ENV     "ENV:SYS/icontrol.prefs"
#define ESO_ENVARC  "ENVARC:SYS/icontrol.prefs"
#define ESO_NAME    "AROS"
#define ESO_FACTORY 1           /* on as delivered */
#else
#error "offscreen_sys.c is for MorphOS and AROS"
#endif

static unsigned char eso_buf[ES_OFFS_FILE_MAX];
static unsigned char eso_out[ES_OFFS_FILE_MAX];

const char *eso_system(void)
{
    return ESO_NAME;
}

/* The whole file, or -1 when it is not there, or -2 when it is there
 * and longer than any file of this kind. */
static int eso_read(const char *name, unsigned char *buf)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_OLDFILE);
    LONG n;

    if (fh == 0) {
        return -1;
    }
    n = Read(fh, buf, ES_OFFS_FILE_MAX);
    if (n == ES_OFFS_FILE_MAX) {
        UBYTE more;

        if (Read(fh, &more, 1) == 1) {
            n = -2;
        }
    }
    Close(fh);
    return n < 0 && n != -2 ? -1 : (int)n;
}

static int eso_write(const char *name, const unsigned char *buf, int len)
{
    BPTR fh = Open((CONST_STRPTR)name, MODE_NEWFILE);
    LONG n;

    if (fh == 0) {
        return 0;
    }
    n = Write(fh, (APTR)buf, len);
    Close(fh);
    return n == len;
}

static int eso_get(const unsigned char *buf, int len)
{
#if defined(__MORPHOS__)
    return es_mos_offscreen((const char *)buf, len);
#else
    return es_aros_offscreen(buf, len);
#endif
}

/* The file with the bit as asked, into eso_out: its length, or -1. */
static int eso_change(const unsigned char *buf, int len, int on)
{
#if defined(__MORPHOS__)
    return es_mos_set_offscreen((const char *)buf, len, on,
                                (char *)eso_out, ES_OFFS_FILE_MAX);
#else
    int i;

    for (i = 0; i < len; i++) {
        eso_out[i] = buf[i];
    }
    return es_aros_set_offscreen(eso_out, len, on) < 0 ? -1 : len;
#endif
}

/* What the system's IControl writes from its defaults, bit as asked. */
static int eso_factory(int on)
{
#if defined(__MORPHOS__)
    return es_mos_default(on, (char *)eso_out, ES_OFFS_FILE_MAX);
#else
    return es_aros_default(on, eso_out);
#endif
}

int eso_query(int archived)
{
    int len = -1;

    if (!archived) {
        len = eso_read(ESO_ENV, eso_buf);
    }
    if (len == -1) {
        len = eso_read(ESO_ENVARC, eso_buf);
    }
    if (len == -1) {
        return ESO_FACTORY;        /* no file: the system's default */
    }
    if (len < 0) {
        return -1;
    }
    return eso_get(eso_buf, len);
}

/*
 * One file: what is there, else what `from` has, else the system's
 * defaults, with the bit as asked. The archived file is made from the
 * defaults when it is missing, not from the live one: a setting the
 * user only tried with Use was never meant to outlive the session.
 */
static int eso_set_one(const char *name, const char *from, int on)
{
    int len = eso_read(name, eso_buf);
    int out;

    if (len >= 0 && eso_get(eso_buf, len) == on) {
        return 1;                  /* it already says so */
    }
    if (len == -1 && from != 0) {
        len = eso_read(from, eso_buf);
    }
    if (len == -1) {
        if (on == ESO_FACTORY) {
            return 1;              /* no file: the default says so */
        }
        out = eso_factory(on);
    } else if (len < 0) {
        return 0;
    } else {
        out = eso_change(eso_buf, len, on);
    }
    if (out < 0) {
        return 0;                  /* not a file we understand: hands off */
    }
    return eso_write(name, eso_out, out);
}

int eso_set(int on, int live, int archived)
{
    int ok = 1;

    /* the archive first: should the live one fail, what survives the
     * next start is already what was asked for */
    if (archived && !eso_set_one(ESO_ENVARC, 0, on)) {
        ok = 0;
    }
    if (live && !eso_set_one(ESO_ENV, ESO_ENVARC, on)) {
        ok = 0;
    }
    return ok;
}
