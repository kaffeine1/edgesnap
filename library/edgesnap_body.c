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
#include <exec/execbase.h>     /* the task lists, for clients that went away */
#include <devices/inputevent.h>
#include <intuition/intuition.h>
#include <graphics/layers.h>
#include <intuition/intuitionbase.h>
#include <intuition/screens.h>      /* PubScreenNode: a screen's public name */

#include <proto/exec.h>
#include <proto/intuition.h>
#include <proto/dos.h>       /* Delay: the tick between two boxes of a glide */

#include "edgesnap.h"
#include "edgesnap_body.h"
#include "panels.h"
#include "gutter.h"
#include "divtokens.h"
#include "zones.h"

/* OS4 declares IntuitionBase as struct Library *; classic/MorphOS as
 * struct IntuitionBase *. Same alias trick as the frontend. */
#define ESB_IBASE ((struct IntuitionBase *)IntuitionBase)

#define ESB_EXCLUDE_SLOTS 16

static struct SignalSemaphore g_sem;
static ESConfig g_cfg;
static ESEngine g_engine;
static LONG g_push_x, g_push_y;   /* raw pointer travel since the last feed */
static ESRegistry g_registry;
static void *g_excluded[ESB_EXCLUDE_SLOTS];
static struct Window *g_ignored[ESB_IGNORE_SLOTS];
static int g_enabled;
static int g_ready;

/*
 * Clients and their roles (2.10). A registration belongs to the task
 * that made it; ids are never reused, so an id kept by a client that
 * is gone can only be stale, never somebody else's.
 */
#define ESB_CLIENT_SLOTS 8
#define ESB_CLIENT_NAME  32
struct ESBClient {
    ULONG id;                  /* 0 = free */
    struct Task *task;
    ULONG roles;               /* ES_CL_* */
    int layout_on;             /* the LAYOUT role's own switch */
    char name[ESB_CLIENT_NAME];
};
static struct ESBClient g_clients[ESB_CLIENT_SLOTS];
static ULONG g_client_next = 1;

/* groups (2.12) live further down, next to the identities they use */
struct ESBGroup;
static int esb_locked_for(struct Window *win);
static int esb_grouped(struct Window *win);
static int esb_group_locked_for(const struct ESBGroup *g);
static void esb_groups_drop_owner(ULONG client);
static void esb_groups_clear(void);
static int esb_group_cell(struct Window *win, const ESRect *cell,
                          const ESRect *usable, ESRect *box, ESRect *area);
static void esb_member_placed(struct Window *win, const ESRect *live,
                              const ESRect *box);
static ULONG g_gen_touch;      /* drags that reached for a locked window */

/* the dividers (2.24) list their windows and move them further down too */
static int esb_serial_known(ULONG serial);
static int esb_seam_list(struct ESBGroup *group);
static LONG esb_seam_move(ESSeam *seam, LONG position, int may_raise,
                          LONG *applied);
static void esb_follow_clear(void);

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
    {
        int i;

        for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
            g_clients[i].id = 0;
        }
        esb_groups_clear();
        esb_follow_clear();
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
    {
        int i;

        for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
            g_clients[i].id = 0;
        }
    }
    esb_groups_clear();
    esb_follow_clear();
    ReleaseSemaphore(&g_sem);
    g_ready = 0;
}

/* -------------------------------------------------------- clients (2.10) */

/*
 * Is this task still around? The current task is; any other one is on
 * exec's ready or waiting list, and a pointer on neither list belongs
 * to a task that ended. Under Disable, because those lists move under
 * the scheduler's hands. Not for hot paths: the input feed compares
 * pointers and leaves the walk to the calls that register.
 */
static int esb_task_alive(struct Task *t)
{
    /* AmigaOS 4 reaches exec through its interface and the library
     * has no SysBase symbol of its own: the base is behind IExec */
#ifdef __amigaos4__
    struct ExecBase *eb = (struct ExecBase *)IExec->Data.LibBase;
#else
    struct ExecBase *eb = (struct ExecBase *)SysBase;
#endif
    struct Node *n;
    int found = 0;

    if (t == NULL) {
        return 0;
    }
    if (t == FindTask(NULL)) {
        return 1;
    }
    Disable();
    for (n = eb->TaskReady.lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
        if ((struct Task *)n == t) {
            found = 1;
            break;
        }
    }
    for (n = eb->TaskWait.lh_Head; !found && n->ln_Succ != NULL;
         n = n->ln_Succ) {
        if ((struct Task *)n == t) {
            found = 1;
        }
    }
    Enable();
    return found;
}

/* g_sem held. Drop the registrations of tasks that are gone. */
static void esb_clients_sweep(void)
{
    int i;

    for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
        if (g_clients[i].id != 0 && !esb_task_alive(g_clients[i].task)) {
            esb_groups_drop_owner(g_clients[i].id);
            g_clients[i].id = 0;
        }
    }
}

/* g_sem held. The registration behind an id, or NULL. */
static struct ESBClient *esb_client_by_id(ULONG id)
{
    int i;

    for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
        if (g_clients[i].id != 0 && g_clients[i].id == id) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/* g_sem held. The registration of a task, or NULL. */
static struct ESBClient *esb_client_of(struct Task *t)
{
    int i;

    for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
        if (g_clients[i].id != 0 && g_clients[i].task == t) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/* g_sem held. Whoever holds the engine role, or NULL. */
static struct ESBClient *esb_engine_owner(void)
{
    int i;

    for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
        if (g_clients[i].id != 0 && (g_clients[i].roles & ES_CL_ENGINE)) {
            return &g_clients[i];
        }
    }
    return NULL;
}

/*
 * g_sem held. May this task feed the engine? Yes when nobody owns it,
 * which is every library before 2.10 and every frontend that never
 * registered; otherwise only the owner's task. A pointer comparison,
 * no walk: this runs on every mouse event.
 */
static int esb_engine_input_ok(void)
{
    struct ESBClient *owner = esb_engine_owner();

    return owner == NULL || owner->task == FindTask(NULL);
}

LONG esb_register_client(const char *name, ULONG wants, ULONG *client_out)
{
    struct Task *me = FindTask(NULL);
    struct ESBClient *c, *owner;
    LONG rc = ES_OK;
    int i;

    if (client_out == NULL || wants == 0 ||
        (wants & ~(ES_CL_ENGINE | ES_CL_LAYOUT)) != 0) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    esb_clients_sweep();
    owner = esb_engine_owner();
    c = esb_client_of(me);
    if ((wants & ES_CL_ENGINE) && owner != NULL && owner != c) {
        rc = ES_ERR_IN_USE;
    } else {
        if (c == NULL) {
            for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
                if (g_clients[i].id == 0) {
                    c = &g_clients[i];
                    break;
                }
            }
        }
        if (c == NULL) {
            rc = ES_ERR_NO_MEMORY;
        } else {
            if (c->id == 0) {
                c->id = g_client_next++;
                if (g_client_next == 0) {
                    g_client_next = 1;     /* never 0 */
                }
                c->task = me;
                c->layout_on = 1;
            }
            c->roles = wants;
            for (i = 0; i < ESB_CLIENT_NAME - 1 && name != NULL && name[i] != '\0'; i++) {
                c->name[i] = name[i];
            }
            c->name[i] = '\0';
            *client_out = c->id;
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_unregister_client(ULONG client)
{
    LONG rc = ES_ERR_STALE;
    int i;

    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    esb_clients_sweep();
    for (i = 0; i < ESB_CLIENT_SLOTS; i++) {
        if (g_clients[i].id != 0 && g_clients[i].id == client) {
            esb_groups_drop_owner(client);
            g_clients[i].id = 0;
            rc = ES_OK;
            break;
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
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
        /* Panels do not move or size. Borderless is deliberately not
         * required: real docks do not guarantee it. Backdrop is not
         * refused either: Ambient can keep a panel behind the windows
         * (field case, 2026-10-08), and the desktop windows that are
         * backdrop cover the whole screen, which the geometry refuses. */
        if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET)) != 0) {
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

/* The rule on a window's own flags, for callers that hold no sample. */
static int esb_flags_snappable(ULONG flags)
{
    if ((flags & WFLG_SIZEGADGET) == 0) {
        return 0;
    }
    if (flags & (WFLG_BACKDROP | WFLG_BORDERLESS)) {
        return 0;
    }
    return 1;
}

static int esb_snappable(const struct ESBSnap *s)
{
    return esb_flags_snappable(s->flags);
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
#ifdef __AROS__
/*
 * Is this one of Wanderer's windows? Its IDCMP port belongs to the
 * Wanderer task. The repaint wait below is for Wanderer alone: other
 * Zune programs repaint what a layer operation leaves them, and the
 * console never does, so waiting on it would only cost time.
 */
static int esb_is_wanderer(struct Window *win)
{
    static const char tag[] = "Wanderer";
    struct Task *t;
    const char *name;
    int i, j;

    if (win->UserPort == NULL) {
        return 0;
    }
    t = win->UserPort->mp_SigTask;
    if (t == NULL || (name = t->tc_Node.ln_Name) == NULL) {
        return 0;
    }
    /* a library links no C runtime: the search is spelled out */
    for (i = 0; name[i] != '\0'; i++) {
        for (j = 0; tag[j] != '\0' && name[i + j] == tag[j]; j++) {
            ;
        }
        if (tag[j] == '\0') {
            return 1;
        }
    }
    return 0;
}

/*
 * Wait until the window has repainted what a layer operation left it
 * to repaint: layers raise LAYERREFRESH when damage arrives, and the
 * window's owner clears it with EndRefresh(), which on AROS comes
 * after the drawing. Capped, generously: a drawer full of icons takes
 * its time under emulation.
 */
static void esb_wait_repaint(struct Window *win, int cap_ticks)
{
    int i, quiet = 0;

    for (i = 0; i < cap_ticks; i++) {
        if (win->WLayer == NULL) {
            return;
        }
        if ((win->WLayer->Flags & LAYERREFRESH) != 0) {
            quiet = 0;
        } else if (++quiet >= 3) {
            return;
        }
        Delay(1);
    }
}

/*
 * ChangeWindowBox() is asynchronous on AROS. Do not manufacture fresh
 * damage until Intuition has installed the final box and left it stable
 * long enough for Wanderer to consume IDCMP_NEWSIZE.
 */
static int esb_wait_box(struct Window *win, const ESRect *box,
                        int cap_ticks)
{
    int i, steady = 0;

    for (i = 0; i < cap_ticks; i++) {
        if (win->LeftEdge == box->x && win->TopEdge == box->y &&
            win->Width == box->w && win->Height == box->h) {
            if (++steady >= 3) {
                return 1;
            }
        } else {
            steady = 0;
        }
        Delay(1);
    }
    return 0;
}

/*
 * Wanderer's resize redraw can leave earlier layer damage black. Once
 * the size is steady, briefly cover the visible part of the drawer with
 * an unpainted SIMPLE_REFRESH layer. Closing it creates new, resize-free
 * damage, which sends Zune through its full redraw path.
 */
static void esb_refresh_wanderer(struct Window *win, const ESRect *box)
{
    struct Screen *scr = win->WScreen;
    struct Window *cover;
    int left, top, right, bottom;

    if (scr == NULL) {
        return;
    }
    left = box->x < 0 ? 0 : box->x;
    top = box->y < 0 ? 0 : box->y;
    right = box->x + box->w;
    bottom = box->y + box->h;
    if (right > (int)scr->Width) {
        right = scr->Width;
    }
    if (bottom > (int)scr->Height) {
        bottom = scr->Height;
    }
    if (right <= left || bottom <= top) {
        return;
    }

    cover = OpenWindowTags(NULL,
                           WA_CustomScreen, scr,
                           WA_Left, left,
                           WA_Top, top,
                           WA_Width, right - left,
                           WA_Height, bottom - top,
                           WA_Flags, WFLG_BORDERLESS |
                                     WFLG_SIMPLE_REFRESH |
                                     WFLG_NOCAREREFRESH,
                           WA_Activate, FALSE,
                           WA_BackFill, LAYERS_NOBACKFILL,
                           TAG_DONE);
    if (cover != NULL) {
        CloseWindow(cover);
        esb_wait_repaint(win, 150);
    }
}
#endif

/*
 * The glide (2.13): the same trip, in a few boxes. Only a snap and a
 * restore take it, never a client's placement, and only when the user
 * asked for it: every step asks the application to redraw itself, and
 * on real hardware with a large window that can cost more than the
 * jump it replaces. es_glide_steps() decides whether the trip is worth
 * showing at all.
 *
 * A covered window is raised first, so it glides where it can be seen.
 * The last box on the way becomes the starting point handed to
 * esb_change_box(): on AROS that function reasons about where the
 * window IS, and after a glide that is no longer where it was.
 *
 * Like the repaint waits below, this runs with the library's semaphore
 * held: the whole glide is a fifth of a second at most, and the task
 * doing it is the one that asked for the snap.
 */
static void esb_change_box(struct Window *win, const ESRect *from,
                           const ESRect *to, int may_raise);

/*
 * One box of a glide. ChangeWindowBox() only asks: Intuition installs
 * the box later, and a request that arrives while the previous one is
 * still pending may be folded into it, which turns a glide into a jump.
 * That is what the first real MorphOS machine showed: a window that
 * shrank glided, a window that grew, and so had to redraw what it
 * gained at every box, jumped. So every box is shown for a tick and,
 * if Intuition has not taken it yet, waited for a little longer. The
 * test is "the box changed", not "the box is what I asked for": a
 * console rounds its size to its cells and would never match.
 *
 * `patience` is how many ticks a box may take. Three is plenty, but
 * on real MorphOS hardware the first snap after the system started
 * never glided while every later one did, new windows included
 * (2026-09-25): the first programmatic resize after boot costs MorphOS
 * more than three ticks, the next box arrived while it was pending,
 * and the two were folded. So the first box of every glide may take
 * ten. The loop stops as soon as the box has changed, so a longer
 * patience slows nothing that is quick.
 */
static void esb_glide_step(struct Window *win, const ESRect *was,
                           const ESRect *box, int patience)
{
    int i;

    ChangeWindowBox(win, box->x, box->y, box->w, box->h);
    for (i = 0; i < patience; i++) {
        Delay(1);
        if (win->LeftEdge != was->x || win->TopEdge != was->y ||
            win->Width != was->w || win->Height != was->h) {
            return;
        }
    }
}

static void esb_glide_box(struct Window *win, const ESRect *from,
                          const ESRect *to, int may_raise)
{
    ESRect at = *from;
    int steps = g_cfg.animate ? es_glide_steps(from, to) : 0;
    int i;

    if (steps > 0 && may_raise && win->WLayer != NULL &&
        win->WLayer->front != NULL) {
        WindowToFront(win);
        Delay(2);
    }
    for (i = 1; i < steps; i++) {
        ESRect r;

        es_glide_rect(from, to, i, steps, &r);
        esb_glide_step(win, &at, &r, i == 1 ? 10 : 3);
        /* where it is, not where it was asked to be: the next box and
         * the AROS repaint dance both reason from here */
        at.x = win->LeftEdge;
        at.y = win->TopEdge;
        at.w = win->Width;
        at.h = win->Height;
    }
    esb_change_box(win, &at, to, may_raise);
}

static void esb_change_box(struct Window *win, const ESRect *from,
                           const ESRect *to, int may_raise)
{
#ifdef __AROS__
    int moved = (from->x != to->x || from->y != to->y);
    int sized = (from->w != to->w || from->h != to->h);
    int covered = win->WLayer != NULL && win->WLayer->front != NULL;
    int wanderer = esb_is_wanderer(win);
    struct Screen *scr = win->WScreen;
    int fits_here = scr != NULL && from->x >= 0 && from->y >= 0 &&
                    from->x + to->w <= (int)scr->Width &&
                    from->y + to->h <= (int)scr->Height;

    /*
     * Wanderer, in three sentences. When a window's size changes, Zune
     * discards the damage the window carries without drawing it and
     * asks for a full redraw, which Wanderer's icon list narrows to the
     * areas gained: damage present at that moment stays pen 0. When
     * damage arrives alone, at a steady size, Zune repaints all of it.
     * Damage is what a raise or a move uncovers. So the size may only
     * change once the damage has been repaired: raise first and wait
     * for the repaint, then size where the window stands, then move,
     * which copies. A window that grows beyond the screen where it
     * stands moves first instead, and waits once more before its size
     * changes, since the move may have uncovered what lay beyond the
     * edge.
     */
    if (moved && may_raise && covered) {
        WindowToFront(win);
        Delay(2);                  /* the raise is an action: let it land */
        if (wanderer) {
            esb_wait_repaint(win, 150);
        }
    }
    if (sized && moved) {
        int grows = (long)to->w * to->h > (long)from->w * from->h;

        if (grows && !fits_here) {
            ChangeWindowBox(win, to->x, to->y, from->w, from->h);
            Delay(2);
            if (wanderer) {
                esb_wait_repaint(win, 150);
            }
        } else {
            ChangeWindowBox(win, from->x, from->y, to->w, to->h);
        }
    }
#else
    (void)from;
    (void)may_raise;
#endif
    ChangeWindowBox(win, to->x, to->y, to->w, to->h);
#ifdef __AROS__
    if (wanderer && (moved || sized) && esb_wait_box(win, to, 150)) {
        esb_wait_repaint(win, 150);
        esb_refresh_wanderer(win, to);
    }
#endif
}

/*
 * The raw travel of the pointer since the last FeedInput, from the
 * input handler that sees the mouse itself. Needed where Intuition
 * pins the pointer to a window that may not leave the screen: the
 * pointer then stands still at the edge while the mouse keeps moving,
 * and only this travel says so. Purely additive: a frontend that never
 * calls it gets the engine of 2.6.
 */
void esb_feed_motion(LONG dx, LONG dy)
{
    ObtainSemaphore(&g_sem);
    if (esb_engine_input_ok()) {       /* the engine's owner, or nobody's */
        g_push_x += dx;
        g_push_y += dy;
    }
    ReleaseSemaphore(&g_sem);
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

    if (win == NULL || zone == ES_ZONE_NONE ||
        (zone > ES_ZONE_MAX && zone != ES_ZONE_CENTRE)) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
        rc = ES_ERR_STALE;
    } else if (esb_is_excluded(win) || !esb_snappable(&s) ||
               esb_locked_for(win)) {
        rc = ES_ERR_REJECTED;
    } else {
        int step = 0;

        if (want != NULL) {
            r = *want;
        } else if (zone == ES_ZONE_CENTRE) {
            /*
             * The middle is not an edge. No width cycle, because there
             * is no "the same side again" to cycle with, and no pair
             * fill, because a centred window has no opposite number.
             * It keeps the size it has: the only size the centre implies.
             */
            es_centre_rect(&s.usable, &s.box, s.min_w, s.min_h,
                           s.max_w, s.max_h, &r);
        } else {
            /*
             * The same side asked for twice runs the width cycle:
             * half, two thirds, one third, half again. Only when the
             * window is already on that side and the caller named no
             * rectangle, which is the hotkey path: a drag that lands
             * on the same edge means "put it back", not "narrow it".
             */
            if (g_cfg.cycle_sizes &&
                es_registry_zone(&g_registry, win) == (int)zone) {
                step = es_registry_step(&g_registry, win) + 1;
                if (step >= ES_STEP_COUNT) {
                    step = 0;
                }
            }
            es_fit_zone_rect_step((int)zone, &s.usable, step, s.min_w,
                                  s.min_h, s.max_w, s.max_h, &r);
        }
        /* Fill what the opposite side is not using, like Windows and
         * macOS: after a pair has been re-balanced to 70/30, the next
         * window snapped to the narrow side gets that 30%. A width the
         * user asked for by cycling is not up for negotiation, so the
         * fill is skipped there. */
        if (step == 0 && zone != ES_ZONE_CENTRE) {
            ObtainSemaphore(&g_sem);
            es_pair_fill(&g_registry, win, (int)zone, &s.usable, s.min_w, s.max_w, &r);
            ReleaseSemaphore(&g_sem);
        }
        /* Refuse rather than snap without a way back: a full registry
         * would make ESnap_UnsnapWindow silently impossible. */
        rc = es_registry_remember_step(&g_registry, win, &s.box, &r,
                                       (int)zone, step);
        if (rc == ES_OK) {
            esb_member_placed(win, &s.box, &r);
            esb_glide_box(win, &s.box, &r, 1);
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
            esb_member_placed(win, &s.box, &out);
            esb_glide_box(win, &s.box, &out, 1);
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
        case ES_OPT_CycleSizes:
            cfg.cycle_sizes = v ? 1 : 0;
            break;
        case ES_OPT_Animate:
            cfg.animate = v ? 1 : 0;
            break;
        case ES_OPT_DragRestore:
            cfg.drag_restore = v ? 1 : 0;
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
    struct ESBClient *c;

    ObtainSemaphore(&g_sem);
    esb_clients_sweep();
    c = esb_client_of(FindTask(NULL));
    if (c != NULL && (c->roles & ES_CL_ENGINE) == 0) {
        /* a layout client switches its own layout, never the engine */
        c->layout_on = on ? 1 : 0;
    } else {
        /* the engine's owner, or a caller from before roles existed */
        g_enabled = on ? 1 : 0;
    }
    ReleaseSemaphore(&g_sem);
    return ES_OK;
}

int esb_enabled(void)
{
    return g_enabled;
}

/* ------------------------------------------------------ the divider */

/*
 * 2.24: every divider call works on one list of windows, built under
 * the semaphore and used up before it is released: the windows of the
 * front screen for the calls of 2.2 and 2.3, those of one group for the
 * token calls (esb_seam_list, with the groups). A seam may hold a whole
 * group on one side and a group may have 64 seams, which is far more
 * than the caller's stack can carry (the library runs on it); so the
 * list, its seams and everything a move needs live here, in static
 * memory that only the holder of g_sem touches, and nothing points
 * into it once the semaphore is released.
 */

/* The seams of one list the library looks at: every one a full group
 * can have, since a window is on side a of at most one seam per axis
 * (checked next to ESB_GROUP_MEMBERS). The front screen can offer more;
 * as before 2.24, the ones past the end are not looked at. */
#define ESB_SEAM_RESULTS ES_DIVTOK_SLOTS

/* The strip of a seam that is moved rather than shown. */
#define ESB_SEAM_LINE_PX 8

/* A listed window, as the list saw it. */
struct ESBSeamWin {
    ULONG serial;              /* 0 when the identity table was full   */
    struct Layer *layer;       /* checked again before a move          */
    ESRect live;               /* where it is                          */
    int min_w, min_h;          /* its own size limits, max 0 for none, */
    int max_w, max_h;          /* as esb_fill reads them               */
    int member;                /* its index in its group, or -1        */
    int moved;                 /* not where EdgeSnap put it, nor on    */
                               /* its way there (esb_seam_add)         */
};

static ESSeamBox g_sl_box[ES_SEAM_BOXES_MAX];         /* the list        */
static struct ESBSeamWin g_sl_win[ES_SEAM_BOXES_MAX];
static int g_sl_n;
static ULONG g_sl_now;                                /* its clock (secs) */
static ESSeam g_sl_seam[ESB_SEAM_RESULTS];            /* its seams       */
static ESSeam g_sl_one;                               /* one, by windows */
static void *g_sl_ref[2][ES_SEAM_SIDE_MAX];           /* ...those windows */
static ESRect g_sl_to[2 * ES_SEAM_SIDE_MAX];          /* a move: targets */
static long g_sl_delta[2 * ES_SEAM_SIDE_MAX];         /* growth          */
static int g_sl_order[2 * ES_SEAM_SIDE_MAX];          /* order of moves  */
static struct Layer *g_sl_layer[2 * ES_SEAM_SIDE_MAX]; /* the cover walk */
static unsigned char g_sl_kind[2 * ES_SEAM_SIDE_MAX];

/* g_sem held. Where ref stands in the list, or -1. */
static int esb_sl_index(const void *ref)
{
    int k;

    for (k = 0; k < g_sl_n; k++) {
        if (g_sl_box[k].ref == ref) {
            return k;
        }
    }
    return -1;
}

/* g_sem held, the list built: its seams into g_sl_seam[], and how many
 * there are there. */
static int esb_seams_found(int thickness)
{
    int m = es_seams_find(g_sl_box, g_sl_n, thickness, g_sl_seam,
                          ESB_SEAM_RESULTS);

    return m < ESB_SEAM_RESULTS ? m : ESB_SEAM_RESULTS;
}

/* How far apart two coordinates are, with no overflow: a line comes
 * from a client and may be anything a LONG holds. */
static unsigned long esb_distance(long p, long q)
{
    return p >= q ? (unsigned long)p - (unsigned long)q
                  : (unsigned long)q - (unsigned long)p;
}

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
 * moved, resized or closed since. The list holds only windows that are
 * there (its builder forgets the registry entries of windows that are
 * gone, and a group loses its members that closed) and knows whether
 * each still lies where EdgeSnap put it; a seam with one that does not
 * reports no seam.
 *
 * What this must NOT do is forget them on a mismatch. ChangeWindowBox()
 * places a window asynchronously, so a query made immediately after a
 * snap sees it still at its old size, a mismatch that lasts a few
 * milliseconds. Forgetting on that transient destroyed the pair
 * permanently and the seam then never appeared at all. Only a window
 * that is GONE is forgotten; a geometry mismatch just means "no seam
 * right now". For the same reason a window of a group may also lie
 * where EdgeSnap put it the time before, and for ESB_TRANSIT_SECS after
 * EdgeSnap asked it to move it may lie anywhere at all (esb_seam_add)
 * (2.24).
 */
static int esb_seam_alive(const ESSeam *seam)
{
    const ESSeamSide *side[2];
    int s, i, k;

    side[0] = &seam->a;
    side[1] = &seam->b;
    for (s = 0; s < 2; s++) {
        for (i = 0; i < side[s]->n; i++) {
            k = esb_sl_index(side[s]->ref[i]);
            if (k < 0 || g_sl_win[k].moved) {
                return 0;
            }
        }
    }
    return 1;
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

/*
 * A seam of a group whose limits cross before any window's own limits
 * are even read can never be moved (es_seams_find reports it all the
 * same, for QueryDividers to flag). The calls of 2.2 and 2.3 never
 * offered one before 2.24, so they look past it: it must not stand in
 * for "the first seam", nor be the nearest one and hide a seam beside
 * it that can be dragged.
 */
static int esb_seam_room(const ESSeam *seam)
{
    return seam->min_pos <= seam->max_pos;
}

/*
 * g_sem held, m seams found. "The first seam" of the calls of 2.2: the
 * first one of the windows the user snapped (key 0), as before 2.24,
 * and the first seam of a group with room (esb_seam_room) only when
 * there is none of those, so that a tiler's seams, which come by line
 * among the vertical ones, do not take the place of the user's.
 */
static ESSeam *esb_first_seam(int m)
{
    int i;

    for (i = 0; i < m; i++) {
        if (g_sl_seam[i].key == 0) {
            return &g_sl_seam[i];
        }
    }
    for (i = 0; i < m; i++) {
        if (esb_seam_room(&g_sl_seam[i])) {
            return &g_sl_seam[i];
        }
    }
    return NULL;
}

LONG esb_query_divider(ULONG thickness, struct ESnapDivider *out)
{
    ESSeam *seam;

    if (out == NULL || thickness < 2 || thickness > 64) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    esb_seam_list(NULL);
    seam = esb_first_seam(esb_seams_found((int)thickness));
    if (seam != NULL && esb_seam_alive(seam)) {
        esb_seam_report(seam, out);
    } else {
        esb_no_seam(out);
    }
    ReleaseSemaphore(&g_sem);
    return ES_OK;
}

/*
 * Every window keeps its own floor and ceiling as well: a seam pushed
 * past a MinWidth or a MaxHeight would make Intuition clamp that
 * window to a box other than the one recorded, and a recorded box that
 * does not match the real one reads as "the user moved it", which ends
 * the seam mid-drag with the other side already moved. The limits are
 * the ones the list read live, so a window that changed its minimum
 * since it was snapped is honoured too. A side keeps its far edge, so
 * its size is the distance from that edge to the divider, less its
 * share of the gap a group keeps (2.24).
 */
static void esb_seam_tighten_live(ESSeam *seam)
{
    int s, i, k;

    for (s = 0; s < 2; s++) {
        const ESSeamSide *side = s ? &seam->b : &seam->a;

        for (i = 0; i < side->n; i++) {
            const struct ESBSeamWin *w;

            k = esb_sl_index(side->ref[i]);
            if (k < 0) {
                continue;
            }
            w = &g_sl_win[k];
            es_seam_tighten_side(seam, s, i,
                                 seam->vertical ? w->min_w : w->min_h,
                                 seam->vertical ? w->max_w : w->max_h);
        }
    }
}

/* Alive, and with the range every window on it can actually accept. */
static int esb_seam_limits(ESSeam *seam)
{
    if (!esb_seam_alive(seam)) {
        return 0;
    }
    esb_seam_tighten_live(seam);
    return seam->min_pos <= seam->max_pos;
}

/*
 * Is the seam really at (x, y), a point on its strip, or is another
 * window lying on top of it there? The registry knows where the
 * snapped windows are, not what covers them, and without this the
 * seam lit up over a window laid on top of a snapped pair (first real
 * MorphOS machine, 2026-09-23), where a click would have resized the
 * windows below.
 *
 * The screen's layers are walked front to back, read as the rest of
 * the body reads them, without layers.library, and es_cover_step
 * decides (core/divtokens.h). From 2.16 to 2.23 the first layer that
 * held the point, and belonged to a window, decided: one of the seam's
 * own windows, or a backdrop, since the gap between them can show the
 * desktop, and the seam was there; anything else covered it. That is
 * still the rule for windows of no group, and wherever the point lies
 * beside the windows of one side only (a hole in the strip). A group
 * keeps a gap between its windows (2.24), and the point can lie in it,
 * between windows of both sides: a window over the gap then covers the
 * seam only if it lies in front of one of the seam's windows beside
 * the point (facing it), and is passed over once the walk has met them
 * all. A window of a group's seam faces the point when it reaches to
 * within the group's gap of it along the seam: where a master meets a
 * stack, as at every T-junction of a layout with a gap, the point
 * level with the gap between two windows of the stack has no window of
 * that side beside it, yet that hole is the gap the layout keeps, and
 * a window behind the whole group must not cover the seam there. In a
 * longer hole, a point further than the gap from every window of that
 * side keeps the rule of 2.23. A frontend's own windows (the handle
 * itself is a window on MorphOS) are passed over, as are layers with
 * no window.
 */
/* How near the strip a point counts as on the seam: where a frontend
 * lights it (three pixels), and one more. */
#define ESB_SEAM_TOUCH_PX 4

/* Is w one of the seam's windows? 1 on side a, 2 on side b, else 0. */
static int esb_seam_side_of(const ESSeam *seam, const struct Window *w)
{
    int i;

    for (i = 0; i < seam->a.n; i++) {
        if ((const struct Window *)seam->a.ref[i] == w) {
            return 1;
        }
    }
    for (i = 0; i < seam->b.n; i++) {
        if ((const struct Window *)seam->b.ref[i] == w) {
            return 2;
        }
    }
    return 0;
}

static int esb_seam_visible_at(const ESSeam *seam, int x, int y)
{
    struct Screen *scr;
    struct Window *lw;
    struct Layer *l;
    ESCoverWalk walk;
    ULONG ilock;
    int nl = 0, facing[2], side, i;

    ObtainSemaphore(&g_sem);       /* the ignored list; then IBase, as elsewhere */
    ilock = LockIBase(0);
    facing[0] = 0;
    facing[1] = 0;
    es_cover_begin(&walk, 0, 0);
    /* the screen the list was built on, if it is still open: never one
     * read from a window that may have closed since */
    for (scr = ESB_IBASE->FirstScreen;
         scr != NULL && (const void *)scr != seam->scope;
         scr = scr->NextScreen) {
        ;
    }
    if (scr != NULL) {
        /* the seam's windows here, and which of them face the point:
         * those that reach to within the group's gap of it along the
         * seam (the gap is 0 for windows of no group) */
        for (lw = scr->FirstWindow;
             lw != NULL && nl < 2 * ES_SEAM_SIDE_MAX; lw = lw->NextWindow) {
            if ((l = lw->WLayer) == NULL ||
                (side = esb_seam_side_of(seam, lw)) == 0) {
                continue;
            }
            g_sl_layer[nl] = l;
            if (seam->vertical ? y >= l->bounds.MinY - seam->gap &&
                                 y <= l->bounds.MaxY + seam->gap
                               : x >= l->bounds.MinX - seam->gap &&
                                 x <= l->bounds.MaxX + seam->gap) {
                g_sl_kind[nl] = ES_LAYER_FACING;
                facing[side - 1]++;
            } else {
                g_sl_kind[nl] = ES_LAYER_SEAM;
            }
            nl++;
        }
        /* windows of no group keep no gap: there the first window
         * holding the point decides, as it always did */
        if (seam->key == 0) {
            facing[0] = 0;
            facing[1] = 0;
        }
        es_cover_begin(&walk, facing[0], facing[1]);
        for (l = scr->LayerInfo.top_layer; l != NULL && !walk.done;
             l = l->back) {
            int holds = x >= l->bounds.MinX && x <= l->bounds.MaxX &&
                        y >= l->bounds.MinY && y <= l->bounds.MaxY;
            int kind = -1;

            for (i = 0; i < nl; i++) {
                if (g_sl_layer[i] == l) {
                    kind = g_sl_kind[i];
                    break;
                }
            }
            if (kind < 0) {
                if (!holds) {
                    continue;      /* only the seam's own count unseen */
                }
                /* Whose layer? Not l->Window: AmigaOS 4 leaves it
                 * empty, and the walk then passed over every window
                 * and found the seam uncovered. Every window names its
                 * own layer in WLayer. */
                for (lw = scr->FirstWindow; lw != NULL; lw = lw->NextWindow) {
                    if (lw->WLayer == l) {
                        break;
                    }
                }
                if (lw == NULL || esb_is_ignored(lw)) {
                    kind = ES_LAYER_SKIP;
                } else if ((lw->Flags & WFLG_BACKDROP) != 0) {
                    kind = ES_LAYER_BACKDROP;
                } else {
                    kind = ES_LAYER_FOREIGN;
                }
            }
            (void)es_cover_step(&walk, kind, holds);
        }
    }
    UnlockIBase(ilock);
    ReleaseSemaphore(&g_sem);
    return walk.visible;
}

/*
 * 2.24: the seam a task is dragging. Up to 2.23 a move named its seam
 * by the line alone and took the nearest one on that axis, so a drag
 * whose seam stopped being one carried on with a seam nobody grabbed.
 * QueryDividerAt now remembers, for the task that asked, the windows of
 * the seam it reported; a move by that task on that line (the one it
 * was told about, or the one its last move asked for: see
 * es_divider_follows) moves the seam of those windows, or none. The
 * windows by serial, so that a window opened at a closed one's address
 * is never taken for it. Eight tasks: the oldest record gives way, a
 * record of a task that is gone first.
 */
#define ESB_FOLLOW_SLOTS 8
struct ESBFollow {
    struct Task *task;         /* NULL = free                          */
    ULONG stamp;               /* when stored: the oldest gives way    */
    int vertical;
    LONG pos;                  /* where the seam is                    */
    LONG last_arg;             /* where the task last asked to move it */
    int na, nb;
    ULONG a[ES_SEAM_SIDE_MAX]; /* the windows of each side, by serial  */
    ULONG b[ES_SEAM_SIDE_MAX];
};
static struct ESBFollow g_follow[ESB_FOLLOW_SLOTS];
static ULONG g_follow_clock;

static void esb_follow_clear(void)
{
    int i;

    for (i = 0; i < ESB_FOLLOW_SLOTS; i++) {
        g_follow[i].task = NULL;
    }
}

/* g_sem held. The record of task t, or NULL. */
static struct ESBFollow *esb_follow_find(struct Task *t)
{
    int i;

    for (i = 0; i < ESB_FOLLOW_SLOTS; i++) {
        if (g_follow[i].task != NULL && g_follow[i].task == t) {
            return &g_follow[i];
        }
    }
    return NULL;
}

/* g_sem held. The calling task's seam is no more. */
static void esb_follow_drop(void)
{
    struct ESBFollow *f = esb_follow_find(FindTask(NULL));

    if (f != NULL) {
        f->task = NULL;
    }
}

/* g_sem held, the list built: the serial of a window of the seam, i
 * counting side a then side b; 0 for none. */
static ULONG esb_seam_serial(const ESSeam *seam, int i)
{
    int k = esb_sl_index(i < seam->a.n ? seam->a.ref[i]
                                       : seam->b.ref[i - seam->a.n]);

    return k >= 0 ? g_sl_win[k].serial : 0;
}

/*
 * g_sem held, IBase not, the list built: seam is the one the calling
 * task means now, at pos, last asked for at last_arg. A window with no
 * serial (the identity table was full) cannot be followed: the task is
 * then left without a record, and its moves take the nearest seam.
 */
static void esb_follow_store(const ESSeam *seam, LONG pos, LONG last_arg)
{
    struct Task *me = FindTask(NULL);
    struct ESBFollow *f;
    int i, n = seam->a.n + seam->b.n;

    for (i = 0; i < n; i++) {
        if (esb_seam_serial(seam, i) == 0) {
            esb_follow_drop();
            return;
        }
    }
    f = esb_follow_find(me);
    for (i = 0; f == NULL && i < ESB_FOLLOW_SLOTS; i++) {
        if (g_follow[i].task == NULL) {
            f = &g_follow[i];
        }
    }
    /* The table is full: the walk under Disable happens here only. */
    for (i = 0; f == NULL && i < ESB_FOLLOW_SLOTS; i++) {
        if (!esb_task_alive(g_follow[i].task)) {
            f = &g_follow[i];
        }
    }
    if (f == NULL) {
        f = &g_follow[0];
        for (i = 1; i < ESB_FOLLOW_SLOTS; i++) {
            if (g_follow[i].stamp < f->stamp) {
                f = &g_follow[i];
            }
        }
    }
    f->task = me;
    f->stamp = ++g_follow_clock;
    f->vertical = seam->vertical != 0;
    f->pos = pos;
    f->last_arg = last_arg;
    f->na = seam->a.n;
    f->nb = seam->b.n;
    for (i = 0; i < n; i++) {
        if (i < seam->a.n) {
            f->a[i] = esb_seam_serial(seam, i);
        } else {
            f->b[i - seam->a.n] = esb_seam_serial(seam, i);
        }
    }
}

/* Does side hold every one of refs[0..n-1]? */
static int esb_side_holds(const ESSeamSide *side, void *const *refs, int n)
{
    int i, j;

    for (i = 0; i < n; i++) {
        for (j = 0; j < side->n && side->ref[j] != refs[i]; j++) {
            ;
        }
        if (j == side->n) {
            return 0;
        }
    }
    return 1;
}

/*
 * g_sem held, the list built: the seam of the windows f names, into
 * *out. ES_ERR_STALE when one of them is gone; ES_ERR_UNSUPPORTED when
 * they are all there but are not that seam any more, on this screen
 * and for this caller. Windows of a group are that seam exactly
 * (es_seam_by_refs); windows of no group keep the rule of the line, so
 * their seam is the one nearest the record on that axis whose sides
 * hold them all, windows that joined it included.
 */
static LONG esb_follow_seam(const struct ESBFollow *f, ESSeam **out)
{
    unsigned long key = 0, best = 0;
    int plain = 0, keyed = 0, n = f->na + f->nb, i, k, m;

    *out = NULL;
    for (i = 0; i < n; i++) {
        if (!esb_serial_known(i < f->na ? f->a[i] : f->b[i - f->na])) {
            return ES_ERR_STALE;
        }
    }
    for (i = 0; i < n; i++) {
        ULONG serial = i < f->na ? f->a[i] : f->b[i - f->na];

        for (k = 0; k < g_sl_n && g_sl_win[k].serial != serial; k++) {
            ;
        }
        if (k == g_sl_n) {
            return ES_ERR_UNSUPPORTED;    /* not on the list */
        }
        if (i < f->na) {
            g_sl_ref[0][i] = g_sl_box[k].ref;
        } else {
            g_sl_ref[1][i - f->na] = g_sl_box[k].ref;
        }
        if (g_sl_box[k].key == 0) {
            plain = 1;
        } else if (!keyed) {
            keyed = 1;
            key = g_sl_box[k].key;
        } else if (g_sl_box[k].key != key) {
            return ES_ERR_UNSUPPORTED;
        }
    }
    if (plain && keyed) {
        return ES_ERR_UNSUPPORTED;
    }
    if (keyed) {
        if (es_seam_by_refs(g_sl_box, g_sl_n, ESB_SEAM_LINE_PX, f->vertical,
                            g_sl_ref[0], f->na, g_sl_ref[1], f->nb,
                            &g_sl_one) != 1) {
            return ES_ERR_UNSUPPORTED;
        }
        *out = &g_sl_one;
        return ES_OK;
    }
    m = esb_seams_found(ESB_SEAM_LINE_PX);
    for (i = 0; i < m; i++) {
        ESSeam *s = &g_sl_seam[i];
        unsigned long d;

        if (s->key != 0 || (s->vertical != 0) != (f->vertical != 0) ||
            !esb_side_holds(&s->a, g_sl_ref[0], f->na) ||
            !esb_side_holds(&s->b, g_sl_ref[1], f->nb)) {
            continue;
        }
        d = esb_distance(s->pos, f->pos);
        if (*out == NULL || d < best) {
            *out = s;
            best = d;
        }
    }
    return *out != NULL ? ES_OK : ES_ERR_UNSUPPORTED;
}

LONG esb_query_divider_at(ULONG thickness, LONG x, LONG y,
                          struct ESnapDivider *out)
{
    ESSeam *seam = NULL;
    int m, i, best_d = 0;

    if (out == NULL || thickness < 2 || thickness > 64) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    esb_seam_list(NULL);
    m = esb_seams_found((int)thickness);
    for (i = 0; i < m; i++) {
        int d;

        if (!esb_seam_room(&g_sl_seam[i])) {
            continue;
        }
        d = esb_box_distance(&g_sl_seam[i].rect, (int)x, (int)y);
        if (seam == NULL || d < best_d) {
            seam = &g_sl_seam[i];
            best_d = d;
        }
    }
    /*
     * Asked about a point ON the seam (within ESB_SEAM_TOUCH_PX of its
     * strip), the answer also says whether the seam is there to be
     * grabbed: the point of the strip nearest to the one asked about
     * must not lie under another window. Asked from further away, the
     * question is only which seam is nearest, and a window covering
     * some other part of it changes nothing.
     */
    if (seam != NULL && esb_seam_limits(seam)) {
        int visible = 1;

        if (best_d <= ESB_SEAM_TOUCH_PX) {
            const ESRect *r = &seam->rect;
            int cx = (int)x, cy = (int)y;

            if (cx < r->x) {
                cx = r->x;
            } else if (cx > r->x + r->w - 1) {
                cx = r->x + r->w - 1;
            }
            if (cy < r->y) {
                cy = r->y;
            } else if (cy > r->y + r->h - 1) {
                cy = r->y + r->h - 1;
            }
            visible = esb_seam_visible_at(seam, cx, cy);
        }
        if (visible) {
            esb_seam_report(seam, out);
            /* 2.24: what this task's next move on that line means; a
             * "no seam" leaves the record alone, since right after a
             * move the windows may not have caught up yet */
            esb_follow_store(seam, seam->pos, seam->pos);
        } else {
            esb_no_seam(out);
        }
    } else {
        esb_no_seam(out);
    }
    ReleaseSemaphore(&g_sem);
    return ES_OK;
}

LONG esb_move_divider_at(LONG vertical, LONG line, LONG position)
{
    struct ESBFollow *f;
    ESSeam *seam = NULL;
    LONG rc = ES_ERR_UNSUPPORTED, applied = 0;
    unsigned long best = 0;
    int m, i;

    ObtainSemaphore(&g_sem);
    esb_seam_list(NULL);
    f = esb_follow_find(FindTask(NULL));
    if (f != NULL && es_divider_follows(f->vertical, (int)f->pos,
                                        (int)f->last_arg, vertical != 0,
                                        (int)line)) {
        /* 2.24: the seam this task grabbed, or none, whatever other
         * seam lies on the line by now */
        rc = esb_follow_seam(f, &seam);
        if (rc == ES_ERR_STALE) {
            f->task = NULL;
        }
    } else {
        /*
         * The seam is named by its line: a layout can hold several, and
         * moving "the first one" would resize windows nobody grabbed.
         *
         * NEAREST on the same axis, not "within a tolerance". A dragged
         * seam moves away from where it was grabbed, so a fixed window of
         * a few pixels stops matching as soon as the user has pulled
         * further than that, which is a drag that works for a moment and
         * then dies. The caller follows the seam it is moving, so nearest
         * is both unambiguous and stable, and it still holds when the
         * position is clamped and the pointer runs on past the limit.
         */
        m = esb_seams_found(ESB_SEAM_LINE_PX);
        for (i = 0; i < m; i++) {
            unsigned long d;

            if (g_sl_seam[i].vertical != (vertical ? 1 : 0) ||
                !esb_seam_room(&g_sl_seam[i])) {
                continue;
            }
            d = esb_distance(g_sl_seam[i].pos, line);
            if (seam == NULL || d < best) {
                best = d;
                seam = &g_sl_seam[i];
            }
        }
        if (seam != NULL) {
            rc = ES_OK;
        }
    }
    if (rc == ES_OK) {
        rc = esb_seam_move(seam, position, 1, &applied);
        if (rc == ES_OK) {
            /* the drag goes on with these windows, wherever it leads */
            esb_follow_store(seam, applied, position);
        } else if (rc == ES_ERR_STALE) {
            esb_follow_drop();
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
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

/* ------------------------------------------------------ work areas (2.11) */

/*
 * The identity of a screen, kept across what a pointer does not
 * survive. A public screen is remembered by name for as long as the
 * library is loaded, so the Workbench screen reopened in another mode
 * finds its id again; a private screen is remembered by address and
 * forgotten once it is no longer open. Sixteen names, which is more
 * screens than a desktop opens; ids are never reused.
 */
#define ESB_SCREEN_SLOTS 16
#define ESB_SCREEN_NAME  32
struct ESBScreenId {
    ULONG id;                  /* 0 = free */
    int is_public;
    char name[ESB_SCREEN_NAME];
    struct Screen *scr;        /* private screens: the address, while open */
};
static struct ESBScreenId g_screens[ESB_SCREEN_SLOTS];
static ULONG g_screen_next = 1;

static int esb_str_eq(const char *a, const char *b)
{
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

/* IBase locked. Is scr an open screen right now? */
static int esb_screen_open_locked(struct Screen *scr)
{
    struct Screen *s;

    for (s = ESB_IBASE->FirstScreen; s != NULL; s = s->NextScreen) {
        if (s == scr) {
            return 1;
        }
    }
    return 0;
}

/*
 * The public name of scr, or an empty string. Takes Intuition's own
 * list lock and must therefore be called with IBase NOT locked.
 */
static void esb_screen_public_name(struct Screen *scr, char *out, int cap)
{
    struct List *l = LockPubScreenList();
    struct Node *n;

    out[0] = '\0';
    if (l != NULL) {
        for (n = l->lh_Head; n->ln_Succ != NULL; n = n->ln_Succ) {
            struct PubScreenNode *p = (struct PubScreenNode *)n;

            if (p->psn_Screen == scr && n->ln_Name != NULL) {
                int i;

                for (i = 0; i < cap - 1 && n->ln_Name[i] != '\0'; i++) {
                    out[i] = n->ln_Name[i];
                }
                out[i] = '\0';
                break;
            }
        }
    }
    UnlockPubScreenList();
}

/* g_sem held. The id of scr, made on first sight; 0 when out of room. */
static ULONG esb_screen_id(struct Screen *scr, const char *name)
{
    struct ESBScreenId *slot = NULL;
    int i;

    for (i = 0; i < ESB_SCREEN_SLOTS; i++) {
        struct ESBScreenId *e = &g_screens[i];

        if (e->id == 0) {
            if (slot == NULL) {
                slot = e;
            }
        } else if (name[0] != '\0') {
            if (e->is_public && esb_str_eq(e->name, name)) {
                e->scr = scr;
                return e->id;
            }
        } else if (!e->is_public && e->scr == scr) {
            return e->id;
        }
    }
    if (slot == NULL) {
        return 0;
    }
    slot->id = g_screen_next++;
    if (g_screen_next == 0) {
        g_screen_next = 1;
    }
    slot->is_public = name[0] != '\0';
    for (i = 0; i < ESB_SCREEN_NAME - 1 && name[i] != '\0'; i++) {
        slot->name[i] = name[i];
    }
    slot->name[i] = '\0';
    slot->scr = scr;
    return slot->id;
}

LONG esb_query_work_areas(struct Screen *scr, struct ESnapWorkArea *buf,
                          ULONG count, ULONG *needed)
{
    char name[ESB_SCREEN_NAME];
    ULONG ilock, id;
    LONG rc = ES_OK;
    int i;

    if (needed == NULL || (buf == NULL && count != 0)) {
        return ES_ERR_BAD_ARGS;
    }
    *needed = 0;
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    ilock = LockIBase(0);
    if (scr == NULL) {
        scr = ESB_IBASE->FirstScreen;
    }
    if (scr == NULL || !esb_screen_open_locked(scr)) {
        UnlockIBase(ilock);
        ReleaseSemaphore(&g_sem);
        return ES_ERR_STALE;
    }
    /* private ids die with their screen */
    for (i = 0; i < ESB_SCREEN_SLOTS; i++) {
        if (g_screens[i].id != 0 && !g_screens[i].is_public &&
            !esb_screen_open_locked(g_screens[i].scr)) {
            g_screens[i].id = 0;
        }
    }
    UnlockIBase(ilock);
    esb_screen_public_name(scr, name, (int)sizeof(name));
    *needed = 1;                   /* one area per screen, for now */
    if (count >= 1) {
        id = esb_screen_id(scr, name);
        if (id == 0) {
            rc = ES_ERR_NO_MEMORY;
        } else {
            ESRect usable;
            ESInsets ins;

            ilock = LockIBase(0);
            if (!esb_screen_open_locked(scr)) {
                UnlockIBase(ilock);
                ReleaseSemaphore(&g_sem);
                return ES_ERR_STALE;
            }
            esb_usable_area_locked(scr, NULL, &usable, &ins);
            buf[0].bounds.x = 0;
            buf[0].bounds.y = 0;
            buf[0].bounds.w = scr->Width;
            buf[0].bounds.h = scr->Height;
            UnlockIBase(ilock);
            buf[0].id = id;
            buf[0].monitor = 0;
            buf[0].area.usable.x = usable.x;
            buf[0].area.usable.y = usable.y;
            buf[0].area.usable.w = usable.w;
            buf[0].area.usable.h = usable.h;
            buf[0].area.insetLeft = ins.l;
            buf[0].area.insetTop = ins.t;
            buf[0].area.insetRight = ins.r;
            buf[0].area.insetBottom = ins.b;
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
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

/*
 * 2.24: a window EdgeSnap snapped, dragged by its title bar and let go
 * outside every zone, gets back the size it had before the snap, under
 * the pointer (es_unsnap_rect). esb_input calls this once it has let go
 * of the semaphore, with the release's actions in a.
 *
 * The decision needs the window where the drop left it. A window that
 * follows the pointer is there already. AROS drags an outline and moves
 * the window as the button comes up, inside the input handler's pass
 * that sees the release, ahead of this task; AmigaOS 4 in its ghost and
 * outline modes may do it later. So the window is looked at until it
 * has left the box it had when the drag began, for ESB_DROP_TICKS at
 * most, holding no lock. Only a window that never moves waits that
 * long, a press that slid off a gadget or a drag put back where it
 * began, and then nothing happens.
 *
 * Then, under the semaphore, the refusals of a snap and the registry's
 * own: snapped to a zone, still the snapped size, and let go beyond the
 * slack from where the snap put it and from where the drag began. The
 * glide is the restore hotkey's, ANIMATE and all; a drag never changes
 * depth on the Amiga, so nothing is raised.
 */
#define ESB_DROP_TICKS 10

static void esb_drag_out(const ESEngineActions *a)
{
    struct Window *win = (struct Window *)a->drop_ref;
    struct ESBSnap s;
    ESRect pre, r;
    int t;

    for (t = 0; ; t++) {
        if (!esb_sample(win, &s)) {
            ObtainSemaphore(&g_sem);
            es_registry_forget(&g_registry, win);
            ReleaseSemaphore(&g_sem);
            return;
        }
        if (s.box.x != a->drop_from.x || s.box.y != a->drop_from.y ||
            s.box.w != a->drop_from.w || s.box.h != a->drop_from.h ||
            t >= ESB_DROP_TICKS) {
            break;
        }
        Delay(1);
    }
    ObtainSemaphore(&g_sem);
    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
    } else if (!esb_is_excluded(win) && esb_snappable(&s) &&
               !esb_locked_for(win) && !esb_grouped(win) &&
               es_registry_drag_out(&g_registry, win, &a->drop_from,
                                    &s.box, &pre) == ES_OK) {
        es_unsnap_rect(&pre, &a->drop_from, a->drop_grab_x,
                       a->drop_grab_y, a->drop_x, a->drop_y, &s.usable,
                       s.min_w, s.min_h, s.max_w, s.max_h, &r);
        if (!esb_same_box(&r, &s.box)) {
            esb_member_placed(win, &s.box, &r);
            esb_glide_box(win, &s.box, &r, 0);
        }
    }
    ReleaseSemaphore(&g_sem);
}

void esb_input(int press, int motion, int release, ULONG quals,
               struct ESnapReport *out)
{
    ESEngineActions a;
    struct ESBSnap s;
    struct Window *win = NULL;
    int do_snap = 0;
    int do_drop = 0;
    int snap_zone = ES_ZONE_NONE;
    ESRect snap_rect;

    esb_report_clear(out);
    if (!g_ready) {
        return;
    }

    ObtainSemaphore(&g_sem);
    if (!esb_engine_input_ok()) {
        ReleaseSemaphore(&g_sem);    /* not the engine's owner: ignored */
        return;
    }
    if (!g_enabled) {
        /* disabled mid-drag: make sure no frame is left behind */
        es_engine_reset(&g_engine, &a);
        esb_absorb(&a, out);
        ReleaseSemaphore(&g_sem);
        return;
    }

    if (press) {
        struct ESBSnap ps;
        struct Window *pw;
        int mx = -1, my = -1;

        /* where the button went down: the pointer on the screen of
         * whatever window is active right now, which may still be the
         * one about to lose the activation; and that window's box,
         * read with it, for a pull that has taken the window away by
         * the first motion (2.24, es_engine_press_on) */
        pw = esb_sample_active(&ps);
        if (pw != NULL) {
            mx = ps.mouse_x;
            my = ps.mouse_y;
        }
        esb_clients_sweep();   /* a dead owner locks nothing; once per press */
        es_engine_press_on(&g_engine, pw, pw != NULL ? &ps.box : NULL,
                           mx, my, &a);
        esb_absorb(&a, out);
    }

    if (motion && g_engine.button_down && !release) {
        win = esb_sample_active(&s);
        if (win == NULL) {
            g_push_x = 0;
            g_push_y = 0;
            es_engine_motion(&g_engine, 0, &a);
        } else {
            ESWinFacts f;
            ULONG bypass = esb_bypass_mask();

            f.push_x = (int)g_push_x;      /* what FeedMotion reported */
            f.push_y = (int)g_push_y;
            g_push_x = 0;
            g_push_y = 0;
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
                if (esb_locked_for(win)) {
                    g_gen_touch++;     /* the owner will want to know */
                } else {
                    f.flags |= ES_WF_SNAPPABLE;
                }
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
        } else if (a.do_drop) {
            int z = es_registry_zone(&g_registry, a.drop_ref);

            /* the cheap half of the decision, here: nothing is waited
             * for on a window EdgeSnap did not snap, nor with the
             * bypass key down as the button came up */
            do_drop = g_cfg.drag_restore &&
                      (quals & esb_bypass_mask()) == 0 &&
                      z != ES_ZONE_NONE && z != ES_ZONE_RECT &&
                      !esb_grouped((struct Window *)a.drop_ref);
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
    /* so does the drop, which waits for Intuition first; a still holds
     * the release's actions */
    if (do_drop) {
        esb_drag_out(&a);
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
    if (!esb_engine_input_ok()) {
        ReleaseSemaphore(&g_sem);      /* not the engine's owner: ignored */
        return;
    }
    es_engine_reset(&g_engine, &a);
    ReleaseSemaphore(&g_sem);
    esb_absorb(&a, out);
}

/*
 * The 2.2 vector, kept with its 2.2 meaning: move whichever seam the
 * layout walk finds first. A 2.x client compiled against 2.2 must keep
 * working on every later 2.x, so this neither moves nor changes shape;
 * anything that knows which seam it means calls esb_move_divider_at().
 * From 2.24 that first seam is moved here, not through
 * esb_move_divider_at(), whose record of the seam the caller last
 * grabbed could name another one on the same line.
 */
LONG esb_move_divider(LONG position)
{
    ESSeam *seam;
    LONG rc = ES_ERR_UNSUPPORTED;

    ObtainSemaphore(&g_sem);
    esb_seam_list(NULL);
    seam = esb_first_seam(esb_seams_found(ESB_SEAM_LINE_PX));
    if (seam != NULL) {
        rc = esb_seam_move(seam, position, 1, NULL);
    }
    ReleaseSemaphore(&g_sem);
    return rc;
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
    ULONG h = esb_hash_mix(2166136261UL, g_gen_touch);
    struct Screen *scr;
    struct Window *w;

    for (scr = ESB_IBASE->FirstScreen; scr != NULL; scr = scr->NextScreen) {
        h = esb_hash_mix(h, (ULONG)(ESTagData)scr);
        for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
            h = esb_hash_mix(h, (ULONG)(ESTagData)w);
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

    if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET)) != 0) {
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
    ESRect r, box, area;
    LONG rc;

    if (!esb_sample(win, &s)) {
        es_registry_forget(&g_registry, win);
        return ES_ERR_STALE;
    }
    if (esb_is_excluded(win) || !esb_snappable(&s) || esb_locked_for(win)) {
        return ES_ERR_REJECTED;
    }
    /*
     * 2.20: a cell of the group's layout. The window's box is the cell
     * less the group's margins and half gaps, and its size limits are
     * fitted against the area less the margins, so a window that has to
     * grow still keeps to the margin it is against. A window in no
     * group takes the cell as it is.
     */
    if ((flags & ES_PF_CELL) != 0 &&
        esb_group_cell(win, want, &s.usable, &box, &area)) {
        es_fit_rect(&box, &area, s.min_w, s.min_h, s.max_w, s.max_h, &r);
    } else {
        es_fit_rect(want, &s.usable, s.min_w, s.min_h, s.max_w, s.max_h,
                    &r);
    }
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
    esb_member_placed(win, &s.box, &r);
    esb_change_box(win, &s.box, &r, (flags & ES_PF_KEEP_ZORDER) == 0);
    return ES_OK;
}

#define ESB_PF_ALL (ES_PF_NO_RESTORE | ES_PF_KEEP_ZORDER | ES_PF_CELL)

/* ---------------------------------------------------------- groups (2.12) */

/*
 * A group: these windows, in this work area, owned by this client.
 * Membership is by serial, the identity of 2.6, so a window that
 * closes and another that opens at its address are never confused;
 * a member whose serial no longer finds a window is dropped whenever
 * the group is touched. Sixteen groups of thirty-two, ids never reused.
 *
 * 2.24: member k also has the box EdgeSnap last put it in, the one
 * before (ChangeWindowBox is asynchronous, so for a moment the window
 * may still show that one) and when EdgeSnap last asked it to move, and
 * the group's divider tokens name their windows by member index.
 * member[], placed[], prev[], asked[] and the token masks move
 * together: a member that leaves is replaced by the last one, with its
 * boxes and its time, in esb_group_drop_member and nowhere else.
 */
#define ESB_GROUP_SLOTS   16
#define ESB_GROUP_NAME    32
#define ESB_GROUP_MEMBERS 32

/*
 * 2.24: how long a member EdgeSnap asked to move may show any box at
 * all and still not count as moved by hand (ES_DIF_MOVED,
 * ES_ERR_CHANGED). Requests queue up in Intuition, and the window goes
 * through each box asked for, or on AROS through the box between two
 * steps of one change (esb_change_box): no box it shows meanwhile says
 * whether the user took it. Whole seconds of intuition's CurrentTime,
 * so the window has at least two of them and less than three.
 */
#define ESB_TRANSIT_SECS 2

struct ESBGroup {
    ULONG id;                  /* 0 = free */
    ULONG area;                /* a work area id, 0 for wherever */
    ULONG owner;               /* the client that made it */
    struct Task *task;         /* its task: the only one that may touch it */
    ULONG flags;               /* ES_GF_* */
    char name[ESB_GROUP_NAME];
    ULONG member[ESB_GROUP_MEMBERS];
    int members;
    int gap;                   /* 2.20: room between two cells     */
    int margin[4];             /* 2.20: left, top, right, bottom   */
    ESRect placed[ESB_GROUP_MEMBERS];  /* 2.24: where EdgeSnap put it */
    ESRect prev[ESB_GROUP_MEMBERS];    /* 2.24: the box before that   */
    ULONG asked[ESB_GROUP_MEMBERS];    /* 2.24: when EdgeSnap last    */
                                       /* asked it to move (seconds), */
                                       /* 0 if not since it joined    */
    ESDivTokens tok;           /* 2.24: its dividers' tokens       */
};
static struct ESBGroup g_groups[ESB_GROUP_SLOTS];
static ULONG g_group_next = 1;

/* A mask names every member, a list holds a whole group, a seam's side
 * holds a whole group, and a group has no more seams than the result
 * array takes (two per member: see ESB_SEAM_RESULTS). */
typedef char esb_tok_bits[(ESB_GROUP_MEMBERS <= ES_DIVTOK_MEMBERS) ? 1 : -1];
typedef char esb_group_listed[(ESB_GROUP_MEMBERS <= ES_SEAM_BOXES_MAX &&
                               ESB_GROUP_MEMBERS <= ES_SEAM_SIDE_MAX)
                              ? 1 : -1];
typedef char esb_group_seams[(2 * ESB_GROUP_MEMBERS <= ESB_SEAM_RESULTS)
                             ? 1 : -1];

/* g_sem held. The serial of a window the library has already seen,
 * without observing: 0 for one it never met, which is in no group. */
static ULONG esb_serial_of_ptr(struct Window *win)
{
    int i;

    for (i = 0; i < ESB_IDENT_SLOTS; i++) {
        if (g_ident[i].win == win && win != NULL) {
            return g_ident[i].serial;
        }
    }
    return 0;
}

/* g_sem held. Is this serial still a window the library knows? */
static int esb_serial_known(ULONG serial)
{
    int i;

    for (i = 0; i < ESB_IDENT_SLOTS; i++) {
        if (g_ident[i].win != NULL && g_ident[i].serial == serial) {
            return 1;
        }
    }
    return 0;
}

/* g_sem held. */
static struct ESBGroup *esb_group_by_id(ULONG id)
{
    int i;

    for (i = 0; i < ESB_GROUP_SLOTS; i++) {
        if (g_groups[i].id != 0 && g_groups[i].id == id) {
            return &g_groups[i];
        }
    }
    return NULL;
}

/* g_sem held. The group a serial belongs to, or NULL. */
static struct ESBGroup *esb_group_of_serial(ULONG serial)
{
    int i, k;

    if (serial == 0) {
        return NULL;
    }
    for (i = 0; i < ESB_GROUP_SLOTS; i++) {
        struct ESBGroup *g = &g_groups[i];

        if (g->id == 0) {
            continue;
        }
        for (k = 0; k < g->members; k++) {
            if (g->member[k] == serial) {
                return g;
            }
        }
    }
    return NULL;
}

/* g_sem held. Where serial stands in g's member list, or -1. */
static int esb_member_index(const struct ESBGroup *g, ULONG serial)
{
    int k;

    if (g == NULL || serial == 0) {
        return -1;
    }
    for (k = 0; k < g->members; k++) {
        if (g->member[k] == serial) {
            return k;
        }
    }
    return -1;
}

/* IBase not held. Intuition's clock, in seconds: ESB_TRANSIT_SECS. */
static ULONG esb_now(void)
{
    ULONG secs = 0, micros = 0;

    CurrentTime(&secs, &micros);
    return secs;
}

/*
 * g_sem held. 2.24: member k of g, seen at live, was just asked to take
 * box: that is now where EdgeSnap put it, and the request is the one
 * esb_member_in_transit counts from. The box before is kept as well,
 * for a window that has not caught up with ChangeWindowBox once its
 * transit is over: the one it was last put in, or, when it has not
 * even left the one before that (asked again before the last request
 * landed), that one, where it still is.
 */
static void esb_member_put(struct ESBGroup *g, int k, const ESRect *live,
                           const ESRect *box)
{
    if (!esb_same_box(live, &g->prev[k]) ||
        esb_same_box(live, &g->placed[k])) {
        g->prev[k] = g->placed[k];
    }
    g->placed[k] = *box;
    g->asked[k] = esb_now();
}

/*
 * g_sem held, now from esb_now. 2.24: was member k of g asked to move
 * no more than ESB_TRANSIT_SECS whole seconds ago? Then it may still be
 * on its way, whatever box it shows, and is never counted as moved. A
 * clock set back since (by hand, or by an hour in the autumn) would
 * hold it there until the clock caught up: the request then counts from
 * the moment the clock is first seen behind it. A member not asked
 * since it joined (asked 0) is in transit only while the clock reads
 * under three seconds, which it does only just after a machine with no
 * clock of its own has started.
 */
static int esb_member_in_transit(struct ESBGroup *g, int k, ULONG now)
{
    if (now < g->asked[k]) {
        g->asked[k] = now;
    }
    return now - g->asked[k] <= ESB_TRANSIT_SECS;
}

/* g_sem held. win, seen at live, was just asked to take box: if it is
 * a member, as esb_member_put says. */
static void esb_member_placed(struct Window *win, const ESRect *live,
                              const ESRect *box)
{
    ULONG serial = esb_serial_of_ptr(win);
    struct ESBGroup *g = esb_group_of_serial(serial);
    int k = esb_member_index(g, serial);

    if (k >= 0) {
        esb_member_put(g, k, live, box);
    }
}

/*
 * g_sem held. 2.20: a cell of a layout made into a window box with the
 * gaps and margins of the window's group, and the area less the
 * margins; 0 for a window in no group.
 */
static int esb_group_cell(struct Window *win, const ESRect *cell,
                          const ESRect *usable, ESRect *box, ESRect *area)
{
    struct ESBGroup *g = esb_group_of_serial(esb_serial_of_ptr(win));

    if (g == NULL) {
        return 0;
    }
    es_cell_rect(cell, usable, g->gap, g->margin, box, area);
    return 1;
}

/*
 * g_sem held. Member k leaves: the last one takes its place, with its
 * boxes and its time, and every token naming k retires (2.24): a token
 * names its windows by member index, so it must never come to name
 * another.
 */
static void esb_group_drop_member(struct ESBGroup *g, int k)
{
    int last = g->members - 1;

    es_divtok_member_left(&g->tok, k, last);
    g->member[k] = g->member[last];
    g->placed[k] = g->placed[last];
    g->prev[k] = g->prev[last];
    g->asked[k] = g->asked[last];
    g->members = last;
}

/* g_sem held, identities observed: members that closed leave. */
static void esb_group_sweep_members(struct ESBGroup *g)
{
    int k = 0;

    while (k < g->members) {
        if (!esb_serial_known(g->member[k])) {
            esb_group_drop_member(g, k);
        } else {
            k++;
        }
    }
}

/*
 * A group goes (deleted, its owner gone, the library cleared): its slot
 * is free, its tokens retire, and the times of its members (2.24) go
 * with it. esb_create_group starts a slot afresh as well.
 */
static void esb_group_free(struct ESBGroup *g)
{
    int k;

    g->id = 0;
    es_divtok_clear(&g->tok);
    for (k = 0; k < ESB_GROUP_MEMBERS; k++) {
        g->asked[k] = 0;
    }
}

static void esb_groups_clear(void)
{
    int i;

    for (i = 0; i < ESB_GROUP_SLOTS; i++) {
        esb_group_free(&g_groups[i]);
    }
}

/* g_sem held. */
static void esb_groups_drop_owner(ULONG client)
{
    int i;

    for (i = 0; i < ESB_GROUP_SLOTS; i++) {
        if (g_groups[i].id != 0 && g_groups[i].owner == client) {
            esb_group_free(&g_groups[i]);
        }
    }
}

/*
 * g_sem held. Does g keep everyone else out? The owner's own task
 * passes. An owner that switched its layout off, or one whose
 * registration is gone (the sweep at every press takes its groups with
 * it), keeps nobody out.
 */
static int esb_group_locked_for(const struct ESBGroup *g)
{
    struct ESBClient *c;

    if (g == NULL || (g->flags & ES_GF_LOCKED) == 0) {
        return 0;
    }
    if (g->task == FindTask(NULL)) {
        return 0;
    }
    c = esb_client_by_id(g->owner);
    return c != NULL && c->layout_on;
}

/* g_sem held. Is win in a group that keeps everyone else out? */
static int esb_locked_for(struct Window *win)
{
    return esb_group_locked_for(esb_group_of_serial(esb_serial_of_ptr(win)));
}

/* g_sem held. Is win in a group at all, locked or not? A drag gives no
 * member its old size back (2.24): where it goes is its group's. */
static int esb_grouped(struct Window *win)
{
    return esb_group_of_serial(esb_serial_of_ptr(win)) != NULL;
}

/* g_sem held. Is this an id QueryWorkAreas handed out? */
static int esb_area_known(ULONG area)
{
    int i;

    for (i = 0; i < ESB_SCREEN_SLOTS; i++) {
        if (g_screens[i].id != 0 && g_screens[i].id == area) {
            return 1;
        }
    }
    return 0;
}

LONG esb_create_group(ULONG area, const char *name, ULONG flags,
                      ULONG *group_out)
{
    struct ESBClient *c;
    struct ESBGroup *g = NULL;
    LONG rc = ES_OK;
    int i;

    if (group_out == NULL || (flags & ~ES_GF_LOCKED) != 0) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    *group_out = 0;
    ObtainSemaphore(&g_sem);
    esb_clients_sweep();
    c = esb_client_of(FindTask(NULL));
    if (c == NULL || (c->roles & ES_CL_LAYOUT) == 0) {
        rc = ES_ERR_REJECTED;          /* the role is what says "mine" */
    } else if (area != 0 && !esb_area_known(area)) {
        rc = ES_ERR_STALE;
    } else {
        for (i = 0; i < ESB_GROUP_SLOTS; i++) {
            if (g_groups[i].id == 0) {
                g = &g_groups[i];
                break;
            }
        }
        if (g == NULL) {
            rc = ES_ERR_NO_MEMORY;
        } else {
            g->id = g_group_next++;
            if (g_group_next == 0) {
                g_group_next = 1;
            }
            g->area = area;
            g->owner = c->id;
            g->task = c->task;
            g->flags = flags;
            g->members = 0;
            g->gap = 0;
            g->margin[0] = g->margin[1] = g->margin[2] = g->margin[3] = 0;
            for (i = 0; i < ESB_GROUP_MEMBERS; i++) {
                g->placed[i].x = g->placed[i].y = 0;
                g->placed[i].w = g->placed[i].h = 0;
                g->prev[i] = g->placed[i];
                g->asked[i] = 0;
            }
            es_divtok_clear(&g->tok);
            for (i = 0; i < ESB_GROUP_NAME - 1 && name != NULL && name[i] != '\0'; i++) {
                g->name[i] = name[i];
            }
            g->name[i] = '\0';
            *group_out = g->id;
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

/* g_sem held. The group, when the caller may touch it: rc says why not. */
static struct ESBGroup *esb_group_mine(ULONG group, LONG *rc)
{
    struct ESBGroup *g = esb_group_by_id(group);

    if (g == NULL) {
        *rc = ES_ERR_STALE;
        return NULL;
    }
    if (g->task != FindTask(NULL)) {
        *rc = ES_ERR_REJECTED;
        return NULL;
    }
    *rc = ES_OK;
    return g;
}

LONG esb_delete_group(ULONG group)
{
    struct ESBGroup *g;
    LONG rc;

    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    g = esb_group_mine(group, &rc);
    if (g != NULL) {
        esb_group_free(g);
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

/*
 * 2.20: the group's gaps and margins. Applied to a copy first, so that
 * a value out of range changes nothing, as ESnap_SetOptionsA does.
 */
LONG esb_set_group_options(ULONG group, const struct TagItem *tags)
{
    const struct TagItem *scan = tags;
    const struct TagItem *ti;
    struct ESBGroup *g;
    int gap, margin[4];
    LONG rc, v;

    if (tags == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    g = esb_group_mine(group, &rc);
    if (g != NULL) {
        gap = g->gap;
        margin[0] = g->margin[0];
        margin[1] = g->margin[1];
        margin[2] = g->margin[2];
        margin[3] = g->margin[3];
        while (rc == ES_OK && (ti = esb_next_tag(&scan)) != NULL) {
            v = (LONG)ti->ti_Data;
            switch (ti->ti_Tag) {
            case ES_GO_Gap:
                if (v < 0 || v > ES_GO_GAP_MAX) {
                    rc = ES_ERR_BAD_ARGS;
                } else {
                    gap = (int)v;
                }
                break;
            case ES_GO_Margin:
                if (v < 0 || v > ES_GO_MARGIN_MAX) {
                    rc = ES_ERR_BAD_ARGS;
                } else {
                    margin[0] = margin[1] = margin[2] = margin[3] = (int)v;
                }
                break;
            case ES_GO_MarginLeft:
            case ES_GO_MarginTop:
            case ES_GO_MarginRight:
            case ES_GO_MarginBottom:
                if (v < 0 || v > ES_GO_MARGIN_MAX) {
                    rc = ES_ERR_BAD_ARGS;
                } else {
                    margin[ti->ti_Tag - ES_GO_MarginLeft] = (int)v;
                }
                break;
            default:
                break;                 /* a later revision's tag */
            }
        }
        if (rc == ES_OK) {
            g->gap = gap;
            g->margin[0] = margin[0];
            g->margin[1] = margin[1];
            g->margin[2] = margin[2];
            g->margin[3] = margin[3];
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

/* g_sem held. The serial of a live window, observing it, or 0; and
 * where it is, when box is not NULL and it was found. */
static ULONG esb_serial_observe(struct Window *win, ESRect *box)
{
    ULONG ilock, serial = 0;
    struct Screen *scr;
    struct Window *w;
    int found = 0;

    ilock = LockIBase(0);
    esb_ident_observe_all();
    for (scr = ESB_IBASE->FirstScreen; scr != NULL && !found;
         scr = scr->NextScreen) {
        for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
            if (w == win) {
                serial = esb_ident_touch(w);
                if (box != NULL) {
                    box->x = w->LeftEdge;
                    box->y = w->TopEdge;
                    box->w = w->Width;
                    box->h = w->Height;
                }
                found = 1;
                break;
            }
        }
    }
    UnlockIBase(ilock);
    return serial;
}

LONG esb_group_add_window(ULONG group, struct Window *win)
{
    struct ESBGroup *g, *other;
    ESRect box;
    ULONG serial;
    LONG rc;
    int k;

    if (win == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    g = esb_group_mine(group, &rc);
    if (g != NULL) {
        serial = esb_serial_observe(win, &box);
        if (serial == 0) {
            rc = ES_ERR_STALE;
        } else {
            for (k = 0; k < ESB_GROUP_SLOTS; k++) {
                if (g_groups[k].id != 0) {
                    esb_group_sweep_members(&g_groups[k]);
                }
            }
            other = esb_group_of_serial(serial);
            if (other == g) {
                rc = ES_OK;                    /* already here */
            } else if (other != NULL) {
                rc = ES_ERR_REJECTED;          /* one group at a time */
            } else if (g->members >= ESB_GROUP_MEMBERS) {
                rc = ES_ERR_NO_MEMORY;
            } else {
                /* 2.24: where it stands is where its seams start from:
                 * the box a snap or placement gave it, if one did, else
                 * the one it has */
                k = g->members++;
                g->member[k] = serial;
                if (!es_registry_snapped(&g_registry, win, &g->placed[k])) {
                    g->placed[k] = box;
                }
                g->prev[k] = g->placed[k];
                g->asked[k] = 0;
                rc = ES_OK;
            }
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_group_remove_window(ULONG group, struct Window *win)
{
    struct ESBGroup *g;
    ULONG serial;
    LONG rc;
    int k;

    if (win == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    g = esb_group_mine(group, &rc);
    if (g != NULL) {
        rc = ES_ERR_STALE;
        serial = esb_serial_of_ptr(win);
        for (k = 0; k < g->members; k++) {
            if (serial != 0 && g->member[k] == serial) {
                esb_group_drop_member(g, k);
                rc = ES_OK;
                break;
            }
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_query_group_of(struct Window *win, ULONG *group_out)
{
    struct ESBGroup *g;
    ULONG serial;
    LONG rc = ES_OK;
    int k;

    if (win == NULL || group_out == NULL) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    *group_out = 0;
    ObtainSemaphore(&g_sem);
    serial = esb_serial_observe(win, NULL);
    if (serial == 0) {
        rc = ES_ERR_STALE;
    } else {
        for (k = 0; k < ESB_GROUP_SLOTS; k++) {
            if (g_groups[k].id != 0) {
                esb_group_sweep_members(&g_groups[k]);
            }
        }
        g = esb_group_of_serial(serial);
        *group_out = (g != NULL) ? g->id : 0;
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

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

/* How each window of a layout grows, and the order of the moves: in
 * static memory, used only with g_sem held, since the library runs on
 * the caller's stack and the two would take half a kilobyte of it. */
static long g_place_delta[ES_PLACE_MAX];
static int g_place_order[ES_PLACE_MAX];

LONG esb_place_windows(struct ESnapPlacement *list, ULONG count,
                       ULONG flags)
{
    ULONG i, k;

    if (list == NULL || count == 0 || count > ES_PLACE_MAX ||
        (flags & ~ESB_PF_ALL) != 0) {
        return ES_ERR_BAD_ARGS;
    }
    ObtainSemaphore(&g_sem);
    /* first pass: what each window is now, so the moves can be ordered */
    for (i = 0; i < count; i++) {
        struct ESBSnap s;

        g_place_delta[i] = 0;
        list[i].result = ES_OK;
        if (list[i].window == NULL || list[i].rect.w <= 0 ||
            list[i].rect.h <= 0) {
            list[i].result = ES_ERR_BAD_ARGS;
        } else if (!esb_sample(list[i].window, &s)) {
            es_registry_forget(&g_registry, list[i].window);
            list[i].result = ES_ERR_STALE;
        } else {
            g_place_delta[i] = (long)list[i].rect.w * (long)list[i].rect.h -
                               (long)s.box.w * (long)s.box.h;
        }
    }
    es_order_by_growth(g_place_delta, (int)count, g_place_order);
    /* second pass: shrinking first, growing last */
    for (k = 0; k < count; k++) {
        ESRect want;

        i = (ULONG)g_place_order[k];
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

/* ------------------------------------------- dividers of the list (2.24) */

/* IBase locked. Is target a window of some screen right now? */
static int esb_window_open_locked(struct Window *target)
{
    struct Screen *scr;
    struct Window *w;

    for (scr = ESB_IBASE->FirstScreen; scr != NULL; scr = scr->NextScreen) {
        for (w = scr->FirstWindow; w != NULL; w = w->NextWindow) {
            if (w == target) {
                return 1;
            }
        }
    }
    return 0;
}

/* g_sem and IBase held, identities observed: the window of a serial,
 * or NULL. Every entry the observation kept is a live window, with
 * the layer it had when it got its serial. */
static struct Window *esb_window_of_serial_locked(ULONG serial)
{
    int i;

    for (i = 0; i < ESB_IDENT_SLOTS; i++) {
        if (g_ident[i].win != NULL && g_ident[i].serial == serial) {
            return g_ident[i].win;
        }
    }
    return NULL;
}

/* IBase held. What a seam needs of w: where it is, its layer and its
 * size limits; not the usable area, which no seam uses. */
static void esb_seam_facts(struct Window *w, struct ESBSeamWin *o)
{
    o->layer = w->WLayer;
    o->live.x = w->LeftEdge;
    o->live.y = w->TopEdge;
    o->live.w = w->Width;
    o->live.h = w->Height;
    o->min_w = w->MinWidth;
    o->min_h = w->MinHeight;
    o->max_w = (int)(UWORD)w->MaxWidth;
    o->max_h = (int)(UWORD)w->MaxHeight;
    if (o->max_w >= 0xFFFF) {
        o->max_w = 0;
    }
    if (o->max_h >= 0xFFFF) {
        o->max_h = 0;
    }
}

/*
 * g_sem and IBase held, identities observed: w goes on the list, with
 * box (where EdgeSnap put it) and scope, as member k of g, or with g
 * NULL as a window of no group. A member found where EdgeSnap put it
 * last has caught up with ChangeWindowBox: the box before stops
 * counting from then on. A member EdgeSnap asked to move moments ago
 * (esb_member_in_transit, by the clock the list was built at) is never
 * moved, whatever it shows; after that it is moved when it is neither
 * where EdgeSnap put it last nor in the box before.
 */
static void esb_seam_add(struct Window *w, const ESRect *box,
                         struct Screen *scope, struct ESBGroup *g, int k)
{
    ESSeamBox *b = &g_sl_box[g_sl_n];
    struct ESBSeamWin *o = &g_sl_win[g_sl_n];

    b->ref = w;
    b->box = *box;
    b->scope = scope;
    b->key = (g != NULL) ? g->id : 0;
    b->gap = (g != NULL) ? g->gap : 0;
    esb_seam_facts(w, o);
    o->serial = esb_serial_of_ptr(w);
    o->member = (g != NULL) ? k : -1;
    if (g == NULL) {
        o->moved = !esb_same_box(&o->live, box);
    } else {
        if (esb_same_box(&o->live, &g->placed[k])) {
            g->prev[k] = g->placed[k];
        }
        o->moved = !esb_member_in_transit(g, k, g_sl_now) &&
                   !esb_same_box(&o->live, &g->placed[k]) &&
                   !esb_same_box(&o->live, &g->prev[k]);
    }
    g_sl_n++;
}

/* For the list of the front screen: a registry slot's group (its index
 * plus one, 0 for none) and member, and the members of one group to
 * list. */
static unsigned char g_sl_slot_g[ES_REGISTRY_SLOTS];
static unsigned char g_sl_slot_k[ES_REGISTRY_SLOTS];
static unsigned char g_sl_pick[ESB_GROUP_MEMBERS];

/*
 * g_sem held, IBase not: the list every divider call works on, into
 * g_sl_box[] and g_sl_win[]; returns how many windows it holds. Takes
 * LockIBase itself, observes every window's identity and lets members
 * that closed leave their groups first, so member indices hold for the
 * rest of the caller's hold.
 *
 * group not NULL: its members (QueryDividers, MoveDividerT), in member
 * order, each in the box EdgeSnap last put it in, on whatever screen it
 * is (the scope keeps screens apart).
 *
 * group NULL: the front screen (the calls of 2.2 and 2.3). First the
 * windows of the snap registry, in slot order, in the box the snap put
 * them in, with key 0, as before 2.24; except that an entry whose
 * window is gone is forgotten here (until 2.23 the check of a seam's
 * windows did it), and a member of a group goes with its group. Then
 * the groups, in group order: the members on that screen, those of the
 * registry first, in the box EdgeSnap last put them in, with the
 * group's id and gap. A group locked for the caller is left out
 * altogether, so its seams are never reported, never moved and never
 * stand in the way of another; and so are members that are excluded
 * or cannot be snapped. A list holds ES_SEAM_BOXES_MAX windows: the
 * registry's always fit, and a group is listed whole or not at all, in
 * group order, so what does not fit is dropped whole.
 */
static int esb_seam_list(struct ESBGroup *group)
{
    struct Screen *s;
    struct Window *w;
    ULONG ilock;
    int i, k, gi, count;

    g_sl_n = 0;
    g_sl_now = esb_now();          /* an Intuition call: not under IBase */
    ilock = LockIBase(0);
    esb_ident_observe_all();
    if (group != NULL) {
        esb_group_sweep_members(group);
        for (k = 0; k < group->members; k++) {
            w = esb_window_of_serial_locked(group->member[k]);
            if (w != NULL && !esb_is_excluded(w) &&
                esb_flags_snappable(w->Flags)) {
                esb_seam_add(w, &group->placed[k], w->WScreen, group, k);
            }
        }
        UnlockIBase(ilock);
        return g_sl_n;
    }
    for (gi = 0; gi < ESB_GROUP_SLOTS; gi++) {
        if (g_groups[gi].id != 0) {
            esb_group_sweep_members(&g_groups[gi]);
        }
    }
    s = ESB_IBASE->FirstScreen;
    for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
        ESRegistryEntry *e = &g_registry.slot[i];
        struct ESBGroup *g;
        ULONG serial;

        g_sl_slot_g[i] = 0;
        if (!e->used) {
            continue;
        }
        w = (struct Window *)e->ref;
        if (!esb_window_open_locked(w)) {
            es_registry_forget(&g_registry, w);
            continue;
        }
        if (s == NULL || w->WScreen != s) {
            continue;
        }
        serial = esb_serial_of_ptr(w);
        g = esb_group_of_serial(serial);
        k = esb_member_index(g, serial);
        if (k < 0) {
            esb_seam_add(w, &e->snapped, s, NULL, 0);
        } else {
            g_sl_slot_g[i] = (unsigned char)(g - g_groups + 1);
            g_sl_slot_k[i] = (unsigned char)k;
        }
    }
    for (gi = 0; s != NULL && gi < ESB_GROUP_SLOTS; gi++) {
        struct ESBGroup *g = &g_groups[gi];
        unsigned long picked = 0;

        if (g->id == 0 || esb_group_locked_for(g)) {
            continue;
        }
        count = 0;
        for (i = 0; i < ES_REGISTRY_SLOTS; i++) {
            k = g_sl_slot_k[i];
            if (g_sl_slot_g[i] == gi + 1 &&
                (picked & ES_DIVTOK_BIT(k)) == 0) {
                picked |= ES_DIVTOK_BIT(k);
                g_sl_pick[count++] = (unsigned char)k;
            }
        }
        for (k = 0; k < g->members; k++) {
            if ((picked & ES_DIVTOK_BIT(k)) == 0) {
                w = esb_window_of_serial_locked(g->member[k]);
                if (w != NULL && w->WScreen == s) {
                    g_sl_pick[count++] = (unsigned char)k;
                }
            }
        }
        /* what may not be snapped is not divided either */
        for (i = 0, k = 0; i < count; i++) {
            w = esb_window_of_serial_locked(g->member[g_sl_pick[i]]);
            if (w != NULL && !esb_is_excluded(w) &&
                esb_flags_snappable(w->Flags)) {
                g_sl_pick[k++] = g_sl_pick[i];
            }
        }
        count = k;
        if (g_sl_n + count > ES_SEAM_BOXES_MAX) {
            continue;              /* listed whole or not at all */
        }
        for (i = 0; i < count; i++) {
            k = g_sl_pick[i];
            esb_seam_add(esb_window_of_serial_locked(g->member[k]),
                         &g->placed[k], s, g, k);
        }
    }
    UnlockIBase(ilock);
    return g_sl_n;
}

/*
 * g_sem held, IBase not: every window of the seam still there, still
 * the window the list saw (its layer), and read again, since a move
 * must not trust what the list saw however recently. ES_ERR_STALE when
 * one is not: a seam with one gone is not a seam, and dropping the
 * state beats resizing a stranger that inherited the address.
 */
static LONG esb_seam_revalidate(const ESSeam *seam)
{
    ULONG ilock;
    LONG rc = ES_OK;
    int i, k, n = seam->a.n + seam->b.n;

    ilock = LockIBase(0);
    for (i = 0; i < n && rc == ES_OK; i++) {
        struct Window *w = (struct Window *)(i < seam->a.n
            ? seam->a.ref[i] : seam->b.ref[i - seam->a.n]);

        k = esb_sl_index(w);
        if (k < 0 || !esb_window_open_locked(w) ||
            w->WLayer != g_sl_win[k].layer) {
            rc = ES_ERR_STALE;
        } else {
            esb_seam_facts(w, &g_sl_win[k]);
        }
    }
    UnlockIBase(ilock);
    return rc;
}

/*
 * g_sem held, IBase not, and held through the whole move, as
 * esb_place_windows does: the one way a seam is moved, by MoveDivider,
 * MoveDividerAt and MoveDividerT (2.24). seam is one of the list's (in
 * g_sl_seam[] or g_sl_one), and its limits are narrowed here. Only its
 * windows move: each keeps its far edge, and the seam its gap. On
 * ES_OK *applied (applied may be NULL) receives the position, clamped
 * to the limits, that the windows were asked to take.
 */
static LONG esb_seam_move(ESSeam *seam, LONG position, int may_raise,
                          LONG *applied)
{
    struct ESBGroup *g = NULL;
    int na = seam->a.n, n = seam->a.n + seam->b.n;
    int pos, i, j;
    LONG rc;

    rc = esb_seam_revalidate(seam);
    if (rc != ES_OK) {
        return rc;
    }
    /* the range the windows can take right now, not the fixed floor */
    esb_seam_tighten_live(seam);
    if (seam->min_pos > seam->max_pos) {
        return ES_ERR_UNSUPPORTED;      /* nothing on it can give */
    }
    if (position < seam->min_pos) {
        pos = seam->min_pos;
    } else if (position > seam->max_pos) {
        pos = seam->max_pos;
    } else {
        pos = (int)position;
    }
    es_gutter_apply(seam, pos, &g_sl_to[0], &g_sl_to[na]);

    if (seam->key != 0) {
        g = esb_group_by_id((ULONG)seam->key);
    }
    for (i = 0; i < n; i++) {
        struct Window *w = (struct Window *)(i < na ? seam->a.ref[i]
                                                    : seam->b.ref[i - na]);
        const struct ESBSeamWin *sw = &g_sl_win[esb_sl_index(w)];
        int zone = es_registry_zone(&g_registry, w);

        /* the registry must follow, or the next drag would start from
         * the geometry the windows had before this one; only where it
         * already holds the window, as a placement does: a window
         * placed without a slot of its own gets none here */
        if (zone != ES_ZONE_NONE) {
            es_registry_remember(&g_registry, w, &sw->live, &g_sl_to[i],
                                 zone);
        }
        /* and so must the group, which finds its seams from there */
        if (g != NULL && sw->member >= 0 && sw->member < g->members) {
            esb_member_put(g, sw->member, &sw->live, &g_sl_to[i]);
        }
        g_sl_delta[i] = (long)g_sl_to[i].w * g_sl_to[i].h -
                        (long)sw->live.w * sw->live.h;
    }

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
    es_order_by_growth(g_sl_delta, n, g_sl_order);
    for (j = 0; j < n; j++) {
        struct Window *w;

        i = g_sl_order[j];
        w = (struct Window *)(i < na ? seam->a.ref[i] : seam->b.ref[i - na]);
        esb_change_box(w, &g_sl_win[esb_sl_index(w)].live, &g_sl_to[i],
                       may_raise);
    }
    if (applied != NULL) {
        *applied = pos;
    }
    return ES_OK;
}

/* ------------------------------------------ dividers of a group (2.24) */

/*
 * A tiler's own seams, by token (core/divtokens.h): QueryDividers lists
 * the seams of a group with a token each, MoveDividerT moves the seam a
 * token names, QueryDividerWindows says which windows those are. A
 * token names the windows on each side by member index, and the seam
 * is rebuilt from exactly those windows with es_seam_by_refs, so it is
 * that seam or none. The table is the group's; the counter is one for
 * every group, never reset while the library is loaded, so a token
 * value is never valid twice (the counter turns round after 0xFFFFFFFF
 * tokens, more than a session hands out, and that is left alone).
 */
static unsigned char g_sl_rank[ESB_SEAM_RESULTS];   /* QueryDividers' order */
static unsigned long g_sl_token[ESB_SEAM_RESULTS];
static ESDivSet g_sl_set[ESB_SEAM_RESULTS];
static unsigned char g_sl_claimed[ES_DIVTOK_SLOTS];
static ESSeamSide g_sl_side;                        /* one side, to order */
static unsigned char g_sl_side_rank[ES_SEAM_SIDE_MAX];
static unsigned char g_sl_bits[ES_SEAM_SIDE_MAX];
static unsigned char g_sl_mem[2][ES_SEAM_SIDE_MAX]; /* a token's members  */
static int g_sl_mem_n[2];
static unsigned long g_token_next = 1;

/* g_sem held, the list of a group built: the seam's windows as member
 * masks; a set no token can name (a window outside the group) when one
 * of them has no member index. */
static void esb_seam_set(const ESSeam *seam, ESDivSet *set)
{
    int s, i, k;

    set->vertical = seam->vertical != 0;
    set->a = 0;
    set->b = 0;
    for (s = 0; s < 2; s++) {
        const ESSeamSide *side = s ? &seam->b : &seam->a;

        for (i = 0; i < side->n; i++) {
            k = esb_sl_index(side->ref[i]);
            if (k < 0 || g_sl_win[k].member < 0) {
                set->a = 0;
                set->b = 0;
                return;
            }
            if (s) {
                set->b |= ES_DIVTOK_BIT(g_sl_win[k].member);
            } else {
                set->a |= ES_DIVTOK_BIT(g_sl_win[k].member);
            }
        }
    }
}

/* g_sem held, the list built: the serial of the first window of a side
 * in the order a tiler is told (top-most along a vertical seam,
 * left-most along a horizontal one); 0 for none. */
static ULONG esb_side_first(const ESSeamSide *side, int vertical)
{
    int k;

    if (side->n < 1) {
        return 0;
    }
    es_divider_side_order(side, vertical, g_sl_side_rank);
    k = esb_sl_index(side->ref[g_sl_side_rank[0]]);
    return k >= 0 ? g_sl_win[k].serial : 0;
}

/*
 * g_sem held: the members token t of g names, each side in the order a
 * tiler is told (es_divider_side_order on the boxes EdgeSnap put them
 * in), into g_sl_mem[][]. 0 when it names a member the group does not
 * have, or no window on a side: no seam of the group.
 */
static int esb_token_members(const struct ESBGroup *g, const ESDivToken *t)
{
    int s, k, i;

    for (s = 0; s < 2; s++) {
        unsigned long mask = s ? t->b : t->a;

        g_sl_side.n = 0;
        for (k = 0; k < ES_DIVTOK_MEMBERS && mask != 0; k++) {
            if ((mask & ES_DIVTOK_BIT(k)) == 0) {
                continue;
            }
            mask &= ~ES_DIVTOK_BIT(k);
            if (k >= g->members) {
                return 0;
            }
            g_sl_bits[g_sl_side.n] = (unsigned char)k;
            g_sl_side.box[g_sl_side.n] = g->placed[k];
            g_sl_side.n++;
        }
        if (mask != 0 || g_sl_side.n == 0) {
            return 0;
        }
        es_divider_side_order(&g_sl_side, t->vertical, g_sl_side_rank);
        for (i = 0; i < g_sl_side.n; i++) {
            g_sl_mem[s][i] = g_sl_bits[g_sl_side_rank[i]];
        }
        g_sl_mem_n[s] = g_sl_side.n;
    }
    return 1;
}

/* g_sem held, the list of the group built: the windows of g_sl_mem[][]
 * as refs, into g_sl_ref[][]. 0 when one is not on the list (excluded
 * by now, or no longer one that can be snapped). */
static int esb_token_refs(void)
{
    int s, i, k;

    for (s = 0; s < 2; s++) {
        for (i = 0; i < g_sl_mem_n[s]; i++) {
            for (k = 0; k < g_sl_n && g_sl_win[k].member != g_sl_mem[s][i];
                 k++) {
                ;
            }
            if (k == g_sl_n) {
                return 0;
            }
            g_sl_ref[s][i] = g_sl_box[k].ref;
        }
    }
    return 1;
}

/* g_sem held. The group whose table holds token, and its slot there;
 * NULL when no group holds it. */
static struct ESBGroup *esb_group_of_token(ULONG token, int *slot)
{
    int i;

    for (i = 0; i < ESB_GROUP_SLOTS; i++) {
        if (g_groups[i].id != 0 &&
            (*slot = es_divtok_find(&g_groups[i].tok, token)) >= 0) {
            return &g_groups[i];
        }
    }
    *slot = -1;
    return NULL;
}

/*
 * g_sem held, the list of g built: are the windows of token t still a
 * seam of the group? Not when one of them is not on the list (excluded
 * by now, or no longer one that can be snapped) or es_seam_by_refs says
 * 0; -1 is a seam with no room, and still is one. Its members are then
 * in g_sl_mem[][] and their refs in g_sl_ref[][].
 */
static int esb_token_holds(const struct ESBGroup *g, const ESDivToken *t)
{
    return esb_token_members(g, t) && esb_token_refs() &&
           es_seam_by_refs(g_sl_box, g_sl_n, ESB_SEAM_LINE_PX, t->vertical,
                           g_sl_ref[0], g_sl_mem_n[0], g_sl_ref[1],
                           g_sl_mem_n[1], NULL) != 0;
}

/*
 * g_sem held, the list of g built, pass 1 of core/divtokens.h done:
 * pass 2. A token no seam of the query claimed retires once its windows
 * are no longer a seam of the group.
 */
static void esb_tokens_retire(struct ESBGroup *g)
{
    int j;

    for (j = 0; j < ES_DIVTOK_SLOTS; j++) {
        const ESDivToken *t = &g->tok.slot[j];

        if (t->token != 0 && !g_sl_claimed[j] && !esb_token_holds(g, t)) {
            es_divtok_retire(&g->tok, j);
        }
    }
}

/* g_sem held, the list of g built: one seam as a tiler is told it. */
static void esb_divider_info(const struct ESBGroup *g, const ESSeam *seam,
                             unsigned long token,
                             struct ESnapDividerInfo *o)
{
    o->token = (ULONG)token;
    o->flags = 0;
    if (esb_group_locked_for(g)) {
        o->flags |= ES_DIF_LOCKED;
    }
    if (!esb_seam_alive(seam)) {
        o->flags |= ES_DIF_MOVED;
    }
    o->vertical = seam->vertical ? 1 : 0;
    o->position = seam->pos;
    if (seam->min_pos > seam->max_pos) {
        o->flags |= ES_DIF_STUCK;
        o->minPosition = seam->pos;
        o->maxPosition = seam->pos;
    } else {
        o->minPosition = seam->min_pos;
        o->maxPosition = seam->max_pos;
    }
    o->from = seam->vertical ? seam->rect.y : seam->rect.x;
    o->to = o->from + (seam->vertical ? seam->rect.h : seam->rect.w);
    o->gap = seam->gap;
    o->countA = (ULONG)seam->a.n;
    o->countB = (ULONG)seam->b.n;
    o->serialA = esb_side_first(&seam->a, seam->vertical);
    o->serialB = esb_side_first(&seam->b, seam->vertical);
    o->reserved[0] = 0;
    o->reserved[1] = 0;
    o->reserved[2] = 0;
}

LONG esb_query_dividers(ULONG group, struct ESnapDividerInfo *buf,
                        ULONG count, ULONG *needed)
{
    struct ESBGroup *g;
    LONG rc = ES_OK;
    int m, i;

    if (needed == NULL || (buf == NULL && count != 0)) {
        return ES_ERR_BAD_ARGS;
    }
    *needed = 0;
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    esb_clients_sweep();           /* a tiler that is gone took its groups */
    g = esb_group_by_id(group);
    if (g == NULL) {
        rc = ES_ERR_STALE;
    } else {
        esb_seam_list(g);
        m = esb_seams_found(ESB_SEAM_LINE_PX);
        for (i = 0; i < m; i++) {
            esb_seam_tighten_live(&g_sl_seam[i]);
            esb_seam_set(&g_sl_seam[i], &g_sl_set[i]);
        }
        /* reuse every token, then retire, then mint: no seam takes the
         * slot of a token another one of this query keeps, and stale
         * tokens leave before a live one is evicted */
        es_divtok_reuse(&g->tok, g_sl_set, m, g_sl_token, g_sl_claimed);
        esb_tokens_retire(g);
        es_divtok_mint(&g->tok, g_sl_set, m, g_sl_token, g_sl_claimed,
                       &g_token_next);
        es_divider_order(g_sl_seam, m, g_sl_rank);
        for (i = 0; i < m && (ULONG)i < count; i++) {
            esb_divider_info(g, &g_sl_seam[g_sl_rank[i]],
                             g_sl_token[g_sl_rank[i]], &buf[i]);
        }
        *needed = (ULONG)m;
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_move_divider_t(ULONG token, LONG position, ULONG flags,
                        LONG *applied)
{
    struct ESBGroup *g;
    LONG rc, pos = 0;
    int slot, i, k, r;

    if (token == 0 || (flags & ~ES_PF_KEEP_ZORDER) != 0) {
        return ES_ERR_BAD_ARGS;
    }
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    esb_clients_sweep();
    g = esb_group_of_token(token, &slot);
    if (g == NULL) {
        rc = ES_ERR_STALE;
    } else if (esb_group_locked_for(g)) {
        rc = ES_ERR_REJECTED;      /* before anything that could retire it */
    } else {
        esb_seam_list(g);          /* a member that left took its tokens */
        slot = es_divtok_find(&g->tok, token);
        if (slot < 0) {
            rc = ES_ERR_STALE;
        } else if (!esb_token_members(g, &g->tok.slot[slot]) ||
                   !esb_token_refs()) {
            es_divtok_retire(&g->tok, slot);
            rc = ES_ERR_STALE;
        } else {
            rc = ES_OK;
            for (i = 0; i < g_sl_mem_n[0] + g_sl_mem_n[1]; i++) {
                k = esb_sl_index(i < g_sl_mem_n[0]
                                 ? g_sl_ref[0][i]
                                 : g_sl_ref[1][i - g_sl_mem_n[0]]);
                if (g_sl_win[k].moved) {
                    rc = ES_ERR_CHANGED;   /* the token stays */
                }
            }
            if (rc == ES_OK) {
                r = es_seam_by_refs(g_sl_box, g_sl_n, ESB_SEAM_LINE_PX,
                                    g->tok.slot[slot].vertical,
                                    g_sl_ref[0], g_sl_mem_n[0],
                                    g_sl_ref[1], g_sl_mem_n[1], &g_sl_one);
                if (r == 0) {
                    es_divtok_retire(&g->tok, slot);
                    rc = ES_ERR_STALE;
                } else if (r < 0) {
                    rc = ES_ERR_UNSUPPORTED;    /* no room: the token stays */
                } else {
                    rc = esb_seam_move(&g_sl_one, position,
                                       (flags & ES_PF_KEEP_ZORDER) == 0, &pos);
                    if (rc == ES_ERR_STALE) {
                        es_divtok_retire(&g->tok, slot);
                    } else if (rc == ES_OK && applied != NULL) {
                        *applied = pos;
                    }
                }
            }
        }
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}

LONG esb_query_divider_windows(ULONG token, ULONG *serials, ULONG count,
                               ULONG *needed)
{
    struct ESBGroup *g;
    LONG rc = ES_OK;
    int slot, s, i, n = 0;

    if (needed == NULL || (serials == NULL && count != 0) || token == 0) {
        return ES_ERR_BAD_ARGS;
    }
    *needed = 0;
    if (!g_ready) {
        return ES_ERR_UNSUPPORTED;
    }
    ObtainSemaphore(&g_sem);
    esb_clients_sweep();
    g = esb_group_of_token(token, &slot);
    if (g != NULL) {
        esb_seam_list(g);          /* a member that left took its tokens */
        slot = es_divtok_find(&g->tok, token);
    }
    if (g == NULL || slot < 0) {
        rc = ES_ERR_STALE;
    } else if (!esb_token_holds(g, &g->tok.slot[slot])) {
        /* not that seam any more: retired here too, as MoveDividerT
         * and QueryDividers retire it */
        es_divtok_retire(&g->tok, slot);
        rc = ES_ERR_STALE;
    } else {
        for (s = 0; s < 2; s++) {
            for (i = 0; i < g_sl_mem_n[s]; i++, n++) {
                if ((ULONG)n < count) {
                    serials[n] = g->member[g_sl_mem[s][i]];
                }
            }
        }
        *needed = (ULONG)n;
    }
    ReleaseSemaphore(&g_sem);
    return rc;
}
