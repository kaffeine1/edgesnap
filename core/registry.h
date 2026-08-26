/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * registry.h - snap registry: pre-snap geometry per window, with the
 * stale-safety rules of the library contract.
 *
 * Pure C89, host-tested. Windows are opaque refs (never dereferenced);
 * on Amiga the glue uses the struct Window pointer as ref, which makes
 * pointer reuse (window closes, another opens at the same address) a
 * real hazard. The registry defends in two ways:
 *
 *   1. the glue prunes entries whose ref no longer names a live window
 *      (es_registry_forget / es_registry_prune with a liveness callback
 *      is deliberately NOT provided: liveness is platform business, the
 *      glue walks its own lists and calls forget);
 *   2. restore only succeeds if the window still sits approximately
 *      where the snap put it (es_registry_restore). If the app or the
 *      user moved/resized it independently, the entry is dropped and
 *      ES_ERR_CHANGED is returned: we never yank a window the user has
 *      already re-arranged - and a recycled pointer almost never sits
 *      exactly on the old snap geometry, which defuses most ABA cases.
 */

#ifndef EDGESNAP_REGISTRY_H
#define EDGESNAP_REGISTRY_H

#include "zones.h"

#define ES_REGISTRY_SLOTS 32

/* How far (px, per coordinate) a snapped window may drift from the
 * recorded snap geometry and still count as "where we put it" - apps
 * with size increments (terminals) round the box we set. */
#define ES_RESTORE_SLACK_PX 16

typedef struct ESRegistryEntry {
    void *ref;
    ESRect prebox;   /* geometry to restore (first pre-snap box)  */
    ESRect snapped;  /* geometry the snap established             */
    int zone;
    int used;
} ESRegistryEntry;

typedef struct ESRegistry {
    ESRegistryEntry slot[ES_REGISTRY_SLOTS];
} ESRegistry;

void es_registry_init(ESRegistry *reg);

/*
 * Record a snap. Keeps the FIRST prebox across re-snaps (snapping an
 * already-snapped window to another zone must restore the original
 * geometry, not the intermediate one) while updating snapped/zone.
 * Returns ES_OK, or ES_ERR_NO_MEMORY when all slots are in use (the
 * caller may es_registry_forget something or accept the failure).
 */
int es_registry_remember(ESRegistry *reg, void *ref, const ESRect *prebox,
                         const ESRect *snapped, int zone);

/* Current zone for ref, or ES_ZONE_NONE when unknown. */
int es_registry_zone(const ESRegistry *reg, void *ref);

/*
 * Restore lookup: given where the window is NOW, decide.
 *   ES_OK          - *out = geometry to restore; entry consumed.
 *   ES_ERR_NOT_SNAPPED - no entry for ref.
 *   ES_ERR_CHANGED - window is not where the snap put it (moved/resized
 *                    independently, or suspected ref reuse); entry
 *                    dropped, nothing to restore.
 */
int es_registry_restore(ESRegistry *reg, void *ref, const ESRect *current,
                        ESRect *out);

/* Drop the entry for ref (window closed, app opted out, ...). */
void es_registry_forget(ESRegistry *reg, void *ref);

#endif /* EDGESNAP_REGISTRY_H */
