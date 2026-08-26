/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_types.h - portable public constants of the EdgeSnap API.
 *
 * DRAFT API v0 - NOT a frozen ABI. Values may still change until the
 * first public library release; after that, existing values are stable
 * forever and only additions are allowed.
 *
 * This header is pure C89 with no Amiga includes: it is shared verbatim
 * by the host-tested core, the platform library glue, and (through
 * edgesnap.h) third-party clients.
 */

#ifndef EDGESNAP_TYPES_H
#define EDGESNAP_TYPES_H

/* Draft API generation; becomes 1 with the first public release. */
#define ES_API_VERSION       0

/* ------------------------------------------------------------- zones */

#define ES_ZONE_NONE         0
#define ES_ZONE_LEFT         1
#define ES_ZONE_RIGHT        2
#define ES_ZONE_TOP_LEFT     3
#define ES_ZONE_TOP_RIGHT    4
#define ES_ZONE_BOTTOM_LEFT  5
#define ES_ZONE_BOTTOM_RIGHT 6
#define ES_ZONE_MAX          7

/* Zone sets (which zones react to a drag) travel as a bit mask. */
#define ES_ZONEBIT(z)        (1u << (z))
#define ES_ZONEMASK_ALL      0x00FEu   /* every zone but ES_ZONE_NONE */

/* ------------------------------------------------------------ errors */

/*
 * Every public entry point returns ES_OK or one negative ES_ERR_* code.
 * The distinctions matter and are part of the contract:
 *
 *   UNSUPPORTED - the capability does not exist here (system/screen);
 *                 asking again will not help until the environment changes.
 *   REJECTED    - the capability exists but policy refuses this window
 *                 (excluded, unsnappable, backdrop/borderless...).
 *   STALE       - the referenced window no longer exists. Never fatal:
 *                 callers must expect windows to vanish at any moment.
 *   NOT_SNAPPED - no snap state is recorded for this window.
 *   CHANGED     - recorded state was dropped because the window's geometry
 *                 was changed independently (by the app or the user).
 */
#define ES_OK                0
#define ES_ERR_UNSUPPORTED  (-1)
#define ES_ERR_REJECTED     (-2)
#define ES_ERR_STALE        (-3)
#define ES_ERR_NOT_SNAPPED  (-4)
#define ES_ERR_CHANGED      (-5)
#define ES_ERR_NO_MEMORY    (-6)
#define ES_ERR_BAD_ARGS     (-7)

/* ------------------------------------------------------ capabilities */

/*
 * Capability discovery: what this build/environment can actually do.
 * Clients must query instead of assuming; bits are added, never reused.
 */
#define ES_CAP_SNAP            0x0001UL
#define ES_CAP_RESTORE         0x0002UL
#define ES_CAP_DRAG_DETECT     0x0004UL
#define ES_CAP_PREVIEW_OUTLINE 0x0008UL
#define ES_CAP_PREVIEW_ALPHA   0x0010UL
#define ES_CAP_HOTKEYS         0x0020UL
#define ES_CAP_GUTTER          0x0040UL

#endif /* EDGESNAP_TYPES_H */
