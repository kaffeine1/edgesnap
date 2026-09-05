#ifndef CLIB_EDGESNAP_PROTOS_H
#define CLIB_EDGESNAP_PROTOS_H

/*
    *** Automatically generated from 'library/aros/edgesnap.conf'. Edits will be lost. ***
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
*/

#include <aros/libcall.h>

#include <intuition/intuition.h>
#include <utility/tagitem.h>
#include "edgesnap.h"

__BEGIN_DECLS


#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP0(ULONG, ESnap_QueryCapabilities,
         LIBBASETYPEPTR, EdgeSnapBase, 5, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP2(LONG, ESnap_SnapWindow,
         AROS_LPA(struct Window *, win, A0),
         AROS_LPA(ULONG, zone, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 6, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP1(LONG, ESnap_UnsnapWindow,
         AROS_LPA(struct Window *, win, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 7, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP2(LONG, ESnap_QueryWindow,
         AROS_LPA(struct Window *, win, A0),
         AROS_LPA(ULONG *, zone_out, A1),
         LIBBASETYPEPTR, EdgeSnapBase, 8, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP2(LONG, ESnap_ExcludeWindow,
         AROS_LPA(struct Window *, win, A0),
         AROS_LPA(BOOL, exclude, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 9, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP1(LONG, ESnap_SetOptionsA,
         AROS_LPA(const struct TagItem *, tags, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 10, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP1(LONG, ESnap_Enable,
         AROS_LPA(BOOL, on, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 11, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP5(void, ESnap_FeedInput,
         AROS_LPA(ULONG, presses, D0),
         AROS_LPA(ULONG, motions, D1),
         AROS_LPA(ULONG, releases, D2),
         AROS_LPA(ULONG, qualifiers, D3),
         AROS_LPA(struct ESnapReport *, report, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 12, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP1(void, ESnap_ResetInput,
         AROS_LPA(struct ESnapReport *, report, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 13, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP2(LONG, ESnap_IgnoreWindows,
         AROS_LPA(struct Window **, windows, A0),
         AROS_LPA(ULONG, count, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 14, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP2(LONG, ESnap_QueryScreenArea,
         AROS_LPA(struct Screen *, screen, A0),
         AROS_LPA(struct ESnapArea *, area, A1),
         LIBBASETYPEPTR, EdgeSnapBase, 15, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP2(LONG, ESnap_QueryDivider,
         AROS_LPA(ULONG, thickness, D0),
         AROS_LPA(struct ESnapDivider *, divider, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 16, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP1(LONG, ESnap_MoveDivider,
         AROS_LPA(LONG, position, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 17, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP4(LONG, ESnap_QueryDividerAt,
         AROS_LPA(ULONG, thickness, D0),
         AROS_LPA(LONG, x, D1),
         AROS_LPA(LONG, y, D2),
         AROS_LPA(struct ESnapDivider *, divider, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 18, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP3(LONG, ESnap_MoveDividerAt,
         AROS_LPA(LONG, vertical, A0),
         AROS_LPA(LONG, line, A1),
         AROS_LPA(LONG, position, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 19, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP4(LONG, ESnap_QueryWindows,
         AROS_LPA(struct Screen *, screen, A0),
         AROS_LPA(struct ESnapWindowInfo *, buf, A1),
         AROS_LPA(ULONG, count, D0),
         AROS_LPA(ULONG *, needed, A2),
         LIBBASETYPEPTR, EdgeSnapBase, 20, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP1(ULONG, ESnap_QueryGeneration,
         AROS_LPA(struct Screen *, screen, A0),
         LIBBASETYPEPTR, EdgeSnapBase, 21, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP3(LONG, ESnap_PlaceWindow,
         AROS_LPA(struct Window *, win, A0),
         AROS_LPA(const struct ESnapRect *, rect, A1),
         AROS_LPA(ULONG, flags, D0),
         LIBBASETYPEPTR, EdgeSnapBase, 22, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

#if !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__)
AROS_LP3(LONG, ESnap_PlaceWindowsA,
         AROS_LPA(struct ESnapPlacement *, list, A0),
         AROS_LPA(ULONG, count, D0),
         AROS_LPA(ULONG, flags, D1),
         LIBBASETYPEPTR, EdgeSnapBase, 23, EdgeSnap
);

#endif /* !defined(__EDGESNAP_LIBAPI__) || (2 <= __EDGESNAP_LIBAPI__) */

__END_DECLS

#endif /* CLIB_EDGESNAP_PROTOS_H */
