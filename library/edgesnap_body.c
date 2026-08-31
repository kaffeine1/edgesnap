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
        es_pair_fill(&g_registry, win, (int)zone, &s.usable, &r);
        ReleaseSemaphore(&g_sem);
        /* Refuse rather than snap without a way back: a full registry
         * would make ESnap_UnsnapWindow silently impossible. */
        rc = es_registry_remember(&g_registry, win, &s.box, &r, (int)zone);
        if (rc == ES_OK) {
            ChangeWindowBox(win, r.x, r.y, r.w, r.h);
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
            ChangeWindowBox(win, out.x, out.y, out.w, out.h);
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

LONG esb_exclude_window(struct Window *win, BOOL exclude)
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

LONG esb_query_divider(ULONG thickness, struct ESnapDivider *out)
{
    ESSeam seam;
    int found;

    if (out == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    if (thickness < 2 || thickness > 64) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    found = es_gutter_find(&g_registry, (int)thickness, &seam);
    ReleaseSemaphore(&g_sem);

    /*
     * A remembered pair is not a real one: either window may have been
     * moved, resized or closed since. Check both against the live
     * geometry and report no seam when they no longer line up.
     *
     * What this must NOT do is forget them. ChangeWindowBox() places a
     * window asynchronously, so a query made immediately after a snap
     * sees the window still at its old size - a mismatch that lasts a
     * few milliseconds. Forgetting on that transient destroyed the
     * pair permanently, and the seam then never appeared at all. Only
     * a window that is gone is forgotten; everything else is simply
     * re-checked next time.
     */
    if (found) {
        struct ESBSnap sa, sb;
        int a_alive = esb_sample((struct Window *)seam.ref_a, &sa);
        int b_alive = esb_sample((struct Window *)seam.ref_b, &sb);

        if (!a_alive || !b_alive) {
            ObtainSemaphore(&g_sem);
            if (!a_alive) {
                es_registry_forget(&g_registry, (void *)seam.ref_a);
            }
            if (!b_alive) {
                es_registry_forget(&g_registry, (void *)seam.ref_b);
            }
            ReleaseSemaphore(&g_sem);
            found = 0;
        } else if (!esb_same_box(&sa.box, &seam.box_a) ||
                   !esb_same_box(&sb.box, &seam.box_b)) {
            found = 0;
        }
    }

    if (!found) {
        /* Leave nothing undefined for the caller: "no seam" used to
         * return with strip/vertical untouched, and the commodity's
         * debug line printed whatever was on its stack. */
        out->present = 0;
        out->vertical = 0;
        out->strip.x = 0;
        out->strip.y = 0;
        out->strip.w = 0;
        out->strip.h = 0;
        out->windowA = NULL;
        out->windowB = NULL;
        return ES_OK;
    }
    out->present = 1;
    out->vertical = seam.vertical;
    out->strip.x = seam.rect.x;
    out->strip.y = seam.rect.y;
    out->strip.w = seam.rect.w;
    out->strip.h = seam.rect.h;
    out->position = seam.pos;
    out->minPosition = seam.min_pos;
    out->maxPosition = seam.max_pos;
    out->windowA = (struct Window *)seam.ref_a;
    out->windowB = (struct Window *)seam.ref_b;
    return ES_OK;
}

LONG esb_move_divider(LONG position)
{
    ESSeam seam;
    ESRect ra, rb;
    struct ESBSnap sa, sb;
    struct Window *wa;
    struct Window *wb;
    int found;

    ObtainSemaphore(&g_sem);
    found = es_gutter_find(&g_registry, 8, &seam);
    ReleaseSemaphore(&g_sem);
    if (!found) {
        return ES_ERR_UNSUPPORTED;
    }

    wa = (struct Window *)seam.ref_a;
    wb = (struct Window *)seam.ref_b;

    /* Both windows must still be there: a divider with one window
     * gone is not a divider, and dropping the state is better than
     * resizing a stranger that inherited the address. */
    if (!esb_sample(wa, &sa) || !esb_sample(wb, &sb)) {
        ObtainSemaphore(&g_sem);
        es_registry_forget(&g_registry, wa);
        es_registry_forget(&g_registry, wb);
        ReleaseSemaphore(&g_sem);
        return ES_ERR_STALE;
    }

    es_gutter_apply(&seam, (int)position, &ra, &rb);

    ObtainSemaphore(&g_sem);
    /* the registry must follow, or the next drag would start from the
     * geometry the windows had before this one */
    es_registry_remember(&g_registry, wa, &sa.box, &ra,
                         es_registry_zone(&g_registry, wa));
    es_registry_remember(&g_registry, wb, &sb.box, &rb,
                         es_registry_zone(&g_registry, wb));
    ReleaseSemaphore(&g_sem);

    ChangeWindowBox(wa, ra.x, ra.y, ra.w, ra.h);
    ChangeWindowBox(wb, rb.x, rb.y, rb.w, rb.h);
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
        es_engine_press(&g_engine, &a);
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
                es_pair_fill(&g_registry, win, a.zone, &s.usable,
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
