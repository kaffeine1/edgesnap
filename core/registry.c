/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * registry.c - snap registry with stale-safe restore.
 */

#include "registry.h"

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
    ESRegistryEntry *e;
    int i;

    e = es_registry_lookup(reg, ref);
    if (e != 0) {
        /* re-snap: keep the original prebox, track the new placement */
        e->snapped = *snapped;
        e->zone = zone;
        return ES_OK;
    }
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        if (!reg->slot[i].used) {
            reg->slot[i].used = 1;
            reg->slot[i].ref = ref;
            reg->slot[i].prebox = *prebox;
            reg->slot[i].snapped = *snapped;
            reg->slot[i].zone = zone;
            return ES_OK;
        }
    }
    return ES_ERR_NO_MEMORY;
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

static int es_near(int a, int b)
{
    int d = a - b;

    if (d < 0) {
        d = -d;
    }
    return d <= ES_RESTORE_SLACK_PX;
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

void es_registry_forget(ESRegistry *reg, void *ref)
{
    ESRegistryEntry *e;

    e = es_registry_lookup(reg, ref);
    if (e != 0) {
        e->used = 0;
    }
}
