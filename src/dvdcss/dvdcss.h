/*****************************************************************************
 * libdvdcss.h: DVD reading library, exported functions.
 *****************************************************************************
 * Copyright (C) 1998-2001 VideoLAN
 * $Id: dvdcss.h,v 1.2 2002/05/26 14:22:23 sam Exp $
 *
 * Authors: St�phane Borel <stef@via.ecp.fr>
 *          Samuel Hocevar <sam@zoy.org>
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111, USA.
 *****************************************************************************/

#ifndef _DVDCSS_DVDCSS_H
#define _DVDCSS_DVDCSS_H 1

#ifdef __cplusplus
extern "C" {
#endif

/*****************************************************************************
 * The libdvdcss structure
 *****************************************************************************/
typedef struct dvdcss_s* dvdcss_handle;

/*****************************************************************************
 * Defines and flags
 *****************************************************************************/
#define DVDCSS_BLOCK_SIZE      2048

#define DVDCSS_NOFLAGS         0
#define DVDCSS_READ_DECRYPT    (1 << 0)
#define DVDCSS_SEEK_MPEG       (1 << 0)
#define DVDCSS_SEEK_KEY        (1 << 1)

/*****************************************************************************
 * Our version number. The variable name contains the interface version
 *****************************************************************************/
extern char *        dvdcss_interface_2;

/*****************************************************************************
 * Exported prototypes
 *****************************************************************************/
extern dvdcss_handle dvdcss_open  ( char *psz_target );
extern int           dvdcss_close ( dvdcss_handle );
extern int           dvdcss_title ( dvdcss_handle,
                                    int i_block );
extern int           dvdcss_seek  ( dvdcss_handle,
                                    int i_blocks,
                                    int i_flags );
extern int           dvdcss_read  ( dvdcss_handle,
                                    void *p_buffer,
                                    int i_blocks,
                                    int i_flags );
extern int           dvdcss_readv ( dvdcss_handle,
                                    void *p_iovec,
                                    int i_blocks,
                                    int i_flags );
extern char *        dvdcss_error ( dvdcss_handle );

#ifdef __cplusplus
}
#endif

#endif /* <dvdcss/dvdcss.h> */
