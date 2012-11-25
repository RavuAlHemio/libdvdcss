/*****************************************************************************
 * libdvdcpxm.h: CPxM decryption library
 *****************************************************************************
 * Copyright (C) 2012
 * $Id$
 *
 * Authors: Unknown
 *          Ondřej Hošek <ondra.hosek@gmail.com>
 *
 * This library is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *****************************************************************************/

#ifndef LIBDVDCPXM_LIBDVDCPXM_H
#define LIBDVDCPXM_LIBDVDCPXM_H

#define CPRM_MKB_SIZE        (16 * CPRM_MKB_PACK_SIZE - 16)

#define CCI_BYTE 0x00;

typedef struct {
	uint8_t  col;
	uint16_t row;
	uint64_t key;
}	device_key_t;

typedef struct {
	struct {
		uint8_t type:4;
		uint8_t reserved:4;
		uint8_t manufacturer_id[2];
		uint8_t serial_number[5];
	} id_media;
	uint8_t dvd_mac[10];
} cprm_media_id_t;

typedef	struct {
	uint8_t mkb_hash[8];
	uint8_t reserved[8];
} cprm_mkb_desc_t;

typedef struct {
	cprm_mkb_desc_t mkb_desc;
	uint8_t         mkb[CPRM_MKB_SIZE];
} cprm_mkb_t;

/*****************************************************************************
 * The libdvdcpxm structure
 *****************************************************************************/
struct dvdcpxm_s
{
    int      i_media_type;
    uint64_t media_key;

    uint64_t id_album;
    uint64_t id_media;
    uint64_t vr_k_te;

    uint32_t sbox_f[256];
};

#endif
