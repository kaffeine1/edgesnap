/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * sidebyside.c - a small layout client of edgesnap.library.
 *
 * The worked example of the SDK: what a program that lays out its own
 * windows does with the library, from opening it to letting it go.
 * It opens two windows, puts them side by side in the usable area of
 * the screen, with a gap between them, and moves the seam between them
 * when the cursor keys are pressed:
 *
 *     Cursor left/right   move the seam by 48 pixels
 *     Esc, or a close gadget   quit
 *
 * A window moved or sized by hand is not fought over: the next key
 * press finds the seam changed and lays both windows out again.
 *
 * One source for AmigaOS 4, MorphOS and AROS. The calls are the same
 * everywhere; only the way a program reaches a library differs, and
 * that is the first block below. Error handling is complete, because
 * the example is also what people copy.
 *
 * Built from the top of the SDK, or of the source tree:
 *   MorphOS      ppc-morphos-gcc -noixemul -Iinclude -o sidebyside \
 *                    examples/sidebyside.c
 *   AmigaOS 4    ppc-amigaos-gcc -Iinclude -o sidebyside \
 *                    examples/sidebyside.c
 *   AROS         gcc -Iinclude/aros -Iinclude -o sidebyside \
 *                    examples/sidebyside.c
 * (on AROS with the system's own gcc, or the cross one of its SDK).
 */

#ifndef __USE_INLINE__
#define __USE_INLINE__   /* AmigaOS 4: the system calls without I...-> */
#endif

#include <stdio.h>

#include <exec/types.h>
#include <exec/libraries.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/intuition.h>

#include "edgesnap.h"

/*
 * Reaching the library. AmigaOS 4 calls through an interface, MorphOS
 * through the ppcinline stubs over the jump table, AROS through the
 * headers genmodule makes from the library's .conf. ES() hides only
 * that: ES(ESnap_CreateGroup)(...) is a plain call on MorphOS and AROS.
 */
#if defined(__amigaos4__)
#include "interfaces/edgesnap.h"
static struct Library *EdgeSnapBase;
static struct EdgeSnapIFace *IEdgeSnap;
#define ES(fn) IEdgeSnap->fn
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;
#elif defined(__AROS__)
#define EDGESNAP_NOLIBINLINE 1
#include <proto/edgesnap.h>
struct Library *EdgeSnapBase;
#define ES(fn) fn
struct IntuitionBase *IntuitionBase;
#else
#include <ppcinline/edgesnap.h>
struct Library *EdgeSnapBase;
#define ES(fn) fn
struct IntuitionBase *IntuitionBase;
#endif

/*
 * OpenLibrary() checks the version only. Every call made here was in
 * the library by revision 24 (2.24, divider tokens), so an older 2.x
 * is refused before a single call: on a library too old to have it,
 * a call would land on a jump table entry that is not there.
 */
#define NEED_REVISION 24

#define GAP   8    /* pixels between the two windows */
#define STEP  48   /* how far one key press moves the seam */

#define RAW_ESC    0x45
#define RAW_RIGHT  0x4E
#define RAW_LEFT   0x4F

static const char *errname(LONG rc)
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
    default:                 return "unknown error";
    }
}

static int opened_libraries(void)
{
    IntuitionBase = (void *)OpenLibrary((STRPTR)"intuition.library", 39);
    if (IntuitionBase == NULL) {
        return 0;
    }
#if defined(__amigaos4__)
    IIntuition = (struct IntuitionIFace *)
        GetInterface(IntuitionBase, "main", 1, NULL);
    if (IIntuition == NULL) {
        return 0;
    }
#endif
    EdgeSnapBase = OpenLibrary((STRPTR)"edgesnap.library", ES_API_VERSION);
    if (EdgeSnapBase == NULL) {
        printf("sidebyside: edgesnap.library %d is not installed\n",
               (int)ES_API_VERSION);
        return 0;
    }
    if (EdgeSnapBase->lib_Revision < NEED_REVISION) {
        printf("sidebyside: edgesnap.library %d.%d is too old, %d.%d "
               "is needed\n", (int)EdgeSnapBase->lib_Version,
               (int)EdgeSnapBase->lib_Revision, (int)ES_API_VERSION,
               NEED_REVISION);
        return 0;
    }
#if defined(__amigaos4__)
    IEdgeSnap = (struct EdgeSnapIFace *)
        GetInterface(EdgeSnapBase, "main", 1, NULL);
    if (IEdgeSnap == NULL) {
        return 0;
    }
#endif
    return 1;
}

static void close_libraries(void)
{
#if defined(__amigaos4__)
    if (IEdgeSnap != NULL) {
        DropInterface((struct Interface *)IEdgeSnap);
    }
    if (IIntuition != NULL) {
        DropInterface((struct Interface *)IIntuition);
    }
#endif
    if (EdgeSnapBase != NULL) {
        CloseLibrary(EdgeSnapBase);
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
    }
}

static struct Window *open_window(struct Screen *scr, const char *title,
                                  BOOL activate)
{
    /* Where it opens does not matter: the layout moves it at once. */
    return OpenWindowTags(NULL,
                          WA_PubScreen, scr,
                          WA_Left, 40,
                          WA_Top, 60,
                          WA_Width, 320,
                          WA_Height, 200,
                          WA_MinWidth, 120,
                          WA_MinHeight, 60,
                          WA_MaxWidth, ~0,
                          WA_MaxHeight, ~0,
                          WA_Title, title,
                          WA_ScreenTitle,
                              "sidebyside: cursor left/right moves the "
                              "seam, Esc quits",
                          WA_Flags, WFLG_DRAGBAR | WFLG_DEPTHGADGET |
                                    WFLG_CLOSEGADGET | WFLG_SIZEGADGET |
                                    WFLG_SMART_REFRESH |
                                    WFLG_NOCAREREFRESH |
                                    (activate ? WFLG_ACTIVATE : 0),
                          WA_IDCMP, IDCMP_CLOSEWINDOW | IDCMP_RAWKEY,
                          TAG_DONE);
}

/*
 * The whole layout: the usable area cut in two at split, a cell each.
 * With ES_PF_CELL the library takes the group's gap off the two cells
 * where they meet, so the windows end up GAP pixels apart and the
 * seam between them is the boundary of the cells, at split.
 */
static LONG lay_out(struct Window *left, struct Window *right,
                    const struct ESnapRect *u, LONG split)
{
    struct ESnapPlacement p[2];
    LONG rc;
    int i;

    p[0].window = left;
    p[0].rect.x = u->x;
    p[0].rect.y = u->y;
    p[0].rect.w = split - u->x;
    p[0].rect.h = u->h;
    p[1].window = right;
    p[1].rect.x = split;
    p[1].rect.y = u->y;
    p[1].rect.w = u->x + u->w - split;
    p[1].rect.h = u->h;
    rc = ES(ESnap_PlaceWindowsA)(p, 2, ES_PF_CELL);
    if (rc != ES_OK) {
        return rc;
    }
    /* Each window has its own result: one may fail and not the other. */
    for (i = 0; i < 2; i++) {
        if (p[i].result != ES_OK) {
            return p[i].result;
        }
    }
    return ES_OK;
}

/*
 * One key press. The seam is asked for afresh each time, for its token
 * and its limits; moving it by its token moves only its two windows.
 * When the token no longer names that seam (STALE) or a window was
 * moved by hand (CHANGED), the program does what a tiler does: it lays
 * its windows out again, which also gives the seam a new token.
 */
static void move_seam(ULONG group, struct Window *left,
                      struct Window *right, const struct ESnapRect *u,
                      LONG *split, LONG delta)
{
    struct ESnapDividerInfo seam;
    ULONG needed = 0;
    LONG applied = 0;
    LONG rc;

    rc = ES(ESnap_QueryDividers)(group, &seam, 1, &needed);
    if (rc == ES_OK && needed >= 1 && seam.token != 0) {
        rc = ES(ESnap_MoveDividerT)(seam.token, seam.position + delta, 0,
                                    &applied);
        if (rc == ES_OK) {
            *split = applied;
            return;
        }
        if (rc == ES_ERR_UNSUPPORTED) {
            return;   /* no room left that way: nothing to do */
        }
    }
    /* No seam (a window is still on its way, or was moved by hand), a
     * retired token, or a changed window: start the layout again from
     * where the seam should be. */
    *split += delta;
    if (*split < u->x + 1) {
        *split = u->x + 1;
    }
    if (*split > u->x + u->w - 1) {
        *split = u->x + u->w - 1;
    }
    rc = lay_out(left, right, u, *split);
    if (rc != ES_OK) {
        printf("sidebyside: laying out again: %s\n", errname(rc));
    }
}

int main(void)
{
    struct Screen *scr = NULL;
    struct Window *left = NULL;
    struct Window *right = NULL;
    struct ESnapWorkArea wa;
    struct TagItem options[2];
    ULONG client = 0;
    ULONG group = 0;
    ULONG needed = 0;
    LONG split;
    LONG rc;
    int done = 0;
    int result = 20;

    if (!opened_libraries()) {
        close_libraries();
        return 20;
    }

    /*
     * 1. Say what this program is: a client that owns the layout of
     *    the groups it makes. The commodity, which owns the drag
     *    engine, keeps running beside it, and neither can switch the
     *    other off.
     */
    rc = ES(ESnap_RegisterClient)("sidebyside", ES_CL_LAYOUT, &client);
    if (rc != ES_OK) {
        printf("sidebyside: RegisterClient: %s\n", errname(rc));
        goto out;
    }

    /*
     * 2. Where the windows may go: the work area of the default public
     *    screen, its usable part being the screen less the title bar,
     *    the docks and the user's margins.
     */
    scr = LockPubScreen(NULL);
    if (scr == NULL) {
        printf("sidebyside: no public screen\n");
        goto out;
    }
    rc = ES(ESnap_QueryWorkAreas)(scr, &wa, 1, &needed);
    if (rc != ES_OK || needed < 1) {
        printf("sidebyside: QueryWorkAreas: %s\n", errname(rc));
        goto out;
    }

    left = open_window(scr, "sidebyside: left", TRUE);
    right = open_window(scr, "sidebyside: right", FALSE);
    UnlockPubScreen(NULL, scr);   /* the windows hold the screen now */
    scr = NULL;
    if (left == NULL || right == NULL) {
        printf("sidebyside: the windows would not open\n");
        goto out;
    }

    /*
     * 3. A group: these two windows, in this work area, owned by this
     *    client. The gap goes with the group. ES_GF_LOCKED would keep
     *    the commodity's drag off them as well; this example lets the
     *    user take a window away and lays it out again afterwards.
     */
    rc = ES(ESnap_CreateGroup)(wa.id, "sidebyside", 0, &group);
    if (rc != ES_OK) {
        printf("sidebyside: CreateGroup: %s\n", errname(rc));
        goto out;
    }
    options[0].ti_Tag = ES_GO_Gap;
    options[0].ti_Data = GAP;
    options[1].ti_Tag = TAG_DONE;
    options[1].ti_Data = 0;
    rc = ES(ESnap_SetGroupOptionsA)(group, options);
    if (rc == ES_OK) {
        rc = ES(ESnap_GroupAddWindow)(group, left);
    }
    if (rc == ES_OK) {
        rc = ES(ESnap_GroupAddWindow)(group, right);
    }
    if (rc != ES_OK) {
        printf("sidebyside: setting up the group: %s\n", errname(rc));
        goto out;
    }

    /* 4. The layout: half each. */
    split = wa.area.usable.x + wa.area.usable.w / 2;
    rc = lay_out(left, right, &wa.area.usable, split);
    if (rc != ES_OK) {
        printf("sidebyside: PlaceWindowsA: %s\n", errname(rc));
        goto out;
    }

    /* 5. Until a close gadget or Esc. */
    while (!done) {
        ULONG sigs = (1UL << left->UserPort->mp_SigBit) |
                     (1UL << right->UserPort->mp_SigBit);
        struct Window *w;

        Wait(sigs);
        for (w = left; w != NULL; w = (w == left) ? right : NULL) {
            struct IntuiMessage *msg;

            while ((msg = (struct IntuiMessage *)GetMsg(w->UserPort))
                   != NULL) {
                ULONG cls = msg->Class;
                UWORD code = msg->Code;

                ReplyMsg((struct Message *)msg);
                if (cls == IDCMP_CLOSEWINDOW ||
                    (cls == IDCMP_RAWKEY && code == RAW_ESC)) {
                    done = 1;
                } else if (cls == IDCMP_RAWKEY && code == RAW_LEFT) {
                    move_seam(group, left, right, &wa.area.usable,
                              &split, -STEP);
                } else if (cls == IDCMP_RAWKEY && code == RAW_RIGHT) {
                    move_seam(group, left, right, &wa.area.usable,
                              &split, STEP);
                }
            }
        }
    }
    result = 0;

out:
    /*
     * 6. Letting go, in the reverse order. The group would die with
     *    the registration and the registration with the task, but a
     *    program says goodbye: the group first, while its windows are
     *    still open, then the role, then the windows.
     */
    if (group != 0) {
        ES(ESnap_DeleteGroup)(group);
    }
    if (client != 0) {
        ES(ESnap_UnregisterClient)(client);
    }
    if (right != NULL) {
        CloseWindow(right);
    }
    if (left != NULL) {
        CloseWindow(left);
    }
    if (scr != NULL) {
        UnlockPubScreen(NULL, scr);
    }
    close_libraries();
    return result;
}
