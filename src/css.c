/*****************************************************************************
 * css.c: Functions for DVD authentification and unscrambling
 *****************************************************************************
 * Copyright (C) 1999-2001 VideoLAN
 * $Id: css.c,v 1.9 2002/06/02 16:05:34 sam Exp $
 *
 * Author: St�phane Borel <stef@via.ecp.fr>
 *         H�kan Hjort <d95hjort@dtek.chalmers.se>
 *
 * based on:
 *  - css-auth by Derek Fawcus <derek@spider.com>
 *  - DVD CSS ioctls example program by Andrew T. Veliath <andrewtv@usa.net>
 *  - The Divide and conquer attack by Frank A. Stevenson <frank@funcom.com>
 *  - DeCSSPlus by Ethan Hawke
 *  - DecVOB
 *  see http://www.lemuria.org/DeCSS/ by Tom Vogt for more information.
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
 * Preamble
 *****************************************************************************/
#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dvdcss/dvdcss.h"

#include "common.h"
#include "css.h"
#include "libdvdcss.h"
#include "csstables.h"
#include "ioctl.h"

/*****************************************************************************
 * Local prototypes
 *****************************************************************************/
static int  CSSGetBusKey ( dvdcss_handle dvdcss );
static int  CSSGetASF    ( dvdcss_handle dvdcss );
static void CSSCryptKey  ( int i_key_type, int i_varient,
                           u8 const *p_challenge, u8 *p_key );
static void CSSDecryptKey( u8 invert, u8 const *p_key, u8 const *p_crypted, 
                           u8 *p_result );
static int  CSSDecryptDiscKey ( u8 const *p_struct_disckey,
                                dvd_key_t p_disc_key );
static void CSSDecryptTitleKey( dvd_key_t p_disc_key, dvd_key_t p_titlekey );
static int  CSSDiscCrack ( dvdcss_handle dvdcss, u8 *p_disc_key );
static int  CSSRecoverKey( int i_start, 
                           u8 const *p_crypted, u8 const *p_decrypted, 
                           u8 const *p_sector_seed, u8 *p_key );
static int  CSSTitleCrack( dvdcss_handle dvdcss, int i_pos, int i_len, 
                           dvd_key_t p_titlekey );
static int  CSSAttackPattern( u8 const p_sec[0x800], int i_pos, u8 *p_key );
static int  CSSAttackPadding( u8 const p_sec[0x800], int i_pos, u8 *p_key );

/*****************************************************************************
 * CSSTest : check if the disc is encrypted or not
 *****************************************************************************/
int CSSTest( dvdcss_handle dvdcss )
{
    int i_ret, i_copyright;

    i_ret = ioctl_ReadCopyright( dvdcss->i_fd, 0 /* i_layer */, &i_copyright );

    if( i_ret < 0 )
    {
        /* Since it's the first ioctl we try to issue, we add a notice */
        _dvdcss_error( dvdcss, "css error: ioctl_ReadCopyright failed, "
                       "make sure there is a DVD in the drive, and that "
                       "you have used the correct device node."
#if defined( WIN32 )
                       "\nAlso note that if you are using Windows NT/2000/XP "
                       "you need to have administrator priviledges to be able "
                       "to use ioctls."
#endif
                     );

        return i_ret;
    }

    return i_copyright;
}

/*****************************************************************************
 * CSSGetBusKey : Go through the CSS Authentication process
 *****************************************************************************
 * It simulates the mutual authentication between logical unit and host,
 * and stops when a session key (called bus key) has been established.
 * Always do the full auth sequence. Some drives seem to lie and always
 * respond with ASF=1.  For instance the old DVD roms on Compaq Armada says
 * that ASF=1 from the start and then later fail with a 'read of scrambled 
 * block without authentication' error.
 *****************************************************************************/
static int CSSGetBusKey( dvdcss_handle dvdcss )
{
    u8        p_buffer[10];
    u8        p_challenge[2*KEY_SIZE];
    dvd_key_t p_key1;
    dvd_key_t p_key2;
    dvd_key_t p_key_check;
    u8        i_varient = 0;
    char      psz_warning[48];
    int       i_ret = -1;
    int       i;

    _dvdcss_debug( dvdcss, "requesting AGID" );
    i_ret = ioctl_ReportAgid( dvdcss->i_fd, &dvdcss->css.i_agid );

    /* We might have to reset hung authentication processes in the drive 
       by invalidating the corresponding AGID'.  As long as we haven't got
       an AGID, invalidate one (in sequence) and try again. */
    for( i = 0; i_ret == -1 && i < 4 ; ++i )
    {
        _dvdcss_debug( dvdcss, "ioctl_ReportAgid failed" );
        
        sprintf( psz_warning, "invalidating AGID %d", i );
        _dvdcss_debug( dvdcss, psz_warning );
        
        /* This is really _not good_, should be handled by the OS.
           Invalidating an AGID could make another process fail some
           where in it's authentication process. */
        dvdcss->css.i_agid = i;
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        
        _dvdcss_debug( dvdcss, "requesting AGID" );
        i_ret = ioctl_ReportAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
    }

    /* Unable to authenticate without AGID */
    if( i_ret == -1 )
    {
        _dvdcss_error( dvdcss, "ioctl_ReportAgid failed, fatal" );
        return -1;
    }

    /* Setup a challenge, any values should work */
    for( i = 0 ; i < 10; ++i )
    {
        p_challenge[i] = i;
    }

    /* Get challenge from host */
    for( i = 0 ; i < 10 ; ++i )
    {
        p_buffer[9-i] = p_challenge[i];
    }

    /* Send challenge to LU */
    if( ioctl_SendChallenge( dvdcss->i_fd,
                             &dvdcss->css.i_agid, p_buffer ) < 0 )
    {
        _dvdcss_error( dvdcss, "ioctl_SendChallenge failed" );
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        return -1;
    }

    /* Get key1 from LU */
    if( ioctl_ReportKey1( dvdcss->i_fd, &dvdcss->css.i_agid, p_buffer ) < 0)
    {
        _dvdcss_error( dvdcss, "ioctl_ReportKey1 failed" );
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        return -1;
    }

    /* Send key1 to host */
    for( i = 0 ; i < KEY_SIZE ; i++ )
    {
        p_key1[i] = p_buffer[4-i];
    }

    for( i = 0 ; i < 32 ; ++i )
    {
        CSSCryptKey( 0, i, p_challenge, p_key_check );

        if( memcmp( p_key_check, p_key1, KEY_SIZE ) == 0 )
        {
            snprintf( psz_warning, sizeof(psz_warning),
                      "drive authentic, using varient %d", i );
            _dvdcss_debug( dvdcss, psz_warning );
            i_varient = i;
            break;
        }
    }

    if( i == 32 )
    {
        _dvdcss_error( dvdcss, "drive would not authenticate" );
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        return -1;
    }

    /* Get challenge from LU */
    if( ioctl_ReportChallenge( dvdcss->i_fd, 
                               &dvdcss->css.i_agid, p_buffer ) < 0 )
    {
        _dvdcss_error( dvdcss, "ioctl_ReportKeyChallenge failed" );
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        return -1;
    }

    /* Send challenge to host */
    for( i = 0 ; i < 10 ; ++i )
    {
        p_challenge[i] = p_buffer[9-i];
    }

    CSSCryptKey( 1, i_varient, p_challenge, p_key2 );

    /* Get key2 from host */
    for( i = 0 ; i < KEY_SIZE ; ++i )
    {
        p_buffer[4-i] = p_key2[i];
    }

    /* Send key2 to LU */
    if( ioctl_SendKey2( dvdcss->i_fd, &dvdcss->css.i_agid, p_buffer ) < 0 )
    {
        _dvdcss_error( dvdcss, "ioctl_SendKey2 failed" );
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        return -1;
    }

    /* The drive has accepted us as authentic. */
    _dvdcss_debug( dvdcss, "authentication established" );

    memcpy( p_challenge, p_key1, KEY_SIZE );
    memcpy( p_challenge + KEY_SIZE, p_key2, KEY_SIZE );

    CSSCryptKey( 2, i_varient, p_challenge, dvdcss->css.p_bus_key );

    return 0;
}

/*****************************************************************************
 * CSSPrintKey : debug function that dumps a key value 
 *****************************************************************************/
static void CSSPrintKey( dvdcss_handle dvdcss, u8 const *data )
{
    char psz_output[80];

    sprintf( psz_output, "the key is %02x %02x %02x %02x %02x",
             data[0], data[1], data[2], data[3], data[4] );
    _dvdcss_debug( dvdcss, psz_output );
}

/*****************************************************************************
 * CSSGetDiscKey : get disc key.
 *****************************************************************************
 * This function should only be called if DVD ioctls are present.
 * It will set dvdcss->i_method = DVDCSS_METHOD_TITLE if it fails to find 
 * a valid disc key.
 * Two decryption methods are offered:
 *  -disc key hash crack,
 *  -decryption with player keys if they are available.
 *****************************************************************************/
int CSSGetDiscKey( dvdcss_handle dvdcss )
{
    unsigned char p_buffer[2048];
    dvd_key_t p_disc_key;
    int i;

    if( CSSGetBusKey( dvdcss ) < 0)
    {
        return -1;
    }

    /* Get encrypted disc key */
    if( ioctl_ReadDiscKey( dvdcss->i_fd, &dvdcss->css.i_agid, p_buffer ) < 0 )
    {
        _dvdcss_error( dvdcss, "ioctl_ReadDiscKey failed" );
        return -1;
    }

    /* This should have invaidated the AGID and got us ASF=1. */
    if( CSSGetASF( dvdcss ) != 1 )
    {
        /* Region mismatch (or region not set) is the most likely source. */  
        _dvdcss_error( dvdcss, "ASF not 1 after reading disc key" );
        ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
        return -1;
    }

    /* Decrypt disc key using bus key */
    for( i = 0 ; i < 2048 ; i++ )
    {
        p_buffer[ i ] ^= dvdcss->css.p_bus_key[ 4 - (i % KEY_SIZE) ];
    }

    switch( dvdcss->i_method )
    {
        case DVDCSS_METHOD_KEY:
            /* Decrypt disc key with player key. */
            _dvdcss_debug( dvdcss, "decrypting disc key with player keys" );
            if( ! CSSDecryptDiscKey( p_buffer, p_disc_key ) )
            {
                break;
            }
            _dvdcss_debug( dvdcss, "no valid player key" );
            /* Fall through */

        case DVDCSS_METHOD_DISC:
            /* Crack Disc key to be able to use it */
            _dvdcss_debug( dvdcss, "cracking disc key from key hash" );
            _dvdcss_debug( dvdcss, "building 64MB table ... this will take some time" );
            memcpy( p_disc_key, p_buffer, KEY_SIZE );
            if( CSSDiscCrack( dvdcss, p_disc_key ) )
            {
                _dvdcss_debug( dvdcss, "failed cracking disc key" );
                dvdcss->i_method = DVDCSS_METHOD_TITLE;
            }
            break;

        default:
            _dvdcss_debug( dvdcss, "disc key won't be decrypted" );
            memset( p_disc_key, 0, KEY_SIZE );
            break;
    }

    memcpy( dvdcss->css.p_disc_key, p_disc_key, KEY_SIZE );
    CSSPrintKey( dvdcss, dvdcss->css.p_disc_key );
    return 0;
}


/*****************************************************************************
 * CSSGetTitleKey : get title key.
 *****************************************************************************/
int CSSGetTitleKey( dvdcss_handle dvdcss, int i_pos, dvd_key_t p_title_key )
{
    u8  p_key[KEY_SIZE];
    int i, i_ret = 0;

    if( dvdcss->b_ioctls && ( dvdcss->i_method == DVDCSS_METHOD_KEY || 
			      dvdcss->i_method == DVDCSS_METHOD_DISC ) )
    {
        /* We have a decrypted Disc key and the ioctls are available,
         * read the title key and decrypt it.
         */

        _dvdcss_debug( dvdcss, "decrypting title key with disc key" );

        /* We need to authenticate again every time to get a new session key */
        if( CSSGetBusKey( dvdcss ) < 0 )
        {
            return -1;
        }

        /* Get encrypted title key */
        if( ioctl_ReadTitleKey( dvdcss->i_fd, &dvdcss->css.i_agid,
                                i_pos, p_key ) < 0 )
        {
            _dvdcss_error( dvdcss, "ioctl_ReadTitleKey failed" );
            i_ret = -1;
        }

        /* Test ASF, it will be reset to 0 if we got a Region error */
        switch( CSSGetASF( dvdcss ) )
        {
            case -1:
                /* An error getting the ASF status, something must be wrong. */
                // ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
                _dvdcss_debug( dvdcss, "lost ASF reqesting Title key" );
                i_ret = -1;
		break;

            case 0:
                /* This might either be a title that has no key, 
                 * or we encountered a region error. */
                _dvdcss_debug( dvdcss, "lost ASF reqesting Title key" );
                break;

            case 1:
                /* Drive status is ok. */
		/* If the title key request failed, but we did not loose ASF,
		 * we might stil have the AGID.  Other code assume that we
		 * will not after this so invalidate it(?). */
		if( i_ret < 0 )
		{
		  //ioctl_InvalidateAgid( dvdcss->i_fd, &dvdcss->css.i_agid );
		}
                break;
        }
	
	if( !( i_ret < 0 ) )
	{
	    /* Decrypt title key using the bus key */
	    for( i = 0 ; i < KEY_SIZE ; i++ )
	    {
		p_key[ i ] ^= dvdcss->css.p_bus_key[ 4 - (i % KEY_SIZE) ];
	    }

	    /* If p_key is all zero then there realy wasn't any key pressent
	     * even though we got to read it without an error. */
	    if( !( p_key[0] | p_key[1] | p_key[2] | p_key[3] | p_key[4] ) )
	    {
		i_ret = 0;
	    }
	    else
	    {
		CSSDecryptTitleKey( dvdcss->css.p_disc_key, p_key );
		i_ret = 1;
	    }

	    /* All went well either there wasn't a key or we have it now. */
	    memcpy( p_title_key, p_key, KEY_SIZE );
	    CSSPrintKey( dvdcss, p_title_key );
	    
	    return i_ret;
	}

	/* The title key request failed */
	_dvdcss_debug( dvdcss, "Reverting to cracking the Title key" );

	/* FALL THROUGH */
    }
    
    /* METHOD is TITLE, we can't use the ioctls or requesting the title key
     * failed above.  For these cases we try to crack the key instead. */
    
    /* For now, the read limit is 9Gb / 2048 =  4718592 sectors. */
    i_ret = CSSTitleCrack( dvdcss, i_pos, 4718592, p_key);

    memcpy( p_title_key, p_key, KEY_SIZE );
    CSSPrintKey( dvdcss, p_title_key );

    return i_ret;
}

/*****************************************************************************
 * CSSDescrambleSector: does the actual descrambling of data
 *****************************************************************************
 * sec : sector to descramble
 * key : title key for this sector
 *****************************************************************************/
int CSSDescrambleSector( dvd_key_t p_key, u8 *p_sec )
{
    unsigned int    i_t1, i_t2, i_t3, i_t4, i_t5, i_t6;
    u8             *p_end = p_sec + 0x800;

    /* PES_scrambling_control */
    if( p_sec[0x14] & 0x30)
    {
        i_t1 = (p_key[0] ^ p_sec[0x54]) | 0x100;
        i_t2 = p_key[1] ^ p_sec[0x55];
        i_t3 = (p_key[2] | (p_key[3] << 8) |
               (p_key[4] << 16)) ^ (p_sec[0x56] |
               (p_sec[0x57] << 8) | (p_sec[0x58] << 16));
        i_t4 = i_t3 & 7;
        i_t3 = i_t3 * 2 + 8 - i_t4;
        p_sec += 0x80;
        i_t5 = 0;

        while( p_sec != p_end )
        {
            i_t4 = p_css_tab2[i_t2] ^ p_css_tab3[i_t1];
            i_t2 = i_t1>>1;
            i_t1 = ( ( i_t1 & 1 ) << 8 ) ^ i_t4;
            i_t4 = p_css_tab5[i_t4];
            i_t6 = ((((((( i_t3 >> 3 ) ^ i_t3 ) >> 1 ) ^
                                         i_t3 ) >> 8 ) ^ i_t3 ) >> 5 ) & 0xff;
            i_t3 = (i_t3 << 8 ) | i_t6;
            i_t6 = p_css_tab4[i_t6];
            i_t5 += i_t6 + i_t4;
            *p_sec = p_css_tab1[*p_sec] ^ ( i_t5 & 0xff );
            p_sec++;
            i_t5 >>= 8;
        }
    }

    return 0;
}

/* Following functions are local */

/*****************************************************************************
 * CSSGetASF : Get Authentification success flag
 *****************************************************************************
 * Returns :
 *  -1 on ioctl error,
 *  0 if the device needs to be authenticated,
 *  1 either.
 *****************************************************************************/
static int CSSGetASF( dvdcss_handle dvdcss )
{
    int i_asf = 0;

    if( ioctl_ReportASF( dvdcss->i_fd, NULL, &i_asf ) != 0 )
    {
        /* The ioctl process has failed */
        _dvdcss_error( dvdcss, "GetASF fatal error" );
        return -1;
    }

    if( i_asf )
    {
        _dvdcss_debug( dvdcss, "GetASF authenticated (ASF=1)" );
    }
    else
    {
        _dvdcss_debug( dvdcss, "GetASF not authenticated (ASF=0)" );
    }

    return i_asf;
}

/*****************************************************************************
 * CSSCryptKey : shuffles bits and unencrypt keys.
 *****************************************************************************
 * Used during authentication and disc key negociation in CSSGetBusKey.
 * i_key_type : 0->key1, 1->key2, 2->buskey.
 * i_varient : between 0 and 31.
 *****************************************************************************/
static void CSSCryptKey( int i_key_type, int i_varient,
                         u8 const *p_challenge, u8 *p_key )
{
    /* Permutation table for challenge */
    u8      pp_perm_challenge[3][10] =
            { { 1, 3, 0, 7, 5, 2, 9, 6, 4, 8 },
              { 6, 1, 9, 3, 8, 5, 7, 4, 0, 2 },
              { 4, 0, 3, 5, 7, 2, 8, 6, 1, 9 } };

    /* Permutation table for varient table for key2 and buskey */
    u8      pp_perm_varient[2][32] =
            { { 0x0a, 0x08, 0x0e, 0x0c, 0x0b, 0x09, 0x0f, 0x0d,
                0x1a, 0x18, 0x1e, 0x1c, 0x1b, 0x19, 0x1f, 0x1d,
                0x02, 0x00, 0x06, 0x04, 0x03, 0x01, 0x07, 0x05,
                0x12, 0x10, 0x16, 0x14, 0x13, 0x11, 0x17, 0x15 },
              { 0x12, 0x1a, 0x16, 0x1e, 0x02, 0x0a, 0x06, 0x0e,
                0x10, 0x18, 0x14, 0x1c, 0x00, 0x08, 0x04, 0x0c,
                0x13, 0x1b, 0x17, 0x1f, 0x03, 0x0b, 0x07, 0x0f,
                0x11, 0x19, 0x15, 0x1d, 0x01, 0x09, 0x05, 0x0d } };

    u8      p_varients[32] =
            {   0xB7, 0x74, 0x85, 0xD0, 0xCC, 0xDB, 0xCA, 0x73,
                0x03, 0xFE, 0x31, 0x03, 0x52, 0xE0, 0xB7, 0x42,
                0x63, 0x16, 0xF2, 0x2A, 0x79, 0x52, 0xFF, 0x1B,
                0x7A, 0x11, 0xCA, 0x1A, 0x9B, 0x40, 0xAD, 0x01 };

    /* The "secret" key */
    u8      p_secret[5] = { 0x55, 0xD6, 0xC4, 0xC5, 0x28 };

    u8      p_bits[30];
    u8      p_scratch[10];
    u8      p_tmp1[5];
    u8      p_tmp2[5];
    u8      i_lfsr0_o;  /* 1 bit used */
    u8      i_lfsr1_o;  /* 1 bit used */
    u32     i_lfsr0;
    u32     i_lfsr1;
    u8      i_css_varient;
    u8      i_cse;
    u8      i_index;
    u8      i_combined;
    u8      i_carry;
    u8      i_val = 0;
    int     i_term = 0;
    int     i_bit;
    int     i;

    for (i = 9; i >= 0; --i)
        p_scratch[i] = p_challenge[pp_perm_challenge[i_key_type][i]];

    i_css_varient = ( i_key_type == 0 ) ? i_varient :
                    pp_perm_varient[i_key_type-1][i_varient];

    /*
     * This encryption engine implements one of 32 variations
     * one the same theme depending upon the choice in the
     * varient parameter (0 - 31).
     *
     * The algorithm itself manipulates a 40 bit input into
     * a 40 bit output.
     * The parameter 'input' is 80 bits.  It consists of
     * the 40 bit input value that is to be encrypted followed
     * by a 40 bit seed value for the pseudo random number
     * generators.
     */

    /* Feed the secret into the input values such that
     * we alter the seed to the LFSR's used above,  then
     * generate the bits to play with.
     */
    for( i = 5 ; --i >= 0 ; )
    {
        p_tmp1[i] = p_scratch[5 + i] ^ p_secret[i] ^ p_crypt_tab2[i];
    }

    /*
     * We use two LFSR's (seeded from some of the input data bytes) to
     * generate two streams of pseudo-random bits.  These two bit streams
     * are then combined by simply adding with carry to generate a final
     * sequence of pseudo-random bits which is stored in the buffer that
     * 'output' points to the end of - len is the size of this buffer.
     *
     * The first LFSR is of degree 25,  and has a polynomial of:
     * x^13 + x^5 + x^4 + x^1 + 1
     *
     * The second LSFR is of degree 17,  and has a (primitive) polynomial of:
     * x^15 + x^1 + 1
     *
     * I don't know if these polynomials are primitive modulo 2,  and thus
     * represent maximal-period LFSR's.
     *
     *
     * Note that we take the output of each LFSR from the new shifted in
     * bit,  not the old shifted out bit.  Thus for ease of use the LFSR's
     * are implemented in bit reversed order.
     *
     */
    
    /* In order to ensure that the LFSR works we need to ensure that the
     * initial values are non-zero.  Thus when we initialise them from
     * the seed,  we ensure that a bit is set.
     */
    i_lfsr0 = ( p_tmp1[0] << 17 ) | ( p_tmp1[1] << 9 ) |
              (( p_tmp1[2] & ~7 ) << 1 ) | 8 | ( p_tmp1[2] & 7 );
    i_lfsr1 = ( p_tmp1[3] << 9 ) | 0x100 | p_tmp1[4];

    i_index = sizeof(p_bits);
    i_carry = 0;

    do
    {
        for( i_bit = 0, i_val = 0 ; i_bit < 8 ; ++i_bit )
        {

            i_lfsr0_o = ( ( i_lfsr0 >> 24 ) ^ ( i_lfsr0 >> 21 ) ^
                        ( i_lfsr0 >> 20 ) ^ ( i_lfsr0 >> 12 ) ) & 1;
            i_lfsr0 = ( i_lfsr0 << 1 ) | i_lfsr0_o;

            i_lfsr1_o = ( ( i_lfsr1 >> 16 ) ^ ( i_lfsr1 >> 2 ) ) & 1;
            i_lfsr1 = ( i_lfsr1 << 1 ) | i_lfsr1_o;

            i_combined = !i_lfsr1_o + i_carry + !i_lfsr0_o;
            /* taking bit 1 */
            i_carry = ( i_combined >> 1 ) & 1;
            i_val |= ( i_combined & 1 ) << i_bit;
        }
    
        p_bits[--i_index] = i_val;
    } while( i_index > 0 );

    /* This term is used throughout the following to
     * select one of 32 different variations on the
     * algorithm.
     */
    i_cse = p_varients[i_css_varient] ^ p_crypt_tab2[i_css_varient];

    /* Now the actual blocks doing the encryption.  Each
     * of these works on 40 bits at a time and are quite
     * similar.
     */
    i_index = 0;
    for( i = 5, i_term = 0 ; --i >= 0 ; i_term = p_scratch[i] )
    {
        i_index = p_bits[25 + i] ^ p_scratch[i];
        i_index = p_crypt_tab1[i_index] ^ ~p_crypt_tab2[i_index] ^ i_cse;

        p_tmp1[i] = p_crypt_tab2[i_index] ^ p_crypt_tab3[i_index] ^ i_term;
    }
    p_tmp1[4] ^= p_tmp1[0];

    for( i = 5, i_term = 0 ; --i >= 0 ; i_term = p_tmp1[i] )
    {
        i_index = p_bits[20 + i] ^ p_tmp1[i];
        i_index = p_crypt_tab1[i_index] ^ ~p_crypt_tab2[i_index] ^ i_cse;

        p_tmp2[i] = p_crypt_tab2[i_index] ^ p_crypt_tab3[i_index] ^ i_term;
    }
    p_tmp2[4] ^= p_tmp2[0];

    for( i = 5, i_term = 0 ; --i >= 0 ; i_term = p_tmp2[i] )
    {
        i_index = p_bits[15 + i] ^ p_tmp2[i];
        i_index = p_crypt_tab1[i_index] ^ ~p_crypt_tab2[i_index] ^ i_cse;
        i_index = p_crypt_tab2[i_index] ^ p_crypt_tab3[i_index] ^ i_term;

        p_tmp1[i] = p_crypt_tab0[i_index] ^ p_crypt_tab2[i_index];
    }
    p_tmp1[4] ^= p_tmp1[0];

    for( i = 5, i_term = 0 ; --i >= 0 ; i_term = p_tmp1[i] )
    {
        i_index = p_bits[10 + i] ^ p_tmp1[i];
        i_index = p_crypt_tab1[i_index] ^ ~p_crypt_tab2[i_index] ^ i_cse;

        i_index = p_crypt_tab2[i_index] ^ p_crypt_tab3[i_index] ^ i_term;

        p_tmp2[i] = p_crypt_tab0[i_index] ^ p_crypt_tab2[i_index];
    }
    p_tmp2[4] ^= p_tmp2[0];

    for( i = 5, i_term = 0 ; --i >= 0 ; i_term = p_tmp2[i] )
    {
        i_index = p_bits[5 + i] ^ p_tmp2[i];
        i_index = p_crypt_tab1[i_index] ^ ~p_crypt_tab2[i_index] ^ i_cse;

        p_tmp1[i] = p_crypt_tab2[i_index] ^ p_crypt_tab3[i_index] ^ i_term;
    }
    p_tmp1[4] ^= p_tmp1[0];

    for(i = 5, i_term = 0 ; --i >= 0 ; i_term = p_tmp1[i] )
    {
        i_index = p_bits[i] ^ p_tmp1[i];
        i_index = p_crypt_tab1[i_index] ^ ~p_crypt_tab2[i_index] ^ i_cse;

        p_key[i] = p_crypt_tab2[i_index] ^ p_crypt_tab3[i_index] ^ i_term;
    }

    return;
}

/*****************************************************************************
 * CSSDecryptKey: decrypt p_crypted with p_key.
 *****************************************************************************
 * Used to decrypt the disc key, with a player key, after requesting it 
 * in CSSGetDiscKey and to decrypt title keys, with a disc key, requested 
 * in CSSGetTitleKey.
 * The player keys and the resulting disc key are only used as KEKs 
 * (key encryption keys).
 * Decryption is slightly dependant on the type of key:
 *  -for disc key, invert is 0x00,
 *  -for title key, invert if 0xff. 
 *****************************************************************************/
static void CSSDecryptKey( u8 invert, u8 const *p_key, 
                           u8 const *p_crypted, u8 *p_result )
{
    unsigned int    i_lfsr1_lo;
    unsigned int    i_lfsr1_hi;
    unsigned int    i_lfsr0;
    unsigned int    i_combined;
    u8              o_lfsr0;
    u8              o_lfsr1;
    u8              k[5];
    int             i;

    i_lfsr1_lo = p_key[0] | 0x100;
    i_lfsr1_hi = p_key[1];

    i_lfsr0    = ( ( p_key[4] << 17 )
                 | ( p_key[3] << 9 )
                 | ( p_key[2] << 1 ) )
                 + 8 - ( p_key[2] & 7 );
    i_lfsr0    = ( p_css_tab4[i_lfsr0 & 0xff] << 24 ) |
                 ( p_css_tab4[( i_lfsr0 >> 8 ) & 0xff] << 16 ) |
                 ( p_css_tab4[( i_lfsr0 >> 16 ) & 0xff] << 8 ) |
                   p_css_tab4[( i_lfsr0 >> 24 ) & 0xff];

    i_combined = 0;
    for( i = 0 ; i < KEY_SIZE ; ++i )
    {
        o_lfsr1     = p_css_tab2[i_lfsr1_hi] ^ p_css_tab3[i_lfsr1_lo];
        i_lfsr1_hi  = i_lfsr1_lo >> 1;
        i_lfsr1_lo  = ( ( i_lfsr1_lo & 1 ) << 8 ) ^ o_lfsr1;
        o_lfsr1     = p_css_tab4[o_lfsr1];

        o_lfsr0 = ((((((( i_lfsr0 >> 8 ) ^ i_lfsr0 ) >> 1 )
                        ^ i_lfsr0 ) >> 3 ) ^ i_lfsr0 ) >> 7 );
        i_lfsr0 = ( i_lfsr0 >> 8 ) | ( o_lfsr0 << 24 );

        i_combined += ( o_lfsr0 ^ invert ) + o_lfsr1;
        k[i] = i_combined & 0xff;
        i_combined >>= 8;
    }

    p_result[4] = k[4] ^ p_css_tab1[p_crypted[4]] ^ p_crypted[3];
    p_result[3] = k[3] ^ p_css_tab1[p_crypted[3]] ^ p_crypted[2];
    p_result[2] = k[2] ^ p_css_tab1[p_crypted[2]] ^ p_crypted[1];
    p_result[1] = k[1] ^ p_css_tab1[p_crypted[1]] ^ p_crypted[0];
    p_result[0] = k[0] ^ p_css_tab1[p_crypted[0]] ^ p_result[4];

    p_result[4] = k[4] ^ p_css_tab1[p_result[4]] ^ p_result[3];
    p_result[3] = k[3] ^ p_css_tab1[p_result[3]] ^ p_result[2];
    p_result[2] = k[2] ^ p_css_tab1[p_result[2]] ^ p_result[1];
    p_result[1] = k[1] ^ p_css_tab1[p_result[1]] ^ p_result[0];
    p_result[0] = k[0] ^ p_css_tab1[p_result[0]];

    return;
}

static const dvd_key_t player_keys[] = 
{
    { 0x01, 0xaf, 0xe3, 0x12, 0x80 },
    { 0x12, 0x11, 0xca, 0x04, 0x3b },
    { 0x14, 0x0c, 0x9e, 0xd0, 0x09 },
    { 0x14, 0x71, 0x35, 0xba, 0xe2 },
    { 0x1a, 0xa4, 0x33, 0x21, 0xa6 },
    { 0x26, 0xec, 0xc4, 0xa7, 0x4e },
    { 0x2c, 0xb2, 0xc1, 0x09, 0xee },
    { 0x2f, 0x25, 0x9e, 0x96, 0xdd },
    { 0x33, 0x2f, 0x49, 0x6c, 0xe0 },
    { 0x35, 0x5b, 0xc1, 0x31, 0x0f },
    { 0x36, 0x67, 0xb2, 0xe3, 0x85 },
    { 0x39, 0x3d, 0xf1, 0xf1, 0xbd },
    { 0x3b, 0x31, 0x34, 0x0d, 0x91 },
    { 0x45, 0xed, 0x28, 0xeb, 0xd3 },
    { 0x48, 0xb7, 0x6c, 0xce, 0x69 },
    { 0x4b, 0x65, 0x0d, 0xc1, 0xee },
    { 0x4c, 0xbb, 0xf5, 0x5b, 0x23 },
    { 0x51, 0x67, 0x67, 0xc5, 0xe0 },
    { 0x53, 0x94, 0xe1, 0x75, 0xbf },
    { 0x57, 0x2c, 0x8b, 0x31, 0xae },
    { 0x63, 0xdb, 0x4c, 0x5b, 0x4a },
    { 0x7b, 0x1e, 0x5e, 0x2b, 0x57 },
    { 0x85, 0xf3, 0x85, 0xa0, 0xe0 },
    { 0xab, 0x1e, 0xe7, 0x7b, 0x72 },
    { 0xab, 0x36, 0xe3, 0xeb, 0x76 },
    { 0xb1, 0xb8, 0xf9, 0x38, 0x03 },
    { 0xb8, 0x5d, 0xd8, 0x53, 0xbd },
    { 0xbf, 0x92, 0xc3, 0xb0, 0xe2 },
    { 0xcf, 0x1a, 0xb2, 0xf8, 0x0a },
    { 0xec, 0xa0, 0xcf, 0xb3, 0xff },
    { 0xfc, 0x95, 0xa9, 0x87, 0x35 }
};

/*****************************************************************************
 * CSSDecryptDiscKey
 *****************************************************************************
 * Decryption of the disc key with player keys if they are available.
 * Try to decrypt the disc key from every position with every player key.
 * p_struct_disckey: the 2048 byte DVD_STRUCT_DISCKEY data
 * p_disc_key: result, the 5 byte disc key
 *****************************************************************************/
static int CSSDecryptDiscKey( u8 const *p_struct_disckey, 
                              dvd_key_t p_disc_key )
{
    u8 p_verify[KEY_SIZE];
    int i, n = 0;

    /* Decrypt disc key with player keys from csskeys.h */
    while( n < sizeof(player_keys) / sizeof(dvd_key_t) )
    {
        for( i = 1; i < 409; i++ )
        {
            /* Check if player key n is the right key for position i. */
            CSSDecryptKey( 0, player_keys[n], p_struct_disckey + 5 * i,
                           p_disc_key );

            /* The first part in the struct_disckey block is the 
             * 'disc key' encrypted with it self.  Using this we 
             * can check if we decrypted the correct key. */
            CSSDecryptKey( 0, p_disc_key, p_struct_disckey, p_verify );

            /* If the position / player key pair worked then return. */
            if( memcmp( p_disc_key, p_verify, 5 ) == 0 )
            {
                return 0;
            }
        }
        n++;
    }
    
    /* Have tried all combinations of positions and keys, 
     * and we still didn't succeed. */
    return -1;
}

/*****************************************************************************
 * CSSDecryptTitleKey
 *****************************************************************************
 * Decrypt the title key using the disc key.
 * p_disc_key: result, the 5 byte disc key
 * p_titlekey: the encrypted title key, gets overwritten by the decrypted key
 *****************************************************************************/
static void CSSDecryptTitleKey( dvd_key_t p_disc_key, dvd_key_t p_titlekey )
{
    CSSDecryptKey( 0xff, p_disc_key, p_titlekey, p_titlekey );
}

/*****************************************************************************
 * CSSDiscCrack: brute force disc key
 * CSS hash reversal function designed by Frank Stevenson
 *****************************************************************************
 * This function uses a big amount of memory to crack the disc key from the   
 * disc key hash, if player keys are not available.
 *****************************************************************************/
#define K1TABLEWIDTH 10

/*
 * Simple function to test if a candidate key produces the given hash
 */
static int investigate( unsigned char *hash, unsigned char *ckey )
{
    unsigned char key[KEY_SIZE];

    CSSDecryptKey( 0, ckey, hash, key);

    return memcmp( key, ckey, KEY_SIZE );
}

static int CSSDiscCrack( dvdcss_handle dvdcss, u8 *p_disc_key )
{
    unsigned char B[5] = { 0,0,0,0,0 }; /* Second Stage of mangle cipher */
    unsigned char C[5] = { 0,0,0,0,0 }; /* Output Stage of mangle cipher
                                         * IntermediateKey */
    unsigned char k[5] = { 0,0,0,0,0 }; /* Mangling cipher key
                                         * Also output from CSS( C ) */
    unsigned char out1[5];              /* five first output bytes of LFSR1 */
    unsigned char out2[5];              /* five first output bytes of LFSR2 */
    unsigned int lfsr1a;                /* upper 9 bits of LFSR1 */
    unsigned int lfsr1b;                /* lower 8 bits of LFSR1 */
    unsigned int tmp, tmp2, tmp3, tmp4,tmp5;
    int i,j;
    unsigned int nStepA;        /* iterator for LFSR1 start state */
    unsigned int nStepB;        /* iterator for possible B[0]     */
    unsigned int nTry;          /* iterator for K[1] possibilities */
    unsigned int nPossibleK1;   /* #of possible K[1] values */
    unsigned char* K1table;     /* Lookup table for possible K[1] */
    unsigned int*  BigTable;    /* LFSR2 startstate indexed by 
                                 * 1,2,5 output byte */

    /*
     * Prepare tables for hash reversal
     */

    
    /* initialize lookup tables for k[1] */
    K1table = malloc( 65536 * K1TABLEWIDTH );
    memset( K1table, 0 , 65536 * K1TABLEWIDTH );
    if( K1table == NULL )
    {
        return -1;
    }

    tmp = p_disc_key[0] ^ p_css_tab1[ p_disc_key[1] ];
    for( i = 0 ; i < 256 ; i++ ) /* k[1] */
    {
        tmp2 = p_css_tab1[ tmp ^ i ]; /* p_css_tab1[ B[1] ]*/

        for( j = 0 ; j < 256 ; j++ ) /* B[0] */
        {
            tmp3 = j ^ tmp2 ^ i; /* C[1] */
            tmp4 = K1table[ K1TABLEWIDTH * ( 256 * j + tmp3 ) ]; /* count of entries  here */
            tmp4++;
/*
            if( tmp4 == K1TABLEWIDTH )
            {
                _dvdcss_debug( dvdcss, "Table disaster %d", tmp4 );
            }
*/
            if( tmp4 < K1TABLEWIDTH )
            {
                K1table[ K1TABLEWIDTH * ( 256 * j + tmp3 ) +    tmp4 ] = i;
            }
            K1table[ K1TABLEWIDTH * ( 256 * j + tmp3 ) ] = tmp4;
        }
    }

    /* Initing our Really big table */
    BigTable = malloc( 16777216 * sizeof(int) );
    memset( BigTable, 0 , 16777216 * sizeof(int) );
    if( BigTable == NULL )
    {
        return -1;
    }

    tmp3 = 0;

    _dvdcss_debug( dvdcss, "initializing the big table" );

    for( i = 0 ; i < 16777216 ; i++ )
    {
        tmp = (( i + i ) & 0x1fffff0 ) | 0x8 | ( i & 0x7 );

        for( j = 0 ; j < 5 ; j++ )
        {
            tmp2=((((((( tmp >> 3 ) ^ tmp ) >> 1 ) ^ tmp ) >> 8 )
                                    ^ tmp ) >> 5 ) & 0xff;
            tmp = ( tmp << 8) | tmp2;
            out2[j] = p_css_tab4[ tmp2 ];
        }

        j = ( out2[0] << 16 ) | ( out2[1] << 8 ) | out2[4];
        BigTable[j] = i;
    }

    /*
     * We are done initing, now reverse hash
     */
    tmp5 = p_disc_key[0] ^ p_css_tab1[ p_disc_key[1] ];

    for( nStepA = 0 ; nStepA < 65536 ; nStepA ++ )
    {
        lfsr1a = 0x100 | ( nStepA >> 8 );
        lfsr1b = nStepA & 0xff;

        /* Generate 5 first output bytes from lfsr1 */
        for( i = 0 ; i < 5 ; i++ )
        {
            tmp = p_css_tab2[ lfsr1b ] ^ p_css_tab3[ lfsr1a ];
            lfsr1b = lfsr1a >> 1;
            lfsr1a = ((lfsr1a&1)<<8) ^ tmp;
            out1[ i ] = p_css_tab4[ tmp ];
        }

        /* cumpute and cache some variables */
        C[0] = nStepA >> 8;
        C[1] = nStepA & 0xff;
        tmp = p_disc_key[3] ^ p_css_tab1[ p_disc_key[4] ];
        tmp2 = p_css_tab1[ p_disc_key[0] ];

        /* Search through all possible B[0] */
        for( nStepB = 0 ; nStepB < 256 ; nStepB++ )
        {
            /* reverse parts of the mangling cipher */
            B[0] = nStepB;
            k[0] = p_css_tab1[ B[0] ] ^ C[0];
            B[4] = B[0] ^ k[0] ^ tmp2;
            k[4] = B[4] ^ tmp;
            nPossibleK1 = K1table[ K1TABLEWIDTH * (256 * B[0] + C[1]) ];

            /* Try out all possible values for k[1] */
            for( nTry = 0 ; nTry < nPossibleK1 ; nTry++ )
            {
                k[1] = K1table[ K1TABLEWIDTH * (256 * B[0] + C[1]) + nTry + 1 ];
                B[1] = tmp5 ^ k[1];

                /* reconstruct output from LFSR2 */
                tmp3 = ( 0x100 + k[0] - out1[0] );
                out2[0] = tmp3 & 0xff;
                tmp3 = tmp3 & 0x100 ? 0x100 : 0xff;
                tmp3 = ( tmp3 + k[1] - out1[1] );
                out2[1] = tmp3 & 0xff;
                tmp3 = ( 0x100 + k[4] - out1[4] );
                out2[4] = tmp3 & 0xff;  /* Can be 1 off  */

                /* test first possible out2[4] */
                tmp4 = ( out2[0] << 16 ) | ( out2[1] << 8 ) | out2[4];
                tmp4 = BigTable[ tmp4 ];
                C[2] = tmp4 & 0xff;
                C[3] = ( tmp4 >> 8 ) & 0xff;
                C[4] = ( tmp4 >> 16 ) & 0xff;
                B[3] = p_css_tab1[ B[4] ] ^ k[4] ^ C[4];
                k[3] = p_disc_key[2] ^ p_css_tab1[ p_disc_key[3] ] ^ B[3];
                B[2] = p_css_tab1[ B[3] ] ^ k[3] ^ C[3];
                k[2] = p_disc_key[1] ^ p_css_tab1[ p_disc_key[2] ] ^ B[2];

                if( ( B[1] ^ p_css_tab1[ B[2] ] ^ k[ 2 ]  ) == C[ 2 ] )
                {
                    if( ! investigate( &p_disc_key[0] , &C[0] ) )
                    {
                        goto end;
                    }
                }

                /* Test second possible out2[4] */
                out2[4] = ( out2[4] + 0xff ) & 0xff;
                tmp4 = ( out2[0] << 16 ) | ( out2[1] << 8 ) | out2[4];
                tmp4 = BigTable[ tmp4 ];
                C[2] = tmp4 & 0xff;
                C[3] = ( tmp4 >> 8 ) & 0xff;
                C[4] = ( tmp4 >> 16 ) & 0xff;
                B[3] = p_css_tab1[ B[4] ] ^ k[4] ^ C[4];
                k[3] = p_disc_key[2] ^ p_css_tab1[ p_disc_key[3] ] ^ B[3];
                B[2] = p_css_tab1[ B[3] ] ^ k[3] ^ C[3];
                k[2] = p_disc_key[1] ^ p_css_tab1[ p_disc_key[2] ] ^ B[2];

                if( ( B[1] ^ p_css_tab1[ B[2] ] ^ k[ 2 ]  ) == C[ 2 ] )
                {
                    if( ! investigate( &p_disc_key[0] , &C[0] ) )
                    {
                        goto end;
                    }
                }
            }
        }
    }

end:

    memcpy( p_disc_key, &C[0], KEY_SIZE );

    free( K1table );
    free( BigTable );

    return 0;
}

/*****************************************************************************
 * CSSRecoverKey : (title) key recovery from chiper and plain text
 * Function designed by Frank Stevenson
 *****************************************************************************
 * Called from CSSAttack* which are inturn called by CSSTitleCrack.  Given
 * a guessed(?) plain text and the chiper text.  Returns -1 on failure.
 *****************************************************************************/
static int CSSRecoverKey( int i_start,
                          u8 const *p_crypted, u8 const *p_decrypted,
                          u8 const *p_sector_seed, u8 *p_key )
{
    u8 p_buffer[10];
    unsigned int i_t1, i_t2, i_t3, i_t4, i_t5, i_t6;
    unsigned int i_try;
    unsigned int i_candidate;
    unsigned int i, j;
    int i_exit = -1;

    for( i = 0 ; i < 10 ; i++ )
    {
        p_buffer[i] = p_css_tab1[p_crypted[i]] ^ p_decrypted[i];
    }

    for( i_try = i_start ; i_try < 0x10000 ; i_try++ )
    {
        i_t1 = i_try >> 8 | 0x100;
        i_t2 = i_try & 0xff;
        i_t3 = 0;               /* not needed */
        i_t5 = 0;

        /* iterate cipher 4 times to reconstruct LFSR2 */
        for( i = 0 ; i < 4 ; i++ )
        {
            /* advance LFSR1 normaly */
            i_t4 = p_css_tab2[i_t2] ^ p_css_tab3[i_t1];
            i_t2 = i_t1 >> 1;
            i_t1 = ( ( i_t1 & 1 ) << 8 ) ^ i_t4;
            i_t4 = p_css_tab5[i_t4];
            /* deduce i_t6 & i_t5 */
            i_t6 = p_buffer[i];
            if( i_t5 )
            {
                i_t6 = ( i_t6 + 0xff ) & 0x0ff;
            }
            if( i_t6 < i_t4 )
            {
                i_t6 += 0x100;
            }
            i_t6 -= i_t4;
            i_t5 += i_t6 + i_t4;
            i_t6 = p_css_tab4[ i_t6 ];
            /* feed / advance i_t3 / i_t5 */
            i_t3 = ( i_t3 << 8 ) | i_t6;
            i_t5 >>= 8;
        }

        i_candidate = i_t3;

        /* iterate 6 more times to validate candidate key */
        for( ; i < 10 ; i++ )
        {
            i_t4 = p_css_tab2[i_t2] ^ p_css_tab3[i_t1];
            i_t2 = i_t1 >> 1;
            i_t1 = ( ( i_t1 & 1 ) << 8 ) ^ i_t4;
            i_t4 = p_css_tab5[i_t4];
            i_t6 = ((((((( i_t3 >> 3 ) ^ i_t3 ) >> 1 ) ^
                                         i_t3 ) >> 8 ) ^ i_t3 ) >> 5 ) & 0xff;
            i_t3 = ( i_t3 << 8 ) | i_t6;
            i_t6 = p_css_tab4[i_t6];
            i_t5 += i_t6 + i_t4;
            if( ( i_t5 & 0xff ) != p_buffer[i] )
            {
                break;
            }

            i_t5 >>= 8;
        }

        if( i == 10 )
        {
            /* Do 4 backwards steps of iterating t3 to deduce initial state */
            i_t3 = i_candidate;
            for( i = 0 ; i < 4 ; i++ )
            {
                i_t1 = i_t3 & 0xff;
                i_t3 = ( i_t3 >> 8 );
                /* easy to code, and fast enough bruteforce
                 * search for byte shifted in */
                for( j = 0 ; j < 256 ; j++ )
                {
                    i_t3 = ( i_t3 & 0x1ffff ) | ( j << 17 );
                    i_t6 = ((((((( i_t3 >> 3 ) ^ i_t3 ) >> 1 ) ^
                                   i_t3 ) >> 8 ) ^ i_t3 ) >> 5 ) & 0xff;
                    if( i_t6 == i_t1 )
                    {
                        break;
                    }
                }
            }

            i_t4 = ( i_t3 >> 1 ) - 4;
            for( i_t5 = 0 ; i_t5 < 8; i_t5++ )
            {
                if( ( ( i_t4 + i_t5 ) * 2 + 8 - ( (i_t4 + i_t5 ) & 7 ) )
                                                                      == i_t3 )
                {
                    p_key[0] = i_try>>8;
                    p_key[1] = i_try & 0xFF;
                    p_key[2] = ( ( i_t4 + i_t5 ) >> 0 ) & 0xFF;
                    p_key[3] = ( ( i_t4 + i_t5 ) >> 8 ) & 0xFF;
                    p_key[4] = ( ( i_t4 + i_t5 ) >> 16 ) & 0xFF;
                    i_exit = i_try + 1;
                }
            }
        }
    }

    if( i_exit >= 0 )
    {
        p_key[0] ^= p_sector_seed[0];
        p_key[1] ^= p_sector_seed[1];
        p_key[2] ^= p_sector_seed[2];
        p_key[3] ^= p_sector_seed[3];
        p_key[4] ^= p_sector_seed[4];
    }

    return i_exit;
}


/******************************************************************************
 * Various pices for the title crack engine.
 ******************************************************************************
 * The length of the PES packet is located at 0x12-0x13.
 * The the copyrigth protection bits are located at 0x14 (bits 0x20 and 0x10).
 * The data of the PES packet begins at 0x15 (if there isn't any PTS/DTS)
 * or at 0x?? if there are both PTS and DTS's.
 * The seed value used with the descrambler key is the 5 bytes at 0x54-0x58.
 * The scrabled part of a sector begins at 0x80. 
 *****************************************************************************/

/* Statistics */
static int i_tries = 0, i_success = 0;

/*****************************************************************************
 * CSSTitleCrack : try to crack title key from the contents of a VOB.
 *****************************************************************************
 * This function is called by CSSGetTitleKey to find a title key, if we've
 * chosen to crack title key instead of decrypting it with the disc key.
 * The DVD should have been opened and be in an authenticated state.
 * i_pos is the starting sector, i_len is the maximum number of sectors to read
 *****************************************************************************/
static int CSSTitleCrack( dvdcss_handle dvdcss, int i_pos, int i_len, 
                          dvd_key_t p_titlekey )
{
    u8       p_buf[0x800];
    const u8 p_packstart[4] = { 0x00, 0x00, 0x01, 0xba };
    int      i_reads = 0;
    int      i_encrypted = 0;
    int      b_stop_scanning = 0;
    int      i_blocks_read;

    i_tries = 0;
    i_success = 0;

    do
    {
        i_pos = _dvdcss_seek( dvdcss, i_pos );
        i_blocks_read = dvdcss_read( dvdcss, p_buf, 1, DVDCSS_NOFLAGS );
        
        /* Either we are at the end of the physical device or the auth
         * have faild / where not done and we got a read error. */
        if( !i_blocks_read )
        {
            _dvdcss_debug( dvdcss, "read returned 0 (end of device?)" );
            break;
        }

        /* Stop when we find a non MPEG stream block. 
         * (We must have reached the end of the stream).
         * For now, allow all blocks that begin with a start code. */
        if( memcmp( p_buf, p_packstart, 3 ) )
        {
            _dvdcss_debug( dvdcss, "non MPEG block found (end of title)" );
            break;
        }

        if( p_buf[0x0d] & 0x07 )
            _dvdcss_debug( dvdcss, "stuffing in pack header" ); 

        /* PES_scrambling_control does not exist in a system_header,
         * a padding_stream or a private_stream2 (and others?). */
        if( p_buf[0x14] & 0x30  && ! ( p_buf[0x11] == 0xbb 
                                       || p_buf[0x11] == 0xbe  
                                       || p_buf[0x11] == 0xbf ) )
        {
            i_encrypted++;

            if( CSSAttackPattern(p_buf, i_reads, p_titlekey) > 0 )
            {
                b_stop_scanning = 1;
            }
#if 0
            if( CSSAttackPadding(p_buf, i_reads, p_titlekey) > 0 )
            {
                b_stop_scanning = 1;
            }
#endif
        }

        i_pos += i_blocks_read;
        i_len -= i_blocks_read;
        i_reads += i_blocks_read;

        /* Emit a progress indication now and then. */
        if( !( i_reads & 0xfff ) ) 
          _dvdcss_debug( dvdcss, "still working..." );

        /* Stop after 2000 blocks if we haven't seen any encrypted blocks. */
        if( i_reads >= 2000 && i_encrypted == 0 ) break;

    } while( !b_stop_scanning && i_len > 0);

    if( i_len <= 0 )
        _dvdcss_debug( dvdcss, "end of title reached" );

    { /* Print some statistics. */
        char psz_info[128];
        snprintf( psz_info, sizeof(psz_info), 
                  "%d of %d attempts successful, %d of %d blocks scrambled", 
                  i_success, i_tries, i_encrypted, i_reads );
        _dvdcss_debug( dvdcss, psz_info );
    }

    if( i_success > 0 /* b_stop_scanning */ )
    {
        _dvdcss_debug( dvdcss, "vts key initialized" );
        return 1;
    }

    if( i_encrypted == 0 )
    {
        memset( p_titlekey, 0, KEY_SIZE );
        _dvdcss_debug( dvdcss, "file was unscrambled" );
        return 0;
    }

    memset( p_titlekey, 0, KEY_SIZE );
    return -1;
}


/******************************************************************************
 * The original Ethan Hawke (DeCSSPlus) attack (modified). 
 ******************************************************************************
 * Tries to find a repeating pattern just before the encrypted part starts.
 * Then it guesses that the plain text for first encrypted bytes are
 * a contiuation of that pattern.
 *****************************************************************************/
static int CSSAttackPattern( u8 const p_sec[0x800], int i_pos, u8 *p_key )
{
    unsigned int i_best_plen = 0;
    unsigned int i_best_p = 0;
    unsigned int i, j;
    
    /* For all cycle length from 2 to 48 */
    for( i = 2 ; i < 0x30 ; i++ )
    {
        /* Find the number of bytes that repeats in cycles. */
        for( j = i + 1;
             j < 0x80 && ( p_sec[0x7F - (j%i)] == p_sec[0x7F - j] );
             j++ )
        {
            /* We have found j repeating bytes with a cycle length i. */
            if( j > i_best_plen )
            {
                i_best_plen = j;
                i_best_p = i;
            }
        }
    }
    
    /* We need at most 10 plain text bytes?, so a make sure that we
     * have at least 20 repeated bytes and that they have cycled at
     * least one time.  */
    if( ( i_best_plen > 3 ) && ( i_best_plen / i_best_p >= 2) )
    {
        int res;
        
        i_tries++;
        memset( p_key, 0, KEY_SIZE );
        res = CSSRecoverKey( 0,  &p_sec[0x80],
                             &p_sec[ 0x80 - 
                                     ( i_best_plen / i_best_p) * i_best_p ],
                             &p_sec[0x54] /* key_seed */, p_key );
        i_success += ( res >= 0 );
#if 0        
        if( res >= 0 )
        {
            fprintf( stderr, "key is %02x %02x %02x %02x %02x ",
                     p_key[0], p_key[1], p_key[2], p_key[3], p_key[4] );
            fprintf( stderr, "at block %5d pattern len %3d period %3d %s\n", 
                     i_pos, i_best_plen, i_best_p, (res>=0?"y":"n") );
        }
#endif
        return ( res >= 0 );
    }
    
    return 0;
}


#if 0
/******************************************************************************
 * Encrypted Padding_stream attack.
 ******************************************************************************
 * DVD specifies that there must only be one type of data in every sector.
 * Every sector is one pack and so must obviously be 2048 bytes long.
 * For the last pice of video data before a VOBU boundary there might not
 * be exactly the right amount of data to fill a sector. They one has to 
 * pad the pack to 2048 bytes. For just a few bytes this is doen in the
 * header but for any large amount you insert a PES packet from the 
 * Padding stream. This looks like 0x00 00 01 be xx xx ff ff ...
 * where xx xx is the length of the padding stream.
 *****************************************************************************/
static int CSSAttackPadding( u8 const p_sec[0x800], int i_pos, u8 *p_key )
{
    unsigned int i_pes_length;
    //static int i_tries = 0, i_success = 0;

    i_pes_length = (p_sec[0x12]<<8) | p_sec[0x13];
    
    /* Coverd by the test below but usfull for debuging. */
    if( i_pes_length == 0x800 - 0x14 ) return 0;
    
    /* There must be room for at least 4? bytes of padding stream, 
     * and it must be encrypted.
     * sector size - pack/pes header - padding startcode - padding length */
    if( ( 0x800 - 0x14 - 4 - 2 - i_pes_length < 4 ) ||
        ( p_sec[0x14 + i_pes_length + 0] == 0x00 &&
          p_sec[0x14 + i_pes_length + 1] == 0x00 &&
          p_sec[0x14 + i_pes_length + 2] == 0x01 ) )
    { 
      fprintf( stderr, "plain %d %02x %02x %02x %02x (type %02x sub %02x)\n", 
               0x800 - 0x14 - 4 - 2 - i_pes_length,
               p_sec[0x14 + i_pes_length + 0],
               p_sec[0x14 + i_pes_length + 1],
               p_sec[0x14 + i_pes_length + 2],
               p_sec[0x14 + i_pes_length + 3],
               p_sec[0x11], p_sec[0x17 + p_sec[0x16]]);
      return 0;
    }
    
    /* If we are here we know that there is a where in the pack a
       encrypted PES header is (startcode + lenght). It's never more 
       than  two packets in the pack, so we 'know' the length. The 
       plaintext at offset (0x14 + i_pes_length) will then be 
       00 00 01 e0/bd/be xx xx, in the case of be the following bytes 
       are also known. */
    
    /* An encrypted SPU PES packet with another encrypted PES packet following.
       Normaly if the following was a padding stream that would be in plain
       text. So it will be another SPU PES packet. */
    if( p_sec[0x11] == 0xbd && 
        p_sec[0x17 + p_sec[0x16]] >= 0x20 &&
        p_sec[0x17 + p_sec[0x16]] <= 0x3f )
    {
        i_tries++;
    }
    
    /* A Video PES packet with another encrypted PES packet following.
     * No reason execpt for time stamps to break the data into two packets.
     * So it's likely that the following PES packet is a padding stream. */
    if( p_sec[0x11] == 0xe0 )
    { 
        i_tries++;
    }
   
    if( 1 )
    {
        //fprintf( stderr, "key is %02x %02x %02x %02x %02x ",
        //           p_key[0], p_key[1], p_key[2], p_key[3], p_key[4] );
        fprintf( stderr, "at block %5d padding len %4d "
                 "type %02x sub %02x\n",  i_pos, i_pes_length, 
                 p_sec[0x11], p_sec[0x17 + p_sec[0x16]]);
    }
    
    return 0;
}
#endif
