/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_cx.c - the EdgeSnap reference commodity (phase 2).
 *
 * One source for AmigaOS 4.x and MorphOS. ALL drag/snap decisions live
 * in the shared library kernel (core/engine.c state machine,
 * core/registry.c stale-safe restore): this file is glue, and must
 * never grow a second snapping engine (docs/LLM_GUIDANCE.md). It:
 *
 *   - watches raw mouse events from a commodities CxCustom object
 *     (input.device context: counters + Signal() only);
 *   - feeds the kernel instantaneous window facts sampled under
 *     LockIBase (usable area is dock/panel aware via core/panels.c);
 *   - executes the kernel's emitted actions: zone preview (MorphOS:
 *     borderless frame windows; OS4: XOR rectangles on the screen
 *     rastport - OpenWindow during an OS4 ghost-drag is unreliable),
 *     and snaps via ChangeWindowBox with re-validation of possibly
 *     stale window refs;
 *   - provides hotkeys through the same snap path the future
 *     ESnap_SnapWindow() will use, Exchange integration, and a
 *     window-dump diagnostic (ctrl alt d).
 *
 * Field-proven rules encoded here (see docs/DESIGN.md spike findings):
 * the engine task never blocks on console I/O (buffered log, flushed
 * only when no drag is in flight), and everything Intuition-facing
 * runs in this task, never in the input handler.
 */

#ifdef __amigaos4__
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <stdarg.h>
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
#include <graphics/view.h>     /* OBP_Precision/PRECISION_GUI          */

#include <proto/dos.h>
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
#include "engine.h"
#include "registry.h"
#include "config.h"
#include "edgesnap_body.h"

/* ---------------------------------------------------------------- tuning */

/* Drag/zone tuning lives in the kernel (ESEngineConfig defaults). */
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
    /*
     * Written by us, read by the input handler. engine_task is cleared
     * BEFORE the commodity tree is torn down (see the shutdown
     * protocol in main): an action already in flight must never signal
     * a task that is about to die.
     */
    struct Task * volatile engine_task;
    volatile ULONG armed;   /* 0 = the action must do nothing at all */
    ULONG engine_sigmask;
    volatile ULONG presses;
    volatile ULONG releases;
    volatile ULONG moves;
    volatile ULONG quals;   /* latest qualifier bits seen with the mouse */
};

static struct SpikeShared g_shared;

/*
 * CxCustom action. RULES: no Intuition calls, no allocations, no waiting.
 * Bump a counter, Signal() the main task, get out.
 */
static void spike_cx_action(CxMsg *msg, CxObj *obj)
{
    struct InputEvent *ie;
    struct Task *engine;

    (void)obj;
    if (!g_shared.armed) {
        return; /* shutting down: touch nothing */
    }
    if (CxMsgType(msg) != CXM_IEVENT) {
        return;
    }
    ie = (struct InputEvent *)CxMsgData(msg);
    if (ie == NULL || ie->ie_Class != IECLASS_RAWMOUSE) {
        return;
    }
    g_shared.quals = ie->ie_Qualifier;
    if (ie->ie_Code == IECODE_LBUTTON) {
        g_shared.presses++;
    } else if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
        g_shared.releases++;
    } else if (ie->ie_Code == IECODE_NOBUTTON) {
        g_shared.moves++;
    } else {
        return;
    }
    /* one read: our task may be clearing this pointer right now */
    engine = g_shared.engine_task;
    if (engine != NULL && g_shared.armed) {
        Signal(engine, g_shared.engine_sigmask);
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

/* --------------------------------------------------- non-blocking log */

/*
 * Field lesson (OS4, 2026-08-26): the engine once printf'd straight to
 * its shell console. Dragging THAT shell window freezes console output
 * on OS4, so the first zone print blocked the engine for the rest of
 * the drag: zones went stale and the preview never opened. Engine-path
 * messages land here and reach the console only when no drag is in
 * flight. The library phase inherits the rule: the engine never blocks
 * on I/O.
 */

#define ES_LOG_LINES 64
#define ES_LOG_CHARS 160

static char g_log_buf[ES_LOG_LINES][ES_LOG_CHARS];
static int g_log_count;
static int g_log_dropped;

static void spike_log(const char *fmt, ...)
{
    va_list ap;

    if (g_log_count >= ES_LOG_LINES) {
        g_log_dropped++;
        return;
    }
    va_start(ap, fmt);
    vsprintf(g_log_buf[g_log_count], fmt, ap); /* short, fixed formats */
    va_end(ap);
    g_log_count++;
}

static void spike_log_flush(void)
{
    int i;

    for (i = 0; i < g_log_count; i++) {
        fputs(g_log_buf[i], stdout);
    }
    if (g_log_dropped > 0) {
        printf("edgesnap: (%d log lines dropped)\n", g_log_dropped);
    }
    if (g_log_count > 0 || g_log_dropped > 0) {
        fflush(stdout);
    }
    g_log_count = 0;
    g_log_dropped = 0;
}

/* ---------------------------------------------------------- preferences */

/*
 * Preferences live in the portable core (core/config.c): one KEY=VALUE
 * parser serves both sources, because on Amiga they have the same
 * shape - lines of an ENV(ARC): file and, later, Workbench tooltypes.
 * This glue only reads bytes and reports what the parser rejected; a
 * bad line never aborts the load, so a truncated prefs file cannot
 * leave the user without snapping.
 */

#define ES_PREFS_ENV    "ENV:EdgeSnap.prefs"
#define ES_PREFS_ENVARC "ENVARC:EdgeSnap.prefs"

static ESConfig g_cfg;

static void spike_config_line(const char *line, const char *src)
{
    int rc = es_config_line(&g_cfg, line);

    if (rc == ES_ERR_UNSUPPORTED) {
        printf("edgesnap: %s: unknown setting: %s\n", src, line);
    } else if (rc != ES_OK) {
        printf("edgesnap: %s: bad value: %s\n", src, line);
    }
}

static int spike_config_load_file(const char *path)
{
    BPTR fh;
    char line[160];

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        return 0;
    }
    while (FGets(fh, (STRPTR)line, (unsigned long)sizeof(line)) != 0) {
        spike_config_line(line, path);
    }
    Close(fh);
    return 1;
}

/* ENV: holds the live settings, ENVARC: the archived ones: prefer the
 * live copy, like every other Amiga preferences client. */
static void spike_config_load(int argc, char **argv)
{
    int i;

    es_config_defaults(&g_cfg);
    if (!spike_config_load_file(ES_PREFS_ENV)) {
        spike_config_load_file(ES_PREFS_ENVARC);
    }
    /* Shell arguments override the file: KEY=VALUE, same vocabulary. */
    for (i = 1; i < argc; i++) {
        spike_config_line(argv[i], "argument");
    }
}

/* ------------------------------------------- library-side declarations */

/* The preview frame must not be mistaken for a dock by the panel scan
 * (it is thin, edge-flush and long - exactly a dock's shape), so the
 * frontend hands its frame windows to the library; defined with the
 * preview below. */
static int spike_is_preview_win(struct Window *w);
static void spike_publish_ignored(void);

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
    /* accent pen obtained per preview session; the colormap pointer is
     * only used between show and hide (a drag lasts seconds, and the
     * screen cannot close while windows live on it). */
    struct ColorMap *pen_cm;
    LONG pen;
    int pen_obtained;
};

static struct Preview g_preview;

/* Tell the library which windows its panel scan must ignore: our own
 * frame bars have exactly a dock's shape. The OS4 frame is drawn XOR
 * on the screen, so there is nothing to ignore there. */
static void spike_publish_ignored(void)
{
#ifdef __amigaos4__
    esb_ignore_windows(NULL, 0);
#else
    esb_ignore_windows(g_preview.bars, 4);
#endif
}

#ifdef __amigaos4__
/*
 * OS4 field finding (2026-08-26): while Intuition ghost-drags a window,
 * opening our preview windows is unreliable (OpenWindow can block on
 * the drag's internal locks until release - frame never seen). So on
 * OS4 the frame is drawn the way Intuition draws its own drag feedback:
 * XOR (COMPLEMENT) rectangles straight on the screen's RastPort. No
 * layers, no locks, and drawing the same frame twice erases it. The
 * public screen stays locked between draw and erase so it cannot close
 * under us. Cosmetic caveat, accepted for the spike: content repainted
 * beneath a drawn frame (the moving ghost itself) can leave brief
 * artifacts on erase.
 */
static struct PreviewXor {
    int drawn;
    ESRect rect;
    struct Screen *scr;   /* pubscreen-locked while drawn */
} g_pxor;

static void spike_pxor_frame(struct Screen *scr, const ESRect *r)
{
    struct RastPort rp;
    int t = ES_FRAME_PX;

    rp = scr->RastPort; /* private copy: leave the screen rastport alone */
    SetDrMd(&rp, COMPLEMENT);
    if (r->w < 2 * t || r->h < 2 * t) {
        t = 1;
    }
    RectFill(&rp, r->x, r->y, r->x + r->w - 1, r->y + t - 1);
    RectFill(&rp, r->x, r->y + r->h - t, r->x + r->w - 1, r->y + r->h - 1);
    if (r->h > 2 * t) {
        RectFill(&rp, r->x, r->y + t, r->x + t - 1, r->y + r->h - t - 1);
        RectFill(&rp, r->x + r->w - t, r->y + t, r->x + r->w - 1,
                 r->y + r->h - t - 1);
    }
}

static void spike_pxor_hide(void)
{
    if (!g_pxor.drawn) {
        return;
    }
    spike_pxor_frame(g_pxor.scr, &g_pxor.rect); /* XOR again = erase */
    UnlockPubScreen(NULL, g_pxor.scr);
    g_pxor.drawn = 0;
    g_pxor.scr = NULL;
}

static void spike_pxor_show(struct Screen *dragscr, const ESRect *r)
{
    struct Screen *scr;

    if (g_pxor.drawn &&
        g_pxor.rect.x == r->x && g_pxor.rect.y == r->y &&
        g_pxor.rect.w == r->w && g_pxor.rect.h == r->h) {
        return;
    }
    spike_pxor_hide();
    scr = LockPubScreen(NULL);
    if (scr == NULL) {
        spike_log("edgesnap: preview: no default public screen\n");
        return;
    }
    if (scr != dragscr) {
        spike_log("edgesnap: preview: drag is not on the default public "
                  "screen, no preview\n");
        UnlockPubScreen(NULL, scr);
        return;
    }
    spike_pxor_frame(scr, r);
    g_pxor.drawn = 1;
    g_pxor.rect = *r;
    g_pxor.scr = scr; /* keep the pubscreen lock until hide */
}
#endif /* __amigaos4__ */

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
#ifdef __amigaos4__
    spike_pxor_hide();
#else
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
    if (g_preview.pen_obtained) {
        ReleasePen(g_preview.pen_cm, (ULONG)g_preview.pen);
        g_preview.pen_obtained = 0;
    }
    g_preview.visible = 0;
    spike_publish_ignored();
#endif
}

#ifndef __amigaos4__
static struct Window *spike_preview_bar(struct Screen *scr, LONG pen,
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
        SetAPen(win->RPort, (ULONG)pen);
        RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    }
    return win;
}
#endif /* !__amigaos4__ */

static void spike_preview_show(struct Screen *dragscr, const ESRect *r)
{
#ifdef __amigaos4__
    spike_pxor_show(dragscr, r);
#else
    struct Screen *scr;
    struct DrawInfo *dri;
    LONG pen = 3;
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
        spike_log("edgesnap: preview: no default public screen\n");
        return;
    }
    if (scr != dragscr) {
        spike_log("edgesnap: preview: drag is not on the default public "
                  "screen, no preview\n");
        UnlockPubScreen(NULL, scr);
        return;
    }

    /*
     * Frame color: an explicit azure accent via ObtainBestPen - exact on
     * the 32-bit screens both OSes run. The old FILLPEN choice was
     * invisible on OS4: its theme resolves FILLPEN to a gray nearly
     * identical to the Workbench background (field finding 2026-08-26).
     */
    g_preview.pen_cm = scr->ViewPort.ColorMap;
    pen = ObtainBestPen(g_preview.pen_cm,
                        0x22222222UL, 0x88888888UL, 0xFFFFFFFFUL,
                        OBP_Precision, PRECISION_GUI,
                        TAG_DONE);
    if (pen != -1) {
        g_preview.pen_obtained = 1;
        g_preview.pen = pen;
    } else {
        pen = 3;
        dri = GetScreenDrawInfo(scr);
        if (dri != NULL) {
            if (dri->dri_NumPens > FILLPEN) {
                pen = dri->dri_Pens[FILLPEN];
            }
            FreeScreenDrawInfo(scr, dri);
        }
        spike_log("edgesnap: preview: ObtainBestPen failed, fallback "
                  "pen %ld\n", (long)pen);
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
    spike_publish_ignored();
    if (!ok) {
        spike_log("edgesnap: preview: OpenWindow failed, no frame\n");
        spike_preview_hide();
    }
#endif
}

/* ------------------------------------------------ kernel glue (phase 2) */

static ULONG g_seen_presses;
static ULONG g_seen_releases;
static ULONG g_seen_moves;

/*
 * The frontend contributes raw input facts and draws the frame; every
 * decision (drag detection, zones, snapping, restore) happens inside
 * the library body. A library must not print, so it reports what it
 * did and we log it here.
 */
static void spike_apply_report(const ESBReport *r)
{
    if (r->preview_hide) {
        spike_preview_hide();
    }
    if (r->drag_started) {
        spike_log("edgesnap: drag detected\n");
    }
    if (r->zone_changed) {
        spike_log("edgesnap: zone -> %s\n", es_zone_name(r->zone));
    }
    if (r->preview_show && g_cfg.preview && r->preview_screen != NULL) {
        spike_preview_show(r->preview_screen, &r->preview_rect);
    }
    if (r->snapped) {
        if (r->snap_rc == ES_OK) {
            spike_log("edgesnap: snap %p -> %s\n", (void *)r->snap_win,
                      es_zone_name(r->snap_zone));
        } else {
            spike_log("edgesnap: snap %p refused (%ld)\n",
                      (void *)r->snap_win, (long)r->snap_rc);
        }
    }
}

static void spike_engine_step(void)
{
    int new_press, new_move, new_release;
    ESBReport r;

    new_press = (g_shared.presses != g_seen_presses);
    new_move = (g_shared.moves != g_seen_moves);
    new_release = (g_shared.releases != g_seen_releases);
    g_seen_presses = g_shared.presses;
    g_seen_moves = g_shared.moves;
    g_seen_releases = g_shared.releases;

    esb_input(new_press, new_move, new_release, g_shared.quals, &r);
    spike_apply_report(&r);

    /* console output only when no drag is in flight (see spike_log) */
    if (!esb_drag_active()) {
        spike_log_flush();
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
        esb_debug_usable(scr, &usable, &ins);
    }
    UnlockIBase(ilock);

    spike_log_flush();
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

/* Active window, for the hotkeys: the library re-validates it anyway. */
static struct Window *spike_active_window(void)
{
    ULONG ilock;
    struct Window *win;

    ilock = LockIBase(0);
    win = ES_IBASE->ActiveWindow;
    UnlockIBase(ilock);
    return win;
}

static void spike_handle_hotkey(LONG id)
{
    struct Window *win;
    LONG rc = ES_OK;

    if (id == HK_DUMP) {
        spike_dump_windows();
        return;
    }
    win = spike_active_window();
    if (win == NULL) {
        printf("edgesnap: no active window\n");
        return;
    }
    switch (id) {
    case HK_SNAP_LEFT:
        rc = esb_snap_window(win, ES_ZONE_LEFT);
        break;
    case HK_SNAP_RIGHT:
        rc = esb_snap_window(win, ES_ZONE_RIGHT);
        break;
    case HK_SNAP_MAX:
        rc = esb_snap_window(win, ES_ZONE_MAX);
        break;
    case HK_RESTORE:
        rc = esb_unsnap_window(win);
        break;
    default:
        return;
    }
    spike_log_flush();
    if (rc != ES_OK) {
        printf("edgesnap: hotkey refused (%ld)\n", (long)rc);
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

int main(int argc, char **argv)
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
    spike_config_load(argc, argv);

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
    nb.nb_Title = (STRPTR)"EdgeSnap 0.2";
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
    g_shared.armed = 1;
    if (!esb_init()) {
        printf("edgesnap: library body failed to initialise\n");
        goto out;
    }
    esb_set_config(&g_cfg);
    spike_publish_ignored();

    ActivateCxObj(broker, 1L);

    printf("EdgeSnap 0.2 (build " __DATE__ " " __TIME__ ") "
           "running (commodity \"EdgeSnap\").\n");
    printf("  drag a window's title bar until the pointer touches a\n");
    printf("  screen edge or corner, then release.\n");
    printf("  hotkeys: ctrl alt cursor left/right/up = snap, down = "
           "restore,\n");
    printf("           ctrl alt d = window dump (dock diagnosis).\n");
    printf("  quit: Ctrl-C here, or remove it from Exchange.\n");
    printf("edgesnap: prefs: zones %04x, edge %d px, corner 1/%d, "
           "drag %d px,\n", (unsigned)g_cfg.engine.zones_mask,
           g_cfg.engine.edge_px, g_cfg.engine.corner_div,
           g_cfg.engine.drag_min_px);
    printf("edgesnap:        preview %s, panel detect %s (margin %d), "
           "bypass %s,\n", g_cfg.preview ? "on" : "off",
           g_cfg.panel_detect ? "on" : "off", g_cfg.panel_margin,
           g_cfg.bypass_qual == ES_QUAL_ALT ? "alt" :
           g_cfg.bypass_qual == ES_QUAL_CTRL ? "ctrl" :
           g_cfg.bypass_qual == ES_QUAL_SHIFT ? "shift" : "none");
    printf("edgesnap:        margins l%d t%d r%d b%d "
           "(file: " ES_PREFS_ENV ")\n", g_cfg.margin.l, g_cfg.margin.t,
           g_cfg.margin.r, g_cfg.margin.b);
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
                        {
                            ESBReport r;

                            esb_enable(FALSE);
                            esb_input_reset(&r);
                            spike_apply_report(&r);
                        }
                        spike_log_flush();
                        break;
                    case CXCMD_ENABLE:
                        ActivateCxObj(broker, 1L);
                        esb_enable(TRUE);
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
    spike_log_flush();
    printf("edgesnap: shutting down\n");

out:
    /*
     * Shutdown protocol - the order is the whole point. Our CxCustom
     * action runs in input.device context and may be executing at this
     * very moment:
     *
     *   1. deactivate the broker, so no new event enters our tree;
     *   2. disarm the action and drop the task pointer WHILE WE ARE
     *      STILL ALIVE, so a call already in flight becomes a no-op
     *      and can never Signal a dying task;
     *   3. give that in-flight call time to return, because on OS4 the
     *      code containing it is unloaded when we exit;
     *   4. only then delete the objects and free everything.
     *
     * Skipping step 2 leaves a Signal() aimed at freed memory - the
     * classic way to wedge commodities for every later client, which
     * is what the restart hang looked like from the outside.
     * edgesnap.library inherits this protocol.
     */
    if (broker != NULL) {
        ActivateCxObj(broker, 0L);
    }
    g_shared.armed = 0;
    g_shared.engine_task = NULL;
    if (broker != NULL) {
        Delay(2L); /* ~40 ms: let any in-flight action return */
        DeleteCxObjAll(broker);
    }
    spike_preview_hide();
    esb_cleanup();
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
