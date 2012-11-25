/**
 * \file dvdcpxm.h
 * \author Unknown
 * \author Ondřej Hošek <ondra.hosek@gmail.com>
 * \brief The \e libdvdcpxm public header.
 *
 * This header contains the public types and functions that applications
 * using \e libdvdcpxm may use.
 */

/*
 * Copyright (C) 2012 VideoLAN
 * $Id$
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with libdvdcss; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */


#ifndef DVDCPXM_DVDCPXM_H
#ifndef _DOXYGEN_SKIP_ME
#define DVDCPXM_DVDCPXM_H 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Library instance handle, to be used for each library call. */
typedef struct dvdcpxm_t* dvdcpxm_t;

/** The block size of a DVD. */
#define DVDCPXM_BLOCK_SIZE     2048

/** The size of the encrypted part of the block. */
#define DVDCPXM_ENCRYPTED_SIZE 1920

/** The default flag to be used by \e libdvdcpxm functions. */
#define DVDCPXM_NOFLAGS        0

/** dvdcpxm_how_scrambled() returns this value when unprotected media is loaded. */
#define DVDCPXM_MEDIA_UNPR     0

/** dvdcpxm_how_scrambled() returns this value when CPPM-protected media is loaded. */
#define DVDCPXM_MEDIA_CPPM     1

/** dvdcpxm_how_scrambled() returns this value when CPRM-protected media is loaded. */
#define DVDCPXM_MEDIA_CPRM     2

/** Flag to ask dvdcpxm_decrypt() to preserve CCI byte of the data it processes. */
#define DVDCPXM_PRESERVE_CCI   (1 << 0)


#if defined(LIBDVDCPXM_EXPORTS)
#define LIBDVDCPXM_EXPORT __declspec(dllexport) extern
#elif defined(LIBDVDCPXM_IMPORTS)
#define LIBDVDCPXM_EXPORT __declspec(dllimport) extern
#elif defined(HAVE_VISIBILITY)
#define LIBDVDCPXM_EXPORT __attribute__((visibility("default"))) extern
#else
#define LIBDVDCPXM_EXPORT extern
#endif

/*
 * Exported prototypes.
 */
LIBDVDCPXM_EXPORT dvdcpxm_t dvdcpxm_init    ( char *psz_target );
LIBDVDCPXM_EXPORT int       dvdcpxm_close   ( dvdcpxm_t );
LIBDVDCPXM_EXPORT int       dvdcpxm_decrypt ( void *p_buffer,
                               int nr_blocks,
                               int flags );

LIBDVDCPXM_EXPORT int       dvdcpxm_how_scrambled ( dvdcpxm_t );

#ifdef __cplusplus
}
#endif

#endif /* DVDCPXM_DVDCPXM_H */
