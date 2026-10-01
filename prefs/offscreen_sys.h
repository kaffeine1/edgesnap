/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * offscreen_sys.h - the system's own setting that lets a dragged window
 * go past the edge of the screen, read and written where MorphOS or
 * AROS keeps it. What the setting is and where it sits in the file is
 * core/offscreen.c's; this is the dos.library around it.
 *
 * On both systems the live file, in ENV:, is watched: writing it
 * changes the setting at once, without a restart. Measured on MorphOS
 * 3.20 (2026-10-01): a window dragged against the left edge stopped at
 * 0 with the setting off and went on to -210 with it on, the file
 * written in between from outside IControl.
 */

#ifndef EDGESNAP_OFFSCREEN_SYS_H
#define EDGESNAP_OFFSCREEN_SYS_H

/* "MorphOS" or "AROS": the system whose setting this is. */
const char *eso_system(void);

/*
 * May windows go past the edge? 1 yes, 0 no, -1 when the file is there
 * and is not one this code understands, in which case it is never
 * written either. A machine with no file has the system's default:
 * no on MorphOS, yes on AROS. `archived` asks what the next start
 * will use, ENVARC:, instead of what is in effect now.
 */
int eso_query(int archived);

/*
 * Set it. Every other byte of the user's file is kept; a file that
 * already says so is not written at all. `live` writes ENV:, which
 * takes effect at once, `archived` ENVARC:, which the next start reads.
 * Returns 1 when everything asked for was done.
 */
int eso_set(int on, int live, int archived);

#endif /* EDGESNAP_OFFSCREEN_SYS_H */
