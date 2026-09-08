/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * engine.c - portable EdgeSnap drag/snap state machine.
 *
 * Behavior extracted from the phase 0 spike exactly as validated on
 * AmigaOS 4 (2026-08-11): drag detection correlates pointer travel with
 * the active window's own movement; zones trigger on the pointer.
 */

#include "engine.h"

static int es_abs(int v)
{
    return v < 0 ? -v : v;
}

void es_engine_config_defaults(ESEngineConfig *cfg)
{
    cfg->edge_px = 12;
    cfg->corner_div = 4;
    cfg->drag_min_px = 4;
    cfg->push_px = 24;
    cfg->zones_mask = ES_ZONEMASK_ALL;
}

static void es_actions_clear(ESEngineActions *out)
{
    out->show_preview = 0;
    out->hide_preview = 0;
    out->do_snap = 0;
    out->drag_started = 0;
    out->zone_changed = 0;
    out->zone = ES_ZONE_NONE;
    out->preview_rect.x = out->preview_rect.y = 0;
    out->preview_rect.w = out->preview_rect.h = 0;
    out->preview_ref = 0;
    out->snap_ref = 0;
    out->snap_zone = ES_ZONE_NONE;
    out->snap_rect = out->preview_rect;
}

static void es_engine_clear_tracking(ESEngine *e)
{
    e->button_down = 0;
    e->dragging = 0;
    e->on_bar = 0;
    e->press_mx = -1;
    e->press_my = -1;
    e->candidate = 0;
    e->zone = ES_ZONE_NONE;
    e->push_acc_x = 0;
    e->push_acc_y = 0;
}

void es_engine_init(ESEngine *e, const ESEngineConfig *cfg)
{
    if (cfg != 0) {
        e->cfg = *cfg;
    } else {
        es_engine_config_defaults(&e->cfg);
    }
    es_engine_clear_tracking(e);
}

void es_engine_press(ESEngine *e, int mx, int my, ESEngineActions *out)
{
    es_actions_clear(out);
    /* A stray preview can only exist if a release was lost; be safe. */
    if (e->zone != ES_ZONE_NONE) {
        out->hide_preview = 1;
    }
    es_engine_clear_tracking(e);
    e->button_down = 1;
    e->press_mx = mx;
    e->press_my = my;
}

void es_engine_motion(ESEngine *e, const ESWinFacts *facts,
                      ESEngineActions *out)
{
    int prev_mx, prev_my;

    es_actions_clear(out);
    out->zone = e->zone;

    if (!e->button_down) {
        return;
    }
    if (facts == 0) {
        e->candidate = 0;
        return;
    }
    prev_mx = e->last.mouse_x;
    prev_my = e->last.mouse_y;
    e->last = *facts;

    if (e->candidate != facts->ref) {
        /*
         * First sight of this window during the press: baseline it.
         * From the PRESS position when it is known. On a system that
         * activates the pressed window a beat after the press (AROS),
         * the first facts still name the old window, and the pressed
         * one is first seen with the pointer already away from where
         * the button went down; judged by the pointer, a press on the
         * bar would look like a press in the body.
         */
        int px = e->press_mx >= 0 ? e->press_mx : facts->mouse_x;
        int py = e->press_my >= 0 ? e->press_my : facts->mouse_y;

        e->candidate = facts->ref;
        e->base_box = facts->box;
        e->base_mx = px;
        e->base_my = py;
        e->dragging = 0;
        /*
         * Did the press land on the drag bar? Remembered now, because
         * on a system that drags windows as an outline the window box
         * does not change until the button is released (AROS One,
         * 2026-09-03), and "the window moved while the pointer moved"
         * would never become true. A press on the bar plus pointer
         * travel is a drag on its own evidence.
         */
        e->on_bar = (facts->flags & ES_WF_DRAGBAR) != 0 &&
                    facts->bar_h > 0 &&
                    px >= facts->box.x && px < facts->box.x + facts->box.w &&
                    py >= facts->box.y && py < facts->box.y + facts->bar_h;
        return;
    }

    if (!e->dragging) {
        int win_moved = (facts->box.x != e->base_box.x ||
                         facts->box.y != e->base_box.y);
        int ptr_moved = (es_abs(facts->mouse_x - e->base_mx) +
                         es_abs(facts->mouse_y - e->base_my)) >=
                        e->cfg.drag_min_px;
        if (ptr_moved && (facts->flags & ES_WF_DRAGBAR) != 0 &&
            (win_moved || e->on_bar)) {
            e->dragging = 1;
            out->drag_started = 1;
        }
    }

    if (e->dragging) {
        int mx = facts->mouse_x;
        int my = facts->mouse_y;
        const ESRect *u = &facts->usable;
        const ESRect *b = &facts->box;
        int z;

        /*
         * A window that may not leave the screen (MorphOS as delivered,
         * AROS with "offscreen move" off) stops at the edge, and
         * Intuition keeps the pointer where it grabbed the title bar:
         * the pointer never reaches the edge, and nothing would ever
         * snap. The raw travel of the mouse still arrives. So while the
         * pointer stands still on an axis and the raw travel keeps
         * pushing, it is accumulated; once it has pushed far enough
         * against an edge the window is flush with, the pointer counts
         * as being ON that edge. A pointer that moves on that axis is
         * free, and the count starts over.
         */
        if (facts->push_x != 0 && mx == prev_mx) {
            e->push_acc_x += facts->push_x;
        } else {
            e->push_acc_x = 0;
        }
        if (facts->push_y != 0 && my == prev_my) {
            e->push_acc_y += facts->push_y;
        } else {
            e->push_acc_y = 0;
        }
        if (e->push_acc_x <= -e->cfg.push_px && b->x <= u->x) {
            mx = u->x;
        } else if (e->push_acc_x >= e->cfg.push_px &&
                   b->x + b->w >= u->x + u->w) {
            mx = u->x + u->w - 1;
        }
        if (e->push_acc_y <= -e->cfg.push_px && b->y <= u->y) {
            my = u->y;
        } else if (e->push_acc_y >= e->cfg.push_px &&
                   b->y + b->h >= u->y + u->h) {
            my = u->y + u->h - 1;
        }
        z = es_zone_from_pointer(u, mx, my, e->cfg.edge_px,
                                 u->h / e->cfg.corner_div);

        /* Holding the bypass qualifier, or landing in a zone the user
         * switched off, means "just move the window". */
        if ((facts->flags & ES_WF_BYPASS) != 0) {
            z = ES_ZONE_NONE;
        } else if (z != ES_ZONE_NONE &&
                   (e->cfg.zones_mask & ES_ZONEBIT(z)) == 0) {
            z = ES_ZONE_NONE;
        }
        if (z != e->zone) {
            e->zone = z;
            out->zone_changed = 1;
            out->zone = z;
            if (z != ES_ZONE_NONE &&
                (facts->flags & ES_WF_SNAPPABLE) != 0) {
                es_fit_zone_rect(z, &facts->usable, facts->min_w,
                                 facts->min_h, facts->max_w, facts->max_h,
                                 &e->zone_rect);
                out->show_preview = 1;
                out->preview_rect = e->zone_rect;
                out->preview_ref = e->candidate;
            } else {
                out->hide_preview = 1;
            }
        }
    }
}

void es_engine_release(ESEngine *e, ESEngineActions *out)
{
    es_actions_clear(out);
    if (!e->button_down) {
        return;
    }
    out->hide_preview = 1;
    if (e->dragging && e->zone != ES_ZONE_NONE && e->candidate != 0 &&
        (e->last.flags & ES_WF_SNAPPABLE) != 0) {
        out->do_snap = 1;
        out->snap_ref = e->candidate;
        out->snap_zone = e->zone;
        out->snap_rect = e->zone_rect;
    }
    es_engine_clear_tracking(e);
}

void es_engine_reset(ESEngine *e, ESEngineActions *out)
{
    es_actions_clear(out);
    out->hide_preview = 1;
    es_engine_clear_tracking(e);
}
