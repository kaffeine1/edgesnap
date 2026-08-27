/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * esnaptest_mos.c - third-party client of edgesnap.library (MorphOS).
 *
 * The same test as the OS4 client, through the classic jump table:
 * OpenLibrary gives a base, and the ppcinline stubs turn each call
 * into the documented register ABI.
 *
 * This is the proof that the public API is usable by software that
 * knows nothing about EdgeSnap's internals: it opens the library like
 * any other, asks what it can do, snaps the window that is active,
 * queries it, and puts it back. It exists to exercise the ABI - if
 * this stops working, the library broke its contract.
 */

#include <stdio.h>

#include <exec/types.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <dos/dos.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>

#include "edgesnap.h"
#include <ppcinline/edgesnap.h>

/* Call form on MorphOS: the ppcinline stubs read EdgeSnapBase from
 * this file's global and marshal the arguments into the registers the
 * library's gates expect. */

struct Library *EdgeSnapBase;

/* This client peeks at the active window itself, so it owns Intuition
 * as any application would; edgesnap.library opens its own. */
struct IntuitionBase *IntuitionBase;

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

    IntuitionBase = (struct IntuitionBase *)
        OpenLibrary("intuition.library", 36);
    if (IntuitionBase == NULL) {
        printf("esnaptest: cannot open intuition.library\n");
        return RETURN_FAIL;
    }

    EdgeSnapBase = OpenLibrary("edgesnap.library", ES_API_VERSION);
    if (EdgeSnapBase == NULL) {
        printf("esnaptest: cannot open edgesnap.library\n");
        return RETURN_FAIL;
    }
    printf("esnaptest by Michele Dipace <michele.dipace@kaffeine.net>\n");
    printf("esnaptest: opened %s %d.%d\n",
           EdgeSnapBase->lib_Node.ln_Name,
           (int)EdgeSnapBase->lib_Version, (int)EdgeSnapBase->lib_Revision);

    caps = ESnap_QueryCapabilities();
    printf("esnaptest: capabilities %08lx (snap %s, restore %s, "
           "drag %s, outline %s, alpha %s, gutter %s)\n",
           (unsigned long)caps,
           (caps & ES_CAP_SNAP) ? "y" : "n",
           (caps & ES_CAP_RESTORE) ? "y" : "n",
           (caps & ES_CAP_DRAG_DETECT) ? "y" : "n",
           (caps & ES_CAP_PREVIEW_OUTLINE) ? "y" : "n",
           (caps & ES_CAP_PREVIEW_ALPHA) ? "y" : "n",
           (caps & ES_CAP_GUTTER) ? "y" : "n");

    {
        ULONG ilock = LockIBase(0);

        win = IntuitionBase->ActiveWindow;
        UnlockIBase(ilock);
    }
    if (win == NULL) {
        printf("esnaptest: no active window to play with\n");
    } else {
        printf("esnaptest: active window %p\n", (void *)win);

        rc = ESnap_SnapWindow(win, ES_ZONE_LEFT);
        printf("esnaptest: ESnap_SnapWindow(left) -> %s\n", rcname(rc));

        rc = ESnap_QueryWindow(win, &zone);
        printf("esnaptest: ESnap_QueryWindow -> %s, zone %lu\n",
               rcname(rc), (unsigned long)zone);

        Delay(75L); /* a second and a half, so the snap is visible */

        rc = ESnap_UnsnapWindow(win);
        printf("esnaptest: ESnap_UnsnapWindow -> %s\n", rcname(rc));

        /* a window that does not exist must be refused, not crash */
        rc = ESnap_SnapWindow((struct Window *)0xDEADBEEF,
                                         ES_ZONE_RIGHT);
        printf("esnaptest: snap of a bogus window -> %s (expected "
               "ES_ERR_STALE)\n", rcname(rc));

        rc = ESnap_SnapWindow(win, 99);
        printf("esnaptest: snap to zone 99 -> %s (expected "
               "ES_ERR_BAD_ARGS)\n", rcname(rc));
    }

    CloseLibrary(EdgeSnapBase);
    CloseLibrary((struct Library *)IntuitionBase);
    printf("esnaptest: done\n");
    return RETURN_OK;
}
