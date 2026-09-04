/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * edgesnap_base.h - the AROS library base, private to the module.
 *
 * Clients see a plain struct Library (libbasetypeextern in the conf).
 * The two extra fields are what genmodule's generated node fills in:
 * the exec base it was initialised with, and the segment list to hand
 * back at expunge.
 */

#ifndef EDGESNAP_BASE_H
#define EDGESNAP_BASE_H

#include <exec/libraries.h>
#include <exec/execbase.h>
#include <dos/bptr.h>

struct EdgeSnapBase {
    struct Library esb_Lib;
    struct ExecBase *esb_SysBase;
    BPTR esb_SegList;
};

#endif /* EDGESNAP_BASE_H */
