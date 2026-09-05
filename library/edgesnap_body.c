/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_body.c - the one implementation behind the public API.
 *
 * Everything that decides or remembers lives here: the engine, the snap
 * registry, exclusions, options, window validation and the dock-aware
 * usable area. Frontends contribute only what is theirs - raw input
 * facts and the drawing of the preview frame - so there can never be a
 * second snapping engine (docs/LLM_GUIDANCE.md).
 *
 * Locking: one semaphore guards the state; LockIBase is taken inside
 * it, briefly, and only to read. Nothing here may run in input.device
 * context.
 *
 * Library bases: the wrapper owns them (the platform skeleton opens
 * them at library init, the commodity at startup) exactly as the SDK
 * expects, so this file just uses them.
 */

#ifdef __amigaos4__
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <exec/types.h>
#include <exec/semaphores.h>
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <intuition/intuitionbase.h>

#include <proto/exec.h>
#include <proto/intuition.h>
#ifdef __AROS__
#include <proto/dos.h>
#endif

#include "edgesnap.h"
#include "edgesnap_body.h"
#include "panels.h"
#include "gutter.h"
#include "zones.h"

/* OS4 declares IntuitionBase as struct Library *; classic/MorphOS as
 * struct IntuitionBase *. Same alias trick as the frontend. */
#define ESB_IBASE ((struct IntuitionBase *)IntuitionBase)

#define ESB_EXCLUDE_SLOTS 16

static struct SignalSemaphore g_sem;
static ESConfig g_cfg;
static ESEngine g_engine;
static ESRegistry g_registry;
static void *g_excluded[ESB_EXCLUDE_SLOTS];
static struct Window *g_ignored[ESB_IGNORE_SLOTS];
static int g_enabled;
static int g_ready;

/* Facts sampled from a live window; ESB internal. */
struct ESBSnap {
    struct Window *win;
    struct Screen *scr;
    ESRect box;
    ESRect usable;
    int min_w, min_h;
    int max_w, max_h;
    int mouse_x, mouse_y;
    int bar_h;
    ULONG flags;
};

int esb_init(void)
{
    if (g_ready) {
        return 1;
    }
    InitSemaphore(&g_sem);
    es_config_defaults(&g_cfg);
    es_engine_init(&g_engine, &g_cfg.engine);
    es_registry_init(&g_registry);
    {
        int i;

        for (i = 0; i < ESB_EXCLUDE_SLOTS; i++) {
            g_excluded[i] = 0;
        }
        for (i = 0; i < ESB_IGNORE_SLOTS; i++) {
            g_ignored[i] = NULL;
        }
    }
    g_enabled = 1;
    g_ready = 1;
    return 1;
}

void esb_cleanup(void)
{
    if (!g_ready) {
        return;
    }
    ObtainSemaphore(&g_sem);
    es_registry_init(&g_registry);
    g_enabled = 0;
    ReleaseSemaphore(&g_sem);
    g_ready = 0;
}

/* ------------------------------------------------------- exclusions */

static int esb_is_excluded(struct Window *win)
{
    int i;

    for (i = 0; i < ESB_EXCLUDE_SLOTS; i++) {
        if (g_excluded[i] == (void *)win) {
            return 1;
        }
    }
    return 0;
}

/* ------------------------------------------------- window sampling */

/* Under LockIBase only: usable area of scr, dock/panel aware. */
static int esb_is_ignored(struct Window *w)
{
    int i;

    for (i = 0; i < ESB_IGNORE_SLOTS; i++) {
        if (g_ignored[i] == w) {
            return 1;
        }
    }
    return 0;
}

void esb_ignore_windows(struct Window **wins, int count)
{
    int i;

    for (i = 0; i < ESB_IGNORE_SLOTS; i++) {
        g_ignored[i] = (wins != NULL && i < count) ? wins[i] : NULL;
    }
}

/*
 * The usable rect for one set of insets. Returns 0 when the insets ate
 * the screen, which is the caller's cue to trust them less.
 */
static int esb_usable_from(struct Screen *scr, const ESInsets *ins,
                           ESRect *out)
{
    int top = scr->BarHeight + 1;

    if (ins->t > top) {
        top = ins->t;
    }
    out->x = ins->l;
    out->y = top;
    out->w = scr->Width - ins->l - ins->r;
    out->h = scr->Height - top - ins->b;
    return out->w > 0 && out->h > 0;
}

static void esb_usable_area_locked(struct Screen *scr, struct Window *skip,
                                   ESRect *out_usable, ESInsets *out_ins)
{
    ESRect scrrect;
    ESInsets ins;
    struct Window *w;

    scrrect.x = 0;
    scrrect.y = 0;
    scrrect.w = scr->Width;
    scrrect.h = scr->Height;

    /*
     * Every window, not the first sixteen. The insets are accumulated
     * as the list is walked, so a desktop may carry as many docks as it
     * likes. The version this replaces filled a 16-entry array first
     * and stopped collecting there, so on a crowded screen every dock
     * past the sixteenth reserved nothing at all.
     */
    es_panel_begin(&ins);
    for (w = scr->FirstWindow; g_cfg.panel_detect && w != NULL;
         w = w->NextWindow) {
        ESRect box;

        if (w == skip || esb_is_ignored(w)) {
            continue;
        }
        /* Panels do not move, size or sit behind everything. Borderless
         * is deliberately not required: real docks do not guarantee it. */
        if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET |
                         WFLG_BACKDROP)) != 0) {
            continue;
        }
        box.x = w->LeftEdge;
        box.y = w->TopEdge;
        box.w = w->Width;
        box.h = w->Height;
        es_panel_add(&scrrect, &box, &ins);
    }
    es_panel_end(&ins, g_cfg.panel_margin);

    ins.l += g_cfg.margin.l;
    ins.r += g_cfg.margin.r;
    ins.t += g_cfg.margin.t;
    ins.b += g_cfg.margin.b;

    if (!esb_usable_from(scr, &ins, out_usable)) {
        /*
         * Panels plus margins left nothing to snap into. Something was
         * mistaken for a dock, or the margins are absurd; either way a
         * degenerate rect would make every zone derived from it
         * nonsense. Give up our own guesses first - the user's margins
         * are an explicit choice, so they outlive our panel detection.
         */
        ins = g_cfg.margin;
        if (!esb_usable_from(scr, &ins, out_usable)) {
            ins.l = 0;
            ins.t = 0;
            ins.r = 0;
            ins.b = 0;
            (void)esb_usable_from(scr, &ins, out_usable);
        }
    }
    if (out_ins != NULL) {
        *out_ins = ins;
    }
}

void esb_debug_usable(struct Screen *scr, ESRect *usable, ESInsets *ins)
{
    esb_usable_area_locked(scr, NULL, usable, ins);
}

/* Under LockIBase only. */
static void esb_fill(struct Window *win, struct Screen *scr,
                     struct ESBSnap *out)
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
    out->bar_h = win->BorderTop;
    out->mouse_x = scr->MouseX;
    out->mouse_y = scr->MouseY;
    esb_usable_area_locked(scr, win, &out->usable, NULL);
}

/*
 * Re-validate a window reference against the live lists and sample it.
 * A caller-supplied struct Window * is never dereferenced outside this
 * locked section: that is the contract that makes ES_ERR_STALE a
 * promise rather than a hope.
 */
static int esb_sample(struct Window *target, struct ESBSnap *out)
{
    ULONG ilock;
    struct Screen *scr;
    struct Window *win;
    int found = 0;

    ilock = LockIBase(0);
    for (scr = ESB_IBASE->FirstScreen; scr != NULL && !found;
         scr = scr->NextScreen) {
        for (win = scr->FirstWindow; win != NULL; win = win->NextWindow) {
            if (win == target) {
                esb_fill(win, scr, out);
                found = 1;
                break;
            }
        }
    }
    UnlockIBase(ilock);
    return found;
}

static struct Window *esb_sample_active(struct ESBSnap *out)
{
    ULONG ilock;
    struct Window *win;

    ilock = LockIBase(0);
    win = ESB_IBASE->ActiveWindow;
    if (win != NULL) {
        esb_fill(win, win->WScreen, out);
    }
    UnlockIBase(ilock);
    return win;
}

static int esb_snappable(const struct ESBSnap *s)
{
    if ((s->flags & WFLG_SIZEGADGET) == 0) {
        return 0;
    }
    if (s->flags & (WFLG_BACKDROP | WFLG_BORDERLESS)) {
        return 0;
    }
    return 1;
}

/* ------------------------------------------------ moving a window */

/*
 * One box change, in the shape each system draws best. AROS's Wanderer
 * redraws only the area a window gained and, after a change that moves
 * and resizes at once, never repaints what the move left behind inside
 * the window: a snapped drawer came up half drawn, with a black strip
 * where the icons should have been (AROS One, 2026-09-05, and an AROS
 * developer confirming the "wild ChangeWindowBox" case). So on AROS a
 * change that does both is made in two: a window that shrinks is sized
 * first and moved after, one that grows is moved first and sized
 * after, so no step covers or uncovers more than it must. The other
 * systems take the one call they have always taken.
 */
static void esb_change_box(struct Window *win, const ESRect *from,
                           const ESRect *to, int may_raise)
{
#ifdef __AROS__
    int moved = (from->x != to->x || from->y != to->y);
    int sized = (from->w != to->w || from->h != to->h);

    /*
     * Fully visible before it moves. A move is a copy of what is on
     * screen, and whatever another window covered cannot be copied:
     * that part arrives as damage inside the old area, which Wanderer
     * never repaints, and shows as a black block. In front, the window
     * has nothing covered and the move is clean. A client that asked
     * to keep the stacking order gets what it asked for, black block
     * and all.
     */
    if (moved && may_raise) {
        WindowToFront(win);
    }
    if (moved && sized) {
        long before = (long)from->w * from->h;
        long after = (long)to->w * to->h;

        if (after <= before) {
            ChangeWindowBox(win, from->x, from->y, to->w, to->h);
        } else {
            ChangeWindowBox(win, to->x, to->y, from->w, from->h);
        }
        /*
         * A beat between the two. Wanderer repairs an area uncovered
         * by the first step with a full redraw only while its size is
         * unchanged: once the size has changed it paints the new areas
         * alone, and damage that arrived in between stays pen 0. The
         * wait lets the repair land before the size moves.
         */
        Delay(3);
    }
#else
    (void)from;
    (void)may_raise;
#endif
    ChangeWindowBox(win, to->x, to->y, to->w, to->h);
}

/* ------------------------------------------------------- public API */

ULONG esb_query_capabilities(void)
{
    ULONG caps = ES_CAP_SNAP | ES_CAP_RESTORE | ES_CAP_DRAG_DETECT |
                 ES_CAP_PREVIEW_OUTLINE | ES_CAP_HOTKEYS | ES_CAP_GUTTER;

    /* ES_CAP_PREVIEW_ALPHA stays out: translucent previews need
     * compositing and are not implemented. A client must be able to
     * tell what is really there. */
    return caps;
}

/* Shared by the programmatic API and the engine's own decision. want
 * may be NULL: then the target rectangle is computed from fresh facts. */
LONG esb_snap_rect(struct Window *win, ULONG zone, const ESRect *want)
{
    struct ESBSnap s;
    ESRect r;
    LONG rc = ES_OK;

    if (win == NULL || zone == ES_ZONE_NONE || zone > ES_ZONE_MAX) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
        rc = ES_ERR_STALE;
    } else if (esb_is_excluded(win) || !esb_snappable(&s)) {
        rc = ES_ERR_REJECTED;
    } else {
        if (want != NULL) {
            r = *want;
        } else {
            es_fit_zone_rect((int)zone, &s.usable, s.min_w, s.min_h,
                             s.max_w, s.max_h, &r);
        }
        /* Fill what the opposite side is not using, like Windows and
         * macOS: after a pair has been re-balanced to 70/30, the next
         * window snapped to the narrow side gets that 30%. */
        ObtainSemaphore(&g_sem);
        es_pair_fill(&g_registry, win, (int)zone, &s.usable, s.min_w, s.max_w, &r);
        ReleaseSemaphore(&g_sem);
        /* Refuse rather than snap without a way back: a full registry
         * would make ESnap_UnsnapWindow silently impossible. */
        rc = es_registry_remember(&g_registry, win, &s.box, &r, (int)zone);
        if (rc == ES_OK) {
            esb_change_box(win, &s.box, &r, 1);
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_snap_window(struct Window *win, ULONG zone)
{
    return esb_snap_rect(win, zone, NULL);
}

LONG esb_unsnap_window(struct Window *win)
{
    struct ESBSnap s;
    ESRect out;
    LONG rc;

    if (win == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
        rc = ES_ERR_STALE;
    } else {
        rc = es_registry_restore(&g_registry, win, &s.box, &out);
        if (rc == ES_OK) {
            esb_change_box(win, &s.box, &out, 1);
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_query_window(struct Window *win, ULONG *zone_out)
{
    struct ESBSnap s;
    LONG rc = ES_OK;

    if (win == NULL || zone_out == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
        rc = ES_ERR_STALE;
        *zone_out = ES_ZONE_NONE;
    } else {
        *zone_out = (ULONG)es_registry_zone(&g_registry, win);
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

static LONG esb_exclude_window_inner(struct Window *win, BOOL exclude)
{
    struct ESBSnap s;
    LONG rc = ES_OK;
    int i;

    if (win == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    if (!esb_sample(win, &s)) {
        rc = ES_ERR_STALE;
    } else if (exclude) {
        if (!esb_is_excluded(win)) {
            rc = ES_ERR_NO_MEMORY;
            for (i = 0; i < ESB_EXCLUDE_SLOTS; i++) {
                if (g_excluded[i] == 0) {
                    g_excluded[i] = (void *)win;
                    rc = ES_OK;
                    break;
                }
            }
        }
    } else {
        for (i = 0; i < ESB_EXCLUDE_SLOTS; i++) {
            if (g_excluded[i] == (void *)win) {
                g_excluded[i] = 0;
            }
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_set_config(const ESConfig *cfg)
{
    if (cfg == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    g_cfg = *cfg;
    g_engine.cfg = g_cfg.engine; /* live: applies to the next drag */
    ReleaseSemaphore(&g_sem);
    return ES_OK;
}

const ESConfig *esb_config(void)
{
    return &g_cfg;
}

/*
 * Minimal tag walker: utility.library is not opened for this, and the
 * chain forms (TAG_MORE/TAG_SKIP/TAG_IGNORE) are part of the contract
 * a caller may legitimately use.
 */
static const struct TagItem *esb_next_tag(const struct TagItem **ptr)
{
    const struct TagItem *t = *ptr;

    if (t == NULL) {
        return NULL;
    }
    for (;;) {
        switch (t->ti_Tag) {
        case TAG_DONE:
            *ptr = NULL;
            return NULL;
        case TAG_IGNORE:
            t++;
            break;
        case TAG_MORE:
            t = (const struct TagItem *)t->ti_Data;
            if (t == NULL) {
                *ptr = NULL;
                return NULL;
            }
            break;
        case TAG_SKIP:
            t += 1 + (LONG)t->ti_Data;
            break;
        default:
            *ptr = t + 1;
            return t;
        }
    }
}

LONG esb_set_options(const struct TagItem *tags)
{
    const struct TagItem *scan = tags;
    const struct TagItem *ti;
    ESConfig cfg;
    LONG v;

    if (tags == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    cfg = g_cfg; /* apply to a copy: a bad tag must change nothing */
    ReleaseSemaphore(&g_sem);

    while ((ti = esb_next_tag(&scan)) != NULL) {
        v = (LONG)ti->ti_Data;
        switch (ti->ti_Tag) {
        case ES_OPT_EdgePx:
            if (v < ES_EDGE_PX_MIN || v > ES_EDGE_PX_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.engine.edge_px = (int)v;
            break;
        case ES_OPT_CornerDiv:
            if (v < ES_CORNER_DIV_MIN || v > ES_CORNER_DIV_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.engine.corner_div = (int)v;
            break;
        case ES_OPT_DragMinPx:
            if (v < ES_DRAG_MIN_PX_MIN || v > ES_DRAG_MIN_PX_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.engine.drag_min_px = (int)v;
            break;
        case ES_OPT_MarginLeft:
            if (v < ES_MARGIN_MIN || v > ES_MARGIN_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.l = (int)v;
            break;
        case ES_OPT_MarginTop:
            if (v < ES_MARGIN_MIN || v > ES_MARGIN_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.t = (int)v;
            break;
        case ES_OPT_MarginRight:
            if (v < ES_MARGIN_MIN || v > ES_MARGIN_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.r = (int)v;
            break;
        case ES_OPT_MarginBottom:
            if (v < ES_MARGIN_MIN || v > ES_MARGIN_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.b = (int)v;
            break;
        case ES_OPT_PanelDetect:
            cfg.panel_detect = v ? 1 : 0;
            break;
        case ES_OPT_PanelMargin:
            if (v < ES_PANEL_MARGIN_MIN ||
                v > ES_PANEL_MARGIN_MAX) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.panel_margin = (int)v;
            break;
        case ES_OPT_Zones:
            cfg.engine.zones_mask = (unsigned)ti->ti_Data & ES_ZONEMASK_ALL;
            break;
        case ES_OPT_Preview:
            cfg.preview = v ? 1 : 0;
            break;
        case ES_OPT_BypassQual:
            if (v < ES_QUAL_NONE || v > ES_QUAL_SHIFT) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.bypass_qual = (int)v;
            break;
        default:
            break; /* unknown tag: ignored, by contract */
        }
    }
    return esb_set_config(&cfg);
}

LONG esb_enable(BOOL on)
{
    ObtainSemaphore(&g_sem);
    g_enabled = on ? 1 : 0;
    ReleaseSemaphore(&g_sem);
    return ES_OK;
}

int esb_enabled(void)
{
    return g_enabled;
}

/* ------------------------------------------------------ the divider */

/* Same slack as the registry restore: apps round sizes to increments,
 * so "where we put it" is a neighbourhood, not a point. */
static int esb_same_box(const ESRect *a, const ESRect *b)
{
    int dx = a->x - b->x;
    int dy = a->y - b->y;
    int dw = a->w - b->w;
    int dh = a->h - b->h;

    if (dx < 0) { dx = -dx; }
    if (dy < 0) { dy = -dy; }
    if (dw < 0) { dw = -dw; }
    if (dh < 0) { dh = -dh; }
    return dx <= ES_RESTORE_SLACK_PX && dy <= ES_RESTORE_SLACK_PX &&
           dw <= ES_RESTORE_SLACK_PX && dh <= ES_RESTORE_SLACK_PX;
}

/*
 * A remembered seam is not a real one: any window on it may have been
 * moved, resized or closed since. Check them all against the live
 * geometry and report no seam when they no longer line up.
 *
 * What this must NOT do is forget them on a mismatch. ChangeWindowBox()
 * places a window asynchronously, so a query made immediately after a
 * snap sees it still at its old size, a mismatch that lasts a few
 * milliseconds. Forgetting on that transient destroyed the pair
 * permanently and the seam then never appeared at all. Only a window
 * that is GONE is forgotten; a geometry mismatch just means "no seam
 * right now".
 */
static int esb_seam_alive(const ESSeam *seam)
{
    const ESSeamSide *side[2];
    int gone = 0;
    int mismatch = 0;
    int k, i;

    side[0] = &seam->a;
    side[1] = &seam->b;
    for (k = 0; k < 2; k++) {
        for (i = 0; i < side[k]->n; i++) {
            struct ESBSnap sn;
            struct Window *w = (struct Window *)side[k]->ref[i];

            if (!esb_sample(w, &sn)) {
                ObtainSemaphore(&g_sem);
                es_registry_forget(&g_registry, (void *)w);
                ReleaseSemaphore(&g_sem);
                gone = 1;
            } else if (!esb_same_box(&sn.box, &side[k]->box[i])) {
                mismatch = 1;
            }
        }
    }
    return !gone && !mismatch;
}

static void esb_seam_report(const ESSeam *seam, struct ESnapDivider *out)
{
    out->present = 1;
    out->vertical = seam->vertical;
    out->strip.x = seam->rect.x;
    out->strip.y = seam->rect.y;
    out->strip.w = seam->rect.w;
    out->strip.h = seam->rect.h;
    out->position = seam->pos;
    out->minPosition = seam->min_pos;
    out->maxPosition = seam->max_pos;
    out->windowCount = seam->a.n + seam->b.n;
    out->windowA = (struct Window *)seam->a.ref[0];
    out->windowB = (struct Window *)seam->b.ref[0];
}

static void esb_no_seam(struct ESnapDivider *out)
{
    out->present = 0;
    out->vertical = 0;
    out->strip.x = 0;
    out->strip.y = 0;
    out->strip.w = 0;
    out->strip.h = 0;
    out->position = 0;
    out->minPosition = 0;
    out->maxPosition = 0;
    out->windowCount = 0;
    out->windowA = NULL;
    out->windowB = NULL;
}

/* Distance from a point to a rectangle, 0 when inside. */
static int esb_box_distance(const ESRect *r, int px, int py)
{
    int dx = 0, dy = 0;

    if (px < r->x) {
        dx = r->x - px;
    } else if (px >= r->x + r->w) {
        dx = px - (r->x + r->w - 1);
    }
    if (py < r->y) {
        dy = r->y - py;
    } else if (py >= r->y + r->h) {
        dy = py - (r->y + r->h - 1);
    }
    return dx > dy ? dx : dy;
}

LONG esb_query_divider(ULONG thickness, struct ESnapDivider *out)
{
    ESSeam seam;
    int found;

    if (out == NULL || thickness < 2 || thickness > 64) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    found = es_gutter_find(&g_registry, (int)thickness, &seam);
    ReleaseSemaphore(&g_sem);
    if (found && esb_seam_alive(&seam)) {
        esb_seam_report(&seam, out);
    } else {
        esb_no_seam(out);
    }
    return ES_OK;
}

/*
 * Every window keeps its own floor and ceiling as well: a seam pushed
 * past a MinWidth or a MaxHeight would make Intuition clamp that
 * window to a box other than the one recorded, and a recorded box that
 * does not match the real one reads as "the user moved it", which ends
 * the seam mid-drag with the other side already moved. The samples are
 * live, so a window that changed its minimum since it was snapped is
 * honoured too. A side keeps its far edge, so its size is the distance
 * from that edge to the divider.
 */
static void esb_seam_tighten(ESSeam *seam, const struct ESBSnap *sa,
                             const struct ESBSnap *sb)
{
    int i;

    for (i = 0; i < seam->a.n; i++) {
        const ESRect *b = &seam->a.box[i];
        int lo, hi;

        if (seam->vertical) {
            lo = b->x + sa[i].min_w;
            hi = sa[i].max_w > 0 ? b->x + sa[i].max_w : 0x7FFF;
        } else {
            lo = b->y + sa[i].min_h;
            hi = sa[i].max_h > 0 ? b->y + sa[i].max_h : 0x7FFF;
        }
        if (lo > seam->min_pos) {
            seam->min_pos = lo;
        }
        if (hi < seam->max_pos) {
            seam->max_pos = hi;
        }
    }
    for (i = 0; i < seam->b.n; i++) {
        const ESRect *b = &seam->b.box[i];
        int lo, hi;

        if (seam->vertical) {
            hi = b->x + b->w - sb[i].min_w;
            lo = sb[i].max_w > 0 ? b->x + b->w - sb[i].max_w : -0x7FFF;
        } else {
            hi = b->y + b->h - sb[i].min_h;
            lo = sb[i].max_h > 0 ? b->y + b->h - sb[i].max_h : -0x7FFF;
        }
        if (lo > seam->min_pos) {
            seam->min_pos = lo;
        }
        if (hi < seam->max_pos) {
            seam->max_pos = hi;
        }
    }
}

/* Alive, and with the range every window on it can actually accept. */
static int esb_seam_limits(ESSeam *seam)
{
    struct ESBSnap sa[ES_SEAM_SIDE_MAX], sb[ES_SEAM_SIDE_MAX];
    int i;

    if (!esb_seam_alive(seam)) {
        return 0;
    }
    for (i = 0; i < seam->a.n; i++) {
        if (!esb_sample((struct Window *)seam->a.ref[i], &sa[i])) {
            return 0;
        }
    }
    for (i = 0; i < seam->b.n; i++) {
        if (!esb_sample((struct Window *)seam->b.ref[i], &sb[i])) {
            return 0;
        }
    }
    esb_seam_tighten(seam, sa, sb);
    return seam->min_pos <= seam->max_pos;
}

LONG esb_query_divider_at(ULONG thickness, LONG x, LONG y,
                          struct ESnapDivider *out)
{
    ESSeam seams[ES_SEAM_MAX];
    int n, i, best = -1, best_d = 0;

    if (out == NULL || thickness < 2 || thickness > 64) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    n = es_gutter_find_all(&g_registry, (int)thickness, seams, ES_SEAM_MAX);
    ReleaseSemaphore(&g_sem);

    for (i = 0; i < n; i++) {
        int d = esb_box_distance(&seams[i].rect, (int)x, (int)y);

        if (best < 0 || d < best_d) {
            best = i;
            best_d = d;
        }
    }
    if (best >= 0 && esb_seam_limits(&seams[best])) {
        esb_seam_report(&seams[best], out);
    } else {
        esb_no_seam(out);
    }
    return ES_OK;
}

LONG esb_move_divider_at(LONG vertical, LONG line, LONG position)
{
    ESSeam seams[ES_SEAM_MAX];
    ESRect ra[ES_SEAM_SIDE_MAX], rb[ES_SEAM_SIDE_MAX];
    struct ESBSnap snap_a[ES_SEAM_SIDE_MAX], snap_b[ES_SEAM_SIDE_MAX];
    const ESSeam *seam = NULL;
    ESSeam chosen;
    int n, i, d;

    ObtainSemaphore(&g_sem);
    n = es_gutter_find_all(&g_registry, 8, seams, ES_SEAM_MAX);
    ReleaseSemaphore(&g_sem);

    /*
     * The seam is named by its line: a layout can hold several, and
     * moving "the first one" would resize windows nobody grabbed.
     *
     * NEAREST on the same axis, not "within a tolerance". A dragged
     * seam moves away from where it was grabbed, so a fixed window of
     * a few pixels stops matching as soon as the user has pulled
     * further than that - which is a drag that works for a moment and
     * then dies. The caller follows the seam it is moving, so nearest
     * is both unambiguous and stable, and it still holds when the
     * position is clamped and the pointer runs on past the limit.
     */
    {
        int best = -1;

        for (i = 0; i < n; i++) {
            if (seams[i].vertical != (vertical ? 1 : 0)) {
                continue;
            }
            d = seams[i].pos - (int)line;
            if (d < 0) {
                d = -d;
            }
            if (best < 0 || d < best) {
                best = d;
                seam = &seams[i];
            }
        }
    }
    if (seam == NULL) {
        return ES_ERR_UNSUPPORTED;
    }

    /* Every window on it must still be there: a seam with one gone is
     * not a seam, and dropping the state beats resizing a stranger
     * that inherited the address. */
    for (i = 0; i < seam->a.n; i++) {
        if (!esb_sample((struct Window *)seam->a.ref[i], &snap_a[i])) {
            return ES_ERR_STALE;
        }
    }
    for (i = 0; i < seam->b.n; i++) {
        if (!esb_sample((struct Window *)seam->b.ref[i], &snap_b[i])) {
            return ES_ERR_STALE;
        }
    }

    /* the range the windows can take right now, not the fixed floor */
    chosen = *seam;
    esb_seam_tighten(&chosen, snap_a, snap_b);
    if (chosen.min_pos > chosen.max_pos) {
        return ES_ERR_UNSUPPORTED;      /* nothing on it can give */
    }
    es_gutter_apply(&chosen, (int)position, ra, rb);

    ObtainSemaphore(&g_sem);
    /* the registry must follow, or the next drag would start from the
     * geometry the windows had before this one */
    for (i = 0; i < seam->a.n; i++) {
        struct Window *w = (struct Window *)seam->a.ref[i];

        es_registry_remember(&g_registry, w, &snap_a[i].box, &ra[i],
                             es_registry_zone(&g_registry, w));
    }
    for (i = 0; i < seam->b.n; i++) {
        struct Window *w = (struct Window *)seam->b.ref[i];

        es_registry_remember(&g_registry, w, &snap_b[i].box, &rb[i],
                             es_registry_zone(&g_registry, w));
    }
    ReleaseSemaphore(&g_sem);

    /*
     * Shrinking first, growing last, as a layout is applied. A window
     * that grows into space another window still holds gets its new
     * size while that space is covered, and on a system whose console
     * or file manager clears a window only when its size changes and
     * never repaints an area uncovered later (AROS One, 2026-09-05),
     * the strip stays pen 0 once the other window has moved away. With
     * the shrinking side moved first, the growing side finds its new
     * area free at the moment it is told its new size.
     */
    {
        long delta[2 * ES_SEAM_SIDE_MAX];
        int order[2 * ES_SEAM_SIDE_MAX];
        int n = 0, k;

        for (i = 0; i < seam->a.n; i++, n++) {
            delta[n] = (long)ra[i].w * ra[i].h -
                       (long)snap_a[i].box.w * snap_a[i].box.h;
        }
        for (i = 0; i < seam->b.n; i++, n++) {
            delta[n] = (long)rb[i].w * rb[i].h -
                       (long)snap_b[i].box.w * snap_b[i].box.h;
        }
        es_order_by_growth(delta, n, order);
        for (k = 0; k < n; k++) {
            int j = order[k];

            if (j < seam->a.n) {
                esb_change_box((struct Window *)seam->a.ref[j],
                               &snap_a[j].box, &ra[j], 1);
            } else {
                j -= seam->a.n;
                esb_change_box((struct Window *)seam->b.ref[j],
                               &snap_b[j].box, &rb[j], 1);
            }
        }
    }
    return ES_OK;
}

LONG esb_query_screen_area(struct Screen *scr, struct ESnapArea *out)
{
    ESRect usable;
    ESInsets ins;
    ULONG ilock;

    if (scr == NULL || out == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    ilock = LockIBase(0);
    esb_usable_area_locked(scr, NULL, &usable, &ins);
    UnlockIBase(ilock);
    ReleaseSemaphore(&g_sem);

    out->usable.x = usable.x;
    out->usable.y = usable.y;
    out->usable.w = usable.w;
    out->usable.h = usable.h;
    out->insetLeft = ins.l;
    out->insetTop = ins.t;
    out->insetRight = ins.r;
    out->insetBottom = ins.b;
    return ES_OK;
}

int esb_drag_active(void)
{
    return g_engine.button_down;
}

/* ------------------------------------------------- interactive path */

/* Amiga qualifier bits for the configured bypass key: a decision, so
 * it belongs here and not in a frontend. */
static ULONG esb_bypass_mask(void)
{
    switch (g_cfg.bypass_qual) {
    case ES_QUAL_ALT:
        return IEQUALIFIER_LALT | IEQUALIFIER_RALT;
    case ES_QUAL_CTRL:
        return IEQUALIFIER_CONTROL;
    case ES_QUAL_SHIFT:
        return IEQUALIFIER_LSHIFT | IEQUALIFIER_RSHIFT;
    default:
        return 0;
    }
}

static void esb_report_clear(struct ESnapReport *out)
{
    out->dragStarted = 0;
    out->zoneChanged = 0;
    out->zone = ES_ZONE_NONE;
    out->previewShow = 0;
    out->previewHide = 0;
    out->previewRect.x = out->previewRect.y = 0;
    out->previewRect.w = out->previewRect.h = 0;
    out->previewScreen = NULL;
    out->snapped = 0;
    out->snapZone = ES_ZONE_NONE;
    out->snapResult = ES_OK;
    out->snapWindow = NULL;
    out->dragActive = 0;
}

static void esb_absorb(const ESEngineActions *a, struct ESnapReport *out)
{
    if (a->drag_started) {
        out->dragStarted = 1;
    }
    if (a->zone_changed) {
        out->zoneChanged = 1;
        out->zone = (ULONG)a->zone;
    }
    if (a->hide_preview) {
        out->previewHide = 1;
        out->previewShow = 0;
    }
    if (a->show_preview) {
        out->previewShow = 1;
        out->previewRect.x = a->preview_rect.x;
        out->previewRect.y = a->preview_rect.y;
        out->previewRect.w = a->preview_rect.w;
        out->previewRect.h = a->preview_rect.h;
    }
}

void esb_input(int press, int motion, int release, ULONG quals,
               struct ESnapReport *out)
{
    ESEngineActions a;
    struct ESBSnap s;
    struct Window *win = NULL;
    int do_snap = 0;
    int snap_zone = ES_ZONE_NONE;
    ESRect snap_rect;

    esb_report_clear(out);
    if (!g_ready) {
        return;
    }

    ObtainSemaphore(&g_sem);
    if (!g_enabled) {
        /* disabled mid-drag: make sure no frame is left behind */
        es_engine_reset(&g_engine, &a);
        esb_absorb(&a, out);
        ReleaseSemaphore(&g_sem);
        return;
    }

    if (press) {
        struct ESBSnap ps;
        int mx = -1, my = -1;

        /* where the button went down: the pointer on the screen of
         * whatever window is active right now, which may still be the
         * one about to lose the activation */
        if (esb_sample_active(&ps) != NULL) {
            mx = ps.mouse_x;
            my = ps.mouse_y;
        }
        es_engine_press(&g_engine, mx, my, &a);
        esb_absorb(&a, out);
    }

    if (motion && g_engine.button_down && !release) {
        win = esb_sample_active(&s);
        if (win == NULL) {
            es_engine_motion(&g_engine, 0, &a);
        } else {
            ESWinFacts f;
            ULONG bypass = esb_bypass_mask();

            f.ref = win;
            f.box = s.box;
            f.usable = s.usable;
            f.mouse_x = s.mouse_x;
            f.mouse_y = s.mouse_y;
            f.min_w = s.min_w;
            f.min_h = s.min_h;
            f.max_w = s.max_w;
            f.max_h = s.max_h;
            f.bar_h = s.bar_h;
            f.flags = 0;
            if (esb_snappable(&s) && !esb_is_excluded(win)) {
                f.flags |= ES_WF_SNAPPABLE;
            }
            if (s.flags & WFLG_DRAGBAR) {
                f.flags |= ES_WF_DRAGBAR;
            }
            if (bypass != 0 && (quals & bypass) != 0) {
                f.flags |= ES_WF_BYPASS;
            }
            es_engine_motion(&g_engine, &f, &a);
            if (a.show_preview) {
                es_pair_fill(&g_registry, win, a.zone, &s.usable, s.min_w, s.max_w,
                             &a.preview_rect);
            }
            out->previewScreen = s.scr;
        }
        esb_absorb(&a, out);
    }

    if (release) {
        es_engine_release(&g_engine, &a);
        esb_absorb(&a, out);
        if (a.do_snap) {
            do_snap = 1;
            snap_zone = a.snap_zone;
            snap_rect = a.snap_rect;
            win = (struct Window *)a.snap_ref;
        }
    }
    ReleaseSemaphore(&g_sem);

    /* The snap takes the semaphore itself: never nest it, and never
     * hold it across ChangeWindowBox for longer than one call. */
    if (do_snap) {
        out->snapped = 1;
        out->snapZone = (ULONG)snap_zone;
        out->snapWindow = win;
        out->snapResult = esb_snap_rect(win, (ULONG)snap_zone, &snap_rect);
    }
    out->dragActive = g_engine.button_down;
}

void esb_input_reset(struct ESnapReport *out)
{
    ESEngineActions a;

    esb_report_clear(out);
    if (!g_ready) {
        return;
    }
    ObtainSemaphore(&g_sem);
    es_engine_reset(&g_engine, &a);
    ReleaseSemaphore(&g_sem);
    esb_absorb(&a, out);
}

/*
 * The 2.2 vector, kept with its 2.2 meaning: move whichever seam the
 * layout walk finds first. A 2.x client compiled against 2.2 must keep
 * working on every later 2.x, so this neither moves nor changes shape;
 * anything that knows which seam it means calls esb_move_divider_at().
 */
LONG esb_move_divider(LONG position)
{
    ESSeam seam;
    int found;

    ObtainSemaphore(&g_sem);
    found = es_gutter_find(&g_registry, 8, &seam);
    ReleaseSemaphore(&g_sem);
    if (!found) {
        return ES_ERR_UNSUPPORTED;
    }
    return esb_move_divider_at((LONG)seam.vertical, (LONG)seam.pos,
                               position);
}

/*
 * Exclusions are held by address (see the header for what that means
 * for a caller). What the library CAN do from outside Intuition is drop
 * an entry once its window is found gone, so that the list does not
 * fill with corpses and a corpse's address is not carried longer than
 * necessary. Done whenever the list is touched, outside the semaphore,
 * because finding a window walks the screens under LockIBase.
 */
static void esb_exclusions_sweep(void)
{
    void *seen[ESB_EXCLUDE_SLOTS];
    struct ESBSnap sn;
    int i;

    ObtainSemaphore(&g_sem);
    for (i = 0; i < ESB_EXCLUDE_SLOTS; i++) {
        seen[i] = g_excluded[i];
    }
    ReleaseSemaphore(&g_sem);

    for (i = 0; i < ESB_EXCLUDE_SLOTS; i++) {
        if (seen[i] != 0 && !esb_sample((struct Window *)seen[i], &sn)) {
            ObtainSemaphore(&g_sem);
            if (g_excluded[i] == seen[i]) {
                g_excluded[i] = 0;
            }
            ReleaseSemaphore(&g_sem);
        }
    }
}

LONG esb_exclude_window(struct Window *win, BOOL exclude)
{
    esb_exclusions_sweep();
    return esb_exclude_window_inner(win, exclude);
}

/* ------------------------------------------------ 2.6: window identity */

/*
 * Every window observed gets a serial that is never reused. The table
 * is walked with the window lists, under g_sem and LockIBase: a window
 * seen again at the same address with the same layer keeps its serial;
 * the same address with another layer is another window, and the old
 * entry dies; an entry not seen in a full walk is a window that is
 * gone. The layer is the tell: Intuition gives every window one of its
 * own, and a reused window address with a reused layer address between
 * two observations is the one case the library cannot see.
 */
#define ESB_IDENT_SLOTS 128

struct ESBIdent {
    struct Window *win;
    struct Layer *layer;
    ULONG serial;
    ULONG pass;                /* the last full walk that saw it     */
};

static struct ESBIdent g_ident[ESB_IDENT_SLOTS];
static ULONG g_ident_next = 1; /* 0 means "none"                     */
static ULONG g_ident_pass;     /* counts the full walks              */

/* g_sem and LockIBase held: the serial of a window seen now. */
static ULONG esb_ident_touch(struct Window *w)
{
    int i, spare = -1;

    for (i = 0; i < ESB_IDENT_SLOTS; i++) {
        struct ESBIdent *e = &g_ident[i];

        if (e->win == w) {
            if (e->layer == w->WLayer) {
                e->pass = g_ident_pass;
                return e->serial;
            }
            e->win = NULL;     /* same address, another window */
            spare = i;
            break;
        }
        if (e->win == NULL && spare < 0) {
            spare = i;
        }
    }
    if (spare < 0) {
        return 0;              /* the table is full: no identity today */
    }
    g_ident[spare].win = w;
    g_ident[spare].layer = w->WLayer;
    g_ident[spare].serial = g_ident_next++;
    g_ident[spare].pass = g_ident_pass;
    return g_ident[spare].serial;
}

/* g_sem and LockIBase held: one full walk, every screen. Windows not
 * seen are gone, and their serials retire with them. */
static void esb_ident_observe_all(void)
{
    struct Screen *scr;
    struct Window *w;
    int i;

    g_ident_pass++;
    for (scr = ESB_IBASE->FirstScreen; scr != NULL; scr = scr->NextScreen) {
        for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
            esb_ident_touch(w);
        }
    }
    for (i = 0; i < ESB_IDENT_SLOTS; i++) {
        if (g_ident[i].win != NULL && g_ident[i].pass != g_ident_pass) {
            g_ident[i].win = NULL;
        }
    }
}

LONG esb_query_window_serial(struct Window *win, ULONG *serial)
{
    ULONG ilock;
    struct Screen *scr;
    struct Window *w;
    int found = 0;

    if (win == NULL || serial == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    *serial = 0;
    ObtainSemaphore(&g_sem);
    ilock = LockIBase(0);
    for (scr = ESB_IBASE->FirstScreen; scr != NULL && !found;
         scr = scr->NextScreen) {
        for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
            if (w == win) {
                *serial = esb_ident_touch(w);
                found = 1;
                break;
            }
        }
    }
    UnlockIBase(ilock);
    ReleaseSemaphore(&g_sem);
    return found ? ES_OK : ES_ERR_STALE;
}

LONG esb_find_window(ULONG serial, struct Window **window)
{
    ULONG ilock;
    struct Screen *scr;
    struct Window *w, *hit = NULL;
    int i;

    if (serial == 0 || window == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    *window = NULL;
    ObtainSemaphore(&g_sem);
    for (i = 0; i < ESB_IDENT_SLOTS; i++) {
        if (g_ident[i].win != NULL && g_ident[i].serial == serial) {
            hit = g_ident[i].win;
            break;
        }
    }
    if (hit != NULL) {
        int alive = 0;

        ilock = LockIBase(0);
        for (scr = ESB_IBASE->FirstScreen; scr != NULL && !alive;
             scr = scr->NextScreen) {
            for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
                if (w == hit && w->WLayer == g_ident[i].layer) {
                    alive = 1;
                    break;
                }
            }
        }
        UnlockIBase(ilock);
        if (!alive) {
            g_ident[i].win = NULL;   /* gone: the serial retires */
            hit = NULL;
        }
    }
    ReleaseSemaphore(&g_sem);
    *window = hit;
    return hit != NULL ? ES_OK : ES_ERR_STALE;
}

/* ------------------------------------------ 2.5: windows and layouts */

/*
 * The generation is a hash of every window's identity and geometry on
 * every screen, taken when asked. The library sees no open or close,
 * so there is nothing to count between calls; what it can promise is
 * that the number moves when the picture has.
 */
static ULONG g_generation;
static ULONG g_gen_hash;
static int g_gen_seen;

static ULONG esb_hash_mix(ULONG h, ULONG v)
{
    return (h ^ v) * 16777619UL;
}

/* Under LockIBase only. */
static ULONG esb_windows_hash(void)
{
    ULONG h = 2166136261UL;
    struct Screen *scr;
    struct Window *w;

    for (scr = ESB_IBASE->FirstScreen; scr != NULL; scr = scr->NextScreen) {
        h = esb_hash_mix(h, (ULONG)(IPTR)scr);
        for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
            h = esb_hash_mix(h, (ULONG)(IPTR)w);
            h = esb_hash_mix(h, (ULONG)(UWORD)w->LeftEdge |
                                ((ULONG)(UWORD)w->TopEdge << 16));
            h = esb_hash_mix(h, (ULONG)(UWORD)w->Width |
                                ((ULONG)(UWORD)w->Height << 16));
        }
    }
    return h;
}

ULONG esb_query_generation(struct Screen *scr)
{
    ULONG ilock, h, g;

    (void)scr;                 /* every screen counts, see the header */
    ObtainSemaphore(&g_sem);
    ilock = LockIBase(0);
    h = esb_windows_hash();
    esb_ident_observe_all();   /* the poll is where windows are seen */
    UnlockIBase(ilock);

    if (!g_gen_seen || h != g_gen_hash) {
        g_gen_hash = h;
        g_gen_seen = 1;
        g_generation++;
    }
    g = g_generation;
    ReleaseSemaphore(&g_sem);
    return g;
}

/* Under LockIBase only: is this window what the panel scan would take
 * for a dock? The same test the usable area applies. */
static int esb_looks_like_panel(struct Screen *scr, struct Window *w)
{
    ESRect scrrect, box;

    if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET | WFLG_BACKDROP)) != 0) {
        return 0;
    }
    scrrect.x = 0;
    scrrect.y = 0;
    scrrect.w = scr->Width;
    scrrect.h = scr->Height;
    box.x = w->LeftEdge;
    box.y = w->TopEdge;
    box.w = w->Width;
    box.h = w->Height;
    return es_panel_classify(&scrrect, &box) != 0;
}

LONG esb_query_windows(struct Screen *scr, struct ESnapWindowInfo *buf,
                       ULONG count, ULONG *needed)
{
    ULONG ilock, n = 0;
    struct Window *w, *active;

    if (needed == NULL || (buf == NULL && count != 0)) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    ilock = LockIBase(0);
    esb_ident_observe_all();
    if (scr == NULL) {
        scr = ESB_IBASE->FirstScreen;
    }
    active = ESB_IBASE->ActiveWindow;
    for (w = (scr != NULL) ? scr->FirstWindow : NULL; w != NULL;
         w = w->NextWindow) {
        if (n < count) {
            struct ESnapWindowInfo *o = &buf[n];
            struct ESBSnap s;
            int zone, i;

            esb_fill(w, scr, &s);
            o->window = w;
            o->rect.x = s.box.x;
            o->rect.y = s.box.y;
            o->rect.w = s.box.w;
            o->rect.h = s.box.h;
            o->flags = 0;
            if (esb_snappable(&s)) {
                o->flags |= ESWI_SNAPPABLE;
            }
            if ((s.flags & WFLG_SIZEGADGET) == 0) {
                o->flags |= ESWI_FIXED_SIZE;
            }
            if (s.flags & WFLG_BORDERLESS) {
                o->flags |= ESWI_BORDERLESS;
            }
            if (s.flags & WFLG_BACKDROP) {
                o->flags |= ESWI_BACKDROP;
            }
            if (esb_is_excluded(w)) {
                o->flags |= ESWI_EXCLUDED;
            }
            if (esb_looks_like_panel(scr, w)) {
                o->flags |= ESWI_PANEL;
            }
            if (w == active) {
                o->flags |= ESWI_ACTIVE;
            }
            zone = es_registry_zone(&g_registry, w);
            if (zone != ES_ZONE_NONE) {
                o->flags |= ESWI_SNAPPED;
            }
            o->zone = (ULONG)zone;
            o->minWidth = s.min_w;
            o->minHeight = s.min_h;
            o->maxWidth = s.max_w;
            o->maxHeight = s.max_h;
            o->serial = esb_ident_touch(w);
            for (i = 0; i < 3; i++) {
                o->reserved[i] = 0;
            }
        }
        n++;
    }
    UnlockIBase(ilock);
    ReleaseSemaphore(&g_sem);
    *needed = n;
    return ES_OK;
}

/* g_sem held. The one placement path PlaceWindow and PlaceWindowsA
 * share: sample, refuse what SnapWindow refuses, fit, record, move. */
static LONG esb_place_locked(struct Window *win, const ESRect *want,
                             ULONG flags)
{
    struct ESBSnap s;
    ESRect r;
    LONG rc;

    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
        return ES_ERR_STALE;
    }
    if (esb_is_excluded(win) || !esb_snappable(&s)) {
        return ES_ERR_REJECTED;
    }
    es_fit_rect(want, &s.usable, s.min_w, s.min_h, s.max_w, s.max_h, &r);
    /*
     * The registry keeps the geometry from before the window was
     * adopted, and follows every placement so that a restore still
     * finds the window where the last placement put it. NO_RESTORE
     * spends no slot on a window that was never adopted, but a window
     * already in the registry is followed all the same: a tiler that
     * re-places its windows many times a minute still owes the user
     * the geometry from before it took them.
     */
    if ((flags & ES_PF_NO_RESTORE) == 0 ||
        es_registry_zone(&g_registry, win) != ES_ZONE_NONE) {
        rc = es_registry_remember(&g_registry, win, &s.box, &r, ES_ZONE_RECT);
        if (rc != ES_OK) {
            return rc;
        }
    }
    esb_change_box(win, &s.box, &r, (flags & ES_PF_KEEP_ZORDER) == 0);
    return ES_OK;
}

#define ESB_PF_ALL (ES_PF_NO_RESTORE | ES_PF_KEEP_ZORDER)

LONG esb_place_window(struct Window *win, const struct ESnapRect *rect,
                      ULONG flags)
{
    ESRect want;
    LONG rc;

    if (win == NULL || rect == NULL || rect->w <= 0 || rect->h <= 0 ||
        (flags & ~ESB_PF_ALL) != 0) {
        return ES_ERR_BAD_ARGS;
    }
    want.x = rect->x;
    want.y = rect->y;
    want.w = rect->w;
    want.h = rect->h;
    ObtainSemaphore(&g_sem);
    rc = esb_place_locked(win, &want, flags);
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_place_windows(struct ESnapPlacement *list, ULONG count,
                       ULONG flags)
{
    long delta[ES_PLACE_MAX];
    int order[ES_PLACE_MAX];
    ULONG i, k;

    if (list == NULL || count == 0 || count > ES_PLACE_MAX ||
        (flags & ~ESB_PF_ALL) != 0) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    /* first pass: what each window is now, so the moves can be ordered */
    for (i = 0; i < count; i++) {
        struct ESBSnap s;

        delta[i] = 0;
        list[i].result = ES_OK;
        if (list[i].window == NULL || list[i].rect.w <= 0 ||
            list[i].rect.h <= 0) {
            list[i].result = ES_ERR_BAD_ARGS;
        } else if (!esb_sample(list[i].window, &s)) {
            es_registry_forget(&g_registry, list[i].window);
            list[i].result = ES_ERR_STALE;
        } else {
            delta[i] = (long)list[i].rect.w * (long)list[i].rect.h -
                       (long)s.box.w * (long)s.box.h;
        }
    }
    es_order_by_growth(delta, (int)count, order);
    /* second pass: shrinking first, growing last */
    for (k = 0; k < count; k++) {
        ESRect want;

        i = (ULONG)order[k];
        if (list[i].result != ES_OK) {
            continue;
        }
        want.x = list[i].rect.x;
        want.y = list[i].rect.y;
        want.w = list[i].rect.w;
        want.h = list[i].rect.h;
        list[i].result = esb_place_locked(list[i].window, &want, flags);
    }
    ReleaseSemaphore(&g_sem);
    return ES_OK;
}
