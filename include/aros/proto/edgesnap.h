#ifndef PROTO_EDGESNAP_H
#define PROTO_EDGESNAP_H

/*
    *** Automatically generated from 'library/aros/edgesnap.conf'. Edits will be lost. ***
    Copyright (C) 1995-2026, The AROS Development Team. All rights reserved.
*/

#include <exec/types.h>
#include <aros/system.h>

#include <clib/edgesnap_protos.h>

 #if !defined(__NOLIBBASE__) && !defined(__EDGESNAP_NOLIBBASE__)
  #if !defined(EdgeSnapBase)
   extern struct Library *EdgeSnapBase;
  #endif
 #endif
 #ifndef __EDGESNAP_LIBBASE
  #define __EDGESNAP_LIBBASE (EdgeSnapBase)
 #endif
#ifndef __aros_getbase_EdgeSnapBase
extern struct Library *__aros_getbase_EdgeSnapBase(void);
#endif

#if !defined(NOLIBINLINE) && !defined(EDGESNAP_NOLIBINLINE)
# include <inline/edgesnap.h>
#elif !defined(NOLIBDEFINES) && !defined(EDGESNAP_NOLIBDEFINES)
# include <defines/edgesnap.h>
#endif

#endif /* PROTO_EDGESNAP_H */
