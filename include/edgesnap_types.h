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

/*
 * API generation. A client opens the library with the generation it
 * needs, and an older library REFUSES to open - which is the only
 * thing standing between a new client and a jump into a vector that
 * does not exist. Learned the hard way on 2026-08-27: a commodity
 * built against generation 1 opened a generation-0 library and died
 * with an ISI at address 0.
 *
 *   0 - snap/unsnap/query/exclude/options/enable/capabilities
 *   1 - adds the frontend integration group (FeedInput, ResetInput,
 *       IgnoreWindows, QueryScreenArea)
 *   2 - adds the divider between snapped windows (QueryDivider,
 *       MoveDivider)
 *   2.3 - a seam is a LINE, not a pair: it may join more than two
 *       windows and a layout may hold several. QueryDividerAt ("what
 *       is under the pointer") appended. (This revision also changed
 *       MoveDivider's arguments in place, which was a mistake and
 *       lived only on the author's machines.)
 *   2.4 - MoveDivider back to its 2.2 arguments and meaning (the first
 *       seam); MoveDividerAt (move a NAMED seam) appended. A revision
 *       of its own so that copylib replaces a 2.3, and so that a
 *       client can tell the two apart: methods appended, never moved,
 *       never changed - the major version is the compatibility
 *       promise, and the revision says which appended vectors exist.
 *   2.5 - the first client beyond the commodity, a tiler, asked for
 *       the windows as the library sees them and for placement in an
 *       arbitrary rectangle: QueryWindows, QueryGeneration, PlaceWindow
 *       and PlaceWindowsA appended, and ES_ZONE_RECT for what they place.
 */
#define ES_API_VERSION       2
/* A client that uses a vector appended after 2.2 must check that the
 * library it opened is new enough to HAVE it: OpenLibrary() compares
 * the version only, and a 2.x too old for the call would send it into
 * a jump table entry that is not there. On MorphOS that is a 68k
 * illegal instruction at PC 0x4e, seen in the field on 2026-09-02. */
#define ES_LIB_MIN_REVISION  4

/* ------------------------------------------------------------- zones */

#define ES_ZONE_NONE         0
#define ES_ZONE_LEFT         1
#define ES_ZONE_RIGHT        2
#define ES_ZONE_TOP_LEFT     3
#define ES_ZONE_TOP_RIGHT    4
#define ES_ZONE_BOTTOM_LEFT  5
#define ES_ZONE_BOTTOM_RIGHT 6
#define ES_ZONE_MAX          7
#define ES_ZONE_RECT         8   /* placed in a rectangle by a client (2.5) */

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
