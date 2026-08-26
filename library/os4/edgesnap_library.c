/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_library.c - AmigaOS 4.x skeleton of edgesnap.library.
 *
 * Pure wrapper: it owns the library node, the interfaces and the
 * library bases, and forwards every method to the shared body in
 * library/edgesnap_body.c. No decision is taken here - that is the
 * whole point of having one body and two skeletons.
 *
 * Lifecycle: the body is initialised on the first Open and torn down
 * on Expunge, so several clients share one engine and one registry
 * (which is exactly why the body serialises everything internally).
 */

#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif

#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/resident.h>
#include <exec/interfaces.h>
#include <dos/dos.h>
#include <intuition/intuition.h>
#include <utility/tagitem.h>

#include <proto/exec.h>
#include <proto/intuition.h>

/* NOTE: with __USE_INLINE__ the SDK macros already route these calls
 * through the global IExec/IIntuition, so they are written plain. */

#include "edgesnap.h"
#include "edgesnap_body.h"
#include "interfaces/edgesnap.h"

#define ES_LIB_NAME    "edgesnap.library"
#define ES_LIB_VERSION 0
#define ES_LIB_REVISION 2
#define ES_LIB_IDSTRING "edgesnap.library 0.2 (26.8.2026)"

/* Bases used by the body through the SDK's inline macros. */
struct Library *IntuitionBase;
struct IntuitionIFace *IIntuition;
struct ExecIFace *IExec;

struct EdgeSnapBase {
    struct Library libNode;
    BPTR segList;
};

/*
 * The compiler emits memcpy/memset for ordinary structure assignment,
 * and pulling them from newlib would make the library depend on a
 * program's C runtime (INewlib). A shared library must stand on its
 * own, so they live here. Built with -fno-tree-loop-distribute-patterns
 * so GCC cannot turn these very loops back into calls to themselves.
 */
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

/* A library is not a program: refuse to be started. */
int32 _start(void)
{
    return RETURN_FAIL;
}

/* ------------------------------------------------------ library node */

static struct Library *libInit(struct Library *base, BPTR seglist,
                               struct ExecIFace *iexec)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)base;

    IExec = iexec;
    esb->libNode.lib_Node.ln_Type = NT_LIBRARY;
    esb->libNode.lib_Node.ln_Pri = 0;
    esb->libNode.lib_Node.ln_Name = (STRPTR)ES_LIB_NAME;
    esb->libNode.lib_Flags = LIBF_SUMUSED | LIBF_CHANGED;
    esb->libNode.lib_Version = ES_LIB_VERSION;
    esb->libNode.lib_Revision = ES_LIB_REVISION;
    esb->libNode.lib_IdString = (STRPTR)ES_LIB_IDSTRING;
    esb->segList = seglist;
    return base;
}

static BPTR libExpunge(struct LibraryManagerInterface *Self)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)Self->Data.LibBase;
    BPTR result = ZERO;

    if (esb->libNode.lib_OpenCnt == 0) {
        esb_cleanup();
        if (IIntuition != NULL) {
            DropInterface((struct Interface *)IIntuition);
            IIntuition = NULL;
        }
        if (IntuitionBase != NULL) {
            CloseLibrary(IntuitionBase);
            IntuitionBase = NULL;
        }
        result = esb->segList;
        Remove(&esb->libNode.lib_Node);
        DeleteLibrary(&esb->libNode);
    } else {
        esb->libNode.lib_Flags |= LIBF_DELEXP;
    }
    return result;
}

static struct Library *libOpen(struct LibraryManagerInterface *Self,
                               ULONG version)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)Self->Data.LibBase;

    if (version > ES_LIB_VERSION) {
        return NULL;
    }
    if (esb->libNode.lib_OpenCnt == 0) {
        /* First client: bring up what the body needs, then the body. */
        IntuitionBase = OpenLibrary("intuition.library", 36);
        if (IntuitionBase != NULL) {
            IIntuition = (struct IntuitionIFace *)
                GetInterface(IntuitionBase, "main", 1, NULL);
        }
        if (IIntuition == NULL || !esb_init()) {
            if (IIntuition != NULL) {
                DropInterface((struct Interface *)IIntuition);
                IIntuition = NULL;
            }
            if (IntuitionBase != NULL) {
                CloseLibrary(IntuitionBase);
                IntuitionBase = NULL;
            }
            return NULL;
        }
    }
    esb->libNode.lib_OpenCnt++;
    esb->libNode.lib_Flags &= ~LIBF_DELEXP;
    return &esb->libNode;
}

static BPTR libClose(struct LibraryManagerInterface *Self)
{
    struct EdgeSnapBase *esb = (struct EdgeSnapBase *)Self->Data.LibBase;

    if (esb->libNode.lib_OpenCnt > 0) {
        esb->libNode.lib_OpenCnt--;
    }
    if (esb->libNode.lib_OpenCnt == 0 &&
        (esb->libNode.lib_Flags & LIBF_DELEXP)) {
        return libExpunge(Self);
    }
    return ZERO;
}

static ULONG libObtain(struct LibraryManagerInterface *Self)
{
    return Self->Data.RefCount++;
}

static ULONG libRelease(struct LibraryManagerInterface *Self)
{
    return Self->Data.RefCount--;
}

static CONST APTR lib_manager_vectors[] = {
    (APTR)libObtain,
    (APTR)libRelease,
    NULL,
    NULL,
    (APTR)libOpen,
    (APTR)libClose,
    (APTR)libExpunge,
    NULL,
    (APTR)-1
};

static CONST struct TagItem lib_managerTags[] = {
    { MIT_Name,        (ULONG)"__library"          },
    { MIT_VectorTable, (ULONG)lib_manager_vectors  },
    { MIT_Version,     1                           },
    { TAG_DONE,        0                           }
};

/* ------------------------------------------------------ main interface */

static ULONG ifObtain(struct EdgeSnapIFace *Self)
{
    return Self->Data.RefCount++;
}

static ULONG ifRelease(struct EdgeSnapIFace *Self)
{
    return Self->Data.RefCount--;
}

static ULONG _ESnap_QueryCapabilities(struct EdgeSnapIFace *Self)
{
    (void)Self;
    return esb_query_capabilities();
}

static LONG _ESnap_SnapWindow(struct EdgeSnapIFace *Self,
                              struct Window *win, ULONG zone)
{
    (void)Self;
    return esb_snap_window(win, zone);
}

static LONG _ESnap_UnsnapWindow(struct EdgeSnapIFace *Self,
                                struct Window *win)
{
    (void)Self;
    return esb_unsnap_window(win);
}

static LONG _ESnap_QueryWindow(struct EdgeSnapIFace *Self,
                               struct Window *win, ULONG *zone_out)
{
    (void)Self;
    return esb_query_window(win, zone_out);
}

static LONG _ESnap_ExcludeWindow(struct EdgeSnapIFace *Self,
                                 struct Window *win, BOOL exclude)
{
    (void)Self;
    return esb_exclude_window(win, exclude);
}

static LONG _ESnap_SetOptionsA(struct EdgeSnapIFace *Self,
                               const struct TagItem *tags)
{
    (void)Self;
    return esb_set_options(tags);
}

static LONG _ESnap_Enable(struct EdgeSnapIFace *Self, BOOL on)
{
    (void)Self;
    return esb_enable(on);
}

/* Order is ABI. Append only, never reorder, never remove. */
static CONST APTR lib_main_vectors[] = {
    (APTR)ifObtain,
    (APTR)ifRelease,
    NULL,
    NULL,
    (APTR)_ESnap_QueryCapabilities,
    (APTR)_ESnap_SnapWindow,
    (APTR)_ESnap_UnsnapWindow,
    (APTR)_ESnap_QueryWindow,
    (APTR)_ESnap_ExcludeWindow,
    (APTR)_ESnap_SetOptionsA,
    (APTR)_ESnap_Enable,
    (APTR)-1
};

static CONST struct TagItem lib_mainTags[] = {
    { MIT_Name,        (ULONG)"main"             },
    { MIT_VectorTable, (ULONG)lib_main_vectors   },
    { MIT_Version,     1                         },
    { TAG_DONE,        0                         }
};

static CONST CONST_APTR lib_interfaces[] = {
    lib_managerTags,
    lib_mainTags,
    NULL
};

static CONST struct TagItem lib_createTags[] = {
    { CLT_DataSize,   sizeof(struct EdgeSnapBase) },
    { CLT_InitFunc,   (ULONG)libInit              },
    { CLT_Interfaces, (ULONG)lib_interfaces       },
    { TAG_DONE,       0                           }
};

static CONST struct Resident lib_res __attribute__((used)) = {
    RTC_MATCHWORD,
    (struct Resident *)&lib_res,
    (APTR)(&lib_res + 1),
    RTF_NATIVE | RTF_AUTOINIT,
    ES_LIB_VERSION,
    NT_LIBRARY,
    0,
    (STRPTR)ES_LIB_NAME,
    (STRPTR)ES_LIB_IDSTRING,
    (APTR)lib_createTags
};

static CONST char USED_VAR verstag[] = "$VER: " ES_LIB_IDSTRING;
