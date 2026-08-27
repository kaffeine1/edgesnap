/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * esnaptest.c - third-party client of edgesnap.library (AmigaOS 4).
 *
 * This is the proof that the public API is usable by software that
 * knows nothing about EdgeSnap's internals: it opens the library like
 * any other, asks what it can do, snaps the window that is active,
 * queries it, and puts it back. It exists to exercise the ABI - if
 * this stops working, the library broke its contract.
 */

#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif

#include <stdio.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include "edgesnap.h"
#include "interfaces/edgesnap.h"

/*
 * Call form on OS4: the SDK's APICALL is __attribute__((libcall)), so
 * the interface pointer is passed implicitly and Self is NOT written
 * at the call site - exactly like IIntuition->ChangeWindowBox(...).
 * The declaration in interfaces/edgesnap.h still names Self first,
 * which is what the callee receives.
 */

static struct Library *EdgeSnapBase;
static struct EdgeSnapIFace *IEdgeSnap;

/* This client peeks at the active window itself, so it owns Intuition
 * as any application would; edgesnap.library opens its own. */
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;

static const char *rcname(LONG rc)
{
    switch (rc) {
    case ES_OK:              return "ES_OK";
    case ES_ERR_UNSUPPORTED: return "ES_ERR_UNSUPPORTED";
    case ES_ERR_REJECTED:    return "ES_ERR_REJECTED";
    case ES_ERR_STALE:       return "ES_ERR_STALE";
    case ES_ERR_NOT_SNAPPED: return "ES_ERR_NOT_SNAPPED";
    case ES_ERR_CHANGED:     return "ES_ERR_CHANGED";
    case ES_ERR_NO_MEMORY:   return "ES_ERR_NO_MEMORY";
    case ES_ERR_BAD_ARGS:    return "ES_ERR_BAD_ARGS";
    default:                 return "?";
    }
}

int main(void)
{
    struct Window *win;
    ULONG caps, zone;
    LONG rc;

    IntuitionBase = OpenLibrary("intuition.library", 36);
    if (IntuitionBase != NULL) {
        IIntuition = (struct IntuitionIFace *)
            GetInterface(IntuitionBase, "main", 1, NULL);
    }
    if (IIntuition == NULL) {
        printf("esnaptest: cannot open intuition.library\n");
        return RETURN_FAIL;
    }

    EdgeSnapBase = OpenLibrary("edgesnap.library", ES_API_VERSION);
    if (EdgeSnapBase == NULL) {
        printf("esnaptest: cannot open edgesnap.library\n");
        return RETURN_FAIL;
    }
    IEdgeSnap = (struct EdgeSnapIFace *)
        GetInterface(EdgeSnapBase, "main", 1, NULL);
    if (IEdgeSnap == NULL) {
        printf("esnaptest: cannot get the main interface\n");
        CloseLibrary(EdgeSnapBase);
        DropInterface((struct Interface *)IIntuition);
        CloseLibrary(IntuitionBase);
        return RETURN_FAIL;
    }
    printf("esnaptest by Michele Dipace <michele.dipace@kaffeine.net>\n");
    printf("esnaptest: opened %s %d.%d\n",
           EdgeSnapBase->lib_Node.ln_Name,
           (int)EdgeSnapBase->lib_Version, (int)EdgeSnapBase->lib_Revision);

    caps = IEdgeSnap->ESnap_QueryCapabilities();
    printf("esnaptest: capabilities %08lx (snap %s, restore %s, "
           "drag %s, outline %s, alpha %s, gutter %s)\n",
           (unsigned long)caps,
           (caps & ES_CAP_SNAP) ? "y" : "n",
           (caps & ES_CAP_RESTORE) ? "y" : "n",
           (caps & ES_CAP_DRAG_DETECT) ? "y" : "n",
           (caps & ES_CAP_PREVIEW_OUTLINE) ? "y" : "n",
           (caps & ES_CAP_PREVIEW_ALPHA) ? "y" : "n",
           (caps & ES_CAP_GUTTER) ? "y" : "n");

    /* The divider: with two windows snapped side by side the library
     * should offer a seam, and moving it must resize both. This is the
     * deterministic half of the feature - the frontend's little handle
     * window is the part a human tests by grabbing it. */
    {
        struct ESnapDivider d;

        rc = IEdgeSnap->ESnap_QueryDivider(8, &d);
        printf("esnaptest: ESnap_QueryDivider -> %s, present %ld\n",
               rcname(rc), (long)d.present);
        if (rc == ES_OK && d.present) {
            printf("esnaptest:   strip %ld,%ld %ldx%ld at %ld "
                   "(limits %ld..%ld)\n",
                   (long)d.strip.x, (long)d.strip.y, (long)d.strip.w,
                   (long)d.strip.h, (long)d.position,
                   (long)d.minPosition, (long)d.maxPosition);
            rc = IEdgeSnap->ESnap_MoveDivider(d.position + 300);
            printf("esnaptest: ESnap_MoveDivider(+300) -> %s\n",
                   rcname(rc));
            Delay(75L);
            rc = IEdgeSnap->ESnap_QueryDivider(8, &d);
            if (rc == ES_OK && d.present) {
                printf("esnaptest:   seam is now at %ld\n",
                       (long)d.position);
            }
        }
    }


    {
        ULONG ilock = LockIBase(0);

        win = ((struct IntuitionBase *)IntuitionBase)->ActiveWindow;
        UnlockIBase(ilock);
    }
    if (win == NULL) {
        printf("esnaptest: no active window to play with\n");
    } else {
        printf("esnaptest: active window %p\n", (void *)win);

        rc = IEdgeSnap->ESnap_SnapWindow(win, ES_ZONE_LEFT);
        printf("esnaptest: ESnap_SnapWindow(left) -> %s\n", rcname(rc));

        rc = IEdgeSnap->ESnap_QueryWindow(win, &zone);
        printf("esnaptest: ESnap_QueryWindow -> %s, zone %lu\n",
               rcname(rc), (unsigned long)zone);

        Delay(75L); /* a second and a half, so the snap is visible */

        rc = IEdgeSnap->ESnap_UnsnapWindow(win);
        printf("esnaptest: ESnap_UnsnapWindow -> %s\n", rcname(rc));

        /* a window that does not exist must be refused, not crash */
        rc = IEdgeSnap->ESnap_SnapWindow((struct Window *)0xDEADBEEF,
                                         ES_ZONE_RIGHT);
        printf("esnaptest: snap of a bogus window -> %s (expected "
               "ES_ERR_STALE)\n", rcname(rc));

        rc = IEdgeSnap->ESnap_SnapWindow(win, 99);
        printf("esnaptest: snap to zone 99 -> %s (expected "
               "ES_ERR_BAD_ARGS)\n", rcname(rc));
    }

    DropInterface((struct Interface *)IEdgeSnap);
    CloseLibrary(EdgeSnapBase);
    DropInterface((struct Interface *)IIntuition);
    CloseLibrary(IntuitionBase);
    printf("esnaptest: done\n");
    return RETURN_OK;
}
