/*
 *	PROGRAM:	InterBase International support
 *	MODULE:		cv_narrow.cpp
 *	DESCRIPTION:	Codeset conversion for narrow character sets.
 *
 * The contents of this file are subject to the Interbase Public
 * License Version 1.0 (the "License"); you may not use this file
 * except in compliance with the License. You may obtain a copy
 * of the License at http://www.Inprise.com/IPL.html
 *
 * Software distributed under the License is distributed on an
 * "AS IS" basis, WITHOUT WARRANTY OF ANY KIND, either express
 * or implied. See the License for the specific language governing
 * rights and limitations under the License.
 *
 * The Original Code was created by Inprise Corporation
 * and its predecessors. Portions created by Inprise Corporation are
 * Copyright (C) Inprise Corporation.
 *
 * All Rights Reserved.
 * Contributor(s): ______________________________________.
 */

#include "firebird.h"
#include "../intl/ldcommon.h"
#include "ld_proto.h"
#include "cv_narrow.h"

void CV_convert_init(CSCONVERT csptr,
					 SSHORT to_cs,
					 SSHORT from_cs,
					 pfn_INTL_convert cvt_fn,
					 const void* datatable,
					 const void* datatable2)
{
	csptr->csconvert_version = 40;
	csptr->csconvert_name = (const ASCII*) "DIRECT";
	csptr->csconvert_from = from_cs;
	csptr->csconvert_to = to_cs;
	csptr->csconvert_convert = cvt_fn;
	csptr->csconvert_datatable = (const BYTE*) datatable;
	csptr->csconvert_misc = (const BYTE*) datatable2;
}



USHORT CV_unicode_to_nc(CSCONVERT obj,
						BYTE *dest_ptr,
						USHORT dest_len,
						const BYTE* src_ptr,
						USHORT src_len,
						SSHORT *err_code,
						USHORT *err_position)
{
	fb_assert(src_ptr != NULL || dest_ptr == NULL);
	fb_assert(err_code != NULL);
	fb_assert(err_position != NULL);
	fb_assert(obj != NULL);
	fb_assert(obj->csconvert_convert == CV_unicode_to_nc);
	fb_assert(obj->csconvert_datatable != NULL);
	fb_assert(obj->csconvert_misc != NULL);

	const USHORT src_start = src_len;
	*err_code = 0;

/* See if we're only after a length estimate */
	if (dest_ptr == NULL)
		return ((USHORT) (src_len + 1) / 2);

	const BYTE* const start = dest_ptr;
	while ((src_len > 1) && dest_len) {
		const UNICODE uni = *((const UNICODE*) src_ptr);
		const UCHAR ch = obj->csconvert_datatable[
									  ((const USHORT*) obj->
									   csconvert_misc)[(USHORT) uni / 256]
									  + (uni % 256)];
		if ((ch == CS_CANT_MAP) && !(uni == CS_CANT_MAP)) {
			*err_code = CS_CONVERT_ERROR;
			break;
		}
		*dest_ptr++ = ch;
		src_ptr += 2;
		src_len -= 2;
		dest_len -= 1;
	};
	if (src_len && !*err_code) {
		if (src_len == 1)
			*err_code = CS_BAD_INPUT;
		else
			*err_code = CS_TRUNCATION_ERROR;
	};
	*err_position = src_start - src_len;
	return (dest_ptr - start);
}


USHORT CV_wc_to_wc(CSCONVERT obj,
				   USHORT *dest_ptr,
				   USHORT dest_len,
				   const USHORT* src_ptr,
				   USHORT src_len,
				   SSHORT *err_code,
				   USHORT *err_position)
{
	fb_assert(src_ptr != NULL || dest_ptr == NULL);
	fb_assert(err_code != NULL);
	fb_assert(err_position != NULL);
	fb_assert(obj != NULL);
	fb_assert(obj->csconvert_convert == CV_wc_to_wc);
	fb_assert(obj->csconvert_datatable != NULL);
	fb_assert(obj->csconvert_misc != NULL);

	const USHORT src_start = src_len;
	*err_code = 0;

/* See if we're only after a length estimate */
	if (dest_ptr == NULL)
		return (src_len);

	const USHORT* const start = dest_ptr;
	while ((src_len > 1) && (dest_len > 1)) {
		const UNICODE uni = *((const UNICODE*) src_ptr);
		const USHORT ch = ((const USHORT*) obj->csconvert_datatable)[
												   ((const USHORT*) obj->
													csconvert_misc)[(USHORT)
																	uni / 256]
												   + (uni % 256)];
		if ((ch == CS_CANT_MAP) && !(uni == CS_CANT_MAP)) {
			*err_code = CS_CONVERT_ERROR;
			break;
		}
		*dest_ptr++ = ch;
		src_ptr++;
		src_len -= 2;
		dest_len -= 2;
	};
	if (src_len && !*err_code) {
		if (src_len == 1)
			*err_code = CS_BAD_INPUT;
		else
			*err_code = CS_TRUNCATION_ERROR;
	};
	*err_position = src_start - src_len;
	return ((dest_ptr - start) * sizeof(*dest_ptr));
}


USHORT CV_nc_to_unicode(CSCONVERT obj,
						BYTE *dest_ptr,
						USHORT dest_len,
						const BYTE* src_ptr,
						USHORT src_len,
						SSHORT *err_code,
						USHORT *err_position)
{
	fb_assert(src_ptr != NULL || dest_ptr == NULL);
	fb_assert(err_code != NULL);
	fb_assert(err_position != NULL);
	fb_assert(obj != NULL);
	fb_assert(obj->csconvert_convert == CV_nc_to_unicode);
	fb_assert(obj->csconvert_datatable != NULL);
	fb_assert(sizeof(UNICODE) == 2);

	const USHORT src_start = src_len;
	*err_code = 0;

/* See if we're only after a length estimate */
	if (dest_ptr == NULL)
		return (src_len * 2);

	const BYTE* const start = dest_ptr;
	while (src_len && (dest_len > 1)) {
		const UNICODE ch = ((const UNICODE*) (obj->csconvert_datatable))[*src_ptr];
		/* No need to check for CS_CONVERT_ERROR, all charsets
		 * must convert to unicode.
		 */

		*((UNICODE *) dest_ptr) = ch;
		src_ptr++;
		src_len--;
		dest_len -= sizeof(UNICODE);
		dest_ptr += sizeof(UNICODE);
	};
	if (src_len && !*err_code) {
		*err_code = CS_TRUNCATION_ERROR;
	};
	*err_position = src_start - src_len;
	return (dest_ptr - start);
}


USHORT CV_wc_copy(CSCONVERT obj,
				  BYTE *dest_ptr,
				  USHORT dest_len,
				  const BYTE* src_ptr,
				  USHORT src_len,
				  SSHORT *err_code,
				  USHORT *err_position)
{
	fb_assert(src_ptr != NULL || dest_ptr == NULL);
	fb_assert(err_code != NULL);
	fb_assert(err_position != NULL);
	fb_assert(obj != NULL);
	fb_assert(obj->csconvert_convert == CV_wc_copy);

	const USHORT src_start = src_len;
	*err_code = 0;

/* See if we're only after a length estimate */
	if (dest_ptr == NULL)
		return (src_len);

	const BYTE* const start = dest_ptr;
	while ((src_len > 1) && (dest_len > 1)) {
		*dest_ptr++ = *src_ptr++;	/* first byte of unicode */
		*dest_ptr++ = *src_ptr++;	/* 2nd   byte of unicode */
		src_len -= 2;
		dest_len -= 2;
	};
	if (src_len && !*err_code) {
		if (src_len == 1)
			*err_code = CS_BAD_INPUT;
		else
			*err_code = CS_TRUNCATION_ERROR;
	};
	*err_position = src_start - src_len;
	return (dest_ptr - start);
}


USHORT eight_bit_convert(CSCONVERT obj,
						 BYTE *dest_ptr,
						 USHORT dest_len,
						 const BYTE* src_ptr,
						 USHORT src_len,
						 SSHORT *err_code,
						 USHORT *err_position)
{
	fb_assert(src_ptr != NULL || dest_ptr == NULL);
	fb_assert(err_code != NULL);
	fb_assert(err_position != NULL);
	fb_assert(obj != NULL);
	fb_assert(obj->csconvert_convert == eight_bit_convert);
	fb_assert(obj->csconvert_datatable != NULL);

	const USHORT src_start = src_len;
	*err_code = 0;

/* See if we're only after a length estimate */
	if (dest_ptr == NULL)
		return (src_len);

	const BYTE* const start = dest_ptr;
	while (src_len && dest_len) {
		const UCHAR ch = obj->csconvert_datatable[*src_ptr];
		if ((ch == CS_CANT_MAP) && (*src_ptr != CS_CANT_MAP)) {
			*err_code = CS_CONVERT_ERROR;
			break;
		};
		*dest_ptr++ = ch;
		src_ptr++;
		src_len--;
		dest_len--;
	};
	if (src_len && !*err_code) {
		*err_code = CS_TRUNCATION_ERROR;
	};
	*err_position = src_start - src_len;
	return (dest_ptr - start);
}



CONVERT_ENTRY(CS_ISO8859_1, CS_DOS_865, CV_dos_865_x_iso8859_1)
{
#include "../intl/conversions/tx865_lat1.h"
	if (dest_cs == CS_ISO8859_1)
		CV_convert_init(csptr, dest_cs, source_cs,
						reinterpret_cast<pfn_INTL_convert>(eight_bit_convert),
						cvt_865_to_iso88591, NULL);
	else
		CV_convert_init(csptr, dest_cs, source_cs,
						reinterpret_cast<pfn_INTL_convert>(eight_bit_convert),
						cvt_iso88591_to_865, NULL);
	CONVERT_RETURN;
}



CONVERT_ENTRY(CS_ISO8859_1, CS_DOS_437, CV_dos_437_x_dos_865)
{
#include "../intl/conversions/tx437_865.h"
	if (dest_cs == CS_DOS_865)
		CV_convert_init(csptr, dest_cs, source_cs,
						reinterpret_cast<pfn_INTL_convert>(eight_bit_convert),
						cvt_437_to_865, NULL);
	else
		CV_convert_init(csptr, dest_cs, source_cs,
						reinterpret_cast<pfn_INTL_convert>(eight_bit_convert),
						cvt_865_to_437, NULL);

	CONVERT_RETURN;
}



CONVERT_ENTRY(CS_ISO8859_1, CS_DOS_437, CV_dos_437_x_iso8859_1)
{
#include "../intl/conversions/tx437_lat1.h"
	if (dest_cs == CS_ISO8859_1)
		CV_convert_init(csptr, dest_cs, source_cs,
						reinterpret_cast<pfn_INTL_convert>(eight_bit_convert),
						cvt_437_to_iso88591, NULL);
	else
		CV_convert_init(csptr, dest_cs, source_cs,
						reinterpret_cast<pfn_INTL_convert>(eight_bit_convert),
						cvt_iso88591_to_437, NULL);

	CONVERT_RETURN;
}

