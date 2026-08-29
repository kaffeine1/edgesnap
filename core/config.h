/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * config.h - portable EdgeSnap preferences.
 *
 * Pure C89, host-tested. One KEY=VALUE parser serves BOTH preference
 * sources, because on Amiga systems they have the same shape: lines of
 * an ENVARC: file and Workbench tooltypes. The platform glue only
 * reads bytes and hands over strings; what a key means, which values
 * are legal, and what happens to a bad one is decided here.
 *
 * Unknown keys and malformed values never abort a load: they are
 * reported per line so the frontend can warn while keeping every
 * setting it did understand (a truncated prefs file must not leave the
 * user without snapping).
 */

#ifndef EDGESNAP_CONFIG_H
#define EDGESNAP_CONFIG_H

#include "engine.h"
#include "panels.h"

/* Bypass qualifier values (ES_QUAL_*) come from the public header on
 * Amiga builds; the host tests define them here. */
#ifndef ES_QUAL_NONE
#define ES_QUAL_NONE  0
#define ES_QUAL_ALT   1
#define ES_QUAL_CTRL  2
#define ES_QUAL_SHIFT 3
#endif

typedef struct ESConfig {
    ESEngineConfig engine;   /* edge_px, corner_div, drag_min_px, zones */
    ESInsets margin;         /* extra user margins, added to panels     */
    int panel_detect;        /* auto-reserve dock/panel strips          */
    int panel_margin;        /* breathing room around detected panels   */
    int preview;             /* show the zone preview frame             */
    int bypass_qual;         /* ES_QUAL_*                               */
} ESConfig;

void es_config_defaults(ESConfig *cfg);

/*
 * Apply one KEY=VALUE pair. Returns ES_OK, ES_ERR_BAD_ARGS (key known,
 * value not legal) or ES_ERR_UNSUPPORTED (key not known).
 */
int es_config_set(ESConfig *cfg, const char *key, const char *value);

/*
 * Apply one raw line: surrounding blanks are ignored, empty lines and
 * comments (# or ;) return ES_OK without changing anything, everything
 * else must be KEY=VALUE. Same return codes as es_config_set.
 */
int es_config_line(ESConfig *cfg, const char *line);

/* ------------------------------------------------- settings, described
 *
 * A preferences GUI must not re-state what a setting is: the moment it
 * does, the file parser and the window drift apart and a value that is
 * legal in one is refused by the other. So the vocabulary describes
 * itself here, once, and a GUI walks the table to build its window -
 * an integer becomes a slider between min and max, a boolean a
 * checkbox, a choice a cycle gadget, the zone mask a row of them.
 *
 * Writing goes through the same door: es_setting_apply() formats the
 * value and hands it to es_config_set(), so the GUI cannot accept
 * anything the file parser would reject.
 */

typedef enum ESSettingKind {
    ES_SET_INT,      /* whole number, min..max                        */
    ES_SET_BOOL,     /* yes or no                                     */
    ES_SET_CHOICE,   /* one of `choices`, value is the index          */
    ES_SET_ZONES     /* the zone mask; value is ES_ZONEBIT(..) | ..   */
} ESSettingKind;

typedef struct ESSetting {
    const char *key;              /* EDGEPX - as written in the file  */
    const char *label;            /* "Edge distance" - for the GUI    */
    const char *help;             /* one line, for a help bubble      */
    int kind;                     /* ESSettingKind                    */
    int min;                      /* ES_SET_INT only                  */
    int max;
    const char *const *choices;   /* ES_SET_CHOICE, NULL-terminated   */
} ESSetting;

/* The whole vocabulary, in the order a preferences window should show
 * it. `count` receives the number of entries. */
const ESSetting *es_settings(int *count);

/* Current value of setting `index`, as an int (see ESSettingKind). */
int es_setting_value(const ESConfig *cfg, int index);

/* Set it, with exactly the validation the file parser applies.
 * Returns ES_OK or ES_ERR_BAD_ARGS. */
int es_setting_apply(ESConfig *cfg, int index, int value);

/*
 * Write the settings as a preferences file. Returns the number of
 * bytes the text needs (excluding the terminator), whether or not it
 * fitted; nothing is written if `size` is too small. Parsing the
 * result back gives the same settings.
 */
int es_config_write(const ESConfig *cfg, char *buf, int size);

#endif /* EDGESNAP_CONFIG_H */
