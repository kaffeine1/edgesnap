/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * panels.c - dock/panel edge-reservation policy.
 */

#include "panels.h"

/* Which edge does this box reserve? The thin dimension picks the
 * orientation; flushness picks the side. */
int es_panel_classify(const ESRect *scr, const ESRect *b)
{
    int horizontal = (b->w >= b->h);

    if (horizontal) {
        if (b->h > scr->h / ES_PANEL_MAX_THICK_DIV) {
            return ES_PEDGE_NONE;
        }
        if (b->w * 100 < scr->w * ES_PANEL_MIN_LEN_PCT) {
            return ES_PEDGE_NONE;
        }
        if (b->y <= scr->y + ES_PANEL_FLUSH_TOL) {
            return ES_PEDGE_TOP;
        }
        if (b->y + b->h >= scr->y + scr->h - ES_PANEL_FLUSH_TOL) {
            return ES_PEDGE_BOTTOM;
        }
        return ES_PEDGE_NONE;
    }
    if (b->w > scr->w / ES_PANEL_MAX_THICK_DIV) {
        return ES_PEDGE_NONE;
    }
    if (b->h * 100 < scr->h * ES_PANEL_MIN_LEN_PCT) {
        return ES_PEDGE_NONE;
    }
    if (b->x <= scr->x + ES_PANEL_FLUSH_TOL) {
        return ES_PEDGE_LEFT;
    }
    if (b->x + b->w >= scr->x + scr->w - ES_PANEL_FLUSH_TOL) {
        return ES_PEDGE_RIGHT;
    }
    return ES_PEDGE_NONE;
}

static int es_max(int a, int b)
{
    return a > b ? a : b;
}

void es_panel_insets(const ESRect *scr, const ESRect *boxes, int count,
                     ESInsets *out)
{
    int i;

    out->l = out->t = out->r = out->b = 0;
    for (i = 0; i < count; i++) {
        const ESRect *b = &boxes[i];

        switch (es_panel_classify(scr, b)) {
        case ES_PEDGE_LEFT:
            out->l = es_max(out->l, (b->x + b->w) - scr->x);
            break;
        case ES_PEDGE_RIGHT:
            out->r = es_max(out->r, (scr->x + scr->w) - b->x);
            break;
        case ES_PEDGE_TOP:
            out->t = es_max(out->t, (b->y + b->h) - scr->y);
            break;
        case ES_PEDGE_BOTTOM:
            out->b = es_max(out->b, (scr->y + scr->h) - b->y);
            break;
        default:
            break;
        }
    }
}
