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
 *
 * From 2.24 it also opens windows of its own, to lay them out as a
 * tiler would and try the seams between them, and to snap one that a
 * hand then drags away.
 */

#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif

#include <stdio.h>

#include <exec/types.h>
#include <exec/tasks.h>
#include <graphics/clip.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <dos/dos.h>
#include <dos/dostags.h>

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

/* --------------------------------------------- 2.24: divider tokens */

/*
 * The seams of a tiler's own group, tested on windows of our own: two,
 * then three, in a group of ours with a gap of 20 between them. Two
 * windows 12 pixels apart or less touch as far as a seam without a gap
 * is concerned, and made one before 2.24 already: at 20, only the
 * group's gap can make this one.
 *
 * Every check prints one line, what was asked, what came back and what
 * was expected, ended by "pass" or "FAIL", and the block ends with how
 * many there were.
 */
#define TEST_GAP 20
#define GAP_A (TEST_GAP - TEST_GAP / 2)   /* of it left of (above) a seam */
#define GAP_B (TEST_GAP / 2)              /* and right of (below) it */

/* A pointer in a tag: ti_Data is an IPTR on AROS, 64 bits wide on
 * x86_64 and aarch64, and a ULONG on the other two. */
#if defined(__AROS__)
#define TAG_PTR(p) ((IPTR)(p))
#else
#define TAG_PTR(p) ((ULONG)(p))
#endif

static int g_pass, g_fail;

static int verdict(int ok)
{
    if (ok) {
        g_pass++;
        printf(": pass\n");
    } else {
        g_fail++;
        printf(": FAIL\n");
    }
    return ok;
}

static void summary(const char *what)
{
    if (g_fail == 0) {
        printf("esnaptest: %s: all %d checks pass\n", what, g_pass);
    } else {
        printf("esnaptest: %s: %d of %d checks FAIL\n", what, g_fail,
               g_pass + g_fail);
    }
}

/*
 * What the checks lay out: A left of a vertical seam, B right of it,
 * and later C below B, in a group of ours, on the default public
 * screen. u is the usable area of that screen; c is the boundary of
 * the vertical seam, hy of the horizontal one between B and C. bg is a
 * window of ours in no group, behind the others, across the gap where
 * the checks ask about it: whatever else lies there, a window does.
 */
struct Rig {
    ULONG me, grp;
    struct Window *w[3];
    struct Window *bg;
    ULONG serial[3];
    struct ESnapRect u;
    LONG c, hy;
};

/*
 * A window the library will place: it wants a size gadget, and neither
 * a backdrop nor a borderless one. Intuition reads a size limit of 0 as
 * the size the window opened with, so the limits are given.
 */
static struct Window *open_test_window(const char *title)
{
    struct Screen *scr = LockPubScreen(NULL);
    struct Window *w = NULL;

    if (scr != NULL) {
        w = OpenWindowTags(NULL, WA_PubScreen, scr, WA_Title, title,
                           WA_Left, 40, WA_Top, 40, WA_Width, 300,
                           WA_Height, 200, WA_MinWidth, 60,
                           WA_MinHeight, 40, WA_MaxWidth, ~0UL,
                           WA_MaxHeight, ~0UL, WA_DragBar, TRUE,
                           WA_DepthGadget, TRUE, WA_SizeGadget, TRUE,
                           WA_SmartRefresh, TRUE, WA_NoCareRefresh, TRUE,
                           WA_IDCMP, 0UL, WA_Activate, FALSE, TAG_DONE);
        UnlockPubScreen(NULL, scr);
    }
    return w;
}

/* Where w is, the four values read together. */
static void box_of(struct Window *w, struct ESnapRect *r)
{
    ULONG ilock = LockIBase(0);

    r->x = w->LeftEdge;
    r->y = w->TopEdge;
    r->w = w->Width;
    r->h = w->Height;
    UnlockIBase(ilock);
}

static int same_box(const struct ESnapRect *a, const struct ESnapRect *b)
{
    return a->x == b->x && a->y == b->y && a->w == b->w && a->h == b->h;
}

/*
 * ChangeWindowBox() only asks: Intuition installs the box later, and on
 * AROS the library asks in two steps. So wait, ten seconds at most,
 * for the n windows to stand in the boxes wanted; 1 when they all do.
 */
static int wait_boxes(struct Window *const *w, const struct ESnapRect *want,
                      int n)
{
    struct ESnapRect r;
    int i, t, ok = 0;

    for (t = 0; t < 100; t++) {
        ok = 1;
        for (i = 0; i < n && ok; i++) {
            box_of(w[i], &r);
            ok = same_box(&r, &want[i]);
        }
        if (ok) {
            break;
        }
        Delay(5L);
    }
    return ok;
}

/* Is w on the front screen, the only one the divider calls of 2.2 and
 * 2.3 look at from 2.24 on? */
static int front_screen(struct Window *w)
{
    ULONG ilock = LockIBase(0);
    int front = ((struct IntuitionBase *)IntuitionBase)->FirstScreen ==
                w->WScreen;

    UnlockIBase(ilock);
    return front;
}

/* A plain window at box: no size gadget, so nothing the library would
 * place, and in no group. */
static struct Window *open_plain_window(const char *title,
                                        const struct ESnapRect *box)
{
    struct Screen *scr = LockPubScreen(NULL);
    struct Window *w = NULL;

    if (scr != NULL) {
        w = OpenWindowTags(NULL, WA_PubScreen, scr, WA_Title, title,
                           WA_Left, box->x, WA_Top, box->y,
                           WA_Width, box->w, WA_Height, box->h,
                           WA_DragBar, TRUE, WA_DepthGadget, TRUE,
                           WA_SmartRefresh, TRUE, WA_NoCareRefresh, TRUE,
                           WA_IDCMP, 0UL, WA_Activate, FALSE, TAG_DONE);
        UnlockPubScreen(NULL, scr);
    }
    return w;
}

/* Does front's layer lie in front of back's? */
static int in_front_of(struct Window *front, struct Window *back)
{
    struct Layer *l;
    ULONG ilock = LockIBase(0);
    int yes = 0;

    for (l = back->WLayer != NULL ? back->WLayer->front : NULL; l != NULL;
         l = l->front) {
        if (l == front->WLayer) {
            yes = 1;
            break;
        }
    }
    UnlockIBase(ilock);
    return yes;
}

/* Depth changes are asked for as well: wait, ten seconds at most, for
 * bg to lie in front of A and B (to_front) or behind them; 1 once it
 * does. */
static int wait_depth(const struct Rig *r, int to_front)
{
    int i, t, ok = 0;

    for (t = 0; t < 100; t++) {
        ok = 1;
        for (i = 0; i < 2 && ok; i++) {
            ok = to_front ? in_front_of(r->bg, r->w[i])
                          : in_front_of(r->w[i], r->bg);
        }
        if (ok) {
            break;
        }
        Delay(5L);
    }
    return ok;
}

/*
 * The cells of the layout with the vertical seam at p and, with three
 * windows, the horizontal one at q: A takes the area left of p, B the
 * rest, or the part of it above q with C below.
 */
static void rig_cells(const struct Rig *r, LONG p, LONG q, int n,
                      struct ESnapRect *cell)
{
    LONG right = r->u.x + r->u.w, bottom = r->u.y + r->u.h;

    cell[0].x = r->u.x;
    cell[0].y = r->u.y;
    cell[0].w = p - r->u.x;
    cell[0].h = r->u.h;
    cell[1].x = p;
    cell[1].y = r->u.y;
    cell[1].w = right - p;
    cell[1].h = (n > 2 ? q : bottom) - r->u.y;
    if (n > 2) {
        cell[2].x = p;
        cell[2].y = q;
        cell[2].w = right - p;
        cell[2].h = bottom - q;
    }
}

/* And the boxes the windows must stand in: each cell less its part of
 * the gap on the sides it shares, nothing on the area's edges. */
static void rig_boxes(const struct Rig *r, LONG p, LONG q, int n,
                      struct ESnapRect *box)
{
    rig_cells(r, p, q, n, box);
    box[0].w -= GAP_A;
    box[1].x += GAP_B;
    box[1].w -= GAP_B;
    if (n > 2) {
        box[1].h -= GAP_A;
        box[2].x += GAP_B;
        box[2].w -= GAP_B;
        box[2].y += GAP_B;
        box[2].h -= GAP_B;
    }
}

/* The n windows into the cells of seams p and q, the way a tiler that
 * moves its windows often places them; ES_OK when the call and every
 * entry are. */
static LONG rig_place(struct Rig *r, LONG p, LONG q, int n)
{
    struct ESnapPlacement pl[3];
    struct ESnapRect cell[3];
    LONG rc;
    int i;

    rig_cells(r, p, q, n, cell);
    for (i = 0; i < n; i++) {
        pl[i].window = r->w[i];
        pl[i].rect = cell[i];
        pl[i].result = 0;
    }
    rc = ES_CALL(ESnap_PlaceWindowsA)(pl, (ULONG)n,
                                      ES_PF_CELL | ES_PF_NO_RESTORE);
    for (i = 0; i < n && rc == ES_OK; i++) {
        rc = pl[i].result;
    }
    return rc;
}

/*
 * Wait for the n windows to stand where seams p and q put them, print
 * each against that, and the gap, as one check named what.
 */
static int rig_check(const struct Rig *r, LONG p, LONG q, int n,
                     const char *what)
{
    struct ESnapRect want[3], got[3];
    int i, ok;

    rig_boxes(r, p, q, n, want);
    ok = wait_boxes(r->w, want, n);
    for (i = 0; i < n; i++) {
        box_of(r->w[i], &got[i]);
        printf("esnaptest:   %c %ld,%ld %ldx%ld (expected %ld,%ld %ldx%ld)\n",
               'A' + i, (long)got[i].x, (long)got[i].y, (long)got[i].w,
               (long)got[i].h, (long)want[i].x, (long)want[i].y,
               (long)want[i].w, (long)want[i].h);
    }
    printf("esnaptest: %s, gap %ld", what,
           (long)(got[1].x - (got[0].x + got[0].w)));
    if (n > 2) {
        printf(" and %ld", (long)(got[2].y - (got[1].y + got[1].h)));
    }
    printf(" (expected %d)", TEST_GAP);
    return verdict(ok);
}

/* The gap between A and B as they stand. */
static LONG rig_gap(const struct Rig *r)
{
    struct ESnapRect a, b;

    box_of(r->w[0], &a);
    box_of(r->w[1], &b);
    return b.x - (a.x + a.w);
}

static LONG set_gap(ULONG grp)
{
    struct TagItem go[3];

    go[0].ti_Tag = ES_GO_Gap;
    go[0].ti_Data = TEST_GAP;
    go[1].ti_Tag = ES_GO_Margin;
    go[1].ti_Data = 0;
    go[2].ti_Tag = TAG_DONE;
    go[2].ti_Data = 0;
    return ES_CALL(ESnap_SetGroupOptionsA)(grp, go);
}

/* QueryDividers of the rig's group, four at most. */
static LONG rig_dividers(const struct Rig *r, struct ESnapDividerInfo *dv,
                         ULONG *need)
{
    *need = 0;
    return ES_CALL(ESnap_QueryDividers)(r->grp, dv, 4, need);
}

static void print_dividers(const struct ESnapDividerInfo *dv, ULONG need)
{
    ULONG i;

    for (i = 0; i < need && i < 4; i++) {
        printf("esnaptest:   token %lu, %s at %ld (limits %ld..%ld), "
               "from %ld to %ld, gap %ld, windows %lu|%lu, serials "
               "%lu|%lu, flags %lx\n", (unsigned long)dv[i].token,
               dv[i].vertical ? "vertical" : "horizontal",
               (long)dv[i].position, (long)dv[i].minPosition,
               (long)dv[i].maxPosition, (long)dv[i].from, (long)dv[i].to,
               (long)dv[i].gap, (unsigned long)dv[i].countA,
               (unsigned long)dv[i].countB, (unsigned long)dv[i].serialA,
               (unsigned long)dv[i].serialB, (unsigned long)dv[i].flags);
    }
}

/* Empty also when a second rig shares the first one's client. */
static void rig_init(struct Rig *r)
{
    int i;

    r->me = 0;
    r->grp = 0;
    r->bg = NULL;
    for (i = 0; i < 3; i++) {
        r->w[i] = NULL;
        r->serial[i] = 0;
    }
}

/*
 * A and B in a group of ours (flags), with a gap of TEST_GAP and no
 * margins, in the two halves of the usable area of their screen. Not
 * split in the middle: a user's own left and right halves would have a
 * seam there. 0 when that could not be done; what was made is in *r
 * for rig_down() either way.
 */
static int rig_up(struct Rig *r, ULONG flags)
{
    struct ESnapArea ua;
    LONG rc;
    int i;

    rig_init(r);
    rc = ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_LAYOUT, &r->me);
    printf("esnaptest: ESnap_RegisterClient(layout) -> %s", rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    r->w[0] = open_test_window("esnaptest A");
    r->w[1] = open_test_window("esnaptest B");
    printf("esnaptest: two windows of our own, A and B -> %p %p",
           (void *)r->w[0], (void *)r->w[1]);
    if (!verdict(r->w[0] != NULL && r->w[1] != NULL)) {
        return 0;
    }
    rc = ES_CALL(ESnap_CreateGroup)(0, "seams", flags, &r->grp);
    printf("esnaptest: ESnap_CreateGroup(%s) -> %s, id %lu",
           flags != 0 ? "locked" : "unlocked", rcname(rc),
           (unsigned long)r->grp);
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    rc = set_gap(r->grp);
    printf("esnaptest: ESnap_SetGroupOptionsA(gap %d, margin 0) -> %s",
           TEST_GAP, rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    for (i = 0; i < 2 && rc == ES_OK; i++) {
        rc = ES_CALL(ESnap_GroupAddWindow)(r->grp, r->w[i]);
    }
    printf("esnaptest: ESnap_GroupAddWindow(A, B) -> %s", rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    for (i = 0; i < 2 && rc == ES_OK; i++) {
        rc = ES_CALL(ESnap_QueryWindowSerial)(r->w[i], &r->serial[i]);
    }
    printf("esnaptest: ESnap_QueryWindowSerial(A, B) -> %s, %lu %lu",
           rcname(rc), (unsigned long)r->serial[0],
           (unsigned long)r->serial[1]);
    if (!verdict(rc == ES_OK && r->serial[0] != 0 && r->serial[1] != 0 &&
                 r->serial[0] != r->serial[1])) {
        return 0;
    }
    rc = ES_CALL(ESnap_QueryScreenArea)(r->w[0]->WScreen, &ua);
    if (rc != ES_OK) {
        printf("esnaptest: ESnap_QueryScreenArea -> %s", rcname(rc));
        return verdict(0);
    }
    r->u = ua.usable;
    if (r->u.w < 480 || r->u.h < 240) {
        printf("esnaptest: a usable area of %ldx%ld is too small for these "
               "checks, skipped\n", (long)r->u.w, (long)r->u.h);
        return 0;
    }
    r->c = r->u.x + r->u.w / 2 - 30;
    r->hy = r->u.y + r->u.h / 2;
    {
        struct ESnapRect box;

        box.x = r->c - 100;
        box.y = r->u.y + r->u.h / 4;
        box.w = 200;
        box.h = r->u.h / 2;
        r->bg = open_plain_window("esnaptest behind", &box);
        if (r->bg != NULL) {
            WindowToBack(r->bg);
        }
        printf("esnaptest: a window of our own in no group, behind A and B "
               "across the gap -> %p", (void *)r->bg);
        if (!verdict(r->bg != NULL && wait_depth(r, 0))) {
            return 0;
        }
    }
    rc = rig_place(r, r->c, 0, 2);
    printf("esnaptest: ESnap_PlaceWindowsA(two cells split at %ld, "
           "ES_PF_CELL | ES_PF_NO_RESTORE) -> %s", (long)r->c, rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    rig_check(r, r->c, 0, 2, "placed in the cells less the gap");
    return 1;
}

/* Everything rig_up() and the checks made goes, on every path. The
 * windows first: a member that closes leaves its group by itself. */
static void rig_down(struct Rig *r)
{
    int i;

    for (i = 0; i < 3; i++) {
        if (r->w[i] != NULL) {
            CloseWindow(r->w[i]);
            r->w[i] = NULL;
        }
    }
    if (r->bg != NULL) {
        CloseWindow(r->bg);
        r->bg = NULL;
    }
    if (r->grp != 0) {
        ES_CALL(ESnap_DeleteGroup)(r->grp);
        r->grp = 0;
    }
    if (r->me != 0) {
        ES_CALL(ESnap_UnregisterClient)(r->me);
        r->me = 0;
    }
}

/*
 * The lock rule is about tasks: a group locked by its owner keeps every
 * other task off its seams, the commodity's included. So a second task
 * asks what the commodity asks and what a tiler of its own would. It
 * prints nothing, since the C library belongs to the task that runs
 * main(), and calls nothing but the libraries: it leaves its answers
 * here and signals. It lists the group's seams only when list says so:
 * a list retires a token that names no seam any more, and a check of
 * the owner's after it would then meet a token already gone.
 */
static struct {
    struct Task *parent;
    LONG sig;
    ULONG group, token;
    LONG x, y, to;
    int list;
    LONG at_rc, at_present;
    struct Window *at_a, *at_b;
    LONG move_rc, list_rc;
    ULONG list_need, list_token, list_flags;
} g_child;

static LONG child_main(void)
{
    struct ESnapDivider d;
    struct ESnapDividerInfo dv[2];
    ULONG need = 0;

    d.present = 0;
    d.windowA = NULL;
    d.windowB = NULL;
    g_child.at_rc = ES_CALL(ESnap_QueryDividerAt)(10, g_child.x, g_child.y,
                                                  &d);
    g_child.at_present = d.present;
    g_child.at_a = d.windowA;
    g_child.at_b = d.windowB;
    g_child.move_rc = ES_CALL(ESnap_MoveDividerT)(g_child.token, g_child.to,
                                                  0, NULL);
    if (g_child.list) {
        dv[0].token = 0;
        dv[0].flags = 0;
        g_child.list_rc = ES_CALL(ESnap_QueryDividers)(g_child.group, dv, 2,
                                                       &need);
        g_child.list_need = need;
        g_child.list_token = dv[0].token;
        g_child.list_flags = dv[0].flags;
    }
    /* the parent goes on once it sees the signal, and in the end
     * unloads this code: under Forbid() it cannot run before this
     * process has returned out of it, which ends the process */
    Forbid();
    Signal(g_child.parent, 1UL << g_child.sig);
    return RETURN_OK;
}

/*
 * child_main() as a process of its own: -1 when it could not be
 * started, 1 when it answered within twenty seconds. One that did not
 * is waited for all the same, since its code goes when this program
 * ends: 2 when it answers in the end, 0 when the wait was broken with
 * ctrl-c, which leaves it to run on in code that may be gone by then.
 */
static int run_child(void)
{
    struct TagItem tags[6];
    struct Process *proc;
    BYTE sig;
    ULONG mask;
    int t, answered = 0;

    sig = AllocSignal(-1);
    if (sig == -1) {
        return -1;
    }
    mask = 1UL << sig;
    SetSignal(0L, mask);
    g_child.parent = FindTask(NULL);
    g_child.sig = sig;
    tags[0].ti_Tag = NP_Entry;
    tags[0].ti_Data = TAG_PTR(child_main);
    tags[1].ti_Tag = NP_Name;
    tags[1].ti_Data = TAG_PTR("esnaptest second task");
    tags[2].ti_Tag = NP_StackSize;
    tags[2].ti_Data = 32768;
#if defined(__MORPHOS__)
    /* native PowerPC code: MorphOS would run it as 68k code otherwise */
    tags[3].ti_Tag = NP_CodeType;
    tags[3].ti_Data = CODETYPE_PPC;
    tags[4].ti_Tag = NP_PPCStackSize;
    tags[4].ti_Data = 32768;
    tags[5].ti_Tag = TAG_DONE;
    tags[5].ti_Data = 0;
#else
    tags[3].ti_Tag = TAG_DONE;
    tags[3].ti_Data = 0;
#endif
    proc = CreateNewProc(tags);
    for (t = 0; proc != NULL && t < 200; t++) {
        if ((SetSignal(0L, 0L) & mask) != 0) {
            answered = 1;
            break;
        }
        Delay(5L);
    }
    if (proc != NULL && !answered) {
        printf("esnaptest: the second task has not answered in twenty "
               "seconds; waiting for it, since its code goes when this "
               "program ends (ctrl-c gives up, at that risk)\n");
        fflush(stdout);
        if ((Wait(mask | SIGBREAKF_CTRL_C) & mask) != 0) {
            answered = 2;
        }
    }
    SetSignal(0L, mask);
    /* a child that did not answer may signal yet: the bit stays taken */
    if (proc == NULL || answered) {
        FreeSignal(sig);
    }
    return proc == NULL ? -1 : answered;
}

/*
 * The second task asks what the commodity asks in the middle of the
 * gap, and to move the seam of tok to `to`, then, when list is not 0,
 * what the group's seams are; the line that says how that went is a
 * check of its own. 1 when its answers are there to look at, 0 when
 * they are not, -1 when it never started (and so left everything as
 * it was).
 */
static int child_ask(const struct Rig *r, ULONG tok, LONG to, int list)
{
    int started;

    g_child.group = r->grp;
    g_child.token = tok;
    g_child.x = r->c;
    g_child.y = r->hy;
    g_child.to = to;
    g_child.list = list;
    g_child.at_rc = g_child.move_rc = g_child.list_rc = 12345;
    g_child.at_present = 0;
    g_child.at_a = g_child.at_b = NULL;
    g_child.list_need = g_child.list_token = g_child.list_flags = 0;
    started = run_child();
    printf("esnaptest: a second task (CreateNewProc) -> %s",
           started < 0 ? "NOT STARTED" :
           started == 0 ? "NO ANSWER" :
           started == 1 ? "answered" : "answered, but LATE");
    verdict(started == 1);
    return started > 0 ? 1 : started;
}

/* One seam between A and B, at the boundary of the cells, with the gap,
 * named by the windows' serials. *tok receives its token. */
static int dt_read(struct Rig *r, ULONG *tok)
{
    struct ESnapDividerInfo dv[4];
    ULONG need = 0, ser[4];
    LONG rc;

    rc = ES_CALL(ESnap_QueryDividers)(r->grp, NULL, 0, &need);
    printf("esnaptest: ESnap_QueryDividers(count 0) -> %s, %lu seam(s) "
           "(expected ES_OK, 1)", rcname(rc), (unsigned long)need);
    verdict(rc == ES_OK && need == 1);
    rc = rig_dividers(r, dv, &need);
    print_dividers(dv, need);
    printf("esnaptest: ESnap_QueryDividers -> %s, %lu seam(s) (expected "
           "ES_OK, 1)", rcname(rc), (unsigned long)need);
    if (!verdict(rc == ES_OK && need == 1 && dv[0].token != 0)) {
        return 0;
    }
    *tok = dv[0].token;
    printf("esnaptest: the seam is %s at %ld, gap %ld, windows %lu|%lu, "
           "flags %lx (expected vertical at %ld, gap %d, 1|1, flags 0)",
           dv[0].vertical ? "vertical" : "horizontal", (long)dv[0].position,
           (long)dv[0].gap, (unsigned long)dv[0].countA,
           (unsigned long)dv[0].countB, (unsigned long)dv[0].flags,
           (long)r->c, TEST_GAP);
    verdict(dv[0].vertical == 1 && dv[0].position == r->c &&
            dv[0].gap == TEST_GAP && dv[0].countA == 1 &&
            dv[0].countB == 1 && dv[0].flags == 0);
    printf("esnaptest: serialA %lu, serialB %lu (expected A's and B's, %lu "
           "and %lu)", (unsigned long)dv[0].serialA,
           (unsigned long)dv[0].serialB, (unsigned long)r->serial[0],
           (unsigned long)r->serial[1]);
    verdict(dv[0].serialA == r->serial[0] && dv[0].serialB == r->serial[1]);
    printf("esnaptest: from %ld to %ld, limits %ld..%ld (expected %ld to "
           "%ld, limits around %ld, reserved 0)", (long)dv[0].from,
           (long)dv[0].to, (long)dv[0].minPosition, (long)dv[0].maxPosition,
           (long)r->u.y, (long)(r->u.y + r->u.h), (long)r->c);
    verdict(dv[0].from == r->u.y && dv[0].to == r->u.y + r->u.h &&
            dv[0].minPosition < r->c && r->c < dv[0].maxPosition &&
            dv[0].reserved[0] == 0 && dv[0].reserved[1] == 0 &&
            dv[0].reserved[2] == 0);
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: asked again -> %s, token %lu (expected the same, "
           "%lu)", rcname(rc), (unsigned long)dv[0].token,
           (unsigned long)*tok);
    verdict(rc == ES_OK && need == 1 && dv[0].token == *tok);
    rc = ES_CALL(ESnap_QueryDividerWindows)(*tok, NULL, 0, &need);
    printf("esnaptest: ESnap_QueryDividerWindows(count 0) -> %s, %lu "
           "(expected ES_OK, 2)", rcname(rc), (unsigned long)need);
    verdict(rc == ES_OK && need == 2);
    ser[0] = ser[1] = 0;
    rc = ES_CALL(ESnap_QueryDividerWindows)(*tok, ser, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows -> %s, %lu %lu (expected "
           "ES_OK, serialA %lu first, serialB %lu at countA)", rcname(rc),
           (unsigned long)ser[0], (unsigned long)ser[1],
           (unsigned long)r->serial[0], (unsigned long)r->serial[1]);
    verdict(rc == ES_OK && need == 2 && ser[0] == r->serial[0] &&
            ser[1] == r->serial[1]);
    return 1;
}

/* Moved by its token: the gap kept, the token kept, and no registry
 * slot spent on windows placed without one. */
static int dt_move(struct Rig *r, ULONG tok)
{
    struct ESnapDividerInfo dv[4];
    ULONG need = 0, za = 99, zb = 99;
    LONG rc, rc2, applied = 0;

    rc = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 100, 0, &applied);
    printf("esnaptest: ESnap_MoveDividerT(+100) -> %s, applied %ld "
           "(expected ES_OK, %ld)", rcname(rc), (long)applied,
           (long)(r->c + 100));
    if (!verdict(rc == ES_OK && applied == r->c + 100)) {
        return 0;
    }
    rig_check(r, r->c + 100, 0, 2, "moved by its token");
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: ESnap_QueryDividers -> %s, token %lu at %ld, flags "
           "%lx (expected the same token at %ld, flags 0)", rcname(rc),
           (unsigned long)dv[0].token, (long)dv[0].position,
           (unsigned long)dv[0].flags, (long)(r->c + 100));
    verdict(rc == ES_OK && need == 1 && dv[0].token == tok &&
            dv[0].position == r->c + 100 && dv[0].flags == 0);
    rc = ES_CALL(ESnap_QueryWindow)(r->w[0], &za);
    rc2 = ES_CALL(ESnap_QueryWindow)(r->w[1], &zb);
    printf("esnaptest: ESnap_QueryWindow(A, B) -> %s %s, zones %lu %lu "
           "(expected %d: no registry slot spent)", rcname(rc), rcname(rc2),
           (unsigned long)za, (unsigned long)zb, ES_ZONE_NONE);
    verdict(rc == ES_OK && rc2 == ES_OK && za == ES_ZONE_NONE &&
            zb == ES_ZONE_NONE);
    return 1;
}

/* Bad arguments for the three calls, none of which may touch the
 * token. */
static void dt_bad_args(struct Rig *r, ULONG tok)
{
    struct ESnapDividerInfo dv[4];
    ULONG need, ser[4];
    LONG rc;

    rc = ES_CALL(ESnap_QueryDividers)(r->grp, NULL, 0, NULL);
    printf("esnaptest: ESnap_QueryDividers(needed NULL) -> %s (expected "
           "ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    need = 12345;
    rc = ES_CALL(ESnap_QueryDividers)(r->grp, NULL, 4, &need);
    printf("esnaptest: ESnap_QueryDividers(buf NULL, count 4) -> %s, needed "
           "%lu (expected ES_ERR_BAD_ARGS, untouched)", rcname(rc),
           (unsigned long)need);
    verdict(rc == ES_ERR_BAD_ARGS && need == 12345);
    rc = ES_CALL(ESnap_QueryDividers)(r->grp + 1000, dv, 4, &need);
    printf("esnaptest: ESnap_QueryDividers(a group that is not there) -> "
           "%s (expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
    rc = ES_CALL(ESnap_MoveDividerT)(0, r->c, 0, NULL);
    printf("esnaptest: ESnap_MoveDividerT(token 0) -> %s (expected "
           "ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    rc = ES_CALL(ESnap_MoveDividerT)(tok, r->c, ES_PF_CELL, NULL);
    printf("esnaptest: ESnap_MoveDividerT(flag ES_PF_CELL) -> %s (expected "
           "ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    rc = ES_CALL(ESnap_MoveDividerT)(tok + 1000000UL, r->c, 0, NULL);
    printf("esnaptest: ESnap_MoveDividerT(a token nobody was given) -> %s "
           "(expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
    rc = ES_CALL(ESnap_QueryDividerWindows)(tok, ser, 4, NULL);
    printf("esnaptest: ESnap_QueryDividerWindows(needed NULL) -> %s "
           "(expected ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    rc = ES_CALL(ESnap_QueryDividerWindows)(tok, NULL, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows(serials NULL, count 4) -> "
           "%s (expected ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    rc = ES_CALL(ESnap_QueryDividerWindows)(0, ser, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows(token 0) -> %s (expected "
           "ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    rc = ES_CALL(ESnap_QueryDividerWindows)(tok + 1000000UL, ser, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows(a token nobody was given) "
           "-> %s (expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: ESnap_QueryDividers after them -> %s, token %lu "
           "(expected the same, %lu)", rcname(rc), (unsigned long)dv[0].token,
           (unsigned long)tok);
    verdict(rc == ES_OK && need == 1 && dv[0].token == tok);
}

/*
 * A window EdgeSnap asked to move in the last two seconds is never
 * moved by hand, whatever box it shows on its way: B taken somewhere
 * else right after a move by token, as a box queued before the last
 * one or the box between AROS's two steps would show it, is still on
 * its way, the seam is not flagged and the next move is taken. Three
 * moves in a row are all taken too. A window moved by hand once that
 * time is over is: the seam is flagged and MoveDividerT refuses it,
 * until a placement puts the window back.
 */
static int dt_transit(struct Rig *r, ULONG tok)
{
    struct ESnapDividerInfo dv[4];
    struct ESnapRect want[2], hand, a;
    ULONG need = 0, t0 = 0, t1 = 0, us = 0;
    LONG rc1, rc2, rc3, rc, ap1 = 0, ap2 = 0, ap3 = 0;
    int landed;

    /* B far from both boxes EdgeSnap knows for it, the one it is to
     * take and the one it was in, at once; the clock read around it */
    rig_boxes(r, r->c + 40, 0, 2, want);
    hand = want[1];
    hand.y += 60;
    hand.h -= 60;
    CurrentTime(&t0, &us);
    rc1 = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 40, 0, &ap1);
    ChangeWindowBox(r->w[1], hand.x, hand.y, hand.w, hand.h);
    landed = wait_boxes(&r->w[1], &hand, 1);
    rc = rig_dividers(r, dv, &need);
    rc2 = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 80, 0, &ap2);
    CurrentTime(&t1, &us);
    printf("esnaptest: ESnap_MoveDividerT(+40), then B 60 pixels down by "
           "hand at once -> %s, applied %ld, B %s (expected ES_OK, %ld, "
           "there)", rcname(rc1), (long)ap1, landed ? "there" : "NOT THERE",
           (long)(r->c + 40));
    if (!verdict(rc1 == ES_OK && ap1 == r->c + 40 && landed)) {
        return 0;
    }
    if (t1 - t0 > 1) {
        /* the library's clock may have left the two seconds behind */
        printf("esnaptest: that took %lu seconds, too long to tell a "
               "window on its way: not checked\n", (unsigned long)(t1 - t0));
    } else {
        printf("esnaptest: ESnap_QueryDividers at once -> %s, token %lu at "
               "%ld, flags %lx (expected the same token at %ld, flags 0: a "
               "window on its way is not moved by hand)", rcname(rc),
               (unsigned long)dv[0].token, (long)dv[0].position,
               (unsigned long)dv[0].flags, (long)(r->c + 40));
        verdict(rc == ES_OK && need == 1 && dv[0].token == tok &&
                dv[0].position == r->c + 40 && dv[0].flags == 0);
        printf("esnaptest: ESnap_MoveDividerT(+80) -> %s, applied %ld "
               "(expected ES_OK, %ld)", rcname(rc2), (long)ap2,
               (long)(r->c + 80));
        verdict(rc2 == ES_OK && ap2 == r->c + 80);
    }
    if (rc2 != ES_OK) {
        rc = rig_place(r, r->c + 80, 0, 2);
        printf("esnaptest: ESnap_PlaceWindowsA, where that move would have "
               "put them -> %s", rcname(rc));
        if (!verdict(rc == ES_OK)) {
            return 0;
        }
    }
    rig_check(r, r->c + 80, 0, 2, "where the move after the hand put them");

    rc1 = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 40, 0, &ap1);
    rc2 = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 80, ES_PF_KEEP_ZORDER,
                                      &ap2);
    rc3 = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 120, 0, &ap3);
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: three ESnap_MoveDividerT in a row (+40, +80 keeping "
           "the order, +120) -> %s %s %s, applied %ld %ld %ld (expected "
           "ES_OK and each)", rcname(rc1), rcname(rc2), rcname(rc3),
           (long)ap1, (long)ap2, (long)ap3);
    verdict(rc1 == ES_OK && rc2 == ES_OK && rc3 == ES_OK &&
            ap1 == r->c + 40 && ap2 == r->c + 80 && ap3 == r->c + 120);
    printf("esnaptest: ESnap_QueryDividers at once -> %s, token %lu at %ld, "
           "flags %lx (expected the same token at %ld, flags 0)", rcname(rc),
           (unsigned long)dv[0].token, (long)dv[0].position,
           (unsigned long)dv[0].flags, (long)(r->c + 120));
    verdict(rc == ES_OK && need == 1 && dv[0].token == tok &&
            dv[0].position == r->c + 120 && dv[0].flags == 0);
    rig_check(r, r->c + 120, 0, 2, "where the last of the three put them");

    /* a hand takes B 60 pixels down, and the library is asked once the
     * two seconds of the last request are well over */
    rig_boxes(r, r->c + 120, 0, 2, want);
    hand = want[1];
    hand.y += 60;
    hand.h -= 60;
    ChangeWindowBox(r->w[1], hand.x, hand.y, hand.w, hand.h);
    Delay(175L);
    printf("esnaptest: B taken by hand (ChangeWindowBox, 60 pixels down), "
           "three and a half seconds on");
    if (!verdict(wait_boxes(&r->w[1], &hand, 1))) {
        return 0;
    }
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: ESnap_QueryDividers -> %s, token %lu, flags %lx "
           "(expected the same token, with ES_DIF_MOVED)", rcname(rc),
           (unsigned long)dv[0].token, (unsigned long)dv[0].flags);
    verdict(rc == ES_OK && need == 1 && dv[0].token == tok &&
            (dv[0].flags & ES_DIF_MOVED) != 0);
    rc = ES_CALL(ESnap_MoveDividerT)(tok, r->c, 0, NULL);
    Delay(25L);
    box_of(r->w[0], &a);
    printf("esnaptest: ESnap_MoveDividerT -> %s, A %ld,%ld %ldx%ld "
           "(expected ES_ERR_CHANGED, A where it was)", rcname(rc),
           (long)a.x, (long)a.y, (long)a.w, (long)a.h);
    verdict(rc == ES_ERR_CHANGED && same_box(&a, &want[0]));

    rc = rig_place(r, r->c, 0, 2);
    printf("esnaptest: ESnap_PlaceWindowsA, back into the cells -> %s",
           rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    rig_check(r, r->c, 0, 2, "back in the cells");
    Delay(175L);                    /* past the transit: where they are */
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: ESnap_QueryDividers three and a half seconds later "
           "-> %s, token %lu at %ld, flags %lx (expected the same token at "
           "%ld, flags 0)", rcname(rc), (unsigned long)dv[0].token,
           (long)dv[0].position, (unsigned long)dv[0].flags, (long)r->c);
    verdict(rc == ES_OK && need == 1 && dv[0].token == tok &&
            dv[0].position == r->c && dv[0].flags == 0);
    return 1;
}

/*
 * What the commodity does, from the owner, who may move a seam of its
 * own locked group: QueryDividerAt in the gap, MoveDividerAt on its
 * line. Both look at the front screen only. The point in the gap has
 * our window behind it, which covers the seam there only once it is
 * brought in front of A and B. Then a move by token far past the
 * limit, which clamps.
 */
static int dt_commodity(struct Rig *r, ULONG tok)
{
    struct ESnapDividerInfo dv[4];
    struct ESnapDivider d, q;
    ULONG need = 0;
    LONG rc, at = r->c, min, applied = 0;
    int ours;

    if (!front_screen(r->w[0])) {
        printf("esnaptest: the windows' screen is not the front one: "
               "ESnap_QueryDividerAt and ESnap_MoveDividerAt skipped\n");
    } else {
        d.present = 0;
        d.windowA = NULL;
        d.windowB = NULL;
        rc = ES_CALL(ESnap_QueryDividerAt)(10, r->c, r->hy, &d);
        printf("esnaptest: ESnap_QueryDividerAt(in the gap, our window "
               "behind it) -> %s, present %ld at %ld, windows %p %p "
               "(expected present 1 at %ld, A and B)", rcname(rc),
               (long)d.present, (long)d.position, (void *)d.windowA,
               (void *)d.windowB, (long)r->c);
        ours = verdict(rc == ES_OK && d.present && d.vertical &&
                       d.position == r->c && d.windowA == r->w[0] &&
                       d.windowB == r->w[1]);
        WindowToFront(r->bg);
        if (!wait_depth(r, 1)) {
            printf("esnaptest: our window brought in front of A and B -> "
                   "NOT THERE");
            verdict(0);
        } else {
            q.present = 1;
            rc = ES_CALL(ESnap_QueryDividerAt)(10, r->c, r->hy, &q);
            printf("esnaptest: our window in front of A and B, "
                   "ESnap_QueryDividerAt(in the gap) -> %s, present %ld "
                   "(expected ES_OK, present 0: covered)", rcname(rc),
                   (long)q.present);
            verdict(rc == ES_OK && !q.present);
        }
        WindowToBack(r->bg);
        printf("esnaptest: and behind them again");
        if (!verdict(wait_depth(r, 0))) {
            return 0;
        }
        if (ours) {
            rc = ES_CALL(ESnap_MoveDividerAt)(1, d.position, r->c + 50);
            printf("esnaptest: ESnap_MoveDividerAt(+50) from the owner -> %s "
                   "(expected ES_OK)", rcname(rc));
            if (verdict(rc == ES_OK)) {
                at = r->c + 50;
                rig_check(r, at, 0, 2, "moved by its line");
            }
        } else {
            /* it would move whatever seam it found instead */
            printf("esnaptest: ESnap_MoveDividerAt skipped\n");
        }
    }
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: ESnap_QueryDividers -> %s, token %lu at %ld "
           "(expected the same token at %ld)", rcname(rc),
           (unsigned long)dv[0].token, (long)dv[0].position, (long)at);
    if (!verdict(rc == ES_OK && need == 1 && dv[0].token == tok &&
                 dv[0].position == at)) {
        return 0;
    }
    min = dv[0].minPosition;
    rc = ES_CALL(ESnap_MoveDividerT)(tok, -30000, 0, &applied);
    printf("esnaptest: ESnap_MoveDividerT(-30000) -> %s, applied %ld "
           "(expected ES_OK, the limit %ld)", rcname(rc), (long)applied,
           (long)min);
    if (verdict(rc == ES_OK && applied == min)) {
        rig_check(r, applied, 0, 2, "at the limit");
    }
    rc = ES_CALL(ESnap_MoveDividerT)(tok, r->c, 0, &applied);
    printf("esnaptest: ESnap_MoveDividerT back -> %s, applied %ld (expected "
           "ES_OK, %ld)", rcname(rc), (long)applied, (long)r->c);
    if (!verdict(rc == ES_OK && applied == r->c)) {
        return 0;
    }
    rig_check(r, r->c, 0, 2, "back at the boundary");
    return 1;
}

/* A second task meets the lock: no seam to see, none to move, and the
 * owner's token still the owner's. That the lock is looked at before
 * the token's windows is checked in dt_third, where they would answer
 * otherwise. */
static int dt_lock(struct Rig *r, ULONG tok)
{
    ULONG need = 0, ser[4];
    LONG rc;
    int started, ours, front = front_screen(r->w[0]);

    started = child_ask(r, tok, r->c + 100, 1);
    if (started <= 0) {
        /* one that never started leaves the layout as it was; one that
         * did not answer may be inside the library still */
        return started < 0;
    }
    if (front) {
        ours = g_child.at_present &&
               (g_child.at_a == r->w[0] || g_child.at_b == r->w[1]);
        printf("esnaptest: its ESnap_QueryDividerAt(in the gap) -> %s, "
               "present %ld%s (expected no seam of the locked group)",
               rcname(g_child.at_rc), (long)g_child.at_present,
               !g_child.at_present ? "" :
               ours ? ", OURS" : ", another seam");
        verdict(g_child.at_rc == ES_OK && !ours);
    }
    printf("esnaptest: its ESnap_MoveDividerT -> %s (expected "
           "ES_ERR_REJECTED)", rcname(g_child.move_rc));
    verdict(g_child.move_rc == ES_ERR_REJECTED);
    printf("esnaptest: its ESnap_QueryDividers -> %s, %lu seam(s), token "
           "%lu, flags %lx (expected ES_OK, 1, the same token, "
           "ES_DIF_LOCKED)", rcname(g_child.list_rc),
           (unsigned long)g_child.list_need, (unsigned long)g_child.list_token,
           (unsigned long)g_child.list_flags);
    verdict(g_child.list_rc == ES_OK && g_child.list_need == 1 &&
            g_child.list_token == tok &&
            (g_child.list_flags & ES_DIF_LOCKED) != 0);
    Delay(25L);
    rig_check(r, r->c, 0, 2, "untouched by the second task");
    rc = ES_CALL(ESnap_QueryDividerWindows)(tok, ser, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows from the owner -> %s, %lu "
           "(expected ES_OK, 2: the token still holds)", rcname(rc),
           (unsigned long)need);
    verdict(rc == ES_OK && need == 2);
    return 1;
}

/*
 * C joins B's side of the seam, facing A: the token of A and B no
 * longer names a seam and is retired for good, though a task the group
 * is locked for is refused before that is looked at; the new seams, A
 * against B and C, and B against C, get new tokens, and move with the
 * gap. Where the gap between B and C meets the first of them, our
 * window behind covers nothing.
 */
static int dt_third(struct Rig *r, ULONG tok, ULONG *vtok, ULONG *htok)
{
    struct ESnapDividerInfo dv[4];
    struct ESnapDivider d;
    ULONG need = 0, ser[4];
    LONG rc, rc2, ap1 = 0, ap2 = 0;
    int started;

    r->w[2] = open_test_window("esnaptest C");
    rc = r->w[2] != NULL ? ES_CALL(ESnap_GroupAddWindow)(r->grp, r->w[2])
                         : ES_ERR_NO_MEMORY;
    if (rc == ES_OK) {
        rc = ES_CALL(ESnap_QueryWindowSerial)(r->w[2], &r->serial[2]);
    }
    printf("esnaptest: a third window, C, into the group -> %s, serial %lu",
           rcname(rc), (unsigned long)r->serial[2]);
    if (!verdict(rc == ES_OK && r->serial[2] != 0)) {
        return 0;
    }
    rc = rig_place(r, r->c, r->hy, 3);
    printf("esnaptest: ESnap_PlaceWindowsA(A as it was, B above C, split at "
           "%ld) -> %s", (long)r->hy, rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    /* the token of A and B names no seam now: a task the group is
     * locked for is refused before that is even looked at. It lists
     * no seams, so the token is still in the table for the owner's
     * own call to find its windows no seam and retire it */
    started = child_ask(r, tok, r->c + 30, 0);
    if (started == 0) {
        return 0;
    }
    if (started > 0) {
        printf("esnaptest: its ESnap_MoveDividerT on A and B -> %s (expected "
               "ES_ERR_REJECTED: the lock comes first, the windows would "
               "answer ES_ERR_STALE and retire the token)",
               rcname(g_child.move_rc));
        verdict(g_child.move_rc == ES_ERR_REJECTED);
    }
    rc = ES_CALL(ESnap_MoveDividerT)(tok, r->c + 30, 0, NULL);
    printf("esnaptest: ESnap_MoveDividerT on A and B, C facing A now -> %s "
           "(expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
    rig_check(r, r->c, r->hy, 3, "placed in three cells");
    rc = rig_dividers(r, dv, &need);
    print_dividers(dv, need);
    printf("esnaptest: ESnap_QueryDividers -> %s, %lu seam(s) (expected "
           "ES_OK, 2)", rcname(rc), (unsigned long)need);
    if (!verdict(rc == ES_OK && need == 2)) {
        return 0;
    }
    *vtok = dv[0].token;
    *htok = dv[1].token;
    printf("esnaptest: first the vertical one, at %ld, windows %lu|%lu, "
           "serials %lu|%lu, token %lu (expected at %ld, 1|2, A|B, a new "
           "token)", (long)dv[0].position, (unsigned long)dv[0].countA,
           (unsigned long)dv[0].countB, (unsigned long)dv[0].serialA,
           (unsigned long)dv[0].serialB, (unsigned long)dv[0].token,
           (long)r->c);
    verdict(dv[0].vertical == 1 && dv[0].position == r->c &&
            dv[0].countA == 1 && dv[0].countB == 2 &&
            dv[0].serialA == r->serial[0] && dv[0].serialB == r->serial[1] &&
            dv[0].gap == TEST_GAP && dv[0].flags == 0 &&
            dv[0].token != 0 && dv[0].token != tok);
    printf("esnaptest: then the horizontal one, at %ld, windows %lu|%lu, "
           "serials %lu|%lu, token %lu (expected at %ld, 1|1, B|C, a new "
           "token)", (long)dv[1].position, (unsigned long)dv[1].countA,
           (unsigned long)dv[1].countB, (unsigned long)dv[1].serialA,
           (unsigned long)dv[1].serialB, (unsigned long)dv[1].token,
           (long)r->hy);
    verdict(dv[1].vertical == 0 && dv[1].position == r->hy &&
            dv[1].countA == 1 && dv[1].countB == 1 &&
            dv[1].serialA == r->serial[1] && dv[1].serialB == r->serial[2] &&
            dv[1].gap == TEST_GAP && dv[1].flags == 0 &&
            dv[1].token != 0 && dv[1].token != tok &&
            dv[1].token != dv[0].token);
    if (front_screen(r->w[0])) {
        /* level with the gap between B and C, where no window of
         * theirs lies beside the point: that hole is the gap the
         * layout keeps, and our window behind it covers nothing */
        d.present = 0;
        rc = ES_CALL(ESnap_QueryDividerAt)(10, r->c, r->hy, &d);
        printf("esnaptest: ESnap_QueryDividerAt(where the gap between B "
               "and C meets the seam) -> %s, present %ld at %ld (expected "
               "present 1 at %ld: not covered by the window behind)",
               rcname(rc), (long)d.present, (long)d.position, (long)r->c);
        verdict(rc == ES_OK && d.present && d.vertical &&
                d.position == r->c);
    }
    rc = ES_CALL(ESnap_MoveDividerT)(tok, r->c, 0, NULL);
    printf("esnaptest: the old token again -> %s (expected ES_ERR_STALE: "
           "retired for good)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
    ser[0] = ser[1] = ser[2] = 0;
    rc = ES_CALL(ESnap_QueryDividerWindows)(*vtok, ser, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows(the vertical one) -> %s, "
           "%lu: %lu %lu %lu (expected ES_OK, 3: serialA %lu first, serialB "
           "%lu at countA 1, then C's %lu)", rcname(rc), (unsigned long)need,
           (unsigned long)ser[0], (unsigned long)ser[1],
           (unsigned long)ser[2], (unsigned long)dv[0].serialA,
           (unsigned long)dv[0].serialB, (unsigned long)r->serial[2]);
    verdict(rc == ES_OK && need == 3 && dv[0].countA == 1 &&
            ser[0] == dv[0].serialA && ser[1] == dv[0].serialB &&
            ser[2] == r->serial[2]);
    rc = ES_CALL(ESnap_MoveDividerT)(*vtok, r->c + 60, 0, &ap1);
    rc2 = ES_CALL(ESnap_MoveDividerT)(*htok, r->hy + 40, 0, &ap2);
    printf("esnaptest: ESnap_MoveDividerT(vertical +60, horizontal +40) -> "
           "%s %s, applied %ld %ld (expected ES_OK, %ld %ld)", rcname(rc),
           rcname(rc2), (long)ap1, (long)ap2, (long)(r->c + 60),
           (long)(r->hy + 40));
    if (!verdict(rc == ES_OK && rc2 == ES_OK && ap1 == r->c + 60 &&
                 ap2 == r->hy + 40)) {
        return 0;
    }
    rig_check(r, r->c + 60, r->hy + 40, 3, "both seams moved, A against two");
    return 1;
}

/*
 * The group deleted and made again with the same windows: they are the
 * same seams, and get new tokens; the old ones stay stale. Then C
 * closes, and the token of the seam it was on goes with it.
 */
static int dt_again(struct Rig *r, ULONG tok, ULONG vtok, ULONG htok)
{
    struct ESnapDividerInfo dv[4];
    ULONG need = 0, ser[4], v2;
    LONG rc, rc2;
    int i;

    rc = ES_CALL(ESnap_DeleteGroup)(r->grp);
    printf("esnaptest: ESnap_DeleteGroup -> %s", rcname(rc));
    verdict(rc == ES_OK);
    r->grp = 0;
    rc = ES_CALL(ESnap_MoveDividerT)(vtok, r->c, 0, NULL);
    rc2 = ES_CALL(ESnap_QueryDividerWindows)(htok, ser, 4, &need);
    printf("esnaptest: its tokens, by ESnap_MoveDividerT and "
           "ESnap_QueryDividerWindows -> %s %s (expected ES_ERR_STALE)",
           rcname(rc), rcname(rc2));
    verdict(rc == ES_ERR_STALE && rc2 == ES_ERR_STALE);
    rc = ES_CALL(ESnap_CreateGroup)(0, "seams again", ES_GF_LOCKED, &r->grp);
    if (rc == ES_OK) {
        rc = set_gap(r->grp);
    }
    for (i = 0; i < 3 && rc == ES_OK; i++) {
        rc = ES_CALL(ESnap_GroupAddWindow)(r->grp, r->w[i]);
    }
    printf("esnaptest: the group made again, gap %d, with A, B and C -> %s, "
           "id %lu", TEST_GAP, rcname(rc), (unsigned long)r->grp);
    if (!verdict(rc == ES_OK)) {
        return 0;
    }
    rc = rig_dividers(r, dv, &need);
    print_dividers(dv, need);
    printf("esnaptest: ESnap_QueryDividers -> %s, %lu seam(s) (expected "
           "ES_OK, 2 at %ld and %ld, where the windows stand, with new "
           "tokens)", rcname(rc), (unsigned long)need, (long)(r->c + 60),
           (long)(r->hy + 40));
    if (!verdict(rc == ES_OK && need == 2 && dv[0].position == r->c + 60 &&
                 dv[1].position == r->hy + 40 && dv[0].token != 0 &&
                 dv[1].token != 0 && dv[0].token != vtok &&
                 dv[0].token != htok && dv[0].token != tok &&
                 dv[1].token != vtok && dv[1].token != htok &&
                 dv[1].token != tok)) {
        return 0;
    }
    v2 = dv[0].token;
    rc = ES_CALL(ESnap_MoveDividerT)(vtok, r->c, 0, NULL);
    printf("esnaptest: the old group's token on the same seam -> %s "
           "(expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);

    CloseWindow(r->w[2]);
    r->w[2] = NULL;
    rc = ES_CALL(ESnap_MoveDividerT)(v2, r->c, 0, NULL);
    printf("esnaptest: C closed, then ESnap_MoveDividerT on the seam it was "
           "on -> %s (expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: ESnap_QueryDividers -> %s, %lu seam(s), windows "
           "%lu|%lu, token %lu (expected ES_OK, 1, 1|1, a new token)",
           rcname(rc), (unsigned long)need, (unsigned long)dv[0].countA,
           (unsigned long)dv[0].countB, (unsigned long)dv[0].token);
    verdict(rc == ES_OK && need == 1 && dv[0].countA == 1 &&
            dv[0].countB == 1 && dv[0].token != 0 && dv[0].token != v2);
    return 1;
}

/*
 * 2.24: the seams of a tiler's group by token, on windows of our own in
 * a locked group: the lock keeps the commodity off them while the
 * checks run, and is itself one of the things checked.
 */
static void test_dividers(void)
{
    struct Rig r;
    ULONG tok = 0, vtok = 0, htok = 0;
    LONG rc;
    int ok;

    g_pass = 0;
    g_fail = 0;
    ok = rig_up(&r, ES_GF_LOCKED);
    if (ok) {
        ok = dt_read(&r, &tok);
    }
    if (ok) {
        ok = dt_move(&r, tok);
    }
    if (ok) {
        dt_bad_args(&r, tok);
        ok = dt_transit(&r, tok);
    }
    if (ok) {
        ok = dt_commodity(&r, tok);
    }
    if (ok) {
        ok = dt_lock(&r, tok);
    }
    if (ok) {
        ok = dt_third(&r, tok, &vtok, &htok);
    }
    if (ok) {
        ok = dt_again(&r, tok, vtok, htok);
    }
    if (ok) {
        rc = ES_CALL(ESnap_DeleteGroup)(r.grp);
        printf("esnaptest: ESnap_DeleteGroup -> %s", rcname(rc));
        verdict(rc == ES_OK);
        r.grp = 0;
        rc = ES_CALL(ESnap_UnregisterClient)(r.me);
        printf("esnaptest: ESnap_UnregisterClient -> %s", rcname(rc));
        verdict(rc == ES_OK);
        r.me = 0;
    } else if (g_fail > 0) {
        printf("esnaptest: the rest of the 2.24 checks skipped after that\n");
    }
    rig_down(&r);
    summary("2.24 dividers");
}

/*
 * esnaptest S: A and B with a gap of 20 in an unlocked group, for
 * twenty seconds, so that the seam between them can be dragged by hand
 * with the commodity's handle: the gap must be the same afterwards.
 * esnaptest SL locks the group, and the commodity must then leave the
 * seam alone.
 */
static void drag_seam(int locked)
{
    struct Rig r;
    struct ESnapDividerInfo dv[4];
    ULONG need = 0;
    LONG gap0, gap1, at0 = 0, at1 = 0;

    g_pass = 0;
    g_fail = 0;
    if (rig_up(&r, locked ? ES_GF_LOCKED : 0)) {
        if (!front_screen(r.w[0])) {
            printf("esnaptest: the windows' screen is not the front one, "
                   "and the commodity looks for seams there only\n");
        }
        gap0 = rig_gap(&r);
        if (rig_dividers(&r, dv, &need) == ES_OK && need >= 1) {
            at0 = dv[0].position;
        }
        printf("esnaptest: the seam between A and B is at %ld, with a gap of "
               "%ld: drag it now, you have twenty seconds\n", (long)at0,
               (long)gap0);
        Delay(50L * 20);
        gap1 = rig_gap(&r);
        if (rig_dividers(&r, dv, &need) == ES_OK && need >= 1) {
            at1 = dv[0].position;
        }
        if (locked) {
            printf("esnaptest: the locked seam is at %ld, gap %ld (expected "
                   "%ld, %d: left alone)", (long)at1, (long)gap1, (long)at0,
                   TEST_GAP);
            verdict(at1 == at0 && gap1 == TEST_GAP);
        } else {
            printf("esnaptest: the seam is at %ld (%s), gap %ld (expected "
                   "dragged, %d: kept)", (long)at1,
                   at1 != at0 ? "dragged" : "NOT DRAGGED", (long)gap1,
                   TEST_GAP);
            verdict(at1 != at0 && gap1 == TEST_GAP);
        }
    }
    rig_down(&r);
    summary(locked ? "a locked seam by hand" : "a seam by hand");
}

/* How far an application may round a size the library asked for, and
 * a box still count as the one asked for: the library's own slack. */
#define TEST_SLACK 16

static int near_px(LONG a, LONG b)
{
    return a - b <= TEST_SLACK && b - a <= TEST_SLACK;
}

/* --------------------------------------------- 2.25: regression checks */

static int near_box(const struct ESnapRect *a, const struct ESnapRect *b)
{
    return near_px(a->x, b->x) && near_px(a->y, b->y) &&
           near_px(a->w, b->w) && near_px(a->h, b->h);
}

/* One live seam before a check changes its windows. */
static int dt25_seam(const struct Rig *r, struct ESnapDividerInfo *out)
{
    struct ESnapDividerInfo dv[4];
    ULONG need = 0;
    LONG rc;

    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: 2.25 ESnap_QueryDividers -> %s, %lu seam(s) "
           "(expected ES_OK, 1, a token)", rcname(rc), (unsigned long)need);
    if (!verdict(rc == ES_OK && need == 1 && dv[0].token != 0 &&
                 dv[0].vertical && dv[0].position == r->c)) {
        return 0;
    }
    *out = dv[0];
    return 1;
}

/* A and B leave their group, then snap as ordinary ungrouped windows.
 * The query must name ours before a line move is allowed to follow it. */
static void dt25_excluded(struct Rig *r)
{
    struct ESnapDivider d, q;
    struct ESnapRect before[2], after[2];
    LONG rc, rc2;
    int i;

    if (!front_screen(r->w[0])) {
        printf("esnaptest: the windows' screen is not in front: "
               "2.25 excluded divider checks skipped\n");
        return;
    }
    rc = ES_CALL(ESnap_DeleteGroup)(r->grp);
    printf("esnaptest: 2.25 A and B out of their group -> %s", rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return;
    }
    r->grp = 0;
    rc = ES_CALL(ESnap_SnapWindow)(r->w[0], ES_ZONE_LEFT);
    rc2 = ES_CALL(ESnap_SnapWindow)(r->w[1], ES_ZONE_RIGHT);
    printf("esnaptest: ESnap_SnapWindow(A left, B right) -> %s %s",
           rcname(rc), rcname(rc2));
    if (!verdict(rc == ES_OK && rc2 == ES_OK)) {
        return;
    }
    Delay(50L);                     /* the snaps must have landed */
    for (i = 0; i < 2; i++) {
        box_of(r->w[i], &before[i]);
    }
    d.present = 0;
    d.windowA = d.windowB = NULL;
    rc = ES_CALL(ESnap_QueryDividerAt)(10, before[1].x, r->hy, &d);
    printf("esnaptest: ESnap_QueryDividerAt(A against B) -> %s, present "
           "%ld (expected ES_OK, our vertical seam)", rcname(rc),
           (long)d.present);
    if (!verdict(rc == ES_OK && d.present && d.vertical &&
                 d.windowA == r->w[0] && d.windowB == r->w[1] &&
                 d.windowCount == 2)) {
        printf("esnaptest: the seam is not ours: excluded move skipped\n");
        return;
    }
    rc = ES_CALL(ESnap_ExcludeWindow)(r->w[0], TRUE);
    printf("esnaptest: ESnap_ExcludeWindow(A, TRUE) -> %s", rcname(rc));
    if (verdict(rc == ES_OK)) {
        q.present = 1;
        rc = ES_CALL(ESnap_QueryDividerAt)(10, d.position, r->hy, &q);
        printf("esnaptest: ESnap_QueryDividerAt after excluding A -> %s, "
               "present %ld (expected ES_OK, 0)", rcname(rc),
               (long)q.present);
        if (verdict(rc == ES_OK && !q.present)) {
            for (i = 0; i < 2; i++) {
                box_of(r->w[i], &before[i]);
            }
            rc = ES_CALL(ESnap_MoveDividerAt)(1, d.position,
                                               d.position + 50);
            Delay(25L);             /* a wrongly accepted move may land */
            for (i = 0; i < 2; i++) {
                box_of(r->w[i], &after[i]);
            }
            printf("esnaptest: ESnap_MoveDividerAt with A excluded -> %s "
                   "(expected ES_ERR_UNSUPPORTED, both boxes untouched)",
                   rcname(rc));
            verdict(rc == ES_ERR_UNSUPPORTED &&
                    near_box(&before[0], &after[0]) &&
                    near_box(&before[1], &after[1]));
        } else {
            printf("esnaptest: a seam was reported: line move skipped\n");
        }
    }
    rc = ES_CALL(ESnap_ExcludeWindow)(r->w[0], FALSE);
    printf("esnaptest: ESnap_ExcludeWindow(A, FALSE) -> %s", rcname(rc));
    verdict(rc == ES_OK);
}

/* No wait or logging between placement and the first group query and
 * move: a remembered request may still be on its way when they join. */
static void dt25_join(struct Rig *r)
{
    struct ESnapPlacement pl[2];
    struct ESnapRect cell[2];
    struct ESnapDividerInfo dv[4];
    ULONG need = 0;
    LONG rc, placed, made = 12345, added[2], queried = 12345;
    LONG moved = 12345, applied = 12345, at = r->c + 40;
    int i;

    rc = ES_CALL(ESnap_DeleteGroup)(r->grp);
    printf("esnaptest: 2.25 delete the setup group before placement -> %s",
           rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return;
    }
    r->grp = 0;
    rig_cells(r, at, 0, 2, cell);
    for (i = 0; i < 2; i++) {
        pl[i].window = r->w[i];
        pl[i].rect = cell[i];
        pl[i].result = added[i] = 12345;
    }
    placed = ES_CALL(ESnap_PlaceWindowsA)(pl, 2, ES_PF_KEEP_ZORDER);
    if (placed == ES_OK && pl[0].result == ES_OK && pl[1].result == ES_OK) {
        made = ES_CALL(ESnap_CreateGroup)(0, "joining", ES_GF_LOCKED,
                                           &r->grp);
        if (made == ES_OK) {
            added[0] = ES_CALL(ESnap_GroupAddWindow)(r->grp, r->w[0]);
            added[1] = ES_CALL(ESnap_GroupAddWindow)(r->grp, r->w[1]);
            if (added[0] == ES_OK && added[1] == ES_OK) {
                queried = rig_dividers(r, dv, &need);
                if (queried == ES_OK && need == 1 && dv[0].token != 0) {
                    moved = ES_CALL(ESnap_MoveDividerT)(dv[0].token,
                                                         at + 30, 0,
                                                         &applied);
                }
            }
        }
    }
    printf("esnaptest: ESnap_PlaceWindowsA with restore -> %s, entries "
           "%s %s (expected ES_OK)", rcname(placed), rcname(pl[0].result),
           rcname(pl[1].result));
    if (!verdict(placed == ES_OK && pl[0].result == ES_OK &&
                 pl[1].result == ES_OK)) {
        return;
    }
    printf("esnaptest: immediately create a group and add A and B -> "
           "%s %s %s (expected ES_OK)", rcname(made), rcname(added[0]),
           rcname(added[1]));
    if (!verdict(made == ES_OK && added[0] == ES_OK && added[1] == ES_OK)) {
        return;
    }
    printf("esnaptest: ESnap_QueryDividers before any wait -> %s, %lu "
           "seam(s) (expected ES_OK, 1, not ES_DIF_MOVED)", rcname(queried),
           (unsigned long)need);
    verdict(queried == ES_OK && need == 1 && dv[0].token != 0 &&
            dv[0].vertical && dv[0].position == at &&
            (dv[0].flags & ES_DIF_MOVED) == 0);
    printf("esnaptest: ESnap_MoveDividerT immediately after joining -> %s, "
           "applied %ld (expected ES_OK, %ld)", rcname(moved),
           (long)applied, (long)(at + 30));
    if (verdict(moved == ES_OK && applied == at + 30)) {
        rig_cells(r, applied, 0, 2, cell);  /* this group has no gap */
        printf("esnaptest: both boxes after the joining move");
        verdict(wait_boxes(r->w, cell, 2));
    }
}

/* First establish a manual move, then break the remembered topology.
 * No query after B's placement may retire the token before the move. */
static void dt25_order(struct Rig *r)
{
    struct ESnapDividerInfo one, dv[4];
    struct ESnapPlacement pl;
    struct ESnapRect want[2], hand;
    ULONG need = 0, ser[4];
    LONG rc, applied = 12345;

    if (!rig_check(r, r->c, 0, 2, "2.25 error order setup") ||
        !dt25_seam(r, &one)) {
        return;
    }
    rig_boxes(r, r->c, 0, 2, want);
    hand = want[0];
    hand.y += 60;
    hand.h -= 60;
    ChangeWindowBox(r->w[0], hand.x, hand.y, hand.w, hand.h);
    Delay(175L);                    /* beyond the two-second transit */
    printf("esnaptest: A moved by hand, three and a half seconds on");
    if (!verdict(wait_boxes(&r->w[0], &hand, 1))) {
        return;
    }
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: before breaking the seam -> %s, %lu seam(s) "
           "(expected the same token, ES_DIF_MOVED)", rcname(rc),
           (unsigned long)need);
    if (!verdict(rc == ES_OK && need == 1 && dv[0].token == one.token &&
                 (dv[0].flags & ES_DIF_MOVED) != 0)) {
        return;
    }
    pl.window = r->w[1];
    pl.rect = r->u;
    pl.rect.w = r->u.w / 3;
    pl.rect.y = r->hy;
    pl.rect.h = r->u.y + r->u.h - r->hy;
    pl.result = 12345;
    rc = ES_CALL(ESnap_PlaceWindowsA)(&pl, 1,
                                       ES_PF_NO_RESTORE | ES_PF_KEEP_ZORDER);
    printf("esnaptest: B placed away from A's seam -> %s, entry %s",
           rcname(rc), rcname(pl.result));
    if (!verdict(rc == ES_OK && pl.result == ES_OK)) {
        return;
    }
    rc = ES_CALL(ESnap_MoveDividerT)(one.token, r->c + 30, 0, &applied);
    printf("esnaptest: ESnap_MoveDividerT with a changed A and no seam -> "
           "%s, applied %ld (expected ES_ERR_STALE, untouched)",
           rcname(rc), (long)applied);
    verdict(rc == ES_ERR_STALE && applied == 12345);
    rc = ES_CALL(ESnap_QueryDividerWindows)(one.token, ser, 4, &need);
    printf("esnaptest: ESnap_QueryDividerWindows after that -> %s "
           "(expected ES_ERR_STALE: retired)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
}

/* rig_check for a rig in one half of the area. The side the two halves
 * share lies inside the usable area, so ES_PF_CELL takes its part of
 * the gap off it as well: above it the larger part, below the smaller. */
static int dt25_half_check(const struct Rig *r, int top, const char *what)
{
    struct ESnapRect want[2], got[2];
    int i, ok;

    rig_boxes(r, r->c, 0, 2, want);
    for (i = 0; i < 2; i++) {
        if (top) {
            want[i].h -= GAP_A;
        } else {
            want[i].y += GAP_B;
            want[i].h -= GAP_B;
        }
    }
    ok = wait_boxes(r->w, want, 2);
    for (i = 0; i < 2; i++) {
        box_of(r->w[i], &got[i]);
        printf("esnaptest:   %c %ld,%ld %ldx%ld (expected %ld,%ld %ldx%ld)\n",
               'A' + i, (long)got[i].x, (long)got[i].y, (long)got[i].w,
               (long)got[i].h, (long)want[i].x, (long)want[i].y,
               (long)want[i].w, (long)want[i].h);
    }
    printf("esnaptest: %s", what);
    return verdict(ok);
}

/* Two groups on one x, in separate halves of the area. The second rig
 * shares the first rig's client; rig_down closes both on every path. */
static void dt25_follow(struct Rig *r, struct Rig *other)
{
    struct ESnapDivider d;
    struct ESnapRect before[2], after[2];
    LONG rc, rc2, first, second;
    int i;

    if (!front_screen(r->w[0])) {
        printf("esnaptest: the windows' screen is not in front: "
               "2.25 stale line checks skipped\n");
        return;
    }
    other->u = r->u;
    r->u.h /= 2;
    other->u.y += r->u.h;
    other->u.h -= r->u.h;
    other->c = r->c;
    other->hy = other->u.y + other->u.h / 2;
    other->w[0] = open_test_window("esnaptest G2 A");
    other->w[1] = open_test_window("esnaptest G2 B");
    printf("esnaptest: 2.25 two more windows for G2 -> %p %p",
           (void *)other->w[0], (void *)other->w[1]);
    if (!verdict(other->w[0] != NULL && other->w[1] != NULL)) {
        return;
    }
    if (!front_screen(other->w[0]) || !front_screen(other->w[1])) {
        printf("esnaptest: G2 is not on the front screen: "
               "2.25 stale line checks skipped\n");
        return;
    }
    rc = ES_CALL(ESnap_CreateGroup)(0, "second seam", ES_GF_LOCKED,
                                     &other->grp);
    if (rc == ES_OK) {
        rc = set_gap(other->grp);
    }
    for (i = 0; i < 2 && rc == ES_OK; i++) {
        rc = ES_CALL(ESnap_GroupAddWindow)(other->grp, other->w[i]);
    }
    printf("esnaptest: G2 with its own two members and gap %d -> %s",
           TEST_GAP, rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return;
    }
    rc = rig_place(r, r->c, 0, 2);
    rc2 = rig_place(other, other->c, 0, 2);
    printf("esnaptest: G1 above G2, both split at %ld -> %s %s",
           (long)r->c, rcname(rc), rcname(rc2));
    if (!verdict(rc == ES_OK && rc2 == ES_OK) ||
        !dt25_half_check(r, 1, "G1 in the top half") ||
        !dt25_half_check(other, 0, "G2 in the bottom half")) {
        return;
    }
    for (i = 0; i < 2; i++) {
        box_of(other->w[i], &before[i]);
    }
    d.present = 0;
    d.windowA = d.windowB = NULL;
    rc = ES_CALL(ESnap_QueryDividerAt)(10, r->c, r->u.y + r->u.h / 2, &d);
    printf("esnaptest: ESnap_QueryDividerAt on G1 -> %s, present %ld "
           "(expected G1's two windows)", rcname(rc), (long)d.present);
    if (!verdict(rc == ES_OK && d.present && d.vertical &&
                 d.position == r->c && d.windowA == r->w[0] &&
                 d.windowB == r->w[1] && d.windowCount == 2)) {
        printf("esnaptest: the queried seam is not G1: stale moves skipped\n");
        return;
    }
    CloseWindow(r->w[1]);
    r->w[1] = NULL;
    first = ES_CALL(ESnap_MoveDividerAt)(1, d.position, d.position + 50);
    second = ES_CALL(ESnap_MoveDividerAt)(1, d.position, d.position + 50);
    Delay(25L);                     /* a wrong second move may land */
    for (i = 0; i < 2; i++) {
        box_of(other->w[i], &after[i]);
    }
    printf("esnaptest: close a G1 window, then two identical "
           "ESnap_MoveDividerAt calls -> %s %s (expected ES_ERR_STALE "
           "twice)", rcname(first), rcname(second));
    verdict(first == ES_ERR_STALE && second == ES_ERR_STALE);
    printf("esnaptest: G2's boxes after both stale line moves "
           "(expected untouched, within slack)");
    verdict(near_box(&before[0], &after[0]) &&
            near_box(&before[1], &after[1]));
}

static void dt25_coordinates(struct Rig *r)
{
    struct ESnapRect before[2], after[2], bad;
    struct ESnapPlacement pl[2];
    struct ESnapDivider d;
    struct ESnapDividerInfo one;
    LONG rc, applied = 12345;
    int i;

    if (!rig_check(r, r->c, 0, 2, "2.25 coordinate setup")) {
        return;
    }
    box_of(r->w[0], &before[0]);
    for (i = 0; i < 3; i++) {
        bad = before[0];
        if (i == 0) {
            bad.x = 40000L;
        } else if (i == 1) {
            bad.y = -40000L;
        } else {
            bad.w = 40000L;
        }
        rc = ES_CALL(ESnap_PlaceWindow)(r->w[0], &bad, 0);
        Delay(25L);                 /* a bad request must not move it */
        box_of(r->w[0], &after[0]);
        printf("esnaptest: ESnap_PlaceWindow(%s) -> %s "
               "(expected ES_ERR_BAD_ARGS, A untouched)",
               i == 0 ? "x 40000" : i == 1 ? "y -40000" : "width 40000",
               rcname(rc));
        verdict(rc == ES_ERR_BAD_ARGS && near_box(&before[0], &after[0]));
    }
    box_of(r->w[1], &before[1]);
    pl[0].window = r->w[0];
    pl[0].rect = before[0];
    pl[0].rect.x = 40000L;
    pl[0].result = 12345;
    pl[1].window = r->w[1];
    pl[1].rect = before[1];
    pl[1].rect.y += 60;
    pl[1].rect.h -= 60;
    pl[1].result = 12345;
    rc = ES_CALL(ESnap_PlaceWindowsA)(pl, 2, ES_PF_KEEP_ZORDER);
    printf("esnaptest: ESnap_PlaceWindowsA(bad A, good B) -> %s, entries "
           "%s %s (expected ES_OK, ES_ERR_BAD_ARGS, ES_OK)", rcname(rc),
           rcname(pl[0].result), rcname(pl[1].result));
    verdict(rc == ES_OK && pl[0].result == ES_ERR_BAD_ARGS &&
            pl[1].result == ES_OK);
    printf("esnaptest: the good batch entry lands");
    verdict(pl[1].result == ES_OK && wait_boxes(&r->w[1], &pl[1].rect, 1));
    Delay(25L);
    box_of(r->w[0], &after[0]);
    box_of(r->w[1], &after[1]);
    printf("esnaptest: bad entry A untouched, good B in its new box");
    verdict(near_box(&before[0], &after[0]) &&
            near_box(&pl[1].rect, &after[1]));

    d.present = 1;
    rc = ES_CALL(ESnap_QueryDividerAt)(10, 100000L, r->hy, &d);
    printf("esnaptest: ESnap_QueryDividerAt(x 100000) -> %s, present %ld "
           "(expected ES_OK, 0)", rcname(rc), (long)d.present);
    verdict(rc == ES_OK && !d.present);

    rc = rig_place(r, r->c, 0, 2);
    printf("esnaptest: replace both windows before the far clamp -> %s",
           rcname(rc));
    if (!verdict(rc == ES_OK) ||
        !rig_check(r, r->c, 0, 2, "back on the seam") ||
        !dt25_seam(r, &one)) {
        return;
    }
    if ((one.flags & ES_DIF_STUCK) != 0 ||
        one.minPosition >= one.maxPosition) {
        printf("esnaptest: the seam has no room: far clamp skipped\n");
        return;
    }
    rc = ES_CALL(ESnap_MoveDividerT)(one.token, 0x7FFFFFFFL, 0, &applied);
    printf("esnaptest: ESnap_MoveDividerT(0x7FFFFFFF) -> %s, applied %ld "
           "(expected ES_OK, maxPosition %ld)", rcname(rc), (long)applied,
           (long)one.maxPosition);
    if (verdict(rc == ES_OK && applied == one.maxPosition)) {
        rig_check(r, applied, 0, 2, "at the far limit");
    }
}

/* A spare slot is a guard, filled and checked byte by byte so padding
 * as well as fields must be left alone by a short-buffer query. */
static void fill_pattern(void *buf, ULONG size)
{
    UBYTE *p = (UBYTE *)buf;
    ULONG i;

    for (i = 0; i < size; i++) {
        p[i] = 0xA5;
    }
}

static int pattern_untouched(const void *buf, ULONG size)
{
    const UBYTE *p = (const UBYTE *)buf;
    ULONG i;

    for (i = 0; i < size; i++) {
        if (p[i] != 0xA5) {
            return 0;
        }
    }
    return 1;
}

static int same_bytes(const void *a, const void *b, ULONG size)
{
    const UBYTE *p = (const UBYTE *)a, *q = (const UBYTE *)b;
    ULONG i;

    for (i = 0; i < size; i++) {
        if (p[i] != q[i]) {
            return 0;
        }
    }
    return 1;
}

static void dt25_coverage(struct Rig *r)
{
    struct ESnapDividerInfo dv[4], short_dv[2];
    ULONG need = 0, short_ser[4], gone;
    ULONG guard_size = (ULONG)(sizeof(short_ser) - sizeof(short_ser[0]));
    LONG rc, applied = 12345;

    r->w[2] = open_test_window("esnaptest short buffers C");
    printf("esnaptest: 2.25 a third window for two seams -> %p",
           (void *)r->w[2]);
    if (!verdict(r->w[2] != NULL)) {
        return;
    }
    rc = ES_CALL(ESnap_GroupAddWindow)(r->grp, r->w[2]);
    if (rc == ES_OK) {
        rc = ES_CALL(ESnap_QueryWindowSerial)(r->w[2], &r->serial[2]);
    }
    if (rc == ES_OK) {
        rc = rig_place(r, r->c, r->hy, 3);
    }
    printf("esnaptest: A left of B above C -> %s", rcname(rc));
    if (!verdict(rc == ES_OK) ||
        !rig_check(r, r->c, r->hy, 3, "short-buffer setup")) {
        return;
    }
    rc = rig_dividers(r, dv, &need);
    printf("esnaptest: the full list -> %s, %lu seam(s) "
           "(expected ES_OK, 2, the first vertical with three windows)",
           rcname(rc), (unsigned long)need);
    if (!verdict(rc == ES_OK && need == 2 && dv[0].token != 0 &&
                 dv[0].vertical && dv[0].countA == 1 && dv[0].countB == 2)) {
        return;
    }
    fill_pattern(short_dv, (ULONG)sizeof(short_dv));
    need = 0;
    rc = ES_CALL(ESnap_QueryDividers)(r->grp, short_dv, 1, &need);
    printf("esnaptest: ESnap_QueryDividers(count 1, two seams) -> %s, "
           "needed %lu (expected ES_OK, 2, first entry written, "
           "second untouched)", rcname(rc), (unsigned long)need);
    verdict(rc == ES_OK && need == 2 &&
            same_bytes(&short_dv[0], &dv[0], (ULONG)sizeof(dv[0])) &&
            pattern_untouched(&short_dv[1], (ULONG)sizeof(short_dv[1])));
    fill_pattern(short_ser, (ULONG)sizeof(short_ser));
    need = 0;
    rc = ES_CALL(ESnap_QueryDividerWindows)(dv[0].token, short_ser, 1, &need);
    printf("esnaptest: ESnap_QueryDividerWindows(count 1, three windows) "
           "-> %s, needed %lu (expected ES_OK, 3, A's serial first, "
           "the rest untouched)", rcname(rc), (unsigned long)need);
    verdict(rc == ES_OK && need == 3 && short_ser[0] == r->serial[0] &&
            pattern_untouched(&short_ser[1], guard_size));

    rc = ES_CALL(ESnap_GroupRemoveWindow)(r->grp, r->w[1]);
    printf("esnaptest: ESnap_GroupRemoveWindow(B) -> %s", rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return;
    }
    rc = ES_CALL(ESnap_MoveDividerT)(dv[0].token, r->c + 30, 0, &applied);
    printf("esnaptest: ESnap_MoveDividerT after a member left -> %s, "
           "applied %ld (expected ES_ERR_STALE, sentinel 12345)",
           rcname(rc), (long)applied);
    verdict(rc == ES_ERR_STALE && applied == 12345);
    gone = r->grp;
    rc = ES_CALL(ESnap_DeleteGroup)(gone);
    printf("esnaptest: ESnap_DeleteGroup before querying its old id -> %s",
           rcname(rc));
    if (!verdict(rc == ES_OK)) {
        return;
    }
    r->grp = 0;
    rc = ES_CALL(ESnap_QueryDividers)(gone, dv, 4, &need);
    printf("esnaptest: ESnap_QueryDividers(the deleted group) -> %s "
           "(expected ES_ERR_STALE)", rcname(rc));
    verdict(rc == ES_ERR_STALE);
}

static void dt25_drag_restore(void)
{
    struct TagItem opt[3];
    LONG rc;

    opt[0].ti_Tag = ES_OPT_DragRestore;
    opt[0].ti_Data = FALSE;
    opt[1].ti_Tag = TAG_DONE;
    opt[1].ti_Data = 0;
    rc = ES_CALL(ESnap_SetOptionsA)(opt);
    printf("esnaptest: ESnap_SetOptionsA(DragRestore FALSE) -> %s "
           "(expected ES_OK)", rcname(rc));
    verdict(rc == ES_OK);
    opt[0].ti_Data = TRUE;
    rc = ES_CALL(ESnap_SetOptionsA)(opt);
    printf("esnaptest: ESnap_SetOptionsA(DragRestore TRUE) -> %s "
           "(expected ES_OK)", rcname(rc));
    verdict(rc == ES_OK);
    opt[0].ti_Data = FALSE;
    opt[1].ti_Tag = ES_OPT_EdgePx;
    opt[1].ti_Data = 40000;
    opt[2].ti_Tag = TAG_DONE;
    opt[2].ti_Data = 0;
    rc = ES_CALL(ESnap_SetOptionsA)(opt);
    printf("esnaptest: ESnap_SetOptionsA(DragRestore FALSE, EdgePx 40000) "
           "-> %s (expected ES_ERR_BAD_ARGS)", rcname(rc));
    verdict(rc == ES_ERR_BAD_ARGS);
    opt[0].ti_Data = TRUE;
    opt[1].ti_Tag = TAG_DONE;
    rc = ES_CALL(ESnap_SetOptionsA)(opt);
    printf("esnaptest: leave DragRestore TRUE -> %s (expected ES_OK)",
           rcname(rc));
    verdict(rc == ES_OK);
}

/* Each part has a fresh rig and is cleaned up even when setup or an
 * assertion fails; none asks for a drag or other input. */
static void test_fixes(void)
{
    struct Rig r, other;

    g_pass = 0;
    g_fail = 0;
    if (rig_up(&r, 0)) {
        dt25_excluded(&r);
    }
    rig_down(&r);
    if (rig_up(&r, ES_GF_LOCKED)) {
        dt25_join(&r);
    }
    rig_down(&r);
    if (rig_up(&r, ES_GF_LOCKED)) {
        dt25_order(&r);
    }
    rig_down(&r);
    rig_init(&other);
    if (rig_up(&r, ES_GF_LOCKED)) {
        dt25_follow(&r, &other);
    }
    rig_down(&other);
    rig_down(&r);
    if (rig_up(&r, ES_GF_LOCKED)) {
        dt25_coordinates(&r);
    }
    rig_down(&r);
    if (rig_up(&r, ES_GF_LOCKED)) {
        dt25_coverage(&r);
    }
    rig_down(&r);
    dt25_drag_restore();
    summary("2.25 fixes");
}

/* Is the pointer on w's title bar? Read with w's box, under the lock. */
static int pointer_on_bar(struct Window *w, LONG *px, LONG *py)
{
    ULONG ilock = LockIBase(0);
    int on;

    *px = w->WScreen->MouseX;
    *py = w->WScreen->MouseY;
    on = *px >= w->LeftEdge && *px < w->LeftEdge + w->Width &&
         *py >= w->TopEdge && *py < w->TopEdge + w->BorderTop;
    UnlockIBase(ilock);
    return on;
}

/*
 * esnaptest D (2.24): a window of our own snapped to the left half, for
 * twenty seconds, so that it can be dragged away by its title bar by
 * hand or by a script. Let go away from the edges, it must have its size
 * from before the snap back and no zone, and the pointer must be on its
 * title bar, which only holds when the mouse is left alone after the
 * drop. esnaptest DL puts the window in a locked group of ours first and
 * snaps it as the owner: the drag must then leave the snapped size and
 * the zone alone.
 */
static void drag_away(int locked)
{
    struct ESnapRect before, snapped, after;
    ULONG me = 0, grp = 0, zone = 99;
    LONG rc = ES_OK, px = 0, py = 0;
    struct Window *w;
    int on_bar, moved;

    g_pass = 0;
    g_fail = 0;
    w = open_test_window("esnaptest D");
    printf("esnaptest: a window of our own, D -> %p", (void *)w);
    if (verdict(w != NULL)) {
        if (locked) {
            rc = ES_CALL(ESnap_RegisterClient)("esnaptest", ES_CL_LAYOUT, &me);
            if (rc == ES_OK) {
                rc = ES_CALL(ESnap_CreateGroup)(0, "drag", ES_GF_LOCKED, &grp);
            }
            if (rc == ES_OK) {
                rc = ES_CALL(ESnap_GroupAddWindow)(grp, w);
            }
            printf("esnaptest: D into a locked group of ours -> %s, group %lu",
                   rcname(rc), (unsigned long)grp);
            verdict(rc == ES_OK);
        }
    }
    if (w != NULL && rc == ES_OK) {
        box_of(w, &before);
        rc = ES_CALL(ESnap_SnapWindow)(w, ES_ZONE_LEFT);
        Delay(50L);
        box_of(w, &snapped);
        printf("esnaptest: ESnap_SnapWindow(D, left)%s -> %s, from %ld,%ld "
               "%ldx%ld to %ld,%ld %ldx%ld", locked ? " by the owner" : "",
               rcname(rc), (long)before.x, (long)before.y, (long)before.w,
               (long)before.h, (long)snapped.x, (long)snapped.y,
               (long)snapped.w, (long)snapped.h);
        if (verdict(rc == ES_OK && (!near_px(snapped.w, before.w) ||
                                    !near_px(snapped.h, before.h)))) {
            printf("esnaptest: drag D by its title bar to the middle of the "
                   "screen, let go and leave the mouse there: you have "
                   "twenty seconds\n");
            fflush(stdout);
            Delay(50L * 20);
            box_of(w, &after);
            on_bar = pointer_on_bar(w, &px, &py);
            rc = ES_CALL(ESnap_QueryWindow)(w, &zone);
            moved = !near_px(after.x, snapped.x) ||
                    !near_px(after.y, snapped.y);
            if (locked) {
                printf("esnaptest: D at %ld,%ld %ldx%ld (%s), zone %lu "
                       "(expected dragged, at the snapped size %ldx%ld, zone "
                       "%d: left alone)", (long)after.x, (long)after.y,
                       (long)after.w, (long)after.h,
                       moved ? "dragged" : "NOT DRAGGED", (unsigned long)zone,
                       (long)snapped.w, (long)snapped.h, ES_ZONE_LEFT);
                verdict(moved && near_px(after.w, snapped.w) &&
                        near_px(after.h, snapped.h) && rc == ES_OK &&
                        zone == ES_ZONE_LEFT);
            } else {
                printf("esnaptest: D at %ld,%ld %ldx%ld (expected %ldx%ld, "
                       "its size from before the snap)", (long)after.x,
                       (long)after.y, (long)after.w, (long)after.h,
                       (long)before.w, (long)before.h);
                verdict(near_px(after.w, before.w) &&
                        near_px(after.h, before.h));
                printf("esnaptest: ESnap_QueryWindow(D) -> %s, zone %lu "
                       "(expected ES_OK, %d: no longer snapped)", rcname(rc),
                       (unsigned long)zone, ES_ZONE_NONE);
                verdict(rc == ES_OK && zone == ES_ZONE_NONE);
                printf("esnaptest: the pointer at %ld,%ld, %s (expected on "
                       "D's title bar, if the mouse was left alone after the "
                       "drop)", (long)px, (long)py,
                       on_bar ? "on D's title bar" : "NOT ON IT");
                verdict(on_bar);
            }
        }
    }
    if (w != NULL) {
        CloseWindow(w);
    }
    if (grp != 0) {
        ES_CALL(ESnap_DeleteGroup)(grp);
    }
    if (me != 0) {
        ES_CALL(ESnap_UnregisterClient)(me);
    }
    summary(locked ? "a locked window dragged away" :
                     "a snapped window dragged away");
}

/*
 * esnaptest LOCK: keep win in a locked group for twenty seconds, so
 * that a drag by hand (or by a tester's script) can show that the
 * commodity leaves it alone and the generation moves.
 */
static void lock_window(struct Window *win)
{
    ULONG me = 0, grp = 0, g0, g1;
    LONG rc;

    if (win == NULL) {
        printf("esnaptest: no active window to lock\n");
        return;
    }
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
}

/* The window the user is working in, or NULL. */
static struct Window *active_window(void)
{
    struct Window *win;
    ULONG ilock = LockIBase(0);

    win = ((struct IntuitionBase *)IntuitionBase)->ActiveWindow;
    UnlockIBase(ilock);
    return win;
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

    /*
     * The modes tried by hand come before anything below that moves or
     * snaps a window, and touch none of the user's windows but the one
     * LOCK puts in a group. LOCK keeps the active window in a locked
     * group for twenty seconds (see lock_window()); S makes a seam of a
     * group of ours, gap 20, to drag by hand with the commodity's handle
     * for twenty seconds, and SL the same locked, to be left alone (see
     * drag_seam()); D snaps a window of ours to the left half, to be
     * dragged away by hand within twenty seconds, and DL the same in a
     * locked group, to be left at its size (see drag_away()). Each
     * cleans up and exits, also on a library too old for it, rather than
     * go on with the run that snaps the active window.
     */
    if (argc > 1 && (argv[1][0] == 'L' || argv[1][0] == 'S' ||
                     argv[1][0] == 'D')) {
        if (argv[1][0] == 'L' && EdgeSnapBase->lib_Revision >= 12) {
            lock_window(active_window());
        } else if (argv[1][0] == 'L') {
            printf("esnaptest: LOCK needs library revision 12, this is %d\n",
                   (int)EdgeSnapBase->lib_Revision);
        } else if (EdgeSnapBase->lib_Revision < 24) {
            printf("esnaptest: %c needs library revision 24, this is %d\n",
                   argv[1][0], (int)EdgeSnapBase->lib_Revision);
        } else if (argv[1][0] == 'S') {
            drag_seam(argv[1][1] == 'L');
        } else {
            drag_away(argv[1][1] == 'L');
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

    /* The divider: with two windows snapped side by side the library
     * should offer a seam, and moving it must resize both. This is the
     * deterministic half of the feature: the frontend's little handle
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

    win = active_window();
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
        /* 2.20: gaps and margins of the group, taken off a cell. */
        if (EdgeSnapBase->lib_Revision >= 20) {
            struct TagItem go[3];
            struct ESnapArea ua;
            struct ESnapRect cell;

            go[0].ti_Tag = ES_GO_Gap;
            go[0].ti_Data = 10;
            go[1].ti_Tag = ES_GO_Margin;
            go[1].ti_Data = 20;
            go[2].ti_Tag = TAG_DONE;
            go[2].ti_Data = 0;
            rc = ES_CALL(ESnap_SetGroupOptionsA)(grp, go);
            printf("esnaptest: ESnap_SetGroupOptionsA(gap 10, margin 20) -> "
                   "%s\n", rcname(rc));
            rc = ES_CALL(ESnap_SetGroupOptionsA)(grp + 1000, go);
            printf("esnaptest: options of a group that is not there -> %s "
                   "(expected ES_ERR_STALE)\n", rcname(rc));
            go[0].ti_Data = 999;
            go[1].ti_Tag = TAG_DONE;
            rc = ES_CALL(ESnap_SetGroupOptionsA)(grp, go);
            printf("esnaptest: a gap of 999 -> %s (expected ES_ERR_BAD_ARGS)\n",
                   rcname(rc));
            if (ES_CALL(ESnap_QueryScreenArea)(win->WScreen, &ua) == ES_OK) {
                /* the left half of the usable area as a cell: the margin
                 * on three sides, half the gap (5) on the seam side */
                cell = ua.usable;
                cell.w = ua.usable.w / 2;
                rc = ES_CALL(ESnap_PlaceWindow)(win, &cell, ES_PF_CELL);
                Delay(50L);
                printf("esnaptest: ESnap_PlaceWindow(left half as a cell) -> "
                       "%s, window %d,%d %dx%d (expected %ld,%ld %ldx%ld, "
                       "its size limits allowing)\n", rcname(rc),
                       win->LeftEdge, win->TopEdge, win->Width, win->Height,
                       (long)(ua.usable.x + 20), (long)(ua.usable.y + 20),
                       (long)(cell.w - 20 - 5), (long)(ua.usable.h - 40));
                ES_CALL(ESnap_UnsnapWindow)(win);
                Delay(25L);
            }
        }
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

    /* 2.24: a tiler's seams by token, on windows of our own. */
    if (EdgeSnapBase->lib_Revision >= 24) {
        test_dividers();
    } else {
        printf("esnaptest: library revision %d has no 2.24 calls, skipped\n",
               (int)EdgeSnapBase->lib_Revision);
    }

    if (EdgeSnapBase->lib_Revision >= 25) {
        test_fixes();
    } else {
        printf("esnaptest: 2.25 checks need revision 25, this is %d, skipped\n",
               (int)EdgeSnapBase->lib_Revision);
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
