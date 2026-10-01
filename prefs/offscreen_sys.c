/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen_sys.c - see offscreen_sys.h.
 */

#ifdef __amigaos4__
/* Call the system by name, as prefs_io.c does. */
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <exec/types.h>
#include <dos/dos.h>
#include <proto/dos.h>

#include "edgesnap.h"           /* ESTagData: an argument the width of a pointer */
#include "offscreen.h"
#include "offscreen_sys.h"

#if defined(__MORPHOS__)
#define ESO_ENV     "ENV:Sys/icontrol.conf"
#define ESO_ENVARC  "ENVARC:Sys/icontrol.conf"
#define ESO_NAME    "MorphOS"
#define ESO_EDITOR  "IControl"
#define ESO_FACTORY 0           /* off as delivered */
#elif defined(__AROS__)
#define ESO_ENV     "ENV:SYS/icontrol.prefs"
#define ESO_ENVARC  "ENVARC:SYS/icontrol.prefs"
#define ESO_NAME    "AROS"
#define ESO_EDITOR  "IControl"
#define ESO_FACTORY 1           /* on as delivered */
#elif defined(__amigaos4__)
#define ESO_ENV     "ENV:Sys/gui.prefs"
#define ESO_ENVARC  "ENVARC:Sys/gui.prefs"
#define ESO_NAME    "AmigaOS 4"
#define ESO_EDITOR  "GUI"
#define ESO_FACTORY 1           /* on as delivered */
#else
#error "offscreen_sys.c is for MorphOS, AROS and AmigaOS 4"
#endif

static unsigned char eso_buf[ES_OFFS_FILE_MAX];
static unsigned char eso_out[ES_OFFS_FILE_MAX];

const char *eso_system(void)
{
    return ESO_NAME;
}

const char *eso_editor(void)
{
    return ESO_EDITOR;
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
#elif defined(__AROS__)
    return es_aros_offscreen(buf, len);
#else
    return es_os4_offscreen(buf, len);
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
#if defined(__AROS__)
    return es_aros_set_offscreen(eso_out, len, on) < 0 ? -1 : len;
#else
    return es_os4_set_offscreen(eso_out, len, on) < 0 ? -1 : len;
#endif
#endif
}

/* What the system's IControl writes from its defaults, bit as asked;
 * -1 on AmigaOS 4, where there is nothing to write from nothing. */
static int eso_factory(int on)
{
#if defined(__MORPHOS__)
    return es_mos_default(on, (char *)eso_out, ES_OFFS_FILE_MAX);
#elif defined(__AROS__)
    return es_aros_default(on, eso_out);
#else
    (void)on;
    return -1;
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

/* ------------------------------------------------------ from a Shell */

/* Case-blind comparison for the Shell words. */
static int eso_word(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        char x = *a++;
        char y = *b++;

        if (x >= 'a' && x <= 'z') {
            x = (char)(x - 'a' + 'A');
        }
        if (y >= 'a' && y <= 'z') {
            y = (char)(y - 'a' + 'A');
        }
        if (x != y) {
            return 0;
        }
    }
    return *a == *b;
}

int eso_is_command(int argc, char **argv)
{
    return argc >= 2 && eso_word(argv[1], "OFFSCREEN");
}

int eso_command(int argc, char **argv)
{
    int state = eso_query(0);

    if (state < 0 || eso_query(1) < 0) {
        Printf((CONST_STRPTR)"EdgeSnapPrefs: the %s preferences file of %s "
               "is not one this program can read\n",
               (ESTagData)ESO_EDITOR, (ESTagData)ESO_NAME);
        return RETURN_ERROR;
    }
    if (argc >= 3 && eso_word(argv[2], "QUERY")) {
        Printf((CONST_STRPTR)"windows can move off-screen: %s\n",
               (ESTagData)(state ? "yes" : "no"));
        return state ? RETURN_OK : RETURN_WARN;
    }
    if (argc >= 3 && (eso_word(argv[2], "ON") || eso_word(argv[2], "OFF"))) {
        state = eso_word(argv[2], "ON");
        if (!eso_set(state, 1, 1)) {
            Printf((CONST_STRPTR)"EdgeSnapPrefs: the %s preferences file of "
                   "%s could not be written\n",
                   (ESTagData)ESO_EDITOR, (ESTagData)ESO_NAME);
            return RETURN_ERROR;
        }
        Printf((CONST_STRPTR)"windows can move off-screen: %s\n",
               (ESTagData)(state ? "yes" : "no"));
        return RETURN_OK;
    }
    Printf((CONST_STRPTR)"EdgeSnapPrefs OFFSCREEN QUERY|ON|OFF\n");
    return RETURN_ERROR;
}
