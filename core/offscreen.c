/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen.c - see offscreen.h.
 */

#include "offscreen.h"

/* ------------------------------------------------------------ MorphOS */

/*
 * MorphOS 3.20's IControl, saved from its defaults: the Flags line,
 * then one line per hotkey that has a trigger, then End. Every line
 * after the first is the same with the choice on or off. A list of
 * lines, not one string: C89 promises string literals of 509
 * characters, and the file is 773.
 */
#define ES_MOS_DEFAULT_FLAGS 0x1283C0UL

static const char *const es_mos_default_rest[] = {
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

static int es_hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    return -1;
}

/*
 * The digits of the Flags line, [*at, *end), and their value. The line
 * is "Flags=0x" and one to eight hex digits, at the start of a line;
 * MorphOS writes it first. 1 when found, 0 when the text has no such
 * line, or one that reads otherwise.
 */
static int es_mos_flags(const char *t, int len, int *at, int *end,
                        unsigned long *value)
{
    static const char key[] = "Flags=0";
    int pos = 0;

    if (t == 0 || len <= 0) {
        return 0;
    }
    while (pos < len) {
        int k = 0;

        while (key[k] != '\0' && pos + k < len && t[pos + k] == key[k]) {
            k++;
        }
        if (key[k] == '\0' && pos + k < len &&
            (t[pos + k] == 'x' || t[pos + k] == 'X')) {
            int d = pos + k + 1;
            int n = 0;
            unsigned long v = 0;
            int h;

            while (d + n < len && (h = es_hex_digit(t[d + n])) >= 0) {
                if (n == 8) {
                    return 0;              /* longer than 32 bits */
                }
                v = (v << 4) | (unsigned long)h;
                n++;
            }
            if (n == 0 || (d + n < len && t[d + n] != '\n' &&
                           t[d + n] != '\r')) {
                return 0;
            }
            *at = d;
            *end = d + n;
            *value = v;
            return 1;
        }
        while (pos < len && t[pos] != '\n') {
            pos++;
        }
        pos++;
    }
    return 0;
}

int es_mos_offscreen(const char *text, int len)
{
    int at, end;
    unsigned long v;

    if (!es_mos_flags(text, len, &at, &end, &v)) {
        return -1;
    }
    return (v & ES_MOS_OFFSCREEN) != 0;
}

/* `v` in upper case hex, at least `width` digits; returns how many. */
static int es_hex(unsigned long v, int width, char *out)
{
    static const char digit[] = "0123456789ABCDEF";
    char tmp[8];
    int n = 0;
    int i;

    do {
        tmp[n++] = digit[v & 0xF];
        v >>= 4;
    } while (v != 0 && n < 8);
    while (n < width && n < 8) {
        tmp[n++] = '0';
    }
    for (i = 0; i < n; i++) {
        out[i] = tmp[n - 1 - i];
    }
    return n;
}

int es_mos_set_offscreen(const char *in, int len, int on,
                         char *out, int size)
{
    char digits[8];
    int at, end, n, i, o;
    unsigned long v;

    if (!es_mos_flags(in, len, &at, &end, &v)) {
        return -1;
    }
    if (on) {
        v |= ES_MOS_OFFSCREEN;
    } else {
        v &= ~ES_MOS_OFFSCREEN;
    }
    n = es_hex(v, end - at, digits);
    if (out == 0 || at + n + (len - end) > size) {
        return -1;
    }
    o = 0;
    for (i = 0; i < at; i++) {
        out[o++] = in[i];
    }
    for (i = 0; i < n; i++) {
        out[o++] = digits[i];
    }
    for (i = end; i < len; i++) {
        out[o++] = in[i];
    }
    return o;
}

int es_mos_default(int on, char *out, int size)
{
    static const char head[] = "Flags=0x";
    char digits[8];
    unsigned long v = ES_MOS_DEFAULT_FLAGS;
    int o = 0;
    int n, i, l;

    if (on) {
        v |= ES_MOS_OFFSCREEN;
    }
    n = es_hex(v, 1, digits);
    if (out == 0) {
        return -1;
    }
    for (i = 0; head[i] != '\0'; i++) {
        if (o == size) {
            return -1;
        }
        out[o++] = head[i];
    }
    for (i = 0; i < n; i++) {
        if (o == size) {
            return -1;
        }
        out[o++] = digits[i];
    }
    if (o == size) {
        return -1;
    }
    out[o++] = '\n';
    for (l = 0; es_mos_default_rest[l] != 0; l++) {
        for (i = 0; es_mos_default_rest[l][i] != '\0'; i++) {
            if (o == size) {
                return -1;
            }
            out[o++] = es_mos_default_rest[l][i];
        }
    }
    return o;
}

/* --------------------------------------------------------------- AROS */

/*
 * An IFF FORM PREF: a PRHD chunk, then ICTL holding struct
 * IControlPrefs, big-endian as everything Amiga. ic_Flags comes after
 * four reserved longs, the timeout and the meta drag qualifier: twenty
 * bytes into the chunk. Bit 28 is the top nibble of its first byte.
 */
#define ES_ICTL_FLAGS_IN_CHUNK 20
#define ES_AROS_BIT_BYTE       0
#define ES_AROS_BIT_MASK       0x10

static unsigned long es_be32(const unsigned char *p)
{
    return ((unsigned long)p[0] << 24) | ((unsigned long)p[1] << 16) |
           ((unsigned long)p[2] << 8) | (unsigned long)p[3];
}

static int es_is(const unsigned char *p, const char *id)
{
    return p[0] == (unsigned char)id[0] && p[1] == (unsigned char)id[1] &&
           p[2] == (unsigned char)id[2] && p[3] == (unsigned char)id[3];
}

/* Where the four bytes of ic_Flags are, or -1. */
static int es_ictl_flags_at(const unsigned char *buf, int len)
{
    int pos = 12;

    if (buf == 0 || len < 12 || !es_is(buf, "FORM") ||
        !es_is(buf + 8, "PREF")) {
        return -1;
    }
    while (pos + 8 <= len) {
        unsigned long size = es_be32(buf + pos + 4);

        if (size > (unsigned long)(len - pos - 8)) {
            return -1;                     /* a chunk longer than the file */
        }
        if (es_is(buf + pos, "ICTL")) {
            if (size < ES_ICTL_FLAGS_IN_CHUNK + 4) {
                return -1;
            }
            return pos + 8 + ES_ICTL_FLAGS_IN_CHUNK;
        }
        pos += 8 + (int)size + (int)(size & 1);   /* chunks are padded even */
    }
    return -1;
}

int es_aros_offscreen(const unsigned char *buf, int len)
{
    int at = es_ictl_flags_at(buf, len);

    if (at < 0) {
        return -1;
    }
    return (buf[at + ES_AROS_BIT_BYTE] & ES_AROS_BIT_MASK) != 0;
}

int es_aros_set_offscreen(unsigned char *buf, int len, int on)
{
    int at = es_ictl_flags_at(buf, len);
    unsigned char was;

    if (at < 0) {
        return -1;
    }
    was = buf[at + ES_AROS_BIT_BYTE];
    if (on) {
        buf[at + ES_AROS_BIT_BYTE] = (unsigned char)(was | ES_AROS_BIT_MASK);
    } else {
        buf[at + ES_AROS_BIT_BYTE] =
            (unsigned char)(was & (unsigned char)~ES_AROS_BIT_MASK);
    }
    return buf[at + ES_AROS_BIT_BYTE] != was;
}

int es_aros_default(int on, unsigned char *buf)
{
    static const unsigned char file[ES_AROS_DEFAULT_LEN] = {
        'F', 'O', 'R', 'M', 0x00, 0x00, 0x00, 0x3E,
        'P', 'R', 'E', 'F',
        'P', 'R', 'H', 'D', 0x00, 0x00, 0x00, 0x06,
        0x00,                       /* ph_Version: PHV_CURRENT          */
        0x00,                       /* ph_Type                          */
        0x00, 0x00, 0x00, 0x00,     /* ph_Flags                         */
        'I', 'C', 'T', 'L', 0x00, 0x00, 0x00, 0x24,
        /* ic_Reserved[4] */
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x32,                 /* ic_TimeOut: 50                  */
        0x00, 0x40,                 /* ic_MetaDrag: left Amiga         */
        0x10, 0x00, 0x80, 0x1E,     /* ic_Flags: off-screen move, 3D   */
                                    /* menus, mode promotion, menu      */
                                    /* snap, string filter, lace        */
        'N', 'M', 'V', 'B',         /* the four command keys            */
        0x00, 0x00,                 /* ic_Reserved2                     */
        0x00, 0x00, 0x00, 0x00,     /* ic_VDragModes                    */
        0x00, 0x00                  /* padding to a multiple of four    */
    };
    int i;

    for (i = 0; i < ES_AROS_DEFAULT_LEN; i++) {
        buf[i] = file[i];
    }
    es_aros_set_offscreen(buf, ES_AROS_DEFAULT_LEN, on);
    return ES_AROS_DEFAULT_LEN;
}

/* ---------------------------------------------------------- AmigaOS 4 */

/*
 * struct GUIPrefs is packed to two bytes: four reserved longs, the
 * version word, the global flags, then gp_ScreenFlags at 22, big
 * endian, so 0x20 sits in its last byte.
 */
#define ES_GUI_FLAGS_BYTE 25
#define ES_GUI_BIT_MASK   0x20

/* Where the next GUI chunk's data starts, from `pos` on, or -1. */
static int es_gui_chunk(const unsigned char *buf, int len, int pos)
{
    while (pos + 8 <= len) {
        unsigned long size = es_be32(buf + pos + 4);

        if (size > (unsigned long)(len - pos - 8)) {
            return -1;                     /* a chunk longer than the file */
        }
        if (es_is(buf + pos, "GUI ") && size > ES_GUI_FLAGS_BYTE) {
            return pos + 8;
        }
        pos += 8 + (int)size + (int)(size & 1);
    }
    return -1;
}

static int es_gui_first(const unsigned char *buf, int len)
{
    if (buf == 0 || len < 12 || !es_is(buf, "FORM") ||
        !es_is(buf + 8, "PREF")) {
        return -1;
    }
    return es_gui_chunk(buf, len, 12);
}

int es_os4_offscreen(const unsigned char *buf, int len)
{
    int at = es_gui_first(buf, len);

    if (at < 0) {
        return -1;
    }
    return (buf[at + ES_GUI_FLAGS_BYTE] & ES_GUI_BIT_MASK) != 0;
}

int es_os4_set_offscreen(unsigned char *buf, int len, int on)
{
    int at = es_gui_first(buf, len);
    int changed = 0;

    if (at < 0) {
        return -1;
    }
    while (at >= 0) {
        unsigned char *b = buf + at + ES_GUI_FLAGS_BYTE;
        unsigned char was = *b;
        unsigned long size = es_be32(buf + at - 4);

        if (on) {
            *b = (unsigned char)(was | ES_GUI_BIT_MASK);
        } else {
            *b = (unsigned char)(was & (unsigned char)~ES_GUI_BIT_MASK);
        }
        if (*b != was) {
            changed = 1;
        }
        at = es_gui_chunk(buf, len, at + (int)size + (int)(size & 1));
    }
    return changed;
}
