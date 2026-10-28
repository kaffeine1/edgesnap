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
    /*
     * The corner only where the pointer stands in it. With a quarter of
     * the height at each end (corner_div 4, until 2026-09-28) half of
     * every side edge was corner, and a window dragged sideways by a
     * title bar in the upper part of the screen landed in a quarter
     * when a half was meant, which is how most windows are dragged.
     * The screen's edges stop the pointer, so the corner itself is an
     * easy target: throw the pointer into it.
     */
    cfg->corner_div = 0;
    cfg->drag_min_px = 4;
    cfg->push_px = 24;
    cfg->zones_mask = ES_ZONEMASK_ALL;
}

int es_engine_top_px(const ESEngineConfig *cfg)
{
    return cfg->corner_div <= 1 ? 0 : cfg->edge_px;
}

int es_engine_corner_px(const ESEngineConfig *cfg, int usable_h)
{
    if (cfg->corner_div <= 1) {
        return 0;
    }
    return usable_h / cfg->corner_div;
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
    out->do_drop = 0;
    out->drop_ref = 0;
    out->drop_start = out->preview_rect;
    out->drop_start_x = out->drop_start_y = 0;
    out->drop_min_px = 0;
    out->drop_from = out->preview_rect;
    out->drop_grab_x = out->drop_grab_y = 0;
    out->drop_x = out->drop_y = 0;
}

static void es_engine_clear_tracking(ESEngine *e)
{
    e->button_down = 0;
    e->dragging = 0;
    e->on_bar = 0;
    e->followed = 0;
    e->press_mx = -1;
    e->press_my = -1;
    e->press_ref = 0;
    e->candidate = 0;
    e->zone = ES_ZONE_NONE;
    e->push_acc_x = 0;
    e->push_acc_y = 0;
    e->push_wall_x = 0;
    e->push_wall_y = 0;
}

/*
 * One axis of the push against a wall. A window that may not leave the
 * screen (MorphOS as delivered, AROS with "offscreen move" off, AmigaOS
 * 4 with off-screen dragging off) stops at the edge, and Intuition keeps
 * the pointer where it grabbed the title bar: the pointer never reaches
 * the edge, but the mouse's own travel still arrives. Pushed on far
 * enough against an edge the window is flush with, the pointer counts as
 * being ON that edge.
 *
 * Under a real hand (MorphOS, 2026-10-05) the first form of this never
 * got there: it started the count over at every report without travel
 * on its axis, and a hand mixes the two axes all the time. So:
 *  - a report with no travel on this axis changes nothing; only the
 *    pointer moving on this axis, which means it is free, starts over;
 *  - travel towards a side the window is not flush with builds nothing;
 *  - until the wall is reached, travel on the other axis wears the count
 *    down by half of itself, so a drift riding along a push never adds
 *    up to a wall (no corner by accident), while a push into a corner,
 *    at an angle steeper than one in two, does;
 *  - once at the wall the axis stays there until the pointer is free
 *    again or has pushed back half the way: the zone does not come and
 *    go under a shaking hand.
 * The count is kept in half pixels, for that half. push_px 0 or less
 * turns the push off. Returns -1 at the low wall (left, top), 1 at the
 * high one (right, bottom), 0 when the pointer is where it is.
 */
static int es_push_axis(int *acc, int *wall, int push, int perp,
                        int pinned, int flush_lo, int flush_hi, int push_px)
{
    int reach = 2 * push_px;         /* the count needed, half pixels */
    int add = 2 * push;
    int wear;

    if (!pinned || push_px <= 0) {
        *acc = 0;
        *wall = 0;
        return 0;
    }
    if ((add < 0 && !flush_lo && *acc <= 0) ||
        (add > 0 && !flush_hi && *acc >= 0)) {
        add = 0;                     /* no wall on that side */
    }
    *acc += add;
    if (*wall == 0) {
        wear = es_abs(perp);
        if (*acc > 0) {
            *acc = *acc > wear ? *acc - wear : 0;
        } else if (*acc < 0) {
            *acc = -*acc > wear ? *acc + wear : 0;
        }
    }
    if (*acc > 2 * reach) {
        *acc = 2 * reach;
    } else if (*acc < -2 * reach) {
        *acc = -2 * reach;
    }
    if (*wall == 0) {
        if (*acc <= -reach && flush_lo) {
            *wall = -1;
        } else if (*acc >= reach && flush_hi) {
            *wall = 1;
        }
    } else if ((*wall < 0 && (*acc > -reach / 2 || !flush_lo)) ||
               (*wall > 0 && (*acc < reach / 2 || !flush_hi))) {
        *wall = 0;
    }
    return *wall;
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

void es_engine_press_on(ESEngine *e, void *ref, const ESRect *box,
                        int mx, int my, ESEngineActions *out)
{
    es_engine_press(e, mx, my, out);
    if (ref != 0 && box != 0) {
        e->press_ref = ref;
        e->press_box = *box;
    }
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
        const ESRect *b;

        e->candidate = facts->ref;
        /*
         * The window that was active at the press, as it stood then
         * (2.24, es_engine_press_on): the press position and that box
         * were sampled together. On AmigaOS 4 and MorphOS the window
         * follows the pointer, and when the press and the first motion
         * are handled apart a fast pull can have taken it well down
         * before this sample; the press would then miss its bar.
         */
        if (e->press_ref != 0 && facts->ref == e->press_ref) {
            e->base_box = e->press_box;
        } else {
            e->base_box = facts->box;
        }
        b = &e->base_box;
        e->base_mx = px;
        e->base_my = py;
        e->dragging = 0;
        e->followed = 0;
        e->first_x = facts->box.x;
        e->first_y = facts->box.y;
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
                    px >= b->x && px < b->x + b->w &&
                    py >= b->y && py < b->y + facts->bar_h;
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
        ESRect drawn;
        int slack = 0;
        int z, wall;

        /* Evidence independent of the press: the box moved from its
         * first sight during a recognised pointer drag. Keep it even
         * if the box later returns there. A left/top resize can do
         * this too; es_registry_drag_out refuses its changed size. */
        if (b->x != e->first_x || b->y != e->first_y) {
            e->followed = 1;
        }

        /*
         * Where the window is being drawn. Dragged as an outline (AROS
         * as delivered, the Raspberry Pi 400, 2026-10-28) the box stays
         * where it was until the release, and a window only held at the
         * edge by its outline was never flush: the push never counted.
         * Until the box has moved, the outline is the box carried by
         * the pointer from the press. Two pixels of slack for where
         * Intuition and we sampled the grab apart; the pointer must
         * still stand still for a push to count.
         */
        drawn = *b;
        if (!e->followed) {
            drawn.x = e->base_box.x + (mx - e->base_mx);
            drawn.y = e->base_box.y + (my - e->base_my);
            slack = 2;
        }

        /* a pointer held back by a window that may not leave the
         * screen: see es_push_axis() */
        wall = es_push_axis(&e->push_acc_x, &e->push_wall_x,
                            facts->push_x, facts->push_y, mx == prev_mx,
                            drawn.x <= u->x + slack,
                            drawn.x + drawn.w >= u->x + u->w - slack,
                            e->cfg.push_px);
        if (wall < 0) {
            mx = u->x;
        } else if (wall > 0) {
            mx = u->x + u->w - 1;
        }
        wall = es_push_axis(&e->push_acc_y, &e->push_wall_y,
                            facts->push_y, facts->push_x, my == prev_my,
                            drawn.y <= u->y + slack,
                            drawn.y + drawn.h >= u->y + u->h - slack,
                            e->cfg.push_px);
        if (wall < 0) {
            my = u->y;
        } else if (wall > 0) {
            my = u->y + u->h - 1;
        }
        z = es_zone_from_pointer_top(u, mx, my, e->cfg.edge_px,
                                     es_engine_corner_px(&e->cfg, u->h),
                                     es_engine_top_px(&e->cfg));

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
    } else if (e->dragging && (e->on_bar || e->followed) &&
               e->zone == ES_ZONE_NONE &&
               e->candidate != 0 &&
               (e->last.flags & ES_WF_SNAPPABLE) != 0) {
        /*
         * 2.24: let go outside every zone, a zone switched off
         * included. From 2.25 a box that followed the pointer also
         * qualifies, even if a late press or activation missed its
         * bar. Keep the start box and pointer for the executor,
         * separate from the box and pointer that supply the grab.
         */
        out->do_drop = 1;
        out->drop_ref = e->candidate;
        out->drop_start = e->base_box;
        /* The pointer that the drag detector used, even when the
         * grab must come from a later sample. With an unknown press,
         * base_mx/my are the pointer at the candidate's first sight. */
        out->drop_start_x = e->base_mx;
        out->drop_start_y = e->base_my;
        out->drop_min_px = e->cfg.drag_min_px;
        if (e->on_bar) {
            out->drop_from = e->base_box;
            out->drop_grab_x = e->base_mx;
            out->drop_grab_y = e->base_my;
        } else {
            /* The press and base_box failed the bar test. Use the
             * latest motion's box and pointer, sampled together:
             * the window lags least as the pointer slows before the
             * release. The same pair feeds drop_point and unsnap. */
            out->drop_from = e->last.box;
            out->drop_grab_x = e->last.mouse_x;
            out->drop_grab_y = e->last.mouse_y;
        }
        out->drop_x = e->last.mouse_x;
        out->drop_y = e->last.mouse_y;
    }
    es_engine_clear_tracking(e);
}

/* Unsigned distances also cover coordinates at opposite int limits. */
static int es_drop_travelled(int sx, int sy, int ex, int ey, int min_px)
{
    unsigned int dx = ex >= sx ? (unsigned int)ex - (unsigned int)sx
                               : (unsigned int)sx - (unsigned int)ex;
    unsigned int dy = ey >= sy ? (unsigned int)ey - (unsigned int)sy
                               : (unsigned int)sy - (unsigned int)ey;

    return min_px <= 0 || dx >= (unsigned int)min_px ||
                         dy >= (unsigned int)min_px;
}

int es_engine_drop_moved(const ESEngineActions *a, int mx, int my)
{
    return es_drop_travelled(a->drop_start_x, a->drop_start_y,
                             mx, my, a->drop_min_px);
}

void es_engine_drop_point(const ESEngineActions *a, const ESRect *live,
                           int mx, int my,
                           int *px, int *py)
{
    if (!es_drop_travelled(a->drop_start.x, a->drop_start.y,
                           live->x, live->y, a->drop_min_px) &&
        es_engine_drop_moved(a, mx, my)) {
        /* A full-height window kept on screen may not move at all.
         * Its box cannot locate the drop, so use the pointer sampled
         * with it. When the box did follow, keep the grab offset:
         * a pointer read after release can be anywhere a fling took
         * it, whereas the box stays where the drag left it. */
        *px = mx;
        *py = my;
    } else {
        *px = live->x + (a->drop_grab_x - a->drop_from.x);
        *py = live->y + (a->drop_grab_y - a->drop_from.y);
    }
}

void es_engine_reset(ESEngine *e, ESEngineActions *out)
{
    es_actions_clear(out);
    out->hide_preview = 1;
    es_engine_clear_tracking(e);
}
