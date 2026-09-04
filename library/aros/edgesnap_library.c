/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_library.c - AROS skeleton of edgesnap.library.
 *
 * Same body, third set of clothes. AROS generates the library node,
 * the jump table and the client headers from edgesnap.conf with its
 * own genmodule tool; what is left to write by hand is each vector as
 * an AROS_LH function over the shared implementation, and the two
 * moments the body cares about: the first open, and the last close.
 * The vector numbers here ARE the ABI and mirror edgesnap.conf, which
 * mirrors the MorphOS .fd: append only, never reorder.
 *
 * The library links without a C runtime, so it provides the memcpy
 * and memset the compiler emits for structure assignment, as the
 * other two skeletons do.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>

#include <proto/exec.h>

#include "edgesnap.h"
#include "edgesnap_body.h"
#include "edgesnap_base.h"
#include "edgesnap_libdefs.h"

/*
 * The exec base the body's calls go through. A module has no startup
 * code to set it: the generated node stores the base it was
 * initialised with in the library node, and the init hook below copies
 * it here before anything in the body can run.
 */
struct ExecBase *SysBase;

/* The body speaks Intuition; the generated library node opens it at
 * init and closes it at expunge, on the strength of this line. */
ADD2LIBS((CONST_STRPTR)"intuition.library", 36, struct IntuitionBase *, IntuitionBase);

static int EdgeSnap_Init(struct EdgeSnapBase *base)
{
    SysBase = base->esb_SysBase;
    return SysBase != NULL;
}

ADD2INITLIB(EdgeSnap_Init, 0);

void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);

void *memcpy(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

/* ------------------------------------------------- open and close */

/* The body is brought up by the first client and taken down by the
 * last: a failed esb_init() refuses the open, as on the other two
 * systems. lib_OpenCnt is still the old count when these run. */
static int EdgeSnap_FirstOpen(struct EdgeSnapBase *base)
{
    if (base->esb_Lib.lib_OpenCnt == 0 && !esb_init()) {
        return 0;
    }
    return 1;
}

static void EdgeSnap_LastClose(struct EdgeSnapBase *base)
{
    if (base->esb_Lib.lib_OpenCnt == 0) {
        esb_cleanup();
    }
}

ADD2OPENLIB(EdgeSnap_FirstOpen, 0);
ADD2CLOSELIB(EdgeSnap_LastClose, 0);

/* ------------------------------------------------------- vectors */

AROS_LH0(ULONG, ESnap_QueryCapabilities,
         struct EdgeSnapBase *, EdgeSnapBase, 5, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_query_capabilities();
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ESnap_SnapWindow,
         AROS_LHA(struct Window *, win, A0),
         AROS_LHA(ULONG, zone, D0),
         struct EdgeSnapBase *, EdgeSnapBase, 6, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_snap_window(win, zone);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ESnap_UnsnapWindow,
         AROS_LHA(struct Window *, win, A0),
         struct EdgeSnapBase *, EdgeSnapBase, 7, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_unsnap_window(win);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ESnap_QueryWindow,
         AROS_LHA(struct Window *, win, A0),
         AROS_LHA(ULONG *, zone_out, A1),
         struct EdgeSnapBase *, EdgeSnapBase, 8, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_query_window(win, zone_out);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ESnap_ExcludeWindow,
         AROS_LHA(struct Window *, win, A0),
         AROS_LHA(BOOL, exclude, D0),
         struct EdgeSnapBase *, EdgeSnapBase, 9, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_exclude_window(win, exclude);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ESnap_SetOptionsA,
         AROS_LHA(const struct TagItem *, tags, A0),
         struct EdgeSnapBase *, EdgeSnapBase, 10, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_set_options(tags);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ESnap_Enable,
         AROS_LHA(BOOL, on, D0),
         struct EdgeSnapBase *, EdgeSnapBase, 11, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_enable(on);
    AROS_LIBFUNC_EXIT
}

AROS_LH5(void, ESnap_FeedInput,
         AROS_LHA(ULONG, presses, D0),
         AROS_LHA(ULONG, motions, D1),
         AROS_LHA(ULONG, releases, D2),
         AROS_LHA(ULONG, qualifiers, D3),
         AROS_LHA(struct ESnapReport *, report, A0),
         struct EdgeSnapBase *, EdgeSnapBase, 12, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    esb_input((int)presses, (int)motions, (int)releases, qualifiers, report);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(void, ESnap_ResetInput,
         AROS_LHA(struct ESnapReport *, report, A0),
         struct EdgeSnapBase *, EdgeSnapBase, 13, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    esb_input_reset(report);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ESnap_IgnoreWindows,
         AROS_LHA(struct Window **, windows, A0),
         AROS_LHA(ULONG, count, D0),
         struct EdgeSnapBase *, EdgeSnapBase, 14, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    esb_ignore_windows(windows, (int)count);
    return ES_OK;
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ESnap_QueryScreenArea,
         AROS_LHA(struct Screen *, screen, A0),
         AROS_LHA(struct ESnapArea *, area, A1),
         struct EdgeSnapBase *, EdgeSnapBase, 15, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_query_screen_area(screen, area);
    AROS_LIBFUNC_EXIT
}

AROS_LH2(LONG, ESnap_QueryDivider,
         AROS_LHA(ULONG, thickness, D0),
         AROS_LHA(struct ESnapDivider *, divider, A0),
         struct EdgeSnapBase *, EdgeSnapBase, 16, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_query_divider(thickness, divider);
    AROS_LIBFUNC_EXIT
}

AROS_LH1(LONG, ESnap_MoveDivider,
         AROS_LHA(LONG, position, D0),
         struct EdgeSnapBase *, EdgeSnapBase, 17, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_move_divider(position);
    AROS_LIBFUNC_EXIT
}

AROS_LH4(LONG, ESnap_QueryDividerAt,
         AROS_LHA(ULONG, thickness, D0),
         AROS_LHA(LONG, x, D1),
         AROS_LHA(LONG, y, D2),
         AROS_LHA(struct ESnapDivider *, divider, A0),
         struct EdgeSnapBase *, EdgeSnapBase, 18, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_query_divider_at(thickness, x, y, divider);
    AROS_LIBFUNC_EXIT
}

AROS_LH3(LONG, ESnap_MoveDividerAt,
         AROS_LHA(LONG, vertical, A0),
         AROS_LHA(LONG, line, A1),
         AROS_LHA(LONG, position, D0),
         struct EdgeSnapBase *, EdgeSnapBase, 19, EdgeSnap)
{
    AROS_LIBFUNC_INIT
    return esb_move_divider_at(vertical, line, position);
    AROS_LIBFUNC_EXIT
}
