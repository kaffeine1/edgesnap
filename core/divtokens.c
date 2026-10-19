/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * divtokens.c - divider tokens of a layout group, the order a tiler is
 * told about seams in, and the covered-seam walk.
 */

#include "divtokens.h"

/* The bits a mask may hold: one for each of ES_DIVTOK_MEMBERS. */
#define ES_DIVTOK_ALL 0xFFFFFFFFUL

/* How many indices a rank of unsigned chars can hold. */
#define ES_DIVIDER_RANKS 256

/* A set a token can name: a window on each side, none on both, and
 * only members a mask can name. */
static int es_divtok_nameable(const ESDivSet *s)
{
    return s->a != 0 && s->b != 0 && (s->a & s->b) == 0 &&
           (s->a & ~ES_DIVTOK_ALL) == 0 && (s->b & ~ES_DIVTOK_ALL) == 0;
}

/* The slot whose token names exactly this set, on this axis, or -1. */
static int es_divtok_holding(const ESDivTokens *t, const ESDivSet *s)
{
    int j;

    if (!es_divtok_nameable(s)) {
        return -1;
    }
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        if (t->slot[j].token != 0 && t->slot[j].a == s->a &&
            t->slot[j].b == s->b &&
            (t->slot[j].vertical != 0) == (s->vertical != 0)) {
            return j;
        }
    }
    return -1;
}

/* Where a new token goes: the slot of the smallest token this query
 * has not claimed, else -1. A free slot holds 0, the smallest of all,
 * so it goes before any token is evicted. */
static int es_divtok_room(const ESDivTokens *t, const unsigned char *claimed)
{
    int j, oldest = -1;

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        if (!claimed[j] &&
            (oldest < 0 || t->slot[j].token < t->slot[oldest].token)) {
            oldest = j;
        }
    }
    return oldest;
}

/* The counter's value, and the counter moved on: never 0, never past
 * ES_DIVTOK_MAX (an unsigned long may be wider). */
static unsigned long es_divtok_take(unsigned long *next)
{
    unsigned long v = *next & ES_DIVTOK_MAX;

    if (v == 0) {
        v = 1;
    }
    *next = (v == ES_DIVTOK_MAX) ? 1 : v + 1;
    return v;
}

void es_divtok_clear(ESDivTokens *t)
{
    int j;

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        es_divtok_retire(t, j);
    }
}

int es_divtok_find(const ESDivTokens *t, unsigned long token)
{
    int j;

    if (token == 0) {
        return -1;
    }
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        if (t->slot[j].token == token) {
            return j;
        }
    }
    return -1;
}

void es_divtok_reuse(const ESDivTokens *t, const ESDivSet *s, int n,
                     unsigned long *tok, unsigned char *claimed)
{
    int i, j;

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        claimed[j] = 0;
    }
    for (i = 0; i < n; i++) {
        tok[i] = 0;
        j = es_divtok_holding(t, &s[i]);
        if (j >= 0) {
            tok[i] = t->slot[j].token;
            claimed[j] = 1;
        }
    }
}

void es_divtok_retire(ESDivTokens *t, int slot)
{
    if (slot < 0 || slot >= ES_DIVTOK_SLOTS) {
        return;
    }
    t->slot[slot].token = 0;
    t->slot[slot].a = 0;
    t->slot[slot].b = 0;
    t->slot[slot].vertical = 0;
}

void es_divtok_mint(ESDivTokens *t, const ESDivSet *s, int n,
                    unsigned long *tok, unsigned char *claimed,
                    unsigned long *next)
{
    int i, j;

    for (i = 0; i < n; i++) {
        if (tok[i] != 0 || !es_divtok_nameable(&s[i])) {
            continue;
        }
        /* One slot per set: a seam equal to one minted a moment ago
         * shares its token (pass 1 found every older one). */
        j = es_divtok_holding(t, &s[i]);
        if (j < 0) {
            j = es_divtok_room(t, claimed);
            if (j < 0) {
                continue;   /* every slot is a token of this query */
            }
            t->slot[j].token = es_divtok_take(next);
            t->slot[j].a = s[i].a;
            t->slot[j].b = s[i].b;
            t->slot[j].vertical = s[i].vertical != 0;
        }
        tok[i] = t->slot[j].token;
        claimed[j] = 1;
    }
}

void es_divtok_member_left(ESDivTokens *t, int k, int last)
{
    unsigned long bk, bl, gone;
    int j;

    if (k < 0 || last < k || last >= ES_DIVTOK_MEMBERS) {
        return;
    }
    bk = ES_DIVTOK_BIT(k);
    bl = ES_DIVTOK_BIT(last);
    gone = ~(bl - 1UL);   /* last and beyond: no such member any more */
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        ESDivToken *e = &t->slot[j];

        if (e->token == 0) {
            continue;
        }
        if (((e->a | e->b) & bk) != 0) {
            es_divtok_retire(t, j);
            continue;
        }
        if ((e->a & bl) != 0) {
            e->a = (e->a & ~bl) | bk;
        }
        if ((e->b & bl) != 0) {
            e->b = (e->b & ~bl) | bk;
        }
        if (((e->a | e->b) & gone) != 0) {
            es_divtok_retire(t, j);
        }
    }
}

/* Where a seam's strip starts along it. */
static int es_divider_from(const ESSeam *s)
{
    return s->vertical ? s->rect.y : s->rect.x;
}

/* Is seam x told about before seam y? Equal ones are not. */
static int es_divider_before(const ESSeam *x, const ESSeam *y)
{
    int xv = x->vertical != 0, yv = y->vertical != 0;

    if (xv != yv) {
        return xv;
    }
    if (x->pos != y->pos) {
        return x->pos < y->pos;
    }
    return es_divider_from(x) < es_divider_from(y);
}

void es_divider_order(const ESSeam *s, int n, unsigned char *rank)
{
    int i, j;
    unsigned char r;

    if (n > ES_DIVIDER_RANKS) {
        n = ES_DIVIDER_RANKS;
    }
    /* Insertion: stable, and n is a few dozen at most. */
    for (i = 0; i < n; i++) {
        r = (unsigned char)i;
        for (j = i; j > 0 && es_divider_before(&s[r], &s[rank[j - 1]]);
             j--) {
            rank[j] = rank[j - 1];
        }
        rank[j] = r;
    }
}

void es_divider_side_order(const ESSeamSide *side, int vertical,
                           unsigned char *rank)
{
    int i, j, n = side->n;
    unsigned char r;

    if (n > ES_SEAM_SIDE_MAX) {
        n = ES_SEAM_SIDE_MAX;
    }
    for (i = 0; i < n; i++) {
        r = (unsigned char)i;
        for (j = i; j > 0; j--) {
            const ESRect *x = &side->box[r], *y = &side->box[rank[j - 1]];

            if (vertical ? x->y >= y->y : x->x >= y->x) {
                break;
            }
            rank[j] = rank[j - 1];
        }
        rank[j] = r;
    }
}

int es_divider_follows(int rvertical, int rpos, int rlast,
                       int vertical, int line)
{
    unsigned long d;

    if ((rvertical != 0) != (vertical != 0)) {
        return 0;
    }
    if (line == rlast) {
        return 1;
    }
    /* The distance in unsigned arithmetic: two ints from a client may
     * lie further apart than an int can say. */
    d = (line >= rpos) ? (unsigned long)line - (unsigned long)rpos
                       : (unsigned long)rpos - (unsigned long)line;
    return d <= (unsigned long)ES_SEAM_TOUCH_TOL;
}

static int es_cover_decide(ESCoverWalk *w, int visible)
{
    w->visible = visible;
    w->done = 1;
    return 1;
}

void es_cover_begin(ESCoverWalk *w, int facing_a, int facing_b)
{
    /* Only in the gap, between a window of each side, may a window be
     * passed over; beside one side alone the point lies in a hole of
     * the strip, and whatever holds it there shows. A side holds no
     * more than ES_SEAM_SIDE_MAX windows. */
    if (facing_a <= 0 || facing_b <= 0) {
        w->facing = 0;
    } else {
        if (facing_a > ES_SEAM_SIDE_MAX) {
            facing_a = ES_SEAM_SIDE_MAX;
        }
        if (facing_b > ES_SEAM_SIDE_MAX) {
            facing_b = ES_SEAM_SIDE_MAX;
        }
        w->facing = facing_a + facing_b;
    }
    w->unseen = w->facing;
    w->done = 0;
    w->visible = 1;
}

int es_cover_step(ESCoverWalk *w, int kind, int holds_point)
{
    if (w->done) {
        return 1;
    }
    switch (kind) {
    case ES_LAYER_FACING:
        if (w->unseen > 0) {
            w->unseen--;
        }
        if (holds_point) {
            return es_cover_decide(w, 1);
        }
        break;
    case ES_LAYER_SEAM:
    case ES_LAYER_BACKDROP:
        if (holds_point) {
            return es_cover_decide(w, 1);
        }
        break;
    case ES_LAYER_FOREIGN:
        /* In front of a facing window not met yet, or no facing window
         * at all: it covers the point. Behind all of them: not. */
        if (holds_point && (w->facing == 0 || w->unseen > 0)) {
            return es_cover_decide(w, 0);
        }
        break;
    default:
        break;
    }
    return 0;
}
