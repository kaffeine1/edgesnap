/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * prefs_io.h - reading and writing the preferences file.
 *
 * Shared by both preferences windows, so that MUI and ReAction differ
 * only in widgets. What a setting means, which values are legal and
 * how the file is spelled all live in core/config.c; this is the two
 * dozen lines of dos.library that stand between that and a disk.
 */

#ifndef EDGESNAP_PREFS_IO_H
#define EDGESNAP_PREFS_IO_H

#include "config.h"

#define ESP_ENV     "ENV:EdgeSnap.prefs"
#define ESP_ENVARC  "ENVARC:EdgeSnap.prefs"

/*
 * Load what is in effect: the live file if there is one, the archived
 * file otherwise, the defaults if neither. Always leaves cfg usable.
 * Returns 1 if a file was read, 0 if the defaults were used.
 */
int esp_load(ESConfig *cfg, ESHotkeys *keys);

/*
 * Write the settings. ENV: always - that is what the running EdgeSnap
 * watches, so this is what "Use" means. With `permanent`, ENVARC: too,
 * which is what makes it survive a reboot: "Save".
 * Returns 1 if everything asked for was written.
 */
int esp_store(const ESConfig *cfg, const ESHotkeys *keys, int permanent);

/* ParseIX is the system's judge. On refusal, bad is the table index;
 * duplicate is the earlier row with the same parsed key, or -1 for a
 * syntax error. The window supplies the requester and keeps its edits. */
int esp_validate_hotkeys(const ESHotkeys *keys, int *bad, int *duplicate);

#endif /* EDGESNAP_PREFS_IO_H */
