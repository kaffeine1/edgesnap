/*
 * Copyright (c) 2026 Michele Dipace <michele.dipace@kaffeine.net>
 * SPDX-License-Identifier: MIT
 *
 * support.c - see support.h.
 */

#ifdef __amigaos4__
/* Call the system by name, as prefs_io.c does. */
#ifndef __USE_INLINE__
#define __USE_INLINE__
#endif
#endif

#include <exec/types.h>
#include <exec/io.h>
#include <devices/clipboard.h>
#include <proto/exec.h>

#include "support.h"

/* IFF lengths are big-endian on every system, AROS on x86 included. */
static void esp_be32(UBYTE *p, ULONG v)
{
    p[0] = (UBYTE)(v >> 24);
    p[1] = (UBYTE)(v >> 16);
    p[2] = (UBYTE)(v >> 8);
    p[3] = (UBYTE)v;
}

static int esp_clip_write(struct IOClipReq *io, const void *data, ULONG len)
{
    io->io_Command = CMD_WRITE;
    io->io_Data = (STRPTR)data;
    io->io_Length = len;
    return DoIO((struct IORequest *)io) == 0 && io->io_Actual == len;
}

int esp_clip_text(const char *text)
{
    struct MsgPort *port;
    struct IOClipReq *io;
    UBYTE head[20];
    ULONG len = 0;
    int ok = 0;

    while (text[len] != '\0') {
        len++;
    }
    port = CreateMsgPort();
    if (port == NULL) {
        return 0;
    }
    io = (struct IOClipReq *)CreateIORequest(port, sizeof(*io));
    if (io == NULL) {
        DeleteMsgPort(port);
        return 0;
    }
    if (OpenDevice((CONST_STRPTR)"clipboard.device", 0,
                   (struct IORequest *)io, 0) == 0) {
        static const UBYTE pad = 0;

        /* FORM <size> FTXT CHRS <len> text, padded to an even length. */
        head[0] = 'F'; head[1] = 'O'; head[2] = 'R'; head[3] = 'M';
        esp_be32(head + 4, 12 + len + (len & 1));
        head[8] = 'F'; head[9] = 'T'; head[10] = 'X'; head[11] = 'T';
        head[12] = 'C'; head[13] = 'H'; head[14] = 'R'; head[15] = 'S';
        esp_be32(head + 16, len);
        io->io_Offset = 0;
        io->io_Error = 0;
        io->io_ClipID = 0;
        ok = esp_clip_write(io, head, sizeof(head)) &&
             esp_clip_write(io, text, len) &&
             ((len & 1) == 0 || esp_clip_write(io, &pad, 1));
        /* The device holds the clip until it is told the write is over,
         * so this goes out whether the writes did or not. */
        io->io_Command = CMD_UPDATE;
        if (DoIO((struct IORequest *)io) != 0) {
            ok = 0;
        }
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);
    DeleteMsgPort(port);
    return ok;
}
