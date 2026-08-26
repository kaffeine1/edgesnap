/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_spike.c - Phase 0 feasibility spike for EdgeSnap.
 *
 * A single-file commodity for AmigaOS 4.x and MorphOS that verifies the two
 * assumptions the whole project rests on:
 *
 *   1. A commodities CxCustom object (running in the input.device context,
 *      priority 56, ahead of Intuition at 50) can observe raw mouse events,
 *      and a heuristic in the main task can tell "the user is dragging a
 *      window" apart from ordinary mouse activity by correlating pointer
 *      movement with the active window's LeftEdge/TopEdge under LockIBase().
 *
 *   2. ChangeWindowBox() on a foreign window right after drag release
 *      actually sticks (or reveals a race with Intuition's own drop
 *      handling - that is a spike finding either way).
 *
 * What it does:
 *   - drag a window's title bar so the POINTER touches a screen edge, then
 *     release: the window snaps to half / quarter / maximized.
 *   - hotkeys work without any drag heuristics (phase 1 preview):
 *       ctrl alt cursor_left   snap active window to the left half
 *       ctrl alt cursor_right  snap active window to the right half
 *       ctrl alt cursor_up     maximize active window (usable area)
 *       ctrl alt cursor_down   restore active window to its pre-snap box
 *   - prints what it observes to stdout: run it from a Shell.
 *
 * Architecture note (the rule that shapes everything): the CxCustom action
 * runs inside input.device - it only bumps counters and Signal()s the main
 * task. Every Intuition call (LockIBase, ChangeWindowBox) happens here in
 * the main task. Under LockIBase we only READ structures; the lock is
 * released before any Intuition call.
 *
 * Known spike-level races, accepted for now and re-checked on real systems:
 *   - a window can close between our snapshot and ChangeWindowBox();
 *   - Intuition may still be finishing its own drag when we snap on
 *     release (if the drop position wins over our box, the library phase
 *     will need a deferred commit via timer.device).
 */

#ifdef __amigaos4__
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos/dos.h> /* RETURN_*, SIGBREAKF_CTRL_C */
#include <devices/inputevent.h>
#include <libraries/commodities.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h> /* DrawInfo pens for the preview frame */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/commodities.h>

#ifdef __MORPHOS__
#include <emul/emulregs.h>
#include <emul/emulinterface.h>
#endif

#include "zones.h"
#include "panels.h"

/* ---------------------------------------------------------------- tuning */

#define ES_EDGE_PX        12   /* pointer this close to an edge = zone     */
#define ES_CORNER_DIV      4   /* corner length = usable_height / this     */
#define ES_DRAG_MIN_PX     4   /* pointer must move this much to count     */
#define ES_RESTORE_SLOTS  16
#define ES_FRAME_PX        4   /* preview frame thickness                  */

/* --------------------------------------------------------- library bases */

/* OS4's proto/intuition.h predeclares IntuitionBase as struct Library *;
 * classic/MorphOS code wants struct IntuitionBase *. Match the SDK and go
 * through a typed alias for structure access. */
#ifdef __amigaos4__
struct Library *IntuitionBase;
#else
struct IntuitionBase *IntuitionBase;
#endif
#define ES_IBASE ((struct IntuitionBase *)IntuitionBase)

/* Same story for GfxBase (needed by the preview frame's RectFill). */
#ifdef __amigaos4__
struct Library *GfxBase;
#else
struct GfxBase *GfxBase;
#endif

struct Library *CxBase;

#ifdef __amigaos4__
struct IntuitionIFace *IIntuition;
struct GraphicsIFace *IGraphics;
struct CommoditiesIFace *ICommodities;
#endif

#ifdef __MORPHOS__
/* PPC task stack (the shell Stack command only sizes the 68k stack). */
unsigned long __stack = 65536UL;
#endif

/* AmigaDOS stack cookie for lanes that honor it. */
static const char *es_stack_cookie = "$STACK:65536";

/* ----------------------------------------- input handler <-> main task */

/*
 * Written by the CxCustom action in input.device context, read by the main
 * task. Counters (not flags) so fast press+release pairs are not lost.
 */
struct SpikeShared {
    struct Task *engine_task;
    ULONG engine_sigmask;
    volatile ULONG presses;
    volatile ULONG releases;
    volatile ULONG moves;
};

static struct SpikeShared g_shared;

/*
 * CxCustom action. RULES: no Intuition calls, no allocations, no waiting.
 * Bump a counter, Signal() the main task, get out.
 */
static void spike_cx_action(CxMsg *msg, CxObj *obj)
{
    struct InputEvent *ie;

    (void)obj;
    if (CxMsgType(msg) != CXM_IEVENT) {
        return;
    }
    ie = (struct InputEvent *)CxMsgData(msg);
    if (ie == NULL || ie->ie_Class != IECLASS_RAWMOUSE) {
        return;
    }
    if (ie->ie_Code == IECODE_LBUTTON) {
        g_shared.presses++;
    } else if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
        g_shared.releases++;
    } else if (ie->ie_Code == IECODE_NOBUTTON) {
        g_shared.moves++;
    } else {
        return;
    }
    if (g_shared.engine_task != NULL) {
        Signal(g_shared.engine_task, g_shared.engine_sigmask);
    }
}

#ifdef __MORPHOS__
/*
 * MorphOS calls commodities custom actions through the 68k ABI (CxMsg in
 * a0, CxObj in a1), so native PPC code needs an EmulLibEntry gate.
 */
static void spike_cx_action_gate(void)
{
    CxMsg *msg = (CxMsg *)REG_A0;
    CxObj *obj = (CxObj *)REG_A1;

    spike_cx_action(msg, obj);
}

static struct EmulLibEntry spike_cx_trap = {
    TRAP_LIB, 0, (void (*)(void))spike_cx_action_gate
};

#define ES_CX_ACTION ((APTR)&spike_cx_trap)
#else
#define ES_CX_ACTION ((APTR)spike_cx_action)
#endif

/* -------------------------------------------------- window snapshotting */

/* Copy of everything we need from a Window/Screen, taken under LockIBase. */
struct WinSnap {
    struct Window *win;
    struct Screen *scr;    /* identity only - never dereferenced later  */
    ESRect box;
    int min_w, min_h;
    int max_w, max_h;      /* 0 = unlimited (0xFFFF already translated) */
    ULONG flags;
    ESRect usable;         /* usable area of the window's screen        */
    int mouse_x, mouse_y;  /* pointer position on that screen           */
};

/* Preview windows must not be mistaken for dock panels; defined after
 * the preview module below. */
static int spike_is_preview_win(struct Window *w);

#define ES_PANEL_SCAN_MAX 16

/*
 * Called under LockIBase only: usable area of scr = screen minus title
 * bar minus dock/panel strips (AmiDock, Ambient panels), macOS-style.
 * Panel candidates: windows that cannot be dragged, sized or are
 * backdrop, minus the snap target and our own preview bars. Borderless
 * is deliberately NOT required - real docks are not guaranteed to set
 * it (MorphOS field finding, 2026-08-26) and the geometry policy in
 * core/panels.c is the real gatekeeper. skip may be NULL.
 */
static void spike_usable_area(struct Screen *scr, struct Window *skip,
                              ESRect *out_usable, ESInsets *out_ins)
{
    ESRect scrrect;
    ESRect panels[ES_PANEL_SCAN_MAX];
    struct Window *w;
    int n = 0;
    int top;

    scrrect.x = 0;
    scrrect.y = 0;
    scrrect.w = scr->Width;
    scrrect.h = scr->Height;
    for (w = scr->FirstWindow; w != NULL && n < ES_PANEL_SCAN_MAX;
         w = w->NextWindow) {
        if (w == skip || spike_is_preview_win(w)) {
            continue;
        }
        if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET |
                         WFLG_BACKDROP)) != 0) {
            continue;
        }
        panels[n].x = w->LeftEdge;
        panels[n].y = w->TopEdge;
        panels[n].w = w->Width;
        panels[n].h = w->Height;
        n++;
    }
    es_panel_insets(&scrrect, panels, n, out_ins);

    top = scr->BarHeight + 1;
    if (out_ins->t > top) {
        top = out_ins->t;
    }
    out_usable->x = out_ins->l;
    out_usable->y = top;
    out_usable->w = scr->Width - out_ins->l - out_ins->r;
    out_usable->h = scr->Height - top - out_ins->b;
}

/* Called under LockIBase only. */
static void spike_fill_snapshot(struct Window *win, struct Screen *scr,
                                struct WinSnap *out)
{
    out->win = win;
    out->scr = scr;
    out->box.x = win->LeftEdge;
    out->box.y = win->TopEdge;
    out->box.w = win->Width;
    out->box.h = win->Height;
    out->min_w = win->MinWidth;
    out->min_h = win->MinHeight;
    out->max_w = (int)(UWORD)win->MaxWidth;
    out->max_h = (int)(UWORD)win->MaxHeight;
    if (out->max_w >= 0xFFFF) {
        out->max_w = 0;
    }
    if (out->max_h >= 0xFFFF) {
        out->max_h = 0;
    }
    out->flags = win->Flags;
    {
        ESInsets ins;

        spike_usable_area(scr, win, &out->usable, &ins);
    }
    out->mouse_x = scr->MouseX;
    out->mouse_y = scr->MouseY;
}

/*
 * Re-validate a window pointer by walking the public Intuition lists, and
 * snapshot it if still alive. Windows can close at any moment, so a stored
 * struct Window * is never trusted without this.
 */
static int spike_snapshot_window(struct Window *target, struct WinSnap *out)
{
    ULONG ilock;
    struct Screen *scr;
    struct Window *win;
    int found = 0;

    ilock = LockIBase(0);
    for (scr = ES_IBASE->FirstScreen; scr != NULL && !found;
         scr = scr->NextScreen) {
        for (win = scr->FirstWindow; win != NULL; win = win->NextWindow) {
            if (win == target) {
                spike_fill_snapshot(win, scr, out);
                found = 1;
                break;
            }
        }
    }
    UnlockIBase(ilock);
    return found;
}

/* Snapshot the active window (or return NULL). */
static struct Window *spike_active_window(struct WinSnap *out)
{
    ULONG ilock;
    struct Window *win;

    ilock = LockIBase(0);
    win = ES_IBASE->ActiveWindow;
    if (win != NULL) {
        spike_fill_snapshot(win, win->WScreen, out);
    }
    UnlockIBase(ilock);
    return win;
}

/* ------------------------------------------------------- restore memory */

struct RestoreSlot {
    struct Window *win;
    ESRect box;
    int used;
};

static struct RestoreSlot g_restore[ES_RESTORE_SLOTS];

static struct RestoreSlot *spike_restore_find(struct Window *win)
{
    int i;

    for (i = 0; i < ES_RESTORE_SLOTS; i++) {
        if (g_restore[i].used && g_restore[i].win == win) {
            return &g_restore[i];
        }
    }
    return NULL;
}

static void spike_restore_remember(struct Window *win, const ESRect *box)
{
    struct RestoreSlot *slot;
    int i;

    if (spike_restore_find(win) != NULL) {
        return; /* keep the original pre-snap geometry */
    }
    slot = NULL;
    for (i = 0; i < ES_RESTORE_SLOTS; i++) {
        if (!g_restore[i].used) {
            slot = &g_restore[i];
            break;
        }
    }
    if (slot == NULL) {
        slot = &g_restore[0]; /* spike: just recycle */
    }
    slot->win = win;
    slot->box = *box;
    slot->used = 1;
}

/* ------------------------------------------------------------- snapping */

static int spike_window_snappable(const struct WinSnap *ws)
{
    if ((ws->flags & WFLG_SIZEGADGET) == 0) {
        return 0;
    }
    if (ws->flags & (WFLG_BACKDROP | WFLG_BORDERLESS)) {
        return 0;
    }
    return 1;
}

static void spike_apply_zone(struct Window *target, int zone)
{
    struct WinSnap ws;
    ESRect r;

    if (!spike_snapshot_window(target, &ws)) {
        printf("edgesnap: window %p vanished, not snapping\n",
               (void *)target);
        return;
    }
    if (!spike_window_snappable(&ws)) {
        printf("edgesnap: window %p not snappable "
               "(no size gadget, or backdrop/borderless)\n", (void *)target);
        return;
    }

    es_fit_zone_rect(zone, &ws.usable, ws.min_w, ws.min_h,
                     ws.max_w, ws.max_h, &r);
    spike_restore_remember(target, &ws.box);

    printf("edgesnap: snap %p -> %s (%d,%d %dx%d)\n", (void *)target,
           es_zone_name(zone), r.x, r.y, r.w, r.h);
    ChangeWindowBox(target, r.x, r.y, r.w, r.h);
}

static void spike_restore_window(struct Window *target)
{
    struct RestoreSlot *slot;
    struct WinSnap ws;

    slot = spike_restore_find(target);
    if (slot == NULL) {
        printf("edgesnap: no saved geometry for window %p\n",
               (void *)target);
        return;
    }
    if (!spike_snapshot_window(target, &ws)) {
        printf("edgesnap: window %p vanished, dropping saved geometry\n",
               (void *)target);
        slot->used = 0;
        return;
    }
    printf("edgesnap: restore %p -> (%d,%d %dx%d)\n", (void *)target,
           slot->box.x, slot->box.y, slot->box.w, slot->box.h);
    ChangeWindowBox(target, slot->box.x, slot->box.y,
                    slot->box.w, slot->box.h);
    slot->used = 0;
}

/* -------------------------------------------------------- zone preview */

/*
 * Aero/macOS-style "where will it land" feedback while dragging: a frame
 * of four thin borderless windows outlining the target rectangle.
 *
 * Why not a translucent window: WA_Opaqueness needs compositing, which the
 * QEMU sm501 has no chance of providing, and the MorphOS equivalent tag
 * differs. Four 4px bars work on any screen of both OSes, like the classic
 * outline drag of old. The translucent fill can become a compositing-only
 * upgrade later.
 *
 * The bars open non-activating and with no IDCMP, so they disturb neither
 * Intuition's drag nor our ActiveWindow-based heuristic; zone changes just
 * close and reopen them (cheap, and avoids async-resize repaint races).
 * With solid window dragging (the default on both OSes) opening windows
 * mid-drag is fine; with outline drag mode OpenWindow would block until
 * release, so the preview simply would not show - no harm.
 */

struct Preview {
    struct Window *bars[4];
    int visible;
    ESRect rect;
};

static struct Preview g_preview;

static int spike_is_preview_win(struct Window *w)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_preview.bars[i] == w) {
            return 1;
        }
    }
    return 0;
}

static void spike_preview_hide(void)
{
    int i;

    if (!g_preview.visible) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (g_preview.bars[i] != NULL) {
            CloseWindow(g_preview.bars[i]);
            g_preview.bars[i] = NULL;
        }
    }
    g_preview.visible = 0;
}

static struct Window *spike_preview_bar(struct Screen *scr, int pen,
                                        int x, int y, int w, int h)
{
    struct Window *win;

    win = OpenWindowTags(NULL,
                         WA_CustomScreen, scr,
                         WA_Left, x,
                         WA_Top, y,
                         WA_Width, w,
                         WA_Height, h,
                         WA_Flags, WFLG_BORDERLESS | WFLG_SMART_REFRESH |
                                   WFLG_NOCAREREFRESH,
                         WA_Activate, FALSE,
                         TAG_DONE);
    if (win != NULL) {
        SetAPen(win->RPort, pen);
        RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    }
    return win;
}

static void spike_preview_show(struct Screen *dragscr, const ESRect *r)
{
    struct Screen *scr;
    struct DrawInfo *dri;
    int pen = 3;
    int t = ES_FRAME_PX;
    int i, ok;
    ESRect bar[4];
    int bars;

    if (g_preview.visible &&
        g_preview.rect.x == r->x && g_preview.rect.y == r->y &&
        g_preview.rect.w == r->w && g_preview.rect.h == r->h) {
        return;
    }
    spike_preview_hide();

    /*
     * Only the default public screen for now: LockPubScreen guarantees it
     * stays alive while the bars open; visitor windows keep it open after
     * the unlock. A drag on another screen just gets no preview.
     */
    scr = LockPubScreen(NULL);
    if (scr == NULL) {
        return;
    }
    if (scr != dragscr) {
        UnlockPubScreen(NULL, scr);
        return;
    }

    dri = GetScreenDrawInfo(scr);
    if (dri != NULL) {
        if (dri->dri_NumPens > FILLPEN) {
            pen = dri->dri_Pens[FILLPEN];
        }
        FreeScreenDrawInfo(scr, dri);
    }

    if (r->w < 2 * t || r->h < 2 * t) {
        t = 1;
    }
    bar[0].x = r->x;
    bar[0].y = r->y;
    bar[0].w = r->w;
    bar[0].h = t;
    bar[1].x = r->x;
    bar[1].y = r->y + r->h - t;
    bar[1].w = r->w;
    bar[1].h = t;
    bar[2].x = r->x;
    bar[2].y = r->y + t;
    bar[2].w = t;
    bar[2].h = r->h - 2 * t;
    bar[3].x = r->x + r->w - t;
    bar[3].y = r->y + t;
    bar[3].w = t;
    bar[3].h = r->h - 2 * t;
    bars = (bar[2].h > 0) ? 4 : 2;

    ok = 1;
    for (i = 0; i < bars; i++) {
        g_preview.bars[i] = spike_preview_bar(scr, pen, bar[i].x, bar[i].y,
                                              bar[i].w, bar[i].h);
        if (g_preview.bars[i] == NULL) {
            ok = 0;
        }
    }
    g_preview.visible = 1; /* set before a possible hide on failure */
    g_preview.rect = *r;
    UnlockPubScreen(NULL, scr);
    if (!ok) {
        spike_preview_hide();
    }
}

/* ---------------------------------------------------- drag state machine */

struct DragState {
    int button_down;
    int dragging;
    struct Window *candidate;
    ESRect base_box;
    int base_mx, base_my;
    int zone;
};

static struct DragState g_drag;

static ULONG g_seen_presses;
static ULONG g_seen_releases;
static ULONG g_seen_moves;

static void spike_drag_reset(void)
{
    g_drag.button_down = 0;
    g_drag.dragging = 0;
    g_drag.candidate = NULL;
    g_drag.zone = ES_ZONE_NONE;
    spike_preview_hide();
}

static void spike_engine_step(void)
{
    int new_press, new_move, new_release;
    struct WinSnap ws;
    struct Window *win;

    new_press = (g_shared.presses != g_seen_presses);
    new_move = (g_shared.moves != g_seen_moves);
    new_release = (g_shared.releases != g_seen_releases);
    g_seen_presses = g_shared.presses;
    g_seen_moves = g_shared.moves;
    g_seen_releases = g_shared.releases;

    if (new_press) {
        spike_drag_reset();
        g_drag.button_down = 1;
    }

    if (new_move && g_drag.button_down && !new_release) {
        win = spike_active_window(&ws);
        if (win == NULL) {
            g_drag.candidate = NULL;
        } else if (g_drag.candidate != win) {
            /* first sight of this window during the press: baseline it */
            g_drag.candidate = win;
            g_drag.base_box = ws.box;
            g_drag.base_mx = ws.mouse_x;
            g_drag.base_my = ws.mouse_y;
            g_drag.dragging = 0;
        } else {
            if (!g_drag.dragging) {
                int win_moved = (ws.box.x != g_drag.base_box.x ||
                                 ws.box.y != g_drag.base_box.y);
                int ptr_moved =
                    (abs(ws.mouse_x - g_drag.base_mx) +
                     abs(ws.mouse_y - g_drag.base_my)) >= ES_DRAG_MIN_PX;
                if (win_moved && ptr_moved &&
                    (ws.flags & WFLG_DRAGBAR) != 0) {
                    g_drag.dragging = 1;
                    printf("edgesnap: drag detected on window %p\n",
                           (void *)win);
                }
            }
            if (g_drag.dragging) {
                int z = es_zone_from_pointer(&ws.usable, ws.mouse_x,
                                             ws.mouse_y, ES_EDGE_PX,
                                             ws.usable.h / ES_CORNER_DIV);
                if (z != g_drag.zone) {
                    g_drag.zone = z;
                    printf("edgesnap: zone -> %s\n", es_zone_name(z));
                    if (z != ES_ZONE_NONE && spike_window_snappable(&ws)) {
                        ESRect target;
                        es_fit_zone_rect(z, &ws.usable, ws.min_w, ws.min_h,
                                         ws.max_w, ws.max_h, &target);
                        spike_preview_show(ws.scr, &target);
                    } else {
                        spike_preview_hide();
                    }
                }
            }
        }
    }

    if (new_release) {
        spike_preview_hide(); /* frame gone before the snap lands */
        if (g_drag.dragging && g_drag.zone != ES_ZONE_NONE &&
            g_drag.candidate != NULL) {
            spike_apply_zone(g_drag.candidate, g_drag.zone);
        }
        spike_drag_reset();
    }
}

/* ------------------------------------------------------- diagnostics */

/*
 * ctrl alt d: dump every window of the active screen with geometry,
 * flags and the panel-policy verdict, plus the resulting usable area.
 * This is the ground-truth probe for tuning dock detection on real
 * systems: collect under LockIBase, print after unlocking.
 */

#define ES_DUMP_MAX 24

struct WinDumpItem {
    ESRect box;
    ULONG flags;
    char title[28];
    int skipped;  /* 0 candidate, 1 ours, 2 excluded by flags */
    int edge;     /* es_panel_classify() when candidate         */
};

static const char *spike_edge_name(int e)
{
    switch (e) {
    case ES_PEDGE_LEFT:
        return "left";
    case ES_PEDGE_RIGHT:
        return "right";
    case ES_PEDGE_TOP:
        return "top";
    case ES_PEDGE_BOTTOM:
        return "bottom";
    default:
        return "-";
    }
}

static void spike_dump_windows(void)
{
    struct WinDumpItem items[ES_DUMP_MAX];
    ESRect scrrect, usable;
    ESInsets ins;
    ULONG ilock;
    struct Screen *scr;
    struct Window *w;
    int n = 0, scrw = 0, scrh = 0, bar = 0, i;

    usable.x = usable.y = usable.w = usable.h = 0;
    ins.l = ins.t = ins.r = ins.b = 0;

    ilock = LockIBase(0);
    scr = ES_IBASE->ActiveScreen;
    if (scr != NULL) {
        scrw = scr->Width;
        scrh = scr->Height;
        bar = scr->BarHeight;
        scrrect.x = 0;
        scrrect.y = 0;
        scrrect.w = scrw;
        scrrect.h = scrh;
        for (w = scr->FirstWindow; w != NULL && n < ES_DUMP_MAX;
             w = w->NextWindow) {
            struct WinDumpItem *it = &items[n];
            const char *src;
            int t = 0;

            it->box.x = w->LeftEdge;
            it->box.y = w->TopEdge;
            it->box.w = w->Width;
            it->box.h = w->Height;
            it->flags = w->Flags;
            src = (const char *)w->Title;
            if (src != NULL) {
                for (; t < 27 && src[t] != '\0'; t++) {
                    it->title[t] = src[t];
                }
            }
            it->title[t] = '\0';
            if (spike_is_preview_win(w)) {
                it->skipped = 1;
            } else if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET |
                                    WFLG_BACKDROP)) != 0) {
                it->skipped = 2;
            } else {
                it->skipped = 0;
            }
            it->edge = (it->skipped == 0) ?
                es_panel_classify(&scrrect, &it->box) : ES_PEDGE_NONE;
            n++;
        }
        spike_usable_area(scr, NULL, &usable, &ins);
    }
    UnlockIBase(ilock);

    if (scr == NULL) {
        printf("edgesnap: dump: no active screen\n");
        return;
    }
    printf("edgesnap: --- window dump, screen %dx%d barheight %d ---\n",
           scrw, scrh, bar);
    for (i = 0; i < n; i++) {
        struct WinDumpItem *it = &items[i];
        const char *verdict;

        if (it->skipped == 1) {
            verdict = "ours";
        } else if (it->skipped == 2) {
            verdict = "skip:flags";
        } else if (it->edge != ES_PEDGE_NONE) {
            verdict = "PANEL";
        } else {
            verdict = "no:geometry";
        }
        printf("edgesnap: %4d,%4d %4dx%4d flags %08lx %-11s %-6s \"%s\"\n",
               it->box.x, it->box.y, it->box.w, it->box.h,
               (unsigned long)it->flags, verdict,
               spike_edge_name(it->edge), it->title);
    }
    printf("edgesnap: insets l=%d t=%d r=%d b=%d -> usable %d,%d %dx%d\n",
           ins.l, ins.t, ins.r, ins.b,
           usable.x, usable.y, usable.w, usable.h);
    fflush(stdout);
}

/* ------------------------------------------------------------ commodity */

#define HK_SNAP_LEFT   1
#define HK_SNAP_RIGHT  2
#define HK_SNAP_MAX    3
#define HK_RESTORE     4
#define HK_DUMP        5

static int spike_add_hotkey(CxObj *broker, struct MsgPort *port,
                            STRPTR descr, LONG id)
{
    CxObj *filter;

    filter = CxFilter(descr);
    if (filter == NULL) {
        return 0;
    }
    AttachCxObj(filter, CxSender(port, id));
    AttachCxObj(filter, CxTranslate(NULL));
    if (CxObjError(filter) != 0) {
        DeleteCxObjAll(filter);
        return 0;
    }
    AttachCxObj(broker, filter);
    return 1;
}

static void spike_handle_hotkey(LONG id)
{
    struct WinSnap ws;
    struct Window *win;

    if (id == HK_DUMP) {
        spike_dump_windows();
        return;
    }
    win = spike_active_window(&ws);
    if (win == NULL) {
        printf("edgesnap: no active window\n");
        return;
    }
    switch (id) {
    case HK_SNAP_LEFT:
        spike_apply_zone(win, ES_ZONE_LEFT);
        break;
    case HK_SNAP_RIGHT:
        spike_apply_zone(win, ES_ZONE_RIGHT);
        break;
    case HK_SNAP_MAX:
        spike_apply_zone(win, ES_ZONE_MAX);
        break;
    case HK_RESTORE:
        spike_restore_window(win);
        break;
    default:
        break;
    }
}

static void spike_close_libs(void)
{
#ifdef __amigaos4__
    if (ICommodities != NULL) {
        DropInterface((struct Interface *)ICommodities);
        ICommodities = NULL;
    }
    if (IGraphics != NULL) {
        DropInterface((struct Interface *)IGraphics);
        IGraphics = NULL;
    }
    if (IIntuition != NULL) {
        DropInterface((struct Interface *)IIntuition);
        IIntuition = NULL;
    }
#endif
    if (CxBase != NULL) {
        CloseLibrary(CxBase);
        CxBase = NULL;
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static int spike_open_libs(void)
{
    IntuitionBase = (void *)OpenLibrary("intuition.library", 36);
    GfxBase = (void *)OpenLibrary("graphics.library", 36);
    CxBase = OpenLibrary("commodities.library", 37);
    if (IntuitionBase == NULL || GfxBase == NULL || CxBase == NULL) {
        spike_close_libs();
        return 0;
    }
#ifdef __amigaos4__
    IIntuition = (struct IntuitionIFace *)
        GetInterface((struct Library *)IntuitionBase, "main", 1, NULL);
    IGraphics = (struct GraphicsIFace *)
        GetInterface((struct Library *)GfxBase, "main", 1, NULL);
    ICommodities = (struct CommoditiesIFace *)
        GetInterface(CxBase, "main", 1, NULL);
    if (IIntuition == NULL || IGraphics == NULL || ICommodities == NULL) {
        spike_close_libs();
        return 0;
    }
#endif
    return 1;
}

int main(void)
{
    struct MsgPort *port = NULL;
    CxObj *broker = NULL;
    CxObj *custom;
    struct NewBroker nb;
    LONG broker_err = 0;
    BYTE engine_sig = -1;
    ULONG port_mask, engine_mask;
    int running = 1;
    int rc = RETURN_FAIL;

    (void)es_stack_cookie;

    if (!spike_open_libs()) {
        printf("edgesnap: need intuition/graphics.library 36 and "
               "commodities.library 37\n");
        return RETURN_FAIL;
    }

    port = CreateMsgPort();
    if (port == NULL) {
        goto out;
    }
    engine_sig = AllocSignal(-1);
    if (engine_sig == -1) {
        goto out;
    }

    memset(&nb, 0, sizeof(nb));
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (STRPTR)"EdgeSnap";
    nb.nb_Title = (STRPTR)"EdgeSnap spike 0.1";
    nb.nb_Descr = (STRPTR)"Drag windows to screen edges to tile them";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Pri = 0;
    nb.nb_Port = port;

    broker = CxBroker(&nb, &broker_err);
    if (broker == NULL) {
        printf("edgesnap: CxBroker failed (%ld)%s\n", (long)broker_err,
               broker_err == CBERR_DUP ? " - already running" : "");
        goto out;
    }

    custom = CxCustom(ES_CX_ACTION, 0);
    AttachCxObj(broker, custom);
    if (!spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_left",
                          HK_SNAP_LEFT) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_right",
                          HK_SNAP_RIGHT) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_up",
                          HK_SNAP_MAX) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_down",
                          HK_RESTORE) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt d", HK_DUMP) ||
        CxObjError(broker) != 0) {
        printf("edgesnap: could not build the commodity tree\n");
        goto out;
    }

    g_shared.engine_task = FindTask(NULL);
    g_shared.engine_sigmask = 1UL << engine_sig;
    spike_drag_reset();

    ActivateCxObj(broker, 1L);

    printf("edgesnap spike 0.1 running (commodity \"EdgeSnap\").\n");
    printf("  drag a window's title bar until the pointer touches a\n");
    printf("  screen edge or corner, then release.\n");
    printf("  hotkeys: ctrl alt cursor left/right/up = snap, down = "
           "restore,\n");
    printf("           ctrl alt d = window dump (dock diagnosis).\n");
    printf("  quit: Ctrl-C here, or remove it from Exchange.\n");
    spike_dump_windows();

    port_mask = 1UL << port->mp_SigBit;
    engine_mask = g_shared.engine_sigmask;

    while (running) {
        ULONG sigs = Wait(port_mask | engine_mask | SIGBREAKF_CTRL_C);

        if (sigs & port_mask) {
            CxMsg *msg;
            while ((msg = (CxMsg *)GetMsg(port)) != NULL) {
                ULONG type = CxMsgType(msg);
                LONG id = CxMsgID(msg);
                ReplyMsg((struct Message *)msg);
                if (type == CXM_IEVENT) {
                    spike_handle_hotkey(id);
                } else if (type == CXM_COMMAND) {
                    switch (id) {
                    case CXCMD_KILL:
                        running = 0;
                        break;
                    case CXCMD_DISABLE:
                        ActivateCxObj(broker, 0L);
                        spike_drag_reset();
                        break;
                    case CXCMD_ENABLE:
                        ActivateCxObj(broker, 1L);
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        if (sigs & engine_mask) {
            spike_engine_step();
        }
        if (sigs & SIGBREAKF_CTRL_C) {
            running = 0;
        }
    }

    rc = RETURN_OK;
    printf("edgesnap: shutting down\n");

out:
    if (broker != NULL) {
        ActivateCxObj(broker, 0L);
        DeleteCxObjAll(broker);
    }
    spike_preview_hide();
    if (port != NULL) {
        CxMsg *msg;
        while ((msg = (CxMsg *)GetMsg(port)) != NULL) {
            ReplyMsg((struct Message *)msg);
        }
        DeleteMsgPort(port);
    }
    if (engine_sig != -1) {
        FreeSignal(engine_sig);
    }
    spike_close_libs();
    return rc;
}
