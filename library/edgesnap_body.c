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
#include "zones.h"

/* OS4 declares IntuitionBase as struct Library *; classic/MorphOS as
 * struct IntuitionBase *. Same alias trick as the frontend. */
#define ESB_IBASE ((struct IntuitionBase *)IntuitionBase)

#define ESB_EXCLUDE_SLOTS 16
#define ESB_PANEL_SCAN_MAX 16

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

static void esb_usable_area_locked(struct Screen *scr, struct Window *skip,
                                   ESRect *out_usable, ESInsets *out_ins)
{
    ESRect scrrect;
    ESRect panels[ESB_PANEL_SCAN_MAX];
    ESInsets ins;
    struct Window *w;
    int n = 0;
    int top;

    scrrect.x = 0;
    scrrect.y = 0;
    scrrect.w = scr->Width;
    scrrect.h = scr->Height;
    for (w = scr->FirstWindow;
         g_cfg.panel_detect && w != NULL && n < ESB_PANEL_SCAN_MAX;
         w = w->NextWindow) {
        if (w == skip || esb_is_ignored(w)) {
            continue;
        }
        /* Panels do not move, size or sit behind everything. Borderless
         * is deliberately not required: real docks do not guarantee it. */
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
    es_panel_insets(&scrrect, panels, n, g_cfg.panel_margin, &ins);

    ins.l += g_cfg.margin.l;
    ins.r += g_cfg.margin.r;
    ins.t += g_cfg.margin.t;
    ins.b += g_cfg.margin.b;

    top = scr->BarHeight + 1;
    if (ins.t > top) {
        top = ins.t;
    }
    out_usable->x = ins.l;
    out_usable->y = top;
    out_usable->w = scr->Width - ins.l - ins.r;
    out_usable->h = scr->Height - top - ins.b;
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
                 ES_CAP_PREVIEW_OUTLINE | ES_CAP_HOTKEYS;

    /* ES_CAP_PREVIEW_ALPHA and ES_CAP_GUTTER are not implemented yet:
     * a client must be able to tell, so they stay out of the mask. */
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
            if (v < 1 || v > 200) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.engine.edge_px = (int)v;
            break;
        case ES_OPT_CornerDiv:
            if (v < 2 || v > 16) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.engine.corner_div = (int)v;
            break;
        case ES_OPT_DragMinPx:
            if (v < 1 || v > 200) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.engine.drag_min_px = (int)v;
            break;
        case ES_OPT_MarginLeft:
            if (v < 0) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.l = (int)v;
            break;
        case ES_OPT_MarginTop:
            if (v < 0) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.t = (int)v;
            break;
        case ES_OPT_MarginRight:
            if (v < 0) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.r = (int)v;
            break;
        case ES_OPT_MarginBottom:
            if (v < 0) {
                return ES_ERR_BAD_ARGS;
            }
            cfg.margin.b = (int)v;
            break;
        case ES_OPT_PanelDetect:
            cfg.panel_detect = v ? 1 : 0;
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

static void esb_report_clear(ESBReport *out)
{
    out->drag_started = 0;
    out->zone_changed = 0;
    out->zone = ES_ZONE_NONE;
    out->preview_show = 0;
    out->preview_hide = 0;
    out->preview_rect.x = out->preview_rect.y = 0;
    out->preview_rect.w = out->preview_rect.h = 0;
    out->preview_screen = NULL;
    out->snapped = 0;
    out->snap_zone = ES_ZONE_NONE;
    out->snap_rc = ES_OK;
    out->snap_win = NULL;
}

static void esb_absorb(const ESEngineActions *a, ESBReport *out)
{
    if (a->drag_started) {
        out->drag_started = 1;
    }
    if (a->zone_changed) {
        out->zone_changed = 1;
        out->zone = a->zone;
    }
    if (a->hide_preview) {
        out->preview_hide = 1;
        out->preview_show = 0;
    }
    if (a->show_preview) {
        out->preview_show = 1;
        out->preview_rect = a->preview_rect;
    }
}

void esb_input(int press, int motion, int release, ULONG quals,
               ESBReport *out)
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
            out->preview_screen = s.scr;
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
        out->snap_zone = snap_zone;
        out->snap_win = win;
        out->snap_rc = esb_snap_rect(win, (ULONG)snap_zone, &snap_rect);
    }
}

void esb_input_reset(ESBReport *out)
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
