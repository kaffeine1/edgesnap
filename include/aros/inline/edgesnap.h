#ifndef INLINE_EDGESNAP_H
#define INLINE_EDGESNAP_H

/*
    *** Automatically generated from 'library/aros/edgesnap.conf'. Edits will be lost. ***
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
*/

/*
    Desc: Inline function(s) for edgesnap
*/

#include <aros/libcall.h>
#include <exec/types.h>
#include <aros/symbolsets.h>
#include <aros/preprocessor/variadic/cast2iptr.hpp>

#if !defined(__EDGESNAP_LIBBASE)
#define __EDGESNAP_LIBBASE __aros_getbase_EdgeSnapBase()
#endif
#ifndef __aros_getbase_EdgeSnapBase
extern struct Library *__aros_getbase_EdgeSnapBase(void);
#endif


#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline ULONG __inline_EdgeSnap_ESnap_QueryCapabilities(APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC0(ULONG, ESnap_QueryCapabilities,\
        struct Library *, (__EdgeSnapBase), 5, EdgeSnap    );
}

#define ESnap_QueryCapabilities() \
    __inline_EdgeSnap_ESnap_QueryCapabilities(__EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_SnapWindow(struct Window * __arg1, ULONG __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_SnapWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(ULONG, (__arg2), D0), \
        struct Library *, (__EdgeSnapBase), 6, EdgeSnap    );
}

#define ESnap_SnapWindow(arg1, arg2) \
    __inline_EdgeSnap_ESnap_SnapWindow((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_UnsnapWindow(struct Window * __arg1, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC1(LONG, ESnap_UnsnapWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 7, EdgeSnap    );
}

#define ESnap_UnsnapWindow(arg1) \
    __inline_EdgeSnap_ESnap_UnsnapWindow((arg1), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_QueryWindow(struct Window * __arg1, ULONG * __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_QueryWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(ULONG *, (__arg2), A1), \
        struct Library *, (__EdgeSnapBase), 8, EdgeSnap    );
}

#define ESnap_QueryWindow(arg1, arg2) \
    __inline_EdgeSnap_ESnap_QueryWindow((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_ExcludeWindow(struct Window * __arg1, BOOL __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_ExcludeWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(BOOL, (__arg2), D0), \
        struct Library *, (__EdgeSnapBase), 9, EdgeSnap    );
}

#define ESnap_ExcludeWindow(arg1, arg2) \
    __inline_EdgeSnap_ESnap_ExcludeWindow((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_SetOptionsA(const struct TagItem * __arg1, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC1(LONG, ESnap_SetOptionsA,\
         AROS_LCA(const struct TagItem *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 10, EdgeSnap    );
}

#define ESnap_SetOptionsA(arg1) \
    __inline_EdgeSnap_ESnap_SetOptionsA((arg1), __EDGESNAP_LIBBASE)

#if !defined(NO_INLINE_STDARG) && !defined(EDGESNAP_NO_INLINE_STDARG)
#define ESnap_SetOptions(...) \
({ \
    const IPTR ESnap_SetOptionsA_args[] = { AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) };\
    ESnap_SetOptionsA((const struct TagItem *)(ESnap_SetOptionsA_args)); \
})
#endif /* !NO_INLINE_STDARG */

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_Enable(BOOL __arg1, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC1(LONG, ESnap_Enable,\
         AROS_LCA(BOOL, (__arg1), D0), \
        struct Library *, (__EdgeSnapBase), 11, EdgeSnap    );
}

#define ESnap_Enable(arg1) \
    __inline_EdgeSnap_ESnap_Enable((arg1), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline void __inline_EdgeSnap_ESnap_FeedInput(ULONG __arg1, ULONG __arg2, ULONG __arg3, ULONG __arg4, struct ESnapReport * __arg5, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    AROS_LC5NR(void, ESnap_FeedInput,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(ULONG, (__arg2), D1), \
         AROS_LCA(ULONG, (__arg3), D2), \
         AROS_LCA(ULONG, (__arg4), D3), \
         AROS_LCA(struct ESnapReport *, (__arg5), A0), \
        struct Library *, (__EdgeSnapBase), 12, EdgeSnap    );
}

#define ESnap_FeedInput(arg1, arg2, arg3, arg4, arg5) \
    __inline_EdgeSnap_ESnap_FeedInput((arg1), (arg2), (arg3), (arg4), (arg5), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline void __inline_EdgeSnap_ESnap_ResetInput(struct ESnapReport * __arg1, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    AROS_LC1NR(void, ESnap_ResetInput,\
         AROS_LCA(struct ESnapReport *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 13, EdgeSnap    );
}

#define ESnap_ResetInput(arg1) \
    __inline_EdgeSnap_ESnap_ResetInput((arg1), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_IgnoreWindows(struct Window ** __arg1, ULONG __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_IgnoreWindows,\
         AROS_LCA(struct Window **, (__arg1), A0), \
         AROS_LCA(ULONG, (__arg2), D0), \
        struct Library *, (__EdgeSnapBase), 14, EdgeSnap    );
}

#define ESnap_IgnoreWindows(arg1, arg2) \
    __inline_EdgeSnap_ESnap_IgnoreWindows((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_QueryScreenArea(struct Screen * __arg1, struct ESnapArea * __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_QueryScreenArea,\
         AROS_LCA(struct Screen *, (__arg1), A0), \
         AROS_LCA(struct ESnapArea *, (__arg2), A1), \
        struct Library *, (__EdgeSnapBase), 15, EdgeSnap    );
}

#define ESnap_QueryScreenArea(arg1, arg2) \
    __inline_EdgeSnap_ESnap_QueryScreenArea((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_QueryDivider(ULONG __arg1, struct ESnapDivider * __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_QueryDivider,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(struct ESnapDivider *, (__arg2), A0), \
        struct Library *, (__EdgeSnapBase), 16, EdgeSnap    );
}

#define ESnap_QueryDivider(arg1, arg2) \
    __inline_EdgeSnap_ESnap_QueryDivider((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_MoveDivider(LONG __arg1, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC1(LONG, ESnap_MoveDivider,\
         AROS_LCA(LONG, (__arg1), D0), \
        struct Library *, (__EdgeSnapBase), 17, EdgeSnap    );
}

#define ESnap_MoveDivider(arg1) \
    __inline_EdgeSnap_ESnap_MoveDivider((arg1), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_QueryDividerAt(ULONG __arg1, LONG __arg2, LONG __arg3, struct ESnapDivider * __arg4, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC4(LONG, ESnap_QueryDividerAt,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(LONG, (__arg2), D1), \
         AROS_LCA(LONG, (__arg3), D2), \
         AROS_LCA(struct ESnapDivider *, (__arg4), A0), \
        struct Library *, (__EdgeSnapBase), 18, EdgeSnap    );
}

#define ESnap_QueryDividerAt(arg1, arg2, arg3, arg4) \
    __inline_EdgeSnap_ESnap_QueryDividerAt((arg1), (arg2), (arg3), (arg4), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_MoveDividerAt(LONG __arg1, LONG __arg2, LONG __arg3, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC3(LONG, ESnap_MoveDividerAt,\
         AROS_LCA(LONG, (__arg1), A0), \
         AROS_LCA(LONG, (__arg2), A1), \
         AROS_LCA(LONG, (__arg3), D0), \
        struct Library *, (__EdgeSnapBase), 19, EdgeSnap    );
}

#define ESnap_MoveDividerAt(arg1, arg2, arg3) \
    __inline_EdgeSnap_ESnap_MoveDividerAt((arg1), (arg2), (arg3), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_QueryWindows(struct Screen * __arg1, struct ESnapWindowInfo * __arg2, ULONG __arg3, ULONG * __arg4, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC4(LONG, ESnap_QueryWindows,\
         AROS_LCA(struct Screen *, (__arg1), A0), \
         AROS_LCA(struct ESnapWindowInfo *, (__arg2), A1), \
         AROS_LCA(ULONG, (__arg3), D0), \
         AROS_LCA(ULONG *, (__arg4), A2), \
        struct Library *, (__EdgeSnapBase), 20, EdgeSnap    );
}

#define ESnap_QueryWindows(arg1, arg2, arg3, arg4) \
    __inline_EdgeSnap_ESnap_QueryWindows((arg1), (arg2), (arg3), (arg4), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline ULONG __inline_EdgeSnap_ESnap_QueryGeneration(struct Screen * __arg1, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC1(ULONG, ESnap_QueryGeneration,\
         AROS_LCA(struct Screen *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 21, EdgeSnap    );
}

#define ESnap_QueryGeneration(arg1) \
    __inline_EdgeSnap_ESnap_QueryGeneration((arg1), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_PlaceWindow(struct Window * __arg1, const struct ESnapRect * __arg2, ULONG __arg3, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC3(LONG, ESnap_PlaceWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(const struct ESnapRect *, (__arg2), A1), \
         AROS_LCA(ULONG, (__arg3), D0), \
        struct Library *, (__EdgeSnapBase), 22, EdgeSnap    );
}

#define ESnap_PlaceWindow(arg1, arg2, arg3) \
    __inline_EdgeSnap_ESnap_PlaceWindow((arg1), (arg2), (arg3), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_PlaceWindowsA(struct ESnapPlacement * __arg1, ULONG __arg2, ULONG __arg3, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC3(LONG, ESnap_PlaceWindowsA,\
         AROS_LCA(struct ESnapPlacement *, (__arg1), A0), \
         AROS_LCA(ULONG, (__arg2), D0), \
         AROS_LCA(ULONG, (__arg3), D1), \
        struct Library *, (__EdgeSnapBase), 23, EdgeSnap    );
}

#define ESnap_PlaceWindowsA(arg1, arg2, arg3) \
    __inline_EdgeSnap_ESnap_PlaceWindowsA((arg1), (arg2), (arg3), __EDGESNAP_LIBBASE)

#if !defined(NO_INLINE_STDARG) && !defined(EDGESNAP_NO_INLINE_STDARG)
#define ESnap_PlaceWindows(arg1, arg2, ...) \
({ \
    const IPTR ESnap_PlaceWindowsA_args[] = { AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) };\
    ESnap_PlaceWindowsA((arg1), (arg2), (ULONG)(ESnap_PlaceWindowsA_args)); \
})
#endif /* !NO_INLINE_STDARG */

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_QueryWindowSerial(struct Window * __arg1, ULONG * __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_QueryWindowSerial,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(ULONG *, (__arg2), A1), \
        struct Library *, (__EdgeSnapBase), 24, EdgeSnap    );
}

#define ESnap_QueryWindowSerial(arg1, arg2) \
    __inline_EdgeSnap_ESnap_QueryWindowSerial((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

static inline LONG __inline_EdgeSnap_ESnap_FindWindow(ULONG __arg1, struct Window ** __arg2, APTR __EdgeSnapBase)
{
    AROS_LIBREQ(EdgeSnapBase, 2)
    return AROS_LC2(LONG, ESnap_FindWindow,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(struct Window **, (__arg2), A0), \
        struct Library *, (__EdgeSnapBase), 25, EdgeSnap    );
}

#define ESnap_FindWindow(arg1, arg2) \
    __inline_EdgeSnap_ESnap_FindWindow((arg1), (arg2), __EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#endif /* INLINE_EDGESNAP_H*/
