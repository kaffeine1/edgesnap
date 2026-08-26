/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * panels.c - dock/panel edge-reservation policy.
 */

#include "panels.h"

/* Which edge does this box reserve? The thin dimension picks the
 * orientation; the nearer edge wins if the box sits in its outer band. */
int es_panel_classify(const ESRect *scr, const ESRect *b)
{
    int horizontal = (b->w >= b->h);

    if (horizontal) {
        int gap_top = b->y - scr->y;
        int gap_bottom = (scr->y + scr->h) - (b->y + b->h);
        int band = scr->h / ES_PANEL_MAX_GAP_DIV;

        if (b->h > scr->h / ES_PANEL_MAX_THICK_DIV) {
            return ES_PEDGE_NONE;
        }
        if (b->w * 100 < scr->w * ES_PANEL_MIN_LEN_PCT) {
            return ES_PEDGE_NONE;
        }
        if (gap_top <= gap_bottom && gap_top <= band) {
            return ES_PEDGE_TOP;
        }
        if (gap_bottom < gap_top && gap_bottom <= band) {
            return ES_PEDGE_BOTTOM;
        }
        return ES_PEDGE_NONE;
    }
    {
        int gap_left = b->x - scr->x;
        int gap_right = (scr->x + scr->w) - (b->x + b->w);
        int band = scr->w / ES_PANEL_MAX_GAP_DIV;

        if (b->w > scr->w / ES_PANEL_MAX_THICK_DIV) {
            return ES_PEDGE_NONE;
        }
        if (b->h * 100 < scr->h * ES_PANEL_MIN_LEN_PCT) {
            return ES_PEDGE_NONE;
        }
        if (gap_left <= gap_right && gap_left <= band) {
            return ES_PEDGE_LEFT;
        }
        if (gap_right < gap_left && gap_right <= band) {
            return ES_PEDGE_RIGHT;
        }
        return ES_PEDGE_NONE;
    }
}

static int es_max(int a, int b)
{
    return a > b ? a : b;
}

void es_panel_insets(const ESRect *scr, const ESRect *boxes, int count,
                     int margin_px, ESInsets *out)
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

    /* breathing room: snapped windows never sit glued to a panel */
    if (out->l > 0) {
        out->l += margin_px;
    }
    if (out->r > 0) {
        out->r += margin_px;
    }
    if (out->t > 0) {
        out->t += margin_px;
    }
    if (out->b > 0) {
        out->b += margin_px;
    }
}
