/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * zones.h - pure C89 zone geometry for EdgeSnap.
 * No Amiga dependencies: this compiles and unit-tests on the host.
 */

#ifndef EDGESNAP_ZONES_H
#define EDGESNAP_ZONES_H

/* ES_ZONE_* live in the public types header: one source of truth shared
 * with the (future) library API. */
#include "edgesnap_types.h"

typedef struct ESRect {
    int x;
    int y;
    int w;
    int h;
} ESRect;

/*
 * Map a pointer position to a zone. "usable" is the screen area available
 * for snapping (screen minus title bar and configured margins). A pointer
 * above usable->y (in the screen bar) still counts as the top edge.
 * edge_px: how close to an edge triggers it. corner_px: how far from a
 * corner, measured along the edge, still counts as that corner.
 * Returns ES_ZONE_NONE when the pointer is in the interior.
 */
int es_zone_from_pointer(const ESRect *usable, int px, int py,
                         int edge_px, int corner_px);

/* Target rectangle for a zone. Halves and quarters tile exactly even for
 * odd usable sizes (right/bottom take the remainder). */
void es_zone_rect(int zone, const ESRect *usable, ESRect *out);

/*
 * es_zone_rect plus window size limits. min/max <= 0 means "no limit"
 * (callers translate the Amiga 0xFFFF "unlimited" convention to 0).
 * When clamping shrinks the rectangle, it stays anchored to the zone's
 * outer edge (right zones stick to the right edge, bottom zones to the
 * bottom edge), like Windows does.
 */
void es_fit_zone_rect(int zone, const ESRect *usable,
                      int min_w, int min_h, int max_w, int max_h,
                      ESRect *out);

/* Static human-readable name, for logs. */
const char *es_zone_name(int zone);

#endif /* EDGESNAP_ZONES_H */
