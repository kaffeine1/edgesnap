/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen_test.c - executable specification of the off-screen
 * setting as MorphOS and AROS keep it. The MorphOS texts are files the
 * system itself wrote: on a fresh 3.20 from IControl's defaults, and on
 * a machine whose user had set IControl up, each saved with the choice
 * off and then on (2026-10-01).
 */

#include <stdio.h>

#include "offscreen.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

/* C89 promises string literals of 509 characters: the files are kept
 * as lists of lines and put together here. */
/* MorphOS 3.20's IControl saved from its defaults (773 bytes) */
static const char *const mos_320_off[] = {
    "Flags=0x1283C0\n",
    "Hotkey=24 Trigger=\"control mouse_leftpress\"\n",
    "Hotkey=\"10\" Trigger=\"control lalt i\"\n",
    "Hotkey=\"12\" Trigger=\"control lalt mouse_leftpress\"\n",
    "Hotkey=\"13\" Trigger=\"lalt mouse_leftpress\"\n",
    "Hotkey=\"8\" Trigger=\"double mouse_leftpress\"\n",
    "Hotkey=\"37\" Trigger=\"ralt double mouse_leftpress\"\n",
    "Hotkey=\"38\" Trigger=\"lalt double mouse_leftpress\"\n",
    "Hotkey=\"16\" Trigger=\"control lalt u\"\n",
    "Hotkey=\"17\" Trigger=\"control lalt a\"\n",
    "Hotkey=\"35\" Trigger=\"control lalt b\"\n",
    "Hotkey=\"25\" Trigger=\"lcommand m\"\n",
    "Hotkey=\"26\" Trigger=\"lcommand n\"\n",
    "Hotkey=\"3\" Trigger=\"up\"\n",
    "Hotkey=\"4\" Trigger=\"down\"\n",
    "Hotkey=\"5\" Trigger=\"left\"\n",
    "Hotkey=\"6\" Trigger=\"right\"\n",
    "Hotkey=\"1\" Trigger=\"esc\"\n",
    "Hotkey=\"0\" Trigger=\"rcommand space\"\n",
    "Hotkey=\"2\" Trigger=\"return\"\n",
    "Hotkey=\"28\" Trigger=\"lcommand b\"\n",
    "Hotkey=\"27\" Trigger=\"lcommand v\"\n",
    "End\n",
    0
};

/* the same, "allow positioning beyond the screen limits" chosen (773 bytes) */
static const char *const mos_320_on[] = {
    "Flags=0x12C3C0\n",
    "Hotkey=24 Trigger=\"control mouse_leftpress\"\n",
    "Hotkey=\"10\" Trigger=\"control lalt i\"\n",
    "Hotkey=\"12\" Trigger=\"control lalt mouse_leftpress\"\n",
    "Hotkey=\"13\" Trigger=\"lalt mouse_leftpress\"\n",
    "Hotkey=\"8\" Trigger=\"double mouse_leftpress\"\n",
    "Hotkey=\"37\" Trigger=\"ralt double mouse_leftpress\"\n",
    "Hotkey=\"38\" Trigger=\"lalt double mouse_leftpress\"\n",
    "Hotkey=\"16\" Trigger=\"control lalt u\"\n",
    "Hotkey=\"17\" Trigger=\"control lalt a\"\n",
    "Hotkey=\"35\" Trigger=\"control lalt b\"\n",
    "Hotkey=\"25\" Trigger=\"lcommand m\"\n",
    "Hotkey=\"26\" Trigger=\"lcommand n\"\n",
    "Hotkey=\"3\" Trigger=\"up\"\n",
    "Hotkey=\"4\" Trigger=\"down\"\n",
    "Hotkey=\"5\" Trigger=\"left\"\n",
    "Hotkey=\"6\" Trigger=\"right\"\n",
    "Hotkey=\"1\" Trigger=\"esc\"\n",
    "Hotkey=\"0\" Trigger=\"rcommand space\"\n",
    "Hotkey=\"2\" Trigger=\"return\"\n",
    "Hotkey=\"28\" Trigger=\"lcommand b\"\n",
    "Hotkey=\"27\" Trigger=\"lcommand v\"\n",
    "End\n",
    0
};

/*
 * A machine whose IControl had been set up by its user, saved with
 * the choice off (816 bytes) */
static const char *const mos_user_off[] = {
    "Flags=0x9489C0\n",
    "Hotkey=39 Trigger=\"double mouse_leftpress\"\n",
    "Hotkey=24 Trigger=\"control mouse_leftpress\"\n",
    "Hotkey=\"10\" Trigger=\"control lalt i\"\n",
    "Hotkey=\"12\" Trigger=\"control lalt mouse_leftpress\"\n",
    "Hotkey=\"13\" Trigger=\"lalt mouse_leftpress\"\n",
    "Hotkey=\"8\" Trigger=\"double mouse_leftpress\"\n",
    "Hotkey=\"37\" Trigger=\"ralt double mouse_leftpress\"\n",
    "Hotkey=\"38\" Trigger=\"lalt double mouse_leftpress\"\n",
    "Hotkey=\"16\" Trigger=\"control lalt u\"\n",
    "Hotkey=\"17\" Trigger=\"control lalt a\"\n",
    "Hotkey=\"35\" Trigger=\"control lalt b\"\n",
    "Hotkey=\"25\" Trigger=\"lcommand m\"\n",
    "Hotkey=\"26\" Trigger=\"lcommand n\"\n",
    "Hotkey=\"3\" Trigger=\"up\"\n",
    "Hotkey=\"4\" Trigger=\"down\"\n",
    "Hotkey=\"5\" Trigger=\"left\"\n",
    "Hotkey=\"6\" Trigger=\"right\"\n",
    "Hotkey=\"1\" Trigger=\"esc\"\n",
    "Hotkey=\"0\" Trigger=\"rcommand space\"\n",
    "Hotkey=\"2\" Trigger=\"return\"\n",
    "Hotkey=\"28\" Trigger=\"lcommand b\"\n",
    "Hotkey=\"27\" Trigger=\"lcommand v\"\n",
    "End\n",
    0
};

/* and saved again with it on: one digit apart (816 bytes) */
static const char *const mos_user_on[] = {
    "Flags=0x94C9C0\n",
    "Hotkey=39 Trigger=\"double mouse_leftpress\"\n",
    "Hotkey=24 Trigger=\"control mouse_leftpress\"\n",
    "Hotkey=\"10\" Trigger=\"control lalt i\"\n",
    "Hotkey=\"12\" Trigger=\"control lalt mouse_leftpress\"\n",
    "Hotkey=\"13\" Trigger=\"lalt mouse_leftpress\"\n",
    "Hotkey=\"8\" Trigger=\"double mouse_leftpress\"\n",
    "Hotkey=\"37\" Trigger=\"ralt double mouse_leftpress\"\n",
    "Hotkey=\"38\" Trigger=\"lalt double mouse_leftpress\"\n",
    "Hotkey=\"16\" Trigger=\"control lalt u\"\n",
    "Hotkey=\"17\" Trigger=\"control lalt a\"\n",
    "Hotkey=\"35\" Trigger=\"control lalt b\"\n",
    "Hotkey=\"25\" Trigger=\"lcommand m\"\n",
    "Hotkey=\"26\" Trigger=\"lcommand n\"\n",
    "Hotkey=\"3\" Trigger=\"up\"\n",
    "Hotkey=\"4\" Trigger=\"down\"\n",
    "Hotkey=\"5\" Trigger=\"left\"\n",
    "Hotkey=\"6\" Trigger=\"right\"\n",
    "Hotkey=\"1\" Trigger=\"esc\"\n",
    "Hotkey=\"0\" Trigger=\"rcommand space\"\n",
    "Hotkey=\"2\" Trigger=\"return\"\n",
    "Hotkey=\"28\" Trigger=\"lcommand b\"\n",
    "Hotkey=\"27\" Trigger=\"lcommand v\"\n",
    "End\n",
    0
};

static int join(const char *const *lines, char *out, int size)
{
    int o = 0;
    int l, i;

    for (l = 0; lines[l] != 0; l++) {
        for (i = 0; lines[l][i] != '\0' && o < size; i++) {
            out[o++] = lines[l][i];
        }
    }
    return o;
}

static int same(const char *a, int alen, const char *b, int blen)
{
    int i;

    if (alen != blen) {
        return 0;
    }
    for (i = 0; i < alen; i++) {
        if (a[i] != b[i]) {
            return 0;
        }
    }
    return 1;
}

static int text_len(const char *t)
{
    int n = 0;

    while (t[n] != '\0') {
        n++;
    }
    return n;
}

/* What IControl writes from its defaults, either way. */
static void test_mos_factory(void)
{
    static char want[ES_OFFS_FILE_MAX], got[ES_OFFS_FILE_MAX];
    int wlen, glen;

    wlen = join(mos_320_off, want, ES_OFFS_FILE_MAX);
    glen = es_mos_default(0, got, ES_OFFS_FILE_MAX);
    CHECK(wlen == 773 && same(want, wlen, got, glen));
    CHECK(es_mos_offscreen(want, wlen) == 0);

    wlen = join(mos_320_on, want, ES_OFFS_FILE_MAX);
    glen = es_mos_default(1, got, ES_OFFS_FILE_MAX);
    CHECK(wlen == 773 && same(want, wlen, got, glen));
    CHECK(es_mos_offscreen(want, wlen) == 1);

    CHECK(es_mos_default(1, got, 772) == -1);
    CHECK(es_mos_default(1, got, 773) == 773);
}

/* Changing the choice in a file gives the file IControl writes. */
static void test_mos_toggle(void)
{
    static char off[ES_OFFS_FILE_MAX], on[ES_OFFS_FILE_MAX];
    static char got[ES_OFFS_FILE_MAX];
    int offlen, onlen, glen;

    offlen = join(mos_320_off, off, ES_OFFS_FILE_MAX);
    onlen = join(mos_320_on, on, ES_OFFS_FILE_MAX);
    glen = es_mos_set_offscreen(off, offlen, 1, got, ES_OFFS_FILE_MAX);
    CHECK(same(on, onlen, got, glen));
    glen = es_mos_set_offscreen(on, onlen, 0, got, ES_OFFS_FILE_MAX);
    CHECK(same(off, offlen, got, glen));
    glen = es_mos_set_offscreen(on, onlen, 1, got, ES_OFFS_FILE_MAX);
    CHECK(same(on, onlen, got, glen));

    offlen = join(mos_user_off, off, ES_OFFS_FILE_MAX);
    onlen = join(mos_user_on, on, ES_OFFS_FILE_MAX);
    CHECK(offlen == 816 && onlen == 816);
    CHECK(es_mos_offscreen(off, offlen) == 0);
    CHECK(es_mos_offscreen(on, onlen) == 1);
    glen = es_mos_set_offscreen(off, offlen, 1, got, ES_OFFS_FILE_MAX);
    CHECK(same(on, onlen, got, glen));
    glen = es_mos_set_offscreen(on, onlen, 0, got, ES_OFFS_FILE_MAX);
    CHECK(same(off, offlen, got, glen));
    CHECK(es_mos_set_offscreen(off, offlen, 1, got, 815) == -1);
}

/* The digits keep their width; a value that needs more gets more. */
static void test_mos_width(void)
{
    static const char zero[] = "Flags=0x0\nEnd\n";
    static const char padded[] = "Flags=0x00004000\nEnd\n";
    static const char crlf[] = "Flags=0x12c3c0\r\nEnd\r\n";
    char got[64];
    int glen;

    glen = es_mos_set_offscreen(zero, text_len(zero), 1, got, sizeof(got));
    CHECK(same("Flags=0x4000\nEnd\n", 17, got, glen));
    glen = es_mos_set_offscreen(padded, text_len(padded), 0, got,
                                sizeof(got));
    CHECK(same("Flags=0x00000000\nEnd\n", 21, got, glen));
    CHECK(es_mos_offscreen(crlf, text_len(crlf)) == 1);
    glen = es_mos_set_offscreen(crlf, text_len(crlf), 0, got, sizeof(got));
    CHECK(same("Flags=0x1283C0\r\nEnd\r\n", 21, got, glen));
}

/* Anything else is not touched. */
static void test_mos_refuses(void)
{
    static const char *const bad[] = {
        "Hotkey=24 Trigger=\"control mouse_leftpress\"\nEnd\n",
        "Flags=0x\nEnd\n",
        "Flags=0x123456789\nEnd\n",
        "Flags=0x12G4\nEnd\n",
        "Flags=1283C0\nEnd\n",
        "XFlags=0x4000\nEnd\n",
        "",
        0
    };
    static const char later[] = "; a line first\nFlags=0x4000\nEnd\n";
    char got[64];
    int i;

    for (i = 0; bad[i] != 0; i++) {
        CHECK(es_mos_offscreen(bad[i], text_len(bad[i])) == -1);
        CHECK(es_mos_set_offscreen(bad[i], text_len(bad[i]), 1, got,
                                   sizeof(got)) == -1);
    }
    CHECK(es_mos_offscreen(later, text_len(later)) == 1);
}

static unsigned long flags_of(const unsigned char *f)
{
    const unsigned char *p = f + 34 + 20;

    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

/* AROS: what its IControl editor writes from its defaults. */
static void test_aros_factory(void)
{
    unsigned char f[ES_AROS_DEFAULT_LEN];

    CHECK(es_aros_default(1, f) == 70);
    CHECK(flags_of(f) == 0x1000801EUL);
    CHECK(es_aros_offscreen(f, 70) == 1);
    CHECK(es_aros_default(0, f) == 70);
    CHECK(flags_of(f) == 0x0000801EUL);
    CHECK(es_aros_offscreen(f, 70) == 0);
    CHECK(es_aros_set_offscreen(f, 70, 1) == 1);
    CHECK(es_aros_set_offscreen(f, 70, 1) == 0);
    CHECK(flags_of(f) == 0x1000801EUL);
    CHECK(es_aros_set_offscreen(f, 70, 0) == 1);
    CHECK(flags_of(f) == 0x0000801EUL);
}

/*
 * The file AROS One 1.3 ships in ENVARC:SYS, as read in the VM: the
 * setting on, and its own choice of the other flags. Turned off and on
 * again by EdgeSnapPrefs there (2026-10-01), only that bit moved.
 */
static void test_aros_one(void)
{
    static const unsigned char shipped[70] = {
        0x46, 0x4f, 0x52, 0x4d, 0x00, 0x00, 0x00, 0x3e,
        0x50, 0x52, 0x45, 0x46, 0x50, 0x52, 0x48, 0x44,
        0x00, 0x00, 0x00, 0x06, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x49, 0x43, 0x54, 0x4c, 0x00, 0x00,
        0x00, 0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x32, 0x00, 0x40, 0x98, 0x03,
        0xc0, 0x1e, 0x4e, 0x4d, 0x56, 0x42, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00
    };
    unsigned char f[70];
    unsigned char g[ES_AROS_DEFAULT_LEN];
    int i, other = 0;

    for (i = 0; i < 70; i++) {
        f[i] = shipped[i];
    }
    CHECK(es_aros_offscreen(f, 70) == 1);
    CHECK(flags_of(f) == 0x9803C01EUL);
    CHECK(es_aros_set_offscreen(f, 70, 0) == 1);
    CHECK(flags_of(f) == 0x8803C01EUL);
    for (i = 0; i < 70; i++) {
        if (i != 54 && f[i] != shipped[i]) {
            other++;
        }
    }
    CHECK(other == 0);
    CHECK(es_aros_set_offscreen(f, 70, 1) == 1);
    for (i = 0; i < 70; i++) {
        if (f[i] != shipped[i]) {
            other++;
        }
    }
    CHECK(other == 0);
    /* the defaults are the same shape, byte for byte outside the flags */
    es_aros_default(1, g);
    for (i = 0; i < 70; i++) {
        if ((i < 54 || i > 57) && g[i] != shipped[i]) {
            other++;
        }
    }
    CHECK(other == 0);
}

/* A chunk before ICTL of odd length is padded; what is not an IControl
 * file, or is cut short, is left alone. */
static void test_aros_shapes(void)
{
    unsigned char f[ES_AROS_DEFAULT_LEN + 12];
    unsigned char g[ES_AROS_DEFAULT_LEN];
    int i;

    es_aros_default(1, g);
    /* FORM, PREF, then a 3-byte chunk and its pad byte, then the rest */
    for (i = 0; i < 12; i++) {
        f[i] = g[i];
    }
    f[12] = 'J'; f[13] = 'U'; f[14] = 'N'; f[15] = 'K';
    f[16] = 0; f[17] = 0; f[18] = 0; f[19] = 3;
    f[20] = 1; f[21] = 2; f[22] = 3; f[23] = 0;
    for (i = 12; i < ES_AROS_DEFAULT_LEN; i++) {
        f[i + 12] = g[i];
    }
    CHECK(es_aros_offscreen(f, ES_AROS_DEFAULT_LEN + 12) == 1);

    es_aros_default(1, g);
    g[0] = 'X';
    CHECK(es_aros_offscreen(g, 70) == -1);
    CHECK(es_aros_set_offscreen(g, 70, 0) == -1 && g[54] == 0x10);
    es_aros_default(1, g);
    CHECK(es_aros_offscreen(g, 60) == -1);       /* ICTL cut short */
    es_aros_default(1, g);
    g[33] = 10;                                   /* ICTL too small   */
    CHECK(es_aros_offscreen(g, 70) == -1);
}

int main(void)
{
    test_mos_factory();
    test_mos_toggle();
    test_mos_width();
    test_mos_refuses();
    test_aros_factory();
    test_aros_one();
    test_aros_shapes();

    if (g_failures == 0) {
        printf("offscreen_test: all tests passed\n");
        return 0;
    }
    printf("offscreen_test: %d failure(s)\n", g_failures);
    return 1;
}
