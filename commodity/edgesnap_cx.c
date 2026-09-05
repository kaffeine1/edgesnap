/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_cx.c - the EdgeSnap reference commodity (phase 2).
 *
 * One source for AmigaOS 4.x and MorphOS. ALL drag/snap decisions live
 * in the portable core (core/engine.c state machine,
 * core/registry.c stale-resistant restore): this file is glue, and must
 * never grow a second snapping engine (docs/LLM_GUIDANCE.md). It:
 *
 *   - watches raw mouse events from a commodities CxCustom object
 *     (input.device context: counters + Signal() only);
 *   - feeds the core instantaneous window facts sampled under
 *     LockIBase (usable area is dock/panel aware via core/panels.c);
 *   - executes the core's emitted actions: zone preview (MorphOS:
 *     borderless frame windows; OS4: XOR rectangles on the screen
 *     rastport - OpenWindow during an OS4 ghost-drag is unreliable),
 *     and snaps via ChangeWindowBox with re-validation of possibly
 *     stale window refs;
 *   - provides hotkeys through the same snap path the future
 *     ESnap_SnapWindow() will use, Exchange integration, and a
 *     window-dump diagnostic (ctrl alt d).
 *
 * Field-proven rules encoded here (see docs/DESIGN.md spike findings):
 * the engine task never blocks on console I/O (buffered log, flushed
 * only when no drag is in flight), and everything Intuition-facing
 * runs in this task, never in the input handler.
 */

#ifdef __amigaos4__
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <exec/types.h>
#include <exec/ports.h>
#include <exec/tasks.h>
#include <dos/dos.h>       /* RETURN_*, SIGBREAKF_CTRL_C                */
#include <dos/notify.h>    /* StartNotify: prefs that change while we run */
#include <devices/inputevent.h>
#include <libraries/commodities.h>
#include <intuition/intuition.h>
#include <intuition/pointerclass.h>   /* POINTERTYPE_* for the seam    */
#include <graphics/layers.h>          /* LAYERS_NOBACKFILL             */
#include <intuition/intuitionbase.h>
#include <intuition/screens.h> /* DrawInfo pens for the preview frame */
#ifdef __amigaos4__
#endif
#include <graphics/view.h>     /* OBP_Precision/PRECISION_GUI          */

#include <workbench/startup.h>
#include <workbench/workbench.h>

#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/icon.h>
#include <proto/intuition.h>
#include <proto/graphics.h>
#include <proto/commodities.h>

#ifdef __MORPHOS__
#include <emul/emulregs.h>
#include <emul/emulinterface.h>
#endif

#include "zones.h"
#include "panels.h"
#include "engine.h"
#include "registry.h"
#include "config.h"
#include "edgesnap_version.h"

/*
 * Which preview frame. Two techniques exist because two families of
 * Intuition exist. Where a window drag keeps the screen's layers
 * locked until release - AmigaOS 4's ghost drag, and AROS's outline
 * drag - a window opened during the drag only appears at release,
 * when we are already closing it: the frame is drawn straight on the
 * screen's RastPort over saved pixels. MorphOS moves windows solid and
 * lets us open real borderless windows, which look after themselves.
 */
#if defined(__amigaos4__)
#define ES_PREVIEW_PIXELS 1
#elif defined(__AROS__)
/*
 * AROS drags a window as an outline drawn in COMPLEMENT mode on the
 * screen (rom/intuition/windowclasses.c), unless IControl's opaque
 * move is on. A frame painted in a solid colour and that outline
 * erase each other wherever they cross. So the frame is an XOR too,
 * and composes with the outline in any order, but the mask is made
 * per pixel as "what is there" XOR "the accent": the first pass turns
 * every pixel of the strips into the accent colour, the second pass
 * with the same mask turns it back. The pixels are read and written
 * through cybergraphics, in the CGX argument order: destination
 * first, the RastPort in the middle, the format last. Reads on AROS
 * go through the software cursor layer (rom/graphics/fakegfxhidd.c),
 * so the mask never contains the pointer.
 */
#define ES_PREVIEW_XOR 1
/*
 * The seam handle leaves for the duration of a seam drag. Elsewhere it
 * is parked as an 8x8 speck under the pointer and follows it; AROS
 * keeps the handle at its full height instead, and every step of the
 * handle uncovers a strip of the window below that AROS backfills in
 * pen 0 and the console never repaints: a trail of black bars across
 * the window, which is what testers reported as "black artifacts".
 * With no window in play the drag is followed through the input
 * counters and the handle comes back on release, where the seam is.
 */
#define ES_SEAM_DRAG_BLIND 1
#include <cybergraphx/cybergraphics.h>
#include <proto/cybergraphics.h>
struct Library *CyberGfxBase;
#define ES_PF_FORMAT RECTFMT_ARGB
#define ES_PF_READ(rp, x, y, buf, mod, w, h) \
    ReadPixelArray((buf), 0, 0, (UWORD)(mod), (rp), (UWORD)(x), (UWORD)(y), \
                   (UWORD)(w), (UWORD)(h), ES_PF_FORMAT)
#define ES_PF_WRITE(buf, mod, rp, x, y, w, h) \
    WritePixelArray((buf), 0, 0, (UWORD)(mod), (rp), (UWORD)(x), (UWORD)(y), \
                    (UWORD)(w), (UWORD)(h), ES_PF_FORMAT)
static void spike_pf_drop_now(void);    /* from the input handler */
#else
#define ES_PREVIEW_WINDOWS 1
#endif

/*
 * The pixel array calls in graphics.library's dialect. Should the
 * pixel technique ever serve a CGX system, remember that cybergraphics
 * takes the arguments in another order: destination first, the
 * RastPort in the middle, the format last.
 */
#ifdef ES_PREVIEW_PIXELS
#define ES_PF_FORMAT PIXF_A8R8G8B8
#define ES_PF_READ(rp, x, y, buf, mod, w, h) \
    ReadPixelArray((rp), (ULONG)(x), (ULONG)(y), (buf), 0, 0, (ULONG)(mod), \
                   ES_PF_FORMAT, (ULONG)(w), (ULONG)(h))
#define ES_PF_WRITE(buf, mod, rp, x, y, w, h) \
    WritePixelArray((buf), 0, 0, (ULONG)(mod), ES_PF_FORMAT, (rp), \
                    (x), (y), (ULONG)(w), (ULONG)(h))
#endif
#include "edgesnap.h"

/*
 * Phase 4d: the commodity is a CLIENT of edgesnap.library, not a
 * program that contains it. The two systems reach the same API in
 * their own way - OS4 through an interface (Self is implicit, the
 * SDK's APICALL is __attribute__((libcall))), MorphOS through the
 * ppcinline stubs over the jump table - so every call is written once
 * as ES_CALL(name)(args).
 */
#if defined(ES_STATIC_CORE)
/* The body is linked in and called directly: no library to open. */
#include "edgesnap_static.h"
#define ES_CALL(fn) fn
#elif defined(__AROS__)
/*
 * AROS: the client headers are generated from library/aros/edgesnap.conf
 * by genmodule. The macro flavour is asked for, not the inline one: the
 * prototypes above are the public ones from edgesnap.h, and a static
 * inline of the same name after them would be a second declaration.
 */
#define EDGESNAP_NOLIBINLINE 1
#include <proto/edgesnap.h>
struct Library *EdgeSnapBase;
#define ES_CALL(fn) fn
#elif defined(__amigaos4__)
#include "interfaces/edgesnap.h"
static struct Library *EdgeSnapBase;
static struct EdgeSnapIFace *IEdgeSnap;
#define ES_CALL(fn) IEdgeSnap->fn
#else
#include <ppcinline/edgesnap.h>
struct Library *EdgeSnapBase;
#define ES_CALL(fn) fn
#endif

/* ---------------------------------------------------------------- tuning */

/* Drag/zone tuning lives in the core (ESEngineConfig defaults). */
#define ES_FRAME_PX        4   /* preview frame thickness                  */
#define ES_DIVIDER_PX     10   /* thickness of the divider handle          */

/* --------------------------------------------------------- library bases */

/* OS4's proto/intuition.h predeclares IntuitionBase as struct Library *;
 * classic/MorphOS code wants struct IntuitionBase *. Match the SDK and go
 * through a typed alias for structure access. */
#ifdef __amigaos4__
struct Library *IntuitionBase;
#else
struct IntuitionBase *IntuitionBase;
#endif
#ifdef __AROS__
#endif
#define ES_IBASE ((struct IntuitionBase *)IntuitionBase)

/* Same story for GfxBase (needed by the preview frame's RectFill). */
#ifdef __amigaos4__
struct Library *GfxBase;
#else
struct GfxBase *GfxBase;
#endif

struct Library *CxBase;

#ifdef __amigaos4__
struct IntuitionIFace *IIntuition;
struct GraphicsIFace *IGraphics;
struct CommoditiesIFace *ICommodities;
#endif

#ifdef __MORPHOS__
/* PPC task stack (the shell Stack command only sizes the 68k stack). */
unsigned long __stack = 65536UL;
#endif

/* AmigaDOS stack cookie for lanes that honor it, and the version
 * string the Version command reads - which is where a user on the
 * target system finds out whose software this is. */
/* Arrays, not pointers, and marked used: a cookie only works if the
 * literal actually survives into the binary for Version/the loader to
 * find. As pointers the compiler was free to drop them - and did. */
static const char es_stack_cookie[] __attribute__((used)) =
    "$STACK:65536";
static const char es_version_cookie[] __attribute__((used)) =
    "$VER: EdgeSnap " ES_VERSION " (" ES_VERSION_DATE ") Michele Dipace";

/* ----------------------------------------- input handler <-> main task */

/*
 * Written by the CxCustom action in input.device context, read by the main
 * task. Counters (not flags) so fast press+release pairs are not lost.
 */
struct SpikeShared {
    /*
     * Written by us, read by the input handler. engine_task is cleared
     * BEFORE the commodity tree is torn down (see the shutdown
     * protocol in main): an action already in flight must never signal
     * a task that is about to die.
     */
    struct Task * volatile engine_task;
    volatile ULONG armed;   /* 0 = the action must do nothing at all */
    ULONG engine_sigmask;
    volatile ULONG presses;
    /* diagnostics: how often the action ran, and which input classes
     * it saw (index = ie_Class & 15); shown by the ctrl alt d dump */
    volatile ULONG diag_calls;
    volatile ULONG diag_class[16];
    volatile ULONG releases;
    volatile ULONG moves;
    volatile ULONG quals;   /* latest qualifier bits seen with the mouse */
};

static struct SpikeShared g_shared;

/*
 * CxCustom action. RULES: no Intuition calls, no allocations, no waiting.
 * Bump a counter, Signal() the main task, get out.
 */
static void spike_cx_action(CxMsg *msg, CxObj *obj)
{
    struct InputEvent *ie;
    struct Task *engine;

    (void)obj;
    if (!g_shared.armed) {
        return; /* shutting down: touch nothing */
    }
    g_shared.diag_calls++;
    if (CxMsgType(msg) != CXM_IEVENT) {
        return;
    }
    ie = (struct InputEvent *)CxMsgData(msg);
    if (ie == NULL) {
        return;
    }
    /* Diagnostics only: which classes reach us. Written so that a port
     * where the mouse speaks another dialect (AROS with a USB tablet,
     * 2026-09-03) shows its hand in the ctrl alt d dump instead of
     * leaving 'drag not detected' to be guessed at. */
    g_shared.diag_class[ie->ie_Class & 15]++;
    /*
     * Motion is not always RAWMOUSE. A USB tablet on AROS reports every
     * move as IECLASS_NEWPOINTERPOS and only the buttons as RAWMOUSE
     * (measured 2026-09-03: 461 pointer events to 2 raw ones in one
     * drag), so a handler that counts moves on RAWMOUSE alone never
     * sees the drag. The engine reads the pointer from the screen, not
     * from the event, so 'the pointer moved' is all we need to know.
     */
    if (ie->ie_Class == IECLASS_NEWPOINTERPOS ||
        ie->ie_Class == IECLASS_POINTERPOS) {
        g_shared.quals = ie->ie_Qualifier;
        g_shared.moves++;
        engine = g_shared.engine_task;
        if (engine != NULL && g_shared.armed) {
            Signal(engine, g_shared.engine_sigmask);
        }
        return;
    }
    if (ie->ie_Class != IECLASS_RAWMOUSE) {
        return;
    }
    g_shared.quals = ie->ie_Qualifier;
    if (ie->ie_Code == IECODE_LBUTTON) {
        g_shared.presses++;
    } else if (ie->ie_Code == (IECODE_LBUTTON | IECODE_UP_PREFIX)) {
#ifdef ES_PREVIEW_XOR
        /*
         * Ahead of Intuition, which sits lower in the chain and moves
         * the window on this very event: a frame taken down after that
         * inverts pixels the move has just repainted. Nothing here but
         * the four rectangle fills Intuition does from this context.
         */
        spike_pf_drop_now();
#endif
        g_shared.releases++;
    } else if (ie->ie_Code == IECODE_NOBUTTON) {
        g_shared.moves++;
    } else {
        return;
    }
    /* one read: our task may be clearing this pointer right now */
    engine = g_shared.engine_task;
    if (engine != NULL && g_shared.armed) {
        Signal(engine, g_shared.engine_sigmask);
    }
}

#ifdef __MORPHOS__
/*
 * MorphOS calls commodities custom actions through the 68k ABI (CxMsg in
 * a0, CxObj in a1), so native PPC code needs an EmulLibEntry gate.
 */
static void spike_cx_action_gate(void)
{
    CxMsg *msg = (CxMsg *)REG_A0;
    CxObj *obj = (CxObj *)REG_A1;

    spike_cx_action(msg, obj);
}

static struct EmulLibEntry spike_cx_trap = {
    TRAP_LIB, 0, (void (*)(void))spike_cx_action_gate
};

#define ES_CX_ACTION ((APTR)&spike_cx_trap)
#else
#define ES_CX_ACTION ((APTR)spike_cx_action)
#endif

/*
 * Started from Workbench (from WBStartup, which is the point of this
 * program) there is no console at all. Everything the user is meant to
 * read goes through spike_out(), which falls silent then - the
 * commodity's face is Exchange, not a shell.
 */
static int g_quit_mode;   /* QUIT argument: stop a running instance */

/*
 * How QUIT reaches the instance that is already running. Commodities
 * only get told THAT another instance tried to start (CXCMD_UNIQUE),
 * never why - and "any second launch stops the first" is too sharp an
 * edge: a stray Run in User-Startup, or a second double-click, would
 * silently disable EdgeSnap. So the asking instance leaves a note in
 * ENV: first, and the running one quits only if it finds it.
 */
#define ES_QUIT_VAR "EdgeSnap.quit"

static int spike_quit_requested(void)
{
    char buf[8];

    if (GetVar((STRPTR)ES_QUIT_VAR, (STRPTR)buf, (LONG)sizeof(buf),
               GVF_GLOBAL_ONLY) < 0) {
        return 0;
    }
    DeleteVar((STRPTR)ES_QUIT_VAR, GVF_GLOBAL_ONLY);
    return 1;
}
static int g_quiet;

/* --------------------------------------------------- non-blocking log */

/*
 * Field lesson (OS4, 2026-08-26): the engine once printf'd straight to
 * its shell console. Dragging THAT shell window freezes console output
 * on OS4, so the first zone print blocked the engine for the rest of
 * the drag: zones went stale and the preview never opened. Engine-path
 * messages land here and reach the console only when no drag is in
 * flight. The library phase inherits the rule: the engine never blocks
 * on I/O.
 */

/*
 * Second lesson, paid for a year's worth of confusion in one day
 * (2026-09-01): a log that floods DESTROYS the evidence. A diagnostic
 * line left in the divider path filled these 64 slots, so the lines
 * that mattered - "drag detected", "zone -> right", "snap ..." - were
 * dropped, and a field video read as "the engine never sees the drag"
 * when the engine was working perfectly. A whole investigation went
 * after dock detection because of it.
 *
 * The rule that follows: nothing on a path that repeats logs by
 * default. If a line cannot justify appearing once per mouse release,
 * it does not belong here at all.
 */

#define ES_LOG_LINES 64
#define ES_LOG_CHARS 160

static char g_log_buf[ES_LOG_LINES][ES_LOG_CHARS];
static int g_log_count;
static int g_log_dropped;

static void spike_log(const char *fmt, ...)
{
    va_list ap;

    if (g_log_count >= ES_LOG_LINES) {
        g_log_dropped++;
        return;
    }
    va_start(ap, fmt);
    vsprintf(g_log_buf[g_log_count], fmt, ap); /* short, fixed formats */
    va_end(ap);
    g_log_count++;
}

static void spike_log_flush(void)
{
    int i;

    if (!g_quiet) {
        for (i = 0; i < g_log_count; i++) {
            fputs(g_log_buf[i], stdout);
        }
        if (g_log_dropped > 0) {
            printf("edgesnap: (%d log lines dropped)\n", g_log_dropped);
        }
        if (g_log_count > 0 || g_log_dropped > 0) {
            fflush(stdout);
        }
    }
    g_log_count = 0;
    g_log_dropped = 0;
}

/* ---------------------------------------------------------- preferences */

/*
 * Preferences live in the portable core (core/config.c): one KEY=VALUE
 * parser serves both sources, because on Amiga they have the same
 * shape - lines of an ENV(ARC): file and, later, Workbench tooltypes.
 * This glue only reads bytes and reports what the parser rejected; a
 * bad line never aborts the load, so a truncated prefs file cannot
 * leave the user without snapping.
 */

static void spike_out(const char *fmt, ...)
{
    va_list ap;

    if (g_quiet) {
        return;
    }
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

#define ES_PREFS_ENV    "ENV:EdgeSnap.prefs"
#define ES_PREFS_ENVARC "ENVARC:EdgeSnap.prefs"

static ESConfig g_cfg;

/*
 * Kept so the settings can be loaded again while running: a
 * preferences window writes ENV:EdgeSnap.prefs and we notice, but
 * whatever the user asked for on the command line at startup still
 * has to win afterwards, exactly as it did the first time.
 */
static int g_argc;
static char **g_argv;

static void spike_config_line(const char *line, const char *src)
{
    int rc = es_config_line(&g_cfg, line);

    if (rc == ES_ERR_UNSUPPORTED) {
        spike_out("edgesnap: %s: unknown setting: %s\n", src, line);
    } else if (rc != ES_OK) {
        spike_out("edgesnap: %s: bad value: %s\n", src, line);
    }
}

static int spike_config_load_file(const char *path)
{
    BPTR fh;
    char line[160];

    fh = Open((STRPTR)path, MODE_OLDFILE);
    if (!fh) {
        return 0;
    }
    while (FGets(fh, (STRPTR)line, (unsigned long)sizeof(line)) != 0) {
        spike_config_line(line, path);
    }
    Close(fh);
    return 1;
}

/*
 * Started from Workbench - which is how it runs when installed in
 * WBStartup - the settings come from the icon's tooltypes. They are
 * KEY=VALUE like everything else, so the same parser reads them; an
 * entry in parentheses is disabled by Amiga convention and skipped,
 * and DONOTWAIT belongs to Workbench, not to us.
 */
static void spike_config_tooltypes(struct WBStartup *wbs)
{
    struct Library *IconBase;
    struct DiskObject *dob;
    struct WBArg *arg;
    BPTR olddir;
    STRPTR *tt;
#ifdef __amigaos4__
    struct IconIFace *IIcon;
#endif

    if (wbs == NULL || wbs->sm_NumArgs < 1 || wbs->sm_ArgList == NULL) {
        return;
    }
    IconBase = OpenLibrary("icon.library", 36);
    if (IconBase == NULL) {
        return;
    }
#ifdef __amigaos4__
    IIcon = (struct IconIFace *)GetInterface(IconBase, "main", 1, NULL);
    if (IIcon == NULL) {
        CloseLibrary(IconBase);
        return;
    }
#endif
    arg = &wbs->sm_ArgList[0];
#ifdef __amigaos4__
    olddir = SetCurrentDir(arg->wa_Lock);
#else
    olddir = CurrentDir(arg->wa_Lock);
#endif
    dob = GetDiskObject((STRPTR)arg->wa_Name);
    if (dob != NULL) {
        for (tt = dob->do_ToolTypes; tt != NULL && *tt != NULL; tt++) {
            const char *entry = (const char *)*tt;

            if (entry[0] == '\0' || entry[0] == '(') {
                continue; /* disabled by convention */
            }
            if (entry[0] == 'D' && entry[1] == 'O' && entry[2] == 'N') {
                continue; /* DONOTWAIT is Workbench's, not ours */
            }
            spike_config_line(entry, "tooltype");
        }
        FreeDiskObject(dob);
    }
#ifdef __amigaos4__
    SetCurrentDir(olddir);
#else
    CurrentDir(olddir);
#endif
#ifdef __amigaos4__
    DropInterface((struct Interface *)IIcon);
#endif
    CloseLibrary(IconBase);
}

/*
 * ENV: holds the live settings, ENVARC: the archived ones: prefer the
 * live copy, like every other Amiga preferences client. What the user
 * asked for THIS launch - Shell arguments, or the icon's tooltypes -
 * wins over the file.
 */
static int spike_arg_is(const char *arg, const char *word)
{
    int i;

    for (i = 0; word[i] != '\0'; i++) {
        char c = arg[i];

        if (c >= 'a' && c <= 'z') {
            c = (char)(c - 'a' + 'A');
        }
        if (c != word[i]) {
            return 0;
        }
    }
    return arg[i] == '\0';
}

static void spike_config_load(int argc, char **argv)
{
    int i;

    g_argc = argc;
    g_argv = argv;

    es_config_defaults(&g_cfg);
    if (!spike_config_load_file(ES_PREFS_ENV)) {
        spike_config_load_file(ES_PREFS_ENVARC);
    }
    if (argc == 0) {
        /* Workbench start: argv is the WBStartup message, and there is
         * no console to talk to. */
        g_quiet = 1;
        spike_config_tooltypes((struct WBStartup *)argv);
        return;
    }
    for (i = 1; i < argc; i++) {
        /* QUIT is a verb, not a setting: it asks a running EdgeSnap to
         * stop and starts nothing. It is what an installer runs before
         * replacing the files, so that the old library can be flushed
         * out of memory without the user rebooting. */
        if (spike_arg_is(argv[i], "QUIT")) {
            g_quit_mode = 1;
            continue;
        }
        spike_config_line(argv[i], "argument");
    }
}

/* ------------------------------------------- library-side declarations */

/* The preview frame must not be mistaken for a dock by the panel scan
 * (it is thin, edge-flush and long - exactly a dock's shape), so the
 * frontend hands its frame windows to the library; defined with the
 * preview below. */
static int spike_is_preview_win(struct Window *w);
static void spike_publish_ignored(void);

/* -------------------------------------------------------- zone preview */

/*
 * Aero/macOS-style "where will it land" feedback while dragging: a frame
 * of four thin borderless windows outlining the target rectangle.
 *
 * Why not a translucent window: WA_Opaqueness needs compositing, which the
 * QEMU sm501 has no chance of providing, and the MorphOS equivalent tag
 * differs. Four 4px bars work on any screen of both OSes, like the classic
 * outline drag of old. The translucent fill can become a compositing-only
 * upgrade later.
 *
 * The bars open non-activating and with no IDCMP, so they disturb neither
 * Intuition's drag nor our ActiveWindow-based heuristic; zone changes just
 * close and reopen them (cheap, and avoids async-resize repaint races).
 * With solid window dragging (the default on both OSes) opening windows
 * mid-drag is fine; with outline drag mode OpenWindow would block until
 * release, so the preview simply would not show - no harm.
 */

/*
 * One accent colour for everything EdgeSnap draws - the preview frame
 * and the seam - obtained once, at startup, and kept.
 *
 * Not per drawing: ObtainBestPen fails while Intuition is dragging a
 * window, and the fallback is FILLPEN, a grey almost identical to the
 * Workbench background. That is how the preview frame came to be drawn
 * and invisible for an afternoon.
 *
 * The pen is kept, but THE SCREEN IS NOT HELD. A locked public screen
 * cannot be closed, and closing the Workbench screen is exactly what
 * Intuition does to change its screenmode: holding the lock for our
 * whole run made EdgeSnap block screenmode changes for as long as it
 * was running (field report, 2026-08-31). We lock for the instants we
 * need it and re-check, between drags, whether the screen under us was
 * replaced - see spike_accent_recheck().
 */
static struct Accent {
    struct Screen *scr;
    struct ColorMap *cm;
    LONG pen;
    int ok;
} g_accent;

static void spike_accent_take(struct Screen *scr)
{
    g_accent.scr = scr;
    g_accent.cm = scr->ViewPort.ColorMap;
    g_accent.pen = ObtainBestPen(g_accent.cm, 0x22222222UL, 0x88888888UL,
                                 0xFFFFFFFFUL, OBP_Precision,
                                 PRECISION_GUI, TAG_DONE);
    if (g_accent.pen != -1) {
        g_accent.ok = 1;
        return;
    }
    {
        struct DrawInfo *dri = GetScreenDrawInfo(scr);

        g_accent.ok = 0;
        g_accent.pen = 3;
        if (dri != NULL) {
            if (dri->dri_NumPens > FILLPEN) {
                g_accent.pen = dri->dri_Pens[FILLPEN];
            }
            FreeScreenDrawInfo(scr, dri);
        }
    }
}

static int spike_accent_init(void)
{
    struct Screen *scr = LockPubScreen(NULL);

    if (scr == NULL) {
        return 0;
    }
    spike_accent_take(scr);
    UnlockPubScreen(NULL, scr);
    return 1;
}

static void spike_accent_free(void)
{
    struct Screen *scr = LockPubScreen(NULL);

    /* Release only into the colormap we actually took the pen from: if
     * the screen was replaced while we ran, that colormap is gone. */
    if (g_accent.ok && scr != NULL && scr == g_accent.scr) {
        ReleasePen(g_accent.cm, (ULONG)g_accent.pen);
    }
    g_accent.ok = 0;
    g_accent.scr = NULL;
    if (scr != NULL) {
        UnlockPubScreen(NULL, scr);
    }
}

/*
 * Between drags: has the public screen been replaced under us, and do
 * we still owe ourselves a proper pen? Both are cheap to answer and
 * neither is safe to ask in the middle of a drag.
 */
static void spike_screen_changed(int replaced);

static void spike_accent_recheck(void)
{
    struct Screen *scr = LockPubScreen(NULL);

    if (scr == NULL) {
        return;
    }
    if (scr != g_accent.scr) {
        /* The old screen is gone and its colormap with it, so the pen
         * is forgotten rather than released into freed memory. */
        g_accent.ok = 0;
        spike_accent_take(scr);
        spike_screen_changed(1);
    } else if (!g_accent.ok) {
        /* We are on the FILLPEN fallback because ObtainBestPen failed
         * mid-drag. No drag is in flight now: try again. */
        spike_accent_take(scr);
        spike_screen_changed(0);   /* same screen: keep what is drawn */
    }
    UnlockPubScreen(NULL, scr);
}

struct Preview {
    struct Window *bars[4];
    int visible;
    ESRect rect;
    /* accent pen obtained per preview session; the colormap pointer is
     * only used between show and hide (a drag lasts seconds, and the
     * screen cannot close while windows live on it). */
    struct ColorMap *pen_cm;
    LONG pen;
    int pen_obtained;
};

static struct Preview g_preview;

/* Tell the library which windows its panel scan must ignore: our own
 * frame bars have exactly a dock's shape. The OS4 frame is drawn XOR
 * on the screen, so there is nothing to ignore there. */
static struct Window *g_divider;   /* forward: defined with the divider */

static void spike_publish_ignored(void)
{
    struct Window *mine[5];
    int n = 0;

#ifdef ES_PREVIEW_WINDOWS
    int i;

    for (i = 0; i < 4; i++) {
        mine[n++] = g_preview.bars[i];
    }
#endif
    mine[n++] = g_divider;
    ES_CALL(ESnap_IgnoreWindows)(mine, (ULONG)n);
}

#if defined(ES_PREVIEW_PIXELS) || defined(ES_PREVIEW_XOR)
/*
 * The frame on AmigaOS 4, and why it is drawn straight onto the screen.
 *
 * While Intuition drags a window, another program's window operations
 * do not take effect until the button is released - opening a window,
 * moving one, bringing one to front. Measured twice, in August and
 * again on 2026-08-29: the four-bar frame that MorphOS uses is placed
 * correctly (the log says so) and stays invisible for the whole drag,
 * even where no window covers it. So on OS4 the frame has to be drawn
 * on the screen's RastPort, which bypasses layers.
 *
 * The first version drew it in COMPLEMENT, the way Intuition draws its
 * own feedback. That works anywhere without knowing the background,
 * and it looks it: COMPLEMENT inverts what is underneath, so over a
 * picture backdrop the frame comes out a different colour on every
 * pixel, and there is no way to repair it when something paints over
 * it - drawing it again would erase it.
 *
 * So: save the pixels under the four strips, fill them with one solid
 * accent colour, and put the pixels back when the frame goes. A solid
 * fill is idempotent, which is what makes the frame repairable - it is
 * refilled on every engine step, so anything that painted over it is
 * covered again straight away.
 *
 * What this still cannot do: if the content under a strip changed
 * while the frame was up (the dragged window passing beneath it), the
 * pixels put back are the old ones. The snap that follows repaints
 * everything, so it is only visible when a drag ends in no zone at all.
 */
static struct PreviewFrame {
    int drawn;
    ESRect rect;
    struct Screen *scr;      /* pubscreen-locked while drawn */
    LONG pen;
    int pen_ok;
    struct ColorMap *cm;
    UBYTE *saved[4];         /* the pixels each strip covered; XOR: scratch */
    UBYTE *mask[4];          /* XOR: per pixel, what was there XOR accent */
    UBYTE accent[4];         /* XOR: the accent as the bytes of an ARGB pixel */
    int plain;               /* XOR: this frame is a plain inversion */
    int shallow;             /* XOR: fewer than 24 bits, reads are lossy */
    int said_plain;          /* XOR: the fallback was logged once */
    ESRect strip[4];
    int strips;
} g_pf;

#define ES_PF_BPP 4          /* PIXF_A8R8G8B8 */

static void spike_pf_strips(const ESRect *r, ESRect *strip, int *count)
{
    int t = ES_FRAME_PX;

    if (r->w < 2 * t || r->h < 2 * t) {
        t = 1;
    }
    strip[0].x = r->x;              strip[0].y = r->y;
    strip[0].w = r->w;              strip[0].h = t;
    strip[1].x = r->x;              strip[1].y = r->y + r->h - t;
    strip[1].w = r->w;              strip[1].h = t;
    strip[2].x = r->x;              strip[2].y = r->y + t;
    strip[2].w = t;                 strip[2].h = r->h - 2 * t;
    strip[3].x = r->x + r->w - t;   strip[3].y = r->y + t;
    strip[3].w = t;                 strip[3].h = r->h - 2 * t;
    *count = (strip[2].h > 0) ? 4 : 2;
}

/* Free the strip buffers. Task context only: the input handler may
 * take the frame down, but it leaves the buffers to the task. */
static void spike_pf_free_buffers(void)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_pf.saved[i] != NULL) {
            FreeVec(g_pf.saved[i]);
            g_pf.saved[i] = NULL;
        }
        if (g_pf.mask[i] != NULL) {
            FreeVec(g_pf.mask[i]);
            g_pf.mask[i] = NULL;
        }
    }
}

#ifndef ES_PREVIEW_XOR
/* Paint the four strips in the accent colour. Called again on every
 * engine step: a solid fill can be repeated, which is the whole reason
 * for not using COMPLEMENT here. */
static void spike_pf_paint(void)
{
    struct RastPort rp;
    int i;

    rp = g_pf.scr->RastPort;   /* a copy: leave the screen's own alone */
    SetAPen(&rp, (ULONG)g_pf.pen);
    SetDrMd(&rp, JAM1);
    for (i = 0; i < g_pf.strips; i++) {
        RectFill(&rp, g_pf.strip[i].x, g_pf.strip[i].y,
                 g_pf.strip[i].x + g_pf.strip[i].w - 1,
                 g_pf.strip[i].y + g_pf.strip[i].h - 1);
    }
}
#else
/* The accent colour as the four bytes of an ARGB pixel. */
static void spike_pf_accent_bytes(void)
{
    ULONG rgb[3];

    if (g_pf.scr == NULL) {
        return;
    }
    GetRGB32(g_pf.scr->ViewPort.ColorMap, (ULONG)g_pf.pen, 1, rgb);
    g_pf.accent[0] = 0xFF;
    g_pf.accent[1] = (UBYTE)(rgb[0] >> 24);
    g_pf.accent[2] = (UBYTE)(rgb[1] >> 24);
    g_pf.accent[3] = (UBYTE)(rgb[2] >> 24);
    /* on 15 and 16 bits a written colour does not read back as itself */
    g_pf.shallow = GetBitMapAttr(g_pf.scr->RastPort.BitMap, BMA_DEPTH) < 24;
}

/*
 * One pass over a strip: read what is there, XOR it with the strip's
 * mask, write it back. The first pass paints the accent, the second
 * one restores. Called under Forbid, from the task or from the input
 * handler; nothing in here allocates or waits. Should the read fail
 * here, the scratch still holds what the first pass wrote, and XORing
 * that with the mask gives back what was there before the frame.
 */
static void spike_pf_xor_strip(int i)
{
    const ESRect *st = &g_pf.strip[i];
    UBYTE *p = g_pf.saved[i];
    const UBYTE *m = g_pf.mask[i];
    ULONG n = (ULONG)(st->w * st->h * ES_PF_BPP), k;

    if (p == NULL || m == NULL) {
        return;
    }
    ES_PF_READ(&g_pf.scr->RastPort, st->x, st->y, p, st->w * ES_PF_BPP,
               st->w, st->h);
    for (k = 0; k < n; k++) {
        p[k] ^= m[k];
    }
    ES_PF_WRITE(p, st->w * ES_PF_BPP, &g_pf.scr->RastPort, st->x, st->y,
                st->w, st->h);
}

/*
 * The plain inversion, for a screen the mask cannot serve: fewer than
 * 24 bits, where a written colour does not read back as itself, or a
 * driver whose reads return nothing. Drawn twice it is gone, and it
 * composes with Intuition's outline all the same; only the colour is
 * whatever the inverse of the background is.
 */
static void spike_pf_invert(void)
{
    struct RastPort rp;
    int i;

    rp = g_pf.scr->RastPort;   /* a copy: leave the screen's own alone */
    SetDrMd(&rp, COMPLEMENT);
    for (i = 0; i < g_pf.strips; i++) {
        RectFill(&rp, g_pf.strip[i].x, g_pf.strip[i].y,
                 g_pf.strip[i].x + g_pf.strip[i].w - 1,
                 g_pf.strip[i].y + g_pf.strip[i].h - 1);
    }
}
#endif

/* Repair the frame (pixel technique only: an XOR frame is never
 * painted over by the outline drag, the two compose). */
static void spike_pf_fill(void)
{
#ifndef ES_PREVIEW_XOR
    if (g_pf.drawn) {
        spike_pf_paint();
    }
#endif
}

#ifdef ES_PREVIEW_XOR
/*
 * After a masked frame has come down, the scratch of every strip holds
 * what the second pass wrote, which is what was there before the
 * frame. Read the strips once more and compare: a difference means the
 * erase did not land, or landed elsewhere, on this driver. Task
 * context only, and the finding is only logged, never repaired: the
 * window may have moved since, and then the difference is legitimate.
 */
static void spike_pf_check_erased(void)
{
    static int said;
    int i;
    ULONG diff = 0, n_total = 0;

    if (said || g_pf.drawn || g_pf.plain || g_pf.scr == NULL) {
        return;
    }
    for (i = 0; i < g_pf.strips; i++) {
        const ESRect *st = &g_pf.strip[i];
        const UBYTE *want = g_pf.saved[i];
        UBYTE *m = g_pf.mask[i];    /* reused as the read buffer */
        ULONG n = (ULONG)(st->w * st->h * ES_PF_BPP), k;

        if (want == NULL || m == NULL) {
            continue;
        }
        ES_PF_READ(&g_pf.scr->RastPort, st->x, st->y, m, st->w * ES_PF_BPP,
                   st->w, st->h);
        for (k = 0; k < n; k++) {
            if ((k & 3) != 0 && m[k] != want[k]) {
                diff++;
            }
        }
        n_total += n;
    }
    if (diff != 0) {
        said = 1;
        spike_log("edgesnap: frame: after the erase %lu of %lu bytes differ "
                  "from what was there (window moved, or the erase did not "
                  "land)\n", (unsigned long)diff, (unsigned long)n_total);
    }
}
#endif

static void spike_pf_hide(void)
{
#ifdef ES_PREVIEW_XOR
    spike_pf_drop_now();       /* the second pass takes it away */
    spike_pf_check_erased();   /* diagnostics for the drivers we cannot see */
    spike_pf_free_buffers();   /* task context: the handler leaves them */
#else
    int i;

    if (!g_pf.drawn) {
        return;
    }
    for (i = 0; i < g_pf.strips; i++) {
        if (g_pf.saved[i] != NULL) {
            ES_PF_WRITE(g_pf.saved[i], g_pf.strip[i].w * ES_PF_BPP,
                        &g_pf.scr->RastPort, g_pf.strip[i].x, g_pf.strip[i].y,
                        g_pf.strip[i].w, g_pf.strip[i].h);
            FreeVec(g_pf.saved[i]);
            g_pf.saved[i] = NULL;
        }
    }
    g_pf.drawn = 0;
#endif
}

#ifdef ES_PREVIEW_XOR
/*
 * Take the XOR frame down, from whichever context asks first: the
 * engine task at the end of a drag, or the input handler on the
 * release event, which on AROS comes before Intuition moves the
 * window. Forbid keeps the two from interleaving: the handler runs in
 * input.device's task, and a task cannot be switched to while another
 * is forbidden. The flag and the drawing change together.
 */
static void spike_pf_drop_now(void)
{
    int i;

    Forbid();
    if (g_pf.drawn && g_pf.scr != NULL) {
        if (g_pf.plain) {
            spike_pf_invert();
        } else {
            for (i = 0; i < g_pf.strips; i++) {
                spike_pf_xor_strip(i);
            }
        }
        g_pf.drawn = 0;
    }
    Permit();
}
#endif

/*
 * Forget the saved pixels instead of restoring them. Used when the
 * screen they were read from has been replaced: writing them back
 * would blit into a RastPort that no longer exists.
 */
static void spike_pf_discard(void)
{
    g_pf.drawn = 0;
    g_pf.strips = 0;
    spike_pf_free_buffers();
}

/*
 * The screen was replaced: the pixels we saved came from a RastPort
 * that no longer exists, so they are dropped rather than drawn back,
 * and the frame follows the accent onto the new screen.
 */
static void spike_screen_changed(int replaced)
{
    if (replaced) {
        spike_pf_discard();
        g_pf.scr = g_accent.scr;
    }
    g_pf.pen = g_accent.pen;
#ifdef ES_PREVIEW_XOR
    spike_pf_accent_bytes();
#endif
}

/*
 * The screen and the accent pen are taken once, at startup, and kept.
 * Asking for a pen in the middle of a drag is asking for trouble: if
 * ObtainBestPen fails there the fallback is FILLPEN, which on the OS4
 * theme is a grey almost identical to the Workbench background - the
 * frame would be drawn and be invisible, which is indistinguishable
 * from not being drawn at all.
 */
static int spike_pf_init(void)
{
    g_pf.scr = g_accent.scr;      /* the screen the accent was taken on */
    g_pf.pen = g_accent.pen;
#ifdef ES_PREVIEW_XOR
    spike_pf_accent_bytes();
#endif
    return g_pf.scr != NULL;
}

static void spike_pf_cleanup(void)
{
    spike_pf_hide();
    g_pf.scr = NULL;
}

static void spike_pf_show(struct Screen *dragscr, const ESRect *r)
{
    int i;

    if (g_pf.drawn &&
        g_pf.rect.x == r->x && g_pf.rect.y == r->y &&
        g_pf.rect.w == r->w && g_pf.rect.h == r->h) {
        spike_pf_fill();       /* same frame: just repair it */
        return;
    }
    spike_pf_hide();

    if (g_pf.scr == NULL || dragscr != g_pf.scr) {
        /* Silent once, and on AROS that silence hid the whole story. */
        spike_log("edgesnap: frame: screen mismatch (drag on %p, frame "
                  "screen %p)\n", (void *)dragscr, (void *)g_pf.scr);
        return;
    }
    g_pf.rect = *r;
    spike_pf_strips(r, g_pf.strip, &g_pf.strips);
    spike_log("edgesnap: frame: %d,%d %dx%d pen %ld strips %d\n",
              r->x, r->y, r->w, r->h, (long)g_pf.pen, g_pf.strips);

#ifndef ES_PREVIEW_XOR
    for (i = 0; i < g_pf.strips; i++) {
        ULONG bytes = (ULONG)(g_pf.strip[i].w * g_pf.strip[i].h * ES_PF_BPP);

        g_pf.saved[i] = (UBYTE *)AllocVec(bytes, MEMF_ANY);
        if (g_pf.saved[i] == NULL) {
            continue;          /* that strip simply will not be restored */
        }
        ES_PF_READ(&g_pf.scr->RastPort, g_pf.strip[i].x, g_pf.strip[i].y,
                   g_pf.saved[i], g_pf.strip[i].w * ES_PF_BPP,
                   g_pf.strip[i].w, g_pf.strip[i].h);
    }
    g_pf.drawn = 1;
    spike_pf_paint();
#else
    spike_pf_free_buffers();   /* a frame the handler took down left them */
    for (i = 0; i < g_pf.strips; i++) {
        ULONG bytes = (ULONG)(g_pf.strip[i].w * g_pf.strip[i].h * ES_PF_BPP);

        g_pf.saved[i] = (UBYTE *)AllocVec(bytes, MEMF_ANY);
        g_pf.mask[i] = (UBYTE *)AllocVec(bytes, MEMF_ANY);
        if (g_pf.saved[i] == NULL || g_pf.mask[i] == NULL) {
            if (g_pf.saved[i] != NULL) {
                FreeVec(g_pf.saved[i]);
                g_pf.saved[i] = NULL;
            }
            if (g_pf.mask[i] != NULL) {
                FreeVec(g_pf.mask[i]);
                g_pf.mask[i] = NULL;
            }
        }
    }
    /*
     * The mask and the first pass in ONE forbidden stretch, all four
     * strips: a release arriving in the middle would find the frame
     * either not yet up or fully up, never half of it. The reads are
     * checked: a driver that returns nothing would leave the mask made
     * of whatever the allocation held, and the second pass would then
     * paint that garbage, black on a fresh system. Such a frame falls
     * back to the plain inversion, which reads nothing.
     */
    Forbid();
    g_pf.plain = g_pf.shallow;
    for (i = 0; i < g_pf.strips && !g_pf.plain; i++) {
        const ESRect *st = &g_pf.strip[i];
        UBYTE *p = g_pf.saved[i];
        ULONG n = (ULONG)(st->w * st->h * ES_PF_BPP), k;

        if (p == NULL || g_pf.mask[i] == NULL) {
            g_pf.plain = 1;    /* no memory: the inversion needs none */
            break;
        }
        if (ES_PF_READ(&g_pf.scr->RastPort, st->x, st->y, p,
                       st->w * ES_PF_BPP, st->w, st->h) <
            (ULONG)(st->w * st->h)) {
            g_pf.plain = 1;
            break;
        }
        for (k = 0; k < n; k++) {
            g_pf.mask[i][k] = p[k] ^ g_pf.accent[k & 3];
        }
    }
    if (!g_pf.plain) {
        int agreed = 1;

        for (i = 0; i < g_pf.strips; i++) {
            const ESRect *st = &g_pf.strip[i];
            UBYTE *p = g_pf.saved[i];
            ULONG n = (ULONG)(st->w * st->h * ES_PF_BPP), k;

            for (k = 0; k < n; k++) {
                p[k] ^= g_pf.mask[i][k];   /* which is the accent */
            }
            ES_PF_WRITE(p, st->w * ES_PF_BPP, &g_pf.scr->RastPort,
                        st->x, st->y, st->w, st->h);
        }
        /*
         * Read the strips back and compare them with the accent. A
         * driver whose reads count pixels but return zeros, or a
         * format that loses bits on the way, would make the second
         * pass paint garbage; caught here, the accent is taken off
         * again and the frame becomes the plain inversion instead.
         */
        for (i = 0; i < g_pf.strips && agreed; i++) {
            const ESRect *st = &g_pf.strip[i];
            UBYTE *p = g_pf.saved[i];
            ULONG n = (ULONG)(st->w * st->h * ES_PF_BPP), k;

            ES_PF_READ(&g_pf.scr->RastPort, st->x, st->y, p,
                       st->w * ES_PF_BPP, st->w, st->h);
            for (k = 0; k < n; k++) {
                if ((k & 3) != 0 && p[k] != g_pf.accent[k & 3]) {
                    agreed = 0;   /* alpha is not compared */
                    break;
                }
            }
        }
        if (!agreed) {
            for (i = 0; i < g_pf.strips; i++) {
                const ESRect *st = &g_pf.strip[i];
                UBYTE *p = g_pf.saved[i];
                ULONG n = (ULONG)(st->w * st->h * ES_PF_BPP), k;

                for (k = 0; k < n; k++) {
                    p[k] = g_pf.mask[i][k] ^ g_pf.accent[k & 3];
                }
                ES_PF_WRITE(p, st->w * ES_PF_BPP, &g_pf.scr->RastPort,
                            st->x, st->y, st->w, st->h);
            }
            g_pf.plain = 1;
        }
    }
    if (g_pf.plain) {
        spike_pf_invert();
    }
    g_pf.drawn = 1;
    Permit();
    if (g_pf.plain && !g_pf.said_plain) {
        g_pf.said_plain = 1;
        spike_log("edgesnap: frame: %s, drawn as a plain inversion\n",
                  g_pf.shallow ? "screen below 24 bits"
                               : "pixel reads not trusted");
    }
#endif
}
#endif /* ES_PREVIEW_PIXELS */

#ifdef ES_PREVIEW_WINDOWS
/* MorphOS draws its preview with borderless windows, which Intuition
 * disposes of with the screen, so there is nothing of ours to drop. */
static void spike_screen_changed(int replaced)
{
    (void)replaced;
}
#endif

static int spike_is_preview_win(struct Window *w)
{
    int i;

    for (i = 0; i < 4; i++) {
        if (g_preview.bars[i] == w) {
            return 1;
        }
    }
    return 0;
}

/*
 * Repair the frame. Something painting over it - the window being
 * dragged, a refresh underneath - takes pieces out of it, and a solid
 * fill can simply be repeated. Called on every engine step while a
 * drag is in flight, which is often enough that the eye never catches
 * the gap. On MorphOS the frame is made of real windows and looks
 * after itself.
 */
static void spike_preview_refresh(void)
{
#if defined(ES_PREVIEW_PIXELS) || defined(ES_PREVIEW_XOR)
    spike_pf_fill();
#endif
}

static void spike_preview_hide(void)
{
#if defined(ES_PREVIEW_PIXELS) || defined(ES_PREVIEW_XOR)
    spike_pf_hide();
#else
    int i;

    if (!g_preview.visible) {
        return;
    }
    for (i = 0; i < 4; i++) {
        if (g_preview.bars[i] != NULL) {
            CloseWindow(g_preview.bars[i]);
            g_preview.bars[i] = NULL;
        }
    }
    if (g_preview.pen_obtained) {
        ReleasePen(g_preview.pen_cm, (ULONG)g_preview.pen);
        g_preview.pen_obtained = 0;
    }
    g_preview.visible = 0;
    spike_publish_ignored();
#endif
}

#ifdef ES_PREVIEW_WINDOWS
static struct Window *spike_preview_bar(struct Screen *scr, LONG pen,
                                        int x, int y, int w, int h)
{
    struct Window *win;

    win = OpenWindowTags(NULL,
                         WA_CustomScreen, scr,
                         WA_Left, x,
                         WA_Top, y,
                         WA_Width, w,
                         WA_Height, h,
                         WA_Flags, WFLG_BORDERLESS | WFLG_SMART_REFRESH |
                                   WFLG_NOCAREREFRESH,
                         WA_Activate, FALSE,
                         TAG_DONE);
    if (win != NULL) {
        SetAPen(win->RPort, (ULONG)pen);
        RectFill(win->RPort, 0, 0, win->Width - 1, win->Height - 1);
    }
    return win;
}
#endif /* ES_PREVIEW_WINDOWS */

static void spike_preview_show(struct Screen *dragscr, const ESRect *r)
{
#if defined(ES_PREVIEW_PIXELS) || defined(ES_PREVIEW_XOR)
    spike_pf_show(dragscr, r);
#else
    struct Screen *scr;
    struct DrawInfo *dri;
    LONG pen = 3;
    int t = ES_FRAME_PX;
    int i, ok;
    ESRect bar[4];
    int bars;

    if (g_preview.visible &&
        g_preview.rect.x == r->x && g_preview.rect.y == r->y &&
        g_preview.rect.w == r->w && g_preview.rect.h == r->h) {
        return;
    }
    spike_preview_hide();

    /*
     * Only the default public screen for now: LockPubScreen guarantees it
     * stays alive while the bars open; visitor windows keep it open after
     * the unlock. A drag on another screen just gets no preview.
     */
    scr = LockPubScreen(NULL);
    if (scr == NULL) {
        spike_log("edgesnap: preview: no default public screen\n");
        return;
    }
    if (scr != dragscr) {
        spike_log("edgesnap: preview: drag is not on the default public "
                  "screen, no preview\n");
        UnlockPubScreen(NULL, scr);
        return;
    }

    /*
     * Frame color: an explicit azure accent via ObtainBestPen - exact on
     * the 32-bit screens both OSes run. The old FILLPEN choice was
     * invisible on OS4: its theme resolves FILLPEN to a gray nearly
     * identical to the Workbench background (field finding 2026-08-26).
     */
    g_preview.pen_cm = scr->ViewPort.ColorMap;
    pen = ObtainBestPen(g_preview.pen_cm,
                        0x22222222UL, 0x88888888UL, 0xFFFFFFFFUL,
                        OBP_Precision, PRECISION_GUI,
                        TAG_DONE);
    if (pen != -1) {
        g_preview.pen_obtained = 1;
        g_preview.pen = pen;
    } else {
        pen = 3;
        dri = GetScreenDrawInfo(scr);
        if (dri != NULL) {
            if (dri->dri_NumPens > FILLPEN) {
                pen = dri->dri_Pens[FILLPEN];
            }
            FreeScreenDrawInfo(scr, dri);
        }
        spike_log("edgesnap: preview: ObtainBestPen failed, fallback "
                  "pen %ld\n", (long)pen);
    }

    if (r->w < 2 * t || r->h < 2 * t) {
        t = 1;
    }
    bar[0].x = r->x;
    bar[0].y = r->y;
    bar[0].w = r->w;
    bar[0].h = t;
    bar[1].x = r->x;
    bar[1].y = r->y + r->h - t;
    bar[1].w = r->w;
    bar[1].h = t;
    bar[2].x = r->x;
    bar[2].y = r->y + t;
    bar[2].w = t;
    bar[2].h = r->h - 2 * t;
    bar[3].x = r->x + r->w - t;
    bar[3].y = r->y + t;
    bar[3].w = t;
    bar[3].h = r->h - 2 * t;
    bars = (bar[2].h > 0) ? 4 : 2;

    ok = 1;
    for (i = 0; i < bars; i++) {
        g_preview.bars[i] = spike_preview_bar(scr, pen, bar[i].x, bar[i].y,
                                              bar[i].w, bar[i].h);
        if (g_preview.bars[i] == NULL) {
            ok = 0;
        }
    }
    g_preview.visible = 1; /* set before a possible hide on failure */
    g_preview.rect = *r;
    UnlockPubScreen(NULL, scr);
    spike_publish_ignored();
    if (!ok) {
        spike_log("edgesnap: preview: OpenWindow failed, no frame\n");
        spike_preview_hide();
    }
#endif
}

/* ----------------------------------------------------- the divider */

/*
 * The handle the user grabs to re-balance two snapped windows. It is a
 * thin window of ours sitting on the seam: it gets its own IDCMP, so
 * nothing is stolen from anyone, and the resizing itself is the
 * library's job - we only report where the pointer went.
 */

/*
 * The double arrow that says "this edge can be dragged", drawn by us.
 *
 * Not WA_PointerType: the system's built-in resize pointers are a
 * V53.37 feature, and on the AmigaOS 4.1 FE test machine asking for
 * one leaves the pointer BLANK - POINTERTYPE_BUSY included, so it is
 * not a missing image but the whole tag. A sprite of our own works on
 * every version of both systems and ignores the user's pointer theme.
 *
 * Intuition shows the ACTIVE window's pointer, so this can only appear
 * once the strip has been clicked - which is exactly when it is wanted.
 */

/*
 * The double arrow, under the name each system gives it: AmigaOS 4
 * points to the compass, MorphOS to the axis.
 */
#if defined(__amigaos4__)
#define ES_PTR_SEAM_VERTICAL   POINTERTYPE_EASTWESTRESIZE
#define ES_PTR_SEAM_HORIZONTAL POINTERTYPE_NORTHSOUTHRESIZE
#elif defined(__AROS__)
/* AROS has no system pointer types to ask for (its pointerclass.h
 * defines none). The strip lighting up under the pointer is the
 * affordance anyway; a double arrow of our own is later work. */
#define ES_PTR_SEAM_NONE 1
#else
#define ES_PTR_SEAM_VERTICAL   POINTERTYPE_HORIZONTALRESIZE
#define ES_PTR_SEAM_HORIZONTAL POINTERTYPE_VERTICALRESIZE
#endif

static struct Window *g_divider;
static int g_divider_vertical;
/* Which seam the strip is on. A layout can hold several - one window
 * facing two stacked ones has a vertical seam moving all three and a
 * horizontal one moving only the two - so the library is told which by
 * the line it sits on. */
static int g_divider_line;
static int g_divider_dragging;
static struct Screen *g_divider_scr;   /* blind drag: where the pointer is read */
/* Is the strip painted right now? It opens invisible and lights up when
 * the pointer comes to it. */
static int g_divider_hot;

/* How near the pointer must come for the seam to show itself, and how
 * far it must go for the seam to disappear again. The two differ so a
 * pointer resting on the boundary does not blink. */
#define ES_SEAM_SHOW_PX  3
#define ES_SEAM_HIDE_PX 14

static void spike_divider_close(void)
{
    if (g_divider != NULL) {
        CloseWindow(g_divider);
        g_divider = NULL;
        g_divider_dragging = 0;
        spike_publish_ignored();
    }
}

/*
 * The seam is visible, and it has to be.
 *
 * It was invisible first, on the macOS reasoning that two tiled windows
 * should simply touch. But Intuition shows the pointer of the ACTIVE
 * window, not of the one under the mouse, so the double arrow only
 * appears once the strip has been grabbed - which leaves nothing at all
 * to tell anyone the seam is there or where it is. Reported from the
 * field as, exactly, "I cannot grab it".
 *
 * So it is drawn, in the same accent colour as the preview frame, and
 * it is ten pixels wide: a line you can see and a target you can hit.
 *
 * But drawn ONLY when it is wanted. A permanent bar across the screen
 * reads as damage rather than as a control, so the strip opens with
 * LAYERS_NOBACKFILL and paints nothing: what shows is the two window
 * edges that were there anyway. It lights up when the pointer comes
 * within ES_SEAM_SHOW_PX of it, which is the feedback Intuition will
 * not give us - it shows the ACTIVE window's pointer, so our double
 * arrow cannot appear on hover, only once the strip has been clicked.
 * Going out again is done by closing and reopening the strip: that
 * damages the area and the windows underneath repaint it, which is the
 * same trick used when the seam moves.
 */
static void spike_divider_paint(void)
{
    if (g_divider == NULL || g_accent.scr == NULL) {
        return;
    }
    SetAPen(g_divider->RPort, (ULONG)g_accent.pen);
    RectFill(g_divider->RPort, 0, 0, g_divider->Width - 1,
             g_divider->Height - 1);
}
/*
 * Grabbing the seam has to activate our strip, or Intuition stops
 * sending us mouse movements the moment the pointer leaves its eight
 * pixels. But an invisible window holding the keyboard is a trap: the
 * user drags the seam, types, and nothing happens anywhere.
 *
 * So the focus is handed on when the drag ends - to one of the two
 * windows just resized, whichever the pointer is on. Remembering who
 * had it before does not work: Intuition activates the window under
 * the pointer BEFORE delivering the click, so by the time we hear
 * about the press the answer is already "us", and sampling earlier
 * races the activation of whatever the user clicked last. Handing it
 * to the window under the pointer needs no history and cannot be
 * stale: the library validated both windows a moment ago.
 */
static void spike_divider_pass_focus(const struct ESnapDivider *d)
{
    struct Window *win;

    if (d == NULL || !d->present || g_divider == NULL) {
        return;
    }
    if (d->vertical) {
        win = (g_divider->WScreen->MouseX < (LONG)d->strip.x) ?
            d->windowA : d->windowB;
    } else {
        win = (g_divider->WScreen->MouseY < (LONG)d->strip.y) ?
            d->windowA : d->windowB;
    }
    if (win != NULL) {
        ActivateWindow(win);
    }
}

static void spike_divider_park(void)
{
    struct Screen *scr;

    if (g_divider == NULL) {
        return;
    }
    scr = g_divider->WScreen;
    ChangeWindowBox(g_divider, (int)scr->MouseX - 4, (int)scr->MouseY - 4,
                    8, 8);
}

static void spike_divider_pointer(void)
{
    if (g_divider == NULL) {
        return;
    }
#ifndef ES_PTR_SEAM_NONE
    SetWindowPointer(g_divider,
                     WA_PointerType, g_divider_vertical ?
                         ES_PTR_SEAM_VERTICAL : ES_PTR_SEAM_HORIZONTAL,
                     TAG_DONE);
#endif
}

/*
 * Distance from the pointer to the strip, 0 when it is inside it. The
 * strip is a thin rectangle, so this is the usual box distance with the
 * two axes taken separately.
 */
static int spike_divider_distance(void)
{
    struct Screen *scr;
    int px, py, dx = 0, dy = 0;

    if (g_divider == NULL) {
        return 0x7FFF;
    }
    scr = g_divider->WScreen;
    px = (int)scr->MouseX;
    py = (int)scr->MouseY;
    if (px < g_divider->LeftEdge) {
        dx = g_divider->LeftEdge - px;
    } else if (px >= g_divider->LeftEdge + g_divider->Width) {
        dx = px - (g_divider->LeftEdge + g_divider->Width - 1);
    }
    if (py < g_divider->TopEdge) {
        dy = g_divider->TopEdge - py;
    } else if (py >= g_divider->TopEdge + g_divider->Height) {
        dy = py - (g_divider->TopEdge + g_divider->Height - 1);
    }
    return dx > dy ? dx : dy;
}

static void spike_divider_sync(void);

/*
 * Light the seam up when the pointer arrives, put it out when it
 * leaves. Called on pointer movement while nothing is being dragged.
 */
static void spike_divider_hover(void)
{
    int d;

    if (g_divider == NULL || g_divider_dragging) {
        return;
    }
    d = spike_divider_distance();
    if (!g_divider_hot && d <= ES_SEAM_SHOW_PX) {
        spike_divider_paint();
        g_divider_hot = 1;
        return;
    }
    if (g_divider_hot && d >= ES_SEAM_HIDE_PX) {
        /* Reopening is what erases it: closing damages the area and the
         * two windows underneath draw themselves back. */
        spike_divider_close();
        spike_divider_sync();
    }
}

/* Ask the library where the seam is and put the handle there. */
static void spike_divider_sync(void)
{
    struct ESnapDivider d;
    struct Screen *scr;

    {
        /*
         * The seam NEAREST THE POINTER, not the first one in the
         * layout: with several seams on screen the one the user means
         * is the one they are reaching for.
         */
        struct Screen *ps = LockPubScreen(NULL);
        LONG px = 0, py = 0;
        LONG rc;

        if (ps != NULL) {
            px = (LONG)ps->MouseX;
            py = (LONG)ps->MouseY;
            UnlockPubScreen(NULL, ps);
        }
        rc = ES_CALL(ESnap_QueryDividerAt)(ES_DIVIDER_PX, px, py, &d);
        if (rc != ES_OK || !d.present) {
            spike_divider_close();
            return;
        }
    }
    if (g_divider != NULL) {
        /*
         * Moving a painted window drags its paint across the screen,
         * so when the seam has actually moved the handle is closed and
         * a new one opened: closing damages the area and the windows
         * underneath repaint it themselves.
         */
        if (g_divider->LeftEdge != (int)d.strip.x ||
            g_divider->TopEdge != (int)d.strip.y ||
            g_divider->Width != (int)d.strip.w ||
            g_divider->Height != (int)d.strip.h) {
            spike_divider_close();
        }
    }
    if (g_divider != NULL) {
        WindowToFront(g_divider);
        g_divider_vertical = (int)d.vertical;
        return;
    }

    scr = LockPubScreen(NULL);
    if (scr == NULL) {
        return;
    }
    /*
     * The handle draws NOTHING. macOS and Windows put no line between
     * two tiled windows - the seam is found by the pointer changing
     * shape over it, and a permanent bar across the screen reads as
     * damage, not as an affordance. LAYERS_NOBACKFILL stops Intuition
     * clearing the strip, so what shows through is the two window
     * edges that are already there.
     */
    g_divider = OpenWindowTags(NULL,
                               WA_CustomScreen, scr,
                               WA_Left, (int)d.strip.x,
                               WA_Top, (int)d.strip.y,
                               WA_Width, (int)d.strip.w,
                               WA_Height, (int)d.strip.h,
                               /*
                                * SIMPLE_REFRESH, not SMART: a smart
                                * window owns a bitmap, and NOBACKFILL
                                * would leave that bitmap uncleared,
                                * which is garbage rather than
                                * transparency. A simple layer with no
                                * backfill leaves the screen pixels
                                * alone, so an unpainted strip shows the
                                * two window edges that were already
                                * there.
                                */
                               WA_Flags, WFLG_BORDERLESS |
                                         WFLG_SIMPLE_REFRESH |
                                         WFLG_NOCAREREFRESH |
                                         WFLG_REPORTMOUSE | WFLG_RMBTRAP,
                               WA_IDCMP, IDCMP_MOUSEBUTTONS |
                                         IDCMP_MOUSEMOVE,
                               WA_Activate, FALSE,
                               WA_BackFill, LAYERS_NOBACKFILL,
                               TAG_DONE);
    UnlockPubScreen(NULL, scr);
    if (g_divider == NULL) {
        spike_log("edgesnap: divider window would not open\n");
        return;
    }
    spike_log("edgesnap: divider handle open\n");
    WindowToFront(g_divider);
    g_divider_vertical = d.vertical;
    g_divider_line = (int)d.position;
    g_divider_hot = 0;          /* opens invisible: see the note above */
    spike_divider_pointer();
    spike_publish_ignored();
}

/*
 * One IDCMP round for the handle.
 *
 * The port is taken ONCE, before the loop, and the loop stops the
 * moment the handle is no longer the same window. Everything in here
 * can close it - a release re-asks where the seam is and there may no
 * longer be one, a failed move closes it outright - and re-reading
 * g_divider->UserPort after that is a read through a freed pointer.
 * Messages left on the old port belong to a window that has gone; the
 * new one, if any, is waited on in the next round.
 */
static void spike_apply_report(const struct ESnapReport *r);

static void spike_divider_events(void)
{
    struct IntuiMessage *im;
    struct MsgPort *port;

    if (g_divider == NULL) {
        return;
    }
    port = g_divider->UserPort;
    while ((im = (struct IntuiMessage *)GetMsg(port)) != NULL) {
        ULONG cls = im->Class;
        UWORD code = im->Code;
        ReplyMsg((struct Message *)im);

        if (cls == IDCMP_MOUSEBUTTONS) {
            if (code == SELECTDOWN) {
#ifdef ES_SEAM_DRAG_BLIND
                {
                    struct ESnapReport rep;

                    g_divider_scr = g_divider->WScreen;
                    spike_divider_close();     /* clears dragging too */
                    g_divider_dragging = 1;
                    /*
                     * The engine saw this press as any other: with the
                     * handle gone the active window is the one we are
                     * about to resize, and "the window moved while the
                     * pointer moved" would read as a drag of it. Told to
                     * forget the press, it sits this one out.
                     */
                    ES_CALL(ESnap_ResetInput)(&rep);
                    spike_apply_report(&rep);
                }
                return;                        /* the port is gone */
#else
                g_divider_dragging = 1;
                /* active window: otherwise the moves stop at our edge */
                ActivateWindow(g_divider);
                spike_divider_pointer();
                /*
                 * Shrink to a speck under the pointer for the duration
                 * of the drag. A full-height strip is invisible only as
                 * long as nothing renders it - and with window
                 * transparency switched on, the compositor renders it,
                 * so a band that lags behind the seam (the windows are
                 * placed asynchronously) or stops at the clamp shows up
                 * as a stray vertical line. Eight pixels under the
                 * pointer cannot be seen, and the window stays active,
                 * which is what keeps the double arrow and the mouse
                 * reports coming.
                 */
                spike_divider_park();
#endif
            } else if (code == SELECTUP) {
                g_divider_dragging = 0;
                /*
                 * Re-ask where the seam is now: the drag moved it, and
                 * it may have stopped existing altogether. The wait is
                 * the same one the snap path needs - ChangeWindowBox()
                 * returns before the windows have moved, and asking too
                 * early finds a pair whose geometry does not match yet,
                 * which reads as "no seam" and takes the handle away.
                 */
                {
                    struct ESnapDivider d;

                    Delay(10L);
                    spike_divider_sync();
                    if (ES_CALL(ESnap_QueryDivider)(ES_DIVIDER_PX, &d) ==
                            ES_OK && d.present) {
                        spike_divider_pass_focus(&d);
                    }
                }
                spike_log_flush();
            }
            if (g_divider == NULL || g_divider->UserPort != port) {
                return;
            }
        } else if (cls == IDCMP_MOUSEMOVE && g_divider_dragging) {
            /*
             * Take the pointer from the SCREEN, not from the message's
             * window-relative coordinates: we move this very window to
             * follow the seam, so a relative reading would be measured
             * against a position that has already changed under it.
             */
            LONG pos = g_divider_vertical ?
                (LONG)g_divider->WScreen->MouseX :
                (LONG)g_divider->WScreen->MouseY;
            LONG rc = ES_CALL(ESnap_MoveDividerAt)((LONG)g_divider_vertical,
                                                   (LONG)g_divider_line,
                                                   pos);

            if (rc == ES_OK) {
                /*
                 * The seam has moved, so its NAME has changed with it:
                 * keep the line up to date or the next call would be
                 * asking for a seam that is no longer where it was, and
                 * the drag would die a few pixels in.
                 */
                g_divider_line = (int)pos;
                /*
                 * Follow the POINTER, not the seam. Asking the library
                 * where the seam is now would race the placement it
                 * has just asked for - ChangeWindowBox() returns before
                 * the window has moved - and the answer would be stale
                 * exactly when the drag is fast. The seam is re-read
                 * once, on release, when everything has settled.
                 */
                spike_divider_park();
            } else {
                spike_log("edgesnap: divider gone (%ld)\n", (long)rc);
                g_divider_dragging = 0;
                spike_divider_close();
                return;
            }
        }
    }
}

/* -------------------------------------------------- core glue (phase 2) */

static ULONG g_seen_presses;
static ULONG g_seen_releases;
static ULONG g_seen_moves;

/*
 * The frontend contributes raw input facts and draws the frame; every
 * decision (drag detection, zones, snapping, restore) happens inside
 * the library body. A library must not print, so it reports what it
 * did and we log it here.
 */
static int g_drag_active;

static void spike_apply_report(const struct ESnapReport *r)
{
    if (r->previewHide) {
        spike_preview_hide();
    }
    if (r->dragStarted) {
        spike_log("edgesnap: drag detected\n");
    }
    if (r->zoneChanged) {
        spike_log("edgesnap: zone -> %s\n", es_zone_name((int)r->zone));
    }
    if (r->previewShow && g_cfg.preview && r->previewScreen != NULL) {
        ESRect rect;

        rect.x = (int)r->previewRect.x;
        rect.y = (int)r->previewRect.y;
        rect.w = (int)r->previewRect.w;
        rect.h = (int)r->previewRect.h;
        spike_preview_show(r->previewScreen, &rect);
    }
    if (r->snapped) {
        /*
         * A new pair may have appeared - or an old one changed shape.
         * ChangeWindowBox() returns before the window has moved, so
         * asking immediately finds the window still at its old size
         * and no seam: wait a fifth of a second first.
         */
        Delay(10L);
        spike_divider_sync();
        if (r->snapResult == ES_OK) {
            spike_log("edgesnap: snap %p -> %s\n", (void *)r->snapWindow,
                      es_zone_name((int)r->snapZone));
        } else {
            spike_log("edgesnap: snap %p refused (%ld)\n",
                      (void *)r->snapWindow, (long)r->snapResult);
        }
    }
    if (g_drag_active != r->dragActive) {
        /* Entering a drag: never draw on a screen that has been
         * replaced. Leaving one: a good pen can be had again. */
        spike_accent_recheck();
    }
    g_drag_active = r->dragActive;
}

static void spike_engine_step(void)
{
    int new_press, new_move, new_release;
    struct ESnapReport r;


    new_press = (g_shared.presses != g_seen_presses);
    new_move = (g_shared.moves != g_seen_moves);
    new_release = (g_shared.releases != g_seen_releases);
    g_seen_presses = g_shared.presses;
    g_seen_moves = g_shared.moves;
    g_seen_releases = g_shared.releases;

#ifdef ES_SEAM_DRAG_BLIND
    if (g_divider_dragging) {
        if (new_move && g_divider_scr != NULL) {
            LONG pos = g_divider_vertical ? (LONG)g_divider_scr->MouseX
                                          : (LONG)g_divider_scr->MouseY;
            LONG rc = ES_CALL(ESnap_MoveDividerAt)((LONG)g_divider_vertical,
                                                   (LONG)g_divider_line, pos);

            if (rc == ES_OK) {
                g_divider_line = (int)pos;   /* the seam's name moved */
            } else {
                spike_log("edgesnap: divider gone (%ld)\n", (long)rc);
                g_divider_dragging = 0;
            }
        }
        if (new_release) {
            struct ESnapDivider d;

            g_divider_dragging = 0;
            Delay(10L);                      /* the windows settle */
            spike_divider_sync();
            if (ES_CALL(ESnap_QueryDivider)(ES_DIVIDER_PX, &d) == ES_OK &&
                d.present) {
                spike_divider_pass_focus(&d);
            }
            spike_log_flush();
        }
        return;   /* the windows move because we move them: no engine */
    }
#endif

    /*
     * The frame comes down BEFORE the release reaches the library. The
     * library snaps the window inside that very call, and putting the
     * saved pixels back afterwards races Intuition: where the window
     * has already moved, the restore paints strips of the old
     * background across it. Taken down now, the pixels go back exactly
     * where they were read, with the window still at its old place.
     */
    if (new_release && g_drag_active) {
        spike_preview_hide();
    }

    ES_CALL(ESnap_FeedInput)((ULONG)new_press, (ULONG)new_move,
                             (ULONG)new_release, g_shared.quals, &r);
    spike_apply_report(&r);

    if (g_drag_active) {
        spike_preview_refresh();
    }
    if (new_move && !g_drag_active) {
        spike_divider_hover();
    }

    /*
     * Every released drag can have broken the pair - a window dragged
     * away, resized, or closed. The library checks the pair against
     * the live windows, so asking again here is what makes the handle
     * disappear when the seam stops existing.
     */
    if (new_release && !r.snapped) {
        spike_divider_sync();
        spike_log_flush();
    }

    /* console output only when no drag is in flight (see spike_log) */
    if (!g_drag_active) {
        spike_log_flush();
    }
}

/* ------------------------------------------------------- diagnostics */

/*
 * ctrl alt d: dump every window of the active screen with geometry,
 * flags and the panel-policy verdict, plus the resulting usable area.
 * This is the ground-truth probe for tuning dock detection on real
 * systems: collect under LockIBase, print after unlocking.
 */

#define ES_DUMP_MAX 24

struct WinDumpItem {
    ESRect box;
    ULONG flags;
    char title[28];
    int skipped;  /* 0 candidate, 1 ours, 2 excluded by flags */
};

static void spike_dump_windows(void)
{
    struct WinDumpItem items[ES_DUMP_MAX];
    ESRect usable;
    ESInsets ins;
    ESRect scrrect;
    ULONG ilock;
    struct Screen *scr;
    struct Window *w;
    int n = 0, scrw = 0, scrh = 0, bar = 0, i;

    usable.x = usable.y = usable.w = usable.h = 0;
    ins.l = ins.t = ins.r = ins.b = 0;

    ilock = LockIBase(0);
    scr = ES_IBASE->ActiveScreen;
    if (scr != NULL) {
        scrw = scr->Width;
        scrh = scr->Height;
        bar = scr->BarHeight;
        for (w = scr->FirstWindow; w != NULL && n < ES_DUMP_MAX;
             w = w->NextWindow) {
            struct WinDumpItem *it = &items[n];
            const char *src;
            int t = 0;

            it->box.x = w->LeftEdge;
            it->box.y = w->TopEdge;
            it->box.w = w->Width;
            it->box.h = w->Height;
            it->flags = w->Flags;
            src = (const char *)w->Title;
            if (src != NULL) {
                for (; t < 27 && src[t] != '\0'; t++) {
                    it->title[t] = src[t];
                }
            }
            it->title[t] = '\0';
            if (spike_is_preview_win(w)) {
                it->skipped = 1;
            } else if ((w->Flags & (WFLG_DRAGBAR | WFLG_SIZEGADGET |
                                    WFLG_BACKDROP)) != 0) {
                it->skipped = 2;
            } else {
                it->skipped = 0;
            }
            n++;
        }

    }
    UnlockIBase(ilock);

    /* Outside the lock: the library takes LockIBase itself. */
    if (scr != NULL) {
        struct ESnapArea area;

        if (ES_CALL(ESnap_QueryScreenArea)(scr, &area) == ES_OK) {
            usable.x = (int)area.usable.x;
            usable.y = (int)area.usable.y;
            usable.w = (int)area.usable.w;
            usable.h = (int)area.usable.h;
            ins.l = (int)area.insetLeft;
            ins.t = (int)area.insetTop;
            ins.r = (int)area.insetRight;
            ins.b = (int)area.insetBottom;
        }
    }

    spike_log_flush();
    if (scr == NULL) {
        spike_out("edgesnap: dump: no active screen\n");
        return;
    }
    scrrect.x = 0;
    scrrect.y = 0;
    scrrect.w = scrw;
    scrrect.h = scrh;
    spike_out("edgesnap: --- window dump, screen %dx%d barheight %d ---\n",
           scrw, scrh, bar);
    for (i = 0; i < n; i++) {
        struct WinDumpItem *it = &items[i];
        const char *verdict;
        int depth = 0;

        /*
         * The verdict per window, not just the total. A dump that says
         * only "candidate" leaves the reader to guess which window ate
         * the screen; es_panel_classify() is exposed for exactly this,
         * so a single dump from a puzzled user is now conclusive.
         */
        if (it->skipped == 1) {
            verdict = "ours";
        } else if (it->skipped == 2) {
            verdict = "skip:flags";
        } else {
            verdict = es_panel_edge_name(es_panel_classify(&scrrect,
                                                           &it->box));
            depth = es_panel_depth(&scrrect, &it->box);
        }
        if (depth > 0) {
            spike_out("edgesnap: %4d,%4d %4dx%4d flags %08lx %-11s "
                      "reserves %4d  \"%s\"\n",
                      it->box.x, it->box.y, it->box.w, it->box.h,
                      (unsigned long)it->flags, verdict, depth, it->title);
        } else {
            spike_out("edgesnap: %4d,%4d %4dx%4d flags %08lx %-11s "
                      "              \"%s\"\n",
                      it->box.x, it->box.y, it->box.w, it->box.h,
                      (unsigned long)it->flags, verdict, it->title);
        }
    }
    spike_out("edgesnap: insets l=%d t=%d r=%d b=%d -> usable %d,%d %dx%d\n",
           ins.l, ins.t, ins.r, ins.b,
           usable.x, usable.y, usable.w, usable.h);
    spike_out("edgesnap: input action calls %lu; presses %lu releases %lu; classes seen:"
              " raw-mouse %lu raw-key %lu newpointerpos %lu pointerpos %lu"
              " other %lu\n",
              (unsigned long)g_shared.diag_calls,
              (unsigned long)g_shared.presses,
              (unsigned long)g_shared.releases,
              (unsigned long)g_shared.diag_class[IECLASS_RAWMOUSE & 15],
              (unsigned long)g_shared.diag_class[IECLASS_RAWKEY & 15],
              (unsigned long)g_shared.diag_class[IECLASS_NEWPOINTERPOS & 15],
              (unsigned long)g_shared.diag_class[IECLASS_POINTERPOS & 15],
              (unsigned long)(g_shared.diag_class[IECLASS_TIMER & 15] +
                              g_shared.diag_class[IECLASS_GADGETDOWN & 15] +
                              g_shared.diag_class[IECLASS_GADGETUP & 15] +
                              g_shared.diag_class[IECLASS_MENULIST & 15] +
                              g_shared.diag_class[IECLASS_DISKREMOVED & 15]));
    fflush(stdout);
}

/* --------------------------------------------- the library we drive */

/*
 * Something went wrong badly enough that EdgeSnap will not run, and the
 * user has to be told SOMEWHERE.
 *
 * The startup line is "Run >NIL: C:EdgeSnap", which is right - nobody
 * wants a console at boot - but it also throws away every word we
 * write. A MorphOS user reported EdgeSnap simply doing nothing after an
 * install, with no way to find out why, and there was none: every
 * failure path here printed to a console that was not there.
 *
 * A requester is intrusive, which is exactly what is wanted for a
 * program that has decided not to start. It appears once, never during
 * normal use.
 */
static void spike_fatal(const char *text)
{
    struct EasyStruct es;

    spike_out("edgesnap: %s\n", text);
    if (IntuitionBase == NULL) {
        return;                 /* too early: nothing to draw on */
    }
    es.es_StructSize = sizeof(es);
    es.es_Flags = 0;
    es.es_Title = (CONST_STRPTR)"EdgeSnap";
    es.es_TextFormat = (CONST_STRPTR)"%s";
    es.es_GadgetFormat = (CONST_STRPTR)"OK";
    EasyRequestArgs(NULL, &es, NULL, (APTR)&text);
}

#if defined(ES_STATIC_CORE)
static int spike_open_edgesnap(void)
{
    /* The body wants Intuition open, which the commodity has done by
     * now, and its own init: the same two things the library glue
     * does in LibOpen(). */
    if (!esb_init()) {
        spike_fatal("EdgeSnap could not initialise its snapping core.");
        return 0;
    }
    return 1;
}

static void spike_close_edgesnap(void)
{
    esb_cleanup();
}
#else
static int spike_open_edgesnap(void)
{
    EdgeSnapBase = OpenLibrary("edgesnap.library", ES_API_VERSION);
    if (EdgeSnapBase == NULL) {
        spike_fatal("edgesnap.library could not be opened.\n\n"
                    "It belongs in LIBS:. If EdgeSnap was installed and "
                    "this still happens, the installation did not "
                    "finish.");
        return 0;
    }
    /*
     * Version 2 is not enough: this commodity calls vectors appended
     * after 2.2, and OpenLibrary() checks only the version. A library
     * that is 2.x but older than the vectors we use would take the call
     * into a jump table entry that is not there - on MorphOS a 68k
     * illegal instruction at PC 0x4e, which is how an installer that
     * replaced the commodity but kept a same-version library crashed
     * on the first seam drag (2026-09-02).
     */
    if (EdgeSnapBase->lib_Revision < ES_LIB_MIN_REVISION) {
        spike_fatal("edgesnap.library is older than this EdgeSnap "
                    "needs.\n\nInstall the package again: the library "
                    "in LIBS: must be replaced along with the "
                    "commodity.");
        CloseLibrary(EdgeSnapBase);
        EdgeSnapBase = NULL;
        return 0;
    }
#ifdef __amigaos4__
    IEdgeSnap = (struct EdgeSnapIFace *)
        GetInterface(EdgeSnapBase, "main", 1, NULL);
    if (IEdgeSnap == NULL) {
        spike_fatal("edgesnap.library is there but has no main "
                    "interface.\n\nIt is probably an older version than "
                    "this EdgeSnap needs: install the package again.");
        CloseLibrary(EdgeSnapBase);
        EdgeSnapBase = NULL;
        return 0;
    }
#endif
    return 1;
}

static void spike_close_edgesnap(void)
{
#ifdef __amigaos4__
    if (IEdgeSnap != NULL) {
        DropInterface((struct Interface *)IEdgeSnap);
        IEdgeSnap = NULL;
    }
#endif
    if (EdgeSnapBase != NULL) {
        CloseLibrary(EdgeSnapBase);
        EdgeSnapBase = NULL;
    }
}
#endif /* ES_STATIC_CORE */

/*
 * Preferences are a frontend concern (files, arguments, tooltypes), so
 * the commodity parses them and hands the library the result as tags -
 * the same road any other client would take.
 */
static void spike_push_config(void)
{
    struct TagItem tags[14];
    int n = 0;

    tags[n].ti_Tag = ES_OPT_EdgePx;
    tags[n++].ti_Data = (ULONG)g_cfg.engine.edge_px;
    tags[n].ti_Tag = ES_OPT_CornerDiv;
    tags[n++].ti_Data = (ULONG)g_cfg.engine.corner_div;
    tags[n].ti_Tag = ES_OPT_DragMinPx;
    tags[n++].ti_Data = (ULONG)g_cfg.engine.drag_min_px;
    tags[n].ti_Tag = ES_OPT_Zones;
    tags[n++].ti_Data = (ULONG)g_cfg.engine.zones_mask;
    tags[n].ti_Tag = ES_OPT_MarginLeft;
    tags[n++].ti_Data = (ULONG)g_cfg.margin.l;
    tags[n].ti_Tag = ES_OPT_MarginTop;
    tags[n++].ti_Data = (ULONG)g_cfg.margin.t;
    tags[n].ti_Tag = ES_OPT_MarginRight;
    tags[n++].ti_Data = (ULONG)g_cfg.margin.r;
    tags[n].ti_Tag = ES_OPT_MarginBottom;
    tags[n++].ti_Data = (ULONG)g_cfg.margin.b;
    tags[n].ti_Tag = ES_OPT_PanelDetect;
    tags[n++].ti_Data = (ULONG)g_cfg.panel_detect;
    tags[n].ti_Tag = ES_OPT_PanelMargin;
    tags[n++].ti_Data = (ULONG)g_cfg.panel_margin;
    tags[n].ti_Tag = ES_OPT_Preview;
    tags[n++].ti_Data = (ULONG)g_cfg.preview;
    tags[n].ti_Tag = ES_OPT_BypassQual;
    tags[n++].ti_Data = (ULONG)g_cfg.bypass_qual;
    tags[n].ti_Tag = TAG_DONE;
    tags[n].ti_Data = 0;

    if (ES_CALL(ESnap_SetOptionsA)(tags) != ES_OK) {
        spike_out("edgesnap: the library refused some preferences\n");
    }
}

/*
 * Preferences changed under us. dos.library tells us the file was
 * written; who wrote it does not matter - the preferences window, an
 * editor, or a script - so there is nothing to co-ordinate with and no
 * protocol to get wrong.
 */
static void spike_config_reload(void)
{
    spike_config_load(g_argc, g_argv);
    spike_push_config();
    spike_out("edgesnap: preferences reloaded: zones %04x, edge %d px, "
              "corner 1/%d, drag %d px,\n",
              (unsigned)g_cfg.engine.zones_mask, g_cfg.engine.edge_px,
              g_cfg.engine.corner_div, g_cfg.engine.drag_min_px);
    spike_out("edgesnap:        preview %s, panel detect %s (margin %d)\n",
              g_cfg.preview ? "on" : "off",
              g_cfg.panel_detect ? "on" : "off", g_cfg.panel_margin);
}

/* ------------------------------------------------------------ commodity */

#define HK_SNAP_LEFT   1
#define HK_SNAP_RIGHT  2
#define HK_SNAP_MAX    3
#define HK_RESTORE     4
#define HK_DUMP        5
#define HK_FRAME       6   /* ctrl alt f: the preview frame, no drag */

static int spike_add_hotkey(CxObj *broker, struct MsgPort *port,
                            STRPTR descr, LONG id)
{
    CxObj *filter;

    filter = CxFilter(descr);
    if (filter == NULL) {
        return 0;
    }
    AttachCxObj(filter, CxSender(port, id));
    AttachCxObj(filter, CxTranslate(NULL));
    if (CxObjError(filter) != 0) {
        DeleteCxObjAll(filter);
        return 0;
    }
    AttachCxObj(broker, filter);
    return 1;
}

static void spike_divider_sync(void);

/* Active window, for the hotkeys: the library re-validates it anyway. */
static struct Window *spike_active_window(void)
{
    ULONG ilock;
    struct Window *win;

    ilock = LockIBase(0);
    win = ES_IBASE->ActiveWindow;
    UnlockIBase(ilock);
    return win;
}

static void spike_handle_hotkey(LONG id)
{
    struct Window *win;
    LONG rc = ES_OK;

    if (id == HK_FRAME) {
        /*
         * Diagnostic: the preview frame with no drag in flight. Shows
         * here and not during a drag: the drag hides it (locked layers,
         * an outline redrawn over it). Shows nowhere: the drawing is at
         * fault. Reported from AROS, where the frame was never seen.
         */
        struct Screen *ps = LockPubScreen(NULL);
        ESRect box;

        box.x = 120;
        box.y = 120;
        box.w = 400;
        box.h = 300;
        if (ps != NULL) {
            spike_preview_show(ps, &box);
            spike_log_flush();
            Delay(60L);
            spike_preview_hide();
            UnlockPubScreen(NULL, ps);
        }
        return;
    }
    if (id == HK_DUMP) {
        spike_dump_windows();
        return;
    }
    win = spike_active_window();
    if (win == NULL) {
        spike_out("edgesnap: no active window\n");
        return;
    }
    switch (id) {
    case HK_SNAP_LEFT:
        rc = ES_CALL(ESnap_SnapWindow)(win, ES_ZONE_LEFT);
        break;
    case HK_SNAP_RIGHT:
        rc = ES_CALL(ESnap_SnapWindow)(win, ES_ZONE_RIGHT);
        break;
    case HK_SNAP_MAX:
        rc = ES_CALL(ESnap_SnapWindow)(win, ES_ZONE_MAX);
        break;
    case HK_RESTORE:
        rc = ES_CALL(ESnap_UnsnapWindow)(win);
        break;
    default:
        return;
    }
    /* A hotkey changes the snapped set exactly as a drag does, so the
     * divider has to be re-checked here too - forgetting this is why
     * the handle appeared after drags but never after a hotkey. */
    spike_divider_sync();
    spike_log_flush();
    if (rc != ES_OK) {
        spike_out("edgesnap: hotkey refused (%ld)\n", (long)rc);
    }
}

static void spike_close_libs(void)
{
#ifdef ES_PREVIEW_XOR
    if (CyberGfxBase != NULL) {
        CloseLibrary(CyberGfxBase);
        CyberGfxBase = NULL;
    }
#endif
#ifdef __amigaos4__
    if (ICommodities != NULL) {
        DropInterface((struct Interface *)ICommodities);
        ICommodities = NULL;
    }
    if (IGraphics != NULL) {
        DropInterface((struct Interface *)IGraphics);
        IGraphics = NULL;
    }
    if (IIntuition != NULL) {
        DropInterface((struct Interface *)IIntuition);
        IIntuition = NULL;
    }
#endif
    if (CxBase != NULL) {
        CloseLibrary(CxBase);
        CxBase = NULL;
    }
    if (GfxBase != NULL) {
        CloseLibrary((struct Library *)GfxBase);
        GfxBase = NULL;
    }
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
}

static int spike_open_libs(void)
{
    IntuitionBase = (void *)OpenLibrary("intuition.library", 36);
    GfxBase = (void *)OpenLibrary("graphics.library", 36);
#ifdef ES_PREVIEW_XOR
    CyberGfxBase = OpenLibrary("cybergraphics.library", 41);
    if (CyberGfxBase == NULL) {
        return 0;
    }
#endif
    CxBase = OpenLibrary("commodities.library", 37);
    if (IntuitionBase == NULL || GfxBase == NULL || CxBase == NULL) {
        spike_close_libs();
        return 0;
    }
#ifdef __amigaos4__
    IIntuition = (struct IntuitionIFace *)
        GetInterface((struct Library *)IntuitionBase, "main", 1, NULL);
    IGraphics = (struct GraphicsIFace *)
        GetInterface((struct Library *)GfxBase, "main", 1, NULL);
    ICommodities = (struct CommoditiesIFace *)
        GetInterface(CxBase, "main", 1, NULL);
    if (IIntuition == NULL || IGraphics == NULL || ICommodities == NULL) {
        spike_close_libs();
        return 0;
    }
#endif
    return 1;
}

int main(int argc, char **argv)
{
    struct MsgPort *port = NULL;
    CxObj *broker = NULL;
    CxObj *custom;
    struct NewBroker nb;
    LONG broker_err = 0;
    BYTE engine_sig = -1;
    BYTE prefs_sig = -1;
    struct NotifyRequest prefs_notify;
    int prefs_watched = 0;
    ULONG port_mask, engine_mask, prefs_mask = 0UL;
    int running = 1;
    int rc = RETURN_FAIL;

    (void)es_stack_cookie;
    (void)es_version_cookie;
    spike_config_load(argc, argv);

    if (!spike_open_libs()) {
        spike_out("edgesnap: need intuition/graphics.library 36 and "
               "commodities.library 37\n");
        return RETURN_FAIL;
    }

    port = CreateMsgPort();
    if (port == NULL) {
        goto out;
    }
    engine_sig = AllocSignal(-1);
    if (engine_sig == -1) {
        goto out;
    }
    prefs_sig = AllocSignal(-1);
    if (prefs_sig != -1) {
        memset(&prefs_notify, 0, sizeof(prefs_notify));
        prefs_notify.nr_Name = (STRPTR)ES_PREFS_ENV;
        prefs_notify.nr_Flags = NRF_SEND_SIGNAL;
        prefs_notify.nr_stuff.nr_Signal.nr_Task = FindTask(NULL);
        prefs_notify.nr_stuff.nr_Signal.nr_SignalNum = (UBYTE)prefs_sig;
        if (StartNotify(&prefs_notify)) {
            prefs_watched = 1;
            prefs_mask = 1UL << prefs_sig;
        }
    }

    memset(&nb, 0, sizeof(nb));
    nb.nb_Version = NB_VERSION;
    nb.nb_Name = (STRPTR)"EdgeSnap";
    nb.nb_Title = (STRPTR)"EdgeSnap " ES_VERSION " by Michele Dipace";
    nb.nb_Descr = (STRPTR)"Drag windows to screen edges to tile them";
    nb.nb_Unique = NBU_UNIQUE | NBU_NOTIFY;
    nb.nb_Pri = 0;
    nb.nb_Port = port;

    if (g_quit_mode) {
        /* The note has to be in place before we knock. */
        SetVar((STRPTR)ES_QUIT_VAR, (STRPTR)"1", -1, GVF_GLOBAL_ONLY);
    }

    broker = CxBroker(&nb, &broker_err);

    if (g_quit_mode) {
        /*
         * QUIT does its work by the act of registering: NBU_UNIQUE tells
         * commodities.library to refuse us, and NBU_NOTIFY makes it tell
         * the instance that got there first - which stops. So there is
         * nothing to do here but report which of the two happened, and
         * leave nothing of our own behind.
         */
        if (broker == NULL && broker_err == CBERR_DUP) {
            Delay(25L); /* half a second: let it read the note */
            spike_out("edgesnap: asked the running EdgeSnap to quit\n");
        } else {
            spike_out("edgesnap: EdgeSnap was not running\n");
        }
        /* Never leave the note lying about: a stale one would turn the
         * next accidental double-start into a shutdown. */
        DeleteVar((STRPTR)ES_QUIT_VAR, GVF_GLOBAL_ONLY);
        rc = RETURN_OK;
        goto out;
    }

    if (broker == NULL) {
        if (broker_err != CBERR_DUP) {
            spike_fatal("EdgeSnap could not register itself with "
                        "Commodities.\n\nIt will not run. Exchange and "
                        "commodities.library are what it needs.");
        } else {
            spike_out("edgesnap: CxBroker failed (%ld) - already "
                      "running\n", (long)broker_err);
        }
        /*
         * Finding ourselves already running is the intended outcome of
         * a second launch, not a failure - and returning one made every
         * boot of a machine with two start lines put up an "EdgeSnap
         * failed with code 20" window across the screen.
         */
        if (broker_err == CBERR_DUP) {
            rc = RETURN_OK;
        }
        goto out;
    }

    custom = CxCustom(ES_CX_ACTION, 0);
    AttachCxObj(broker, custom);
    if (!spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_left",
                          HK_SNAP_LEFT) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_right",
                          HK_SNAP_RIGHT) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_up",
                          HK_SNAP_MAX) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt cursor_down",
                          HK_RESTORE) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt d", HK_DUMP) ||
        !spike_add_hotkey(broker, port, (STRPTR)"ctrl alt f", HK_FRAME) ||
        CxObjError(broker) != 0) {
        spike_out("edgesnap: could not build the commodity tree\n");
        goto out;
    }

    g_shared.engine_task = FindTask(NULL);
    g_shared.engine_sigmask = 1UL << engine_sig;
    g_shared.armed = 1;
    if (!spike_open_edgesnap()) {
        goto out;
    }
    spike_push_config();
    spike_publish_ignored();

    /* The accent colour, before any drag can ask for one. */
    if (!spike_accent_init()) {
        spike_out("edgesnap: no public screen, nothing will be drawn\n");
    }
#if defined(ES_PREVIEW_PIXELS) || defined(ES_PREVIEW_XOR)
    if (!spike_pf_init()) {
        spike_out("edgesnap: no public screen, no preview frame\n");
    }
#endif

    ActivateCxObj(broker, 1L);

    spike_out("EdgeSnap " ES_VERSION " (build " __DATE__ " " __TIME__ ") "
           "running (commodity \"EdgeSnap\").\n");
    spike_out("  Copyright (c) 2026 Michele Dipace "
           "<michele.dipace@kaffeine.net>, MIT license.\n");
    spike_out("  drag a window's title bar until the pointer touches a\n");
    spike_out("  screen edge or corner, then release.\n");
    spike_out("  hotkeys: ctrl alt cursor left/right/up = snap, down = "
           "restore,\n");
    spike_out("           ctrl alt d = window dump (dock diagnosis).\n");
    spike_out("  quit: Ctrl-C here, or remove it from Exchange.\n");
    spike_out("edgesnap: prefs: zones %04x, edge %d px, corner 1/%d, "
           "drag %d px,\n", (unsigned)g_cfg.engine.zones_mask,
           g_cfg.engine.edge_px, g_cfg.engine.corner_div,
           g_cfg.engine.drag_min_px);
    spike_out("edgesnap:        preview %s, panel detect %s (margin %d), "
           "bypass %s,\n", g_cfg.preview ? "on" : "off",
           g_cfg.panel_detect ? "on" : "off", g_cfg.panel_margin,
           g_cfg.bypass_qual == ES_QUAL_ALT ? "alt" :
           g_cfg.bypass_qual == ES_QUAL_CTRL ? "ctrl" :
           g_cfg.bypass_qual == ES_QUAL_SHIFT ? "shift" : "none");
    spike_out("edgesnap:        margins l%d t%d r%d b%d "
           "(file: " ES_PREFS_ENV ")\n", g_cfg.margin.l, g_cfg.margin.t,
           g_cfg.margin.r, g_cfg.margin.b);
    spike_dump_windows();

    port_mask = 1UL << port->mp_SigBit;
    engine_mask = g_shared.engine_sigmask;

    while (running) {
        /* The divider handle comes and goes with the snapped pairs, so
         * its port joins the wait mask fresh on every round. */
        ULONG div_mask = (g_divider != NULL) ?
            (1UL << g_divider->UserPort->mp_SigBit) : 0UL;
        ULONG sigs = Wait(port_mask | engine_mask | div_mask |
                          prefs_mask | SIGBREAKF_CTRL_C);

        if (prefs_mask != 0UL && (sigs & prefs_mask) != 0) {
            spike_config_reload();
        }

        if (div_mask != 0 && (sigs & div_mask) != 0) {
            spike_divider_events();
        }

        if (sigs & port_mask) {
            CxMsg *msg;
            while ((msg = (CxMsg *)GetMsg(port)) != NULL) {
                ULONG type = CxMsgType(msg);
                LONG id = CxMsgID(msg);
                ReplyMsg((struct Message *)msg);
                if (type == CXM_IEVENT) {
                    spike_handle_hotkey(id);
                } else if (type == CXM_COMMAND) {
                    switch (id) {
                    case CXCMD_KILL:
                        running = 0;
                        break;
                    case CXCMD_UNIQUE:
                        /* Another instance tried to start. Only "QUIT"
                         * means stop; a plain second launch just gets
                         * refused, and we carry on. */
                        if (spike_quit_requested()) {
                            spike_out("edgesnap: asked to quit\n");
                            running = 0;
                        }
                        break;
                    case CXCMD_DISABLE:
                        ActivateCxObj(broker, 0L);
                        {
                            struct ESnapReport r;

                            ES_CALL(ESnap_Enable)(FALSE);
                            ES_CALL(ESnap_ResetInput)(&r);
                            spike_apply_report(&r);
                        }
                        spike_log_flush();
                        break;
                    case CXCMD_ENABLE:
                        ActivateCxObj(broker, 1L);
                        ES_CALL(ESnap_Enable)(TRUE);
                        break;
                    default:
                        break;
                    }
                }
            }
        }
        if (sigs & engine_mask) {
            spike_engine_step();
        }
        if (sigs & SIGBREAKF_CTRL_C) {
            running = 0;
        }
    }

    rc = RETURN_OK;
    spike_log_flush();
    spike_out("edgesnap: shutting down\n");

out:
    /*
     * Shutdown protocol - the order is the whole point. Our CxCustom
     * action runs in input.device context and may be executing at this
     * very moment:
     *
     *   1. deactivate the broker, so no new event enters our tree;
     *   2. disarm the action and drop the task pointer WHILE WE ARE
     *      STILL ALIVE, so a call already in flight becomes a no-op
     *      and can never Signal a dying task;
     *   3. give that in-flight call time to return, because on OS4 the
     *      code containing it is unloaded when we exit;
     *   4. only then delete the objects and free everything.
     *
     * Skipping step 2 leaves a Signal() aimed at freed memory - the
     * classic way to wedge commodities for every later client, which
     * is what the restart hang looked like from the outside.
     * edgesnap.library inherits this protocol.
     */
    if (broker != NULL) {
        ActivateCxObj(broker, 0L);
    }
    g_shared.armed = 0;
    g_shared.engine_task = NULL;
    if (broker != NULL) {
        Delay(2L); /* ~40 ms: let any in-flight action return */
        DeleteCxObjAll(broker);
    }
    spike_preview_hide();
#if defined(ES_PREVIEW_PIXELS) || defined(ES_PREVIEW_XOR)
    spike_pf_cleanup();
#endif
    spike_divider_close();
    spike_accent_free();
    spike_close_edgesnap();
    if (port != NULL) {
        CxMsg *msg;
        while ((msg = (CxMsg *)GetMsg(port)) != NULL) {
            ReplyMsg((struct Message *)msg);
        }
        DeleteMsgPort(port);
    }
    if (prefs_watched) {
        EndNotify(&prefs_notify);
    }
    if (prefs_sig != -1) {
        FreeSignal(prefs_sig);
    }
    if (engine_sig != -1) {
        FreeSignal(engine_sig);
    }
    spike_close_libs();
    return rc;
}
