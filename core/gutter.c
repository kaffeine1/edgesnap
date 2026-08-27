/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * gutter.c - seam geometry for the divider between snapped windows.
 */

#include "gutter.h"

static int es_min(int a, int b)
{
    return a < b ? a : b;
}

static int es_max(int a, int b)
{
    return a > b ? a : b;
}

static int es_near(int a, int b)
{
    int d = a - b;

    if (d < 0) {
        d = -d;
    }
    return d <= ES_SEAM_TOUCH_TOL;
}

/* Do the two rectangles share enough of their vertical extent? */
static int es_overlap_v(const ESRect *a, const ESRect *b, int *from, int *to)
{
    int lo = es_max(a->y, b->y);
    int hi = es_min(a->y + a->h, b->y + b->h);
    int shorter = es_min(a->h, b->h);

    if (hi - lo <= 0 || shorter <= 0) {
        return 0;
    }
    if ((hi - lo) * 100 < shorter * ES_SEAM_OVERLAP_PCT) {
        return 0;
    }
    *from = lo;
    *to = hi;
    return 1;
}

static int es_overlap_h(const ESRect *a, const ESRect *b, int *from, int *to)
{
    int lo = es_max(a->x, b->x);
    int hi = es_min(a->x + a->w, b->x + b->w);
    int shorter = es_min(a->w, b->w);

    if (hi - lo <= 0 || shorter <= 0) {
        return 0;
    }
    if ((hi - lo) * 100 < shorter * ES_SEAM_OVERLAP_PCT) {
        return 0;
    }
    *from = lo;
    *to = hi;
    return 1;
}

static void es_seam_fill(ESSeam *out, int vertical, int thickness,
                         const ESRegistryEntry *a, const ESRegistryEntry *b,
                         int from, int to)
{
    int half = thickness / 2;

    out->valid = 1;
    out->vertical = vertical;
    out->ref_a = a->ref;
    out->ref_b = b->ref;
    out->box_a = a->snapped;
    out->box_b = b->snapped;
    if (vertical) {
        out->pos = b->snapped.x;
        out->rect.x = out->pos - half;
        out->rect.y = from;
        out->rect.w = thickness;
        out->rect.h = to - from;
        out->min_pos = a->snapped.x + ES_SEAM_MIN_SIDE;
        out->max_pos = b->snapped.x + b->snapped.w - ES_SEAM_MIN_SIDE;
    } else {
        out->pos = b->snapped.y;
        out->rect.x = from;
        out->rect.y = out->pos - half;
        out->rect.w = to - from;
        out->rect.h = thickness;
        out->min_pos = a->snapped.y + ES_SEAM_MIN_SIDE;
        out->max_pos = b->snapped.y + b->snapped.h - ES_SEAM_MIN_SIDE;
    }
    if (out->min_pos > out->max_pos) {
        /* the pair is too small to be divided sensibly */
        out->valid = 0;
    }
}

int es_gutter_find(const ESRegistry *reg, int thickness_px, ESSeam *out)
{
    int i, j;

    out->valid = 0;
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        const ESRegistryEntry *a = &reg->slot[i];

        if (!a->used) {
            continue;
        }
        for (j = 0; j < ES_REGISTRY_SLOTS; j++) {
            const ESRegistryEntry *b = &reg->slot[j];
            int from, to;

            if (i == j || !b->used) {
                continue;
            }
            /* a on the left, b on the right */
            if (es_near(a->snapped.x + a->snapped.w, b->snapped.x) &&
                es_overlap_v(&a->snapped, &b->snapped, &from, &to)) {
                es_seam_fill(out, 1, thickness_px, a, b, from, to);
                if (out->valid) {
                    return 1;
                }
            }
            /* a on top, b below */
            if (es_near(a->snapped.y + a->snapped.h, b->snapped.y) &&
                es_overlap_h(&a->snapped, &b->snapped, &from, &to)) {
                es_seam_fill(out, 0, thickness_px, a, b, from, to);
                if (out->valid) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

void es_gutter_apply(const ESSeam *seam, int new_pos,
                     ESRect *out_a, ESRect *out_b)
{
    int pos = new_pos;

    if (pos < seam->min_pos) {
        pos = seam->min_pos;
    }
    if (pos > seam->max_pos) {
        pos = seam->max_pos;
    }

    *out_a = seam->box_a;
    *out_b = seam->box_b;

    if (seam->vertical) {
        int b_right = seam->box_b.x + seam->box_b.w;

        out_a->w = pos - seam->box_a.x;
        out_b->x = pos;
        out_b->w = b_right - pos;
    } else {
        int b_bottom = seam->box_b.y + seam->box_b.h;

        out_a->h = pos - seam->box_a.y;
        out_b->y = pos;
        out_b->h = b_bottom - pos;
    }
}
