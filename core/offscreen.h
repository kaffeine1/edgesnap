/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen.h - the system's own setting that lets a dragged window go
 * past the edge of the screen, read and changed where the system keeps
 * it. With it off, a window dragged against an edge stops there and
 * holds the pointer back where it has the title bar: the pointer never
 * reaches the edge, and only the push of library 2.7 snaps.
 *
 *   MorphOS  ENV:Sys/icontrol.conf, a text file whose first line is
 *            "Flags=0x..."; bit 0x4000 is IControl's "allow
 *            positioning beyond the screen limits". Off as delivered.
 *   AROS     ENV:SYS/icontrol.prefs, an IFF FORM PREF whose ICTL chunk
 *            holds struct IControlPrefs; ICF_OFFSCREENLAYERS, bit 28
 *            of ic_Flags, is IControl's "Offscreen move". On as
 *            delivered.
 *
 * Pure C89 over a file held in memory, so the host tests can hold it
 * to the files the systems really write; the reading and writing are
 * the preferences program's (prefs/offscreen_sys.c). Only that one bit
 * is ever touched: every other byte of the user's file goes back as it
 * came.
 */

#ifndef EDGESNAP_OFFSCREEN_H
#define EDGESNAP_OFFSCREEN_H

/* Longer than either file: MorphOS's is under a kilobyte even with
 * every hotkey of IControl given a trigger, AROS's is 70 bytes. */
#define ES_OFFS_FILE_MAX 4096

/* ------------------------------------------------------------ MorphOS */

#define ES_MOS_OFFSCREEN 0x4000UL

/* 1 when windows may go past the edge, 0 when not, -1 when the text
 * has no "Flags=0x" line this code can read. */
int es_mos_offscreen(const char *text, int len);

/*
 * The same text with the bit as asked, into `out`: only the digits of
 * the Flags line change, and they keep their width when the new value
 * fits in it. Returns the new length, or -1 when the text is not one
 * this code understands or `out` (of `size` bytes) is too small; then
 * nothing is to be written.
 */
int es_mos_set_offscreen(const char *in, int len, int on,
                         char *out, int size);

/*
 * What MorphOS 3.20's IControl writes from its own defaults, with the
 * bit as asked: for a machine where IControl was never saved, which has
 * no file and runs on the built-in defaults. Byte for byte what the
 * editor wrote on a fresh 3.20 when only that choice was changed
 * (2026-10-01). Returns the length, or -1 when `size` is too small.
 */
int es_mos_default(int on, char *out, int size);

/* --------------------------------------------------------------- AROS */

#define ES_AROS_OFFSCREEN   0x10000000UL
#define ES_AROS_DEFAULT_LEN 70

/* 1 when windows may go past the edge, 0 when not, -1 when the buffer
 * is not an IControl preferences file. */
int es_aros_offscreen(const unsigned char *buf, int len);

/* Set or clear the bit in place. 1 when the buffer changed, 0 when it
 * already said so, -1 when it is not an IControl preferences file, in
 * which case nothing is touched. */
int es_aros_set_offscreen(unsigned char *buf, int len, int on);

/*
 * The file AROS's IControl editor writes from its defaults
 * (workbench/prefs/icontrol/prefs.c, Prefs_Default and Prefs_ExportFH),
 * with the bit as asked. Its ICTL chunk is sizeof(struct IControlPrefs):
 * 34 bytes of fields, 36 on x86_64, where the structure is padded to a
 * multiple of four, and the editor's loader takes no other size; the
 * file AROS One 1.3 ships is laid out the same way. Needs
 * ES_AROS_DEFAULT_LEN bytes, returns that length.
 */
int es_aros_default(int on, unsigned char *buf);

#endif /* EDGESNAP_OFFSCREEN_H */
