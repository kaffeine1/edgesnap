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
    e->candidate = 0;
    e->zone = ES_ZONE_NONE;
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

void es_engine_press(ESEngine *e, ESEngineActions *out)
{
    es_actions_clear(out);
    /* A stray preview can only exist if a release was lost; be safe. */
    if (e->zone != ES_ZONE_NONE) {
        out->hide_preview = 1;
    }
    es_engine_clear_tracking(e);
    e->button_down = 1;
}

void es_engine_motion(ESEngine *e, const ESWinFacts *facts,
                      ESEngineActions *out)
{
    es_actions_clear(out);
    out->zone = e->zone;

    if (!e->button_down) {
        return;
    }
    if (facts == 0) {
        e->candidate = 0;
        return;
    }
    e->last = *facts;

    if (e->candidate != facts->ref) {
        /* first sight of this window during the press: baseline it */
        e->candidate = facts->ref;
        e->base_box = facts->box;
        e->base_mx = facts->mouse_x;
        e->base_my = facts->mouse_y;
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
                    facts->mouse_x >= facts->box.x &&
                    facts->mouse_x < facts->box.x + facts->box.w &&
                    facts->mouse_y >= facts->box.y &&
                    facts->mouse_y < facts->box.y + facts->bar_h;
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
        int z = es_zone_from_pointer(&facts->usable, facts->mouse_x,
                                     facts->mouse_y, e->cfg.edge_px,
                                     facts->usable.h / e->cfg.corner_div);

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
