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

/*
 * The one client written three times over, or rather once: the calls
 * are the same on every system, only the way a program reaches a
 * library differs. AmigaOS 4 goes through an interface, MorphOS
 * through the ppcinline stubs over the jump table, AROS through the
 * headers genmodule made from library/aros/edgesnap.conf. This is the
 * whole of what a second client needs to know.
 */
#if defined(__amigaos4__)
#include "interfaces/edgesnap.h"
static struct Library *EdgeSnapBase;
static struct EdgeSnapIFace *IEdgeSnap;
#define ES_CALL(fn) IEdgeSnap->fn
/* This client peeks at the active window itself, so it owns Intuition
 * (OS4 wants the interface as well). */
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;
#elif defined(__AROS__)
#define EDGESNAP_NOLIBINLINE 1
#include <proto/edgesnap.h>
struct Library *EdgeSnapBase;
#define ES_CALL(fn) fn
struct IntuitionBase *IntuitionBase;
#else
#include <ppcinline/edgesnap.h>
struct Library *EdgeSnapBase;
#define ES_CALL(fn) fn
struct IntuitionBase *IntuitionBase;
#endif

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
    case ES_ERR_IN_USE:      return "ES_ERR_IN_USE";
    default:                 return "?";
    }
}

int main(int argc, char **argv)
{
    struct Window *win;
    ULONG caps, zone;
    LONG rc;

    IntuitionBase = (void *)OpenLibrary("intuition.library", 36);
    if (IntuitionBase == NULL) {
        printf("esnaptest: cannot open intuition.library\n");
        return RETURN_FAIL;
    }
#if defined(__amigaos4__)
    IIntuition = (struct IntuitionIFace *)
        GetInterface(IntuitionBase, "main", 1, NULL);
    if (IIntuition == NULL) {
        printf("esnaptest: cannot get Intuition's interface\n");
        CloseLibrary(IntuitionBase);
        return RETURN_FAIL;
    }
#endif

    EdgeSnapBase = OpenLibrary("edgesnap.library", ES_API_VERSION);
    if (EdgeSnapBase == NULL) {
        printf("esnaptest: cannot open edgesnap.library\n");
        return RETURN_FAIL;
    }
#if defined(__amigaos4__)
    IEdgeSnap = (struct EdgeSnapIFace *)
        GetInterface(EdgeSnapBase, "main", 1, NULL);
    if (IEdgeSnap == NULL) {
        printf("esnaptest: cannot get the main interface\n");
        CloseLibrary(EdgeSnapBase);
        DropInterface((struct Interface *)IIntuition);
        CloseLibrary(IntuitionBase);
        return RETURN_FAIL;
    }
#endif
    printf("esnaptest by Michele Dipace <michele.dipace@kaffeine.net>\n");
    printf("esnaptest: opened %s %d.%d\n",
           EdgeSnapBase->lib_Node.ln_Name,
           (int)EdgeSnapBase->lib_Version, (int)EdgeSnapBase->lib_Revision);

    caps = ES_CALL(ESnap_QueryCapabilities)();
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

        rc = ES_CALL(ESnap_QueryDivider)(8, &d);
        printf("esnaptest: ESnap_QueryDivider -> %s, present %ld\n",
               rcname(rc), (long)d.present);
        if (rc == ES_OK && d.present) {
            printf("esnaptest:   strip %ld,%ld %ldx%ld at %ld "
                   "(limits %ld..%ld)\n",
                   (long)d.strip.x, (long)d.strip.y, (long)d.strip.w,
                   (long)d.strip.h, (long)d.position,
                   (long)d.minPosition, (long)d.maxPosition);
            rc = ES_CALL(ESnap_MoveDivider)(d.position + 300);
            printf("esnaptest: ESnap_MoveDivider(+300) -> %s\n",
                   rcname(rc));
            Delay(75L);
            rc = ES_CALL(ESnap_QueryDivider)(8, &d);
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
    /*
     * esnaptest LOCK: keep the active window in a locked group for
     * twenty seconds, so that a drag by hand (or by a tester's script)
     * can show that the commodity leaves it alone and the generation
     * moves. Then clean up and exit.
     */
    if (argc > 1 && argv[1][0] == 'L' && EdgeSnapBase->lib_Revision >= 12 &&
        win != NULL) {
        ULONG me = 0, grp = 0, g0, g1;

        ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_LAYOUT, &me);
        rc = ES_CALL(ESnap_CreateGroup)(0, "lock", ES_GF_LOCKED, &grp);
        rc = ES_CALL(ESnap_GroupAddWindow)(grp, win);
        g0 = ES_CALL(ESnap_QueryGeneration)(NULL);
        printf("esnaptest: window %p locked in group %lu (%s), generation "
               "%lu: drag it now, you have twenty seconds\n", (void *)win,
               (unsigned long)grp, rcname(rc), (unsigned long)g0);
        Delay(50L * 20);
        g1 = ES_CALL(ESnap_QueryGeneration)(NULL);
        printf("esnaptest: generation now %lu (%s)\n", (unsigned long)g1,
               g1 != g0 ? "moved: a drag touched it" : "unchanged");
        ES_CALL(ESnap_DeleteGroup)(grp);
        ES_CALL(ESnap_UnregisterClient)(me);
#if defined(__amigaos4__)
        DropInterface((struct Interface *)IEdgeSnap);
#endif
        CloseLibrary(EdgeSnapBase);
#if defined(__amigaos4__)
        DropInterface((struct Interface *)IIntuition);
#endif
        CloseLibrary((struct Library *)IntuitionBase);
        printf("esnaptest: done\n");
        return RETURN_OK;
    }

    if (win == NULL) {
        printf("esnaptest: no active window to play with\n");
    } else {
        printf("esnaptest: active window %p\n", (void *)win);

        rc = ES_CALL(ESnap_SnapWindow)(win, ES_ZONE_LEFT);
        printf("esnaptest: ESnap_SnapWindow(left) -> %s\n", rcname(rc));

        rc = ES_CALL(ESnap_QueryWindow)(win, &zone);
        printf("esnaptest: ESnap_QueryWindow -> %s, zone %lu\n",
               rcname(rc), (unsigned long)zone);

        Delay(75L); /* a second and a half, so the snap is visible */

        rc = ES_CALL(ESnap_UnsnapWindow)(win);
        printf("esnaptest: ESnap_UnsnapWindow -> %s\n", rcname(rc));

        /* a window that does not exist must be refused, not crash */
        rc = ES_CALL(ESnap_SnapWindow)((struct Window *)0xDEADBEEF,
                                         ES_ZONE_RIGHT);
        printf("esnaptest: snap of a bogus window -> %s (expected "
               "ES_ERR_STALE)\n", rcname(rc));

        rc = ES_CALL(ESnap_SnapWindow)(win, 99);
        printf("esnaptest: snap to zone 99 -> %s (expected "
               "ES_ERR_BAD_ARGS)\n", rcname(rc));

        /* 2.9: the middle, at the window's own size. An older library
         * answers BAD_ARGS here, which is the point of the revision. */
        if (EdgeSnapBase->lib_Revision >= 9) {
            rc = ES_CALL(ESnap_SnapWindow)(win, ES_ZONE_CENTRE);
            printf("esnaptest: ESnap_SnapWindow(centre) -> %s\n",
                   rcname(rc));
            Delay(75L);
            rc = ES_CALL(ESnap_UnsnapWindow)(win);
            printf("esnaptest: ESnap_UnsnapWindow after centre -> %s\n",
                   rcname(rc));
        }
    }

    /* 2.5: the calls a tiler makes. Only on a library that has them. */
    if (EdgeSnapBase->lib_Revision >= 5) {
        struct ESnapWindowInfo info[8];
        ULONG needed = 0, gen0, gen1, i;

        gen0 = ES_CALL(ESnap_QueryGeneration)(NULL);
        rc = ES_CALL(ESnap_QueryWindows)(NULL, info, 8, &needed);
        printf("esnaptest: ESnap_QueryWindows -> %s, %lu windows, "
               "generation %lu\n", rcname(rc), (unsigned long)needed,
               (unsigned long)gen0);
        for (i = 0; i < needed && i < 8; i++) {
            printf("esnaptest:   %p %ld,%ld %ldx%ld flags %04lx zone %lu "
                   "min %ldx%ld\n", (void *)info[i].window,
                   (long)info[i].rect.x, (long)info[i].rect.y,
                   (long)info[i].rect.w, (long)info[i].rect.h,
                   (unsigned long)info[i].flags, (unsigned long)info[i].zone,
                   (long)info[i].minWidth, (long)info[i].minHeight);
        }
        if (win != NULL) {
            struct ESnapRect r;
            struct ESnapPlacement one;

            r.x = 100;
            r.y = 100;
            r.w = 400;
            r.h = 300;
            rc = ES_CALL(ESnap_PlaceWindow)(win, &r, 0);
            printf("esnaptest: ESnap_PlaceWindow(100,100 400x300) -> %s\n",
                   rcname(rc));
            Delay(25L);
            gen1 = ES_CALL(ESnap_QueryGeneration)(NULL);
            printf("esnaptest: generation %lu -> %lu (%s)\n",
                   (unsigned long)gen0, (unsigned long)gen1,
                   gen1 != gen0 ? "moved, as it should" : "did not move");
            rc = ES_CALL(ESnap_QueryWindow)(win, &zone);
            printf("esnaptest: ESnap_QueryWindow -> %s, zone %lu (expected "
                   "%d, rect)\n", rcname(rc), (unsigned long)zone,
                   ES_ZONE_RECT);
            Delay(50L);
            one.window = win;
            one.rect.x = 300;
            one.rect.y = 150;
            one.rect.w = 500;
            one.rect.h = 350;
            one.result = 0;
            rc = ES_CALL(ESnap_PlaceWindowsA)(&one, 1, ES_PF_NO_RESTORE);
            printf("esnaptest: ESnap_PlaceWindowsA(1) -> %s, entry %s\n",
                   rcname(rc), rcname(one.result));
            Delay(50L);
            rc = ES_CALL(ESnap_UnsnapWindow)(win);
            printf("esnaptest: ESnap_UnsnapWindow after placing -> %s\n",
                   rcname(rc));
        }
    } else {
        printf("esnaptest: library revision %d has no 2.5 calls, skipped\n",
               (int)EdgeSnapBase->lib_Revision);
    }

    /* 2.6: identity. The serial must come back to the same window,
     * and a serial nobody was given must be refused. */
    if (EdgeSnapBase->lib_Revision >= 7) {
        ES_CALL(ESnap_FeedMotion)(0, 0);
        printf("esnaptest: ESnap_FeedMotion(0, 0) accepted (2.7)\n");
    }
    if (EdgeSnapBase->lib_Revision >= 6 && win != NULL) {
        ULONG serial = 0, again = 0;
        struct Window *back = NULL;

        rc = ES_CALL(ESnap_QueryWindowSerial)(win, &serial);
        printf("esnaptest: ESnap_QueryWindowSerial -> %s, serial %lu\n",
               rcname(rc), (unsigned long)serial);
        rc = ES_CALL(ESnap_QueryWindowSerial)(win, &again);
        printf("esnaptest: asked again -> %s, serial %lu (%s)\n",
               rcname(rc), (unsigned long)again,
               again == serial ? "same, as it should" : "DIFFERENT");
        rc = ES_CALL(ESnap_FindWindow)(serial, &back);
        printf("esnaptest: ESnap_FindWindow(%lu) -> %s, %p (%s)\n",
               (unsigned long)serial, rcname(rc), (void *)back,
               back == win ? "the same window" : "ANOTHER window");
        rc = ES_CALL(ESnap_FindWindow)(serial + 1000000UL, &back);
        printf("esnaptest: ESnap_FindWindow(unknown) -> %s (expected "
               "ES_ERR_STALE)\n", rcname(rc));
    }

    /* 2.10: roles. A layout client may always register; the engine
     * role is taken when the commodity runs, free when it does not;
     * a layout client's Enable must leave the engine alone; an id
     * given back twice is stale the second time. */
    if (EdgeSnapBase->lib_Revision >= 10) {
        ULONG me = 0, engine = 0;

        rc = ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_LAYOUT, &me);
        printf("esnaptest: ESnap_RegisterClient(layout) -> %s, id %lu\n",
               rcname(rc), (unsigned long)me);
        rc = ES_CALL(ESnap_Enable)(FALSE);
        printf("esnaptest: ESnap_Enable(FALSE) as a layout client -> %s "
               "(the commodity must keep snapping)\n", rcname(rc));
        ES_CALL(ESnap_Enable)(TRUE);
        rc = ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_ENGINE, &engine);
        printf("esnaptest: ESnap_RegisterClient(engine) -> %s (ES_ERR_IN_USE "
               "while the commodity runs, ES_OK when it does not)\n",
               rcname(rc));
        if (rc == ES_OK) {
            rc = ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_LAYOUT, &me);
            printf("esnaptest: back to layout only -> %s, id %lu (%s)\n",
                   rcname(rc), (unsigned long)me,
                   me == engine ? "same id, as it should" : "DIFFERENT id");
        }
        rc = ES_CALL(ESnap_UnregisterClient)(me);
        printf("esnaptest: ESnap_UnregisterClient -> %s\n", rcname(rc));
        rc = ES_CALL(ESnap_UnregisterClient)(me);
        printf("esnaptest: unregistered twice -> %s (expected "
               "ES_ERR_STALE)\n", rcname(rc));
    }

    /* 2.11: work areas. One per screen for now, with an id that comes
     * back the same when asked again. */
    if (EdgeSnapBase->lib_Revision >= 11) {
        struct ESnapWorkArea wa[4];
        ULONG need = 0, first = 0;

        rc = ES_CALL(ESnap_QueryWorkAreas)(NULL, NULL, 0, &need);
        printf("esnaptest: ESnap_QueryWorkAreas(count 0) -> %s, %lu area(s)\n",
               rcname(rc), (unsigned long)need);
        rc = ES_CALL(ESnap_QueryWorkAreas)(NULL, wa, 4, &need);
        if (rc == ES_OK && need >= 1) {
            first = wa[0].id;
            printf("esnaptest: area id %lu monitor %lu, screen %ldx%ld, "
                   "usable %ld,%ld %ldx%ld, insets %ld %ld %ld %ld\n",
                   (unsigned long)wa[0].id, (unsigned long)wa[0].monitor,
                   (long)wa[0].bounds.w, (long)wa[0].bounds.h,
                   (long)wa[0].area.usable.x, (long)wa[0].area.usable.y,
                   (long)wa[0].area.usable.w, (long)wa[0].area.usable.h,
                   (long)wa[0].area.insetLeft, (long)wa[0].area.insetTop,
                   (long)wa[0].area.insetRight, (long)wa[0].area.insetBottom);
        } else {
            printf("esnaptest: ESnap_QueryWorkAreas -> %s\n", rcname(rc));
        }
        rc = ES_CALL(ESnap_QueryWorkAreas)(NULL, wa, 4, &need);
        printf("esnaptest: asked again -> %s, id %lu (%s)\n", rcname(rc),
               (unsigned long)wa[0].id,
               wa[0].id == first ? "same, as it should" : "DIFFERENT");
    }

    /* 2.12: groups. Made by a layout client, in a work area, by serial;
     * the owner may still snap its own locked window; a window is in
     * one group at a time; a deleted group is stale. */
    if (EdgeSnapBase->lib_Revision >= 12 && win != NULL) {
        ULONG me = 0, area = 0, grp = 0, grp2 = 0, of = 0, need = 0;
        struct ESnapWorkArea wa[1];

        rc = ES_CALL(ESnap_CreateGroup)(0, "orphan", 0, &grp);
        printf("esnaptest: ESnap_CreateGroup without a role -> %s (expected "
               "ES_ERR_REJECTED)\n", rcname(rc));
        ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_LAYOUT, &me);
        if (ES_CALL(ESnap_QueryWorkAreas)(NULL, wa, 1, &need) == ES_OK) {
            area = wa[0].id;
        }
        rc = ES_CALL(ESnap_CreateGroup)(area, "test", ES_GF_LOCKED, &grp);
        printf("esnaptest: ESnap_CreateGroup(area %lu, locked) -> %s, id %lu\n",
               (unsigned long)area, rcname(rc), (unsigned long)grp);
        rc = ES_CALL(ESnap_GroupAddWindow)(grp, win);
        printf("esnaptest: ESnap_GroupAddWindow -> %s\n", rcname(rc));
        rc = ES_CALL(ESnap_QueryGroupOf)(win, &of);
        printf("esnaptest: ESnap_QueryGroupOf -> %s, group %lu (%s)\n",
               rcname(rc), (unsigned long)of,
               of == grp ? "the one we made" : "ANOTHER");
        rc = ES_CALL(ESnap_SnapWindow)(win, ES_ZONE_LEFT);
        printf("esnaptest: the owner snaps its locked window -> %s\n",
               rcname(rc));
        Delay(50L);
        ES_CALL(ESnap_UnsnapWindow)(win);
        rc = ES_CALL(ESnap_CreateGroup)(0, "second", 0, &grp2);
        rc = ES_CALL(ESnap_GroupAddWindow)(grp2, win);
        printf("esnaptest: the same window into a second group -> %s "
               "(expected ES_ERR_REJECTED)\n", rcname(rc));
        rc = ES_CALL(ESnap_GroupRemoveWindow)(grp, win);
        printf("esnaptest: ESnap_GroupRemoveWindow -> %s\n", rcname(rc));
        rc = ES_CALL(ESnap_QueryGroupOf)(win, &of);
        printf("esnaptest: ESnap_QueryGroupOf after removing -> %s, group %lu "
               "(expected 0)\n", rcname(rc), (unsigned long)of);
        rc = ES_CALL(ESnap_DeleteGroup)(grp);
        printf("esnaptest: ESnap_DeleteGroup -> %s\n", rcname(rc));
        rc = ES_CALL(ESnap_DeleteGroup)(grp);
        printf("esnaptest: deleted twice -> %s (expected ES_ERR_STALE)\n",
               rcname(rc));
        ES_CALL(ESnap_DeleteGroup)(grp2);
        ES_CALL(ESnap_UnregisterClient)(me);
    }

#if defined(__amigaos4__)
    DropInterface((struct Interface *)IEdgeSnap);
#endif
    CloseLibrary(EdgeSnapBase);
#if defined(__amigaos4__)
    DropInterface((struct Interface *)IIntuition);
#endif
    CloseLibrary((struct Library *)IntuitionBase);
    printf("esnaptest: done\n");
    return RETURN_OK;
}
