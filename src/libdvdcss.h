/*****************************************************************************
 * private.h: private DVD reading library data
 *****************************************************************************
 * Copyright (C) 1998-2001 VideoLAN
 * $Id: libdvdcss.h,v 1.2 2002/04/04 14:21:25 sam Exp $
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

/*****************************************************************************
 * The libdvdcss structure
 *****************************************************************************/
struct dvdcss_s
{
    /* File descriptor */
    int i_fd;
    int i_seekpos;

    /* Decryption stuff */
    int          i_method;
    css_t        css;
    boolean_t    b_ioctls;
    boolean_t    b_encrypted;
    dvd_title_t *p_titles;

    /* Error management */
    char     *psz_error;
    boolean_t b_errors;
    boolean_t b_debug;

#if defined( WIN32 )
    char *p_readv_buffer;
    int  i_readv_buf_size;
#else
    int i_raw_fd;
    int i_read_fd;
#endif
};

/*****************************************************************************
 * libdvdcss method: used like init flags
 *****************************************************************************/
#define DVDCSS_METHOD_KEY        0
#define DVDCSS_METHOD_DISC       1
#define DVDCSS_METHOD_TITLE      2

/*****************************************************************************
 * Functions used across the library
 *****************************************************************************/
int _dvdcss_seek  ( dvdcss_handle, int i_blocks );
int _dvdcss_read  ( dvdcss_handle, void *p_buffer, int i_blocks );

/*****************************************************************************
 * Error management
 *****************************************************************************/
#if defined( _WIN32 ) && defined( _MSC_VER )
#   define DVDCSS_ERROR( x ) fprintf( stderr, "libdvdcss error: %s\n", x );
#   define DVDCSS_DEBUG( x ) fprintf( stderr, "libdvdcss debug: %s\n", x );
#else
#   define DVDCSS_ERROR( x... ) fprintf( stderr, "libdvdcss error: %s\n", ##x );
#   define DVDCSS_DEBUG( x... ) fprintf( stderr, "libdvdcss debug: %s\n", ##x );
#endif

static inline void _dvdcss_error( dvdcss_handle dvdcss, char *psz_string )
{
    if( dvdcss->b_errors )
    {
        DVDCSS_ERROR( psz_string );
    }

    dvdcss->psz_error = psz_string;
}

static inline void _dvdcss_debug( dvdcss_handle dvdcss, char *psz_string )
{
    if( dvdcss->b_debug )
    {
        DVDCSS_DEBUG( psz_string );
    }
}


