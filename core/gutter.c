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

/*
 * Collect the windows whose edge lies on `line` and build the seam
 * there. A side is every window touching the line from that direction,
 * so one window facing two stacked ones gives a side of 1 and a side
 * of 2, and all three move together.
 *
 * The strip spans only where windows actually face each other: the
 * union of the overlaps, not the union of the windows, so a seam does
 * not stretch past the region it can act on.
 */
static int es_seam_at(const ESRegistry *reg, int vertical, int line,
                      int thickness, ESSeam *out)
{
    int half = thickness / 2;
    int from = 0, to = 0, have_span = 0;
    int i, j;

    out->valid = 0;
    out->vertical = vertical;
    out->a.n = 0;
    out->b.n = 0;

    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        const ESRegistryEntry *e = &reg->slot[i];
        const ESRect *r;
        int near_a, near_b;

        if (!e->used) {
            continue;
        }
        r = &e->snapped;
        near_a = vertical ? es_near(r->x + r->w, line) : es_near(r->y + r->h, line);
        near_b = vertical ? es_near(r->x, line) : es_near(r->y, line);
        if (near_a && out->a.n < ES_SEAM_SIDE_MAX) {
            out->a.ref[out->a.n] = e->ref;
            out->a.box[out->a.n] = *r;
            out->a.n++;
        } else if (near_b && out->b.n < ES_SEAM_SIDE_MAX) {
            out->b.ref[out->b.n] = e->ref;
            out->b.box[out->b.n] = *r;
            out->b.n++;
        }
    }
    if (out->a.n == 0 || out->b.n == 0) {
        return 0;
    }

    /* Only the stretches where a window on one side truly faces one on
     * the other count as seam: that is what the handle can act on. */
    for (i = 0; i < out->a.n; i++) {
        for (j = 0; j < out->b.n; j++) {
            int lo, hi;
            int ok = vertical
                ? es_overlap_v(&out->a.box[i], &out->b.box[j], &lo, &hi)
                : es_overlap_h(&out->a.box[i], &out->b.box[j], &lo, &hi);

            if (!ok) {
                continue;
            }
            if (!have_span) {
                from = lo;
                to = hi;
                have_span = 1;
            } else {
                if (lo < from) {
                    from = lo;
                }
                if (hi > to) {
                    to = hi;
                }
            }
        }
    }
    if (!have_span) {
        return 0;
    }

    out->pos = line;
    if (vertical) {
        out->rect.x = line - half;
        out->rect.y = from;
        out->rect.w = thickness;
        out->rect.h = to - from;
    } else {
        out->rect.x = from;
        out->rect.y = line - half;
        out->rect.w = to - from;
        out->rect.h = thickness;
    }

    /* Every window keeps its floor, so the tightest one decides. */
    out->min_pos = -0x7FFF;
    out->max_pos = 0x7FFF;
    for (i = 0; i < out->a.n; i++) {
        int lim = vertical
            ? out->a.box[i].x + ES_SEAM_MIN_SIDE
            : out->a.box[i].y + ES_SEAM_MIN_SIDE;

        if (lim > out->min_pos) {
            out->min_pos = lim;
        }
    }
    for (i = 0; i < out->b.n; i++) {
        int lim = vertical
            ? out->b.box[i].x + out->b.box[i].w - ES_SEAM_MIN_SIDE
            : out->b.box[i].y + out->b.box[i].h - ES_SEAM_MIN_SIDE;

        if (lim < out->max_pos) {
            out->max_pos = lim;
        }
    }
    if (out->min_pos > out->max_pos) {
        return 0;              /* too small to be divided sensibly */
    }
    out->valid = 1;
    return 1;
}

/* Has a seam on this line already been written? Boundaries are found
 * once per window that touches them, so the same line comes up twice. */
static int es_seam_seen(const ESSeam *seams, int n, int vertical, int line)
{
    int i;

    for (i = 0; i < n; i++) {
        if (seams[i].vertical == vertical && es_near(seams[i].pos, line)) {
            return 1;
        }
    }
    return 0;
}

int es_gutter_find_all(const ESRegistry *reg, int thickness_px,
                       ESSeam *out, int max)
{
    int n = 0;
    int i, pass;

    for (pass = 0; pass < 2 && n < max; pass++) {
        int vertical = (pass == 0);

        for (i = 0; i < ES_REGISTRY_SLOTS && n < max; i++) {
            const ESRegistryEntry *e = &reg->slot[i];
            int line;

            if (!e->used) {
                continue;
            }
            /* Every window's leading edge is a candidate boundary. */
            line = vertical ? e->snapped.x : e->snapped.y;
            if (es_seam_seen(out, n, vertical, line)) {
                continue;
            }
            if (es_seam_at(reg, vertical, line, thickness_px, &out[n])) {
                n++;
            }
        }
    }
    return n;
}

int es_gutter_find(const ESRegistry *reg, int thickness_px, ESSeam *out)
{
    return es_gutter_find_all(reg, thickness_px, out, 1) == 1;
}

void es_gutter_apply(const ESSeam *seam, int new_pos,
                     ESRect *out_a, ESRect *out_b)
{
    int pos = new_pos;
    int i;

    if (pos < seam->min_pos) {
        pos = seam->min_pos;
    }
    if (pos > seam->max_pos) {
        pos = seam->max_pos;
    }
    for (i = 0; i < seam->a.n; i++) {
        out_a[i] = seam->a.box[i];
        if (seam->vertical) {
            out_a[i].w = pos - seam->a.box[i].x;
        } else {
            out_a[i].h = pos - seam->a.box[i].y;
        }
    }
    for (i = 0; i < seam->b.n; i++) {
        int far = seam->vertical
            ? seam->b.box[i].x + seam->b.box[i].w
            : seam->b.box[i].y + seam->b.box[i].h;

        out_b[i] = seam->b.box[i];
        if (seam->vertical) {
            out_b[i].x = pos;
            out_b[i].w = far - pos;
        } else {
            out_b[i].y = pos;
            out_b[i].h = far - pos;
        }
    }
}

/* Which side of the screen does this zone occupy? */
static int es_zone_is_left(int z)
{
    return z == ES_ZONE_LEFT || z == ES_ZONE_TOP_LEFT ||
           z == ES_ZONE_BOTTOM_LEFT;
}

static int es_zone_is_right(int z)
{
    return z == ES_ZONE_RIGHT || z == ES_ZONE_TOP_RIGHT ||
           z == ES_ZONE_BOTTOM_RIGHT;
}

int es_pair_fill(const ESRegistry *reg, void *self, int zone,
                 const ESRect *usable, ESRect *rect)
{
    int want_left = es_zone_is_left(zone);
    int want_right = es_zone_is_right(zone);
    int i;

    if (!want_left && !want_right) {
        return 0;   /* maximise and the like: nothing to complement */
    }
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        const ESRegistryEntry *e = &reg->slot[i];
        int from, to;

        if (!e->used || e->ref == self) {
            continue;
        }
        /* the partner must be on the other side and share enough of
         * this rectangle's height to be the thing beside it */
        if (want_left && !es_zone_is_right(e->zone)) {
            continue;
        }
        if (want_right && !es_zone_is_left(e->zone)) {
            continue;
        }
        if (!es_overlap_v(rect, &e->snapped, &from, &to)) {
            continue;
        }
        if (want_left) {
            int edge = e->snapped.x;

            if (edge > rect->x + ES_SEAM_MIN_SIDE &&
                edge <= usable->x + usable->w) {
                rect->w = edge - rect->x;
                return 1;
            }
        } else {
            int edge = e->snapped.x + e->snapped.w;

            if (edge < usable->x + usable->w - ES_SEAM_MIN_SIDE &&
                edge >= usable->x) {
                rect->w = (rect->x + rect->w) - edge;
                rect->x = edge;
                return 1;
            }
        }
    }
    return 0;
}
