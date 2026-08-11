/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * zones.c - pure C89 zone geometry for EdgeSnap. Host-testable.
 */

#include "zones.h"

int es_zone_from_pointer(const ESRect *u, int px, int py,
                         int edge_px, int corner_px)
{
    int on_left, on_right, on_top;

    on_left = (px <= u->x + edge_px);
    on_right = (px >= u->x + u->w - 1 - edge_px);
    /* py < u->y is the screen title bar: still the top edge. */
    on_top = (py <= u->y + edge_px);

    if (on_left) {
        if (py <= u->y + corner_px) {
            return ES_ZONE_TOP_LEFT;
        }
        if (py >= u->y + u->h - 1 - corner_px) {
            return ES_ZONE_BOTTOM_LEFT;
        }
        return ES_ZONE_LEFT;
    }
    if (on_right) {
        if (py <= u->y + corner_px) {
            return ES_ZONE_TOP_RIGHT;
        }
        if (py >= u->y + u->h - 1 - corner_px) {
            return ES_ZONE_BOTTOM_RIGHT;
        }
        return ES_ZONE_RIGHT;
    }
    if (on_top) {
        if (px <= u->x + corner_px) {
            return ES_ZONE_TOP_LEFT;
        }
        if (px >= u->x + u->w - 1 - corner_px) {
            return ES_ZONE_TOP_RIGHT;
        }
        return ES_ZONE_MAX;
    }
    return ES_ZONE_NONE;
}

void es_zone_rect(int zone, const ESRect *u, ESRect *out)
{
    int half_w = u->w / 2;
    int half_h = u->h / 2;

    out->x = u->x;
    out->y = u->y;
    out->w = u->w;
    out->h = u->h;

    switch (zone) {
    case ES_ZONE_LEFT:
        out->w = half_w;
        break;
    case ES_ZONE_RIGHT:
        out->x = u->x + half_w;
        out->w = u->w - half_w;
        break;
    case ES_ZONE_TOP_LEFT:
        out->w = half_w;
        out->h = half_h;
        break;
    case ES_ZONE_TOP_RIGHT:
        out->x = u->x + half_w;
        out->w = u->w - half_w;
        out->h = half_h;
        break;
    case ES_ZONE_BOTTOM_LEFT:
        out->w = half_w;
        out->y = u->y + half_h;
        out->h = u->h - half_h;
        break;
    case ES_ZONE_BOTTOM_RIGHT:
        out->x = u->x + half_w;
        out->w = u->w - half_w;
        out->y = u->y + half_h;
        out->h = u->h - half_h;
        break;
    case ES_ZONE_MAX:
    default:
        break;
    }
}

void es_fit_zone_rect(int zone, const ESRect *u,
                      int min_w, int min_h, int max_w, int max_h,
                      ESRect *out)
{
    int right_edge, bottom_edge;

    es_zone_rect(zone, u, out);
    right_edge = out->x + out->w;
    bottom_edge = out->y + out->h;

    if (min_w > 0 && out->w < min_w) {
        out->w = min_w;
    }
    if (max_w > 0 && out->w > max_w) {
        out->w = max_w;
    }
    if (min_h > 0 && out->h < min_h) {
        out->h = min_h;
    }
    if (max_h > 0 && out->h > max_h) {
        out->h = max_h;
    }

    switch (zone) {
    case ES_ZONE_RIGHT:
    case ES_ZONE_TOP_RIGHT:
    case ES_ZONE_BOTTOM_RIGHT:
        out->x = right_edge - out->w;
        break;
    default:
        break;
    }
    switch (zone) {
    case ES_ZONE_BOTTOM_LEFT:
    case ES_ZONE_BOTTOM_RIGHT:
        out->y = bottom_edge - out->h;
        break;
    default:
        break;
    }
}

const char *es_zone_name(int zone)
{
    switch (zone) {
    case ES_ZONE_NONE:
        return "none";
    case ES_ZONE_LEFT:
        return "left";
    case ES_ZONE_RIGHT:
        return "right";
    case ES_ZONE_TOP_LEFT:
        return "top-left";
    case ES_ZONE_TOP_RIGHT:
        return "top-right";
    case ES_ZONE_BOTTOM_LEFT:
        return "bottom-left";
    case ES_ZONE_BOTTOM_RIGHT:
        return "bottom-right";
    case ES_ZONE_MAX:
        return "maximize";
    default:
        return "?";
    }
}
