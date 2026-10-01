/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen_sys.h - the system's own setting that lets a dragged window
 * go past the edge of the screen, read and written where MorphOS, AROS
 * or AmigaOS 4 keeps it. What the setting is and where it sits in the
 * file is core/offscreen.c's; this is the dos.library around it.
 *
 * On all three the live file, in ENV:, is watched: writing it changes
 * the setting at once, without a restart. Measured on MorphOS 3.20
 * (2026-10-01): a window dragged against the left edge stopped at 0
 * with the setting off and went on to -210 with it on, the file written
 * in between from outside IControl; on AmigaOS 4.1 Final Edition IPrefs
 * applied the GUI file the same way. Nothing here sets it in Intuition
 * directly: on AmigaOS 4 only IPrefs may change GUI settings globally.
 */

#ifndef EDGESNAP_OFFSCREEN_SYS_H
#define EDGESNAP_OFFSCREEN_SYS_H

/* "MorphOS", "AROS" or "AmigaOS 4": the system whose setting this is. */
const char *eso_system(void);

/* The system's own editor for it: "IControl", or "GUI" on AmigaOS 4. */
const char *eso_editor(void);

/*
 * May windows go past the edge? 1 yes, 0 no, -1 when the file is there
 * and is not one this code understands, in which case it is never
 * written either. A machine with no file has the system's default:
 * no on MorphOS, yes on AROS and AmigaOS 4. `archived` asks what the
 * next start will use, ENVARC:, instead of what is in effect now.
 */
int eso_query(int archived);

/*
 * Set it. Every other byte of the user's file is kept; a file that
 * already says so is not written at all. `live` writes ENV:, which
 * takes effect at once, `archived` ENVARC:, which the next start reads.
 * Returns 1 when everything asked for was done. On AmigaOS 4 a machine
 * with no GUI preferences at all is left alone: that file is the
 * system's whole look, not one to write from nothing.
 */
int eso_set(int on, int live, int archived);

/*
 * The Shell form, for both preferences programs and the installer:
 *   EdgeSnapPrefs OFFSCREEN QUERY|ON|OFF
 * eso_is_command() says whether the arguments are that; eso_command()
 * does it and returns the program's return code: QUERY 0 when windows
 * can go past the edge and 5 when not, every form 10 when the system's
 * file is not one this code can read or write.
 */
int eso_is_command(int argc, char **argv);
int eso_command(int argc, char **argv);

#endif /* EDGESNAP_OFFSCREEN_SYS_H */
