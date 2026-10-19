/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * divtokens_test.c - executable specification of the divider tokens of
 * a layout group, the order a tiler is told about seams in, the rule a
 * drag follows its seam by, and the covered-seam walk.
 */

#include <limits.h>
#include <stdio.h>

#include "divtokens.h"

static int g_failures = 0;

#define CHECK(cond) \
    do { \
        if (!(cond)) { \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            g_failures++; \
        } \
    } while (0)

#define M(k) ES_DIVTOK_BIT(k)

/* Big enough for the clamp of es_divider_order; kept out of the stack. */
#define POOL 300
static ESSeam g_pool[POOL];

static ESDivSet set(int vertical, unsigned long a, unsigned long b)
{
    ESDivSet s;

    s.vertical = vertical;
    s.a = a;
    s.b = b;
    return s;
}

static int same_table(const ESDivTokens *x, const ESDivTokens *y)
{
    int j;

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        if (x->slot[j].token != y->slot[j].token ||
            x->slot[j].a != y->slot[j].a || x->slot[j].b != y->slot[j].b ||
            x->slot[j].vertical != y->slot[j].vertical) {
            return 0;
        }
    }
    return 1;
}

static int used_slots(const ESDivTokens *t)
{
    int j, n = 0;

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        if (t->slot[j].token != 0) {
            n++;
        }
    }
    return n;
}

/* A distinct set for every i in 0..63: 32 vertical, 32 horizontal. */
static ESDivSet nth_set(int i)
{
    return set(i < 32, M(i % 32), M((i + 1) % 32));
}

/* A table holding tokens 1..64, token i + 1 in slot i, for nth_set(i). */
static void full_table(ESDivTokens *t, unsigned long *next)
{
    ESDivSet s[ES_DIVTOK_SLOTS];
    unsigned long tok[ES_DIVTOK_SLOTS];
    unsigned char claimed[ES_DIVTOK_SLOTS];
    int i;

    es_divtok_clear(t);
    *next = 1;
    for (i = 0; i < ES_DIVTOK_SLOTS; i++) {
        s[i] = nth_set(i);
    }
    es_divtok_reuse(t, s, ES_DIVTOK_SLOTS, tok, claimed);
    es_divtok_mint(t, s, ES_DIVTOK_SLOTS, tok, claimed, next);
}

/* A whole query as the library runs it, without a retire pass. */
static void query(ESDivTokens *t, const ESDivSet *s, int n,
                  unsigned long *tok, unsigned long *next)
{
    unsigned char claimed[ES_DIVTOK_SLOTS];

    es_divtok_reuse(t, s, n, tok, claimed);
    es_divtok_mint(t, s, n, tok, claimed, next);
}

static void test_clear_and_find_on_an_empty_table(void)
{
    ESDivTokens t;
    int j;

    es_divtok_clear(&t);
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        CHECK(t.slot[j].token == 0);
    }
    CHECK(es_divtok_find(&t, 0) == -1);
    CHECK(es_divtok_find(&t, 1) == -1);
    CHECK(es_divtok_find(&t, ES_DIVTOK_MAX) == -1);
}

static void test_mint_counts_up_and_reuse_gives_the_same(void)
{
    ESDivTokens t;
    ESDivSet s[3], r[3];
    unsigned long tok[3], next = 1;
    unsigned char claimed[ES_DIVTOK_SLOTS];
    int j, s0, s1, s2;

    es_divtok_clear(&t);
    s[0] = set(1, M(0), M(1));
    s[1] = set(1, M(2), M(3) | M(4));
    s[2] = set(0, M(0) | M(1), M(5));
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        claimed[j] = 7;
    }
    es_divtok_reuse(&t, s, 3, tok, claimed);
    CHECK(tok[0] == 0 && tok[1] == 0 && tok[2] == 0);
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        CHECK(claimed[j] == 0);
    }
    es_divtok_mint(&t, s, 3, tok, claimed, &next);
    CHECK(tok[0] == 1 && tok[1] == 2 && tok[2] == 3);
    CHECK(next == 4);
    s0 = es_divtok_find(&t, 1);
    s1 = es_divtok_find(&t, 2);
    s2 = es_divtok_find(&t, 3);
    CHECK(s0 >= 0 && s1 >= 0 && s2 >= 0);
    CHECK(s0 != s1 && s1 != s2 && s0 != s2);
    CHECK(claimed[s0] && claimed[s1] && claimed[s2]);
    CHECK(used_slots(&t) == 3);
    CHECK(t.slot[s1].vertical == 1 && t.slot[s1].a == M(2) &&
          t.slot[s1].b == (M(3) | M(4)));
    CHECK(t.slot[s2].vertical == 0);

    /* the next query, in another order: the same tokens, none minted */
    r[0] = s[2];
    r[1] = s[0];
    r[2] = s[1];
    es_divtok_reuse(&t, r, 3, tok, claimed);
    CHECK(tok[0] == 3 && tok[1] == 1 && tok[2] == 2);
    CHECK(claimed[s0] && claimed[s1] && claimed[s2]);
    es_divtok_mint(&t, r, 3, tok, claimed, &next);
    CHECK(tok[0] == 3 && tok[1] == 1 && tok[2] == 2);
    CHECK(next == 4 && used_slots(&t) == 3);

    /* any non-zero axis is vertical, and a token keeps it as 1 */
    r[0] = set(2, M(0), M(1));
    es_divtok_reuse(&t, r, 1, tok, claimed);
    CHECK(tok[0] == 1);
    r[0] = set(-1, M(6), M(7));
    tok[0] = 0;
    es_divtok_mint(&t, r, 1, tok, claimed, &next);
    CHECK(tok[0] == 4);
    CHECK(t.slot[es_divtok_find(&t, 4)].vertical == 1);
}

/* The same windows above and below each other are another seam: the
 * axis is part of a token's name. */
static void test_axis_change_mints_a_new_token(void)
{
    ESDivTokens t;
    ESDivSet s;
    unsigned long tok, next = 1;

    es_divtok_clear(&t);
    s = set(1, M(0), M(1));
    query(&t, &s, 1, &tok, &next);
    CHECK(tok == 1);
    s = set(0, M(0), M(1));
    query(&t, &s, 1, &tok, &next);
    CHECK(tok == 2);
    CHECK(t.slot[es_divtok_find(&t, 2)].vertical == 0);
    /* the old one stays until the library's retire pass says so */
    CHECK(es_divtok_find(&t, 1) >= 0);
    /* sides swapped: another seam too */
    s = set(1, M(1), M(0));
    query(&t, &s, 1, &tok, &next);
    CHECK(tok == 3);
}

/* Table full; a query of one new seam (listed first) and 63 known ones,
 * the oldest token among them. A single pass would let the new seam
 * evict token 1, which a later seam of the same query then misses: the
 * new seam must take the one slot nobody in the query claims. */
static void test_new_seam_evicts_the_oldest_unclaimed(void)
{
    ESDivTokens t;
    ESDivSet s[ES_DIVTOK_SLOTS];
    unsigned long tok[ES_DIVTOK_SLOTS], next;
    int i, n = 0;

    full_table(&t, &next);
    CHECK(next == 65 && used_slots(&t) == ES_DIVTOK_SLOTS);
    for (i = 0; i < ES_DIVTOK_SLOTS; i++) {
        CHECK(t.slot[i].token == (unsigned long)(i + 1));
    }
    s[n++] = set(1, M(0) | M(2), M(1));
    for (i = ES_DIVTOK_SLOTS - 1; i >= 0; i--) {
        if (i != 39) {   /* token 40 is not in this query */
            s[n++] = nth_set(i);
        }
    }
    CHECK(n == ES_DIVTOK_SLOTS);
    query(&t, s, n, tok, &next);
    CHECK(tok[0] == 65);
    CHECK(es_divtok_find(&t, 65) == 39);
    CHECK(es_divtok_find(&t, 40) == -1);
    CHECK(es_divtok_find(&t, 1) == 0);
    for (i = 1; i < n; i++) {
        CHECK(tok[i] != 0 && tok[i] != 40 && tok[i] != 65);
        CHECK(es_divtok_find(&t, tok[i]) >= 0);
    }
    CHECK(tok[n - 1] == 1);   /* nth_set(0), the oldest, listed last */
    CHECK(next == 66);
}

/* 65 seams on a full table of 64: the extra one gets no token, and no
 * seam of the query loses its own. */
static void test_sixty_five_seams_on_a_full_table(void)
{
    ESDivTokens t, before;
    ESDivSet s[ES_DIVTOK_SLOTS + 1];
    unsigned long tok[ES_DIVTOK_SLOTS + 1], next;
    int i, n = 0;

    full_table(&t, &next);
    before = t;
    for (i = 0; i < 32; i++) {
        s[n++] = nth_set(i);
    }
    s[n++] = set(0, M(3), M(9) | M(10));
    for (i = 32; i < ES_DIVTOK_SLOTS; i++) {
        s[n++] = nth_set(i);
    }
    query(&t, s, n, tok, &next);
    CHECK(tok[32] == 0);
    for (i = 0; i < 32; i++) {
        CHECK(tok[i] == (unsigned long)(i + 1));
        CHECK(tok[i + 33] == (unsigned long)(i + 33));
    }
    CHECK(same_table(&t, &before));
    CHECK(next == 65);
}

/* 65 new seams on a full table of old tokens: the first 64 evict every
 * old one, none evicts a token minted a moment before in the same
 * query, and the 65th finds no room. */
static void test_sixty_five_new_seams(void)
{
    ESDivTokens t;
    ESDivSet s[ES_DIVTOK_SLOTS + 1];
    unsigned long tok[ES_DIVTOK_SLOTS + 1], next;
    int i;

    full_table(&t, &next);
    /* two windows on side b, where every old set has one */
    for (i = 0; i < ES_DIVTOK_SLOTS; i++) {
        s[i] = set(i < 32, M(i % 32),
                   M((i + 1) % 32) | M((i + 2) % 32));
    }
    s[ES_DIVTOK_SLOTS] = set(1, M(0), M(5) | M(9));
    query(&t, s, ES_DIVTOK_SLOTS + 1, tok, &next);
    for (i = 0; i < ES_DIVTOK_SLOTS; i++) {
        CHECK(tok[i] == (unsigned long)(65 + i));
        CHECK(es_divtok_find(&t, tok[i]) == i);
        CHECK(es_divtok_find(&t, (unsigned long)(i + 1)) == -1);
    }
    CHECK(tok[ES_DIVTOK_SLOTS] == 0);
    CHECK(used_slots(&t) == ES_DIVTOK_SLOTS && next == 129);
}

/* Eviction goes by value, not by slot: after evictions the smallest
 * token need not sit in the first slot. A free slot comes before any
 * eviction. */
static void test_eviction_order(void)
{
    ESDivTokens t;
    ESDivSet s[3];
    unsigned long tok[3], next;
    int j;

    full_table(&t, &next);
    s[0] = set(1, M(0), M(5) | M(6));
    s[1] = set(1, M(1), M(5) | M(6));
    s[2] = set(1, M(2), M(5) | M(6));
    query(&t, s, 3, tok, &next);
    CHECK(tok[0] == 65 && tok[1] == 66 && tok[2] == 67);
    CHECK(es_divtok_find(&t, 65) == 0 && es_divtok_find(&t, 66) == 1 &&
          es_divtok_find(&t, 67) == 2);
    for (j = 1; j <= 3; j++) {
        CHECK(es_divtok_find(&t, (unsigned long)j) == -1);
    }
    s[0] = set(0, M(3), M(5) | M(6));
    query(&t, s, 1, tok, &next);
    CHECK(tok[0] == 68);
    CHECK(es_divtok_find(&t, 68) == 3);   /* token 4's slot */
    CHECK(es_divtok_find(&t, 65) == 0);

    es_divtok_retire(&t, 10);             /* token 11 */
    s[0] = set(0, M(4), M(5) | M(6));
    query(&t, s, 1, tok, &next);
    CHECK(tok[0] == 69);
    CHECK(es_divtok_find(&t, 69) == 10);
    CHECK(es_divtok_find(&t, 5) == 4);    /* the oldest stays */
}

static void test_retire(void)
{
    ESDivTokens t, before;
    ESDivSet s;
    unsigned long tok, next = 1;
    int j;

    es_divtok_clear(&t);
    s = set(1, M(0), M(1));
    query(&t, &s, 1, &tok, &next);
    j = es_divtok_find(&t, tok);
    CHECK(tok == 1 && j >= 0);
    before = t;
    es_divtok_retire(&t, -1);
    es_divtok_retire(&t, ES_DIVTOK_SLOTS);
    CHECK(same_table(&t, &before));
    es_divtok_retire(&t, j);
    CHECK(es_divtok_find(&t, 1) == -1);
    CHECK(t.slot[j].a == 0 && t.slot[j].b == 0);
    /* the same windows again: a new value, the old one never returns */
    query(&t, &s, 1, &tok, &next);
    CHECK(tok == 2);
    CHECK(es_divtok_find(&t, 1) == -1);
    es_divtok_clear(&t);
    CHECK(used_slots(&t) == 0 && es_divtok_find(&t, 2) == -1);
}

/* The counter never gives 0 and stays within 32 bits on every system
 * (wrapping round is accepted, see divtokens.h). */
static void test_counter(void)
{
    ESDivTokens t;
    ESDivSet s[2];
    unsigned long tok[2], next, big;

    s[0] = set(1, M(0), M(1));
    s[1] = set(1, M(2), M(3));

    es_divtok_clear(&t);
    next = 0;
    query(&t, s, 1, tok, &next);
    CHECK(tok[0] == 1 && next == 2);

    es_divtok_clear(&t);
    next = ES_DIVTOK_MAX;
    query(&t, s, 1, tok, &next);
    CHECK(tok[0] == ES_DIVTOK_MAX);
    CHECK(next == 1);
    query(&t, s, 2, tok, &next);
    CHECK(tok[0] == ES_DIVTOK_MAX);
    CHECK(tok[1] == 1);
    CHECK(next == 2);
    CHECK(es_divtok_find(&t, ES_DIVTOK_MAX) >= 0);

    /* a counter wider than 32 bits is cut to 32 (on a 32-bit system
     * this is 1 already) */
    es_divtok_clear(&t);
    big = ES_DIVTOK_MAX;
    big = big + 2;
    next = big;
    query(&t, s, 1, tok, &next);
    CHECK(tok[0] == 1 && next == 2);
}

/* Sets no token can name get none and take no slot. */
static void test_sets_no_token_names(void)
{
    ESDivTokens t;
    ESDivSet s[4];
    unsigned long tok[4], next = 1, hi;
    int n = 3, i;

    es_divtok_clear(&t);
    s[0] = set(1, 0, M(1));
    s[1] = set(1, M(0), 0);
    s[2] = set(0, M(0) | M(1), M(1));
    hi = ES_DIVTOK_MAX;
    hi = hi + 1;   /* bit 32 on a 64-bit system, 0 on a 32-bit one */
    if (hi != 0) {
        s[n++] = set(1, hi | M(0), M(1));
    }
    query(&t, s, n, tok, &next);
    for (i = 0; i < n; i++) {
        CHECK(tok[i] == 0);
    }
    CHECK(used_slots(&t) == 0 && next == 1);
}

/* Two seams of one query between the same windows share one token, and
 * mint alone finds a set the table already holds. */
static void test_equal_sets_share_a_token(void)
{
    ESDivTokens t;
    ESDivSet s[3];
    unsigned long tok[3], next = 1;
    unsigned char claimed[ES_DIVTOK_SLOTS];
    int j;

    es_divtok_clear(&t);
    s[0] = set(1, M(0), M(1));
    s[1] = set(0, M(0), M(2));
    s[2] = set(1, M(0), M(1));
    query(&t, s, 3, tok, &next);
    CHECK(tok[0] == 1 && tok[1] == 2 && tok[2] == 1);
    CHECK(used_slots(&t) == 2 && next == 3);

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        claimed[j] = 0;
    }
    tok[0] = 0;
    es_divtok_mint(&t, s, 1, tok, claimed, &next);
    CHECK(tok[0] == 1 && next == 3);
    CHECK(claimed[es_divtok_find(&t, 1)] == 1);

    /* a claimed slot is never taken, even when it is free */
    es_divtok_clear(&t);
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        claimed[j] = (unsigned char)(j != 5);
    }
    tok[0] = 0;
    tok[1] = 0;
    es_divtok_mint(&t, s, 2, tok, claimed, &next);
    CHECK(tok[0] == 3 && tok[1] == 0);
    CHECK(es_divtok_find(&t, 3) == 5 && used_slots(&t) == 1);
}

/* Fill slot j with a token of its own. */
static void put(ESDivTokens *t, int j, unsigned long token, int vertical,
                unsigned long a, unsigned long b)
{
    t->slot[j].token = token;
    t->slot[j].vertical = vertical;
    t->slot[j].a = a;
    t->slot[j].b = b;
}

static void test_member_left(void)
{
    ESDivTokens t;
    ESDivSet s;
    unsigned long tok, next = 100;

    es_divtok_clear(&t);
    put(&t, 0, 1, 1, M(0), M(1));
    put(&t, 1, 2, 1, M(2), M(3));
    put(&t, 2, 3, 0, M(0) | M(2), M(4));
    put(&t, 3, 4, 1, M(5), M(31));
    put(&t, 4, 5, 0, M(31) | M(6), M(7));
    put(&t, 5, 6, 0, M(8), M(2) | M(9));

    /* member 2 leaves, member 31 takes its place */
    es_divtok_member_left(&t, 2, 31);
    CHECK(es_divtok_find(&t, 2) == -1);
    CHECK(es_divtok_find(&t, 3) == -1);
    CHECK(es_divtok_find(&t, 6) == -1);
    CHECK(t.slot[0].token == 1 && t.slot[0].a == M(0) &&
          t.slot[0].b == M(1) && t.slot[0].vertical == 1);
    CHECK(t.slot[3].token == 4 && t.slot[3].a == M(5) &&
          t.slot[3].b == M(2));
    CHECK(t.slot[4].token == 5 && t.slot[4].a == (M(2) | M(6)) &&
          t.slot[4].b == M(7) && t.slot[4].vertical == 0);
    CHECK(used_slots(&t) == 3);

    /* the renamed tokens are found by the windows' new indices */
    s = set(1, M(5), M(2));
    query(&t, &s, 1, &tok, &next);
    CHECK(tok == 4);
    s = set(1, M(5), M(31));
    query(&t, &s, 1, &tok, &next);
    CHECK(tok == 100);   /* a new seam; 2, 3 and 6 never come back */
}

/* k == last: the last member leaves, nothing moves. */
static void test_member_left_the_last_member(void)
{
    ESDivTokens t;

    es_divtok_clear(&t);
    put(&t, 0, 1, 1, M(0), M(4));
    put(&t, 1, 2, 1, M(1), M(2));
    put(&t, 2, 3, 0, M(3), M(0));
    es_divtok_member_left(&t, 4, 4);
    CHECK(es_divtok_find(&t, 1) == -1);
    CHECK(t.slot[1].token == 2 && t.slot[1].a == M(1) &&
          t.slot[1].b == M(2));
    CHECK(t.slot[2].token == 3 && t.slot[2].a == M(3) &&
          t.slot[2].b == M(0));

    /* the top bit, member 31 */
    es_divtok_clear(&t);
    put(&t, 0, 1, 1, M(30), M(31));
    put(&t, 1, 2, 1, M(29), M(30));
    es_divtok_member_left(&t, 31, 31);
    CHECK(es_divtok_find(&t, 1) == -1);
    CHECK(es_divtok_find(&t, 2) == 1);
    es_divtok_clear(&t);
    put(&t, 0, 1, 0, M(29), M(31));
    es_divtok_member_left(&t, 30, 31);
    CHECK(t.slot[0].token == 1 && t.slot[0].a == M(29) &&
          t.slot[0].b == M(30));

    /* the only member leaves: no token can stay */
    es_divtok_clear(&t);
    put(&t, 7, 1, 0, M(0), M(1));
    put(&t, 8, 2, 0, M(2), M(3));
    es_divtok_member_left(&t, 0, 0);
    CHECK(used_slots(&t) == 0);
}

/* The table's first and last slots are looked at like the others, and
 * a token naming a member beyond the new count (none should) retires. */
static void test_member_left_whole_table(void)
{
    ESDivTokens t;
    int j;

    es_divtok_clear(&t);
    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        put(&t, j, (unsigned long)(j + 1), j & 1, M(0), M(1));
    }
    put(&t, 0, 1, 1, M(3), M(7));          /* names k       */
    put(&t, 62, 63, 1, M(1), M(9));        /* names last    */
    put(&t, 63, 64, 0, M(7), M(2) | M(3)); /* names k       */
    put(&t, 31, 32, 1, M(12), M(0));       /* beyond last   */
    es_divtok_member_left(&t, 3, 9);
    CHECK(t.slot[0].token == 0);
    CHECK(t.slot[63].token == 0);
    CHECK(t.slot[31].token == 0);
    CHECK(t.slot[62].token == 63 && t.slot[62].a == M(1) &&
          t.slot[62].b == M(3));
    for (j = 1; j < 62; j++) {
        if (j != 31) {
            CHECK(t.slot[j].token == (unsigned long)(j + 1) &&
                  t.slot[j].a == M(0) && t.slot[j].b == M(1));
        }
    }
}

static void test_member_left_out_of_range(void)
{
    ESDivTokens t, before;

    es_divtok_clear(&t);
    put(&t, 0, 1, 1, M(0), M(1));
    put(&t, 1, 2, 1, M(2), M(31));
    before = t;
    es_divtok_member_left(&t, -1, 5);
    es_divtok_member_left(&t, 0, ES_DIVTOK_MEMBERS);
    es_divtok_member_left(&t, 3, 2);
    es_divtok_member_left(&t, ES_DIVTOK_MEMBERS, ES_DIVTOK_MEMBERS);
    CHECK(same_table(&t, &before));
}

static void seam_at(ESSeam *s, int vertical, int pos, int from)
{
    s->valid = 1;
    s->vertical = vertical;
    s->pos = pos;
    s->a.n = 0;
    s->b.n = 0;
    if (vertical) {
        s->rect.x = pos - 4;
        s->rect.w = 8;
        s->rect.y = from;
        s->rect.h = 100;
    } else {
        s->rect.y = pos - 4;
        s->rect.h = 8;
        s->rect.x = from;
        s->rect.w = 100;
    }
}

static void test_divider_order(void)
{
    ESSeam *s = g_pool;
    unsigned char rank[8];
    int i;

    seam_at(&s[0], 0, 100, 0);     /* horizontal at 100        */
    seam_at(&s[1], 1, 500, 0);     /* vertical at 500          */
    seam_at(&s[2], 1, 200, 600);   /* vertical at 200, from 600 */
    seam_at(&s[3], 0, 50, 0);      /* horizontal at 50         */
    seam_at(&s[4], 1, 200, 0);     /* vertical at 200, from 0  */
    seam_at(&s[5], 1, 200, 0);     /* the same again           */
    seam_at(&s[6], 0, 100, 0);     /* the same as 0            */
    seam_at(&s[7], 2, 200, 0);     /* vertical too             */
    es_divider_order(s, 8, rank);
    CHECK(rank[0] == 4 && rank[1] == 5 && rank[2] == 7);
    CHECK(rank[3] == 2 && rank[4] == 1);
    CHECK(rank[5] == 3 && rank[6] == 0 && rank[7] == 6);

    /* a horizontal seam's strip starts at rect.x */
    seam_at(&s[0], 0, 300, 900);
    seam_at(&s[1], 0, 300, -20);
    es_divider_order(s, 2, rank);
    CHECK(rank[0] == 1 && rank[1] == 0);

    /* nothing written for no seams; one seam is itself */
    for (i = 0; i < 8; i++) {
        rank[i] = 0xEE;
    }
    es_divider_order(s, 0, rank);
    es_divider_order(s, -3, rank);
    CHECK(rank[0] == 0xEE);
    es_divider_order(s, 1, rank);
    CHECK(rank[0] == 0 && rank[1] == 0xEE);
}

/* Is seam x before seam y in the order, by an independent reading of
 * the keys? */
static int key_cmp(const ESSeam *x, const ESSeam *y)
{
    int xa = x->vertical ? 0 : 1, ya = y->vertical ? 0 : 1;
    int xf = x->vertical ? x->rect.y : x->rect.x;
    int yf = y->vertical ? y->rect.y : y->rect.x;

    if (xa != ya) {
        return xa < ya ? -1 : 1;
    }
    if (x->pos != y->pos) {
        return x->pos < y->pos ? -1 : 1;
    }
    if (xf != yf) {
        return xf < yf ? -1 : 1;
    }
    return 0;
}

static unsigned long g_seed = 12345;

static int rnd(int n)
{
    g_seed = (g_seed * 1103515245UL + 12345UL) & 0xFFFFFFFFUL;
    return (int)((g_seed >> 16) % (unsigned long)n);
}

/* Many lists of up to 64 seams with frequent ties: the rank is a
 * permutation, sorted, and stable. */
static void test_divider_order_random(void)
{
    ESSeam *s = g_pool;
    unsigned char rank[64], seen[64];
    int round, n, i, ok;

    for (round = 0; round < 500; round++) {
        n = 1 + rnd(64);
        for (i = 0; i < n; i++) {
            seam_at(&s[i], rnd(2), rnd(4) * 100 - 100, rnd(3) * 50 - 50);
        }
        es_divider_order(s, n, rank);
        ok = 1;
        for (i = 0; i < n; i++) {
            seen[i] = 0;
        }
        for (i = 0; i < n; i++) {
            if (rank[i] >= n || seen[rank[i]]) {
                ok = 0;
                break;
            }
            seen[rank[i]] = 1;
        }
        for (i = 0; ok && i + 1 < n; i++) {
            int c = key_cmp(&s[rank[i]], &s[rank[i + 1]]);

            if (c > 0 || (c == 0 && rank[i] > rank[i + 1])) {
                ok = 0;
            }
        }
        CHECK(ok);
        if (!ok) {
            break;
        }
    }
}

/* rank holds unsigned chars: past 256 seams nothing more is written. */
static void test_divider_order_cut_at_256(void)
{
    static unsigned char rank[POOL];
    int i, ok = 1;

    for (i = 0; i < POOL; i++) {
        seam_at(&g_pool[i], 1, 10, 0);
        rank[i] = 0x5A;
    }
    es_divider_order(g_pool, POOL, rank);
    for (i = 0; i < 256; i++) {
        if (rank[i] != (unsigned char)i) {
            ok = 0;
        }
    }
    for (i = 256; i < POOL; i++) {
        if (rank[i] != 0x5A) {
            ok = 0;
        }
    }
    CHECK(ok);
}

static void put_box(ESSeamSide *side, int i, int x, int y)
{
    side->box[i].x = x;
    side->box[i].y = y;
    side->box[i].w = 100;
    side->box[i].h = 100;
    side->ref[i] = 0;
}

static void test_side_order(void)
{
    ESSeamSide side;
    unsigned char rank[ES_SEAM_SIDE_MAX + 8];
    int i, ok;

    /* along a vertical seam: top-most first, whatever x says */
    side.n = 3;
    put_box(&side, 0, 0, 500);
    put_box(&side, 1, 50, 0);
    put_box(&side, 2, -10, 250);
    es_divider_side_order(&side, 1, rank);
    CHECK(rank[0] == 1 && rank[1] == 2 && rank[2] == 0);
    /* along a horizontal one: left-most first */
    side.n = 2;
    put_box(&side, 0, 300, 0);
    put_box(&side, 1, 0, 900);
    es_divider_side_order(&side, 0, rank);
    CHECK(rank[0] == 1 && rank[1] == 0);
    /* windows starting at one place keep the side's order */
    side.n = 4;
    put_box(&side, 0, 0, 200);
    put_box(&side, 1, 0, 100);
    put_box(&side, 2, 0, 200);
    put_box(&side, 3, 0, 100);
    es_divider_side_order(&side, 1, rank);
    CHECK(rank[0] == 1 && rank[1] == 3 && rank[2] == 0 && rank[3] == 2);
    es_divider_side_order(&side, 0, rank);
    CHECK(rank[0] == 0 && rank[1] == 1 && rank[2] == 2 && rank[3] == 3);

    /* a full side, back to front */
    side.n = ES_SEAM_SIDE_MAX;
    for (i = 0; i < ES_SEAM_SIDE_MAX; i++) {
        put_box(&side, i, 0, 1000 - 30 * i);
    }
    es_divider_side_order(&side, 1, rank);
    ok = 1;
    for (i = 0; i < ES_SEAM_SIDE_MAX; i++) {
        if (rank[i] != ES_SEAM_SIDE_MAX - 1 - i) {
            ok = 0;
        }
    }
    CHECK(ok);

    /* n out of range: never reads or writes past the side */
    for (i = 0; i < ES_SEAM_SIDE_MAX + 8; i++) {
        rank[i] = 0xEE;
    }
    side.n = ES_SEAM_SIDE_MAX + 8;
    es_divider_side_order(&side, 1, rank);
    CHECK(rank[0] == ES_SEAM_SIDE_MAX - 1 && rank[ES_SEAM_SIDE_MAX] == 0xEE &&
          rank[ES_SEAM_SIDE_MAX + 7] == 0xEE);
    rank[0] = 0xEE;
    side.n = -1;
    es_divider_side_order(&side, 1, rank);
    side.n = 0;
    es_divider_side_order(&side, 0, rank);
    CHECK(rank[0] == 0xEE);
}

static void test_follows(void)
{
    /* a drag names the line it last asked for, however far it went */
    CHECK(es_divider_follows(1, 960, 1400, 1, 1400));
    /* a press names the line it was told about, within the tolerance */
    CHECK(es_divider_follows(1, 960, 1400, 1, 960));
    CHECK(es_divider_follows(1, 960, 1400, 1, 960 + ES_SEAM_TOUCH_TOL));
    CHECK(es_divider_follows(1, 960, 1400, 1, 960 - ES_SEAM_TOUCH_TOL));
    CHECK(!es_divider_follows(1, 960, 1400, 1, 960 + ES_SEAM_TOUCH_TOL + 1));
    CHECK(!es_divider_follows(1, 960, 1400, 1, 960 - ES_SEAM_TOUCH_TOL - 1));
    CHECK(!es_divider_follows(1, 960, 1400, 1, 1401));
    /* the other axis never follows */
    CHECK(!es_divider_follows(1, 960, 1400, 0, 1400));
    CHECK(!es_divider_follows(0, 960, 960, 1, 960));
    CHECK(es_divider_follows(0, 540, 540, 0, 545));
    CHECK(es_divider_follows(2, 540, 540, 1, 545));
    /* positions from a client may be anything an int holds */
    CHECK(!es_divider_follows(1, INT_MIN, 0, 1, INT_MAX));
    CHECK(!es_divider_follows(1, INT_MAX, 0, 1, INT_MIN));
    CHECK(es_divider_follows(1, INT_MAX, 0, 1, INT_MAX - ES_SEAM_TOUCH_TOL));
    CHECK(es_divider_follows(1, INT_MIN, 0, 1, INT_MIN + ES_SEAM_TOUCH_TOL));
    CHECK(es_divider_follows(1, 0, INT_MIN, 1, INT_MIN));
    CHECK(!es_divider_follows(1, -5, 0, 1, INT_MAX));
}

/* Feed a walk for a point with fa and fb facing windows on each side;
 * returns w->visible, checking it decided when expected. */
static int walk(int fa, int fb, const int *kind, const int *holds, int n,
                int decided_at)
{
    ESCoverWalk w;
    int i, at = -1;

    es_cover_begin(&w, fa, fb);
    for (i = 0; i < n; i++) {
        if (es_cover_step(&w, kind[i], holds[i])) {
            at = i;
            break;
        }
    }
    CHECK(at == decided_at);
    CHECK(w.done == (decided_at >= 0));
    return w.visible;
}

static void test_cover_walk(void)
{
    /* front to back B1 (seam, not facing), F (foreign, over the
     * gap), A (facing), B2 (facing), backdrop: F is in front of both
     * windows beside the point, so the seam is covered */
    {
        int k[5] = { ES_LAYER_SEAM, ES_LAYER_FOREIGN, ES_LAYER_FACING,
                     ES_LAYER_FACING, ES_LAYER_BACKDROP };
        int h[5] = { 0, 1, 0, 0, 1 };

        CHECK(walk(1, 1, k, h, 5, 1) == 0);
    }
    /* B (facing) in front, F over the gap, A (facing): F is in front
     * of A, covered */
    {
        int k[4] = { ES_LAYER_FACING, ES_LAYER_FOREIGN, ES_LAYER_FACING,
                     ES_LAYER_BACKDROP };
        int h[4] = { 0, 1, 0, 1 };

        CHECK(walk(1, 1, k, h, 4, 1) == 0);
    }
    /* F behind both facing windows, then the backdrop: seen */
    {
        int k[4] = { ES_LAYER_FACING, ES_LAYER_FACING, ES_LAYER_FOREIGN,
                     ES_LAYER_BACKDROP };
        int h[4] = { 0, 0, 1, 1 };

        CHECK(walk(1, 1, k, h, 4, 3) == 1);
    }
    /* no facing window: the first window holding the point decides, a
     * foreign one covers (the rule before 2.24) */
    {
        int k[3] = { ES_LAYER_SEAM, ES_LAYER_FOREIGN, ES_LAYER_BACKDROP };
        int h[3] = { 0, 1, 1 };

        CHECK(walk(0, 0, k, h, 3, 1) == 0);
        CHECK(walk(-4, -4, k, h, 3, 1) == 0);
    }
    /* a window of the seam holding the point: seen, whatever is behind */
    {
        int k[3] = { ES_LAYER_FOREIGN, ES_LAYER_SEAM, ES_LAYER_FOREIGN };
        int h[3] = { 0, 1, 1 };

        CHECK(walk(0, 0, k, h, 3, 1) == 1);
        CHECK(walk(1, 1, k, h, 3, 1) == 1);
    }
    /* no gap: the point is inside a facing window; in front of it a
     * foreign window covers, behind it nothing matters */
    {
        int k[3] = { ES_LAYER_FOREIGN, ES_LAYER_FACING, ES_LAYER_BACKDROP };
        int h[3] = { 1, 1, 1 };
        int k2[3] = { ES_LAYER_FACING, ES_LAYER_FACING, ES_LAYER_FOREIGN };
        int h2[3] = { 0, 1, 1 };

        CHECK(walk(1, 1, k, h, 3, 0) == 0);
        CHECK(walk(1, 1, k2, h2, 3, 1) == 1);
    }
    /* layers of no kind are passed over, even holding the point, and so
     * is anything not holding it */
    {
        int k[6] = { ES_LAYER_SKIP, 99, -1, ES_LAYER_FOREIGN,
                     ES_LAYER_BACKDROP, ES_LAYER_FOREIGN };
        int h[6] = { 1, 1, 1, 0, 0, 1 };

        CHECK(walk(1, 1, k, h, 6, 5) == 0);
    }
    /* the backdrop first: seen */
    {
        int k[2] = { ES_LAYER_BACKDROP, ES_LAYER_FOREIGN };
        int h[2] = { 1, 1 };

        CHECK(walk(1, 1, k, h, 2, 0) == 1);
    }
    /* the walk runs out undecided: seen */
    {
        int k[3] = { ES_LAYER_FACING, ES_LAYER_FOREIGN, ES_LAYER_FACING };
        int h[3] = { 0, 0, 0 };

        CHECK(walk(1, 1, k, h, 3, -1) == 1);
        CHECK(walk(0, 0, k, h, 0, -1) == 1);
    }
    /* a facing window fed twice cannot make the walk believe it met
     * one it has not: one of three seen twice, F still covers; and
     * more feeds than facing windows leave none unseen, no fewer */
    {
        int k[3] = { ES_LAYER_FACING, ES_LAYER_FACING, ES_LAYER_FOREIGN };
        int h[3] = { 0, 0, 1 };
        ESCoverWalk w;

        CHECK(walk(2, 1, k, h, 3, 2) == 0);
        es_cover_begin(&w, 1, 1);
        es_cover_step(&w, ES_LAYER_FACING, 0);
        es_cover_step(&w, ES_LAYER_FACING, 0);
        es_cover_step(&w, ES_LAYER_FACING, 0);
        CHECK(w.unseen == 0 && !w.done);
    }
    /* a hole in the strip: facing windows of one side only, so the
     * point is not in a gap and a foreign window behind them that holds
     * it shows there and covers it, as before 2.24 (a snapped window
     * across the whole top, two below with room between them, and a
     * window behind them all in that room) */
    {
        int k[3] = { ES_LAYER_FACING, ES_LAYER_FOREIGN, ES_LAYER_BACKDROP };
        int h[3] = { 0, 1, 1 };
        int k2[4] = { ES_LAYER_FACING, ES_LAYER_FACING, ES_LAYER_FOREIGN,
                      ES_LAYER_BACKDROP };
        int h2[4] = { 0, 0, 1, 1 };

        CHECK(walk(1, 0, k, h, 3, 1) == 0);
        CHECK(walk(0, 1, k, h, 3, 1) == 0);
        CHECK(walk(1, -1, k, h, 3, 1) == 0);
        CHECK(walk(2, 0, k2, h2, 4, 2) == 0);
        /* the same layers with a window of each side beside the point:
         * a gap, and the window behind both is passed over */
        CHECK(walk(1, 1, k2, h2, 4, 3) == 1);
    }
    /* in a hole a window of the seam holding the point still decides */
    {
        int k[3] = { ES_LAYER_FOREIGN, ES_LAYER_FACING, ES_LAYER_FOREIGN };
        int h[3] = { 0, 1, 1 };

        CHECK(walk(1, 0, k, h, 3, 1) == 1);
    }
    /* how many count: both sides, each at most a full side; none
     * unless both sides have one */
    {
        ESCoverWalk w;

        es_cover_begin(&w, 2, 3);
        CHECK(w.facing == 5 && w.unseen == 5 && !w.done && w.visible);
        es_cover_begin(&w, 3, 0);
        CHECK(w.facing == 0 && w.unseen == 0);
        es_cover_begin(&w, 0, 3);
        CHECK(w.facing == 0 && w.unseen == 0);
        es_cover_begin(&w, ES_SEAM_SIDE_MAX + 7, 1000000);
        CHECK(w.facing == 2 * ES_SEAM_SIDE_MAX);
        es_cover_begin(&w, INT_MAX, INT_MAX);
        CHECK(w.facing == 2 * ES_SEAM_SIDE_MAX);
    }
    /* once decided, nothing changes it */
    {
        ESCoverWalk w;

        es_cover_begin(&w, 1, 1);
        CHECK(es_cover_step(&w, ES_LAYER_FOREIGN, 1) == 1);
        CHECK(w.visible == 0);
        CHECK(es_cover_step(&w, ES_LAYER_SEAM, 1) == 1);
        CHECK(es_cover_step(&w, ES_LAYER_BACKDROP, 1) == 1);
        CHECK(w.visible == 0 && w.done == 1);
    }
}

int main(void)
{
    test_clear_and_find_on_an_empty_table();
    test_mint_counts_up_and_reuse_gives_the_same();
    test_axis_change_mints_a_new_token();
    test_new_seam_evicts_the_oldest_unclaimed();
    test_sixty_five_seams_on_a_full_table();
    test_sixty_five_new_seams();
    test_eviction_order();
    test_retire();
    test_counter();
    test_sets_no_token_names();
    test_equal_sets_share_a_token();
    test_member_left();
    test_member_left_the_last_member();
    test_member_left_whole_table();
    test_member_left_out_of_range();
    test_divider_order();
    test_divider_order_random();
    test_divider_order_cut_at_256();
    test_side_order();
    test_follows();
    test_cover_walk();

    if (g_failures == 0) {
        printf("divtokens_test: all tests passed\n");
        return 0;
    }
    printf("divtokens_test: %d failure(s)\n", g_failures);
    return 1;
}
