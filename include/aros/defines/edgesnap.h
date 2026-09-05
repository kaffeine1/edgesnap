#ifndef DEFINES_EDGESNAP_H
#define DEFINES_EDGESNAP_H

/*
    *** Automatically generated from 'library/aros/edgesnap.conf'. Edits will be lost. ***
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
*/

/*
    Desc: Defines for edgesnap
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

__BEGIN_DECLS


#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryCapabilities_WB(__EdgeSnapBase) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC0(ULONG, ESnap_QueryCapabilities,\
        struct Library *, (__EdgeSnapBase), 5, EdgeSnap);\
})

#define ESnap_QueryCapabilities() \
    __ESnap_QueryCapabilities_WB(__EDGESNAP_LIBBASE)

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_SnapWindow_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_SnapWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(ULONG, (__arg2), D0), \
        struct Library *, (__EdgeSnapBase), 6, EdgeSnap);\
})

#define ESnap_SnapWindow(arg1, arg2) \
    __ESnap_SnapWindow_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_UnsnapWindow_WB(__EdgeSnapBase, __arg1) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC1(LONG, ESnap_UnsnapWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 7, EdgeSnap);\
})

#define ESnap_UnsnapWindow(arg1) \
    __ESnap_UnsnapWindow_WB(__EDGESNAP_LIBBASE, (arg1))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryWindow_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_QueryWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(ULONG *, (__arg2), A1), \
        struct Library *, (__EdgeSnapBase), 8, EdgeSnap);\
})

#define ESnap_QueryWindow(arg1, arg2) \
    __ESnap_QueryWindow_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_ExcludeWindow_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_ExcludeWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(BOOL, (__arg2), D0), \
        struct Library *, (__EdgeSnapBase), 9, EdgeSnap);\
})

#define ESnap_ExcludeWindow(arg1, arg2) \
    __ESnap_ExcludeWindow_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_SetOptionsA_WB(__EdgeSnapBase, __arg1) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC1(LONG, ESnap_SetOptionsA,\
         AROS_LCA(const struct TagItem *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 10, EdgeSnap);\
})

#define ESnap_SetOptionsA(arg1) \
    __ESnap_SetOptionsA_WB(__EDGESNAP_LIBBASE, (arg1))

#if !defined(NO_INLINE_STDARG) && !defined(EDGESNAP_NO_INLINE_STDARG)
#define ESnap_SetOptions(...) \
({ \
    const IPTR ESnap_SetOptionsA_args[] = { AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) };\
    ESnap_SetOptionsA((const struct TagItem *)(ESnap_SetOptionsA_args)); \
})
#endif /* !NO_INLINE_STDARG */

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_Enable_WB(__EdgeSnapBase, __arg1) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC1(LONG, ESnap_Enable,\
         AROS_LCA(BOOL, (__arg1), D0), \
        struct Library *, (__EdgeSnapBase), 11, EdgeSnap);\
})

#define ESnap_Enable(arg1) \
    __ESnap_Enable_WB(__EDGESNAP_LIBBASE, (arg1))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_FeedInput_WB(__EdgeSnapBase, __arg1, __arg2, __arg3, __arg4, __arg5) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC5NR(void, ESnap_FeedInput,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(ULONG, (__arg2), D1), \
         AROS_LCA(ULONG, (__arg3), D2), \
         AROS_LCA(ULONG, (__arg4), D3), \
         AROS_LCA(struct ESnapReport *, (__arg5), A0), \
        struct Library *, (__EdgeSnapBase), 12, EdgeSnap);\
})

#define ESnap_FeedInput(arg1, arg2, arg3, arg4, arg5) \
    __ESnap_FeedInput_WB(__EDGESNAP_LIBBASE, (arg1), (arg2), (arg3), (arg4), (arg5))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_ResetInput_WB(__EdgeSnapBase, __arg1) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC1NR(void, ESnap_ResetInput,\
         AROS_LCA(struct ESnapReport *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 13, EdgeSnap);\
})

#define ESnap_ResetInput(arg1) \
    __ESnap_ResetInput_WB(__EDGESNAP_LIBBASE, (arg1))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_IgnoreWindows_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_IgnoreWindows,\
         AROS_LCA(struct Window **, (__arg1), A0), \
         AROS_LCA(ULONG, (__arg2), D0), \
        struct Library *, (__EdgeSnapBase), 14, EdgeSnap);\
})

#define ESnap_IgnoreWindows(arg1, arg2) \
    __ESnap_IgnoreWindows_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryScreenArea_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_QueryScreenArea,\
         AROS_LCA(struct Screen *, (__arg1), A0), \
         AROS_LCA(struct ESnapArea *, (__arg2), A1), \
        struct Library *, (__EdgeSnapBase), 15, EdgeSnap);\
})

#define ESnap_QueryScreenArea(arg1, arg2) \
    __ESnap_QueryScreenArea_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryDivider_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_QueryDivider,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(struct ESnapDivider *, (__arg2), A0), \
        struct Library *, (__EdgeSnapBase), 16, EdgeSnap);\
})

#define ESnap_QueryDivider(arg1, arg2) \
    __ESnap_QueryDivider_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_MoveDivider_WB(__EdgeSnapBase, __arg1) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC1(LONG, ESnap_MoveDivider,\
         AROS_LCA(LONG, (__arg1), D0), \
        struct Library *, (__EdgeSnapBase), 17, EdgeSnap);\
})

#define ESnap_MoveDivider(arg1) \
    __ESnap_MoveDivider_WB(__EDGESNAP_LIBBASE, (arg1))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryDividerAt_WB(__EdgeSnapBase, __arg1, __arg2, __arg3, __arg4) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC4(LONG, ESnap_QueryDividerAt,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(LONG, (__arg2), D1), \
         AROS_LCA(LONG, (__arg3), D2), \
         AROS_LCA(struct ESnapDivider *, (__arg4), A0), \
        struct Library *, (__EdgeSnapBase), 18, EdgeSnap);\
})

#define ESnap_QueryDividerAt(arg1, arg2, arg3, arg4) \
    __ESnap_QueryDividerAt_WB(__EDGESNAP_LIBBASE, (arg1), (arg2), (arg3), (arg4))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_MoveDividerAt_WB(__EdgeSnapBase, __arg1, __arg2, __arg3) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC3(LONG, ESnap_MoveDividerAt,\
         AROS_LCA(LONG, (__arg1), A0), \
         AROS_LCA(LONG, (__arg2), A1), \
         AROS_LCA(LONG, (__arg3), D0), \
        struct Library *, (__EdgeSnapBase), 19, EdgeSnap);\
})

#define ESnap_MoveDividerAt(arg1, arg2, arg3) \
    __ESnap_MoveDividerAt_WB(__EDGESNAP_LIBBASE, (arg1), (arg2), (arg3))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryWindows_WB(__EdgeSnapBase, __arg1, __arg2, __arg3, __arg4) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC4(LONG, ESnap_QueryWindows,\
         AROS_LCA(struct Screen *, (__arg1), A0), \
         AROS_LCA(struct ESnapWindowInfo *, (__arg2), A1), \
         AROS_LCA(ULONG, (__arg3), D0), \
         AROS_LCA(ULONG *, (__arg4), A2), \
        struct Library *, (__EdgeSnapBase), 20, EdgeSnap);\
})

#define ESnap_QueryWindows(arg1, arg2, arg3, arg4) \
    __ESnap_QueryWindows_WB(__EDGESNAP_LIBBASE, (arg1), (arg2), (arg3), (arg4))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryGeneration_WB(__EdgeSnapBase, __arg1) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC1(ULONG, ESnap_QueryGeneration,\
         AROS_LCA(struct Screen *, (__arg1), A0), \
        struct Library *, (__EdgeSnapBase), 21, EdgeSnap);\
})

#define ESnap_QueryGeneration(arg1) \
    __ESnap_QueryGeneration_WB(__EDGESNAP_LIBBASE, (arg1))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_PlaceWindow_WB(__EdgeSnapBase, __arg1, __arg2, __arg3) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC3(LONG, ESnap_PlaceWindow,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(const struct ESnapRect *, (__arg2), A1), \
         AROS_LCA(ULONG, (__arg3), D0), \
        struct Library *, (__EdgeSnapBase), 22, EdgeSnap);\
})

#define ESnap_PlaceWindow(arg1, arg2, arg3) \
    __ESnap_PlaceWindow_WB(__EDGESNAP_LIBBASE, (arg1), (arg2), (arg3))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_PlaceWindowsA_WB(__EdgeSnapBase, __arg1, __arg2, __arg3) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC3(LONG, ESnap_PlaceWindowsA,\
         AROS_LCA(struct ESnapPlacement *, (__arg1), A0), \
         AROS_LCA(ULONG, (__arg2), D0), \
         AROS_LCA(ULONG, (__arg3), D1), \
        struct Library *, (__EdgeSnapBase), 23, EdgeSnap);\
})

#define ESnap_PlaceWindowsA(arg1, arg2, arg3) \
    __ESnap_PlaceWindowsA_WB(__EDGESNAP_LIBBASE, (arg1), (arg2), (arg3))

#if !defined(NO_INLINE_STDARG) && !defined(EDGESNAP_NO_INLINE_STDARG)
#define ESnap_PlaceWindows(arg1, arg2, ...) \
({ \
    const IPTR ESnap_PlaceWindowsA_args[] = { AROS_PP_VARIADIC_CAST2IPTR(__VA_ARGS__) };\
    ESnap_PlaceWindowsA((arg1), (arg2), (ULONG)(ESnap_PlaceWindowsA_args)); \
})
#endif /* !NO_INLINE_STDARG */

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_QueryWindowSerial_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_QueryWindowSerial,\
         AROS_LCA(struct Window *, (__arg1), A0), \
         AROS_LCA(ULONG *, (__arg2), A1), \
        struct Library *, (__EdgeSnapBase), 24, EdgeSnap);\
})

#define ESnap_QueryWindowSerial(arg1, arg2) \
    __ESnap_QueryWindowSerial_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)

#define __ESnap_FindWindow_WB(__EdgeSnapBase, __arg1, __arg2) ({\
        AROS_LIBREQ(EdgeSnapBase,2)\
        AROS_LC2(LONG, ESnap_FindWindow,\
         AROS_LCA(ULONG, (__arg1), D0), \
         AROS_LCA(struct Window **, (__arg2), A0), \
        struct Library *, (__EdgeSnapBase), 25, EdgeSnap);\
})

#define ESnap_FindWindow(arg1, arg2) \
    __ESnap_FindWindow_WB(__EDGESNAP_LIBBASE, (arg1), (arg2))

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

__END_DECLS

#endif /* DEFINES_EDGESNAP_H*/
