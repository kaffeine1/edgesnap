/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_library.c - MorphOS skeleton of edgesnap.library.
 *
 * Same body, different clothes. Where AmigaOS 4 exposes interfaces,
 * MorphOS keeps the classic negative-offset jump table, and every
 * vector is entered through the 68k ABI - so each entry point is an
 * EmulLibEntry gate that picks its arguments out of the emulated
 * registers and calls the shared implementation. The register
 * assignment below IS the ABI: it matches edgesnap_lib.fd and must
 * never be reordered once released.
 *
 * As on OS4 the library links without a C runtime, so it provides the
 * memcpy/memset that the compiler emits for structure assignment.
 */

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/nodes.h>
#include <exec/execbase.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>

#include <emul/emulregs.h>
#include <emul/emulinterface.h>

#include <proto/exec.h>

#include "edgesnap.h"
#include "edgesnap_body.h"

#define ES_LIB_NAME     "edgesnap.library"
#define ES_LIB_VERSION  0
#define ES_LIB_REVISION 2
#define ES_LIB_IDSTRING "edgesnap.library 0.2 (26.8.2026)\r\n"

struct ExecBase *SysBase;
struct IntuitionBase *IntuitionBase;

struct EdgeSnapBase {
    struct Library lib;
    BPTR segList;
};

/* The compiler emits these for structure assignment; a shared library
 * must not depend on a C runtime, so they live here. Built with
 * -fno-tree-loop-distribute-patterns so GCC cannot rewrite the loops
 * into calls to themselves. */
void *memcpy(void *dst, const void *src, unsigned long n);
void *memset(void *dst, int c, unsigned long n);

void *memcpy(void *dst, const void *src, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;
    const unsigned char *s = (const unsigned char *)src;

    while (n-- > 0) {
        *d++ = *s++;
    }
    return dst;
}

void *memset(void *dst, int c, unsigned long n)
{
    unsigned char *d = (unsigned char *)dst;

    while (n-- > 0) {
        *d++ = (unsigned char)c;
    }
    return dst;
}

/* ------------------------------------------------------ library node */

static struct Library *LibInit(struct Library *base, BPTR seglist,
                               struct ExecBase *sysbase);
static ULONG LibOpen(void);
static BPTR LibClose(void);
static BPTR LibExpunge(void);
static ULONG LibReserved(void);

/* RTF_PPC: rt_Init's function is native PPC with this exact signature
 * (see exec/resident.h), so no gate is needed for it. */
static struct Library *LibInit(struct Library *base, BPTR seglist,
                               struct ExecBase *sysbase)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)base;

    SysBase = sysbase;
    esb->lib.lib_Node.ln_Type = NT_LIBRARY;
    esb->lib.lib_Node.ln_Pri = 0;
    esb->lib.lib_Node.ln_Name = (STRPTR)ES_LIB_NAME;
    esb->lib.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    esb->lib.lib_Version = ES_LIB_VERSION;
    esb->lib.lib_Revision = ES_LIB_REVISION;
    esb->lib.lib_IdString = (STRPTR)ES_LIB_IDSTRING;
    esb->segList = seglist;
    return base;
}

static ULONG LibOpen(void)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)REG_A6;

    if (esb->lib.lib_OpenCnt == 0) {
        /* First client: what the body needs, then the body. */
        IntuitionBase = (struct IntuitionBase *)
            OpenLibrary("intuition.library", 36);
        if (IntuitionBase == NULL || !esb_init()) {
            if (IntuitionBase != NULL) {
                CloseLibrary((struct Library *)IntuitionBase);
                IntuitionBase = NULL;
            }
            return 0;
        }
    }
    esb->lib.lib_OpenCnt++;
    esb->lib.lib_Flags &= ~LIBF_DELEXP;
    return (ULONG)esb;
}

static BPTR LibExpungeBase(struct EdgeSnapBase *esb)
{
    BPTR seglist;

    if (esb->lib.lib_OpenCnt != 0) {
        esb->lib.lib_Flags |= LIBF_DELEXP;
        return (BPTR)0;
    }
    esb_cleanup();
    if (IntuitionBase != NULL) {
        CloseLibrary((struct Library *)IntuitionBase);
        IntuitionBase = NULL;
    }
    seglist = esb->segList;
    Remove(&esb->lib.lib_Node);
    FreeMem((APTR)((ULONG)esb - esb->lib.lib_NegSize),
            esb->lib.lib_NegSize + esb->lib.lib_PosSize);
    return seglist;
}

static BPTR LibClose(void)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)REG_A6;

    if (esb->lib.lib_OpenCnt > 0) {
        esb->lib.lib_OpenCnt--;
    }
    if (esb->lib.lib_OpenCnt == 0 && (esb->lib.lib_Flags & LIBF_DELEXP)) {
        return LibExpungeBase(esb);
    }
    return (BPTR)0;
}

static BPTR LibExpunge(void)
{
    return LibExpungeBase((struct EdgeSnapBase *)REG_A6);
}

static ULONG LibReserved(void)
{
    return 0;
}

/* ------------------------------------------- API gates (68k ABI in) */

/*
 * Register assignment - this is the ABI, mirrored in edgesnap_lib.fd:
 *   win     -> a0      zone/on/exclude -> d0
 *   zone_out/tags     -> a1 / a0
 */

static ULONG G_QueryCapabilities(void)
{
    return esb_query_capabilities();
}

static LONG G_SnapWindow(void)
{
    return esb_snap_window((struct Window *)REG_A0, (ULONG)REG_D0);
}

static LONG G_UnsnapWindow(void)
{
    return esb_unsnap_window((struct Window *)REG_A0);
}

static LONG G_QueryWindow(void)
{
    return esb_query_window((struct Window *)REG_A0, (ULONG *)REG_A1);
}

static LONG G_ExcludeWindow(void)
{
    return esb_exclude_window((struct Window *)REG_A0, (BOOL)REG_D0);
}

static LONG G_SetOptionsA(void)
{
    return esb_set_options((const struct TagItem *)REG_A0);
}

static LONG G_Enable(void)
{
    return esb_enable((BOOL)REG_D0);
}

#define ES_GATE(name, fn) \
    static struct EmulLibEntry name = \
        { TRAP_LIB, 0, (void (*)(void))fn }

ES_GATE(GATE_LibOpen, LibOpen);
ES_GATE(GATE_LibClose, LibClose);
ES_GATE(GATE_LibExpunge, LibExpunge);
ES_GATE(GATE_LibReserved, LibReserved);
ES_GATE(GATE_QueryCapabilities, G_QueryCapabilities);
ES_GATE(GATE_SnapWindow, G_SnapWindow);
ES_GATE(GATE_UnsnapWindow, G_UnsnapWindow);
ES_GATE(GATE_QueryWindow, G_QueryWindow);
ES_GATE(GATE_ExcludeWindow, G_ExcludeWindow);
ES_GATE(GATE_SetOptionsA, G_SetOptionsA);
ES_GATE(GATE_Enable, G_Enable);

/* Vector order is the ABI. Append only, never reorder, never remove. */
static const APTR FuncTable[] = {
    (APTR)&GATE_LibOpen,
    (APTR)&GATE_LibClose,
    (APTR)&GATE_LibExpunge,
    (APTR)&GATE_LibReserved,
    (APTR)&GATE_QueryCapabilities,
    (APTR)&GATE_SnapWindow,
    (APTR)&GATE_UnsnapWindow,
    (APTR)&GATE_QueryWindow,
    (APTR)&GATE_ExcludeWindow,
    (APTR)&GATE_SetOptionsA,
    (APTR)&GATE_Enable,
    (APTR)-1
};

static const APTR InitTable[4] = {
    (APTR)sizeof(struct EdgeSnapBase),
    (APTR)FuncTable,
    NULL,
    (APTR)LibInit
};

extern const struct Resident RomTag;

static const char LibName[] = ES_LIB_NAME;
static const char LibIdString[] = ES_LIB_IDSTRING;

const struct Resident RomTag __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&RomTag,
    (APTR)(&RomTag + 1),
    RTF_PPC | RTF_EXTENDED | RTF_AUTOINIT,
    ES_LIB_VERSION,
    NT_LIBRARY,
    0,
    (char *)LibName,
    (char *)LibIdString,
    (APTR)InitTable,
    ES_LIB_REVISION,
    NULL
};

static const char verstag[] __attribute__((used)) =
    "$VER: " ES_LIB_IDSTRING;
