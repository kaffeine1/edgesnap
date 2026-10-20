/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * registry.c - snap registry with stale-resistant restore.
 *
 * Resistant, not safe, and the word is chosen: restore acts only if the
 * window still sits on the geometry it was snapped to, which defuses a
 * reused reference in every case that matters - but a new window at the
 * same address AND within the tolerance would still match. A library
 * outside Intuition cannot do better than that; only window identity
 * owned by the system could. Claiming "safe" would promise what we
 * cannot keep.
 */

#include "registry.h"
#include <limits.h>

void es_registry_init(ESRegistry *reg)
{
    int i;

    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        reg->slot[i].used = 0;
        reg->slot[i].ref = 0;
    }
}

static ESRegistryEntry *es_registry_lookup(ESRegistry *reg, void *ref)
{
    int i;

    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        if (reg->slot[i].used && reg->slot[i].ref == ref) {
            return &reg->slot[i];
        }
    }
    return 0;
}

int es_registry_remember(ESRegistry *reg, void *ref, const ESRect *prebox,
                         const ESRect *snapped, int zone)
{
    return es_registry_remember_step(reg, ref, prebox, snapped, zone, 0);
}

int es_registry_remember_step(ESRegistry *reg, void *ref,
                              const ESRect *prebox, const ESRect *snapped,
                              int zone, int step)
{
    ESRegistryEntry *e;
    int i;

    e = es_registry_lookup(reg, ref);
    if (e != 0) {
        /* re-snap: keep the original prebox, track the new placement */
        e->snapped = *snapped;
        e->zone = zone;
        e->step = step;
        return ES_OK;
    }
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        if (!reg->slot[i].used) {
            reg->slot[i].used = 1;
            reg->slot[i].ref = ref;
            reg->slot[i].prebox = *prebox;
            reg->slot[i].snapped = *snapped;
            reg->slot[i].zone = zone;
            reg->slot[i].step = step;
            return ES_OK;
        }
    }
    return ES_ERR_NO_MEMORY;
}

int es_registry_step(const ESRegistry *reg, void *ref)
{
    int i;

    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        if (reg->slot[i].used && reg->slot[i].ref == ref) {
            return reg->slot[i].step;
        }
    }
    return 0;
}

int es_registry_snapped(const ESRegistry *reg, void *ref, ESRect *out)
{
    int i;

    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        if (reg->slot[i].used && reg->slot[i].ref == ref) {
            if (out != 0) {
                *out = reg->slot[i].snapped;
            }
            return 1;
        }
    }
    return 0;
}

int es_registry_zone(const ESRegistry *reg, void *ref)
{
    int i;

    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        if (reg->slot[i].used && reg->slot[i].ref == ref) {
            return reg->slot[i].zone;
        }
    }
    return ES_ZONE_NONE;
}

/* Any two int coordinates, saturating when their distance cannot fit. */
static int es_distance(int a, int b)
{
    unsigned int d = a >= b ? (unsigned int)a - (unsigned int)b
                             : (unsigned int)b - (unsigned int)a;

    return d > (unsigned int)INT_MAX ? INT_MAX : (int)d;
}

static int es_near(int a, int b)
{
    return es_distance(a, b) <= ES_RESTORE_SLACK_PX;
}

int es_registry_restore(ESRegistry *reg, void *ref, const ESRect *current,
                        ESRect *out)
{
    ESRegistryEntry *e;

    e = es_registry_lookup(reg, ref);
    if (e == 0) {
        return ES_ERR_NOT_SNAPPED;
    }
    if (!es_near(current->x, e->snapped.x) ||
        !es_near(current->y, e->snapped.y) ||
        !es_near(current->w, e->snapped.w) ||
        !es_near(current->h, e->snapped.h)) {
        e->used = 0;
        return ES_ERR_CHANGED;
    }
    *out = e->prebox;
    e->used = 0;
    return ES_OK;
}

int es_registry_drag_out(ESRegistry *reg, void *ref, const ESRect *start,
                         const ESRect *now, ESRect *out)
{
    ESRegistryEntry *e;

    e = es_registry_lookup(reg, ref);
    if (e == 0 || !((e->zone >= ES_ZONE_LEFT && e->zone <= ES_ZONE_MAX) ||
                    e->zone == ES_ZONE_CENTRE)) {
        return ES_ERR_NOT_SNAPPED;
    }
    /* a drag moves a window; one that changed size is not ours to size */
    if (!es_near(start->w, e->snapped.w) || !es_near(start->h, e->snapped.h) ||
        !es_near(now->w, e->snapped.w) || !es_near(now->h, e->snapped.h)) {
        return ES_ERR_CHANGED;
    }
    if ((es_near(now->x, e->snapped.x) && es_near(now->y, e->snapped.y)) ||
        (es_near(now->x, start->x) && es_near(now->y, start->y))) {
        return ES_ERR_CHANGED;     /* it hardly moved */
    }
    *out = e->prebox;
    e->used = 0;
    return ES_OK;
}

void es_registry_forget(ESRegistry *reg, void *ref)
{
    ESRegistryEntry *e;

    e = es_registry_lookup(reg, ref);
    if (e != 0) {
        e->used = 0;
    }
}
