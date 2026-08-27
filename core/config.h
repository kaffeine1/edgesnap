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

#endif /* EDGESNAP_CONFIG_H */
